// ---- Home: your own things, one press from playing ----------------------------------------

struct HomeItem { Channel channel; int tab = 0; double at = 0, length = 0; };
struct HomeRow { std::string title; bool posters = false; bool wide = false; std::vector<HomeItem> items; int sel = 0; float scroll = 0; };
static std::vector<HomeRow> g_home;
static int g_homeRow = 0, g_homeButton = 0;
static float g_homeScroll = 0;

static std::vector<Channel> savedRecords(int tab, const char* kind)
{
	std::string text;
	Playlist saved;
	if (readTextFile(listPathFor(tab, kind), text))
		parseM3U(text, saved);
	return saved.channels;
}

static void buildHome()
{
	std::vector<HomeRow> rows;
	// keep each row's place when the lists are rebuilt
	const auto place = [](const std::string& title) {
		for (const HomeRow& row : g_home)
			if (row.title == title)
				return row.sel;
		return 0;
	};

	HomeRow watching;
	watching.title = "Continue watching";
	watching.posters = true;
	watching.wide = true;                              // the TV layout shows these as wide stills
	for (int tab = 1; tab <= 2; tab++)
	{
		// where each was left: "seconds, length, address" per line
		std::unordered_map<std::string, std::pair<double, double>> left;
		std::string text;
		if (readTextFile(listPathFor(tab, "resume"), text))
		{
			size_t pos = 0;
			while (pos < text.size())
			{
				size_t end = text.find('\n', pos);
				if (end == std::string::npos)
					end = text.size();
				const std::string line = text.substr(pos, end - pos);
				pos = end + 1;
				const size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
				if (b != std::string::npos)
					left[line.substr(b + 1)] = { atof(line.substr(0, a).c_str()), atof(line.substr(a + 1, b - a - 1).c_str()) };
			}
		}
		for (const Channel& channel : savedRecords(tab, "recent"))
		{
			const auto found = left.find(channel.url);
			if (found == left.end() || found->second.second <= 0 || watching.items.size() >= 30)
				continue;
			HomeItem item;
			item.channel = channel;
			item.tab = tab;
			item.at = found->second.first;
			item.length = found->second.second;
			watching.items.push_back(item);
		}
	}
	if (!watching.items.empty())
		rows.push_back(watching);

	const Tab& live = g_tabs[0];
	const auto liveRow = [&](const char* title, const std::vector<int>& list, size_t most) {
		HomeRow row;
		row.title = title;
		for (const int channel : list)
		{
			if (row.items.size() >= most || channel < 0 || channel >= (int)live.playlist.channels.size())
				continue;
			HomeItem item;
			item.channel = live.playlist.channels[(size_t)channel];
			row.items.push_back(item);
		}
		if (!row.items.empty())
			rows.push_back(row);
	};
	{
		// favourite channels, gathered by the country their group belongs to
		std::vector<std::pair<std::string, std::vector<int>>> byCountry;
		for (const int channel : live.favourites)
		{
			if (channel < 0 || channel >= (int)live.playlist.channels.size())
				continue;
			const std::string country = countryOf(live.playlist.channels[(size_t)channel].group);
			bool placed = false;
			for (auto& entry : byCountry)
				if (entry.first == country)
				{
					entry.second.push_back(channel);
					placed = true;
				}
			if (!placed)
				byCountry.push_back({ country, { channel } });
		}
		std::sort(byCountry.begin(), byCountry.end(), [](const std::pair<std::string, std::vector<int>>& a, const std::pair<std::string, std::vector<int>>& b) {
			if ((a.first == "Other") != (b.first == "Other"))
				return b.first == "Other";
			return a.first < b.first;
		});
		if (byCountry.size() <= 1)
			liveRow("Favorite channels", live.favourites, 40);
		else
			for (const auto& entry : byCountry)
			{
				const std::string title = "Favorite channels  \xC2\xB7  " + entry.first;
				liveRow(title.c_str(), entry.second, 40);
			}
	}
	liveRow("Recently watched channels", live.recent, 20);

	static const char* const titles[3] = { "", "Favorite movies", "Favorite series" };
	for (int tab = 1; tab <= 2; tab++)
	{
		HomeRow row;
		row.title = titles[tab];
		row.posters = true;
		for (const Channel& channel : savedRecords(tab, "favourites"))
		{
			if (row.items.size() >= 40)
				break;
			HomeItem item;
			item.channel = channel;
			item.tab = tab;
			row.items.push_back(item);
		}
		if (!row.items.empty())
			rows.push_back(row);
	}
	for (HomeRow& row : rows)
	{
		row.sel = place(row.title);
		if (row.sel > (int)row.items.size() - 1)
			row.sel = (int)row.items.size() - 1;
		row.scroll = (float)row.sel;
	}
	g_home.swap(rows);
	if (g_homeRow > (int)g_home.size() - 1)
		g_homeRow = g_home.empty() ? 0 : (int)g_home.size() - 1;
}

// A library that has not been opened yet still has its saved lists; they are read before anything
// is added to them, so a film started from Home does not wipe out what was there.
static void ensureLists()
{
	if (T->state == 2 || !g_groupChannels.empty())
		return;
	g_playlist = Playlist();
	g_playlist.lazy = true;
	buildGroups();
	g_byUrl.clear();
	g_isFavourite.clear();
	loadResume();
	loadList("favourites", g_favourites);
	loadList("recent", g_recent);
	for (const int channel : g_favourites)
		g_isFavourite[(size_t)channel] = 1;
}

static void playHomeItem(const HomeItem& item)
{
	if (isSeriesItem(item.channel.url))
	{
		// a favourite series: go to its page in the Series tab
		switchTab(item.tab);
		ensureLists();
		openDetails(addChannel(item.channel, false));
		return;
	}
	Tab* const showingTab = T;
	const int showing = g_tab;
	T = &g_tabs[item.tab];
	g_tab = item.tab;
	int index = -1;
	if (item.tab == 0)
	{
		const auto found = g_byUrl.find(item.channel.url);
		index = found != g_byUrl.end() ? found->second : g_playlist.lazy && !g_groupChannels.empty() ? addChannel(item.channel, false) : -1;
	}
	else
	{
		ensureLists();
		index = addChannel(item.channel, false);
	}
	if (index >= 0)
		startChannel(index, item.at);
	T = showingTab;
	g_tab = showing;
	g_playing = -1;
	if (index >= 0)
	{
		g_fullscreen = true;
		g_overlayUntil = 0;
		showControlsSoon();
	}
}

static void drawHome()
{
	const float top = kColumnTop;
	if (g_home.empty())
	{
		centredIn(0, 1920, top + 170, 40, kText, "Welcome");
		centredIn(0, 1920, top + 232, 24, kMuted, "Your favorites and the things you are part-way through will appear here.");
		static const int goes[3] = { 0, 1, 2 };
		for (int b = 0; b < 3; b++)
		{
			const float bw = 300, bx = 960 - 480 + (float)b * 330, by = top + 330;
			const bool focused = (!pointerOn() && g_homeButton == b) || hovering(bx, by, bw, 84);
			if (focused)
				gfx::roundRect(bx - 8, by - 8, bw + 16, 100, 26, (kBorder & 0xffffff00) | 0x48);
			gfx::panel(bx, by, bw, 84, 20, 2, focused ? kBorder : kLine, focused ? kSelected : kChip);
			centredTight(bx, bw, by + 26, 28, focused ? kAccent : kText, kTabName[goes[b]]);
			const int tab = goes[b];
			clickable(bx, by, bw, 84, [tab] { switchTab(tab); });
		}
		return;
	}

	// where each row sits, and how far down the page is scrolled to keep the chosen row in view
	std::vector<float> rowTop(g_home.size()), rowHeight(g_home.size());
	float y = 0;
	for (size_t i = 0; i < g_home.size(); i++)
	{
		rowTop[i] = y;
		rowHeight[i] = g_home[i].posters ? 436.0f : 256.0f;
		y += rowHeight[i];
	}
	const float room = 1080 - top - 20;
	float want = rowTop[(size_t)g_homeRow] + rowHeight[(size_t)g_homeRow] - room;
	if (want < 0) want = 0;
	if (want > rowTop[(size_t)g_homeRow]) want = rowTop[(size_t)g_homeRow];
	glide(g_homeScroll, want, 12.0f);

	gfx::clip(0, top, 1920, 1080 - top);
	for (size_t r = 0; r < g_home.size(); r++)
	{
		HomeRow& row = g_home[r];
		const float ry = top + 14 + rowTop[r] - g_homeScroll;
		if (ry > 1080 || ry + rowHeight[r] < top)
			continue;
		const bool rowFocus = (int)r == g_homeRow && !pointerOn();
		gfx::text(96, ry, 28, rowFocus ? kText : kMuted, row.title);
		const float cardW = row.posters ? 200.0f : 236.0f, cardH = row.posters ? 300.0f : 132.0f, cell = cardW + 26;
		const int visible = (int)((1920 - 96 - 40) / cell);
		// the row slides to keep its chosen card in view
		float target = row.scroll;
		if ((float)row.sel < target) target = (float)row.sel;
		if ((float)row.sel > target + (float)visible - 1) target = (float)row.sel - (float)visible + 1;
		target = (float)(int)(target + 0.5f);
		glide(row.scroll, target);
		for (int pass = 0; pass < 2; pass++)
			for (int i = (int)row.scroll - 1; i < (int)row.scroll + visible + 2 && i < (int)row.items.size(); i++)
			{
				if (i < 0)
					continue;
				const HomeItem& item = row.items[(size_t)i];
				const float cx = 96 + ((float)i - row.scroll) * cell, cy = ry + 48;
				const bool focused = rowFocus && i == row.sel;
				const bool hot = focused || hovering(cx, cy, cardW, cardH);
				if ((pass == 1) != hot)
					continue;
				if (row.posters)
					drawPosterCard(item.channel, cx, cy, cardW, cardH, hot, false, item.at, item.length);
				else
				{
					// a channel: its logo on a tile
					const float pop = hot ? g_cardPop : 1.0f, w = cardW * pop, h = cardH * pop, x = cx - (w - cardW) / 2, yy = cy - (h - cardH) / 2;
					if (hot)
						gfx::roundRect(x - 8, yy - 8, w + 16, h + 16, 22, (kBorder & 0xffffff00) | 0x55);
					gfx::panel(x, yy, w, h, 16, hot ? 3.0f : 1.0f, hot ? kBorder : kTileLine, kTile);
					const int logo = item.channel.logo.empty() ? -1 : logoImage(item.channel.logo);
					int lw = 0, lh = 0;
					if (logo >= 0 && gfx::imageSize(logo, lw, lh) && lw > 0 && lh > 0)
					{
						const float fit = ((w - 50) / (float)lw) < ((h - 36) / (float)lh) ? (w - 50) / (float)lw : (h - 36) / (float)lh;
						gfx::image(logo, x + (w - (float)lw * fit) / 2, yy + (h - (float)lh * fit) / 2, (float)lw * fit, (float)lh * fit);
					}
					else
						centredTight(x, w, yy + h / 2 - 26, 40, kMuted, initials(item.channel.name));
				}
				gfx::text(cx, cy + cardH + 12, 21, hot ? kAccent : kText, item.channel.name, cardW);
				if (row.posters && item.length > 0)
					gfx::text(cx, cy + cardH + 40, 18, kMuted, clockText(item.length - item.at) + " left", cardW);
				const int rowIndex = (int)r;
				clickable(cx, cy, cardW, cardH + 40, [rowIndex, i] {
					if (rowIndex < (int)g_home.size() && i < (int)g_home[(size_t)rowIndex].items.size())
					{
						g_homeRow = rowIndex;
						g_home[(size_t)rowIndex].sel = i;
						const HomeItem chosen = g_home[(size_t)rowIndex].items[(size_t)i];
						playHomeItem(chosen);
					}
				});
			}
	}
	gfx::unclip();
	if (g_popFor != g_homeRow * 1000 + g_home[(size_t)g_homeRow].sel)
	{
		g_popFor = g_homeRow * 1000 + g_home[(size_t)g_homeRow].sel;
		g_cardPop = 1;
	}
	glide(g_cardPop, 1.05f, 18.0f);
}

static void homeInput(Action action)
{
	if (g_home.empty())
	{
		switch (action)
		{
		case A_LEFT: g_homeButton = (g_homeButton + 2) % 3; break;
		case A_RIGHT: g_homeButton = (g_homeButton + 1) % 3; break;
		case A_OK: switchTab(g_homeButton); break;
		default: break;
		}
		return;
	}
	HomeRow& row = g_home[(size_t)g_homeRow];
	switch (action)
	{
	case A_UP: if (g_homeRow > 0) g_homeRow--; break;
	case A_DOWN: if (g_homeRow < (int)g_home.size() - 1) g_homeRow++; break;
	case A_LEFT: if (row.sel > 0) row.sel--; break;
	case A_RIGHT: if (row.sel < (int)row.items.size() - 1) row.sel++; break;
	case A_PAGE_UP: row.sel = row.sel > 5 ? row.sel - 5 : 0; break;
	case A_PAGE_DOWN: row.sel = row.sel + 5 < (int)row.items.size() ? row.sel + 5 : (int)row.items.size() - 1; break;
	case A_OK:
	{
		const HomeItem chosen = row.items[(size_t)row.sel];
		playHomeItem(chosen);
		break;
	}
	default: break;
	}
}

