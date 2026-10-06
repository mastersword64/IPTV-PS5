// ---- Search: channels, films and series by name, in one place -------------------------------
// What the app already holds is searched at once; a portal is also asked, and its matches join in.
// The results are shown as rows, drawn and steered exactly as the TV-style Home is.

static std::string g_searchDone;                        // the words last searched for
static std::vector<HomeRow> g_searchRows;
static int g_searchRow = 0, g_searchState = 0;          // state: 1 asking the server, 2 finished
static std::mutex g_searchMutex;
static int g_searchRequest = 0;
static bool g_searchArrived = false;
static std::vector<Channel> g_searchFound[3];

static void searchAdd(int tab, const Channel& channel)
{
	static const char* const titles[3] = { "Live TV", "Movies", "Series" };
	HomeRow* row = nullptr;
	for (HomeRow& have : g_searchRows)
		if (have.title == titles[tab])
			row = &have;
	if (!row)
	{
		HomeRow fresh;
		fresh.title = titles[tab];
		fresh.posters = tab != 0;
		// kept in the order live, movies, series
		size_t at = 0;
		while (at < g_searchRows.size() && (g_searchRows[at].title == titles[0] || (tab == 2 && g_searchRows[at].title == titles[1])))
			at++;
		g_searchRows.insert(g_searchRows.begin() + (long)at, fresh);
		row = &g_searchRows[at];
	}
	if (row->items.size() >= 60)
		return;
	for (const HomeItem& have : row->items)
		if (have.channel.url == channel.url)
			return;
	HomeItem item;
	item.channel = channel;
	item.tab = tab;
	const auto left = g_tabs[tab].resume.find(channel.url);
	if (left != g_tabs[tab].resume.end())
	{
		item.at = left->second.first;
		item.length = left->second.second;
	}
	row->items.push_back(item);
}

static void startSearch()
{
	const std::string words = trim(g_searchText);
	g_searchDone = words;
	g_searchRows.clear();
	g_searchRow = 0;
	int request;
	{
		std::lock_guard<std::mutex> lock(g_searchMutex);
		request = ++g_searchRequest;
		g_searchArrived = false;
	}
	g_searchState = 0;
	if (words.size() < 2 || g_active < 0)
		return;
	// what is already here: every channel and title the app has seen so far
	const std::string needle = lower(words);
	for (int tab = 0; tab < 3; tab++)
		for (const Channel& channel : g_tabs[tab].playlist.channels)
			if (!g_tabs[tab].hiddenUrls.count(channel.url) && lower(channel.name).find(needle) != std::string::npos)
				searchAdd(tab, channel);
	const Source source = g_sources[(size_t)g_active];
	if (!sourceCanSearch(source))
	{
		g_searchState = 2;
		return;
	}
	g_searchState = 1;
	logLine("search: asking the server for \"%s\"", words.c_str());
	startThread("search", [source, words, request] {
		std::vector<Channel> found[3];
		for (int media = 0; media < 3; media++)
		{
			std::string error;
			if (!searchSource(source, media, words, found[media], error))
				logLine("search: %s: %s", kTabName[media], error.c_str());
			// a portal that ignores the search words returns its whole list: keep only real matches
			const std::string needle = lower(words);
			std::vector<Channel> matching;
			for (Channel& channel : found[media])
				if (lower(channel.name).find(needle) != std::string::npos)
					matching.push_back(std::move(channel));
			found[media].swap(matching);
			std::lock_guard<std::mutex> lock(g_searchMutex);
			if (request != g_searchRequest)
				return;
		}
		std::lock_guard<std::mutex> lock(g_searchMutex);
		if (request != g_searchRequest)
			return;
		for (int media = 0; media < 3; media++)
			g_searchFound[media] = std::move(found[media]);
		g_searchArrived = true;
	});
}

static void openSearchKeyboard()
{
	g_searchTyping = true;
	openKeyboard(&g_searchText, 40, false, "Search");
}

static void drawSearch()
{
	// typed something new?
	if (g_searchTyping && !g_keyboard)
	{
		g_searchTyping = false;
		if (trim(g_searchText) != g_searchDone)
			startSearch();
	}
	{
		std::lock_guard<std::mutex> lock(g_searchMutex);
		if (g_searchArrived)
		{
			g_searchArrived = false;
			for (int media = 0; media < 3; media++)
			{
				for (const Channel& channel : g_searchFound[media])
					searchAdd(media, channel);
				g_searchFound[media].clear();
			}
			g_searchState = 2;
			int total = 0;
			for (const HomeRow& row : g_searchRows)
				total += (int)row.items.size();
			logLine("search: %d results", total);
		}
	}
	const float keepScale = gfx::radiusScale();
	if (!g_searchRows.empty())
	{
		// the results, as rows: drawn by the same hand as Home
		g_home.swap(g_searchRows);
		std::swap(g_homeRow, g_searchRow);
		if (g_homeRow > (int)g_home.size() - 1)
			g_homeRow = (int)g_home.size() - 1;
		for (HomeRow& row : g_home)
			if (row.sel > (int)row.items.size() - 1)
				row.sel = (int)row.items.size() - 1;
		drawTvHome();
		g_home.swap(g_searchRows);
		std::swap(g_homeRow, g_searchRow);
		gfx::setRadiusScale(1.0f);
		const std::string label = "Search:  " + g_searchDone + (g_searchState == 1 ? "   \xC2\xB7   still asking the server..." : "");
		const float w = gfx::textWidth(21, label) + 44;
		const bool hot = hovering(150, 44, w, 46);
		gfx::roundRect(150, 44, w, 46, 23, hot ? kTvWhite : 0xffffff2a);
		gfx::text(172, 55, 21, hot ? kTvInk : kTvWhite, label);
		gfx::text(150 + w + 18, 57, 18, kTvDim, "\xE2\x96\xB3  New search");
		clickable(150, 44, w, 46, [] { openSearchKeyboard(); });
		gfx::setRadiusScale(keepScale);
		return;
	}
	gfx::setRadiusScale(1.0f);
	gfx::rect(0, 0, 1920, 1080, kTvBack);
	gfx::roundRect(1180, -260, 1100, 1100, 550, 0xffffff08);
	gfx::bold(true);
	centredIn(92, 1828, 300, 56, kTvWhite, "Search");
	gfx::bold(false);
	const float fw = 900, fx = 92 + (1828 - fw) / 2;
	const bool hot = (!pointerOn() && !g_tvOnRail) || hovering(fx, 400, fw, 80);
	if (hot)
		gfx::roundRect(fx - 8, 392, fw + 16, 96, 48, 0xffffff30);
	gfx::roundRect(fx, 400, fw, 80, 40, hot ? kTvWhite : 0xffffff2a);
	centredIn(fx, fw, 424, 28, g_searchText.empty() ? (hot ? 0x0e121e90 : kTvDim) : (hot ? kTvInk : kTvWhite), g_searchText.empty() ? "Channels, films and series" : g_searchText);
	clickable(fx, 400, fw, 80, [] { openSearchKeyboard(); });
	if (g_searchState == 1)
	{
		spinner(1006 - 110, 546, 14, kTvWhite);
		centredIn(92, 1828, 532, 23, kTvSoft, "Searching...");
	}
	else if (!g_searchDone.empty() && g_searchDone.size() < 2)
		centredIn(92, 1828, 532, 23, kTvDim, "Type at least two letters");
	else if (g_searchState == 2)
		centredIn(92, 1828, 532, 23, kTvSoft, "Nothing found for \"" + g_searchDone + "\"");
	else
		centredIn(92, 1828, 532, 23, kTvDim, "\xE2\x9C\x95  to type");
	if (g_active >= 0 && !sourceCanSearch(g_sources[(size_t)g_active]))
		centredIn(92, 1828, 600, 19, kTvDim, "This kind of playlist is searched from what has been opened so far; a portal is asked for everything.");
	drawTvRail();
	gfx::setRadiusScale(keepScale);
}

static void searchInput(Action action)
{
	if (action == A_SEARCH)
	{
		openSearchKeyboard();
		return;
	}
	if (!g_searchRows.empty())
	{
		g_home.swap(g_searchRows);
		std::swap(g_homeRow, g_searchRow);
		if (g_homeRow > (int)g_home.size() - 1)
			g_homeRow = (int)g_home.size() - 1;
		tvHomeInput(action);
		g_home.swap(g_searchRows);
		std::swap(g_homeRow, g_searchRow);
		return;
	}
	if (tvRailInput(action))
		return;
	if (action == A_OK)
		openSearchKeyboard();
	else if (action == A_LEFT || action == A_BACK)
		tvOpenRail();
}

