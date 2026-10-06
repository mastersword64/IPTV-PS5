#include "player.h"
#include "log.h"
#include "threads.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <SDL.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
	const int kOutRate = 48000;
	const size_t kRingSamples = (size_t)kOutRate * 2;      // one second of stereo
	const size_t kMaxFrames = 4;
	const double kAudioLatency = 0.06;                     // sound is heard a little after it is handed over
	const double kStallSeconds = 20.0;                     // give up on a silent connection after this long

	double nowS()
	{
		using namespace std::chrono;
		return duration<double>(steady_clock::now().time_since_epoch()).count();
	}

	std::string errorText(int code)
	{
		char text[128] = "";
		av_strerror(code, text, sizeof(text));
		return text;
	}

	struct QueuedFrame { AVFrame* frame; double pts; };

	struct Session
	{
		std::string url, userAgent, referer;
		std::atomic<bool> abort{ false };
		std::atomic<double> lastIo{ 0 };

		AVFormatContext* fmt = nullptr;
		AVCodecContext* vdec = nullptr;
		AVCodecContext* adec = nullptr;
		int vidx = -1, aidx = -1;
		SwsContext* sws = nullptr;
		SwrContext* swr = nullptr;

		std::mutex m;
		std::condition_variable cv;
		std::deque<AVPacket*> vq, aq;
		size_t queuedBytes = 0;
		bool eof = false;
		int workers = 0;                                    // decoder threads still running

		std::deque<QueuedFrame> frames;
		AVFrame* current = nullptr;                         // the picture on screen

		std::vector<int16_t> ring = std::vector<int16_t>(kRingSamples);
		size_t ringRead = 0, ringCount = 0;
		double ringEndPts = 0;                              // time of the newest sample in the ring
		double audioPts = 0, audioWall = 0;                 // sound clock, as of the last hand-over
		bool audioClock = false;

		double startWall = 0;
		double baseWall = 0, basePts = 0;                   // picture-only clock
		bool baseSet = false;

		// pause and seek (films and episodes)
		std::atomic<bool> paused{ false };
		double seekTo = -1;                                 // a requested position, or -1
		int serial = 0;                                     // goes up at every seek; older work is discarded
		bool videoFlush = false, audioFlush = false;        // the decoders must forget what they hold
		double duration = 0, startTime = 0, position = 0;
		int lastFactor = 0;                                 // the reduction in use, to log when it changes

		// sound tracks and subtitles
		std::vector<player::Track> audioTracks, subtitleTracks;
		int readAidx = -1;                                  // the sound stream being read (aidx is the one being decoded)
		int sidx = -1;                                      // the subtitle stream being collected, or -1
		int wantAudio = -2, wantSubtitle = -2;              // requests from the viewer (-2: none)
		AVCodecContext* pendingAdec = nullptr;              // a new sound decoder waiting for the sound thread to take it
		int pendingAidx = -1;
		struct Cue { double start, end; std::string text; };
		std::deque<Cue> cues;

		player::State state = player::OPENING;
		std::string message;

		~Session()
		{
			for (AVPacket* p : vq) av_packet_free(&p);
			for (AVPacket* p : aq) av_packet_free(&p);
			for (QueuedFrame& f : frames) av_frame_free(&f.frame);
			if (current) av_frame_free(&current);
			if (sws) sws_freeContext(sws);
			if (swr) swr_free(&swr);
			if (vdec) avcodec_free_context(&vdec);
			if (adec) avcodec_free_context(&adec);
			if (pendingAdec) avcodec_free_context(&pendingAdec);
			if (fmt) avformat_close_input(&fmt);
		}

		void fail(const std::string& why)
		{
			std::lock_guard<std::mutex> lock(m);
			state = player::FAILED;
			message = why;
			eof = true;
			cv.notify_all();
			logLine("player: failed: %s", why.c_str());
		}
	};

	typedef std::shared_ptr<Session> SessionPtr;

	std::mutex g_currentMutex;
	SessionPtr g_current;
	SDL_AudioDeviceID g_device = 0;

	SessionPtr current()
	{
		std::lock_guard<std::mutex> lock(g_currentMutex);
		return g_current;
	}

	int onInterrupt(void* opaque)
	{
		Session* s = static_cast<Session*>(opaque);
		return s->abort || nowS() - s->lastIo.load() > kStallSeconds;
	}

	std::atomic<int> g_warningLines{ 0 };

	void onAvLog(void*, int level, const char* format, va_list arguments)
	{
		// errors always; warnings (which carry the server's exact answer when a stream is refused)
		// only for the first few lines of each stream, so playback problems do not flood the log
		if (level > AV_LOG_WARNING || (level > AV_LOG_ERROR && g_warningLines.fetch_add(1) >= 25))
			return;
		char text[400];
		vsnprintf(text, sizeof(text), format, arguments);
		size_t length = strlen(text);
		while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r'))
			text[--length] = 0;
		if (length > 0)
			logLine("ffmpeg: %s", text);
	}

	std::atomic<int> g_displayW{ 1920 }, g_displayH{ 1080 };
	std::atomic<int> g_shown{ 0 }, g_dropped{ 0 }, g_decoded{ 0 };

	std::string metadata(const AVStream* stream, const char* key)
	{
		const AVDictionaryEntry* entry = av_dict_get(stream->metadata, key, nullptr, 0);
		return entry && entry->value ? entry->value : "";
	}

	// "English  ·  5.1  ·  eac3"
	std::string trackLabel(const AVStream* stream, int number, bool sound)
	{
		static const char* const known[][2] = {
			{ "eng", "English" }, { "fre", "French" }, { "fra", "French" }, { "ger", "German" }, { "deu", "German" },
			{ "spa", "Spanish" }, { "ita", "Italian" }, { "por", "Portuguese" }, { "rus", "Russian" }, { "ara", "Arabic" },
			{ "tur", "Turkish" }, { "hin", "Hindi" }, { "urd", "Urdu" }, { "jpn", "Japanese" }, { "kor", "Korean" },
			{ "chi", "Chinese" }, { "zho", "Chinese" }, { "dut", "Dutch" }, { "nld", "Dutch" }, { "pol", "Polish" },
			{ "swe", "Swedish" }, { "nor", "Norwegian" }, { "dan", "Danish" }, { "fin", "Finnish" }, { "gre", "Greek" },
			{ "ell", "Greek" }, { "heb", "Hebrew" }, { "per", "Persian" }, { "fas", "Persian" }, { "ukr", "Ukrainian" },
			{ "rum", "Romanian" }, { "ron", "Romanian" }, { "hun", "Hungarian" }, { "cze", "Czech" }, { "ces", "Czech" },
			{ "tam", "Tamil" }, { "tel", "Telugu" }, { "ben", "Bengali" }, { "pan", "Punjabi" }, { "und", "" } };
		std::string language = metadata(stream, "language");
		for (const auto& pair : known)
			if (language == pair[0])
				language = pair[1];
		std::string label = language.empty() ? "Track " + std::to_string(number) : language;
		const std::string title = metadata(stream, "title");
		if (!title.empty() && title != language && title.size() < 40)
			label += "  \xC2\xB7  " + title;
		if (sound)
		{
			const int channels = stream->codecpar->ch_layout.nb_channels;
			label += std::string("  \xC2\xB7  ") + (channels == 1 ? "Mono" : channels == 2 ? "Stereo" : channels == 6 ? "5.1" : channels == 8 ? "7.1" : "Surround");
			label += std::string("  \xC2\xB7  ") + avcodec_get_name(stream->codecpar->codec_id);
		}
		return label;
	}

	bool isTextSubtitle(AVCodecID id)
	{
		return id == AV_CODEC_ID_SUBRIP || id == AV_CODEC_ID_ASS || id == AV_CODEC_ID_SSA || id == AV_CODEC_ID_TEXT
			|| id == AV_CODEC_ID_MOV_TEXT || id == AV_CODEC_ID_WEBVTT;
	}

	// The words of one subtitle, from the stream as stored. Text subtitles need no decoder: the
	// styling marks are dropped and the words kept.
	std::string cueText(AVCodecID id, const uint8_t* data, int size)
	{
		if (!data || size <= 0)
			return "";
		std::string raw((const char*)data, (size_t)size);
		if (id == AV_CODEC_ID_MOV_TEXT)
		{
			if (size < 2)
				return "";
			const size_t length = ((size_t)data[0] << 8) | data[1];
			raw = raw.substr(2, length < raw.size() - 2 ? length : raw.size() - 2);
		}
		else if (id == AV_CODEC_ID_ASS || id == AV_CODEC_ID_SSA)
		{
			// "order,layer,style,name,margins...,effect,words": the words follow the eighth comma
			size_t at = 0;
			for (int commas = 0; commas < 8 && at != std::string::npos; commas++)
			{
				at = raw.find(',', at);
				if (at != std::string::npos)
					at++;
			}
			if (at == std::string::npos)
				return "";
			raw = raw.substr(at);
		}
		std::string out;
		for (size_t i = 0; i < raw.size(); i++)
		{
			const char c = raw[i];
			if (c == '{' || c == '<')
			{
				// {\an8} and <i> style marks (a lone "<" in a sentence has no close nearby and is kept)
				const size_t close = raw.find(c == '{' ? '}' : '>', i);
				const bool mark = close != std::string::npos && close - i < 80 && close > i + 1 && raw[i + 1] != ' ';
				if (mark)
				{
					i = close;
					continue;
				}
			}
			if (c == '\\' && i + 1 < raw.size() && (raw[i + 1] == 'N' || raw[i + 1] == 'n'))
			{
				out += '\n';
				i++;
			}
			else if (c == '\\' && i + 1 < raw.size() && raw[i + 1] == 'h')
			{
				out += ' ';
				i++;
			}
			else if (c == '\r' || c == '\0')
				continue;
			else
				out += c;
		}
		while (!out.empty() && (out.back() == '\n' || out.back() == ' '))
			out.pop_back();
		size_t start = 0;
		while (start < out.size() && (out[start] == '\n' || out[start] == ' '))
			start++;
		return out.substr(start);
	}

	std::atomic<int> g_pictureBudget{ 3200000 };      // the most bytes a picture should be when handed to the graphics chip

	// One plane, reduced. `factor` is in tenths: 10 same size, 15 two-thirds, 20 half, 30 a third,
	// 40 a quarter. Every output dot is the weighted average of the source dots it covers, so nothing
	// sparkles and nothing is skipped. `shift` is 2 for 10-bit sources (which also brings them to 8 bits).
	template <typename Sample>
	void reducePlane(const uint8_t* source, ptrdiff_t sourceStride, int sw, int sh, uint8_t* to, ptrdiff_t toStride, int dw, int dh, int factor, int shift)
	{
		const auto row = [&](int y) { return (const Sample*)(source + (ptrdiff_t)(y < sh ? y : sh - 1) * sourceStride); };
		if (factor == 15)
		{
			// three source dots become two: weights 2:1 and 1:2, across and down
			for (int y = 0; y < dh; y++)
			{
				const int by = (y / 2) * 3, oy = y % 2;
				const Sample* r0 = row(by + oy);
				const Sample* r1 = row(by + oy + 1);
				const unsigned wy0 = oy ? 1 : 2, wy1 = oy ? 2 : 1;
				uint8_t* out = to + (ptrdiff_t)y * toStride;
				for (int x = 0; x < dw; x++)
				{
					const int bx = (x / 2) * 3 + (x % 2);
					const int x0 = bx < sw ? bx : sw - 1, x1 = bx + 1 < sw ? bx + 1 : sw - 1;
					const unsigned wx0 = x % 2 ? 1 : 2, wx1 = x % 2 ? 2 : 1;
					const unsigned sum = wy0 * (wx0 * r0[x0] + wx1 * r0[x1]) + wy1 * (wx0 * r1[x0] + wx1 * r1[x1]);
					const unsigned value = (sum + ((9u << shift) >> 1)) / (9u << shift);
					out[x] = (uint8_t)(value > 255 ? 255 : value);
				}
			}
			return;
		}
		const int n = factor / 10;                         // whole-number reduction: n by n dots become one
		const unsigned total = (unsigned)(n * n) << shift;
		for (int y = 0; y < dh; y++)
		{
			uint8_t* out = to + (ptrdiff_t)y * toStride;
			const Sample* rows[4];
			for (int k = 0; k < n; k++)
				rows[k] = row(y * n + k);
			const int whole = sw / n < dw ? sw / n : dw;   // columns that lie fully inside the source
			if (n == 2)
			{
				// the common case (4K on a 1080p screen), written plainly so it runs fast
				const Sample* a = rows[0];
				const Sample* b = rows[1];
				for (int x = 0; x < whole; x++)
				{
					const unsigned value = ((unsigned)a[x * 2] + a[x * 2 + 1] + b[x * 2] + b[x * 2 + 1] + (2u << shift)) >> (2 + shift);
					out[x] = (uint8_t)(value > 255 ? 255 : value);
				}
			}
			else
			for (int x = 0; x < whole; x++)
			{
				unsigned sum = 0;
				for (int k = 0; k < n; k++)
					for (int j = 0; j < n; j++)
						sum += rows[k][x * n + j];
				const unsigned value = (sum + (total >> 1)) / total;
				out[x] = (uint8_t)(value > 255 ? 255 : value);
			}
			for (int x = whole; x < dw; x++)
			{
				unsigned sum = 0;
				for (int k = 0; k < n; k++)
					for (int j = 0; j < n; j++)
						sum += rows[k][x * n + j < sw ? x * n + j : sw - 1];
				const unsigned value = (sum + (total >> 1)) / total;
				out[x] = (uint8_t)(value > 255 ? 255 : value);
			}
		}
	}

	// How much to reduce a picture: enough to fit the screen, and enough to fit the byte budget
	// (which reflects how quickly this console's graphics layer accepts pictures).
	int chooseFactor(int width, int height)
	{
		static const int options[5] = { 10, 15, 20, 30, 40 };
		const long long budget = g_pictureBudget.load();
		for (const int factor : options)
		{
			const long long w = (long long)width * 10 / factor, h = (long long)height * 10 / factor;
			const bool fitsScreen = w <= (long long)g_displayW.load() * 11 / 10 && h <= (long long)g_displayH.load() * 11 / 10;
			if (fitsScreen && w * h * 3 / 2 <= budget)
				return factor;
		}
		return 40;
	}

	// Turns a decoded picture into the plain 8-bit form the screen is drawn from, reduced by `factor`.
	// Done here, on the decoding side, so the drawing side is handed as little as it needs.
	// Returns nullptr for formats it does not handle.
	AVFrame* prepareFrame(const AVFrame* in, int factor)
	{
		const bool deep = in->format == AV_PIX_FMT_YUV420P10LE;
		if (!deep && in->format != AV_PIX_FMT_YUV420P && in->format != AV_PIX_FMT_YUVJ420P)
			return nullptr;
		AVFrame* out = av_frame_alloc();
		if (!out)
			return nullptr;
		out->format = in->format == AV_PIX_FMT_YUVJ420P ? AV_PIX_FMT_YUVJ420P : AV_PIX_FMT_YUV420P;
		out->width = (in->width * 10 / factor) & ~1;
		out->height = (in->height * 10 / factor) & ~1;
		if (out->width < 2 || out->height < 2 || av_frame_get_buffer(out, 32) < 0)
		{
			av_frame_free(&out);
			return nullptr;
		}
		av_frame_copy_props(out, in);
		for (int plane = 0; plane < 3; plane++)
		{
			const int sw = plane ? (in->width + 1) / 2 : in->width, sh = plane ? (in->height + 1) / 2 : in->height;
			const int dw = plane ? out->width / 2 : out->width, dh = plane ? out->height / 2 : out->height;
			if (deep)
				reducePlane<uint16_t>(in->data[plane], in->linesize[plane], sw, sh, out->data[plane], out->linesize[plane], dw, dh, factor, 2);
			else
				reducePlane<uint8_t>(in->data[plane], in->linesize[plane], sw, sh, out->data[plane], out->linesize[plane], dw, dh, factor, 0);
		}
		return out;
	}

	AVCodecContext* openDecoder(AVFormatContext* fmt, int index, int threads)
	{
		AVStream* stream = fmt->streams[index];
		const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
		if (!codec)
		{
			logLine("player: no decoder for %s", avcodec_get_name(stream->codecpar->codec_id));
			return nullptr;
		}
		AVCodecContext* ctx = avcodec_alloc_context3(codec);
		if (!ctx)
			return nullptr;
		if (avcodec_parameters_to_context(ctx, stream->codecpar) < 0)
		{
			avcodec_free_context(&ctx);
			return nullptr;
		}
		ctx->pkt_timebase = stream->time_base;
		ctx->thread_count = threads;
		if (avcodec_open2(ctx, codec, nullptr) < 0)
		{
			avcodec_free_context(&ctx);
			return nullptr;
		}
		return ctx;
	}

	// Takes the next packet of one kind. Returns false when the stream is over or stopped.
	bool takePacket(Session& s, std::deque<AVPacket*>& queue, AVPacket*& packet, bool& flushWanted, bool& flush, int& serial)
	{
		std::unique_lock<std::mutex> lock(s.m);
		for (;;)
		{
			if (s.abort)
				return false;
			if (!queue.empty())
			{
				flush = flushWanted;
				flushWanted = false;
				serial = s.serial;
				packet = queue.front();
				queue.pop_front();
				s.queuedBytes -= (size_t)packet->size;
				s.cv.notify_all();
				return true;
			}
			if (s.eof)
			{
				flush = false;
				serial = s.serial;
				packet = nullptr; // tells the decoder to hand out what it still holds
				return true;
			}
			s.cv.wait_for(lock, std::chrono::milliseconds(50));
		}
	}

	void workerDone(Session& s)
	{
		std::lock_guard<std::mutex> lock(s.m);
		s.workers--;
		s.cv.notify_all();
	}

	void videoThread(SessionPtr s)
	{
		AVFrame* frame = av_frame_alloc();
		const AVStream* stream = s->fmt->streams[s->vidx];
		const double tb = av_q2d(stream->time_base);
		const double step = stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0 ? 1.0 / av_q2d(stream->avg_frame_rate) : 0.04;
		double last = 0;
		bool finished = false;

		while (!finished && !s->abort)
		{
			AVPacket* packet = nullptr;
			bool flush = false;
			int serial = 0;
			if (!takePacket(*s, s->vq, packet, s->videoFlush, flush, serial))
				break;
			if (flush)
				avcodec_flush_buffers(s->vdec);           // after a seek
			avcodec_send_packet(s->vdec, packet);
			if (packet)
				av_packet_free(&packet);
			else
				finished = true;

			while (!s->abort && avcodec_receive_frame(s->vdec, frame) == 0)
			{
				const double pts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? (double)frame->best_effort_timestamp * tb : last + step;
				last = pts;

				g_decoded++;
				// reduced to fit the screen and what the graphics layer can take in time
				const int factor = chooseFactor(frame->width, frame->height);
				if (factor != s->lastFactor)
				{
					s->lastFactor = factor;
					logLine("player: pictures are %dx%d, shown from %dx%d", frame->width, frame->height,
						(frame->width * 10 / factor) & ~1, (frame->height * 10 / factor) & ~1);
				}
				AVFrame* out = nullptr;
				if (factor == 10 && (frame->format == AV_PIX_FMT_YUV420P || frame->format == AV_PIX_FMT_YUVJ420P))
				{
					out = av_frame_alloc();
					if (out)
						av_frame_move_ref(out, frame);
				}
				else
					out = prepareFrame(frame, factor);
				if (!out && frame->width > 0)
				{
					// an unusual format (4:2:2, 4:4:4, ...): the general-purpose converter handles it
					out = av_frame_alloc();
					s->sws = sws_getCachedContext(s->sws, frame->width, frame->height, (AVPixelFormat)frame->format,
						frame->width, frame->height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
					if (out)
					{
						out->format = AV_PIX_FMT_YUV420P;
						out->width = frame->width;
						out->height = frame->height;
					}
					if (!out || !s->sws || av_frame_get_buffer(out, 32) < 0)
					{
						av_frame_free(&out);
						av_frame_unref(frame);
						continue;
					}
					sws_scale(s->sws, (const uint8_t* const*)frame->data, frame->linesize, 0, frame->height, out->data, out->linesize);
					av_frame_copy_props(out, frame);
				}
				av_frame_unref(frame);
				if (!out)
					continue;

				std::unique_lock<std::mutex> lock(s->m);
				while (!s->abort && serial == s->serial && s->frames.size() >= kMaxFrames)
					s->cv.wait_for(lock, std::chrono::milliseconds(20));
				if (s->abort)
				{
					lock.unlock();
					av_frame_free(&out);
					break;
				}
				if (serial != s->serial)
				{
					lock.unlock();
					av_frame_free(&out);                  // decoded from before a seek
					continue;
				}
				s->frames.push_back({ out, pts });
			}
		}
		av_frame_free(&frame);
		workerDone(*s);
	}

	void audioThread(SessionPtr s)
	{
		AVFrame* frame = av_frame_alloc();
		double tb = av_q2d(s->fmt->streams[s->aidx]->time_base);
		AVChannelLayout outLayout, inLayout;
		av_channel_layout_default(&outLayout, 2);
		memset(&inLayout, 0, sizeof(inLayout));
		int inRate = 0, inFormat = -1;
		std::vector<int16_t> samples;
		double endPts = 0;
		bool finished = false;

		while (!finished && !s->abort)
		{
			AVPacket* packet = nullptr;
			bool flush = false;
			int serial = 0;
			if (!takePacket(*s, s->aq, packet, s->audioFlush, flush, serial))
				break;
			{
				// the viewer chose another sound track: its decoder replaces the old one here
				AVCodecContext* old = nullptr;
				{
					std::lock_guard<std::mutex> lock(s->m);
					if (s->pendingAdec)
					{
						old = s->adec;
						s->adec = s->pendingAdec;
						s->aidx = s->pendingAidx;
						s->pendingAdec = nullptr;
						flush = false;
					}
				}
				if (old)
				{
					avcodec_free_context(&old);
					tb = av_q2d(s->fmt->streams[s->aidx]->time_base);
				}
			}
			if (flush)
				avcodec_flush_buffers(s->adec);           // after a seek
			avcodec_send_packet(s->adec, packet);
			if (packet)
				av_packet_free(&packet);
			else
				finished = true;

			while (!s->abort && avcodec_receive_frame(s->adec, frame) == 0)
			{
				if (frame->nb_samples <= 0 || frame->sample_rate <= 0)
				{
					av_frame_unref(frame);
					continue;
				}
				AVChannelLayout layout;
				memset(&layout, 0, sizeof(layout));
				if (frame->ch_layout.nb_channels > 0 && frame->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC)
					av_channel_layout_copy(&layout, &frame->ch_layout);
				else
					av_channel_layout_default(&layout, frame->ch_layout.nb_channels > 0 ? frame->ch_layout.nb_channels : 2);

				if (!s->swr || inRate != frame->sample_rate || inFormat != frame->format || av_channel_layout_compare(&layout, &inLayout) != 0)
				{
					if (s->swr)
						swr_free(&s->swr);
					av_channel_layout_uninit(&inLayout);
					av_channel_layout_copy(&inLayout, &layout);
					inRate = frame->sample_rate;
					inFormat = frame->format;
					if (swr_alloc_set_opts2(&s->swr, &outLayout, AV_SAMPLE_FMT_S16, kOutRate, &inLayout, (AVSampleFormat)inFormat, inRate, 0, nullptr) < 0
						|| swr_init(s->swr) < 0)
					{
						if (s->swr)
							swr_free(&s->swr);
						logLine("player: this sound format could not be converted");
					}
				}
				av_channel_layout_uninit(&layout);
				if (!s->swr)
				{
					av_frame_unref(frame);
					continue;
				}

				const int capacity = (int)av_rescale_rnd(swr_get_delay(s->swr, inRate) + frame->nb_samples, kOutRate, inRate, AV_ROUND_UP) + 64;
				samples.resize((size_t)capacity * 2);
				uint8_t* outPlanes[1] = { (uint8_t*)samples.data() };
				const int made = swr_convert(s->swr, outPlanes, capacity, (const uint8_t**)frame->extended_data, frame->nb_samples);
				if (frame->pts != AV_NOPTS_VALUE)
					endPts = (double)frame->pts * tb + (double)frame->nb_samples / (double)inRate;
				else if (made > 0)
					endPts += (double)made / (double)kOutRate;
				av_frame_unref(frame);
				if (made <= 0)
					continue;

				const size_t count = (size_t)made * 2;
				std::unique_lock<std::mutex> lock(s->m);
				while (!s->abort && serial == s->serial && s->ringCount + count > s->ring.size())
					s->cv.wait_for(lock, std::chrono::milliseconds(20));
				if (s->abort)
					break;
				if (serial != s->serial)
					continue;                             // decoded from before a seek
				size_t write = (s->ringRead + s->ringCount) % s->ring.size();
				for (size_t i = 0; i < count; i++)
				{
					s->ring[write] = samples[i];
					if (++write == s->ring.size())
						write = 0;
				}
				s->ringCount += count;
				s->ringEndPts = endPts;
			}
		}
		av_channel_layout_uninit(&inLayout);
		av_channel_layout_uninit(&outLayout);
		av_frame_free(&frame);
		workerDone(*s);
	}

	void readThread(SessionPtr s)
	{
		s->lastIo = nowS();
		logLine("player: opening %s", s->url.c_str());

		AVDictionary* options = nullptr;
		av_dict_set(&options, "user_agent", s->userAgent.empty() ? "VLC/3.0.20 LibVLC/3.0.20" : s->userAgent.c_str(), 0);
		if (!s->referer.empty())
			av_dict_set(&options, "referer", s->referer.c_str(), 0);
		av_dict_set(&options, "reconnect", "1", 0);
		av_dict_set(&options, "reconnect_streamed", "1", 0);
		av_dict_set(&options, "reconnect_delay_max", "4", 0);
		av_dict_set(&options, "rw_timeout", "15000000", 0);

		s->fmt = avformat_alloc_context();
		if (!s->fmt)
		{
			av_dict_free(&options);
			s->fail("Out of memory");
			return;
		}
		s->fmt->interrupt_callback.callback = onInterrupt;
		s->fmt->interrupt_callback.opaque = s.get();
		s->fmt->max_analyze_duration = 3 * AV_TIME_BASE;

		int result = avformat_open_input(&s->fmt, s->url.c_str(), nullptr, &options);
		av_dict_free(&options);
		if (result < 0)
		{
			s->fmt = nullptr; // already freed by FFmpeg
			if (s->abort)
				return;
			if (s->url.compare(0, 8, "https://") == 0 && result == AVERROR_PROTOCOL_NOT_FOUND)
				s->fail("Secure (https) streams are not supported yet");
			else
				s->fail("Could not open the stream: " + errorText(result));
			return;
		}
		s->lastIo = nowS();
		result = avformat_find_stream_info(s->fmt, nullptr);
		if (result < 0)
		{
			if (!s->abort)
				s->fail("Could not read the stream: " + errorText(result));
			return;
		}

		s->vidx = av_find_best_stream(s->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		s->aidx = av_find_best_stream(s->fmt, AVMEDIA_TYPE_AUDIO, -1, s->vidx, nullptr, 0);
		AVCodecContext* vdec = s->vidx >= 0 ? openDecoder(s->fmt, s->vidx, 8) : nullptr;
		AVCodecContext* adec = s->aidx >= 0 ? openDecoder(s->fmt, s->aidx, 1) : nullptr;

		// The sound tracks that can be played, and the text subtitles, for the viewer to choose from.
		std::vector<player::Track> sounds, subtitles;
		for (unsigned i = 0; i < s->fmt->nb_streams; i++)
		{
			const AVStream* stream = s->fmt->streams[i];
			if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
			{
				if (avcodec_find_decoder(stream->codecpar->codec_id))
					sounds.push_back({ (int)i, trackLabel(stream, (int)sounds.size() + 1, true) });
				else
					logLine("player: a sound track in %s cannot be played by this build", avcodec_get_name(stream->codecpar->codec_id));
			}
			else if (stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
			{
				if (isTextSubtitle(stream->codecpar->codec_id))
					subtitles.push_back({ (int)i, trackLabel(stream, (int)subtitles.size() + 1, false) });
				else
					logLine("player: subtitles in %s (pictures, not text) are not supported", avcodec_get_name(stream->codecpar->codec_id));
			}
		}
		if (!adec)
		{
			// the stream's first choice of sound cannot be played: use the first track that can
			for (const player::Track& track : sounds)
			{
				adec = openDecoder(s->fmt, track.id, 1);
				if (adec)
				{
					s->aidx = track.id;
					break;
				}
			}
		}
		logLine("player: %d sound track%s, %d subtitle track%s", (int)sounds.size(), sounds.size() == 1 ? "" : "s", (int)subtitles.size(), subtitles.size() == 1 ? "" : "s");
		if (!vdec && !adec)
		{
			s->fail("Nothing playable in this stream (its picture and sound formats are not supported)");
			return;
		}
		for (unsigned i = 0; i < s->fmt->nb_streams; i++)
		{
			const bool wanted = (vdec && (int)i == s->vidx) || (adec && (int)i == s->aidx);
			s->fmt->streams[i]->discard = wanted ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
		}
		if (vdec)
			logLine("player: picture %s %dx%d, %s, %d decoding threads", avcodec_get_name(vdec->codec_id), vdec->width, vdec->height,
				av_get_pix_fmt_name(vdec->pix_fmt) ? av_get_pix_fmt_name(vdec->pix_fmt) : "?", vdec->thread_count);
		if (adec)
			logLine("player: sound %s %d Hz", avcodec_get_name(adec->codec_id), adec->sample_rate);

		{
			std::lock_guard<std::mutex> lock(s->m);
			s->audioTracks = sounds;
			s->subtitleTracks = subtitles;
			s->readAidx = adec ? s->aidx : -1;
			s->vdec = vdec;
			s->adec = adec;
			s->startWall = nowS();
			s->state = player::PLAYING;
			s->workers = (vdec ? 1 : 0) + (adec ? 1 : 0);
		}
		if (vdec)
			startThread("video", [s] { videoThread(s); });
		if (adec)
			startThread("audio", [s] { audioThread(s); });

		{
			std::lock_guard<std::mutex> lock(s->m);
			s->duration = s->fmt->duration > 0 ? (double)s->fmt->duration / AV_TIME_BASE : 0;
			s->startTime = s->fmt->start_time != AV_NOPTS_VALUE ? (double)s->fmt->start_time / AV_TIME_BASE : 0;
		}
		if (s->duration > 0)
			logLine("player: length %.0f s", s->duration);

		AVPacket* packet = av_packet_alloc();
		while (!s->abort && packet)
		{
			double target = -1;
			int wantAudio, wantSubtitle;
			{
				std::lock_guard<std::mutex> lock(s->m);
				target = s->seekTo;
				s->seekTo = -1;
				wantAudio = s->wantAudio;
				wantSubtitle = s->wantSubtitle;
				s->wantAudio = s->wantSubtitle = -2;
			}
			if (wantAudio >= 0 && adec && wantAudio != s->readAidx && wantAudio < (int)s->fmt->nb_streams)
			{
				AVCodecContext* fresh = openDecoder(s->fmt, wantAudio, 1);
				if (fresh)
				{
					std::lock_guard<std::mutex> lock(s->m);
					s->fmt->streams[s->readAidx]->discard = AVDISCARD_ALL;
					s->fmt->streams[wantAudio]->discard = AVDISCARD_DEFAULT;
					s->readAidx = wantAudio;
					if (s->pendingAdec)
						avcodec_free_context(&s->pendingAdec);
					s->pendingAdec = fresh;
					s->pendingAidx = wantAudio;
					for (AVPacket* old : s->aq)
					{
						s->queuedBytes -= (size_t)old->size;
						av_packet_free(&old);
					}
					s->aq.clear();
					s->ringCount = 0;
					s->ringRead = 0;
					s->audioClock = false;
					// a film restarts from where it is, so the new sound comes in at once and in step
					if (s->duration > 0 && target < 0)
						target = s->position;
					logLine("player: sound track changed");
				}
			}
			if (wantSubtitle >= -1 && wantSubtitle != s->sidx && wantSubtitle < (int)s->fmt->nb_streams)
			{
				std::lock_guard<std::mutex> lock(s->m);
				if (s->sidx >= 0)
					s->fmt->streams[s->sidx]->discard = AVDISCARD_ALL;
				s->sidx = wantSubtitle;
				if (s->sidx >= 0)
					s->fmt->streams[s->sidx]->discard = AVDISCARD_DEFAULT;
				s->cues.clear();
				if (s->duration > 0 && target < 0 && s->sidx >= 0)
					target = s->position;
				logLine("player: subtitles %s", s->sidx >= 0 ? "on" : "off");
			}
			if (target >= 0)
			{
				s->lastIo = nowS();
				const int moved = av_seek_frame(s->fmt, -1, (int64_t)((target + s->startTime) * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
				std::lock_guard<std::mutex> lock(s->m);
				if (moved >= 0)
				{
					// everything read or decoded so far belongs to the old position
					for (AVPacket* old : s->vq) av_packet_free(&old);
					for (AVPacket* old : s->aq) av_packet_free(&old);
					s->vq.clear();
					s->aq.clear();
					s->queuedBytes = 0;
					for (QueuedFrame& old : s->frames) av_frame_free(&old.frame);
					s->frames.clear();
					s->ringCount = 0;
					s->ringRead = 0;
					s->cues.clear();
					s->serial++;
					s->videoFlush = s->audioFlush = true;
					s->audioClock = false;
					s->baseSet = false;
					s->startWall = nowS();
				}
				else
					logLine("player: could not move to %.0f s: %s", target, errorText(moved).c_str());
				s->cv.notify_all();
				continue;
			}
			{
				// Do not read further ahead than the decoders can use.
				std::unique_lock<std::mutex> lock(s->m);
				const bool full = s->queuedBytes > 48u * 1024 * 1024
					|| (s->vq.size() > 200 && (!adec || s->aq.size() > 10))
					|| (s->aq.size() > 400 && (!vdec || s->vq.size() > 10));
				if (full)
				{
					s->cv.wait_for(lock, std::chrono::milliseconds(20));
					continue;
				}
			}
			s->lastIo = nowS();
			result = av_read_frame(s->fmt, packet);
			if (result == AVERROR(EAGAIN))
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}
			if (result < 0)
			{
				if (!s->abort)
					logLine("player: the stream stopped: %s", errorText(result).c_str());
				break;
			}
			std::deque<AVPacket*>* queue = nullptr;
			if (vdec && packet->stream_index == s->vidx)
				queue = &s->vq;
			else if (adec && packet->stream_index == s->readAidx)
				queue = &s->aq;
			else if (packet->stream_index == s->sidx && s->sidx >= 0)
			{
				// a subtitle: kept with its times until the picture reaches it
				const AVStream* stream = s->fmt->streams[s->sidx];
				const std::string text = cueText(stream->codecpar->codec_id, packet->data, packet->size);
				if (!text.empty() && packet->pts != AV_NOPTS_VALUE)
				{
					const double tb = av_q2d(stream->time_base);
					const double start = (double)packet->pts * tb;
					const double length = packet->duration > 0 ? (double)packet->duration * tb : 4.0;
					std::lock_guard<std::mutex> lock(s->m);
					s->cues.push_back({ start, start + (length > 12 ? 12 : length), text });
					if (s->cues.size() > 400)
						s->cues.pop_front();
				}
			}
			if (!queue)
			{
				av_packet_unref(packet);
				continue;
			}
			AVPacket* kept = av_packet_alloc();
			if (!kept)
			{
				av_packet_unref(packet);
				continue;
			}
			av_packet_move_ref(kept, packet);
			std::lock_guard<std::mutex> lock(s->m);
			s->queuedBytes += (size_t)kept->size;
			queue->push_back(kept);
			s->cv.notify_all();
		}
		if (packet)
			av_packet_free(&packet);

		std::unique_lock<std::mutex> lock(s->m);
		s->eof = true;
		s->cv.notify_all();
		// The decoders use the opened stream; wait for them before letting go of it.
		while (s->workers > 0)
			s->cv.wait_for(lock, std::chrono::milliseconds(50));
	}

	void onAudio(void*, Uint8* stream, int length)
	{
		memset(stream, 0, (size_t)length);
		const SessionPtr s = current();
		if (!s)
			return;
		if (s->paused)
			return;
		std::lock_guard<std::mutex> lock(s->m);
		size_t count = (size_t)length / sizeof(int16_t);
		if (count > s->ringCount)
			count = s->ringCount;
		if (count == 0)
			return;
		int16_t* out = (int16_t*)stream;
		for (size_t i = 0; i < count; i++)
		{
			out[i] = s->ring[s->ringRead];
			if (++s->ringRead == s->ring.size())
				s->ringRead = 0;
		}
		s->ringCount -= count;
		s->audioPts = s->ringEndPts - (double)(s->ringCount / 2) / (double)kOutRate;
		s->audioWall = nowS();
		s->audioClock = true;
		if (!s->vdec)
			s->position = s->audioPts - s->startTime;
		s->cv.notify_all();
	}
}

void player::init()
{
	avformat_network_init();
	av_log_set_level(AV_LOG_WARNING);
	av_log_set_callback(onAvLog);

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
		logLine("player: SDL audio did not start: %s", SDL_GetError());
	SDL_AudioSpec wanted, got;
	SDL_zero(wanted);
	SDL_zero(got);
	wanted.freq = kOutRate;
	wanted.format = AUDIO_S16SYS;
	wanted.channels = 2;
	wanted.samples = 1024;
	wanted.callback = onAudio;
	g_device = SDL_OpenAudioDevice(nullptr, 0, &wanted, &got, 0);
	if (g_device == 0)
		logLine("player: no sound device: %s", SDL_GetError());
	else
	{
		logLine("player: sound device open: %d Hz, %d channels, format 0x%x, blocks of %d", got.freq, (int)got.channels, (unsigned)got.format, (int)got.samples);
		SDL_PauseAudioDevice(g_device, 0);
	}
}

void player::play(const std::string& url, const std::string& userAgent, const std::string& referer)
{
	stop();
	g_warningLines = 0;
	SessionPtr s = std::make_shared<Session>();
	s->url = url;
	s->userAgent = userAgent;
	s->referer = referer;
	{
		std::lock_guard<std::mutex> lock(g_currentMutex);
		g_current = s;
	}
	startThread("reader", [s] { readThread(s); });
}

void player::stop()
{
	SessionPtr s;
	{
		std::lock_guard<std::mutex> lock(g_currentMutex);
		s.swap(g_current);
	}
	if (!s)
		return;
	// The background threads notice this, finish on their own, and free the stream.
	s->abort = true;
	std::lock_guard<std::mutex> lock(s->m);
	s->cv.notify_all();
}

player::State player::state()
{
	const SessionPtr s = current();
	if (!s)
		return IDLE;
	std::lock_guard<std::mutex> lock(s->m);
	if (s->state == PLAYING && s->eof && s->workers == 0 && s->frames.empty() && s->ringCount == 0)
		return ENDED;
	return s->state;
}

std::string player::message()
{
	const SessionPtr s = current();
	if (!s)
		return "";
	std::lock_guard<std::mutex> lock(s->m);
	return s->message;
}

bool player::hasVideo()
{
	const SessionPtr s = current();
	if (!s)
		return false;
	std::lock_guard<std::mutex> lock(s->m);
	return s->vdec != nullptr;
}

bool player::nextPicture(Picture& out)
{
	const SessionPtr s = current();
	if (!s)
		return false;
	std::lock_guard<std::mutex> lock(s->m);
	if (s->frames.empty() || s->paused)
		return false;

	const double now = nowS();
	const double front = s->frames.front().pts;
	double clock;
	bool wall = false;
	if (s->adec && s->audioClock && now - s->audioWall < 1.0)
	{
		double since = now - s->audioWall;
		if (since > 0.1)
			since = 0.1;
		clock = s->audioPts - kAudioLatency + since;
		s->baseSet = false;
	}
	else if (s->adec && !s->audioClock && !s->eof && now - s->startWall < 3.0)
		return false; // sound is about to start: wait for it so the two line up
	else
	{
		wall = true;
		if (!s->baseSet)
		{
			s->basePts = front;
			s->baseWall = now;
			s->baseSet = true;
		}
		clock = s->basePts + (now - s->baseWall);
	}

	bool take;
	const double ahead = front - clock;
	if (ahead > 3.0 || ahead < -3.0)
	{
		// The stream's timestamps jumped: show this picture now and carry on from it.
		if (wall)
		{
			s->basePts = front;
			s->baseWall = now;
		}
		take = true;
	}
	else
	{
		while (s->frames.size() > 1 && s->frames[1].pts <= clock)
		{
			av_frame_free(&s->frames.front().frame); // too late to show
			s->frames.pop_front();
			g_dropped++;
		}
		take = s->frames.front().pts <= clock;
	}
	if (!take)
		return false;

	if (s->current)
		av_frame_free(&s->current);
	s->current = s->frames.front().frame;
	s->frames.pop_front();
	s->cv.notify_all();

	s->position = front - s->startTime;
	const AVFrame* f = s->current;
	for (int i = 0; i < 3; i++)
	{
		out.planes[i] = f->data[i];
		out.linesize[i] = f->linesize[i];
	}
	out.width = f->width;
	out.height = f->height;
	out.fullRange = f->color_range == AVCOL_RANGE_JPEG || f->format == AV_PIX_FMT_YUVJ420P;
	// which colour system the picture uses, and whether it is HDR (which needs converting for this screen)
	if (f->colorspace == AVCOL_SPC_BT2020_NCL || f->colorspace == AVCOL_SPC_BT2020_CL)
		out.matrix = 2;
	else if (f->colorspace == AVCOL_SPC_BT709 || (f->colorspace == AVCOL_SPC_UNSPECIFIED && f->height >= 720))
		out.matrix = 1;
	else
		out.matrix = 0;
	out.transfer = f->color_trc == AVCOL_TRC_SMPTE2084 ? 1 : f->color_trc == AVCOL_TRC_ARIB_STD_B67 ? 2 : 0;
	g_shown++;
	return true;
}

void player::pause(bool on)
{
	const SessionPtr s = current();
	if (!s)
		return;
	std::lock_guard<std::mutex> lock(s->m);
	s->paused = on;
	if (!on)
	{
		// the clocks stood still: start them again from here
		s->baseSet = false;
		s->audioClock = false;
		s->startWall = nowS();
	}
	s->cv.notify_all();
}

bool player::paused()
{
	const SessionPtr s = current();
	return s && s->paused;
}

double player::duration()
{
	const SessionPtr s = current();
	if (!s)
		return 0;
	std::lock_guard<std::mutex> lock(s->m);
	return s->duration;
}

double player::position()
{
	const SessionPtr s = current();
	if (!s)
		return 0;
	std::lock_guard<std::mutex> lock(s->m);
	return s->position < 0 ? 0 : s->position;
}

void player::seekBy(double seconds)
{
	const SessionPtr s = current();
	if (!s)
		return;
	std::lock_guard<std::mutex> lock(s->m);
	if (s->duration <= 0 || s->eof)
		return;                                        // live, or already at the end
	double target = (s->seekTo >= 0 ? s->seekTo : s->position) + seconds;
	if (target > s->duration - 3) target = s->duration - 3;
	if (target < 0) target = 0;
	s->seekTo = target;
	s->position = target;                              // shown at once, before the picture catches up
	s->cv.notify_all();
}

void player::setDisplaySize(int width, int height)
{
	g_displayW = width > 0 ? width : 1920;
	g_displayH = height > 0 ? height : 1080;
}

void player::setPictureBudget(int bytes)
{
	g_pictureBudget = bytes < 200000 ? 200000 : bytes;
}

void player::takeCounts(int& decoded, int& shown, int& dropped)
{
	decoded = g_decoded.exchange(0);
	shown = g_shown.exchange(0);
	dropped = g_dropped.exchange(0);
}

std::vector<player::Track> player::audioTracks()
{
	const SessionPtr s = current();
	if (!s)
		return {};
	std::lock_guard<std::mutex> lock(s->m);
	return s->audioTracks;
}

std::vector<player::Track> player::subtitleTracks()
{
	const SessionPtr s = current();
	if (!s)
		return {};
	std::lock_guard<std::mutex> lock(s->m);
	return s->subtitleTracks;
}

int player::audioTrack()
{
	const SessionPtr s = current();
	if (!s)
		return -1;
	std::lock_guard<std::mutex> lock(s->m);
	return s->wantAudio >= 0 ? s->wantAudio : s->readAidx;
}

int player::subtitleTrack()
{
	const SessionPtr s = current();
	if (!s)
		return -1;
	std::lock_guard<std::mutex> lock(s->m);
	return s->wantSubtitle >= -1 ? s->wantSubtitle : s->sidx;
}

void player::setAudioTrack(int id)
{
	const SessionPtr s = current();
	if (!s)
		return;
	std::lock_guard<std::mutex> lock(s->m);
	s->wantAudio = id;
	s->cv.notify_all();
}

void player::setSubtitleTrack(int id)
{
	const SessionPtr s = current();
	if (!s)
		return;
	std::lock_guard<std::mutex> lock(s->m);
	s->wantSubtitle = id < 0 ? -1 : id;
	s->cv.notify_all();
}

std::string player::subtitleText()
{
	const SessionPtr s = current();
	if (!s)
		return "";
	std::lock_guard<std::mutex> lock(s->m);
	if (s->sidx < 0)
		return "";
	const double now = s->position + s->startTime;
	while (!s->cues.empty() && s->cues.front().end < now - 2.0)
		s->cues.pop_front();
	std::string text;
	for (const Session::Cue& cue : s->cues)
	{
		if (cue.start > now)
			break;
		if (cue.end > now)
			text = text.empty() ? cue.text : text + "\n" + cue.text;
	}
	return text;
}

void player::seekTo(double seconds)
{
	const SessionPtr s = current();
	if (!s)
		return;
	std::lock_guard<std::mutex> lock(s->m);
	if (s->duration <= 0 || s->eof)
		return;
	if (seconds > s->duration - 3) seconds = s->duration - 3;
	if (seconds < 0) seconds = 0;
	s->seekTo = seconds;
	s->position = seconds;
	s->cv.notify_all();
}
