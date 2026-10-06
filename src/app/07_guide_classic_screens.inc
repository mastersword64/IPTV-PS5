static const char* const kNoun[3] = { "channels", "movies", "series" };

struct GuideEntry { double fetched = 0; bool failed = false; std::vector<Programme> items; };
static bool g_gridGuideReset = false;                   // a new playlist: the grid's own store is emptied too
static std::unordered_map<std::string, GuideEntry> g_guide;     // by channel address
static std::mutex g_guideMutex;
static int g_guideRequest = 0;
static bool g_guideBusy = false, g_guideArrived = false, g_guideOk = false;
static std::string g_guideFor, g_guideWanted;
static double g_guideRestSince = 0;
static std::vector<Programme> g_guideItems;

// A new playlist: the episode queue, any open question and the guide belong to the old one.
static void forgetPlaybackState()
{
	g_queue.clear();
	g_queueAt = -1;
	g_askChannel = -1;
	g_lastLive = -1;
	g_guide.clear();
	g_gridGuideReset = true;
	std::lock_guard<std::mutex> lock(g_guideMutex);
	g_guideRequest++;
	g_guideBusy = false;
	g_guideArrived = false;
}

static XmltvGuide g_xmltv;                              // an M3U playlist's own guide, when it names one
static std::mutex g_xmltvMutex;
static std::atomic<int> g_xmltvState{ 0 };              // 0 none, 1 fetching, 2 ready, 3 failed
static int g_xmltvRequest = 0;

static void startXmltv(std::string address)
{
	// the playlist may list several, separated by commas: the first is used
	const size_t comma = address.find(',');
	if (comma != std::string::npos)
		address = address.substr(0, comma);
	address = trim(address);
	int request;
	{
		std::lock_guard<std::mutex> lock(g_xmltvMutex);
		request = ++g_xmltvRequest;
		g_xmltv = XmltvGuide();
	}
	if (address.compare(0, 4, "http") != 0)
	{
		g_xmltvState = 0;
		return;
	}
	g_xmltvState = 1;
	logLine("guide: fetching %s", address.c_str());
	startThread("xmltv", [address, request] {
		std::string body, xml;
		bool ok = httpGetQuiet(address, body, 120u * 1024 * 1024, 180, 3);
		if (ok && body.size() > 2 && (unsigned char)body[0] == 0x1f && (unsigned char)body[1] == 0x8b)
		{
			ok = gunzip(body, xml, 600u * 1024 * 1024);
			body.clear();
		}
		else
			xml.swap(body);
		XmltvGuide guide;
		const long long now = (long long)time(nullptr);
		if (ok)
			ok = parseXmltv(xml, now - 12 * 3600, now + 36 * 3600, guide);
		std::lock_guard<std::mutex> lock(g_xmltvMutex);
		if (request != g_xmltvRequest)
			return;
		logLine(ok ? "guide: %d channels have programmes (%d MB read)" : "guide: could not be read (%d channels, %d MB)", (int)guide.programmes.size(), (int)(xml.size() >> 20));
		g_xmltv = std::move(guide);
		g_xmltvState = ok ? 2 : 3;
	});
}

static std::string clockAt(long long when);

// Looks up (and when needed, asks the server for) what is on a live channel.
static const GuideEntry* guideFor(const Channel& channel)
{
	const double now = nowSeconds();
	if (g_xmltvState != 0)
	{
		// an M3U playlist with a guide of its own: answered from what was fetched
		if (g_xmltvState == 1)
			return nullptr;
		GuideEntry& entry = g_guide[channel.url];
		if (entry.fetched == 0 || now - entry.fetched > 1800)
		{
			entry.fetched = now;
			entry.items.clear();
			std::lock_guard<std::mutex> lock(g_xmltvMutex);
			std::string id = lower(channel.tvgId);
			if (id.empty() || !g_xmltv.programmes.count(id))
			{
				const auto named = g_xmltv.idByName.find(lower(channel.name));
				if (named != g_xmltv.idByName.end())
					id = named->second;
			}
			const auto found = g_xmltv.programmes.find(id);
			if (found != g_xmltv.programmes.end())
				entry.items = found->second;
			for (Programme& programme : entry.items)
			{
				programme.from = clockAt(programme.start);      // on the viewer's clock
				programme.to = clockAt(programme.stop);
			}
			entry.failed = entry.items.empty();
		}
		return &entry;
	}
	{
		// file what the background request brought back
		std::lock_guard<std::mutex> lock(g_guideMutex);
		if (g_guideArrived)
		{
			GuideEntry& entry = g_guide[g_guideFor];
			entry.fetched = now;
			entry.failed = !g_guideOk;
			entry.items = std::move(g_guideItems);
			g_guideItems.clear();
			g_guideArrived = false;
			g_guideBusy = false;
		}
	}
	const auto found = g_guide.find(channel.url);
	const bool fresh = found != g_guide.end() && now - found->second.fetched < 600;
	if (!fresh && !g_guideBusy)
	{
		// ask once the choice has rested on this channel for a moment
		if (g_guideWanted != channel.url)
		{
			g_guideWanted = channel.url;
			g_guideRestSince = now;
		}
		else if (now - g_guideRestSince > 0.6)
		{
			g_guideBusy = true;
			g_guideFor = channel.url;
			const int request = ++g_guideRequest;
			const Source source = g_sources[(size_t)g_active];
			const Channel asked = channel;
			startThread("guide", [source, asked, request] {
				std::vector<Programme> items;
				std::string error;
				const bool ok = fetchGuide(source, asked, items, error);
				std::lock_guard<std::mutex> lock(g_guideMutex);
				if (request != g_guideRequest)
					return;
				g_guideItems = std::move(items);
				g_guideOk = ok;
				g_guideArrived = true;
			});
		}
	}
	return found != g_guide.end() ? &found->second : nullptr;
}

// The programme on now, from what has already been fetched (nothing is asked for here).
static const Programme* onNow(const std::string& url)
{
	const auto found = g_guide.find(url);
	if (found == g_guide.end() || found->second.items.empty())
		return nullptr;
	const long long now = (long long)time(nullptr);
	for (const Programme& programme : found->second.items)
		if (programme.start > 0 && programme.start <= now && now < programme.stop)
			return &programme;
	return found->second.items[0].start == 0 ? &found->second.items[0] : nullptr;
}

static std::string clockOfDay(const Programme& programme, bool end)
{
	const std::string& given = end ? programme.to : programme.from;
	if (!given.empty())
		return given;
	const time_t when = (time_t)(end ? programme.stop : programme.start);
	struct tm parts;
	if (when <= 0 || !localtime_r(&when, &parts))
		return "";
	char text[16];
	snprintf(text, sizeof(text), "%02d:%02d", parts.tm_hour, parts.tm_min);
	return text;
}

static std::string sectionName(int section)
{
	static const char* const all[3] = { "All channels", "All movies", "All series" };
	if (section == S_RECENT && T != &g_tabs[0])
		return "Continue watching";
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
	if (g_tab >= 3 || g_detail)
		return;                                    // nothing to filter on Home, Account or a title's own page
	const bool forCategories = inGroups() && g_focus == F_CATEGORIES && !T->drill;
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
	const bool plain = g_tab >= 3 || g_detail;      // screens with nothing to filter
	if (g_tab == kHomeTab)
		label = "Home";
	else if (g_tab == kAccountTab)
		label = "Account";
	else if (g_detail && g_detailItem >= 0 && g_detailItem < (int)g_playlist.channels.size())
		label = g_playlist.channels[(size_t)g_detailItem].name;
	else if (T->filterCats)
		label = "Categories";
	else if (T->drill)
		label = T->drillTitle;
	else if (inGroups())
		label = "Groups / " + g_playlist.groups[(size_t)g_catSel];
	const float w = gfx::text(520, 30, 24, kText, label, plain ? 900.0f : 420.0f);
	if (!plain)
	{
		if (g_filter.empty())
			gfx::text(520 + w + 26, 30, 24, kMuted, T->filterCats ? "Filter the categories..." : "Filter this section...");
		else
			gfx::text(520 + w + 26, 30, 24, kAccent, g_filter, 460);
		gfx::text(1444, 30, 24, kMuted, "\xE2\x96\xB3");                // triangle button
	}

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

static std::string clockNow();
static void openGrid();

// Home, Live TV, Movies, Series, Account
static void drawTabs()
{
	for (int place = 0; place < kTabCount; place++)
	{
		const int tab = kTabOrder[place];
		const float x = 96 + (float)place * 202, w = 190;
		const bool current = tab == g_tab, hover = hovering(x, 86, w, 48);
		gfx::panel(x, 86, w, 48, 24, current || hover ? 2.0f : 1.0f, current || hover ? kBorder : kLine, current ? kSelected : kChip);
		centredTight(x, w, 97, 23, current ? kAccent : kText, kTabName[tab]);
		clickable(x, 86, w, 48, [tab] { switchTab(tab); });
	}
	gfx::text(96 + (float)kTabCount * 202 + 14, 100, 20, kMuted, "L2 / R2  Switch");
	if (g_tab == 0 && T->state == 2)
	{
		// the programme guide grid for the list on show
		const float gx = 1560, gw = 170;
		const bool hover = hovering(gx, 86, gw, 48);
		gfx::panel(gx, 86, gw, 48, 24, hover ? 2.0f : 1.0f, hover ? kBorder : kLine, kChip);
		centredTight(gx, gw, 97, 23, hover ? kAccent : kText, "\xE2\x96\xA4  Guide");
		clickable(gx, 86, gw, 48, [] { openGrid(); });
	}
	const std::string clock = clockNow();
	gfx::text(1892 - gfx::textWidth(28, clock), 94, 28, kText, clock);
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
	if (T->catScroll - (float)g_catTop > 30 || (float)g_catTop - T->catScroll > 30)
		T->catScroll = (float)g_catTop;
	glide(T->catScroll, (float)g_catTop, 20.0f);
	gfx::clip(x, kListTop - 2, w, (float)kCategoryRows * kCatRowH + 2);
	for (int at = (int)T->catScroll; at < (int)T->catScroll + kCategoryRows + 1; at++)
	{
		if (at >= count)
			break;
		const int i = T->cats[(size_t)at];
		const float y = kListTop + ((float)at - T->catScroll) * kCatRowH;
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
	gfx::unclip();
	drawScrollbar(x + w - 8, kListTop, (float)kCategoryRows * kCatRowH, g_catTop, count, kCategoryRows);
}

static void showControlsSoon();
static void lastChannel();
static void leaveFullscreen();
static void openDetails(int index);
static void drawSubtitles(float x, float w, float bottom, float size);
static void drawSubtitlePicture(float areaX, float areaY, float areaW, float areaH);

// Plays the selected entry (a second time on the same one fills the screen); a series opens its episodes.
static void activateSelected()
{
	const int channel = selectedChannel();
	if (channel < 0)
		return;
	if (g_tab == 1 || g_tab == 2)
	{
		openDetails(channel);                       // films and series have a page of their own
		return;
	}
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
		return;
	}
	const auto left = T->resume.find(g_playlist.channels[(size_t)channel].url);
	if (g_tab > 0 && left != T->resume.end() && left->second.first > 45)
	{
		// part-watched: ask whether to carry on or start again
		g_askChannel = channel;
		g_askPosition = left->second.first;
		g_askSel = 0;
		return;
	}
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
	if (T->listScroll - (float)g_top > 24 || (float)g_top - T->listScroll > 24)
		T->listScroll = (float)g_top;                  // a long jump is not animated
	glide(T->listScroll, (float)g_top, 20.0f);
	gfx::clip(x, kListTop - 2, w, (float)kChannelRows * kRowH + 2);
	for (int i = (int)T->listScroll; i < (int)T->listScroll + kChannelRows + 1; i++)
	{
		if (i >= (int)g_view.size())
			break;
		const int channel = g_view[i];
		const Channel& info = g_playlist.channels[(size_t)channel];
		const float y = kListTop + ((float)i - T->listScroll) * kRowH;
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
		{
			// the channel's logo or the film's poster once it has arrived; its initials until then
			const int logo = info.logo.empty() ? -1 : logoImage(info.logo);
			int lw = 0, lh = 0;
			if (logo >= 0 && gfx::imageSize(logo, lw, lh) && lw > 0 && lh > 0)
			{
				const float fit = 48.0f / (float)(lw > lh ? lw : lh);
				gfx::image(logo, x + 47 - (float)lw * fit / 2, y + 36 - (float)lh * fit / 2, (float)lw * fit, (float)lh * fit);
			}
			else
				centredTight(x + 20, 54, y + 23, 22, playing ? kAccent : kText, initials(info.name));
		}

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
			const auto left = media > 0 ? T->resume.find(info.url) : T->resume.end();
			const Programme* now = media == 0 ? onNow(info.url) : nullptr;
			if (left != T->resume.end() && left->second.second > 0)
			{
				// part-watched: a bar and the time reached
				const float done = (float)(left->second.first / left->second.second);
				gfx::roundRect(x + 90, y + 52, 150, 6, 3, kLine);
				gfx::roundRect(x + 90, y + 52, 150 * (done > 1 ? 1 : done), 6, 3, kBorder);
				gfx::text(x + 254, y + 43, 20, kMuted, clockText(left->second.first) + " of " + clockText(left->second.second), w - 370);
			}
			else
			{
				const std::string under = series ? "Series  \xC2\xB7  \xE2\x9C\x95 to see the episodes" : now ? now->title : info.group;
				gfx::text(x + 90, y + 43, 20, kMuted, under, w - 200);
			}
		}

		gfx::text(x + w - 62, y + 18, 32, g_isFavourite[(size_t)channel] ? kAccent : kMuted, g_isFavourite[(size_t)channel] ? "\xE2\x98\x85" : "\xE2\x98\x86");
	}
	gfx::unclip();
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
		drawSubtitlePicture(x, kColumnTop, w, videoH);
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
	if (!(g_nowOn && g_nowVod) && g_tab == 0 && (g_nowOn || selected >= 0))
	{
		// what is on this channel now, and what follows
		const Channel& shown = g_nowOn && g_nowTab == 0 ? g_now : g_playlist.channels[(size_t)(selected >= 0 ? selected : 0)];
		const GuideEntry* guide = selected >= 0 || g_nowOn ? guideFor(shown) : nullptr;
		const long long clock = (long long)time(nullptr);
		int drawn = 0;
		if (guide)
			for (const Programme& programme : guide->items)
			{
				if (programme.stop > 0 && programme.stop <= clock)
					continue;                              // already over
				const bool current = programme.start > 0 && programme.start <= clock && clock < programme.stop;
				const float h = current ? 104.0f : 62.0f;
				if (y + h > 1036)
					break;
				gfx::panel(x + 12, y, w - 24, h, 14, current ? 2.0f : 1.0f, current ? kBorder : kLine, current ? kSelected : kCard);
				const std::string times = clockOfDay(programme, false) + " \xE2\x80\x93 " + clockOfDay(programme, true);
				const float tw = gfx::text(x + 34, y + 18, 21, kMuted, times);
				if (current)
				{
					gfx::panel(x + 50 + tw, y + 15, 60, 26, 13, 1, kLive, kLiveBack);
					centredTight(x + 50 + tw, 60, y + 19, 17, kLive, "NOW");
					gfx::text(x + 34, y + 48, 25, kAccent, programme.title, w - 70);
					const float done = (float)(clock - programme.start) / (float)(programme.stop - programme.start);
					gfx::roundRect(x + 34, y + 86, w - 68, 6, 3, kLine);
					gfx::roundRect(x + 34, y + 86, (w - 68) * (done < 0 ? 0 : done > 1 ? 1 : done), 6, 3, kBorder);
				}
				else
					gfx::text(x + 52 + tw, y + 17, 23, kText, programme.title, w - 90 - tw);
				y += h + 10;
				drawn++;
			}
		if (drawn == 0 && y + 62 < 1040)
		{
			gfx::panel(x + 12, y, w - 24, 62, 14, 1, kLine, kCard);
			gfx::text(x + 34, y + 18, 22, kMuted, !guide || (guide->items.empty() && !guide->failed) ? "Looking up the programme..." : "No program information available", w - 70);
		}
	}

	gfx::text(x + 16, 1046, 20, kMuted,
		"\xE2\x9C\x95 Play   \xE2\x96\xA1 Favorite   \xE2\x96\xB3 Filter   R3 More (hide, country)   L3 Last channel   L2 / R2 Tabs   OPTIONS Settings", w - 32);
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

