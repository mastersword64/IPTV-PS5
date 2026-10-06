// ---- settings: pointer and display ------------------------------------------------

static int g_pointerSpeed = 4;                  // 1 (slow) to 10 (fast)
static int g_pointerSize = 1;                   // 0 small, 1 medium, 2 large
static std::string g_pointerType = "Arrow";     // a shape drawn by the app, or the name of a pointer file
static bool g_showFps = false;
static int g_resolution = 0;                    // 0 automatic, 1 1080p, 2 4K (used at the next start)
static int g_videoDetail = 1;                   // 0 sharpest, 1 balanced, 2 smoothest
static int g_pictureFit = 0;                    // full screen: 0 fit, 1 zoom, 2 stretch
static int g_subtitleSize = 1;                  // 0 small, 1 medium, 2 large
static double g_subtitleDelay = 0, g_audioDelay = 0;   // seconds (see player.h for which way each goes)
static bool g_autoNext = true;                  // play the next episode when one ends
// A theme file is lines of "name=value". Colours are six or eight hex digits (RRGGBB or RRGGBBAA).
// Anything a file leaves out is taken from the first built-in theme.
static void loadThemeFiles(const std::string& dataDir)
{
	static std::deque<std::string> words;               // names and descriptions live here
	const std::string folder = dataDir + "/themes";
	if (mkdir(folder.c_str(), 0777) == 0)
	{
		// first run: leave an example, which is itself a working theme
		writeTextFile(folder + "/ocean.txt",
			"name=Ocean\ndescription=An example theme file. Copy it, change the colours, and upload it to this folder.\n"
			"background=06141f\nbackground_bottom=0a2a3a\npanel=ffffff0d\nchip=ffffff16\ntile=ffffff1c\ntile_line=ffffff30\nrail=ffffff0d\n"
			"selected=2ec4b640\nborder=5ee6d8\nborder_dim=5ee6d866\nband=2ec4b626\ntext=eefcff\nmuted=8fb3bf\naccent=5ee6d8\n"
			"live=ff7a85\nlive_back=3a1c26\nline=ffffff24\ndeep=03090f\ncard=ffffff0f\ndialog=0c2230\nhover=ffffff12\nscroll=ffffff50\npointer=ffffff\nradius=1.3\n");
	}
	DIR* dir = opendir(folder.c_str());
	if (!dir)
		return;
	std::vector<std::string> files;
	while (const dirent* entry = readdir(dir))
	{
		const std::string name = entry->d_name;
		if (name.size() > 4 && name.compare(name.size() - 4, 4, ".txt") == 0)
			files.push_back(name);
	}
	closedir(dir);
	std::sort(files.begin(), files.end());
	for (const std::string& file : files)
	{
		std::string text;
		if (!readTextFile(folder + "/" + file, text))
			continue;
		Theme theme = kBuiltInThemes[0];
		std::string name = file.substr(0, file.size() - 4), description = "From the file " + file;
		size_t pos = 0;
		int colours = 0;
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
			if (key == "name") { name = value; continue; }
			if (key == "description") { description = value; continue; }
			if (key == "radius") { theme.radius = (float)atof(value.c_str()); if (theme.radius < 0.3f || theme.radius > 3) theme.radius = 1; continue; }
			std::string hex = value;
			if (!hex.empty() && hex[0] == '#')
				hex.erase(0, 1);
			if (hex.size() != 6 && hex.size() != 8)
				continue;
			const uint32_t colour = hex.size() == 6 ? ((uint32_t)strtoul(hex.c_str(), nullptr, 16) << 8) | 0xff : (uint32_t)strtoul(hex.c_str(), nullptr, 16);
			struct Named { const char* key; uint32_t Theme::* field; };
			static const Named named[] = {
				{ "background", &Theme::backgroundTop }, { "background_bottom", &Theme::backgroundBottom }, { "pane", &Theme::pane }, { "panel", &Theme::column },
				{ "chip", &Theme::chip }, { "tile", &Theme::tile }, { "tile_line", &Theme::tileLine }, { "rail", &Theme::rail }, { "selected", &Theme::selected },
				{ "border", &Theme::border }, { "border_dim", &Theme::borderDim }, { "band", &Theme::band }, { "text", &Theme::text }, { "muted", &Theme::muted },
				{ "accent", &Theme::accent }, { "live", &Theme::live }, { "live_back", &Theme::liveBack }, { "line", &Theme::line }, { "deep", &Theme::deep },
				{ "card", &Theme::card }, { "dialog", &Theme::dialog }, { "hover", &Theme::hover }, { "scroll", &Theme::scroll }, { "pointer", &Theme::pointer } };
			for (const Named& entry : named)
				if (key == entry.key)
				{
					theme.*entry.field = colour;
					colours++;
					if (key == "background" && text.find("background_bottom") == std::string::npos)
						theme.backgroundBottom = colour;
				}
		}
		if (colours == 0 || kThemes.size() >= 24)
			continue;
		words.push_back(name);
		theme.name = words.back().c_str();
		words.push_back(description);
		theme.description = words.back().c_str();
		kThemes.push_back(theme);
		logLine("theme: \"%s\" loaded from %s", theme.name, file.c_str());
	}
}

// ================= profiles: favourites and history of one's own =================
struct Profile { int id; std::string name; };
static std::vector<Profile> g_profiles = { { 0, "Main" } };
static int g_profile = 0;                       // which of them is in use (an index)
static bool g_menuSounds = true;                // soft tones on moving, choosing and going back

static std::string profileSuffix()
{
	const int id = g_profile >= 0 && g_profile < (int)g_profiles.size() ? g_profiles[(size_t)g_profile].id : 0;
	return id ? "-p" + std::to_string(id) : std::string();
}

static int g_layout = 0;                        // how the app is laid out: 0 classic, 1 TV
static bool g_autoPreview = true;               // TV layout: resting on a live channel starts it playing
static bool g_deinterlace = true;               // smooth the combing of interlaced broadcasts
static std::string g_tvCountry;                 // TV layout: only this country's groups are listed ("" for all)
static int g_wallSort = 0;                      // the poster wall: 0 as listed, 1 A to Z, 2 newest first, 3 best rated first
static std::string g_pin;                       // locks the Groups settings (empty: no lock)
static bool g_pinOpen = false;                  // the right PIN has been given since the app started
static int g_sleepMinutes = 0;                  // sleep timer (0: off); not kept between runs
static double g_sleepAt = 0;
static int g_clockOffsetMinutes = 0;            // the clock shown: this many minutes from world time (UTC)
static bool g_clockSet = false;                 // the viewer has chosen it (otherwise the console's own is tried)
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

static int g_profileWanted = 0;                 // the saved profile's id, matched to the list once that is read

// The profiles are kept in a file of their own: one "id<TAB>name" per line.
static void saveProfiles()
{
	std::string text;
	for (const Profile& profile : g_profiles)
		text += std::to_string(profile.id) + "\t" + profile.name + "\n";
	writeTextFile(g_dataDir + "/profiles.txt", text);
}

static void loadProfiles()
{
	std::string text;
	if (readTextFile(g_dataDir + "/profiles.txt", text))
	{
		std::vector<Profile> read;
		size_t pos = 0;
		while (pos < text.size())
		{
			size_t end = text.find('\n', pos);
			if (end == std::string::npos)
				end = text.size();
			const std::string line = text.substr(pos, end - pos);
			pos = end + 1;
			const size_t tab = line.find('\t');
			if (tab != std::string::npos && !trim(line.substr(tab + 1)).empty())
				read.push_back({ atoi(line.c_str()), trim(line.substr(tab + 1)) });
		}
		if (!read.empty() && read[0].id == 0)
			g_profiles = read;
	}
	g_profile = 0;
	for (int i = 0; i < (int)g_profiles.size(); i++)
		if (g_profiles[(size_t)i].id == g_profileWanted)
			g_profile = i;
}

static void saveSettings()
{
	char text[900];
	snprintf(text, sizeof(text), "pointer_speed=%d\npointer_size=%d\npointer_type=%s\nshow_fps=%d\nresolution=%d\nvideo_detail=%d\n"
		"picture_fit=%d\nsubtitle_size=%d\nsubtitle_delay_ms=%d\naudio_delay_ms=%d\nauto_next=%d\nclock_offset_min=%d\nclock_set=%d\n"
		"wall_sort=%d\npin=%s\nlayout=%d\nauto_preview=%d\ndeinterlace=%d\nmenu_sounds=%d\nprofile=%d\n",
		g_pointerSpeed, g_pointerSize, g_pointerType.c_str(), g_showFps ? 1 : 0, g_resolution, g_videoDetail,
		g_pictureFit, g_subtitleSize, (int)(g_subtitleDelay * 1000), (int)(g_audioDelay * 1000), g_autoNext ? 1 : 0,
		g_clockOffsetMinutes, g_clockSet ? 1 : 0, g_wallSort, g_pin.c_str(), g_layout, g_autoPreview ? 1 : 0, g_deinterlace ? 1 : 0, g_menuSounds ? 1 : 0,
		g_profile >= 0 && g_profile < (int)g_profiles.size() ? g_profiles[(size_t)g_profile].id : 0);
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
			else if (key == "picture_fit") g_pictureFit = atoi(value.c_str());
			else if (key == "subtitle_size") g_subtitleSize = atoi(value.c_str());
			else if (key == "subtitle_delay_ms") g_subtitleDelay = atoi(value.c_str()) / 1000.0;
			else if (key == "audio_delay_ms") g_audioDelay = atoi(value.c_str()) / 1000.0;
			else if (key == "auto_next") g_autoNext = value != "0";
			else if (key == "clock_offset_min") g_clockOffsetMinutes = atoi(value.c_str());
			else if (key == "clock_set") g_clockSet = value == "1";
			else if (key == "wall_sort") g_wallSort = atoi(value.c_str());
			else if (key == "layout") g_layout = atoi(value.c_str()) == 1 ? 1 : 0;
			else if (key == "auto_preview") g_autoPreview = value != "0";
			else if (key == "deinterlace") g_deinterlace = value != "0";
			else if (key == "menu_sounds") g_menuSounds = value != "0";
			else if (key == "profile") g_profileWanted = atoi(value.c_str());
			else if (key == "pin") g_pin = value.size() == 4 ? value : std::string();
		}
	}
	if (g_pointerSpeed < 1) g_pointerSpeed = 1;
	if (g_pointerSpeed > 10) g_pointerSpeed = 10;
	if (g_pointerSize < 0 || g_pointerSize > 2) g_pointerSize = 1;
	if (g_resolution < 0 || g_resolution > 2) g_resolution = 0;
	if (g_videoDetail < 0 || g_videoDetail > 2) g_videoDetail = 1;
	if (g_pictureFit < 0 || g_pictureFit > 2) g_pictureFit = 0;
	if (g_subtitleSize < 0 || g_subtitleSize > 2) g_subtitleSize = 1;
	if (g_subtitleDelay < -10 || g_subtitleDelay > 10) g_subtitleDelay = 0;
	if (g_audioDelay < -1 || g_audioDelay > 1) g_audioDelay = 0;
	player::setSubtitleDelay(g_subtitleDelay);
	player::setAudioDelay(g_audioDelay);
	if (!g_clockSet)
	{
		// start from the console's own idea of local time, when it has one
		const time_t now = time(nullptr);
		struct tm local, world;
		if (localtime_r(&now, &local) && gmtime_r(&now, &world))
		{
			int minutes = (local.tm_hour - world.tm_hour) * 60 + (local.tm_min - world.tm_min);
			if (local.tm_mday != world.tm_mday)
				minutes += (local.tm_year > world.tm_year || (local.tm_year == world.tm_year && local.tm_yday > world.tm_yday)) ? 1440 : -1440;
			g_clockOffsetMinutes = minutes;
		}
	}
	if (g_clockOffsetMinutes < -720 || g_clockOffsetMinutes > 840) g_clockOffsetMinutes = 0;
	if (g_wallSort < 0 || g_wallSort > 3) g_wallSort = 0;

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

// The TV layout has a look of its own - black, white and translucent - which Settings, the
// keyboard and the pop-ups take on while that layout is in use. The classic layout uses the
// colour theme chosen in Settings.
static const Theme kTvLook = { "TV", "",
	0x0a0c12ff, 0x0a0c12ff,
	0x00000000, 0xffffff0c, 0xffffff1a, 0xffffff20, 0xffffff3a, 0xffffff10,
	0xffffff2e, 0xffffffff, 0xffffff70, 0xffffff14,
	0xffffffff, 0xffffff96, 0xffffffff, 0xff5c66ff, 0x40151aff,
	0xffffff30, 0x05060aff, 0xffffff10, 0x14171fff, 0xffffff14, 0xffffff60, 0xffffffff, 1.5f };

static void applyLook()
{
	if (g_layout != 1)
	{
		applyTheme(g_theme);
		return;
	}
	const Theme& t = kTvLook;
	kBackground = t.backgroundTop; kBackgroundBottom = t.backgroundBottom;
	kPane = t.pane; kColumn = t.column; kChip = t.chip; kTile = t.tile; kTileLine = t.tileLine; kRail = t.rail;
	kSelected = t.selected; kBorder = t.border; kBorderDim = t.borderDim; kBand = t.band;
	kText = t.text; kMuted = t.muted; kAccent = t.accent; kLive = t.live; kLiveBack = t.liveBack;
	kLine = t.line; kDeep = t.deep; kCard = t.card; kDialog = t.dialog; kHover = t.hover; kScroll = t.scroll; kPointer = t.pointer;
	gfx::setRadiusScale(t.radius);
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

