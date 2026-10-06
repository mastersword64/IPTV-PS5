#include "logos.h"
#include "gfx.h"
#include "images.h"
#include "log.h"
#include "net.h"
#include "threads.h"

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

namespace
{
	const int kLargest = 112;          // logos are kept no larger than this on a side
	const int kLargestPoster = 330;    // posters are shown bigger, so they keep more
	const size_t kMostPictures = 900;  // plenty for a long browse; after that, tiles stay as initials

	struct Ready { std::string url; std::vector<uint8_t> rgba; int w = 0, h = 0; };

	std::string g_folder;
	// These two are never destroyed: the worker threads use them until the program is gone, and
	// tearing them down at exit while a worker is waiting on them can hang the exit.
	std::mutex& g_mutex = *new std::mutex;
	std::condition_variable& g_wake = *new std::condition_variable;
	std::deque<std::string> g_wanted;                   // newest at the back; the newest is fetched first
	std::deque<Ready> g_ready;
	std::unordered_map<std::string, int> g_known;       // address -> picture, -1 failed, -2 being fetched
	bool g_started = false, g_working = false;
	size_t g_made = 0;

	std::string fileFor(const std::string& url)
	{
		unsigned long long hash = 1469598103934665603ULL;
		for (const char c : url)
		{
			hash ^= (unsigned char)c;
			hash *= 1099511628211ULL;
		}
		char name[40];
		snprintf(name, sizeof(name), "/%016llx.logo", hash);
		return g_folder + name;
	}

	// The saved form: "LG", width, height (two bytes each), then the dots.
	bool readSaved(const std::string& path, Ready& out)
	{
		FILE* f = fopen(path.c_str(), "rb");
		if (!f)
			return false;
		unsigned char head[6];
		bool ok = fread(head, 1, 6, f) == 6 && head[0] == 'L' && head[1] == 'G';
		if (ok)
		{
			out.w = head[2] | (head[3] << 8);
			out.h = head[4] | (head[5] << 8);
			ok = out.w > 0 && out.h > 0 && out.w <= 1024 && out.h <= 1024;
		}
		if (ok)
		{
			out.rgba.resize((size_t)out.w * (size_t)out.h * 4);
			ok = fread(out.rgba.data(), 1, out.rgba.size(), f) == out.rgba.size();
		}
		fclose(f);
		return ok;
	}

	void writeSaved(const std::string& path, const Ready& picture)
	{
		FILE* f = fopen(path.c_str(), "wb");
		if (!f)
			return;
		const unsigned char head[6] = { 'L', 'G', (unsigned char)(picture.w & 0xff), (unsigned char)(picture.w >> 8), (unsigned char)(picture.h & 0xff), (unsigned char)(picture.h >> 8) };
		fwrite(head, 1, 6, f);
		fwrite(picture.rgba.data(), 1, picture.rgba.size(), f);
		fclose(f);
	}

	// Shrinks a picture so its longer side is at most kLargest, averaging the dots each new dot covers.
	void shrink(const std::vector<uint8_t>& from, int w, int h, int largest, Ready& out)
	{
		const int kLargest = largest;
		const int longest = w > h ? w : h;
		if (longest <= kLargest)
		{
			out.rgba = from;
			out.w = w;
			out.h = h;
			return;
		}
		out.w = w * kLargest / longest > 0 ? w * kLargest / longest : 1;
		out.h = h * kLargest / longest > 0 ? h * kLargest / longest : 1;
		out.rgba.resize((size_t)out.w * (size_t)out.h * 4);
		for (int y = 0; y < out.h; y++)
		{
			const int y0 = y * h / out.h, y1 = (y + 1) * h / out.h > y0 ? (y + 1) * h / out.h : y0 + 1;
			for (int x = 0; x < out.w; x++)
			{
				const int x0 = x * w / out.w, x1 = (x + 1) * w / out.w > x0 ? (x + 1) * w / out.w : x0 + 1;
				// see-through dots must not darken the average, so colour is weighted by how solid each dot is
				unsigned long long r = 0, g = 0, b = 0, a = 0;
				for (int sy = y0; sy < y1 && sy < h; sy++)
					for (int sx = x0; sx < x1 && sx < w; sx++)
					{
						const uint8_t* p = &from[((size_t)sy * (size_t)w + (size_t)sx) * 4];
						r += (unsigned long long)p[0] * p[3];
						g += (unsigned long long)p[1] * p[3];
						b += (unsigned long long)p[2] * p[3];
						a += p[3];
					}
				const unsigned long long count = (unsigned long long)(y1 - y0) * (unsigned long long)(x1 - x0);
				uint8_t* to = &out.rgba[((size_t)y * (size_t)out.w + (size_t)x) * 4];
				to[0] = (uint8_t)(a ? r / a : 0);
				to[1] = (uint8_t)(a ? g / a : 0);
				to[2] = (uint8_t)(a ? b / a : 0);
				to[3] = (uint8_t)(count ? a / count : 0);
			}
		}
	}

	void work(int lane)
	{
		for (;;)
		{
			std::string url;
			{
				std::unique_lock<std::mutex> lock(g_mutex);
				while (g_wanted.empty())
					g_wake.wait_for(lock, std::chrono::milliseconds(500));
				url = g_wanted.back();                  // the most recently asked for is on screen now
				g_wanted.pop_back();
			}
			// the first letter says which size is wanted; the rest is the address
			Ready picture;
			picture.url = url;
			const bool poster = url[0] == 'P';
			const std::string address = url.substr(1);
			const std::string saved = fileFor(url);
			bool ok = readSaved(saved, picture);
			if (!ok)
			{
				std::string body;
				std::vector<uint8_t> rgba;
				int w = 0, h = 0;
				if (httpGetQuiet(address, body, 4u * 1024 * 1024, 12, lane) && loadImageMemory((const uint8_t*)body.data(), body.size(), rgba, w, h))
				{
					shrink(rgba, w, h, poster ? kLargestPoster : kLargest, picture);
					writeSaved(saved, picture);
					ok = true;
				}
			}
			std::lock_guard<std::mutex> lock(g_mutex);
			if (ok)
				g_ready.push_back(std::move(picture));
			else
				g_known[url] = -1;
		}
	}
}

void logosStart(const std::string& dataDir)
{
	g_folder = dataDir + "/logos";
	mkdir(g_folder.c_str(), 0777);
	g_started = true;
	if (!imagesSupported())
		logLine("logos: this build has no picture decoder, so logos and posters are not shown");
}

int logoImage(const std::string& address, bool poster)
{
	if (!g_started || !imagesSupported() || address.size() < 12 || address.compare(0, 4, "http") != 0)
		return -1;
	const std::string url = (poster ? "P" : "L") + address;
	std::lock_guard<std::mutex> lock(g_mutex);
	const auto found = g_known.find(url);
	if (found != g_known.end())
		return found->second >= 0 ? found->second : -1;
	if (g_made + g_ready.size() >= kMostPictures)
		return -1;
	g_known[url] = -2;
	g_wanted.push_back(url);
	// a long scroll asks for many that are no longer on screen: the oldest requests are let go
	while (g_wanted.size() > 40)
	{
		g_known.erase(g_wanted.front());
		g_wanted.pop_front();
	}
	if (!g_working)
	{
		g_working = true;
		for (int lane = 0; lane < 3; lane++)          // three at a time: walls of posters fill in quickly
			startThread("logos", [lane] { work(lane); });
	}
	g_wake.notify_all();
	return -1;
}

void logosPump()
{
	// a few per frame, so a burst of arrivals does not hold up the drawing
	for (int i = 0; i < 4; i++)
	{
		Ready picture;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			if (g_ready.empty())
				return;
			picture = std::move(g_ready.front());
			g_ready.pop_front();
		}
		const int image = gfx::imageCreate(picture.rgba.data(), picture.w, picture.h, true);
		std::lock_guard<std::mutex> lock(g_mutex);
		g_known[picture.url] = image >= 0 ? image : -1;
		if (image >= 0)
			g_made++;
	}
}
