#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Plays one network stream at a time: FFmpeg reads and decodes it on background
// threads, sound goes out through SDL, and pictures are handed to the drawing
// thread when they are due (timed against the sound).
namespace player
{
	enum State { IDLE, OPENING, PLAYING, ENDED, FAILED };

	struct Picture
	{
		const uint8_t* planes[3];
		int linesize[3];
		int width, height;
		bool fullRange;
		int matrix;                               // 0: BT.601 (SD), 1: BT.709 (HD), 2: BT.2020 (UHD)
		int transfer;                             // 0: ordinary, 1: HDR (PQ), 2: HDR (HLG)
	};

	void init();                                  // once, after SDL is up
	void play(const std::string& url, const std::string& userAgent, const std::string& referer);
	void stop();
	State state();
	std::string message();                        // why it failed, when it did
	bool hasVideo();
	// True when a new picture is due; its memory stays valid until the next call or stop().
	bool nextPicture(Picture& out);

	// Films and episodes have a length and can be paused and moved through; live streams cannot.
	double duration();                            // seconds, or 0 for a live stream
	double position();                            // seconds from the start
	void seekBy(double seconds);
	void pause(bool on);
	bool paused();

	void seekTo(double seconds);

	// Sound tracks and text subtitles carried by what is playing. `id` identifies a track.
	struct Track
	{
		int id;
		std::string label;
	};
	std::vector<Track> audioTracks();
	std::vector<Track> subtitleTracks();
	int audioTrack();                             // the one in use
	int subtitleTrack();                          // the one in use, or -1 for none
	void setAudioTrack(int id);
	void setSubtitleTrack(int id);                // -1 turns subtitles off
	std::string subtitleText();                   // the words to show now ("" for none)

	// The size of the screen being drawn to; pictures far larger than it are halved while decoding.
	void setDisplaySize(int width, int height);
	// The most bytes one picture should be when handed to the graphics chip. Larger pictures are
	// reduced while decoding (in steps: two-thirds, half, a third, a quarter).
	void setPictureBudget(int bytes);
	// Pictures decoded, shown and skipped (for arriving late) since the last call.
	void takeCounts(int& decoded, int& shown, int& dropped);
}
