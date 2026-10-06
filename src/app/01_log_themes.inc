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

static const Theme kBuiltInThemes[] = {
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
// The themes on offer: the built-in ones, then any found as files in the app's "themes" folder.
static std::vector<Theme> kThemes(kBuiltInThemes, kBuiltInThemes + sizeof(kBuiltInThemes) / sizeof(kBuiltInThemes[0]));
#define kThemeCount ((int)kThemes.size())
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

