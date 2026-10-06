/* IPTV for PS5: a native player for M3U playlists, modelled on IPTVnator.
 *
 * It is started by the same PS5 entry point the EmulationStation port uses
 * (es_ps5_main.cpp from the kit), which calls es_main() once the console side
 * is ready. It manages several playlists (M3U files, M3U web addresses, Xtream Codes accounts),
 * browses groups and channels in IPTVnator's layout, and plays streams. */
#include "gfx.h"
#include "cursors.h"
#include "images.h"
#include "log.h"
#include "net.h"
#include "player.h"
#include "playlist.h"
#include "sources.h"
#include "threads.h"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---- log -------------------------------------------------------------------

static std::string g_dataDir = "/app0";
static FILE* g_log = nullptr;
static std::mutex g_logMutex;

void logLine(const char* format, ...)
{
	std::lock_guard<std::mutex> lock(g_logMutex);
	if (!g_log)
		return;
	// each line starts with the seconds since the app started, to show where time goes
	static const auto began = std::chrono::steady_clock::now();
	fprintf(g_log, "[%7.1f] ", std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count());
	va_list arguments;
	va_start(arguments, format);
	vfprintf(g_log, format, arguments);
	va_end(arguments);
	fputc('\n', g_log);
	fflush(g_log);
}

static bool usableFolder(const std::string& path)
{
	mkdir(path.c_str(), 0777);
	const std::string probe = path + "/.write-test";
	FILE* f = fopen(probe.c_str(), "wb");
	if (!f)
		return false;
	fclose(f);
	remove(probe.c_str());
	return true;
}

static void writeTextFile(const std::string& path, const std::string& text)
{
	FILE* f = fopen(path.c_str(), "wb");
	if (!f)
		return;
	fwrite(text.data(), 1, text.size(), f);
	fclose(f);
}

// ---- themes ----------------------------------------------------------------
// Every colour the interface uses comes from the theme in use, chosen in Settings > Themes.

struct Theme
{
	const char* name;
	const char* description;
	uint32_t backgroundTop, backgroundBottom;   // the screen behind everything (a gradient when they differ)
	uint32_t pane, column, chip, tile, tileLine, rail;
	uint32_t selected, border, borderDim, band;
	uint32_t text, muted, accent, live, liveBack;
	uint32_t line, deep, card, dialog, hover, scroll, pointer;
	float radius;                                // 1 = as designed; higher is rounder
};

static const Theme kThemes[] = {
	{ "IPTVnator Dark", "The original look: navy panels with blue highlights",
	  0x10131aff, 0x10131aff,
	  0x10131aff, 0x141825ff, 0x1b2030ff, 0x232a3cff, 0x343c52ff, 0x151925ff,
	  0x1e2a47ff, 0x6f9cf5ff, 0x3a4a72ff, 0x222944ff,
	  0xf1f3f8ff, 0x8b93a7ff, 0x8ab4ffff, 0xf2707aff, 0x3a1c26ff,
	  0x2a3044ff, 0x0c0f15ff, 0x171b28ff, 0x141825ff, 0xffffff12, 0xffffff50, 0xffffffff, 1.0f },
	{ "Aurora", "Modern: glass panels over a violet-to-teal night sky, round corners",
	  0x140a26ff, 0x04161cff,
	  0x00000000, 0xffffff0b, 0xffffff14, 0xffffff1a, 0xffffff30, 0xffffff0d,
	  0x8b5cf63c, 0xb49cffff, 0xb49cff66, 0x8b5cf626,
	  0xf6f3ffff, 0xa8a3c0ff, 0xcdbdffff, 0xff7a90ff, 0xff7a9030,
	  0xffffff26, 0x00000070, 0xffffff10, 0x161024ff, 0xffffff16, 0xffffff55, 0xffffffff, 1.7f },
	{ "Midnight", "Pure black for OLED screens, sharp corners, mint highlight",
	  0x000000ff, 0x000000ff,
	  0x000000ff, 0x0a0a0bff, 0x111214ff, 0x1a1c1fff, 0x2a2d32ff, 0x0a0a0bff,
	  0x0f2a22ff, 0x34d399ff, 0x1f6b52ff, 0x101816ff,
	  0xf2f5f3ff, 0x8a9490ff, 0x5eeab4ff, 0xff6b6bff, 0x331616ff,
	  0x24272bff, 0x000000ff, 0x0e0f11ff, 0x0c0d0eff, 0xffffff12, 0xffffff50, 0xffffffff, 0.45f },
	{ "Daylight", "Light, like the original light theme: white panels, blue highlights",
	  0xeef1f7ff, 0xeef1f7ff,
	  0xeef1f7ff, 0xffffffff, 0xffffffff, 0xe6ebf5ff, 0xc9d2e3ff, 0xe3e8f2ff,
	  0xdbe7ffff, 0x3b6fe0ff, 0x9db8f0ff, 0xe6ecfaff,
	  0x1a2030ff, 0x5c667aff, 0x2456c8ff, 0xd92d4aff, 0xfde3e8ff,
	  0xcdd5e3ff, 0xf3f5faff, 0xffffffff, 0xffffffff, 0x0000000e, 0x00000045, 0x1a2030ff, 1.0f },
};
static const int kThemeCount = (int)(sizeof(kThemes) / sizeof(kThemes[0]));
static int g_theme = 0;

static uint32_t kBackground, kBackgroundBottom, kPane, kColumn, kChip, kTile, kTileLine, kRail;
static uint32_t kSelected, kBorder, kBorderDim, kBand;
static uint32_t kText, kMuted, kAccent, kLive, kLiveBack;
static uint32_t kLine, kDeep, kCard, kDialog, kHover, kScroll, kPointer;
static const uint32_t kOnVideo = 0xf1f3f8ff, kOnVideoMuted = 0xa9afbdff;   // text over the picture, whatever the theme

static void applyTheme(int index)
{
	if (index < 0 || index >= kThemeCount)
		index = 0;
	g_theme = index;
	const Theme& t = kThemes[index];
	kBackground = t.backgroundTop; kBackgroundBottom = t.backgroundBottom;
	kPane = t.pane; kColumn = t.column; kChip = t.chip; kTile = t.tile; kTileLine = t.tileLine; kRail = t.rail;
	kSelected = t.selected; kBorder = t.border; kBorderDim = t.borderDim; kBand = t.band;
	kText = t.text; kMuted = t.muted; kAccent = t.accent; kLive = t.live; kLiveBack = t.liveBack;
	kLine = t.line; kDeep = t.deep; kCard = t.card; kDialog = t.dialog; kHover = t.hover; kScroll = t.scroll; kPointer = t.pointer;
	gfx::setRadiusScale(t.radius);
}

// The screen behind everything: a flat colour, or a top-to-bottom blend drawn as bands.
static void drawBackground()
{
	if (kBackground == kBackgroundBottom)
		return; // already cleared to it
	const int bands = 54;
	for (int i = 0; i < bands; i++)
	{
		const float t = (float)i / (float)(bands - 1);
		uint32_t colour = 0xff;
		for (int shift = 24; shift >= 8; shift -= 8)
		{
			const float a = (float)((kBackground >> shift) & 0xff), b = (float)((kBackgroundBottom >> shift) & 0xff);
			colour |= (uint32_t)(a + (b - a) * t + 0.5f) << shift;
		}
		gfx::rect(0, (float)i * 20, 1920, 20, colour);
	}
}

static double nowSeconds();

// ---- settings: pointer and display ------------------------------------------------

static int g_pointerSpeed = 4;                  // 1 (slow) to 10 (fast)
static int g_pointerSize = 1;                   // 0 small, 1 medium, 2 large
static std::string g_pointerType = "Arrow";     // a shape drawn by the app, or the name of a pointer file
static bool g_showFps = false;
static int g_resolution = 0;                    // 0 automatic, 1 1080p, 2 4K (used at the next start)
static int g_videoDetail = 1;                   // 0 sharpest, 1 balanced, 2 smoothest
static double g_uploadRate = 75000;             // bytes of picture the graphics layer accepts per millisecond (measured)
static int g_drawW = 1920, g_drawH = 1080;      // what is actually being drawn
static double g_fps = 0, g_slowestMs = 0;       // measured over the last second or so

// The pointer can be a shape drawn by the app (the first two), or a pointer file: the ones that
// come with the app, and any the viewer uploads (Windows .cur / .ani cursors, or pictures).
static const char* const kPointerShapes[] = { "Dot", "Ring" };
static const int kPointerShapeCount = 2;
struct PointerArt
{
	std::string path;
	bool tried = false;
	std::vector<int> images;                    // one per frame; empty if the file could not be read
	int width = 0, height = 0, hotX = 0, hotY = 0;
	double frameSeconds = 0.1;
};
static std::vector<std::string> g_pointerNames;                 // the shapes, then the files found
static std::unordered_map<std::string, PointerArt> g_pointerArt; // by name (the file name without its ending)

static void saveSettings()
{
	char text[512];
	snprintf(text, sizeof(text), "pointer_speed=%d\npointer_size=%d\npointer_type=%s\nshow_fps=%d\nresolution=%d\nvideo_detail=%d\n",
		g_pointerSpeed, g_pointerSize, g_pointerType.c_str(), g_showFps ? 1 : 0, g_resolution, g_videoDetail);
	writeTextFile(g_dataDir + "/settings.txt", text);
}

static void loadSettings()
{
	std::string text;
	if (readTextFile(g_dataDir + "/settings.txt", text))
	{
		size_t pos = 0;
		while (pos < text.size())
		{
			size_t end = text.find('\n', pos);
			if (end == std::string::npos)
				end = text.size();
			const std::string line = text.substr(pos, end - pos);
			pos = end + 1;
			const size_t equals = line.find('=');
			if (equals == std::string::npos)
				continue;
			const std::string key = trim(line.substr(0, equals)), value = trim(line.substr(equals + 1));
			if (key == "pointer_speed") g_pointerSpeed = atoi(value.c_str());
			else if (key == "pointer_size") g_pointerSize = atoi(value.c_str());
			else if (key == "pointer_type" && !value.empty()) g_pointerType = value;
			else if (key == "show_fps") g_showFps = value == "1";
			else if (key == "resolution") g_resolution = atoi(value.c_str());
			else if (key == "video_detail") g_videoDetail = atoi(value.c_str());
		}
	}
	if (g_pointerSpeed < 1) g_pointerSpeed = 1;
	if (g_pointerSpeed > 10) g_pointerSpeed = 10;
	if (g_pointerSize < 0 || g_pointerSize > 2) g_pointerSize = 1;
	if (g_resolution < 0 || g_resolution > 2) g_resolution = 0;
	if (g_videoDetail < 0 || g_videoDetail > 2) g_videoDetail = 1;

	// Pointer files: those that come with the app, then the viewer's own (which win on a name clash).
	g_pointerNames.assign(kPointerShapes, kPointerShapes + kPointerShapeCount);
	mkdir((g_dataDir + "/pointers").c_str(), 0777);
	const std::string folders[2] = { "/app0/assets/pointers", g_dataDir + "/pointers" };
	for (const std::string& folder : folders)
	{
		DIR* dir = opendir(folder.c_str());
		if (!dir)
			continue;
		std::vector<std::string> found;
		while (const dirent* entry = readdir(dir))
		{
			const std::string name = entry->d_name;
			const size_t dot = name.find_last_of('.');
			if (dot == std::string::npos || dot == 0)
				continue;
			std::string extension = name.substr(dot + 1);
			for (char& c : extension)
				if (c >= 'A' && c <= 'Z')
					c = (char)(c + 32);
			if (extension == "cur" || extension == "ani" || extension == "ico" || extension == "png" || extension == "jpg"
				|| extension == "jpeg" || extension == "bmp" || extension == "gif" || extension == "tga")
				found.push_back(name);
		}
		closedir(dir);
		// in order of their names as shown (without the file ending)
		std::sort(found.begin(), found.end(), [](const std::string& a, const std::string& b) {
			return a.substr(0, a.find_last_of('.')) < b.substr(0, b.find_last_of('.'));
		});
		for (const std::string& file : found)
		{
			const std::string name = file.substr(0, file.find_last_of('.'));
			if (!g_pointerArt.count(name))
				g_pointerNames.push_back(name);
			PointerArt art;
			art.path = folder + "/" + file;
			g_pointerArt[name] = art;
		}
	}
	bool known = false;
	for (const std::string& name : g_pointerNames)
		if (name == g_pointerType)
			known = true;
	if (!known)
		g_pointerType = g_pointerArt.count("Arrow") ? "Arrow" : "Dot";
}

// Video pictures are reduced, while decoding, to what the graphics layer can accept in a set time:
// longer for the sharpest picture, shorter for the smoothest motion.
static void applyVideoBudget()
{
	static const double milliseconds[3] = { 40.0, 20.0, 11.0 };
	player::setPictureBudget((int)(g_uploadRate * milliseconds[g_videoDetail]));
}

// Draws one pointer with its hot spot (the point that clicks) at (x, y).
static void drawPointerShape(const std::string& type, float x, float y)
{
	const float s = g_pointerSize == 0 ? 0.75f : g_pointerSize == 2 ? 1.5f : 1.0f;
	const float keepScale = gfx::radiusScale();
	gfx::setRadiusScale(1.0f);                      // a theme's corner style must not reshape the pointer
	const uint32_t shade = 0x00000058;

	PointerArt* art = nullptr;
	const auto found = g_pointerArt.find(type);
	if (found != g_pointerArt.end())
	{
		art = &found->second;
		if (!art->tried)
		{
			// read the first time it is needed
			art->tried = true;
			std::vector<CursorFrame> frames;
			if (loadCursorFile(art->path, frames, art->frameSeconds))
			{
				art->width = frames[0].width;
				art->height = frames[0].height;
				art->hotX = frames[0].hotX;
				art->hotY = frames[0].hotY;
				for (const CursorFrame& frame : frames)
				{
					const int image = gfx::imageCreate(frame.rgba.data(), frame.width, frame.height, frame.width > 48);
					if (image >= 0)
						art->images.push_back(image);
				}
			}
			logLine("pointer: %s: %s (%d frame%s, %dx%d)", type.c_str(), art->images.empty() ? "could not be read" : "loaded",
				(int)art->images.size(), art->images.size() == 1 ? "" : "s", art->width, art->height);
		}
		if (art->images.empty())
			art = nullptr;
	}

	if (art)
	{
		// Small cursors (the usual 32-pixel ones) are enlarged by whole steps so their pixels stay
		// even; larger artwork is fitted to a fixed size.
		float scale;
		if (art->width <= 48)
			scale = g_pointerSize == 0 ? 1.0f : g_pointerSize == 2 ? 3.0f : 2.0f;
		else
			scale = 58.0f * s / (float)(art->width > art->height ? art->width : art->height);
		const size_t frame = art->images.size() > 1 ? (size_t)(nowSeconds() / art->frameSeconds) % art->images.size() : 0;
		gfx::image(art->images[frame], x - (float)art->hotX * scale, y - (float)art->hotY * scale, (float)art->width * scale, (float)art->height * scale);
	}
	else if (type == "Ring")
	{
		const float r = 13 * s, t = 3 * s;
		gfx::panel(x - r + 1, y - r + 3, r * 2, r * 2, r, t + 1, shade, 0);
		gfx::panel(x - r, y - r, r * 2, r * 2, r, t, kPointer, 0);
		gfx::roundRect(x - 2.5f * s, y - 2.5f * s, 5 * s, 5 * s, 2.5f * s, kPointer);
	}
	else
	{
		// the dot: a few faint, slightly larger discs set a little lower make its shadow
		static const float shadow[4][2] = { { 34, 0x0c }, { 28, 0x16 }, { 23, 0x24 }, { 19, 0x38 } };
		for (const auto& layer : shadow)
			gfx::roundRect(x - layer[0] * s / 2 + 1, y - layer[0] * s / 2 + 4, layer[0] * s, layer[0] * s, layer[0] * s / 2, (uint32_t)layer[1]);
		gfx::roundRect(x - 8 * s, y - 8 * s, 16 * s, 16 * s, 8 * s, kPointer);
	}
	gfx::setRadiusScale(keepScale);
}

// ---- playlists (loaded in the background) --------------------------------------

static std::vector<Source> g_sources;                  // every playlist the viewer has
static int g_active = -1;                              // which one is open
static std::string g_activeId;

// ---- the three libraries: live TV, movies, series ---------------------------------
// Each has its own categories, lists, favourites and place in the screen. The rest of the code
// works on "the current one" through the names below, so switching tab is switching `T`.

enum Section { S_ALL, S_GROUPS, S_FAVOURITES, S_RECENT, S_COUNT };
enum Focus { F_RAIL, F_CATEGORIES, F_CHANNELS };

struct Tab
{
	Playlist playlist;
	std::vector<std::string> groupNames;               // [0] is "All"
	std::vector<std::vector<int>> groupChannels;       // entry numbers per group
	std::unordered_map<std::string, int> groupIndex;   // group name -> place in groupChannels
	std::unordered_map<std::string, int> byUrl;        // every known entry, by address
	std::vector<char> groupState;                      // per category: 0 not fetched, 1 fetching, 2 done, 3 failed
	std::vector<char> isFavourite;
	std::vector<int> favourites, recent;               // entry numbers, newest first for recent
	std::vector<int> view;                             // what the middle column shows
	std::vector<int> cats;                             // the categories shown (not hidden, matching the filter)
	std::unordered_set<std::string> hidden;            // category names the viewer has hidden
	std::string filter;
	bool filterCats = false;                           // the filter applies to category names, not entries
	Section section = S_ALL;
	Focus focus = F_CHANNELS;
	int catSel = 0, catTop = 0, sel = 0, top = 0;
	// a series that has been opened: its episodes take over the middle column
	bool drill = false;
	int drillState = 0, drillItem = -1;                // state: 1 fetching, 2 listed, 3 failed
	std::vector<int> drillList;
	std::string drillTitle, drillError;
	int state = 0;                                     // 0 not loaded, 1 loading, 2 ready, 3 failed
	std::string error;
};

static const int kTabCount = 4;                         // live, movies, series, account
static const int kAccountTab = 3;
static const char* const kTabName[kTabCount] = { "Live TV", "Movies", "Series", "Account" };
static Tab g_tabs[3];
static Tab* T = &g_tabs[0];
static int g_tab = 0;

#define g_playlist (T->playlist)
#define g_groupNames (T->groupNames)
#define g_groupChannels (T->groupChannels)
#define g_groupIndex (T->groupIndex)
#define g_byUrl (T->byUrl)
#define g_groupState (T->groupState)
#define g_isFavourite (T->isFavourite)
#define g_favourites (T->favourites)
#define g_recent (T->recent)
#define g_view (T->view)
#define g_filter (T->filter)
#define g_section (T->section)
#define g_focus (T->focus)
#define g_catSel (T->catSel)
#define g_catTop (T->catTop)
#define g_sel (T->sel)
#define g_top (T->top)

// The middle column is the category list's partner only when there are categories to show.
static bool inGroups()
{
	return g_section == S_GROUPS && !T->cats.empty();
}

// Where the selected category sits in the list of shown categories.
static int catPlace()
{
	for (int i = 0; i < (int)T->cats.size(); i++)
		if (T->cats[(size_t)i] == g_catSel)
			return i;
	return 0;
}
static std::atomic<int> g_loadState{ 0 };              // 0 loading, 1 done, 2 failed
static std::string g_loadError;

static void buildGroups()
{
	g_groupNames.clear();
	g_groupChannels.clear();
	g_groupNames.push_back("All channels");
	g_groupChannels.emplace_back();
	std::unordered_map<std::string, int>& index = g_groupIndex;
	index.clear();
	for (const std::string& name : g_playlist.groups)
	{
		index[name] = (int)g_groupNames.size();
		g_groupNames.push_back(name);
		g_groupChannels.emplace_back();
	}
	for (int i = 0; i < (int)g_playlist.channels.size(); i++)
	{
		g_groupChannels[0].push_back(i);
		g_groupChannels[index[g_playlist.channels[i].group]].push_back(i);
	}
}

// A load can be abandoned (the viewer cancels, or picks another playlist), so the background
// thread hands its result over only if it is still the one being waited for.
static std::mutex g_loadMutex;
static int g_loadRequest = 0;
static Playlist g_loadedPlaylist;
static double g_loadStarted = 0;

// Runs on its own thread: fetches a fresh copy of the playlist (this is the update on every start).
static void loadPlaylist(const Source& source, int request)
{
	logLine("playlist: loading \"%s\" (%s)", source.name.c_str(), sourceKind(source));
	Playlist fresh;
	std::string error;
	const bool ok = fetchSource(source, g_dataDir, fresh, error);
	std::lock_guard<std::mutex> lock(g_loadMutex);
	if (request != g_loadRequest)
	{
		logLine("playlist: \"%s\" finished after it was no longer wanted", source.name.c_str());
		return;
	}
	if (!ok)
	{
		g_loadError = error;
		logLine("playlist: %s", error.c_str());
		g_loadState = 2;
		return;
	}
	logLine("playlist: %d channels in %d groups", (int)fresh.channels.size(), (int)fresh.groups.size());
	g_loadedPlaylist = std::move(fresh);
	g_loadState = 1;
}

// ---- input -----------------------------------------------------------------

enum Action { A_UP, A_DOWN, A_LEFT, A_RIGHT, A_OK, A_BACK, A_PAGE_UP, A_PAGE_DOWN, A_MENU, A_FAVOURITE, A_SEARCH, A_TAB_PREV, A_TAB_NEXT };

static std::vector<Action> g_actions;
static bool g_padDir[4], g_stickDir[4], g_keyDir[4];   // up, down, left, right
static bool g_heldBefore[4];
static double g_repeatAt[4];
static bool g_haveController = false;
static int g_rawLogged = 0, g_buttonsLogged = 0;

static double nowSeconds()
{
	return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

static void openPad(int index)
{
	if (SDL_IsGameController(index))
	{
		if (SDL_GameControllerOpen(index))
		{
			g_haveController = true;
			logLine("input: controller %d opened: %s", index, SDL_GameControllerNameForIndex(index));
		}
	}
	else if (SDL_JoystickOpen(index))
		logLine("input: joystick %d opened (no standard button layout known): %s", index, SDL_JoystickNameForIndex(index));
}

static float g_stickX = 0, g_stickY = 0;      // left stick: moves the pointer
static void stick(int axis, int value)
{
	if (axis == 0) g_stickX = (float)value / 32767.0f;
	if (axis == 1) g_stickY = (float)value / 32767.0f;
}

// right stick: scrolls the list (how far it is pushed sets the speed)
static float g_scrollStick = 0, g_scrollCarry = 0;
static int g_axisLogged = 0;

// How many rows to move this frame.
static int scrollSteps(double seconds)
{
	const float push = g_scrollStick < 0 ? -g_scrollStick : g_scrollStick;
	if (push < 0.25f)
	{
		g_scrollCarry = 0;
		return 0;
	}
	const float t = ((push > 1.0f ? 1.0f : push) - 0.25f) / 0.75f;
	const float rowsPerSecond = 3.0f + 42.0f * t * t;
	g_scrollCarry += (g_scrollStick < 0 ? -1.0f : 1.0f) * rowsPerSecond * (float)seconds;
	const int steps = (int)g_scrollCarry;
	g_scrollCarry -= (float)steps;
	return steps;
}

static void onEvent(const SDL_Event& e, bool& running)
{
	switch (e.type)
	{
	case SDL_QUIT:
		running = false;
		break;
	case SDL_CONTROLLERDEVICEADDED:
		openPad(e.cdevice.which);
		break;
	case SDL_JOYDEVICEADDED:
		if (!SDL_IsGameController(e.jdevice.which))
			openPad(e.jdevice.which);
		break;
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP:
	{
		const bool down = e.type == SDL_CONTROLLERBUTTONDOWN;
		if (down && g_buttonsLogged < 30)
		{
			g_buttonsLogged++;
			logLine("input: controller button %d pressed (Options is %d)", (int)e.cbutton.button, (int)SDL_CONTROLLER_BUTTON_START);
		}
		switch (e.cbutton.button)
		{
		case SDL_CONTROLLER_BUTTON_DPAD_UP: g_padDir[0] = down; break;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN: g_padDir[1] = down; break;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT: g_padDir[2] = down; break;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: g_padDir[3] = down; break;
		case SDL_CONTROLLER_BUTTON_A: if (down) g_actions.push_back(A_OK); break;
		case SDL_CONTROLLER_BUTTON_B: if (down) g_actions.push_back(A_BACK); break;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: if (down) g_actions.push_back(A_PAGE_UP); break;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: if (down) g_actions.push_back(A_PAGE_DOWN); break;
		case SDL_CONTROLLER_BUTTON_START:      // Options
		case SDL_CONTROLLER_BUTTON_BACK:       // Create / Share
		case 20:                               // the touchpad click, on SDL versions that report it
			if (down) g_actions.push_back(A_MENU);
			break;
		case SDL_CONTROLLER_BUTTON_X: if (down) g_actions.push_back(A_FAVOURITE); break;
		case SDL_CONTROLLER_BUTTON_Y: if (down) g_actions.push_back(A_SEARCH); break;
		default: break;
		}
		break;
	}
	case SDL_CONTROLLERAXISMOTION:
		if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) stick(0, e.caxis.value);
		if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) stick(1, e.caxis.value);
		if (e.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) g_scrollStick = (float)e.caxis.value / 32767.0f;
		if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
		{
			// L2 / R2: one press each time the trigger is pulled past half-way
			static bool pulled[2] = { false, false };
			const int side = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT ? 1 : 0;
			const bool now = e.caxis.value > 16000;
			if (now && !pulled[side])
				g_actions.push_back(side ? A_TAB_NEXT : A_TAB_PREV);
			pulled[side] = now;
			break;
		}
		if (e.caxis.axis != SDL_CONTROLLER_AXIS_LEFTX && e.caxis.axis != SDL_CONTROLLER_AXIS_LEFTY && g_axisLogged < 4 && (e.caxis.value > 20000 || e.caxis.value < -20000))
		{
			g_axisLogged++;
			logLine("input: controller axis %d moved (right stick up/down is axis %d)", (int)e.caxis.axis, (int)SDL_CONTROLLER_AXIS_RIGHTY);
		}
		break;

	// A pad SDL has no layout for: a best guess, and the first presses are logged so it can be corrected.
	case SDL_JOYBUTTONDOWN:
		if (g_haveController)
			break;
		if (g_rawLogged++ < 40)
			logLine("input: raw joystick button %d", (int)e.jbutton.button);
		if (e.jbutton.button == 0) g_actions.push_back(A_OK);
		else if (e.jbutton.button == 1) g_actions.push_back(A_BACK);
		else if (e.jbutton.button == 4) g_actions.push_back(A_PAGE_UP);
		else if (e.jbutton.button == 5) g_actions.push_back(A_PAGE_DOWN);
		break;
	case SDL_JOYHATMOTION:
		if (g_haveController)
			break;
		g_padDir[0] = (e.jhat.value & SDL_HAT_UP) != 0;
		g_padDir[1] = (e.jhat.value & SDL_HAT_DOWN) != 0;
		g_padDir[2] = (e.jhat.value & SDL_HAT_LEFT) != 0;
		g_padDir[3] = (e.jhat.value & SDL_HAT_RIGHT) != 0;
		break;
	case SDL_JOYAXISMOTION:
		if (!g_haveController && e.jaxis.axis < 2)
			stick(e.jaxis.axis, e.jaxis.value);
		else if (!g_haveController && (e.jaxis.axis == 3 || e.jaxis.axis == 4))
			g_scrollStick = (float)e.jaxis.value / 32767.0f;
		break;

	case SDL_KEYDOWN:
	case SDL_KEYUP:
	{
		const bool down = e.type == SDL_KEYDOWN;
		switch (e.key.keysym.sym)
		{
		case SDLK_UP: g_keyDir[0] = down; break;
		case SDLK_DOWN: g_keyDir[1] = down; break;
		case SDLK_LEFT: g_keyDir[2] = down; break;
		case SDLK_RIGHT: g_keyDir[3] = down; break;
		case SDLK_RETURN: if (down && !e.key.repeat) g_actions.push_back(A_OK); break;
		case SDLK_ESCAPE:
		case SDLK_BACKSPACE: if (down && !e.key.repeat) g_actions.push_back(A_BACK); break;
		case SDLK_PAGEUP: if (down) g_actions.push_back(A_PAGE_UP); break;
		case SDLK_PAGEDOWN: if (down) g_actions.push_back(A_PAGE_DOWN); break;
		case SDLK_f: if (down && !e.key.repeat) g_actions.push_back(A_FAVOURITE); break;
		case SDLK_s: if (down && !e.key.repeat) g_actions.push_back(A_SEARCH); break;
		case SDLK_r: if (down && !e.key.repeat) g_actions.push_back(A_MENU); break;
		case SDLK_LEFTBRACKET: if (down && !e.key.repeat) g_actions.push_back(A_TAB_PREV); break;
		case SDLK_RIGHTBRACKET: if (down && !e.key.repeat) g_actions.push_back(A_TAB_NEXT); break;
		default: break;
		}
		break;
	}
	default:
		break;
	}
}

// ---- pointer ---------------------------------------------------------------
// The left stick moves a pointer; Cross clicks whatever is under it. Everything
// that can be clicked registers its area while it is being drawn.

struct HitArea { float x, y, w, h; std::function<void()> click; };
static std::vector<HitArea> g_hits;
static float g_pointerX = 960, g_pointerY = 540;
static double g_pointerUntil = 0;              // shown until this time

static bool pointerOn()
{
	return nowSeconds() < g_pointerUntil;
}

static bool hovering(float x, float y, float w, float h)
{
	return pointerOn() && g_pointerX >= x && g_pointerX < x + w && g_pointerY >= y && g_pointerY < y + h;
}

static void clickable(float x, float y, float w, float h, std::function<void()> click)
{
	g_hits.push_back({ x, y, w, h, std::move(click) });
}

static void movePointer(double seconds)
{
	const float length = sqrtf(g_stickX * g_stickX + g_stickY * g_stickY);
	if (length < 0.2f)
		return;
	// slow near the centre for small targets, fast at full tilt to cross the screen
	const float push = (length > 1.0f ? 1.0f : length) - 0.2f;
	const float level = (float)g_pointerSpeed;
	const float speed = 45.0f * level + 215.0f * level * (push / 0.8f) * (push / 0.8f);
	g_pointerX += g_stickX / length * speed * (float)seconds;
	g_pointerY += g_stickY / length * speed * (float)seconds;
	if (g_pointerX < 0) g_pointerX = 0;
	if (g_pointerX > 1919) g_pointerX = 1919;
	if (g_pointerY < 0) g_pointerY = 0;
	if (g_pointerY > 1079) g_pointerY = 1079;
	g_pointerUntil = nowSeconds() + 6;
}

// Clicks the topmost area under the pointer. Returns false if there is none.
static bool clickPointer()
{
	for (size_t i = g_hits.size(); i-- > 0;)
	{
		const HitArea& area = g_hits[i];
		if (g_pointerX >= area.x && g_pointerX < area.x + area.w && g_pointerY >= area.y && g_pointerY < area.y + area.h)
		{
			const std::function<void()> click = area.click; // the list may change while it runs
			g_pointerUntil = nowSeconds() + 6;
			if (click)
				click();
			return true;
		}
	}
	return false;
}

static void drawPointer()
{
	if (pointerOn())
		drawPointerShape(g_pointerType, g_pointerX, g_pointerY);
}

// Held directions repeat, slowly at first and then quickly, for long lists.
static void repeatDirections()
{
	static const Action actions[4] = { A_UP, A_DOWN, A_LEFT, A_RIGHT };
	const double now = nowSeconds();
	for (int i = 0; i < 4; i++)
	{
		const bool held = g_padDir[i] || g_stickDir[i] || g_keyDir[i];
		if (g_padDir[i] || g_keyDir[i])
			g_pointerUntil = 0; // using the direction buttons puts the pointer away
		if (held && !g_heldBefore[i])
		{
			g_actions.push_back(actions[i]);
			g_repeatAt[i] = now + 0.38;
		}
		else if (held && now >= g_repeatAt[i])
		{
			g_actions.push_back(actions[i]);
			g_repeatAt[i] = now + 0.055;
		}
		g_heldBefore[i] = held;
	}
}

// ---- the browser (laid out like IPTVnator) -------------------------------------
//
//   top bar:  playlist name and channel count | filter field
//   rail:     sections (all channels, groups, favourites, recently watched)
//   columns:  [categories, in the groups section] | channels | player and programme panel


enum Screen { LOADING, BROWSE, MANAGER };

static const char* const kSectionName[S_COUNT] = { "All channels", "Groups", "Favorites", "Recently watched" };
static const char* const kSectionIcon[S_COUNT] = { "\xE2\x96\xB6", "\xE2\x96\xA4", "\xE2\x99\xA5", "\xE2\x86\xBA" }; // play, list, heart, history

static Screen g_screen = LOADING;


static int g_playing = -1;                     // what is playing, as an entry of the current tab (-1: none of them)
static Channel g_now;                          // what is playing, whichever tab it came from
static bool g_nowOn = false, g_nowVod = false;
static int g_nowTab = 0, g_nowIndex = -1;
static bool g_fullscreen = false;
static double g_overlayUntil = 0, g_retryAt = 0;
static int g_retries = 0;
static bool g_gotPicture = false;

static bool g_railOnPlaylists = false;         // the rail's last button, which opens the playlist manager
static bool g_haveBrowse = false;              // a playlist is loaded and can be browsed

// on-screen keyboard (defined further down)
static bool g_keyboard = false;
static void openKeyboard(std::string* target, size_t limit, bool isFilter, const std::string& label);
static void openManager(const std::string& notice);
static void reloadPlaylist();
static void cancelLoading();

// Favourites, recently watched and the last channel are kept per playlist.
static std::string listPath(const char* kind)
{
	static const char* const part[3] = { "", "-movies", "-series" };
	return g_dataDir + "/" + kind + part[T - g_tabs] + "-" + g_activeId + ".txt";
}

static const int kChannelRows = 11, kCategoryRows = 16;
static const float kRowH = 76, kCatRowH = 53, kColumnTop = 146, kListTop = 212;

static std::string lower(const std::string& text)
{
	std::string out = text;
	for (char& c : out)
		if (c >= 'A' && c <= 'Z')
			c = (char)(c + 32);
	return out;
}

// ---- lists, categories fetched on demand ------------------------------------------
// For a "lazy" library only the category names are known at first. A category's entries are
// fetched in the background when it is opened, and join the lists as they arrive.

static std::mutex g_genreMutex;
static std::atomic<int> g_genreRequest{ 0 };           // changes when the fetch in progress is no longer wanted
static std::vector<Channel> g_genreArrived;            // entries fetched but not yet in the lists
static int g_genreFinished = 0;                        // 0 running, 1 done, 2 failed
static std::string g_genreError, g_genreFailure;
static int g_genreLoading = -1;                        // the category being fetched, or -1
static const std::vector<int> kNothing;

static std::string g_toast;                            // a short message at the foot of the screen
static double g_toastUntil = 0;

static void toast(const std::string& text)
{
	g_toast = text;
	g_toastUntil = nowSeconds() + 4;
}

// Adds an entry to the current library (or finds it, if already known). Returns its number.
// Episodes are kept out of the "All" list: they belong to their series.
static int addChannel(const Channel& channel, bool listInAll = true)
{
	const auto found = g_byUrl.find(channel.url);
	if (found != g_byUrl.end())
		return found->second;
	const int index = (int)g_playlist.channels.size();
	g_playlist.channels.push_back(channel);
	g_isFavourite.push_back(0);
	if (listInAll)
		g_groupChannels[0].push_back(index);
	g_byUrl[channel.url] = index;
	return index;
}

static void addToGroup(int index, int groupPlace)
{
	std::vector<int>& list = g_groupChannels[(size_t)groupPlace];
	for (const int existing : list)
		if (existing == index)
			return;
	list.push_back(index);
}

// Favourites and recently watched are saved with their names, so they work before the
// category they came from has been opened.
static void saveList(const char* file, const std::vector<int>& list)
{
	std::vector<Channel> channels;
	for (const int channel : list)
		channels.push_back(g_playlist.channels[(size_t)channel]);
	writeTextFile(listPath(file), channelsToM3U(channels));
}

static void loadList(const char* file, std::vector<int>& list)
{
	list.clear();
	std::string text;
	Playlist saved;
	if (!readTextFile(listPath(file), text) || !parseM3U(text, saved))
		return;
	for (const Channel& channel : saved.channels)
	{
		const auto found = g_byUrl.find(channel.url);
		if (found != g_byUrl.end())
			list.push_back(found->second);
		else if (g_playlist.lazy)
		{
			// its category has not been opened yet: the saved copy stands in
			const auto group = g_groupIndex.find(channel.group);
			const int index = addChannel(channel, group != g_groupIndex.end());
			if (group != g_groupIndex.end())
				addToGroup(index, group->second);
			list.push_back(index);
		}
	}
}

// ---- hidden categories ---------------------------------------------------------------

static void saveHidden()
{
	std::string text;
	for (const std::string& name : g_playlist.groups)
		if (T->hidden.count(name))
			text += name + "\n";
	writeTextFile(listPath("hidden"), text);
}

static void loadHidden()
{
	T->hidden.clear();
	std::string text;
	if (!readTextFile(listPath("hidden"), text))
		return;
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t end = text.find('\n', pos);
		if (end == std::string::npos)
			end = text.size();
		const std::string name = trim(text.substr(pos, end - pos));
		if (!name.empty())
			T->hidden.insert(name);
		pos = end + 1;
	}
}

// Works out which categories are shown: not hidden, and matching the filter when the filter
// is being applied to category names.
static void computeCats()
{
	T->cats.clear();
	const std::string needle = T->filterCats ? lower(g_filter) : std::string();
	for (int i = 0; i < (int)g_playlist.groups.size(); i++)
	{
		const std::string& name = g_playlist.groups[(size_t)i];
		if (T->hidden.count(name))
			continue;
		if (!needle.empty() && lower(name).find(needle) == std::string::npos)
			continue;
		T->cats.push_back(i);
	}
	bool found = false;
	for (const int shown : T->cats)
		if (shown == g_catSel)
			found = true;
	if (!found)
	{
		g_catSel = T->cats.empty() ? 0 : T->cats[0];
		g_catTop = 0;
	}
}

static void rebuildView(int keepChannel);
static int selectedChannel();

static void stopGenreLoading()
{
	std::lock_guard<std::mutex> lock(g_genreMutex);
	g_genreRequest++;
	g_genreArrived.clear();
	g_genreFinished = 0;
	if (g_genreLoading >= 0 && g_genreLoading < (int)g_groupState.size() && g_groupState[(size_t)g_genreLoading] == 1)
		g_groupState[(size_t)g_genreLoading] = 0;
	g_genreLoading = -1;
}

static void startGenreLoading(int category)
{
	stopGenreLoading();
	g_groupState[(size_t)category] = 1;
	g_genreLoading = category;
	const int request = g_genreRequest.load();
	const int media = (int)(T - g_tabs);
	const Source source = g_sources[(size_t)g_active];
	const std::string id = g_playlist.groupIds[(size_t)category], group = g_playlist.groups[(size_t)category];
	logLine("category: fetching \"%s\"", group.c_str());
	startThread("category", [source, media, id, group, request] {
		std::string error;
		const bool ok = fetchCategory(source, media, id, [&](std::vector<Channel>& batch) {
			std::lock_guard<std::mutex> lock(g_genreMutex);
			if (request != g_genreRequest.load())
				return false;
			for (Channel& channel : batch)
			{
				channel.group = group;
				g_genreArrived.push_back(std::move(channel));
			}
			return true;
		}, error);
		std::lock_guard<std::mutex> lock(g_genreMutex);
		if (request != g_genreRequest.load())
			return;
		g_genreFinished = ok ? 1 : 2;
		g_genreError = error;
	});
}

// Called every frame while browsing: starts fetching the open category, and files what has arrived.
static void pumpGenres()
{
	if (!g_playlist.lazy || g_playlist.groups.empty() || T->state != 2)
		return;

	std::vector<Channel> arrived;
	int finished = 0;
	std::string error;
	{
		std::lock_guard<std::mutex> lock(g_genreMutex);
		arrived.swap(g_genreArrived);
		finished = g_genreFinished;
		error = g_genreError;
		if (finished)
			g_genreFinished = 0;
	}
	if (!arrived.empty() && g_genreLoading >= 0)
	{
		const int keep = selectedChannel();
		const int top = g_top, selected = g_sel;
		for (const Channel& channel : arrived)
			addToGroup(addChannel(channel), g_genreLoading + 1);
		const bool showing = !T->drill && ((inGroups() && g_catSel == g_genreLoading) || g_section == S_ALL);
		if (showing)
		{
			rebuildView(keep);
			if (keep < 0)
				g_sel = selected;
			g_top = top;                          // the list grows without jumping
		}
	}
	if (finished && g_genreLoading >= 0)
	{
		g_groupState[(size_t)g_genreLoading] = finished == 1 ? 2 : 3;
		if (finished == 2)
		{
			g_genreFailure = error;
			logLine("category: failed: %s", error.c_str());
		}
		else
			logLine("category: \"%s\" has %d entries", g_playlist.groups[(size_t)g_genreLoading].c_str(), (int)g_groupChannels[(size_t)g_genreLoading + 1].size());
		g_genreLoading = -1;
	}

	// Fetch the open category once the selection has rested on it for a moment
	// (so scrolling through the list does not fetch every category passed).
	static int restingOn = -1;
	static const Tab* restingTab = nullptr;
	static double restingSince = 0;
	if (!inGroups())
		return;
	if (g_catSel != restingOn || restingTab != T)
	{
		restingOn = g_catSel;
		restingTab = T;
		restingSince = nowSeconds();
	}
	if (g_groupState[(size_t)g_catSel] == 0 && g_genreLoading != g_catSel && nowSeconds() - restingSince > 0.45)
		startGenreLoading(g_catSel);
}

// Fills the middle column for the current section, category and filter.
static void rebuildView(int keepChannel)
{
	const std::vector<int>* base = &kNothing;
	if (T->drill)
		base = &T->drillList;
	else if (g_groupChannels.empty())
		base = &kNothing;
	else if (g_section == S_GROUPS)
		base = T->cats.empty() ? &kNothing : &g_groupChannels[(size_t)g_catSel + 1];
	else if (g_section == S_FAVOURITES)
		base = &g_favourites;
	else if (g_section == S_RECENT)
		base = &g_recent;
	else
		base = &g_groupChannels[0];

	const bool skipHidden = !T->drill && g_section == S_ALL && !T->hidden.empty();
	const std::string needle = T->filterCats ? std::string() : lower(g_filter);
	g_view.clear();
	if (!skipHidden && needle.empty())
		g_view = *base;
	else
		for (const int channel : *base)
		{
			const Channel& info = g_playlist.channels[(size_t)channel];
			if (skipHidden && T->hidden.count(info.group))
				continue;
			if (!needle.empty() && lower(info.name).find(needle) == std::string::npos)
				continue;
			g_view.push_back(channel);
		}
	g_sel = 0;
	g_top = 0;
	for (int i = 0; i < (int)g_view.size(); i++)
	{
		if (g_view[i] == keepChannel)
		{
			g_sel = i;
			break;
		}
	}
}

static int selectedChannel()
{
	return g_sel >= 0 && g_sel < (int)g_view.size() ? g_view[g_sel] : -1;
}

// ---- a series and its episodes ---------------------------------------------------------

static std::mutex g_drillMutex;
static int g_drillRequest = 0, g_drillDone = 0;        // done: 1 listed, 2 failed
static std::vector<Channel> g_drillArrived;
static std::string g_drillFailure;

static void closeDrill()
{
	if (!T->drill)
		return;
	{
		std::lock_guard<std::mutex> lock(g_drillMutex);
		g_drillRequest++;
		g_drillDone = 0;
	}
	T->drill = false;
	T->drillState = 0;
	T->drillList.clear();
	rebuildView(T->drillItem);
}

static void openSeries(int index)
{
	const Channel series = g_playlist.channels[(size_t)index];
	T->drill = true;
	T->drillState = 1;
	T->drillItem = index;
	T->drillTitle = series.name;
	T->drillError.clear();
	T->drillList.clear();
	g_focus = F_CHANNELS;
	rebuildView(-1);
	int request;
	{
		std::lock_guard<std::mutex> lock(g_drillMutex);
		request = ++g_drillRequest;
		g_drillDone = 0;
	}
	const Source source = g_sources[(size_t)g_active];
	logLine("series: opening \"%s\"", series.name.c_str());
	startThread("series", [source, series, request] {
		std::vector<Channel> episodes;
		std::string error;
		const bool ok = fetchEpisodes(source, series.url, episodes, error);
		std::lock_guard<std::mutex> lock(g_drillMutex);
		if (request != g_drillRequest)
			return;
		g_drillArrived = std::move(episodes);
		g_drillFailure = error;
		g_drillDone = ok ? 1 : 2;
	});
}

static void pumpDrill()
{
	if (!T->drill || T->drillState != 1)
		return;
	std::vector<Channel> episodes;
	int done;
	std::string error;
	{
		std::lock_guard<std::mutex> lock(g_drillMutex);
		done = g_drillDone;
		if (!done)
			return;
		g_drillDone = 0;
		episodes.swap(g_drillArrived);
		error = g_drillFailure;
	}
	if (done == 2)
	{
		T->drillState = 3;
		T->drillError = error;
		return;
	}
	for (Channel& episode : episodes)
	{
		episode.group = T->drillTitle;
		T->drillList.push_back(addChannel(episode, false));
	}
	T->drillState = 2;
	rebuildView(-1);
}

// ---- libraries: setting one up, and loading movies and series when their tab is opened ---

// Prepares the current tab once its playlist is in place.
static void initTab()
{
	g_byUrl.clear();
	for (int i = (int)g_playlist.channels.size() - 1; i >= 0; i--)
		g_byUrl[g_playlist.channels[(size_t)i].url] = i;
	g_groupState.assign(g_playlist.groups.size(), g_playlist.lazy ? 0 : 2);
	g_isFavourite.assign(g_playlist.channels.size(), 0);
	loadHidden();
	loadList("favourites", g_favourites);
	loadList("recent", g_recent);
	for (const int channel : g_favourites)
		g_isFavourite[(size_t)channel] = 1;

	// A library that is fetched a category at a time opens on its categories.
	g_section = g_playlist.lazy ? S_GROUPS : S_ALL;
	g_focus = g_playlist.lazy ? F_CATEGORIES : F_CHANNELS;
	g_catSel = g_catTop = 0;
	g_filter.clear();
	T->filterCats = false;
	T->drill = false;
	T->drillState = 0;
	computeCats();
	T->state = 2;
}

static std::mutex g_libMutex;
static int g_libRequest = 0, g_libKind = -1, g_libDone = 0;
static Playlist g_libArrived;
static std::string g_libError;

static void startLibrary(int kind)
{
	if (g_libKind > 0 && g_libKind != kind && g_tabs[g_libKind].state == 1)
		g_tabs[g_libKind].state = 0;                   // its load is being replaced by this one
	g_tabs[kind].state = 1;
	g_tabs[kind].error.clear();
	int request;
	{
		std::lock_guard<std::mutex> lock(g_libMutex);
		request = ++g_libRequest;
		g_libDone = 0;
	}
	g_libKind = kind;
	const Source source = g_sources[(size_t)g_active];
	logLine("library: loading %s", kTabName[kind]);
	startThread("library", [source, kind, request] {
		Playlist fresh;
		std::string error;
		const bool ok = fetchLibrary(source, kind, g_dataDir, fresh, error);
		std::lock_guard<std::mutex> lock(g_libMutex);
		if (request != g_libRequest)
			return;
		g_libArrived = std::move(fresh);
		g_libError = error;
		g_libDone = ok ? 1 : 2;
	});
}

static void buildGroups();

static void pumpLibrary()
{
	Playlist fresh;
	std::string error;
	int done;
	{
		std::lock_guard<std::mutex> lock(g_libMutex);
		done = g_libDone;
		if (!done)
			return;
		g_libDone = 0;
		fresh = std::move(g_libArrived);
		g_libArrived = Playlist();
		error = g_libError;
	}
	Tab* const showing = T;
	T = &g_tabs[g_libKind];
	if (done == 1)
	{
		g_playlist = std::move(fresh);
		buildGroups();
		initTab();
		rebuildView(-1);
	}
	else
	{
		T->state = 3;
		T->error = error;
		logLine("library: %s", error.c_str());
	}
	T = showing;
}

static std::mutex g_accountMutex;
static int g_accountState = 0, g_accountRequest = 0;   // state: 1 fetching, 2 shown, 3 failed
static std::vector<std::pair<std::string, std::string>> g_accountRows;
static std::string g_accountError;

static void startAccount()
{
	int request;
	{
		std::lock_guard<std::mutex> lock(g_accountMutex);
		request = ++g_accountRequest;
		g_accountState = 1;
	}
	const Source source = g_sources[(size_t)g_active];
	startThread("account", [source, request] {
		std::vector<std::pair<std::string, std::string>> rows;
		std::string error;
		const bool ok = fetchAccount(source, rows, error);
		std::lock_guard<std::mutex> lock(g_accountMutex);
		if (request != g_accountRequest)
			return;
		g_accountRows = std::move(rows);
		g_accountError = error;
		g_accountState = ok ? 2 : 3;
	});
}

static void switchTab(int tab)
{
	tab = (tab + kTabCount) % kTabCount;
	if (tab == g_tab)
		return;
	stopGenreLoading();
	if (T->drill && T->drillState == 1)
		closeDrill();
	g_tab = tab;
	if (tab == kAccountTab)
	{
		if (g_accountState == 0 || g_accountState == 3)
			startAccount();
		return;
	}
	T = &g_tabs[tab];
	g_playing = g_nowOn && g_nowTab == tab ? g_nowIndex : -1;
	if (tab > 0 && (T->state == 0 || T->state == 3 || (T->state == 1 && g_libKind != tab)))
		startLibrary(tab);
}

// Called when a playlist has finished loading: live TV is in tab 0, the others start empty.
static void onPlaylistLoaded()
{
	initTab();
	g_keyboard = false;
	g_playing = -1;
	g_nowOn = false;
	g_fullscreen = false;
	g_railOnPlaylists = false;
	g_haveBrowse = true;
	{
		std::lock_guard<std::mutex> lock(g_accountMutex);
		g_accountRequest++;
		g_accountState = 0;
		g_accountRows.clear();
	}

	// Start on the channel watched last time, if it is still in the playlist.
	int last = -1;
	std::string url;
	if (readTextFile(listPath("last"), url))
	{
		const auto found = g_byUrl.find(trim(url));
		if (found != g_byUrl.end())
			last = found->second;
	}
	rebuildView(last);
}

static void toggleFavourite(int channel)
{
	if (channel < 0)
		return;
	if (g_isFavourite[channel])
	{
		g_isFavourite[channel] = 0;
		for (size_t i = 0; i < g_favourites.size(); i++)
			if (g_favourites[i] == channel)
			{
				g_favourites.erase(g_favourites.begin() + (long)i);
				break;
			}
	}
	else
	{
		g_isFavourite[channel] = 1;
		g_favourites.push_back(channel);
	}
	saveList("favourites", g_favourites);
	if (g_section == S_FAVOURITES)
	{
		const int position = g_sel;
		rebuildView(-1);
		g_sel = position < (int)g_view.size() ? position : (int)g_view.size() - 1;
		if (g_sel < 0)
			g_sel = 0;
	}
}

// A Stalker channel's real address has to be asked for from the portal first, in the background.
static std::mutex g_linkMutex;
static int g_linkRequest = 0, g_linkState = 0;        // state: 0 none, 1 asking, 2 ready, 3 failed
static std::string g_linkUrl, g_linkError;

static void cancelLink()
{
	std::lock_guard<std::mutex> lock(g_linkMutex);
	g_linkRequest++;
	g_linkState = 0;
}

static std::string userAgentFor(const Channel& channel)
{
	const Source& source = g_sources[(size_t)g_active];
	if (!channel.userAgent.empty())
		return channel.userAgent;
	if (!source.ua.empty())
		return source.ua;
	return source.type == "stalker" || !source.mac.empty() ? kStalkerUserAgent : "";
}

static void beginPlayback(const Channel& channel)
{
	if (channel.url.compare(0, 10, "stalker://") != 0)
	{
		cancelLink();
		player::play(channel.url, userAgentFor(channel), channel.referer);
		return;
	}
	player::stop();
	int request;
	{
		std::lock_guard<std::mutex> lock(g_linkMutex);
		request = ++g_linkRequest;
		g_linkState = 1;
	}
	const Source source = g_sources[(size_t)g_active];
	const std::string command = channel.url.substr(10);
	startThread("link", [source, command, request] {
		std::string url, error;
		const bool ok = stalkerResolve(source, command, url, error);
		std::lock_guard<std::mutex> lock(g_linkMutex);
		if (request != g_linkRequest)
			return;                                   // another channel was chosen meanwhile
		g_linkState = ok ? 2 : 3;
		g_linkUrl = url;
		g_linkError = error;
	});
}

static void startChannel(int channel)
{
	if (channel < 0)
		return;
	const Channel info = g_playlist.channels[(size_t)channel];
	logLine("watch: %s", info.name.c_str());
	g_playing = channel;
	g_now = info;
	g_nowOn = true;
	g_nowVod = false;
	g_nowTab = g_tab;
	g_nowIndex = channel;
	gfx::videoClear();
	g_gotPicture = false;
	g_retries = 0;
	g_retryAt = 0;
	g_overlayUntil = nowSeconds() + 6;
	beginPlayback(info);
	writeTextFile(listPath("last"), info.url);

	for (size_t i = 0; i < g_recent.size(); i++)
		if (g_recent[i] == channel)
		{
			g_recent.erase(g_recent.begin() + (long)i);
			break;
		}
	g_recent.insert(g_recent.begin(), channel);
	if (g_recent.size() > 60)
		g_recent.resize(60);
	saveList("recent", g_recent);
}

static double g_uploadSeconds = 0;              // time spent handing pictures to the graphics chip, for the log
static int g_uploads = 0;

// Keeps the stream going (retrying a few times when it drops) and returns what to tell the viewer.
static std::string updatePlayback()
{
	if (!g_nowOn)
		return "";
	const Channel& channel = g_now;
	const double now = nowSeconds();

	player::Picture picture;
	if (player::nextPicture(picture))
	{
		const double before = nowSeconds();
		gfx::videoUpload(picture.planes, picture.linesize, picture.width, picture.height, picture.fullRange, picture.matrix, picture.transfer);
		g_uploadSeconds += nowSeconds() - before;
		g_uploads++;
		g_gotPicture = true;
	}
	if (player::duration() > 0)
		g_nowVod = true;                           // a film or an episode: it has a length

	int linkState;
	std::string linkUrl, linkError;
	{
		std::lock_guard<std::mutex> lock(g_linkMutex);
		linkState = g_linkState;
		linkUrl = g_linkUrl;
		linkError = g_linkError;
		if (linkState == 2)
			g_linkState = 0;
	}
	if (linkState == 1)
		return "Connecting...";
	if (linkState == 2)
	{
		player::play(linkUrl, userAgentFor(channel), "");
		return "Connecting...";
	}

	const player::State state = player::state();
	if (state == player::ENDED && g_nowVod)
		return "Finished";                         // a film that reached its end is not restarted
	if (linkState == 3 || state == player::FAILED || state == player::ENDED)
	{
		if (g_retryAt == 0)
			g_retryAt = now + 3;
		if (g_retries < 5 && now >= g_retryAt)
		{
			g_retries++;
			g_retryAt = 0;
			logLine("watch: retry %d", g_retries);
			beginPlayback(channel);
		}
		std::string status = linkState == 3 ? linkError : state == player::FAILED ? player::message() : "The stream ended";
		if (g_retries < 5)
			status += "  (trying again)";
		return status;
	}
	if (state == player::OPENING)
		return "Connecting...";
	if (state == player::PLAYING && player::paused())
		return "Paused";
	if (state == player::PLAYING && !player::hasVideo())
		return "Sound only";
	if (state == player::PLAYING && !g_gotPicture)
		return "Loading...";
	return "";
}

static void keepVisible(int selected, int& top, int count, int rows)
{
	if (selected < top) top = selected;
	if (selected >= top + rows) top = selected - rows + 1;
	if (top > count - rows) top = count - rows;
	if (top < 0) top = 0;
}

static void moveSelection(int& selected, int count, int by)
{
	if (count <= 0)
	{
		selected = 0;
		return;
	}
	if (by == 1 && selected == count - 1) selected = 0;          // wrap around at the ends
	else if (by == -1 && selected == 0) selected = count - 1;
	else
	{
		selected += by;
		if (selected < 0) selected = 0;
		if (selected > count - 1) selected = count - 1;
	}
}

static void centredIn(float x, float w, float y, float size, uint32_t colour, const std::string& text)
{
	const float width = gfx::textWidth(size, text);
	gfx::text(width > w - 24 ? x + 12 : x + (w - width) / 2, y, size, colour, text, w - 24);
}

// Centred with no width limit: for icons, initials and labels on buttons.
static void centredTight(float x, float w, float y, float size, uint32_t colour, const std::string& text)
{
	gfx::text(x + (w - gfx::textWidth(size, text)) / 2, y, size, colour, text);
}

// Two letters to stand in for a channel logo, as IPTVnator shows when there is none.
static std::string initials(const std::string& name)
{
	std::string out;
	bool wordStart = true;
	for (const char c : name)
	{
		const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
		if (letter && wordStart && out.size() < 2)
			out += (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
		wordStart = !letter;
	}
	return out.empty() ? "TV" : out;
}

static void drawScrollbar(float x, float y, float height, int top, int count, int rows)
{
	if (count <= rows)
		return;
	float size = height * (float)rows / (float)count;
	if (size < 28)
		size = 28;
	gfx::roundRect(x, y + (height - size) * (float)top / (float)(count - rows), 5, size, 2, kScroll);
}

static const char* const kNoun[3] = { "channels", "movies", "series" };

static std::string sectionName(int section)
{
	static const char* const all[3] = { "All channels", "All movies", "All series" };
	return section == S_ALL ? all[T - g_tabs] : kSectionName[section];
}

// "1:02:45" or "12:07"
static std::string clockText(double seconds)
{
	const int total = seconds < 0 ? 0 : (int)seconds;
	char text[32];
	if (total >= 3600)
		snprintf(text, sizeof(text), "%d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
	else
		snprintf(text, sizeof(text), "%d:%02d", total / 60, total % 60);
	return text;
}

static void leaveFilter()
{
	if (!T->filterCats)
		g_filter.clear();                          // a filter on entries belongs to the list it was typed for
}

// The filter applies to whichever column is in use: category names, or the entries.
static void openFilter()
{
	const bool forCategories = g_tab != kAccountTab && inGroups() && g_focus == F_CATEGORIES && !T->drill;
	if (forCategories != T->filterCats)
	{
		g_filter.clear();
		T->filterCats = forCategories;
		computeCats();
		rebuildView(selectedChannel());
	}
	openKeyboard(&g_filter, 40, true, forCategories ? "Filter categories" : "Filter");
}

static void applyFilter(int keepChannel)
{
	const int category = g_catSel;
	computeCats();
	if (g_catSel != category)
		keepChannel = -1;
	rebuildView(keepChannel);
}

static void drawTopBar()
{
	// playlist chip: opens Settings
	const bool chipHover = hovering(96, 14, 380, 60);
	gfx::panel(96, 14, 380, 60, 14, chipHover ? 2.0f : 1.0f, chipHover ? kBorder : kLine, kChip);
	clickable(96, 14, 380, 60, [] { openManager(""); });
	gfx::roundRect(106, 24, 40, 40, 10, kTile);
	centredTight(106, 40, 30, 24, kText, "\xE2\x98\xB0");
	const Source& source = g_sources[(size_t)g_active];
	char count[96];
	if (g_playlist.lazy)
		snprintf(count, sizeof(count), "%d categories  \xC2\xB7  %s", (int)g_playlist.groups.size(), sourceKind(source));
	else
		snprintf(count, sizeof(count), "%d %s  \xC2\xB7  %s", (int)g_playlist.channels.size(), kNoun[T - g_tabs], sourceKind(source));
	gfx::text(160, 20, 26, kText, source.name, 300);
	gfx::text(160, 49, 19, kMuted, count, 300);

	// filter field
	const bool active = g_keyboard || hovering(496, 14, 1000, 60);
	gfx::panel(496, 14, 1000, 60, 14, active ? 2.0f : 1.0f, active ? kBorder : kLine, kChip);
	clickable(496, 14, 1000, 60, [] { openFilter(); });
	std::string label = sectionName(g_section);
	if (T->filterCats)
		label = "Categories";
	else if (T->drill)
		label = T->drillTitle;
	else if (inGroups())
		label = "Groups / " + g_playlist.groups[(size_t)g_catSel];
	const float w = gfx::text(520, 30, 24, kText, label, 420);
	if (g_filter.empty())
		gfx::text(520 + w + 26, 30, 24, kMuted, T->filterCats ? "Filter the categories..." : "Filter this section...");
	else
		gfx::text(520 + w + 26, 30, 24, kAccent, g_filter, 460);
	gfx::text(1444, 30, 24, kMuted, "\xE2\x96\xB3");                    // triangle button

	const bool reloadHover = hovering(1516, 14, 170, 60);
	gfx::panel(1516, 14, 170, 60, 14, reloadHover ? 2.0f : 1.0f, reloadHover ? kBorder : kLine, kChip);
	clickable(1516, 14, 170, 60, [] { reloadPlaylist(); });
	gfx::text(1534, 24, 34, kText, "\xE2\x86\xBB");                     // reload
	gfx::text(1580, 31, 24, kText, "Reload");

	const bool settingsHover = hovering(1702, 14, 190, 60);
	gfx::panel(1702, 14, 190, 60, 14, settingsHover ? 2.0f : 1.0f, settingsHover ? kBorder : kLine, kChip);
	clickable(1702, 14, 190, 60, [] { openManager(""); });
	gfx::text(1720, 22, 34, kText, "\xE2\x9A\x99");                     // gear
	gfx::text(1768, 31, 24, kText, "Settings");
}

// Live TV, Movies, Series, Account
static void drawTabs()
{
	for (int tab = 0; tab < kTabCount; tab++)
	{
		const float x = 96 + (float)tab * 202, w = 190;
		const bool current = tab == g_tab, hover = hovering(x, 86, w, 48);
		gfx::panel(x, 86, w, 48, 24, current || hover ? 2.0f : 1.0f, current || hover ? kBorder : kLine, current ? kSelected : kChip);
		centredTight(x, w, 97, 23, current ? kAccent : kText, kTabName[tab]);
		clickable(x, 86, w, 48, [tab] { switchTab(tab); });
	}
	gfx::text(96 + (float)kTabCount * 202 + 14, 100, 20, kMuted, "L2 / R2  Switch");
}

static void chooseSection(int section)
{
	g_railOnPlaylists = false;
	closeDrill();
	if (section != (int)g_section)
	{
		g_section = (Section)section;
		g_filter.clear();
		T->filterCats = false;
		computeCats();
		rebuildView(g_playing);
	}
}

static void drawRail()
{
	const float top = kColumnTop;
	gfx::roundRect(10, top, 68, (float)S_COUNT * 76 + 8, 16, kRail);
	for (int i = 0; i < S_COUNT; i++)
	{
		const float y = top + 8 + (float)i * 76;
		const bool current = i == (int)g_section;
		if (current)
			gfx::panel(14, y, 60, 60, 14, 2, g_focus == F_RAIL && !g_railOnPlaylists ? kBorder : kBorderDim, kSelected);
		else if (hovering(14, y, 60, 60))
			gfx::roundRect(14, y, 60, 60, 14, kHover);
		centredTight(14, 60, y + 13, 30, current ? kAccent : kText, kSectionIcon[i]);
		clickable(14, y, 60, 60, [i] { chooseSection(i); g_focus = F_CHANNELS; });
	}

	// settings, at the foot of the rail
	const float py = 1004;
	const bool onIt = g_focus == F_RAIL && g_railOnPlaylists;
	if (onIt || hovering(14, py, 60, 60))
		gfx::panel(14, py, 60, 60, 14, 2, kBorder, kSelected);
	else
		gfx::roundRect(14, py, 60, 60, 14, kRail);
	centredTight(14, 60, py + 11, 34, kText, "\xE2\x9A\x99");
	clickable(14, py, 60, 60, [] { openManager(""); });

	if (g_focus == F_RAIL)
	{
		// the name of the choice, next to the rail
		const std::string name = g_railOnPlaylists ? "Settings" : sectionName(g_section);
		const float y = g_railOnPlaylists ? py : top + 8 + (float)g_section * 76;
		const float w = gfx::textWidth(24, name) + 36;
		gfx::panel(84, y + 8, w, 44, 10, 1, kBorder, kDeep);
		gfx::text(102, y + 16, 24, kText, name);
	}
}

static void selectCategory(int category)
{
	if (category == g_catSel)
		return;
	closeDrill();
	g_catSel = category;
	leaveFilter();
	rebuildView(g_playing);
}

static void hideCategory(int category)
{
	const std::string name = g_playlist.groups[(size_t)category];
	const int place = catPlace();
	T->hidden.insert(name);
	saveHidden();
	closeDrill();
	computeCats();
	if (!T->cats.empty())
		g_catSel = T->cats[(size_t)(place < (int)T->cats.size() ? place : (int)T->cats.size() - 1)];
	rebuildView(g_playing);
	toast("Hidden: " + name + "   (bring it back in Settings > Groups)");
}

static void drawCategories(float x, float w)
{
	static const char* const heading[3] = { "Live Categories", "Movie Categories", "Series Categories" };
	gfx::rect(x, kColumnTop, w, 1080 - kColumnTop, kPane);
	gfx::text(x + 22, kColumnTop + 18, 28, kText, heading[T - g_tabs]);
	const int count = (int)T->cats.size();
	if (T->filterCats || !T->hidden.empty())
	{
		char shown[48];
		snprintf(shown, sizeof(shown), "%d of %d", count, (int)g_playlist.groups.size());
		gfx::text(x + w - 28 - gfx::textWidth(20, shown), kColumnTop + 24, 20, kMuted, shown);
	}
	if (count == 0)
	{
		centredIn(x, w, 420, 24, kMuted, T->filterCats && !g_filter.empty() ? "No category matches" : "Every category is hidden");
		if (!T->hidden.empty())
			centredIn(x, w, 458, 20, kMuted, "Settings > Groups brings them back");
		return;
	}
	const int place = catPlace();
	keepVisible(place, g_catTop, count, kCategoryRows);
	for (int row = 0; row < kCategoryRows; row++)
	{
		const int at = g_catTop + row;
		if (at >= count)
			break;
		const int i = T->cats[(size_t)at];
		const float y = kListTop + (float)row * kCatRowH;
		const bool selected = i == g_catSel;
		if (selected)
			gfx::panel(x + 12, y, w - 24, kCatRowH - 5, 10, 2, g_focus == F_CATEGORIES ? kBorder : kBorderDim, kSelected);
		else if (hovering(x + 12, y, w - 24, kCatRowH - 5))
			gfx::roundRect(x + 12, y, w - 24, kCatRowH - 5, 10, kHover);
		clickable(x + 12, y, w - 24, kCatRowH - 5, [i] {
			g_focus = F_CATEGORIES;
			selectCategory(i);
		});
		char number[16];
		const char state = g_groupState[(size_t)i];
		if (state == 2)
			snprintf(number, sizeof(number), "%d", (int)g_groupChannels[(size_t)i + 1].size());
		else
			snprintf(number, sizeof(number), "%s", state == 1 ? "..." : state == 3 ? "!" : "");
		const float nw = gfx::textWidth(22, number);
		gfx::text(x + w - 28 - nw, y + 12, 22, kMuted, number);
		gfx::text(x + 26, y + 9, 26, selected ? kAccent : kText, g_playlist.groups[(size_t)i], w - 80 - nw);
	}
	drawScrollbar(x + w - 8, kListTop, (float)kCategoryRows * kCatRowH, g_catTop, count, kCategoryRows);
}

static void showControlsSoon();
static void drawSubtitles(float x, float w, float bottom, float size);

// Plays the selected entry (a second time on the same one fills the screen); a series opens its episodes.
static void activateSelected()
{
	const int channel = selectedChannel();
	if (channel < 0)
		return;
	if (isSeriesItem(g_playlist.channels[(size_t)channel].url))
	{
		openSeries(channel);
		return;
	}
	if (channel == g_playing)
	{
		g_fullscreen = true;
		g_overlayUntil = 0;
		showControlsSoon();
	}
	else
		startChannel(channel);
}

static void drawChannels(float x, float w)
{
	const int media = (int)(T - g_tabs);
	gfx::rect(x, kColumnTop, w, 1080 - kColumnTop, kColumn);
	std::string title = sectionName(g_section);
	if (T->drill)
	{
		gfx::rect(x, kColumnTop, w, 58, kBand);
		title = "\xE2\x80\xB9  " + T->drillTitle;
		clickable(x, kColumnTop, w, 58, [] { closeDrill(); });
	}
	else if (inGroups())
	{
		gfx::rect(x, kColumnTop, w, 58, kBand);
		title = g_playlist.groups[(size_t)g_catSel];
	}
	const char categoryState = T->drill ? (T->drillState == 2 ? 2 : T->drillState) : inGroups() ? g_groupState[(size_t)g_catSel] : 2;
	char count[32];
	snprintf(count, sizeof(count), categoryState == 1 ? "%d..." : "%d", (int)g_view.size());
	const float cw = gfx::textWidth(22, count);
	gfx::text(x + w - 26 - cw, kColumnTop + 20, 22, kMuted, count);
	gfx::text(x + 22, kColumnTop + 15, 28, kText, title, w - 80 - cw);

	if (T->state != 2)
	{
		// the library itself (movies or series) is not here yet
		if (T->state == 3)
		{
			centredIn(x, w, 400, 26, kLive, std::string("No ") + kNoun[media] + " to show");
			centredIn(x, w, 442, 20, kMuted, T->error);
		}
		else
			centredIn(x, w, 420, 26, kMuted, "Loading...");
		return;
	}
	const bool filtering = !g_filter.empty() && !T->filterCats;
	if (g_view.empty() && !filtering && categoryState != 2)
	{
		if (categoryState == 3)
		{
			centredIn(x, w, 400, 26, kLive, T->drill ? "The episodes could not be loaded" : "This category could not be loaded");
			centredIn(x, w, 442, 20, kMuted, T->drill ? T->drillError : g_genreFailure);
			centredIn(x, w, 492, 22, kMuted, T->drill ? "Press \xE2\x97\x8B to go back" : "Press \xE2\x9C\x95 to try again");
			if (!T->drill)
				clickable(x, 380, w, 150, [] { g_groupState[(size_t)g_catSel] = 0; });
		}
		else
			centredIn(x, w, 420, 26, kMuted, T->drill ? "Loading episodes..." : "Loading...");
		return;
	}
	if (g_view.empty())
	{
		const std::string why = filtering ? "Nothing matches the filter" :
			g_section == S_FAVOURITES ? "No favorites yet" :
			g_section == S_RECENT ? "Nothing watched yet" :
			g_section == S_GROUPS && T->cats.empty() ? "" :
			g_playlist.lazy && g_section == S_ALL ? "Open a category first" : std::string("No ") + kNoun[media];
		centredIn(x, w, 420, 26, kMuted, why);
		if (g_section == S_FAVOURITES && !filtering)
			centredIn(x, w, 462, 22, kMuted, "Press \xE2\x96\xA1 on an entry to add it");
		if (g_playlist.lazy && g_section == S_ALL && !filtering)
			centredIn(x, w, 462, 20, kMuted, "This list fills with the categories you open in Groups");
		return;
	}

	keepVisible(g_sel, g_top, (int)g_view.size(), kChannelRows);
	for (int row = 0; row < kChannelRows; row++)
	{
		const int i = g_top + row;
		if (i >= (int)g_view.size())
			break;
		const int channel = g_view[i];
		const Channel& info = g_playlist.channels[(size_t)channel];
		const float y = kListTop + (float)row * kRowH;
		const bool selected = i == g_sel, playing = channel == g_playing;
		const bool series = isSeriesItem(info.url);
		if (selected)
			gfx::panel(x + 6, y, w - 18, kRowH - 4, 14, 2, g_focus == F_CHANNELS ? kBorder : kBorderDim, kSelected);
		else if (hovering(x + 6, y, w - 18, kRowH - 4))
			gfx::roundRect(x + 6, y, w - 18, kRowH - 4, 14, kHover);
		clickable(x + 6, y, w - 18, kRowH - 4, [i] {
			g_focus = F_CHANNELS;
			g_sel = i;
			activateSelected();
		});
		clickable(x + w - 78, y, 66, kRowH - 4, [channel] { toggleFavourite(channel); });   // the star

		gfx::panel(x + 20, y + 9, 54, 54, 12, 1, kTileLine, kTile);
		centredTight(x + 20, 54, y + 23, 22, playing ? kAccent : kText, initials(info.name));

		char number[16];
		snprintf(number, sizeof(number), "%d. ", i + 1);
		const uint32_t colour = playing || selected ? kAccent : kText;
		const float nw = gfx::text(x + 90, y + 9, 27, colour, number);
		gfx::text(x + 90 + nw, y + 9, 27, colour, info.name, w - 190 - nw);
		if (playing)
		{
			const char* const badge = g_nowVod ? "PLAYING" : "LIVE";
			const float bw = g_nowVod ? 86.0f : 58.0f;
			gfx::panel(x + 90, y + 44, bw, 22, 11, 1, kLive, kLiveBack);
			centredTight(x + 90, bw, y + 47, 15, kLive, badge);
			gfx::text(x + 100 + bw, y + 43, 20, kMuted, info.group, w - 212 - bw);
		}
		else
		{
			const std::string under = series ? "Series  \xC2\xB7  \xE2\x9C\x95 to see the episodes" :
				media == 0 && inGroups() ? "No program information available" : info.group;
			gfx::text(x + 90, y + 43, 20, kMuted, under, w - 200);
		}

		gfx::text(x + w - 62, y + 18, 32, g_isFavourite[(size_t)channel] ? kAccent : kMuted, g_isFavourite[(size_t)channel] ? "\xE2\x98\x85" : "\xE2\x98\x86");
	}
	drawScrollbar(x + w - 8, kListTop, (float)kChannelRows * kRowH, g_top, (int)g_view.size(), kChannelRows);
}

// A thin bar showing how far through a film or episode playback is.
static void drawProgress(float x, float y, float w, uint32_t track, uint32_t textColour)
{
	const double length = player::duration(), at = player::position();
	if (length <= 0)
		return;
	const float done = (float)(at / length > 1 ? 1 : at / length);
	const std::string left = clockText(at), right = clockText(length);
	const float lw = gfx::textWidth(20, left), rw = gfx::textWidth(20, right);
	gfx::text(x, y - 7, 20, textColour, left);
	gfx::text(x + w - rw, y - 7, 20, textColour, right);
	const float bx = x + lw + 16, bw = w - lw - rw - 32;
	gfx::roundRect(bx, y, bw, 8, 4, track);
	if (done > 0.004f)
		gfx::roundRect(bx, y, bw * done, 8, 4, kBorder);
}

static void drawPlayerPane(float x, float w, const std::string& status)
{
	const float videoH = w * 9 / 16;
	gfx::rect(x, kColumnTop, w, 1080 - kColumnTop, kPane);
	gfx::rect(x, kColumnTop, w, videoH, 0x000000ff);

	if (!g_nowOn)
	{
		gfx::panel(x + w / 2 - 44, kColumnTop + videoH / 2 - 70, 88, 64, 10, 4, 0x3a4050ff, 0x000000ff);
		centredTight(x + w / 2 - 44, 88, kColumnTop + videoH / 2 - 58, 34, 0x3a4050ff, "\xE2\x96\xB6");
		centredIn(x, w, kColumnTop + videoH / 2 + 24, 28, kOnVideoMuted, "Please select a channel to start playback");
	}
	else
	{
		gfx::videoDraw(x, kColumnTop, w, videoH);
		clickable(x, kColumnTop, w, videoH, [] { g_fullscreen = true; g_overlayUntil = 0; showControlsSoon(); });
		drawSubtitles(x, w, kColumnTop + videoH - 10, 26);
		if (!status.empty())
		{
			if (g_gotPicture)
				gfx::rect(x, kColumnTop + videoH / 2 - 40, w, 80, 0x000000aa);
			centredIn(x, w, kColumnTop + videoH / 2 - 16, 28, kOnVideo, status);
		}
	}

	float y = kColumnTop + videoH + 14;
	if (g_nowOn && g_nowVod)
	{
		drawProgress(x + 24, y + 8, w - 48, kLine, kMuted);
		y += 40;
	}
	else if (g_tab == 0)
	{
		// the programme panel under the picture
		centredIn(x, w, y, 24, kText, "Today");
		gfx::text(x + 24, y - 4, 30, kText, "\xE2\x80\xB9");
		gfx::text(x + w - 40, y - 4, 30, kText, "\xE2\x80\xBA");
		y += 46;
	}

	const int selected = selectedChannel();
	if ((g_nowOn || selected >= 0) && y + 96 < 1040)
	{
		const Channel& info = g_nowOn ? g_now : g_playlist.channels[(size_t)selected];
		const bool live = g_nowOn;
		gfx::panel(x + 12, y, w - 24, 96, 14, live ? 2.0f : 1.0f, live ? kBorder : kLine, live ? kSelected : kCard);
		float tx = x + 34;
		if (live)
		{
			const char* const badge = g_nowVod ? "PLAYING" : "LIVE";
			const float bw = g_nowVod ? 96.0f : 66.0f;
			gfx::panel(tx, y + 16, bw, 26, 13, 1, kLive, kLiveBack);
			centredTight(tx, bw, y + 20, 17, kLive, badge);
			tx += bw + 16;
		}
		gfx::text(tx, y + 16, 22, kMuted, info.group, x + w - 40 - tx);
		gfx::text(x + 34, y + 52, 27, live ? kAccent : kText, info.name, w - 70);
		y += 108;
	}
	if (!(g_nowOn && g_nowVod) && g_tab == 0 && y + 62 < 1040)
	{
		gfx::panel(x + 12, y, w - 24, 62, 14, 1, kLine, kCard);
		gfx::text(x + 34, y + 18, 22, kMuted, "No program information available", w - 70);
	}

	gfx::text(x + 16, 1046, 20, kMuted,
		"\xE2\x9C\x95 Play / Full screen   \xE2\x96\xA1 Favorite / Hide group   \xE2\x96\xB3 Filter   \xE2\x97\x8B Back   L2 / R2 Tabs   OPTIONS Settings", w - 32);
}

static void drawAccount()
{
	const float x = 96, w = 1728, top = kColumnTop;
	gfx::roundRect(x, top, w, 1080 - top - 20, 16, kColumn);
	gfx::text(x + 30, top + 22, 30, kText, "Account");

	std::vector<std::pair<std::string, std::string>> rows;
	int state;
	std::string error;
	{
		std::lock_guard<std::mutex> lock(g_accountMutex);
		rows = g_accountRows;
		state = g_accountState;
		error = g_accountError;
	}
	if (state == 2 || state == 3)
	{
		const Tab& live = g_tabs[0];
		char text[64];
		if (live.playlist.lazy)
			snprintf(text, sizeof(text), "%d", (int)live.playlist.groups.size());
		else
			snprintf(text, sizeof(text), "%d in %d groups", (int)live.playlist.channels.size(), (int)live.playlist.groups.size());
		rows.emplace_back(live.playlist.lazy ? "Live categories" : "Live channels", text);
	}
	float y = top + 84;
	if (state <= 1)
		gfx::text(x + 30, y, 24, kMuted, "Asking the server...");
	for (const auto& row : rows)
	{
		if (y > 1000)
			break;
		gfx::panel(x + 20, y, w - 40, 54, 12, 1, kLine, kCard);
		gfx::text(x + 44, y + 15, 23, kMuted, row.first, 300);
		gfx::text(x + 380, y + 14, 25, kText, row.second, w - 440);
		y += 62;
	}
	if (state == 3 && !error.empty())
		gfx::text(x + 30, y + 10, 22, kLive, "The server could not be asked for more: " + error, w - 60);
	gfx::text(x + 30, 1024, 20, kMuted, "L2 / R2 Tabs    \xE2\x97\x8B Back to Live TV    OPTIONS Settings");
}

// ---- the player's on-screen controls ------------------------------------------------
// Shown over the picture in full screen. Films get a time bar and skip buttons; live TV gets
// channel buttons. Both get sound-track, subtitle and favourite buttons.

enum ControlButton { B_BACK5, B_BACK30, B_PLAY, B_FWD30, B_FWD5, B_PREV, B_NEXT, B_AUDIO, B_SUBTITLES, B_FAVOURITE };
static int g_ctlRow = 0;                        // films: 0 the time bar, 1 the buttons
static int g_ctlFocus = 0;                      // which button
static int g_trackMenu = 0, g_trackSel = 0;     // the open list: 0 none, 1 sound tracks, 2 subtitles
static bool g_cleanScreen = false;              // the viewer put everything away (Square): nothing but the picture

static std::vector<int> controlButtons()
{
	std::vector<int> buttons;
	if (g_nowVod)
		buttons = { B_BACK5, B_BACK30, B_PLAY, B_FWD30, B_FWD5 };
	else
		buttons = { B_PREV, B_NEXT };
	buttons.push_back(B_AUDIO);
	buttons.push_back(B_SUBTITLES);
	if (g_playing >= 0)
		buttons.push_back(B_FAVOURITE);
	return buttons;
}

static bool controlsShown()
{
	return nowSeconds() < g_overlayUntil || g_trackMenu != 0;
}

static void showControls(double seconds = 5)
{
	if (!controlsShown())
	{
		// coming back from hidden: start on the time bar (films) or the first button
		g_ctlRow = g_nowVod ? 0 : 1;
		g_ctlFocus = g_nowVod ? 2 : 0;
	}
	g_cleanScreen = false;
	g_overlayUntil = nowSeconds() + seconds;
}

static void changeChannel(int by)
{
	if (g_nowVod || g_nowTab != g_tab || g_tab == kAccountTab || g_view.empty())
		return;
	moveSelection(g_sel, (int)g_view.size(), by);
	if (!isSeriesItem(g_playlist.channels[(size_t)g_view[g_sel]].url))
		startChannel(g_view[g_sel]);
}

static void pressControl(int button)
{
	switch (button)
	{
	case B_BACK5: player::seekBy(-300); break;
	case B_BACK30: player::seekBy(-30); break;
	case B_FWD30: player::seekBy(30); break;
	case B_FWD5: player::seekBy(300); break;
	case B_PLAY: player::pause(!player::paused()); break;
	case B_PREV: changeChannel(-1); break;
	case B_NEXT: changeChannel(1); break;
	case B_AUDIO:
	{
		const std::vector<player::Track> tracks = player::audioTracks();
		if (tracks.size() < 2)
		{
			toast(tracks.empty() ? "No sound tracks listed for this video" : "This video has one sound track");
			break;
		}
		g_trackMenu = 1;
		g_trackSel = 0;
		for (int i = 0; i < (int)tracks.size(); i++)
			if (tracks[(size_t)i].id == player::audioTrack())
				g_trackSel = i;
		break;
	}
	case B_SUBTITLES:
	{
		const std::vector<player::Track> tracks = player::subtitleTracks();
		if (tracks.empty())
		{
			toast("This video has no text subtitles");
			break;
		}
		g_trackMenu = 2;
		g_trackSel = 0;                             // "Off" is the first row
		for (int i = 0; i < (int)tracks.size(); i++)
			if (tracks[(size_t)i].id == player::subtitleTrack())
				g_trackSel = i + 1;
		break;
	}
	case B_FAVOURITE: if (g_playing >= 0) toggleFavourite(g_playing); break;
	default: break;
	}
}

static void chooseTrack(int row)
{
	if (g_trackMenu == 1)
	{
		const std::vector<player::Track> tracks = player::audioTracks();
		if (row >= 0 && row < (int)tracks.size())
			player::setAudioTrack(tracks[(size_t)row].id);
	}
	else if (g_trackMenu == 2)
	{
		const std::vector<player::Track> tracks = player::subtitleTracks();
		if (row == 0)
			player::setSubtitleTrack(-1);
		else if (row - 1 < (int)tracks.size())
			player::setSubtitleTrack(tracks[(size_t)row - 1].id);
	}
	g_trackMenu = 0;
	showControls();
}

// The symbols on the buttons, drawn from simple shapes so they are crisp at any size.
static void drawControlIcon(int button, float cx, float cy, float s, uint32_t colour)
{
	const auto arrow = [&](float x, float direction) {
		// one triangle of a "skip" symbol, pointing right (1) or left (-1)
		gfx::triangle(x - 0.20f * s * direction, cy - 0.26f * s, x - 0.20f * s * direction, cy + 0.26f * s, x + 0.20f * s * direction, cy, colour);
	};
	switch (button)
	{
	case B_PLAY:
		if (player::paused())
			gfx::triangle(cx - 0.20f * s, cy - 0.30f * s, cx - 0.20f * s, cy + 0.30f * s, cx + 0.32f * s, cy, colour);
		else
		{
			gfx::roundRect(cx - 0.22f * s, cy - 0.28f * s, 0.15f * s, 0.56f * s, 0.05f * s, colour);
			gfx::roundRect(cx + 0.07f * s, cy - 0.28f * s, 0.15f * s, 0.56f * s, 0.05f * s, colour);
		}
		break;
	case B_FWD30: arrow(cx - 0.14f * s, 1); arrow(cx + 0.20f * s, 1); break;
	case B_BACK30: arrow(cx + 0.14f * s, -1); arrow(cx - 0.20f * s, -1); break;
	case B_FWD5:
		arrow(cx - 0.22f * s, 1); arrow(cx + 0.10f * s, 1);
		gfx::roundRect(cx + 0.30f * s, cy - 0.26f * s, 0.09f * s, 0.52f * s, 0.03f * s, colour);
		break;
	case B_BACK5:
		arrow(cx + 0.22f * s, -1); arrow(cx - 0.10f * s, -1);
		gfx::roundRect(cx - 0.39f * s, cy - 0.26f * s, 0.09f * s, 0.52f * s, 0.03f * s, colour);
		break;
	case B_PREV:
		gfx::triangle(cx - 0.30f * s, cy + 0.14f * s, cx + 0.30f * s, cy + 0.14f * s, cx, cy - 0.26f * s, colour);
		break;
	case B_NEXT:
		gfx::triangle(cx - 0.30f * s, cy - 0.14f * s, cx + 0.30f * s, cy - 0.14f * s, cx, cy + 0.26f * s, colour);
		break;
	case B_AUDIO:
		// a loudspeaker with two sound bars
		gfx::rect(cx - 0.34f * s, cy - 0.11f * s, 0.16f * s, 0.22f * s, colour);
		gfx::triangle(cx - 0.20f * s, cy - 0.11f * s, cx + 0.02f * s, cy - 0.30f * s, cx + 0.02f * s, cy + 0.30f * s, colour);
		gfx::triangle(cx - 0.20f * s, cy - 0.11f * s, cx - 0.20f * s, cy + 0.11f * s, cx + 0.02f * s, cy + 0.30f * s, colour);
		gfx::roundRect(cx + 0.12f * s, cy - 0.13f * s, 0.07f * s, 0.26f * s, 0.03f * s, colour);
		gfx::roundRect(cx + 0.27f * s, cy - 0.24f * s, 0.07f * s, 0.48f * s, 0.03f * s, colour);
		break;
	case B_SUBTITLES:
		// a caption box with two lines of text
		gfx::panel(cx - 0.36f * s, cy - 0.25f * s, 0.72f * s, 0.50f * s, 0.09f * s, 0.07f * s, colour, 0);
		gfx::roundRect(cx - 0.23f * s, cy - 0.08f * s, 0.30f * s, 0.06f * s, 0.03f * s, colour);
		gfx::roundRect(cx + 0.11f * s, cy - 0.08f * s, 0.12f * s, 0.06f * s, 0.03f * s, colour);
		gfx::roundRect(cx - 0.23f * s, cy + 0.05f * s, 0.14f * s, 0.06f * s, 0.03f * s, colour);
		gfx::roundRect(cx - 0.05f * s, cy + 0.05f * s, 0.28f * s, 0.06f * s, 0.03f * s, colour);
		break;
	case B_FAVOURITE:
	{
		const bool on = g_playing >= 0 && g_isFavourite[(size_t)g_playing];
		const char* const star = on ? "\xE2\x98\x85" : "\xE2\x98\x86";
		gfx::text(cx - gfx::textWidth(0.62f * s, star) / 2, cy - 0.40f * s, 0.62f * s, colour, star);
		break;
	}
	default: break;
	}
}

// Subtitles: white words with a dark edge, centred, their last line resting on `bottom`.
static void drawSubtitles(float x, float w, float bottom, float size)
{
	const std::string words = player::subtitleText();
	if (words.empty())
		return;
	// split into lines, wrapping any that are too wide
	std::vector<std::string> lines;
	size_t pos = 0;
	while (pos <= words.size() && lines.size() < 6)
	{
		size_t end = words.find('\n', pos);
		if (end == std::string::npos)
			end = words.size();
		std::string line = words.substr(pos, end - pos);
		pos = end + 1;
		while (gfx::textWidth(size, line) > w * 0.86f)
		{
			size_t cut = line.size();
			while (cut > 0 && (line[cut - 1] != ' ' || gfx::textWidth(size, line.substr(0, cut)) > w * 0.86f))
				cut--;
			if (cut == 0)
				break;
			lines.push_back(line.substr(0, cut - 1));
			line = line.substr(cut);
		}
		if (!line.empty())
			lines.push_back(line);
	}
	const float lineH = size * 1.28f, edge = size / 18 < 1.5f ? 1.5f : size / 18;
	float y = bottom - lineH * (float)lines.size();
	for (const std::string& line : lines)
	{
		const float lx = x + (w - gfx::textWidth(size, line)) / 2;
		static const float around[8][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -0.7f, -0.7f }, { 0.7f, -0.7f }, { -0.7f, 0.7f }, { 0.7f, 0.7f } };
		for (const auto& offset : around)
			gfx::text(lx + offset[0] * edge, y + offset[1] * edge, size, 0x000000e0, line);
		gfx::text(lx, y + edge * 1.2f, size, 0x00000090, line);     // a little shadow underneath
		gfx::text(lx, y, size, 0xffffffff, line);
		y += lineH;
	}
}

static void drawControls(const std::string& status)
{
	const float keepScale = gfx::radiusScale();
	gfx::setRadiusScale(1.0f);                      // these are round whatever the theme
	const bool shown = controlsShown();
	const uint32_t white = 0xffffffff, soft = 0xffffffb0, dark = 0x0b0e14ff;

	gfx::rect(0, 0, 1920, 1080, 0x000000ff);
	gfx::videoDraw(0, 0, 1920, 1080);
	clickable(0, 0, 1920, 1080, [] {
		if (controlsShown())
		{
			g_overlayUntil = 0;                     // a click on the picture puts the controls away
			g_cleanScreen = true;
		}
		else
			showControls();
	});
	if (pointerOn() && !shown && !g_cleanScreen)
		showControls(3);

	drawSubtitles(0, 1920, shown ? 800.0f : 1030.0f, 44);

	// What is happening (connecting, paused, an error), unless the viewer asked for a clean screen
	// and it is only the pause notice.
	if (!status.empty() && !(g_cleanScreen && status == "Paused"))
	{
		const float w = gfx::textWidth(38, status) + 90;
		gfx::roundRect((1920 - w) / 2, 486, w, 84, 42, 0x000000b0);
		centredTight((1920 - w) / 2, w, 507, 38, white, status);
	}
	if (!shown)
	{
		gfx::setRadiusScale(keepScale);
		return;
	}

	// soft shading top and bottom, so the controls read over any picture
	for (int i = 0; i < 48; i++)
	{
		// many thin bands, each a little darker than the last, read as one smooth fade
		const float t = (float)i / 47.0f;
		gfx::rect(0, (float)i * 4, 1920, 4, (uint32_t)(150.0f * (1 - t) * (1 - t)));
		gfx::rect(0, 1080 - 336 + (float)i * 7, 1920, 7, (uint32_t)(225.0f * t * sqrtf(t)));
	}

	// top: back, title, where it is from
	const bool backHover = hovering(44, 30, 60, 60);
	gfx::panel(44, 30, 60, 60, 30, 2, backHover ? kBorder : 0xffffff50, backHover ? 0xffffff30 : 0xffffff18);
	gfx::triangle(82, 44, 82, 76, 62, 60, white);
	clickable(44, 30, 60, 60, [] { g_fullscreen = false; });
	float tx = 126;
	if (!g_nowVod)
	{
		gfx::roundRect(tx, 44, 76, 32, 16, 0xe5484dff);
		centredTight(tx, 76, 50, 19, white, "LIVE");
		tx += 92;
	}
	gfx::text(tx, 36, 38, white, g_now.name, 1700 - tx);
	gfx::text(tx, 84, 22, soft, g_now.group, 1700 - tx);

	const bool film = g_nowVod;
	if (film)
	{
		// the time bar
		const double length = player::duration(), at = player::position();
		const float bx = 200, bw = 1520, by = 884;
		const float done = length > 0 ? (float)(at / length > 1 ? 1 : at / length) : 0;
		const bool barFocus = (!pointerOn() && g_ctlRow == 0 && g_trackMenu == 0) || hovering(bx - 10, by - 22, bw + 20, 52);
		const float h = barFocus ? 12.0f : 8.0f;
		gfx::text(bx - 26 - gfx::textWidth(24, clockText(at)), by - 12, 24, white, clockText(at));
		gfx::text(bx + bw + 26, by - 12, 24, soft, clockText(length));
		gfx::roundRect(bx, by - h / 2 + 4, bw, h, h / 2, 0xffffff40);
		if (done > 0.002f)
			gfx::roundRect(bx, by - h / 2 + 4, bw * done, h, h / 2, kBorder);
		const float knob = barFocus ? 15.0f : 9.0f, kx = bx + bw * done;
		if (barFocus)
			gfx::roundRect(kx - knob - 9, by + 4 - knob - 9, (knob + 9) * 2, (knob + 9) * 2, knob + 9, (kBorder & 0xffffff00) | 0x48);
		gfx::roundRect(kx - knob, by + 4 - knob, knob * 2, knob * 2, knob, white);
		clickable(bx - 10, by - 22, bw + 20, 52, [bx, bw, length] {
			const float fraction = (g_pointerX - bx) / bw;
			player::seekTo(length * (double)(fraction < 0 ? 0 : fraction > 1 ? 1 : fraction));
			g_ctlRow = 0;
			showControls();
		});
	}

	// the buttons
	const std::vector<int> buttons = controlButtons();
	static const char* const labels[] = { "5 min", "30 s", "", "30 s", "5 min", "Previous", "Next", "Audio", "Subtitles", "Favorite" };
	const float cy = 986;
	int centre = 0;
	for (const int button : buttons)
		if (button <= B_NEXT)
			centre++;
	float trackX[2] = { 0, 0 };
	for (int i = 0; i < (int)buttons.size(); i++)
	{
		const int button = buttons[(size_t)i];
		const bool hero = button == B_PLAY;
		const float size = hero ? 92.0f : 70.0f;
		float cx;
		if (button <= B_NEXT)
			cx = 960 + ((float)i - (float)(centre - 1) / 2) * (film ? 124.0f : 150.0f);     // the transport buttons, centred
		else
			cx = 1836 - (float)((int)buttons.size() - 1 - i) * 118;                        // the rest, at the right
		if (button == B_AUDIO) trackX[0] = cx;
		if (button == B_SUBTITLES) trackX[1] = cx;
		const bool focus = (!pointerOn() && g_trackMenu == 0 && (g_ctlRow == 1 || !film) && i == g_ctlFocus) || hovering(cx - size / 2, cy - size / 2, size, size);
		if (focus)
			gfx::roundRect(cx - size / 2 - 10, cy - size / 2 - 10, size + 20, size + 20, size / 2 + 10, (kBorder & 0xffffff00) | 0x50);   // glow
		const uint32_t fill = focus ? kBorder : hero ? white : 0xffffff22;
		gfx::panel(cx - size / 2, cy - size / 2, size, size, size / 2, 2, focus || hero ? 0 : 0xffffff55, fill);
		drawControlIcon(button, cx, cy, hero ? size * 0.80f : size * 0.72f, focus || hero ? dark : white);
		if (labels[button][0])
			centredTight(cx - 60, 120, cy + size / 2 + 8, 17, focus ? white : soft, labels[button]);
		clickable(cx - size / 2, cy - size / 2, size, size, [i, button] {
			g_ctlRow = 1;
			g_ctlFocus = i;
			showControls();
			pressControl(button);
		});
	}

	// the list of sound tracks or subtitles, above its button
	if (g_trackMenu != 0)
	{
		std::vector<std::string> rows;
		int current = 0;
		if (g_trackMenu == 1)
		{
			const std::vector<player::Track> tracks = player::audioTracks();
			for (int i = 0; i < (int)tracks.size(); i++)
			{
				rows.push_back(tracks[(size_t)i].label);
				if (tracks[(size_t)i].id == player::audioTrack())
					current = i;
			}
		}
		else
		{
			const std::vector<player::Track> tracks = player::subtitleTracks();
			rows.push_back("Off");
			for (int i = 0; i < (int)tracks.size(); i++)
			{
				rows.push_back(tracks[(size_t)i].label);
				if (tracks[(size_t)i].id == player::subtitleTrack())
					current = i + 1;
			}
		}
		const int count = (int)rows.size(), visible = count < 9 ? count : 9;
		if (g_trackSel > count - 1) g_trackSel = count - 1;
		const int first = g_trackSel < visible ? 0 : g_trackSel - visible + 1;
		const float w = 560, rowH = 56, h = 70 + (float)visible * rowH;
		float x = trackX[g_trackMenu - 1] - w / 2;
		if (x + w > 1890) x = 1890 - w;
		const float y = 880 - h;
		clickable(0, 0, 1920, 1080, [] { g_trackMenu = 0; });       // a click outside closes it
		gfx::panel(x, y, w, h, 20, 2, 0xffffff40, 0x12151cf4);
		clickable(x, y, w, h, [] {});
		gfx::text(x + 26, y + 18, 24, soft, g_trackMenu == 1 ? "Audio" : "Subtitles");
		for (int row = 0; row < visible; row++)
		{
			const int i = first + row;
			const float ry = y + 58 + (float)row * rowH;
			const bool selected = (!pointerOn() && i == g_trackSel) || hovering(x + 10, ry, w - 20, rowH - 6);
			if (selected)
				gfx::roundRect(x + 10, ry, w - 20, rowH - 6, 12, kBorder);
			if (i == current)
				gfx::text(x + 28, ry + 11, 26, selected ? dark : kBorder, "\xE2\x9C\x93");
			gfx::text(x + 66, ry + 11, 25, selected ? dark : white, rows[(size_t)i], w - 96);
			clickable(x + 10, ry, w - 20, rowH - 6, [i] { chooseTrack(i); });
		}
	}
	gfx::setRadiusScale(keepScale);
}

static void showControlsSoon()
{
	g_trackMenu = 0;
	g_cleanScreen = false;
	showControls(4);
}

static void controlsInput(Action action)
{
	if (g_trackMenu != 0)
	{
		const int count = g_trackMenu == 1 ? (int)player::audioTracks().size() : (int)player::subtitleTracks().size() + 1;
		switch (action)
		{
		case A_UP: moveSelection(g_trackSel, count, -1); break;
		case A_DOWN: moveSelection(g_trackSel, count, 1); break;
		case A_OK: chooseTrack(g_trackSel); break;
		case A_BACK:
		case A_LEFT:
		case A_RIGHT: g_trackMenu = 0; showControls(); break;
		default: break;
		}
		return;
	}

	const bool film = g_nowVod, shown = controlsShown();
	const std::vector<int> buttons = controlButtons();
	const int count = (int)buttons.size();
	if (g_ctlFocus > count - 1)
		g_ctlFocus = count - 1;
	switch (action)
	{
	case A_BACK: g_fullscreen = false; return;
	case A_MENU: openManager(""); return;
	case A_FAVOURITE:
		// Square: everything off the screen at once, or back again
		if (shown)
		{
			g_overlayUntil = 0;
			g_trackMenu = 0;
			g_cleanScreen = true;
			g_toastUntil = 0;
			return;
		}
		break;
	case A_PAGE_UP: if (film) player::seekBy(-300); break;
	case A_PAGE_DOWN: if (film) player::seekBy(300); break;
	case A_LEFT:
	case A_RIGHT:
	{
		const int by = action == A_LEFT ? -1 : 1;
		if (film && (!shown || g_ctlRow == 0))
			player::seekBy(30.0 * by);              // on the time bar (or with the controls away): skip
		else if (shown)
			g_ctlFocus = (g_ctlFocus + count + by) % count;
		break;
	}
	case A_UP:
		if (film)
			g_ctlRow = 0;
		else
			changeChannel(-1);
		break;
	case A_DOWN:
		if (film)
		{
			if (shown)
				g_ctlRow = 1;
		}
		else
			changeChannel(1);
		break;
	case A_OK:
		if (film && (!shown || g_ctlRow == 0))
			player::pause(!player::paused());
		else if (shown)
			pressControl(buttons[(size_t)g_ctlFocus]);
		break;
	default: break;
	}
	// A clean screen stays clean while skipping or pausing; Square (or the pointer) brings the controls back.
	if (!g_cleanScreen || action == A_FAVOURITE)
		showControls();
}

static void browseInput(Action action)
{
	if (g_fullscreen)
	{
		controlsInput(action);
		return;
	}

	if (action == A_TAB_PREV || action == A_TAB_NEXT)
	{
		switchTab(g_tab + (action == A_TAB_PREV ? -1 : 1));
		return;
	}
	if (action == A_MENU)
	{
		openManager("");
		return;
	}
	if (g_tab == kAccountTab)
	{
		if (action == A_BACK)
			switchTab(0);
		return;
	}
	if (action == A_SEARCH)
	{
		openFilter();
		return;
	}

	const bool groups = g_section == S_GROUPS && !g_playlist.groups.empty();
	if (g_focus == F_RAIL)
	{
		// the sections, then the settings button
		int place = g_railOnPlaylists ? S_COUNT : (int)g_section;
		switch (action)
		{
		case A_UP: place = (place + S_COUNT) % (S_COUNT + 1); break;
		case A_DOWN: place = (place + 1) % (S_COUNT + 1); break;
		case A_RIGHT:
		case A_OK:
			if (g_railOnPlaylists)
			{
				if (action == A_OK)
					openManager("");
			}
			else
				g_focus = groups ? F_CATEGORIES : F_CHANNELS;
			break;
		default: break;
		}
		if (place == S_COUNT)
			g_railOnPlaylists = true;
		else if (g_railOnPlaylists || place != (int)g_section)
			chooseSection(place);
	}
	else if (g_focus == F_CATEGORIES)
	{
		const int count = (int)T->cats.size();
		int place = catPlace();
		const int before = place;
		switch (action)
		{
		case A_UP: moveSelection(place, count, -1); break;
		case A_DOWN: moveSelection(place, count, 1); break;
		case A_PAGE_UP: moveSelection(place, count, -kCategoryRows); break;
		case A_PAGE_DOWN: moveSelection(place, count, kCategoryRows); break;
		case A_RIGHT:
		case A_OK:
			if (count == 0)
				break;
			if (!g_view.empty() || T->drill)
				g_focus = F_CHANNELS;
			else if (g_groupState[(size_t)g_catSel] == 3)
				g_groupState[(size_t)g_catSel] = 0;        // try again
			break;
		case A_FAVOURITE: if (count > 0) hideCategory(g_catSel); break;
		case A_LEFT:
		case A_BACK: g_focus = F_RAIL; break;
		default: break;
		}
		if (count > 0 && place != before)
			selectCategory(T->cats[(size_t)place]);
	}
	else
	{
		const int count = (int)g_view.size();
		switch (action)
		{
		case A_UP: moveSelection(g_sel, count, -1); break;
		case A_DOWN: moveSelection(g_sel, count, 1); break;
		case A_PAGE_UP: moveSelection(g_sel, count, -kChannelRows); break;
		case A_PAGE_DOWN: moveSelection(g_sel, count, kChannelRows); break;
		case A_BACK:
			if (T->drill)
				closeDrill();                              // from the episodes back to the series
			else
				g_focus = groups ? F_CATEGORIES : F_RAIL;
			break;
		case A_LEFT: g_focus = groups ? F_CATEGORIES : F_RAIL; break;
		case A_OK: activateSelected(); break;
		case A_FAVOURITE: toggleFavourite(selectedChannel()); break;
		default: break;
		}
	}
}

static void drawBrowse()
{
	pumpLibrary();
	if (g_tab != kAccountTab)
	{
		pumpGenres();
		pumpDrill();
	}
	const std::string status = updatePlayback();

	if (g_fullscreen && g_nowOn)
	{
		drawControls(status);
		if (nowSeconds() < g_toastUntil)
		{
			const float w = gfx::textWidth(24, g_toast) + 60;
			gfx::panel((1920 - w) / 2, 150, w, 56, 16, 2, kBorder, kDialog);
			centredTight((1920 - w) / 2, w, 165, 24, kText, g_toast);
		}
		return;
	}
	g_trackMenu = 0;

	drawTopBar();
	drawTabs();
	if (g_tab == kAccountTab)
		drawAccount();
	else
	{
		const bool groups = g_section == S_GROUPS && !g_playlist.groups.empty();
		float x = 90;
		if (groups)
		{
			drawCategories(x, 400);
			x += 400;
		}
		const float listW = groups ? 560 : 640;
		drawChannels(x, listW);
		x += listW;
		gfx::rect(x, kColumnTop, 2, 1080 - kColumnTop, kLine);
		drawPlayerPane(x + 2, 1920 - x - 2, status);
		drawRail();                                 // last, so its label sits above the columns
	}
	if (nowSeconds() < g_toastUntil)
	{
		const float w = gfx::textWidth(24, g_toast) + 60;
		gfx::panel((1920 - w) / 2, 960, w, 56, 16, 2, kBorder, kDialog);
		centredTight((1920 - w) / 2, w, 975, 24, kText, g_toast);
	}
}

// ---- opening a playlist ---------------------------------------------------------

static void startLoading(int index)
{
	if (index < 0 || index >= (int)g_sources.size())
		return;
	cancelLink();
	stopGenreLoading();
	{
		std::lock_guard<std::mutex> lock(g_libMutex);
		g_libRequest++;
		g_libDone = 0;
	}
	{
		std::lock_guard<std::mutex> lock(g_drillMutex);
		g_drillRequest++;
		g_drillDone = 0;
	}
	player::stop();
	gfx::videoClear();
	g_playing = -1;
	g_nowOn = false;
	g_fullscreen = false;
	g_keyboard = false;
	g_haveBrowse = false;
	g_active = index;
	g_activeId = sourceId(g_sources[(size_t)index]);
	writeTextFile(g_dataDir + "/active.txt", g_activeId);
	g_screen = LOADING;
	int request;
	{
		std::lock_guard<std::mutex> lock(g_loadMutex);
		request = ++g_loadRequest;
		g_loadState = 0;
	}
	g_loadStarted = nowSeconds();
	const Source source = g_sources[(size_t)index];
	startThread("playlist", [source, request] { loadPlaylist(source, request); });
}

// Gives up waiting for a playlist and goes to Settings, so a slow or dead address never traps the viewer.
static void cancelLoading()
{
	if (g_screen != LOADING)
		return;
	{
		std::lock_guard<std::mutex> lock(g_loadMutex);
		g_loadRequest++;
		g_loadState = 0;
	}
	httpCancel();
	logLine("playlist: loading cancelled by the viewer");
	openManager("Loading was cancelled. Pick another playlist, or open this one again to retry.");
}

static void reloadPlaylist()
{
	startLoading(g_active);
}

// ---- on-screen keyboard ---------------------------------------------------------

static std::string* g_keyTarget = nullptr;
static std::string g_keyLabel;
static size_t g_keyLimit = 40;
static bool g_keyIsFilter = false, g_keyShift = false;
static int g_keyRow = 1, g_keyCol = 0;
static const int kKeyRowCount = 6;
static const char* const kKeyRows[5] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm", "./:-_@?=&%" };
static const char* const kKeySpecial[5] = { "Shift", "Space", "Delete", "Clear", "Done" };

static void openKeyboard(std::string* target, size_t limit, bool isFilter, const std::string& label)
{
	g_keyTarget = target;
	g_keyLimit = limit;
	g_keyIsFilter = isFilter;
	g_keyLabel = label;
	g_keyShift = false;
	g_keyRow = 1;
	g_keyCol = 0;
	g_keyboard = true;
}

static int keyCount(int row)
{
	return row < 5 ? (int)strlen(kKeyRows[row]) : 5;
}

static void pressKey(int row, int col)
{
	if (!g_keyTarget)
		return;
	std::string& text = *g_keyTarget;
	const int keep = g_keyIsFilter ? selectedChannel() : -1;
	if (row < 5)
	{
		char c = kKeyRows[row][col];
		if (g_keyShift && c >= 'a' && c <= 'z')
			c = (char)(c - 32);
		if (text.size() < g_keyLimit)
			text += c;
	}
	else if (col == 0)
		g_keyShift = !g_keyShift;
	else if (col == 1)
	{
		if (!text.empty() && text.size() < g_keyLimit)
			text += ' ';
	}
	else if (col == 2)
	{
		if (!text.empty())
			text.pop_back();
	}
	else if (col == 3)
		text.clear();
	else
		g_keyboard = false;
	if (g_keyIsFilter)
	{
		applyFilter(keep);
		if (!g_keyboard && !T->filterCats)
			g_focus = F_CHANNELS;
	}
}

static void drawKeyboard()
{
	const float w = 1100, h = 580, x = (1920 - w) / 2, y = 470, key = 98, keyH = 66, gap = 6;
	gfx::rect(0, 0, 1920, 1080, 0x00000080);
	clickable(0, 0, 1920, 1080, [] {});                       // nothing behind the keyboard can be clicked
	gfx::panel(x, y, w, h, 18, 2, kBorder, kDialog);
	const float lw = gfx::text(x + 30, y + 22, 24, kMuted, g_keyLabel + ":");
	gfx::panel(x + 46 + lw, y + 12, w - 76 - lw, 46, 10, 1, kLine, kDeep);
	const std::string shown = (g_keyTarget ? *g_keyTarget : std::string()) + "_";
	// long text (web addresses) shows its end, where the typing happens
	std::string tail = shown;
	while (tail.size() > 1 && gfx::textWidth(24, tail) > w - 110 - lw)
		tail.erase(0, 1);
	gfx::text(x + 60 + lw, y + 22, 24, kText, tail);

	for (int row = 0; row < kKeyRowCount; row++)
	{
		const int count = keyCount(row);
		const float kw = row < 5 ? key : 204;
		const float rowX = x + (w - ((float)count * kw + (float)(count - 1) * gap)) / 2;
		for (int col = 0; col < count; col++)
		{
			const float kx = rowX + (float)col * (kw + gap), ky = y + 76 + (float)row * (keyH + gap);
			const bool selected = (!pointerOn() && row == g_keyRow && col == g_keyCol) || hovering(kx, ky, kw, keyH);
			const bool lit = row == 5 && col == 0 && g_keyShift;
			gfx::panel(kx, ky, kw, keyH, 10, selected ? 2.0f : 1.0f, selected ? kBorder : kLine, selected || lit ? kSelected : kTile);
			std::string label;
			if (row < 5)
			{
				char c = kKeyRows[row][col];
				if (g_keyShift && c >= 'a' && c <= 'z')
					c = (char)(c - 32);
				label = std::string(1, c);
			}
			else
				label = kKeySpecial[col];
			centredTight(kx, kw, ky + (row < 5 ? 17.0f : 20.0f), row < 5 ? 28.0f : 23.0f, selected || lit ? kAccent : kText, label);
			clickable(kx, ky, kw, keyH, [row, col] { g_keyRow = row; g_keyCol = col; pressKey(row, col); });
		}
	}
	centredIn(x, w, y + h - 34, 19, kMuted, "\xE2\x9C\x95 Type    \xE2\x96\xA1 Delete    L1 Shift    \xE2\x97\x8B / \xE2\x96\xB3 Done    Left stick  Pointer");
}

static void keyboardInput(Action action)
{
	switch (action)
	{
	case A_UP:
	case A_DOWN:
	{
		// keep roughly the same place across rows of different lengths
		const float place = ((float)g_keyCol + 0.5f) / (float)keyCount(g_keyRow);
		g_keyRow = (g_keyRow + (action == A_UP ? kKeyRowCount - 1 : 1)) % kKeyRowCount;
		g_keyCol = (int)(place * (float)keyCount(g_keyRow));
		break;
	}
	case A_LEFT: g_keyCol = (g_keyCol + keyCount(g_keyRow) - 1) % keyCount(g_keyRow); break;
	case A_RIGHT: g_keyCol = (g_keyCol + 1) % keyCount(g_keyRow); break;
	case A_OK: pressKey(g_keyRow, g_keyCol); break;
	case A_FAVOURITE: pressKey(5, 2); break;                  // square: delete
	case A_PAGE_UP: pressKey(5, 0); break;                    // L1: shift
	case A_BACK:
	case A_SEARCH:
	case A_MENU: pressKey(5, 4); break;                       // done
	default: break;
	}
}

// ---- "add playlist" form --------------------------------------------------------

struct FormField { std::string label, value, hint; };
static bool g_form = false;
static std::string g_formKind, g_formTitle, g_formError;
static std::vector<FormField> g_formFields;
static int g_formSel = 0;                       // the fields, then Save, then Cancel

static int g_formEdit = -1;                     // the playlist being edited, or -1 when adding

static void openForm(const std::string& kind, int editIndex)
{
	const Source* editing = editIndex >= 0 ? &g_sources[(size_t)editIndex] : nullptr;
	g_formKind = kind;
	g_formEdit = editIndex;
	g_formError.clear();
	g_formFields.clear();
	g_formFields.push_back({ "Name", editing ? editing->name : "", "optional" });
	if (kind == "url")
	{
		g_formTitle = editing ? "Edit M3U playlist" : "Add M3U playlist from a web address";
		g_formFields.push_back({ "Playlist URL", editing ? editing->a : "http://", "the address of the .m3u or .m3u8 file" });
		g_formFields.push_back({ "User agent", editing ? editing->ua : "", "optional; leave empty unless your provider needs one" });
	}
	else if (kind == "xtream")
	{
		g_formTitle = editing ? "Edit Xtream Codes account" : "Add Xtream Codes account";
		g_formFields.push_back({ "Server URL", editing ? editing->a : "http://", "for example http://example.com:8080" });
		g_formFields.push_back({ "Username", editing ? editing->b : "", "" });
		g_formFields.push_back({ "Password", editing ? editing->c : "", "" });
		g_formFields.push_back({ "MAC address", editing ? editing->mac : "", "optional; if your provider ties the account to one, like 00:1A:79:12:34:56" });
		g_formFields.push_back({ "User agent", editing ? editing->ua : "", "optional" });
	}
	else
	{
		g_formTitle = editing ? "Edit Stalker portal" : "Add Stalker / Ministra portal";
		g_formFields.push_back({ "Portal URL", editing ? editing->a : "http://", "for example http://example.com/c/" });
		g_formFields.push_back({ "MAC address", editing ? editing->b : "00:1A:79:", "the one registered with your provider" });
	}
	g_formSel = 1;
	g_form = true;
}

static std::string hostOf(const std::string& address)
{
	const size_t start = address.find("://");
	const size_t from = start == std::string::npos ? 0 : start + 3;
	const size_t end = address.find_first_of("/:?", from);
	return address.substr(from, end == std::string::npos ? std::string::npos : end - from);
}

static void submitForm()
{
	Source source;
	const std::string name = trim(g_formFields[0].value);
	const std::string address = trim(g_formFields[1].value);
	if (address.find("://") == std::string::npos || hostOf(address).empty())
	{
		g_formError = "Enter the web address, starting with http:// or https://";
		return;
	}
	if (g_formKind == "url")
	{
		source = Source{ "url", name.empty() ? hostOf(address) : name, address, "", "", "", trim(g_formFields[2].value) };
	}
	else if (g_formKind == "xtream")
	{
		const std::string user = trim(g_formFields[2].value), pass = trim(g_formFields[3].value);
		const std::string typedMac = trim(g_formFields[4].value), mac = normalizeMac(typedMac);
		if (!typedMac.empty() && mac.empty())
		{
			g_formError = "The MAC address needs six pairs of digits, like 00:1A:79:12:34:56 (or leave it empty)";
			return;
		}
		if ((user.empty() || pass.empty()) && mac.empty())
		{
			g_formError = "Enter the username and password (or, for a MAC-only account, just the MAC address)";
			return;
		}
		source = Source{ "xtream", name.empty() ? hostOf(address) : name, address, user, pass, "", trim(g_formFields[5].value), mac };
	}
	else
	{
		const std::string mac = normalizeMac(g_formFields[2].value);
		if (mac.empty())
		{
			g_formError = "The MAC address needs six pairs of digits, like 00:1A:79:12:34:56";
			return;
		}
		source = Source{ "stalker", name.empty() ? hostOf(address) : name, address, mac, "", "", "" };
	}

	int index = g_formEdit;
	if (index >= 0)
	{
		// Editing keeps the playlist's identity (and so its favourites); its saved copy is out of date.
		source.id = sourceId(g_sources[(size_t)index]);
		remove((g_dataDir + "/cache-" + source.id + ".m3u").c_str());
		g_sources[(size_t)index] = source;
	}
	else
	{
		source.id = sourceId(source);
		for (int i = 0; i < (int)g_sources.size(); i++)
			if (sourceId(g_sources[(size_t)i]) == source.id)
				index = i;
		if (index < 0)
		{
			g_sources.push_back(source);
			index = (int)g_sources.size() - 1;
		}
		else
			g_sources[(size_t)index] = source;
	}
	saveSources(g_dataDir, g_sources);
	g_form = false;
	startLoading(index);
}

static void editField(int index)
{
	g_formSel = index;
	g_formError.clear();
	openKeyboard(&g_formFields[(size_t)index].value, 300, false, g_formFields[(size_t)index].label);
}

static void drawForm()
{
	const int fields = (int)g_formFields.size();
	const float rowH = fields > 5 ? 96.0f : 104.0f;
	const float w = 1100, h = 150 + (float)fields * rowH + 130, x = (1920 - w) / 2, y = (1080 - h) / 2;
	gfx::rect(0, 0, 1920, 1080, 0x00000090);
	clickable(0, 0, 1920, 1080, [] {});
	gfx::panel(x, y, w, h, 20, 2, kLine, kDialog);
	gfx::text(x + 40, y + 32, 32, kText, g_formTitle, w - 80);

	for (int i = 0; i < fields; i++)
	{
		const FormField& field = g_formFields[(size_t)i];
		const float fy = y + 104 + (float)i * rowH;
		const bool selected = (!pointerOn() && !g_keyboard && g_formSel == i) || hovering(x + 40, fy + 30, w - 80, 56);
		gfx::text(x + 44, fy, 21, kMuted, field.hint.empty() ? field.label : field.label + "   (" + field.hint + ")", w - 90);
		gfx::panel(x + 40, fy + 30, w - 80, 56, 12, selected ? 2.0f : 1.0f, selected ? kBorder : kLine, kDeep);
		gfx::text(x + 60, fy + 44, 25, kText, field.value, w - 120);
		clickable(x + 40, fy + 30, w - 80, 56, [i] { editField(i); });
	}

	const float by = y + h - 104;
	if (!g_formError.empty())
		gfx::text(x + 44, by - 34, 21, kLive, g_formError, w - 90);
	static const char* const labels[2] = { "Save and open", "Cancel" };
	for (int b = 0; b < 2; b++)
	{
		const float bx = x + w - 40 - (float)(2 - b) * 250 + (float)b * 10, bw = 240;
		const bool selected = (!pointerOn() && !g_keyboard && g_formSel == fields + b) || hovering(bx, by, bw, 62);
		gfx::panel(bx, by, bw, 62, 14, 2, selected ? kBorder : kLine, b == 0 ? kSelected : kTile);
		centredTight(bx, bw, by + 18, 24, selected ? kAccent : kText, labels[b]);
		if (b == 0)
			clickable(bx, by, bw, 62, [] { submitForm(); });
		else
			clickable(bx, by, bw, 62, [] { g_form = false; });
	}
	gfx::text(x + 44, by + 20, 20, kMuted, "\xE2\x9C\x95 Edit / Choose    \xE2\x97\x8B Cancel", 520);
}

static void formInput(Action action)
{
	const int fields = (int)g_formFields.size(), count = fields + 2;
	switch (action)
	{
	case A_UP: g_formSel = (g_formSel + count - 1) % count; break;
	case A_DOWN: g_formSel = (g_formSel + 1) % count; break;
	case A_LEFT: if (g_formSel == fields + 1) g_formSel = fields; break;
	case A_RIGHT: if (g_formSel == fields) g_formSel = fields + 1; break;
	case A_OK:
		if (g_formSel < fields)
			editField(g_formSel);
		else if (g_formSel == fields)
			submitForm();
		else
			g_form = false;
		break;
	case A_BACK: g_form = false; break;
	default: break;
	}
}

// ---- playlist manager -----------------------------------------------------------

static int g_mgrSel = 0, g_mgrButton = 0, g_mgrTop = 0, g_mgrConfirm = -1;
static bool g_mgrOnButtons = false;
static std::string g_notice;
static const int kMgrRows = 7, kMgrButtons = 3;
static int g_mgrTab = 0;                        // 0 playlists, 1 themes, 2 groups
static int g_themeSel = 0;
static const int kMgrTabs = 4;
static const char* const kMgrTabName[kMgrTabs] = { "Playlists", "Themes", "Groups", "Pointer" };
static int g_ptrSel = 0;                        // the Pointer tab's rows
static const int kPtrRows = 5;
// the Groups tab: which categories are shown
static int g_hideTab = 0, g_hideSel = 0, g_hideTop = 0, g_hideButton = 0;
static bool g_hideOnButtons = false;
static const int kHideRows = 13;
static const char* const kMgrButtonLabel[kMgrButtons] = { "+   Add M3U playlist (URL)", "+   Add Xtream Codes", "+   Add Stalker portal (MAC address)" };
static const char* const kMgrButtonKind[kMgrButtons] = { "url", "xtream", "stalker" };

static void openManager(const std::string& notice)
{
	g_notice = notice;
	g_mgrConfirm = -1;
	g_mgrSel = g_active >= 0 ? g_active : 0;
	g_mgrOnButtons = g_sources.empty();
	g_fullscreen = false;
	g_keyboard = false;
	g_mgrTab = 0;
	g_themeSel = g_theme;
	g_hideTab = (int)(T - g_tabs);
	g_hideSel = g_hideTop = 0;
	g_hideOnButtons = false;
	g_screen = MANAGER;
}

// Shows or hides categories of one library. `group` is a category number, or -1 for all of them
// (then `hide` says which way).
static void setHidden(int tabIndex, int group, bool hide)
{
	Tab* const showing = T;
	T = &g_tabs[tabIndex];
	if (group >= 0)
	{
		const std::string& name = g_playlist.groups[(size_t)group];
		if (T->hidden.count(name))
			T->hidden.erase(name);
		else
			T->hidden.insert(name);
	}
	else
	{
		T->hidden.clear();
		if (hide)
			for (const std::string& name : g_playlist.groups)
				T->hidden.insert(name);
	}
	saveHidden();
	const int keep = selectedChannel();
	if (T->drill)
		closeDrill();
	computeCats();
	rebuildView(keep);
	T = showing;
}

// The Pointer tab: speed, size and shape of the pointer, plus what the screen is doing.
static void changeSetting(int row, int by)
{
	if (row == 0)
	{
		g_pointerSpeed += by;
		if (g_pointerSpeed < 1) g_pointerSpeed = 1;
		if (g_pointerSpeed > 10) g_pointerSpeed = 10;
	}
	else if (row == 1)
		g_pointerSize = (g_pointerSize + 3 + by) % 3;
	else if (row == 2)
	{
		int at = 0;
		const int count = (int)g_pointerNames.size();
		for (int i = 0; i < count; i++)
			if (g_pointerNames[(size_t)i] == g_pointerType)
				at = i;
		g_pointerType = g_pointerNames[(size_t)((at + count + by) % count)];
	}
	else if (row == 3)
		g_showFps = !g_showFps;
	else if (row == 4)
	{
		g_videoDetail = (g_videoDetail + 3 + by) % 3;
		applyVideoBudget();
	}
	saveSettings();
}

static void drawPointerTab()
{
	static const char* const labels[kPtrRows] = { "Pointer speed", "Pointer size", "Pointer", "Show frame rate", "Video picture" };
	static const char* const details[3] = { "Sharpest", "Balanced", "Smoothest" };
	static const char* const sizes[3] = { "Small", "Medium", "Large" };
	const float lx = 96, lw = 1120, top = 208, rowH = 96;
	gfx::roundRect(lx, top, lw, 1080 - top - 64, 16, kColumn);
	for (int row = 0; row < kPtrRows; row++)
	{
		const float ry = top + 18 + (float)row * rowH;
		const bool selected = (!pointerOn() && row == g_ptrSel) || hovering(lx + 14, ry, lw - 28, rowH - 10);
		if (selected)
			gfx::panel(lx + 14, ry, lw - 28, rowH - 10, 14, 2, kBorder, kSelected);
		gfx::text(lx + 40, ry + 28, 27, kText, labels[row]);

		// the value, between two arrows
		const float vx = lx + 470, vw = 520;
		const bool leftHover = hovering(vx, ry + 14, 60, 58), rightHover = hovering(vx + vw - 60, ry + 14, 60, 58);
		gfx::panel(vx, ry + 14, 60, 58, 12, leftHover ? 2.0f : 1.0f, leftHover ? kBorder : kLine, kChip);
		gfx::panel(vx + vw - 60, ry + 14, 60, 58, 12, rightHover ? 2.0f : 1.0f, rightHover ? kBorder : kLine, kChip);
		centredTight(vx, 60, ry + 22, 34, kText, "\xE2\x80\xB9");
		centredTight(vx + vw - 60, 60, ry + 22, 34, kText, "\xE2\x80\xBA");
		clickable(lx + 14, ry, lw - 28, rowH - 10, [row] { g_ptrSel = row; });
		clickable(vx, ry + 14, 60, 58, [row] { g_ptrSel = row; changeSetting(row, -1); });
		clickable(vx + vw - 60, ry + 14, 60, 58, [row] { g_ptrSel = row; changeSetting(row, 1); });

		const float cx = vx + 70, cw = vw - 140;
		if (row == 0)
		{
			// ten steps
			for (int step = 0; step < 10; step++)
				gfx::roundRect(cx + 10 + (float)step * (cw - 20) / 10, ry + 34, (cw - 20) / 10 - 6, 18, 5, step < g_pointerSpeed ? kBorder : kLine);
			char number[8];
			snprintf(number, sizeof(number), "%d", g_pointerSpeed);
			gfx::text(vx + vw + 20, ry + 28, 26, kAccent, number);
		}
		else if (row == 1)
			centredTight(cx, cw, ry + 29, 26, kAccent, sizes[g_pointerSize]);
		else if (row == 2)
			centredIn(cx, cw, ry + 29, 26, kAccent, g_pointerType);
		else if (row == 3)
			centredTight(cx, cw, ry + 29, 26, kAccent, g_showFps ? "On" : "Off");
		else
			centredTight(cx, cw, ry + 29, 26, kAccent, details[g_videoDetail]);
	}
	gfx::text(lx + 40, top + 18 + (float)kPtrRows * rowH + 6, 20, kMuted, "Video picture: Sharpest keeps the most detail; Smoothest keeps motion and the menus fluid.", lw - 80);

	// preview and measurements
	const float bx = 1250, bw = 590;
	gfx::panel(bx, top, bw, 230, 16, 1, kLine, kCard);
	gfx::text(bx + 26, top + 18, 23, kText, "Preview");
	gfx::roundRect(bx + 26, top + 60, bw - 52, 150, 12, kChip);
	drawPointerShape(g_pointerType, bx + bw / 2, top + 135);

	const float iy = top + 250;
	gfx::panel(bx, iy, bw, 330, 16, 1, kLine, kCard);
	gfx::text(bx + 26, iy + 18, 23, kText, "Screen");
	char line[96];
	snprintf(line, sizeof(line), "Drawing at %d x %d (the console scales it to your TV)", g_drawW, g_drawH);
	gfx::text(bx + 26, iy + 58, 21, kMuted, line);
	snprintf(line, sizeof(line), "%.0f frames per second (slowest %.0f ms)", g_fps, g_slowestMs);
	gfx::text(bx + 26, iy + 88, 21, g_fps >= 57 ? kMuted : kLive, line);
	snprintf(line, sizeof(line), "Video pictures accepted at %.0f MB per second", g_uploadRate / 1000.0);
	gfx::text(bx + 26, iy + 118, 21, kMuted, line);
	gfx::text(bx + 26, iy + 170, 23, kText, "Your own pointer");
	gfx::text(bx + 26, iy + 208, 20, kMuted, imagesSupported() ? "Upload Windows cursors (.cur, .ani) or PNG pictures to:" : "Upload Windows cursors (.cur, .ani) by FTP to:", bw - 52);
	gfx::text(bx + 26, iy + 236, 20, kAccent, g_dataDir + "/pointers", bw - 52);
	gfx::text(bx + 26, iy + 264, 20, kMuted, "They join the Pointer list at the next start.", bw - 52);
	gfx::text(bx + 26, iy + 292, 20, kMuted, "Animated cursors play.", bw - 52);
	gfx::text(bx, 1040, 20, kMuted, "Left / Right Change    L1 / R1 Tab    \xE2\x97\x8B / OPTIONS Back", bw);
}

static void pointerTabInput(Action action)
{
	switch (action)
	{
	case A_UP: g_ptrSel = (g_ptrSel + kPtrRows - 1) % kPtrRows; break;
	case A_DOWN: g_ptrSel = (g_ptrSel + 1) % kPtrRows; break;
	case A_LEFT: changeSetting(g_ptrSel, -1); break;
	case A_RIGHT:
	case A_OK: changeSetting(g_ptrSel, 1); break;
	case A_BACK:
	case A_MENU:
		if (g_haveBrowse)
			g_screen = BROWSE;
		else
			g_mgrTab = 0;
		break;
	default: break;
	}
}

static void drawGroupsTab()
{
	const float lx = 96, lw = 1120, top = 208, rowH = 58;
	const Tab& tab = g_tabs[g_hideTab];
	gfx::roundRect(lx, top, lw, 1080 - top - 64, 16, kColumn);
	gfx::text(lx + 26, top + 16, 24, kText, std::string(kTabName[g_hideTab]) + " groups");
	const int count = g_haveBrowse && tab.state == 2 ? (int)tab.playlist.groups.size() : 0;
	if (count > 0)
	{
		char shown[64];
		snprintf(shown, sizeof(shown), "%d shown, %d hidden", count - (int)tab.hidden.size(), (int)tab.hidden.size());
		gfx::text(lx + lw - 30 - gfx::textWidth(20, shown), top + 20, 20, kMuted, shown);
	}
	else
	{
		centredIn(lx, lw, top + 220, 26, kMuted, !g_haveBrowse ? "Open a playlist first" : tab.state == 2 ? "This library has no groups" : "Open this tab on the main screen first");
	}
	keepVisible(g_hideSel, g_hideTop, count, kHideRows);
	for (int row = 0; row < kHideRows; row++)
	{
		const int i = g_hideTop + row;
		if (i >= count)
			break;
		const float ry = top + 58 + (float)row * rowH;
		const std::string& name = tab.playlist.groups[(size_t)i];
		const bool hidden = tab.hidden.count(name) != 0;
		const bool selected = (!pointerOn() && !g_hideOnButtons && i == g_hideSel) || hovering(lx + 14, ry, lw - 28, rowH - 6);
		if (selected)
			gfx::panel(lx + 14, ry, lw - 28, rowH - 6, 12, 2, kBorder, kSelected);
		// a tick box: filled when the group is shown
		gfx::panel(lx + 32, ry + 11, 30, 30, 7, 2, hidden ? kTileLine : kBorder, hidden ? kDeep : kSelected);
		if (!hidden)
			centredTight(lx + 32, 30, ry + 13, 22, kAccent, "\xE2\x9C\x93");
		gfx::text(lx + 84, ry + 12, 25, hidden ? kMuted : kText, name, lw - 300);
		if (hidden)
			gfx::text(lx + lw - 130, ry + 15, 20, kMuted, "hidden");
		const int tabIndex = g_hideTab;
		clickable(lx + 14, ry, lw - 28, rowH - 6, [i, tabIndex] { g_hideOnButtons = false; g_hideSel = i; setHidden(tabIndex, i, false); });
	}
	drawScrollbar(lx + lw - 10, top + 58, (float)kHideRows * rowH, g_hideTop, count, kHideRows);

	const float bx = 1250, bw = 590;
	static const char* const labels[2] = { "Show all groups", "Hide all groups" };
	for (int b = 0; b < 2; b++)
	{
		const float by = top + (float)b * 88;
		const bool selected = (!pointerOn() && g_hideOnButtons && g_hideButton == b) || hovering(bx, by, bw, 74);
		gfx::panel(bx, by, bw, 74, 16, 2, selected ? kBorder : kLine, selected ? kSelected : kChip);
		gfx::text(bx + 30, by + 22, 26, selected ? kAccent : kText, labels[b]);
		const int tabIndex = g_hideTab;
		clickable(bx, by, bw, 74, [b, tabIndex] { g_hideOnButtons = true; g_hideButton = b; setHidden(tabIndex, -1, b == 1); });
	}
	const float iy = top + 186;
	gfx::panel(bx, iy, bw, 232, 16, 1, kLine, kCard);
	gfx::text(bx + 26, iy + 20, 23, kText, "Hidden groups");
	gfx::text(bx + 26, iy + 58, 20, kMuted, "A hidden group leaves the group list, and its", bw - 52);
	gfx::text(bx + 26, iy + 86, 20, kMuted, "channels leave the \"All\" list. Favorites stay.", bw - 52);
	gfx::text(bx + 26, iy + 130, 20, kMuted, "On the main screen, \xE2\x96\xA1 on a group hides it.", bw - 52);
	gfx::text(bx + 26, iy + 174, 20, kMuted, "L2 / R2 here: Live TV, Movies or Series groups.", bw - 52);
	gfx::text(bx, 1040, 20, kMuted, "\xE2\x9C\x95 Show / Hide    L1 / R1 Tab    \xE2\x97\x8B / OPTIONS Back", bw);
}

static void groupsTabInput(Action action)
{
	const Tab& tab = g_tabs[g_hideTab];
	const int count = g_haveBrowse && tab.state == 2 ? (int)tab.playlist.groups.size() : 0;
	switch (action)
	{
	case A_UP:
	case A_DOWN:
		if (g_hideOnButtons)
			g_hideButton = 1 - g_hideButton;
		else
			moveSelection(g_hideSel, count, action == A_UP ? -1 : 1);
		break;
	case A_RIGHT: g_hideOnButtons = true; break;
	case A_LEFT: g_hideOnButtons = false; break;
	case A_OK:
		if (count == 0)
			break;
		if (g_hideOnButtons)
			setHidden(g_hideTab, -1, g_hideButton == 1);
		else
			setHidden(g_hideTab, g_hideSel, false);
		break;
	case A_TAB_PREV:
	case A_TAB_NEXT:
		g_hideTab = (g_hideTab + (action == A_TAB_PREV ? 2 : 1)) % 3;
		g_hideSel = g_hideTop = 0;
		break;
	case A_BACK:
	case A_MENU:
		if (g_haveBrowse)
			g_screen = BROWSE;
		else
			g_mgrTab = 0;
		break;
	default: break;
	}
}

static void chooseTheme(int index)
{
	g_themeSel = index;
	applyTheme(index);
	writeTextFile(g_dataDir + "/theme.txt", kThemes[g_theme].name);
	logLine("theme: %s", kThemes[g_theme].name);
}

// A small picture of the interface in a theme's colours.
static void drawThemePreview(const Theme& t, float x, float y, float w, float h)
{
	gfx::rect(x, y, w, h / 2, t.backgroundTop);
	gfx::rect(x, y + h / 2, w, h - h / 2, t.backgroundBottom);
	gfx::rect(x + 8, y + 22, w * 0.42f, h - 30, t.column);                 // the channel list
	gfx::roundRect(x + 8, y + 6, w * 0.3f, 10, 4, t.chip);                  // the top bar
	gfx::roundRect(x + 14 + w * 0.3f, y + 6, w * 0.55f, 10, 4, t.chip);
	for (int row = 0; row < 4; row++)
	{
		const float ry = y + 28 + (float)row * 21;
		if (row == 1)
			gfx::panel(x + 12, ry - 3, w * 0.42f - 8, 19, 5, 2, t.border, t.selected);
		gfx::roundRect(x + 17, ry, 12, 12, 3, t.tile);
		gfx::rect(x + 35, ry + 2, w * 0.2f, 4, row == 1 ? t.accent : t.text);
		gfx::rect(x + 35, ry + 9, w * 0.13f, 3, t.muted);
	}
	gfx::rect(x + w * 0.42f + 14, y + 22, w * 0.58f - 22, (h - 30) * 0.6f, 0x000000ff);   // the picture
	gfx::panel(x + w * 0.42f + 14, y + 26 + (h - 30) * 0.6f, w * 0.58f - 22, (h - 30) * 0.4f - 4, 5, 1, t.line, t.card);
	gfx::rect(x + w * 0.42f + 22, y + 34 + (h - 30) * 0.6f, w * 0.25f, 4, t.accent);
}

static void drawThemes()
{
	const float lx = 96, lw = 1120, top = 208, rowH = 196;
	gfx::roundRect(lx, top, lw, 1080 - top - 64, 16, kColumn);
	for (int i = 0; i < kThemeCount; i++)
	{
		const Theme& t = kThemes[i];
		const float ry = top + 14 + (float)i * rowH;
		const bool selected = (!pointerOn() && i == g_themeSel) || hovering(lx + 14, ry, lw - 28, rowH - 10);
		if (selected)
			gfx::panel(lx + 14, ry, lw - 28, rowH - 10, 14, 2, kBorder, kSelected);
		gfx::panel(lx + 30, ry + 14, 284, 158, 10, 2, kLine, 0x00000000);
		drawThemePreview(t, lx + 32, ry + 16, 280, 154);
		gfx::text(lx + 344, ry + 30, 32, i == g_theme ? kAccent : kText, t.name);
		gfx::text(lx + 344, ry + 80, 21, kMuted, t.description, lw - 380);
		if (i == g_theme)
		{
			gfx::panel(lx + lw - 170, ry + 30, 120, 34, 17, 1, kBorder, kSelected);
			centredTight(lx + lw - 170, 120, ry + 37, 18, kAccent, "IN USE");
		}
		else
			gfx::text(lx + 344, ry + 124, 20, kMuted, "\xE2\x9C\x95  Use this theme");
		clickable(lx + 14, ry, lw - 28, rowH - 10, [i] { chooseTheme(i); });
	}

	const float bx = 1250, bw = 590;
	gfx::panel(bx, top, bw, 196, 16, 1, kLine, kCard);
	gfx::text(bx + 26, top + 20, 23, kText, "Themes");
	gfx::text(bx + 26, top + 58, 20, kMuted, "A theme changes the colours and the shape of", bw - 52);
	gfx::text(bx + 26, top + 86, 20, kMuted, "the corners everywhere in the app.", bw - 52);
	gfx::text(bx + 26, top + 130, 20, kMuted, "Your choice is remembered.", bw - 52);
	gfx::text(bx, 1040, 20, kMuted, g_haveBrowse ? "\xE2\x9C\x95 Use    L1 / R1 Tab    \xE2\x97\x8B / OPTIONS Back" : "\xE2\x9C\x95 Use    L1 / R1 Tab", bw);
}

static void managerButton(int which)
{
	g_mgrOnButtons = true;
	g_mgrButton = which;
	openForm(kMgrButtonKind[which], -1);
}

static void editSource(int index)
{
	if (index < 0 || index >= (int)g_sources.size())
		return;
	g_mgrOnButtons = false;
	g_mgrSel = index;
	if (g_sources[(size_t)index].type == "file")
		g_notice = "A file has nothing to edit; upload a new file to replace it";
	else
		openForm(g_sources[(size_t)index].type, index);
}

static void deleteSource(int index)
{
	if (index < 0 || index >= (int)g_sources.size())
		return;
	const Source source = g_sources[(size_t)index];
	if ((source.type == "file" || source.type == "url") && source.a.compare(0, 12, "/app0/assets") == 0)
	{
		g_notice = "That playlist is built into the app, so it cannot be removed here";
		g_mgrConfirm = -1;
		return;
	}
	if (source.type == "file")
		remove(source.a.c_str());                // a file uploaded to the console
	cancelLink();
	std::string builtIn;
	if (source.type == "url" && readTextFile(g_dataDir + "/playlist-url.txt", builtIn) && trim(builtIn) == source.a)
		remove((g_dataDir + "/playlist-url.txt").c_str());
	remove((g_dataDir + "/cache-" + sourceId(source) + ".m3u").c_str());

	g_sources.erase(g_sources.begin() + index);
	saveSources(g_dataDir, g_sources);
	if (index == g_active)
	{
		player::stop();
		gfx::videoClear();
		g_playing = -1;
		g_nowOn = false;
		g_active = -1;
		g_haveBrowse = false;
	}
	else if (index < g_active)
		g_active--;
	if (g_mgrSel >= (int)g_sources.size())
		g_mgrSel = (int)g_sources.size() - 1;
	if (g_mgrSel < 0)
		g_mgrSel = 0;
	g_mgrOnButtons = g_sources.empty();
	g_mgrConfirm = -1;
	g_notice = "Removed \"" + source.name + "\"";
}

static void drawManager()
{
	// heading, as on IPTVnator's start page
	gfx::panel(96, 40, 92, 92, 18, 2, kBorderDim, kChip);
	centredTight(96, 92, 58, 50, kAccent, "\xE2\x9A\x99");
	gfx::text(212, 46, 40, kText, "Settings");
	gfx::text(212, 98, 24, kMuted, "Playlists, themes, groups, pointer and display");

	// tabs
	for (int tab = 0; tab < kMgrTabs; tab++)
	{
		const float tx = 96 + (float)tab * 212, tw = 200;
		const bool current = tab == g_mgrTab, hover = hovering(tx, 148, tw, 46);
		gfx::panel(tx, 148, tw, 46, 23, current || hover ? 2.0f : 1.0f, current || hover ? kBorder : kLine, current ? kSelected : kChip);
		centredTight(tx, tw, 158, 22, current ? kAccent : kText, kMgrTabName[tab]);
		clickable(tx, 148, tw, 46, [tab] { g_mgrTab = tab; g_mgrConfirm = -1; });
	}
	gfx::text(96 + (float)kMgrTabs * 212 + 14, 160, 20, kMuted, "L1 / R1  Switch tab");
	if (g_mgrTab == 1)
	{
		drawThemes();
		return;
	}
	if (g_mgrTab == 2)
	{
		drawGroupsTab();
		return;
	}
	if (g_mgrTab == 3)
	{
		drawPointerTab();
		return;
	}

	const float lx = 96, lw = 1120, top = 252, rowH = 96;
	gfx::roundRect(lx, top - 44, lw, 1080 - top - 20, 16, kColumn);
	gfx::text(lx + 26, top - 30, 24, kText, "Playlists");
	char count[32];
	snprintf(count, sizeof(count), "%d", (int)g_sources.size());
	gfx::text(lx + lw - 30 - gfx::textWidth(22, count), top - 28, 22, kMuted, count);

	float y = top + 14;
	if (!g_notice.empty())
	{
		gfx::panel(lx + 14, y, lw - 28, 52, 12, 1, kLive, kLiveBack);
		gfx::text(lx + 34, y + 14, 22, kText, g_notice, lw - 70);
		y += 64;
	}
	if (g_sources.empty())
	{
		centredIn(lx, lw, y + 180, 30, kMuted, "No playlists yet");
		centredIn(lx, lw, y + 230, 22, kMuted, "Add one with the buttons on the right");
	}
	keepVisible(g_mgrSel, g_mgrTop, (int)g_sources.size(), kMgrRows);
	for (int row = 0; row < kMgrRows; row++)
	{
		const int i = g_mgrTop + row;
		if (i >= (int)g_sources.size())
			break;
		const Source& source = g_sources[(size_t)i];
		const float ry = y + (float)row * rowH;
		const bool selected = (!pointerOn() && !g_mgrOnButtons && i == g_mgrSel) || hovering(lx + 14, ry, lw - 28, rowH - 8);
		if (selected)
			gfx::panel(lx + 14, ry, lw - 28, rowH - 8, 14, 2, kBorder, kSelected);
		gfx::panel(lx + 30, ry + 16, 56, 56, 12, 1, kTileLine, kTile);
		centredTight(lx + 30, 56, ry + 33, 19, kText, source.type == "xtream" ? "XC" : source.type == "stalker" ? "STB" : source.type == "url" ? "URL" : "M3U");
		gfx::text(lx + 106, ry + 12, 28, i == g_active ? kAccent : kText, source.name, lw - 440);
		gfx::text(lx + 106, ry + 50, 20, kMuted, std::string(sourceKind(source)) + "  \xC2\xB7  " + sourceDetail(source), lw - 440);
		if (g_mgrConfirm == i)
			gfx::text(lx + lw - 300, ry + 30, 22, kLive, "\xE2\x96\xA1 again to remove");
		else if (i == g_active && g_haveBrowse)
		{
			gfx::panel(lx + lw - 150, ry + 28, 104, 30, 15, 1, kBorder, kSelected);
			centredTight(lx + lw - 150, 104, ry + 33, 18, kAccent, "OPEN");
		}
		clickable(lx + 14, ry, lw - 28, rowH - 8, [i] { g_mgrOnButtons = false; g_mgrSel = i; startLoading(i); });
		if (source.type != "file" && g_mgrConfirm != i)
		{
			// an Edit button on each row, for the pointer
			const float ex = lx + lw - 290, ew = 110;
			const bool editHover = hovering(ex, ry + 22, ew, 42);
			gfx::panel(ex, ry + 22, ew, 42, 12, editHover ? 2.0f : 1.0f, editHover ? kBorder : kTileLine, kTile);
			centredTight(ex, ew, ry + 32, 20, editHover ? kAccent : kText, "Edit");
			clickable(ex, ry + 22, ew, 42, [i] { editSource(i); });
		}
	}

	// actions
	const float bx = 1250, bw = 590;
	for (int b = 0; b < kMgrButtons; b++)
	{
		const float by = top - 44 + (float)b * 88;
		const bool selected = (!pointerOn() && g_mgrOnButtons && g_mgrButton == b) || hovering(bx, by, bw, 74);
		gfx::panel(bx, by, bw, 74, 16, 2, selected ? kBorder : kLine, selected ? kSelected : kChip);
		gfx::text(bx + 30, by + 22, 26, selected ? kAccent : kText, kMgrButtonLabel[b]);
		clickable(bx, by, bw, 74, [b] { managerButton(b); });
	}
	const float iy = top - 44 + (float)kMgrButtons * 88 + 10;
	gfx::panel(bx, iy, bw, 252, 16, 1, kLine, kCard);
	gfx::text(bx + 26, iy + 20, 23, kText, "M3U files from your computer");
	gfx::text(bx + 26, iy + 58, 20, kMuted, "Upload .m3u or .m3u8 files by FTP into:");
	gfx::text(bx + 26, iy + 88, 20, kAccent, g_dataDir + "/playlists", bw - 52);
	gfx::text(bx + 26, iy + 118, 20, kMuted, "They appear in this list at the next start.");
	gfx::text(bx + 26, iy + 166, 23, kText, "Automatic updates");
	gfx::text(bx + 26, iy + 204, 20, kMuted, "A playlist is fetched fresh each time it is opened.", bw - 52);

	gfx::text(bx, 1040, 20, kMuted, g_haveBrowse ? "\xE2\x9C\x95 Open    \xE2\x96\xB3 Edit    \xE2\x96\xA1 Remove    \xE2\x97\x8B / OPTIONS Back" : "\xE2\x9C\x95 Open    \xE2\x96\xB3 Edit    \xE2\x96\xA1 Remove", bw);
}

static void managerInput(Action action)
{
	const int count = (int)g_sources.size();
	if (action != A_FAVOURITE)
		g_mgrConfirm = -1;
	if (action == A_PAGE_UP || action == A_PAGE_DOWN)
	{
		g_mgrTab = (g_mgrTab + (action == A_PAGE_UP ? kMgrTabs - 1 : 1)) % kMgrTabs;
		return;
	}
	if (g_mgrTab == 2)
	{
		groupsTabInput(action);
		return;
	}
	if (g_mgrTab == 3)
	{
		pointerTabInput(action);
		return;
	}
	if (g_mgrTab == 1)
	{
		switch (action)
		{
		case A_UP: g_themeSel = (g_themeSel + kThemeCount - 1) % kThemeCount; break;
		case A_DOWN: g_themeSel = (g_themeSel + 1) % kThemeCount; break;
		case A_OK: chooseTheme(g_themeSel); break;
		case A_BACK:
		case A_MENU:
			if (g_haveBrowse)
				g_screen = BROWSE;
			else
				g_mgrTab = 0;
			break;
		default: break;
		}
		return;
	}
	switch (action)
	{
	case A_UP:
	case A_DOWN:
		if (g_mgrOnButtons)
			g_mgrButton = (g_mgrButton + (action == A_UP ? kMgrButtons - 1 : 1)) % kMgrButtons;
		else
			moveSelection(g_mgrSel, count, action == A_UP ? -1 : 1);
		break;
	case A_RIGHT: g_mgrOnButtons = true; break;
	case A_LEFT: if (count > 0) g_mgrOnButtons = false; break;
	case A_OK:
		if (g_mgrOnButtons)
			managerButton(g_mgrButton);
		else if (count > 0)
			startLoading(g_mgrSel);
		break;
	case A_SEARCH: if (!g_mgrOnButtons && count > 0) editSource(g_mgrSel); break;
	case A_FAVOURITE:
		if (g_mgrOnButtons || count == 0)
			break;
		if (g_mgrConfirm == g_mgrSel)
			deleteSource(g_mgrSel);
		else
			g_mgrConfirm = g_mgrSel;
		break;
	case A_BACK:
	case A_MENU:                                 // Options again closes settings
		if (g_haveBrowse)
			g_screen = BROWSE;
		break;
	default: break;
	}
}

// ---- start-up --------------------------------------------------------------

static SDL_Window* createWindow(int& width, int& height)
{
	SDL_DisplayMode mode = {};
	if (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0)
	{
		width = mode.w;
		height = mode.h;
	}
	logLine("video: display reported as %dx%d at %d Hz", width, height, mode.refresh_rate);
	const int modes = SDL_GetNumDisplayModes(0);
	for (int i = 0; i < modes && i < 12; i++)
	{
		SDL_DisplayMode offered;
		if (SDL_GetDisplayMode(0, i, &offered) == 0)
			logLine("video: the display offers %dx%d at %d Hz", offered.w, offered.h, offered.refresh_rate);
	}
	if (g_resolution == 1)
	{
		width = 1920;
		height = 1080;
	}
	else if (g_resolution == 2)
	{
		width = 3840;
		height = 2160;
	}
	if (g_resolution != 0)
		logLine("video: asking for %dx%d (chosen in Settings)", width, height);

	// The PS5 OpenGL layer offers desktop OpenGL; other requests are fallbacks.
	static const int attempts[4][3] = {
		{ SDL_GL_CONTEXT_PROFILE_CORE, 4, 6 }, { SDL_GL_CONTEXT_PROFILE_CORE, 3, 3 }, { SDL_GL_CONTEXT_PROFILE_ES, 3, 0 }, { 0, 0, 0 } };
	for (const auto& attempt : attempts)
	{
		SDL_GL_ResetAttributes();
		if (attempt[1] != 0)
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, attempt[0]);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, attempt[1]);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, attempt[2]);
		}
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_Window* window = SDL_CreateWindow("IPTV", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
		if (!window)
		{
			logLine("video: no window with OpenGL %d.%d: %s", attempt[1], attempt[2], SDL_GetError());
			continue;
		}
		if (SDL_GL_CreateContext(window))
		{
			logLine("video: OpenGL %d.%d context created", attempt[1], attempt[2]);
			return window;
		}
		logLine("video: no OpenGL %d.%d context: %s", attempt[1], attempt[2], SDL_GetError());
		SDL_DestroyWindow(window);
	}
	return nullptr;
}

static void scrollBy(int steps)
{
	if (steps == 0 || g_keyboard || g_form || g_screen == LOADING)
		return;
	const auto move = [steps](int& selected, int count) {
		if (count <= 0)
			return;
		selected += steps;
		if (selected < 0) selected = 0;
		if (selected > count - 1) selected = count - 1;
	};
	if (g_screen == MANAGER)
	{
		if (g_mgrTab == 3)
		{
			g_ptrSel += steps > 0 ? 1 : -1;
			if (g_ptrSel < 0) g_ptrSel = 0;
			if (g_ptrSel > kPtrRows - 1) g_ptrSel = kPtrRows - 1;
		}
		else if (g_mgrTab == 2)
		{
			const Tab& tab = g_tabs[g_hideTab];
			g_hideOnButtons = false;
			move(g_hideSel, g_haveBrowse && tab.state == 2 ? (int)tab.playlist.groups.size() : 0);
		}
		else if (g_mgrTab == 1)
			move(g_themeSel, kThemeCount);
		else if (!g_sources.empty())
		{
			g_mgrOnButtons = false;
			move(g_mgrSel, (int)g_sources.size());
		}
		return;
	}
	if (g_fullscreen)
		return;
	if (g_tab == kAccountTab)
		return;
	const bool categories = inGroups() && (pointerOn() ? g_pointerX >= 90 && g_pointerX < 490 : g_focus == F_CATEGORIES);
	if (categories)
	{
		int place = catPlace();
		move(place, (int)T->cats.size());
		selectCategory(T->cats[(size_t)place]);
	}
	else
		move(g_sel, (int)g_view.size());
}

int es_main(int, char**)
{
	g_dataDir = usableFolder("/data/iptv") ? "/data/iptv" : "/app0";
	{
		// keep the log of the run before this one, for looking into problems afterwards
		std::string previous;
		if (readTextFile(g_dataDir + "/iptv-log.txt", previous) && !previous.empty())
			writeTextFile(g_dataDir + "/iptv-log-previous.txt", previous);
	}
	g_log = fopen((g_dataDir + "/iptv-log.txt").c_str(), "wb");
	logLine("IPTV for PS5 starting; data folder: %s", g_dataDir.c_str());
	logMissingSystemFunctions();
	mkdir((g_dataDir + "/playlists").c_str(), 0777);

	if (SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		logLine("SDL video did not start: %s", SDL_GetError());
		return 1;
	}
	if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0)
		logLine("input: joysticks unavailable: %s", SDL_GetError());
	if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
		logLine("input: controller layouts unavailable: %s", SDL_GetError());

	loadSettings();
	g_resolution = 0;       // the graphics layer on this console draws at 1080p only; the choice was removed
	// A resolution chosen in Settings is tried once. If the app did not get as far as showing
	// pictures with it last time, it goes back to automatic so it cannot get stuck failing to start.
	const std::string trying = g_dataDir + "/resolution-trying.txt";
	std::string marker;
	if (readTextFile(trying, marker))
	{
		logLine("video: the resolution chosen in Settings did not work last time; back to automatic");
		g_resolution = 0;
		saveSettings();
		remove(trying.c_str());
	}
	if (g_resolution != 0)
		writeTextFile(trying, "1");

	int width = 1920, height = 1080;
	SDL_Window* window = createWindow(width, height);
	if (!window && g_resolution != 0)
	{
		logLine("video: that resolution was refused; using the display's own");
		g_resolution = 0;
		saveSettings();
		width = 1920;
		height = 1080;
		window = createWindow(width, height);
	}
	if (!window)
	{
		logLine("video: no window could be created");
		return 1;
	}
	if (SDL_GL_SetSwapInterval(1) != 0)
		logLine("video: could not tie drawing to the screen's refresh: %s", SDL_GetError());
	int drawW = width, drawH = height;
	SDL_GL_GetDrawableSize(window, &drawW, &drawH);
	g_drawW = drawW;
	g_drawH = drawH;
	player::setDisplaySize(drawW, drawH);
	logLine("video: drawing at %dx%d", drawW, drawH);

	{
		// the theme chosen last time
		std::string saved;
		int index = 0;
		if (readTextFile(g_dataDir + "/theme.txt", saved))
			for (int i = 0; i < kThemeCount; i++)
				if (trim(saved) == kThemes[i].name)
					index = i;
		applyTheme(index);
		logLine("theme: %s", kThemes[index].name);
	}

	std::string error;
	if (!gfx::init(drawW, drawH, "/app0/assets/font.ttf", error))
	{
		logLine("gfx: %s", error.c_str());
		return 1;
	}

	// How fast this console's graphics layer takes in pictures decides how large video pictures can be.
	g_uploadRate = gfx::measureUploads();
	applyVideoBudget();

	for (int i = 0; i < SDL_NumJoysticks(); i++)
		openPad(i);
	player::init();

	// The saved playlists, plus any files found on the console. The one used last opens by itself.
	g_sources = loadSources(g_dataDir);
	saveSources(g_dataDir, g_sources);
	logLine("playlists: %d known", (int)g_sources.size());
	if (g_sources.empty())
		openManager("");
	else
	{
		int index = 0;
		std::string last;
		if (readTextFile(g_dataDir + "/active.txt", last))
			for (int i = 0; i < (int)g_sources.size(); i++)
				if (sourceId(g_sources[(size_t)i]) == trim(last))
					index = i;
		startLoading(index);
	}
	logLine("start-up finished; entering the main loop");

	bool running = true;
	double before = nowSeconds();
	// frame timing: measured every second for the display in Settings, logged every ten
	int framesThisSecond = 0, framesLogged = 0, shown = 0;
	double secondStart = before, logStart = before, slowest = 0, slowestLogged = 0, frameStart = before;
	while (running)
	{
		{
			const double t = nowSeconds();
			const double took = (t - frameStart) * 1000.0;
			frameStart = t;
			if (took > slowest) slowest = took;
			if (took > slowestLogged) slowestLogged = took;
			framesThisSecond++;
			framesLogged++;
			if (t - secondStart >= 1.0)
			{
				g_fps = framesThisSecond / (t - secondStart);
				g_slowestMs = slowest;
				framesThisSecond = 0;
				slowest = 0;
				secondStart = t;
			}
			if (t - logStart >= 10.0)
			{
				int decoded = 0, picturesShown = 0, picturesSkipped = 0;
				player::takeCounts(decoded, picturesShown, picturesSkipped);
				logLine("frames: %.1f per second, slowest %.0f ms, %d draw batches, drawing at %dx%d; video: %.1f decoded, %.1f shown, %.1f skipped per second, %.1f ms to hand over each picture",
					framesLogged / (t - logStart), slowestLogged, gfx::drawCalls(), g_drawW, g_drawH,
					decoded / (t - logStart), picturesShown / (t - logStart), picturesSkipped / (t - logStart),
					g_uploads ? g_uploadSeconds * 1000.0 / g_uploads : 0.0);
				g_uploadSeconds = 0;
				g_uploads = 0;
				framesLogged = 0;
				slowestLogged = 0;
				logStart = t;
			}
			if (shown < 200 && ++shown == 200 && g_resolution != 0)
				remove(trying.c_str());                 // the chosen resolution works
		}
		SDL_Event event;
		while (SDL_PollEvent(&event))
			onEvent(event, running);
		repeatDirections();
		const double now = nowSeconds();
		const double elapsed = now - before > 0.1 ? 0.1 : now - before;
		movePointer(elapsed);
		scrollBy(scrollSteps(elapsed));
		before = now;

		if (g_screen == LOADING && g_loadState != 0)
		{
			if (g_loadState == 1)
			{
				stopGenreLoading();
				for (Tab& tab : g_tabs)
					tab = Tab();
				T = &g_tabs[0];
				g_tab = 0;
				{
					std::lock_guard<std::mutex> lock(g_loadMutex);
					g_playlist = std::move(g_loadedPlaylist);
					g_loadedPlaylist = Playlist();
				}
				buildGroups();
				onPlaylistLoaded();
				g_screen = BROWSE;
			}
			else
				openManager(g_loadError);
		}

		for (const Action action : g_actions)
		{
			if (g_screen == LOADING)
			{
				// Only one thing can be done while waiting: give up and go to Settings.
				// (Ignored for the first moment, so the press that started the load cannot cancel it.)
				if (nowSeconds() - g_loadStarted > 0.4)
				{
					if (action == A_BACK || action == A_MENU)
						cancelLoading();
					else if (action == A_OK && pointerOn())
					{
						clickPointer();
						g_hits.clear();
					}
				}
				continue;
			}
			if (action == A_OK && pointerOn())
			{
				clickPointer();
				g_hits.clear(); // what was on screen may have changed
			}
			else if (g_keyboard)
				keyboardInput(action);
			else if (g_form)
				formInput(action);
			else if (g_screen == BROWSE)
				browseInput(action);
			else if (g_screen == MANAGER)
				managerInput(action);
		}
		g_actions.clear();

		g_hits.clear();
		gfx::begin(kBackground);
		drawBackground();
		if (g_screen == LOADING)
		{
			const int waited = (int)(nowSeconds() - g_loadStarted);
			char title[64];
			if (waited >= 3)
				snprintf(title, sizeof(title), "Loading the playlist...  %d s", waited);
			else
				snprintf(title, sizeof(title), "Loading the playlist...");
			centredIn(0, 1920, 440, 44, kText, title);
			if (g_active >= 0)
				centredIn(0, 1920, 510, 26, kMuted, g_sources[(size_t)g_active].name);
			const long long received = httpReceivedBytes();
			if (received > 0)
			{
				char progress[64];
				snprintf(progress, sizeof(progress), "%.1f MB received", (double)received / 1048576.0);
				centredIn(0, 1920, 552, 22, kAccent, progress);
			}
			// the way out, always available
			const float bx = 720, by = 600, bw = 480, bh = 72;
			const bool hover = hovering(bx, by, bw, bh);
			gfx::panel(bx, by, bw, bh, 16, 2, hover ? kBorder : kBorderDim, hover ? kSelected : kChip);
			centredTight(bx, bw, by + 20, 26, hover ? kAccent : kText, "\xE2\x9A\x99   Cancel and open Settings");
			clickable(bx, by, bw, bh, [] { cancelLoading(); });
			centredIn(0, 1920, 700, 22, kMuted, "\xE2\x97\x8B or OPTIONS  Cancel and open Settings");
		}
		else if (g_screen == MANAGER)
			drawManager();
		else
			drawBrowse();
		if (g_screen != LOADING)
		{
			if (g_form)
				drawForm();
			if (g_keyboard)
				drawKeyboard();
		}
		if (g_showFps)
		{
			char text[64];
			snprintf(text, sizeof(text), "%.0f fps   %dx%d", g_fps, g_drawW, g_drawH);
			const float w = gfx::textWidth(20, text) + 24;
			gfx::roundRect(1920 - w - 6, 4, w, 30, 8, 0x000000b0);
			gfx::text(1920 - w + 6, 9, 20, 0xffffffff, text);
		}
		drawPointer();
		gfx::end();
		SDL_GL_SwapWindow(window);
	}

	player::stop();
	return 0;
}

// A function the start-up file asks for that the console does not offer to a title.
extern "C" mode_t umask(mode_t mask) { (void)mask; return 0; }
