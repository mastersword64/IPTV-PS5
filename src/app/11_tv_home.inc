// ---- the TV layout ------------------------------------------------------------------------
// A second way of laying the app out, in the manner of a television's own home screen: the
// picture fills the screen and everything else floats over it; a slim rail down the left replaces
// the bar and tabs; whatever is in focus turns white. The app's data and behaviour are shared
// with the classic layout - only the drawing and the way focus moves differ. Screens that have
// not been drawn this way yet use their classic form.

static const uint32_t kTvBack = 0x0a0c12ff, kTvWhite = 0xffffffff, kTvSoft = 0xffffffb4, kTvDim = 0xffffff78, kTvInk = 0x0e121eff;
static bool g_tvOnRail = false;                 // focus is on the rail
static int g_tvRailSel = 0;
struct TvRailItem { const char* icon; const char* name; int tab; };   // tab -1: Settings
static const int kTvRailCount = 7;
static const TvRailItem kTvRail[kTvRailCount] = {
	{ "\xE2\x8C\x82", "Home", 4 }, { "", "Search", 5 }, { "\xE2\x96\xB6", "Live TV", 0 }, { "\xE2\x96\xA6", "Movies", 1 },
	{ "\xE2\x98\xB0", "Series", 2 }, { "\xE2\x98\xBA", "Account", 3 }, { "\xE2\x9A\x99", "Settings", -1 } };
static float g_tvRailWidth = 92, g_tvRowScroll = 0;

static void tvRailGo(int place)
{
	g_tvOnRail = false;
	if (kTvRail[place].tab < 0)
		openManager("");
	else
		switchTab(kTvRail[place].tab);
}

// A rounded label: "2021", "★ 7.8"
static float tvChip(float x, float y, const std::string& words, bool solid = false)
{
	const float w = gfx::textWidth(19, words) + 28;
	gfx::roundRect(x, y, w, 34, 17, solid ? kTvWhite : 0xffffff2a);
	gfx::text(x + 14, y + 7, 19, solid ? kTvInk : kTvWhite, words);
	return w + 10;
}

static void drawTvRail()
{
	glide(g_tvRailWidth, g_tvOnRail ? 260.0f : 92.0f, 16.0f);
	const float w = g_tvRailWidth;
	const bool open = w > 180;
	gfx::rect(0, 0, w, 1080, g_tvOnRail ? 0x080a10f0 : 0x080a1090);
	for (int i = 0; i < kTvRailCount; i++)
	{
		const float y = i == kTvRailCount - 1 ? 980.0f : 130.0f + (float)i * 88;
		const bool here = kTvRail[i].tab == g_tab;
		const bool focused = (g_tvOnRail && !pointerOn() && i == g_tvRailSel) || hovering(8, y, w - 16, 68);
		if (focused)
			gfx::roundRect(14, y, w - 28, 68, 34, kTvWhite);
		else if (here)
			gfx::roundRect(14, y, w - 28, 68, 34, 0xffffff26);
		const uint32_t colour = focused ? kTvInk : here ? kTvWhite : kTvSoft;
		if (kTvRail[i].icon[0])
			centredTight(14, 64, y + 16, 30, colour, kTvRail[i].icon);
		else
		{
			// a magnifying glass: a ring and its handle
			const float keep = gfx::radiusScale();
			gfx::setRadiusScale(1.0f);
			gfx::panel(34, y + 20, 22, 22, 11, 3, colour, 0);
			gfx::triangle(52, y + 40, 55, y + 37, 62, y + 47, colour);
			gfx::triangle(52, y + 40, 62, y + 47, 59, y + 50, colour);
			gfx::setRadiusScale(keep);
		}
		if (open)
		{
			gfx::bold(focused);
			gfx::text(90, y + 20, 23, colour, kTvRail[i].name);
			gfx::bold(false);
		}
		clickable(8, y, w - 16, 68, [i] { tvRailGo(i); });
	}
}

static uint32_t tvTint(const std::string& name);

// The colour behind a screen eases from one title's colour to the next instead of jumping.
static float g_tvWash[3] = { 40, 50, 80 };

static void tvWash(const std::string& name, float fadeFrom)
{
	const uint32_t tint = tvTint(name);
	const float target[3] = { (float)((tint >> 24) & 255), (float)((tint >> 16) & 255), (float)((tint >> 8) & 255) };
	for (int i = 0; i < 3; i++)
		glide(g_tvWash[i], target[i], 6.0f);
	const uint32_t colour = ((uint32_t)g_tvWash[0] << 24) | ((uint32_t)g_tvWash[1] << 16) | ((uint32_t)g_tvWash[2] << 8);
	gfx::rect(0, 0, 1920, 1080, kTvBack);
	gfx::rect(0, 0, 1920, 1080, colour | 0x70);
	gfx::roundRect(1180, -260, 1100, 1100, 550, 0xffffff12);                               // soft light, upper right
	gfx::roundRect(1420, 120, 760, 760, 380, 0xffffff10);
	// shading: dark on the left for the words, and from `fadeFrom` downwards for what sits below
	for (int i = 0; i < 64; i++)
	{
		const float t = 1.0f - (float)i / 63.0f;
		gfx::rect((float)i * 21, 0, 21, 1080, (kTvBack & 0xffffff00) | (uint32_t)(225.0f * t * sqrtf(t)));
	}
	for (int i = 0; i < 56; i++)
	{
		const float t = (float)i / 55.0f;
		gfx::rect(0, fadeFrom + (float)i * 6, 1920, 6, (kTvBack & 0xffffff00) | (uint32_t)(250.0f * t * sqrtf(t)));
	}
	gfx::rect(0, fadeFrom + 56 * 6, 1920, 1080 - fadeFrom - 56 * 6, (kTvBack & 0xffffff00) | 0xfa);
}

// A picture appears over a quarter of a second the first time it is drawn, instead of popping in.
static float artFade(int image)
{
	static std::unordered_map<int, double> firstSeen;
	const double now = nowSeconds();
	const auto found = firstSeen.find(image);
	if (found == firstSeen.end())
	{
		firstSeen[image] = now;
		return 0;
	}
	const double age = (now - found->second) / 0.28;
	return age >= 1 ? 1.0f : (float)age;
}

// The words about the thing in focus fade in afresh when the focus moves to something else.
static float tvHeroFade(const std::string& key)
{
	static std::string shown;
	static double since = 0;
	if (key != shown)
	{
		shown = key;
		since = nowSeconds();
	}
	const double age = (nowSeconds() - since) / 0.22;
	return age >= 1 ? 1.0f : (float)(0.15 + 0.85 * age);
}

static uint32_t faded(uint32_t colour, float amount)
{
	return (colour & 0xffffff00) | (uint32_t)((float)(colour & 0xff) * amount);
}

// The white frame of focus travels from card to card.
static float g_tvFrame[4] = { 0, 0, 0, 0 }, g_tvFrameWant[4] = { 0, 0, 0, 0 };
static bool g_tvFrameSet = false, g_tvFrameLive = false;

static void tvFocusAt(float x, float y, float w, float h)
{
	g_tvFrameWant[0] = x; g_tvFrameWant[1] = y; g_tvFrameWant[2] = w; g_tvFrameWant[3] = h;
	g_tvFrameSet = true;
}

static void tvDrawFocus()
{
	if (!g_tvFrameSet)
	{
		g_tvFrameLive = false;                      // nothing in focus here: next time, start in place
		return;
	}
	g_tvFrameSet = false;
	const float dx = g_tvFrameWant[0] - g_tvFrame[0], dy = g_tvFrameWant[1] - g_tvFrame[1];
	const bool far = dx > 900 || dx < -900 || dy > 500 || dy < -500;
	for (int i = 0; i < 4; i++)
	{
		if (!g_tvFrameLive || far || pointerOn())
			g_tvFrame[i] = g_tvFrameWant[i];
		else
			glide(g_tvFrame[i], g_tvFrameWant[i], 22.0f);
	}
	g_tvFrameLive = true;
	gfx::panel(g_tvFrame[0] - 3, g_tvFrame[1] - 3, g_tvFrame[2] + 6, g_tvFrame[3] + 6, 17, 3.5f, kTvWhite, 0);
}

static bool g_tvOnHero = false;                 // focus is on the buttons under the big title
static int g_tvHeroButton = 0;

// A colour for something that has no picture, always the same for the same name.
static uint32_t tvTint(const std::string& name)
{
	static const uint32_t palette[10] = { 0xb5483aff, 0x2c6fb8ff, 0x2a8a70ff, 0xc08a2cff, 0x7a4cc0ff,
		0xb83f78ff, 0x3a94b8ff, 0x5f9a3eff, 0x596079ff, 0xc2603cff };
	unsigned hash = 2166136261u;
	for (const char c : name)
		hash = (hash ^ (unsigned char)c) * 16777619u;
	return palette[hash % 10];
}

// Which part of a picture fills a box of another shape (the rest is cut off, a little more from
// the bottom than the top, since what matters in a poster is usually in its upper half).
static void coverCrop(int pw, int ph, float w, float h, float& u0, float& v0, float& u1, float& v1)
{
	u0 = v0 = 0;
	u1 = v1 = 1;
	const float picture = (float)pw / (float)ph, box = w / h;
	if (picture > box)
	{
		const float kept = box / picture;
		u0 = (1 - kept) / 2;
		u1 = u0 + kept;
	}
	else
	{
		const float kept = picture / box;
		v0 = (1 - kept) * 0.28f;
		v1 = v0 + kept;
	}
}

// One card of the TV layout. `kind`: 0 an upright poster, 1 a wide still, 2 a channel logo on a tile.
static void tvCard(const Channel& channel, int kind, float bx, float by, float cardW, float cardH, bool hot, double at, double length)
{
	const float pop = hot ? g_cardPop : 1.0f, w = cardW * pop, h = cardH * pop, x = bx - (w - cardW) / 2, y = by - (h - cardH) / 2;
	if (hot)
		gfx::roundRect(x - 10, y - 10, w + 20, h + 20, 24, 0xffffff38);                  // white glow
	const uint32_t tint = tvTint(channel.name);
	const int art = channel.logo.empty() ? -1 : logoImage(channel.logo, kind != 2);
	int aw = 0, ah = 0;
	const bool have = art >= 0 && gfx::imageSize(art, aw, ah) && aw > 0 && ah > 0;
	if (kind == 2)
	{
		// a channel: its logo, whole, on a dark tile touched with its colour
		gfx::roundRect(x, y, w, h, 14, 0x1b1f2cff);
		gfx::roundRect(x, y, w, h, 14, (tint & 0xffffff00) | 0x30);
		if (have)
		{
			const float fit = ((w - 64) / (float)aw) < ((h - 44) / (float)ah) ? (w - 64) / (float)aw : (h - 44) / (float)ah;
			gfx::image(art, x + (w - (float)aw * fit) / 2, y + (h - (float)ah * fit) / 2, (float)aw * fit, (float)ah * fit);
		}
		else
		{
			gfx::bold(true);
			centredTight(x, w, y + h / 2 - 28, 42, kTvWhite, initials(channel.name));
			gfx::bold(false);
		}
	}
	else if (have)
	{
		float u0, v0, u1, v1;
		coverCrop(aw, ah, w, h, u0, v0, u1, v1);
		const float seen = artFade(art);
		if (seen < 1)
			gfx::roundRect(x, y, w, h, 14, tint);       // its colour shows through while the picture arrives
		gfx::picture(art, x, y, w, h, 14, u0, v0, u1, v1, seen);
	}
	else
	{
		// no picture: its colour, its initials
		gfx::roundRect(x, y, w, h, 14, tint);
		gfx::roundRect(x, y + h * 0.45f, w, h * 0.55f, 14, 0x00000048);
		gfx::bold(true);
		centredTight(x, w, y + h * 0.5f - (kind == 1 ? 40.0f : 46.0f), kind == 1 ? 54.0f : 60.0f, 0xffffffe6, initials(channel.name));
		gfx::bold(false);
	}
	if (length > 0)
	{
		gfx::roundRect(x + 12, y + h - 16, w - 24, 6, 3, 0x000000a8);
		gfx::roundRect(x + 12, y + h - 16, (w - 24) * (float)(at / length > 1 ? 1 : at / length), 6, 3, kTvWhite);
	}
	if (hot)
		tvFocusAt(x, y, w, h);                                                          // the white frame of focus goes here
}

// A film or series from Home, opened on its own page in its library.
static void openHomeDetails(const HomeItem& item)
{
	if (item.tab < 1 || item.tab > 2)
		return;
	switchTab(item.tab);
	ensureLists();
	openDetails(addChannel(item.channel, false));
}

// The buttons under the big title: what can be done with the thing in focus.
static std::vector<std::string> tvHeroButtons(const HomeItem& item)
{
	std::vector<std::string> buttons;
	const bool series = isSeriesItem(item.channel.url);
	if (!series)
		buttons.push_back(item.length > 0 ? "\xE2\x96\xB6   Resume  \xC2\xB7  " + clockText(item.length - item.at) + " left" : item.tab == 0 ? "\xE2\x96\xB6   Watch" : "\xE2\x96\xB6   Play");
	if (item.tab > 0)
		buttons.push_back(series ? "Episodes" : "Details");
	return buttons;
}

static void pressTvHero(int button)
{
	const HomeRow& row = g_home[(size_t)g_homeRow];
	const HomeItem item = row.items[(size_t)row.sel];
	const bool series = isSeriesItem(item.channel.url);
	g_tvOnHero = false;
	if (button == 0 && !series)
		playHomeItem(item);
	else
		openHomeDetails(item);
}

static void drawTvHome()
{
	const float keepScale = gfx::radiusScale();
	gfx::setRadiusScale(1.0f);
	gfx::rect(0, 0, 1920, 1080, kTvBack);

	HomeRow& row = g_home[(size_t)g_homeRow];
	const HomeItem& item = row.items[(size_t)row.sel];
	// what the focused thing says about itself, when its library has been opened and knows more
	Channel info = item.channel;
	if (item.tab >= 0 && item.tab < 3)
	{
		const Tab& library = g_tabs[item.tab];
		const auto known = library.byUrl.find(item.channel.url);
		if (known != library.byUrl.end() && known->second < (int)library.playlist.channels.size())
			info = library.playlist.channels[(size_t)known->second];
	}

	// backdrop, the whole screen: the title's own colour; a channel also shows its logo, large
	const bool channelRow = item.tab == 0;
	tvWash(info.name, 300);
	if (channelRow)
	{
		const int logo = !info.logo.empty() ? logoImage(info.logo) : -1;
		int pw = 0, ph = 0;
		if (logo >= 0 && gfx::imageSize(logo, pw, ph) && pw > 0 && ph > 0 && pw >= 100)
		{
			// only when the logo is large enough to stay sharp at this size
			const float fit = 220.0f / (float)(pw > ph ? pw : ph);
			gfx::image(logo, 1560 - (float)pw * fit / 2, 250 - (float)ph * fit / 2, (float)pw * fit, (float)ph * fit, faded(0xffffffff, artFade(logo)));
		}
	}

	// the focused thing, large
	const float x0 = 150;
	std::string label = row.title;
	for (char& c : label)
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 32);
	gfx::bold(true);
	{
		const float w = gfx::textWidth(17, label) + 30;
		gfx::roundRect(x0, 128, w, 34, 17, kTvWhite);
		gfx::text(x0 + 15, 136, 17, kTvInk, label);
	}
	const float in = tvHeroFade(info.url);
	gfx::text(x0, 178 + (1 - in) * 10, 68, faded(kTvWhite, in), info.name, 1150);
	gfx::bold(false);
	float cx = x0;
	if (!info.year.empty()) cx += tvChip(cx, 280, info.year);
	if (!info.rating.empty()) cx += tvChip(cx, 280, "\xE2\x98\x85 " + info.rating);
	if (!info.length.empty()) cx += tvChip(cx, 280, info.length);
	if (!info.genres.empty() && cx < x0 + 600) cx += tvChip(cx, 280, info.genres);
	else if (!info.group.empty() && cx < x0 + 600) cx += tvChip(cx, 280, info.group);
	if (!info.plot.empty())
		drawWrapped(x0, 334, 900, 23, kTvSoft, info.plot, 2);
	else if (channelRow)
	{
		const Programme* now = onNow(info.url);
		gfx::text(x0, 334, 23, kTvSoft, now ? "On now:  " + now->title : "Live channel", 900);
	}

	// the buttons under it
	const std::vector<std::string> buttons = tvHeroButtons(item);
	if (g_tvHeroButton > (int)buttons.size() - 1)
		g_tvHeroButton = (int)buttons.size() - 1;
	float bx = x0;
	gfx::bold(true);
	for (int b = 0; b < (int)buttons.size(); b++)
	{
		const float bw = gfx::textWidth(21, buttons[(size_t)b]) + 64;
		const bool focused = (g_tvOnHero && !pointerOn() && g_tvHeroButton == b) || hovering(bx, 418, bw, 62);
		const bool primary = b == 0;
		if (focused)
			gfx::roundRect(bx - 8, 410, bw + 16, 78, 39, 0xffffff40);
		gfx::roundRect(bx, 418, bw, 62, 31, focused ? kTvWhite : primary ? 0xffffffd8 : 0xffffff2c);
		centredTight(bx, bw, 437, 21, focused || primary ? kTvInk : kTvWhite, buttons[(size_t)b]);
		clickable(bx, 418, bw, 62, [b] { g_tvHeroButton = b; pressTvHero(b); });
		bx += bw + 18;
	}
	gfx::bold(false);
	if (item.length > 0)
	{
		gfx::roundRect(x0, 500, 460, 6, 3, 0xffffff40);
		gfx::roundRect(x0, 500, 460 * (float)(item.at / item.length > 1 ? 1 : item.at / item.length), 6, 3, kTvWhite);
	}
	const std::string clock = clockNow();
	gfx::text(1860 - gfx::textWidth(26, clock), 50, 26, kTvSoft, clock);

	// the rows: the one in use sits at a fixed height, the others follow below
	const float rowsTop = 556;
	std::vector<float> rowTop(g_home.size());
	float total = 0;
	for (size_t i = 0; i < g_home.size(); i++)
	{
		rowTop[i] = total;
		total += g_home[i].wide ? 300.0f : g_home[i].posters ? 392.0f : 262.0f;
	}
	glide(g_tvRowScroll, rowTop[(size_t)g_homeRow], 13.0f);
	gfx::clip(0, rowsTop - 26, 1920, 1080 - rowsTop + 26);
	for (size_t r = 0; r < g_home.size(); r++)
	{
		HomeRow& list = g_home[r];
		const float ry = rowsTop + rowTop[r] - g_tvRowScroll;
		if (ry > 1080 || ry < rowsTop - 440)
			continue;
		const bool rowFocus = (int)r == g_homeRow && !pointerOn() && !g_tvOnRail && !g_tvOnHero;
		gfx::bold(true);
		gfx::text(x0, ry, 26, (int)r == g_homeRow ? kTvWhite : kTvDim, list.title);
		gfx::bold(false);
		const int kind = list.wide ? 1 : list.posters ? 0 : 2;
		const float cardW = kind == 1 ? 300.0f : kind == 0 ? 180.0f : 240.0f, cardH = kind == 1 ? 169.0f : kind == 0 ? 270.0f : 135.0f, cell = cardW + 24;
		const int visible = (int)((1920 - x0 - 30) / cell);
		float target = list.scroll;
		if ((float)list.sel < target) target = (float)list.sel;
		if ((float)list.sel > target + (float)visible - 1) target = (float)list.sel - (float)visible + 1;
		glide(list.scroll, (float)(int)(target + 0.5f));
		for (int pass = 0; pass < 2; pass++)
			for (int i = (int)list.scroll - 1; i < (int)list.scroll + visible + 2 && i < (int)list.items.size(); i++)
			{
				if (i < 0)
					continue;
				const HomeItem& entry = list.items[(size_t)i];
				const float cardX = x0 + ((float)i - list.scroll) * cell, cardY = ry + 50;
				const bool hot = (rowFocus && i == list.sel) || hovering(cardX, cardY, cardW, cardH);
				if ((pass == 1) != hot)
					continue;
				tvCard(entry.channel, kind, cardX, cardY, cardW, cardH, hot, entry.at, entry.length);
				gfx::bold(hot);
				gfx::text(cardX, cardY + cardH + (hot ? 22.0f : 12.0f), 19, hot ? kTvWhite : kTvSoft, entry.channel.name, cardW);
				gfx::bold(false);
				const int rowIndex = (int)r;
				clickable(cardX, cardY, cardW, cardH + 40, [rowIndex, i] {
					if (rowIndex < (int)g_home.size() && i < (int)g_home[(size_t)rowIndex].items.size())
					{
						g_tvOnRail = g_tvOnHero = false;
						g_homeRow = rowIndex;
						g_home[(size_t)rowIndex].sel = i;
						const HomeItem chosen = g_home[(size_t)rowIndex].items[(size_t)i];
						playHomeItem(chosen);
					}
				});
			}
	}
	gfx::unclip();
	if (g_popFor != g_homeRow * 1000 + row.sel)
	{
		g_popFor = g_homeRow * 1000 + row.sel;
		g_cardPop = 1;
	}
	glide(g_cardPop, 1.08f, 18.0f);

	tvDrawFocus();
	drawTvRail();
	gfx::setRadiusScale(keepScale);
}

static bool tvRailInput(Action action);

static void tvHomeInput(Action action)
{
	if (tvRailInput(action))
		return;
	const HomeRow& row = g_home[(size_t)g_homeRow];
	if (g_tvOnHero)
	{
		const int count = (int)tvHeroButtons(row.items[(size_t)row.sel]).size();
		switch (action)
		{
		case A_LEFT:
			if (g_tvHeroButton > 0)
				g_tvHeroButton--;
			else
			{
				g_tvOnHero = false;
				g_tvOnRail = true;
				g_tvRailSel = 0;
			}
			break;
		case A_RIGHT: if (g_tvHeroButton < count - 1) g_tvHeroButton++; break;
		case A_DOWN:
		case A_BACK: g_tvOnHero = false; break;
		case A_OK: pressTvHero(g_tvHeroButton); break;
		default: break;
		}
		return;
	}
	if (action == A_UP && g_homeRow == 0)
	{
		g_tvOnHero = true;                          // above the first row: the buttons
		g_tvHeroButton = 0;
		return;
	}
	if ((action == A_LEFT && row.sel == 0) || action == A_BACK)
	{
		g_tvOnRail = true;                          // off the left edge: the rail
		g_tvRailSel = 0;
		return;
	}
	homeInput(action);
}

static void openMore();
static bool g_more = false;

