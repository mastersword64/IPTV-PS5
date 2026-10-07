// ---- hiding single channels, and the "More" menu (R3) ---------------------------------------

static void hideChannel(int index)
{
	if (index < 0 || index >= (int)g_playlist.channels.size())
		return;
	const Channel channel = g_playlist.channels[(size_t)index];
	if (T->hiddenUrls.insert(channel.url).second)
		T->hiddenChannels.push_back(channel);
	saveHiddenChannels();
	const int place = g_sel;
	rebuildView(-1);
	g_sel = place < (int)g_view.size() ? place : (int)g_view.size() - 1;
	if (g_sel < 0)
		g_sel = 0;
	toast("Hidden: " + channel.name + "   (Settings > Groups brings it back)");
}

// Hides (or, with `hide` false, shows) a set of the current library's groups in one go.
static void hideGroups(const std::vector<int>& groups, bool hide)
{
	for (const int group : groups)
	{
		if (group < 0 || group >= (int)g_playlist.groups.size())
			continue;
		if (hide)
			T->hidden.insert(g_playlist.groups[(size_t)group]);
		else
			T->hidden.erase(g_playlist.groups[(size_t)group]);
	}
	saveHidden();
	const int keep = selectedChannel();
	closeDrill();
	computeCats();
	rebuildView(keep);
}

enum MoreAction { M_FAVOURITE, M_HIDE_CHANNEL, M_HIDE_GROUP, M_HIDE_COUNTRY, M_ONLY_COUNTRY, M_SORT };
struct MoreItem { std::string label; int action; int value; };
static std::vector<MoreItem> g_moreItems;
static int g_moreSel = 0, g_moreChannel = -1, g_moreGroup = -1;
static std::string g_moreTitle, g_moreCountry;

static void openMore()
{
	if (g_tab >= 3 || g_detail || T->state != 2)
		return;
	g_moreItems.clear();
	g_moreChannel = g_focus == F_CHANNELS ? selectedChannel() : -1;
	g_moreGroup = -1;
	if (g_focus == F_CATEGORIES && inGroups())
		g_moreGroup = g_catSel;
	else if (g_moreChannel >= 0)
	{
		const auto found = g_groupIndex.find(g_playlist.channels[(size_t)g_moreChannel].group);
		if (found != g_groupIndex.end())
			g_moreGroup = found->second - 1;
	}
	if (g_focus == F_RAIL)
		g_moreGroup = -1;

	const bool wall = T != &g_tabs[0];
	g_moreTitle = "More";
	if (g_moreChannel >= 0)
	{
		g_moreTitle = g_playlist.channels[(size_t)g_moreChannel].name;
		g_moreItems.push_back({ g_isFavourite[(size_t)g_moreChannel] ? "Remove from favorites" : "Add to favorites", M_FAVOURITE, 0 });
		g_moreItems.push_back({ wall ? "Hide this title" : "Hide this channel", M_HIDE_CHANNEL, 0 });
	}
	if (g_moreGroup >= 0 && g_moreGroup < (int)g_playlist.groups.size())
	{
		const std::string& name = g_playlist.groups[(size_t)g_moreGroup];
		if (g_moreChannel < 0)
			g_moreTitle = name;
		g_moreItems.push_back({ "Hide the group \"" + name + "\"", M_HIDE_GROUP, 0 });
		g_moreCountry = countryOf(name);
		if (g_moreCountry != "Other")
		{
			int count = 0;
			for (const std::string& group : g_playlist.groups)
				if (countryOf(group) == g_moreCountry)
					count++;
			if (count > 1)
				g_moreItems.push_back({ "Hide all " + std::to_string(count) + " \"" + g_moreCountry + "\" groups", M_HIDE_COUNTRY, 0 });
			g_moreItems.push_back({ "Show only \"" + g_moreCountry + "\" groups", M_ONLY_COUNTRY, 0 });
		}
	}
	if (wall)
	{
		static const char* const orders[4] = { "As listed", "A to Z", "Newest first", "Best rated first" };
		for (int i = 0; i < 4; i++)
			g_moreItems.push_back({ std::string(i == g_wallSort ? "\xE2\x9C\x93  " : "     ") + "Order: " + orders[i], M_SORT, i });
	}
	if (g_moreItems.empty())
		return;
	g_moreSel = 0;
	g_more = true;
}

static void doMore(const MoreItem& item)
{
	g_more = false;
	switch (item.action)
	{
	case M_FAVOURITE: toggleFavourite(g_moreChannel); break;
	case M_HIDE_CHANNEL: hideChannel(g_moreChannel); break;
	case M_HIDE_GROUP:
		hideGroups({ g_moreGroup }, true);
		toast("Hidden: " + g_playlist.groups[(size_t)g_moreGroup] + "   (Settings > Groups brings it back)");
		break;
	case M_HIDE_COUNTRY:
	case M_ONLY_COUNTRY:
	{
		// every group of that country, or every group that is not
		std::vector<int> groups;
		for (int i = 0; i < (int)g_playlist.groups.size(); i++)
			if ((countryOf(g_playlist.groups[(size_t)i]) == g_moreCountry) == (item.action == M_HIDE_COUNTRY))
				groups.push_back(i);
		hideGroups(groups, true);
		toast(item.action == M_HIDE_COUNTRY ? "Hidden: every \"" + g_moreCountry + "\" group" : "Showing only \"" + g_moreCountry + "\" groups   (Settings > Groups changes it)");
		break;
	}
	case M_SORT:
	{
		g_wallSort = item.value;
		saveSettings();
		const int keep = selectedChannel();
		rebuildView(keep);
		break;
	}
	default: break;
	}
}

static void drawMore()
{
	const float rowH = 60, w = 760, h = 96 + (float)g_moreItems.size() * rowH, x = (1920 - w) / 2, y = (1080 - h) / 2;
	gfx::rect(0, 0, 1920, 1080, 0x00000088);
	clickable(0, 0, 1920, 1080, [] { g_more = false; });
	gfx::panel(x, y, w, h, 20, 2, kLine, kDialog);
	clickable(x, y, w, h, [] {});
	gfx::text(x + 34, y + 26, 26, kMuted, g_moreTitle, w - 68);
	for (int i = 0; i < (int)g_moreItems.size(); i++)
	{
		const float ry = y + 76 + (float)i * rowH;
		const bool selected = (!pointerOn() && i == g_moreSel) || hovering(x + 14, ry, w - 28, rowH - 6);
		if (selected)
			gfx::panel(x + 14, ry, w - 28, rowH - 6, 12, 2, kBorder, kSelected);
		gfx::text(x + 36, ry + 13, 25, selected ? kAccent : kText, g_moreItems[(size_t)i].label, w - 72);
		clickable(x + 14, ry, w - 28, rowH - 6, [i] {
			if (i < (int)g_moreItems.size())
			{
				const MoreItem chosen = g_moreItems[(size_t)i];
				doMore(chosen);
			}
		});
	}
}

static void moreInput(Action action)
{
	switch (action)
	{
	case A_UP: moveSelection(g_moreSel, (int)g_moreItems.size(), -1); break;
	case A_DOWN: moveSelection(g_moreSel, (int)g_moreItems.size(), 1); break;
	case A_OK:
		if (g_moreSel < (int)g_moreItems.size())
		{
			const MoreItem chosen = g_moreItems[(size_t)g_moreSel];
			doMore(chosen);
		}
		break;
	case A_BACK:
	case A_MORE: g_more = false; break;
	default: break;
	}
}

// L3: back to the live channel watched before this one.
static void lastChannel()
{
	if (g_tab != 0 || g_lastLive < 0 || g_lastLive >= (int)g_playlist.channels.size() || g_lastLive == g_playing)
		return;
	const int channel = g_lastLive;
	startChannel(channel);
	for (int i = 0; i < (int)g_view.size(); i++)
		if (g_view[(size_t)i] == channel)
			g_sel = i;
}

// A library has just been fetched (T is that library). Things known before by their number in the
// old, partial list are found again by their address.
static void libraryArrived(bool onScreen)
{
	const int kind = (int)(T - g_tabs);
	if (g_nowOn && g_nowTab == kind)
	{
		const auto found = g_byUrl.find(g_now.url);
		g_nowIndex = found != g_byUrl.end() ? found->second : -1;
		if (onScreen)
			g_playing = g_nowIndex;
		g_queue.clear();
		g_queueAt = -1;
	}
	if (g_detail && onScreen)
	{
		const auto found = g_byUrl.find(g_detailUrl);
		if (found != g_byUrl.end())
			openDetails(found->second);
		else
			g_detail = false;
	}
}

// "Resume, or start again?" for a part-watched film or episode.
static void answerResume(int choice)
{
	const int channel = g_askChannel;
	const double at = g_askPosition;
	g_askChannel = -1;
	if (channel >= 0 && channel < (int)g_playlist.channels.size())
		beginWatching(channel, choice == 0 ? at : 0);
}

static void drawResumeQuestion()
{
	if (g_askChannel < 0 || g_askChannel >= (int)g_playlist.channels.size())
	{
		g_askChannel = -1;
		return;
	}
	const float w = 860, h = 300, x = (1920 - w) / 2, y = (1080 - h) / 2;
	gfx::rect(0, 0, 1920, 1080, 0x00000090);
	clickable(0, 0, 1920, 1080, [] { g_askChannel = -1; });
	gfx::panel(x, y, w, h, 20, 2, kLine, kDialog);
	clickable(x, y, w, h, [] {});
	gfx::text(x + 40, y + 34, 30, kText, g_playlist.channels[(size_t)g_askChannel].name, w - 80);
	gfx::text(x + 40, y + 84, 22, kMuted, "You have watched part of this.");
	const std::string labels[2] = { "Resume from " + clockText(g_askPosition), "Start from the beginning" };
	for (int b = 0; b < 2; b++)
	{
		const float bw = 380, bx = x + 40 + (float)b * (bw + 20), by = y + h - 130;
		const bool selected = (!pointerOn() && g_askSel == b) || hovering(bx, by, bw, 76);
		gfx::panel(bx, by, bw, 76, 16, 2, selected ? kBorder : kLine, selected ? kSelected : kChip);
		centredIn(bx, bw, by + 24, 25, selected ? kAccent : kText, labels[b]);
		clickable(bx, by, bw, 76, [b] { answerResume(b); });
	}
}

static void resumeInput(Action action)
{
	switch (action)
	{
	case A_LEFT:
	case A_RIGHT:
	case A_UP:
	case A_DOWN: g_askSel = 1 - g_askSel; break;
	case A_OK: answerResume(g_askSel); break;
	case A_BACK: g_askChannel = -1; break;
	default: break;
	}
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
		switchTabBy(action == A_TAB_PREV ? -1 : 1);
		return;
	}
	if (action == A_MENU)
	{
		openManager("");
		return;
	}
	if (g_grid && g_tab == 0)
	{
		gridInput(action);
		return;
	}
	if (g_tab == kSearchTab)
	{
		searchInput(action);
		return;
	}
	if (g_tab == kRadioTab)
	{
		radioInput(action);
		return;
	}
	if (g_layout == 1)
	{
		if (g_tab == 0)
			tvLiveInput(action);
		else if (g_tab == kHomeTab)
		{
			if (g_home.empty())
				tvPlainInput(action);
			else
				tvHomeInput(action);
		}
		else if (g_tab == kAccountTab)
			tvPlainInput(action);
		else if (g_detail)
			tvDetailsInput(action);
		else
			tvWallInput(action);
		return;
	}
	if (g_tab == kHomeTab)
	{
		homeInput(action);
		return;
	}
	if (g_tab == kAccountTab)
	{
		if (action == A_BACK)
			switchTab(kHomeTab);
		return;
	}
	if (g_detail)
	{
		detailsInput(action);
		return;
	}
	if (action == A_SEARCH)
	{
		openFilter();
		return;
	}
	if (action == A_MORE)
	{
		openMore();
		return;
	}
	if (action == A_LAST)
	{
		lastChannel();
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
	else if (g_tab == 1 || g_tab == 2)
	{
		// the poster wall: a grid
		const int count = (int)g_view.size(), columns = g_gridColumns > 0 ? g_gridColumns : 1;
		switch (action)
		{
		case A_LEFT:
			if (count == 0 || g_sel % columns == 0)
				g_focus = groups ? F_CATEGORIES : F_RAIL;
			else
				g_sel--;
			break;
		case A_RIGHT: if (g_sel + 1 < count) g_sel++; break;
		case A_UP: if (g_sel - columns >= 0) g_sel -= columns; break;
		case A_DOWN:
			if (g_sel + columns < count)
				g_sel += columns;
			else if (count > 0 && g_sel / columns < (count - 1) / columns)
				g_sel = count - 1;                          // the last row is shorter
			break;
		case A_PAGE_UP: g_sel = g_sel - columns * 2 >= 0 ? g_sel - columns * 2 : g_sel % columns; break;
		case A_PAGE_DOWN: if (count > 0) g_sel = g_sel + columns * 2 < count ? g_sel + columns * 2 : count - 1; break;
		case A_BACK: g_focus = groups ? F_CATEGORIES : F_RAIL; break;
		case A_OK: activateSelected(); break;
		case A_FAVOURITE: toggleFavourite(selectedChannel()); break;
		default: break;
		}
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
	if (g_tab < 3)
	{
		pumpGenres();
		pumpDrill();
	}
	if (g_sleepAt > 0 && nowSeconds() >= g_sleepAt)
	{
		g_sleepAt = 0;
		g_sleepMinutes = 0;
		if (g_nowOn)
		{
			stopPlayback();
			toast("Sleep timer: playback stopped");
		}
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

	if (g_grid && g_tab == 0)
	{
		drawGrid();                                 // the guide grid, the same in both layouts
		tvToast(960.0f);
		return;
	}
	if (g_tab == kSearchTab)
	{
		drawSearch();                               // the same in both layouts
		tvToast(1006.0f);
		return;
	}
	if (g_tab == kRadioTab)
	{
		drawRadio(status);                          // the same in both layouts
		tvToast(1006.0f);
		return;
	}
	if (g_layout == 1)
	{
		// the TV layout: no bar, no tabs; each screen fills the display, with the rail at its left
		if (g_tab == 0)
			drawTvLive(status);
		else if (g_tab == kHomeTab)
		{
			if (g_home.empty())
				drawTvWelcome();
			else
				drawTvHome();
		}
		else if (g_tab == kAccountTab)
			drawTvAccount();
		else
		{
			drawTvWall();
			if (g_detail)
				drawTvDetails();
		}
		tvToast(g_tab == 0 ? 1300.0f : 1006.0f);
		return;
	}
	drawTopBar();
	drawTabs();
	if (g_tab == kHomeTab)
		drawHome();
	else if (g_tab == kAccountTab)
		drawAccount();
	else if (g_tab == 1 || g_tab == 2)
	{
		// films and series: categories, then a wall of posters; a chosen title opens its own page
		const bool groups = g_section == S_GROUPS && !g_playlist.groups.empty();
		float x = 90;
		if (groups)
		{
			drawCategories(x, 400);
			x += 400;
		}
		drawGrid(x, 1920 - x);
		drawRail();
		if (g_detail)
			drawDetails();
	}
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

