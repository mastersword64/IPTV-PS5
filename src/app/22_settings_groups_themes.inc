// ---- Settings > Groups: by country ---------------------------------------------------------
// With hundreds of groups, one long list is unusable. Groups are gathered by the country (or
// region) their names start with: the left list is the countries, the middle list the groups of
// the country chosen. A whole country can be shown or hidden in one press.

struct CountryGroups { std::string name; std::vector<int> groups; };
static std::vector<CountryGroups> g_countries;
static const Tab* g_countriesOf = nullptr;
static size_t g_countriesSize = 0;
static int g_hideColumn = 0;                    // 0 countries, 1 groups (or hidden channels), 2 buttons
static int g_hideCountry = 0, g_hideCountryTop = 0;
static bool g_hideChannelList = false;          // the middle list shows hidden channels instead of groups
static std::string g_pinTyped;
static int g_pinAsking = 0;                     // 1: typing the PIN to get in, 2: typing a new PIN
static std::string g_pinNote;

static void buildCountries(const Tab& tab)
{
	if (g_hideCountriesReset)
	{
		g_countriesOf = nullptr;                       // Settings was reopened: the playlist may have changed
		g_hideCountriesReset = false;
	}
	if (g_countriesOf == &tab && g_countriesSize == tab.playlist.groups.size())
		return;
	g_countriesOf = &tab;
	g_countriesSize = tab.playlist.groups.size();
	g_countries.clear();
	std::unordered_map<std::string, size_t> place;
	for (int i = 0; i < (int)tab.playlist.groups.size(); i++)
	{
		const std::string country = countryOf(tab.playlist.groups[(size_t)i]);
		const auto found = place.find(country);
		if (found == place.end())
		{
			place[country] = g_countries.size();
			g_countries.push_back({ country, { i } });
		}
		else
			g_countries[found->second].groups.push_back(i);
	}
	// alphabetical, with the unlabelled ones last
	std::sort(g_countries.begin(), g_countries.end(), [](const CountryGroups& a, const CountryGroups& b) {
		if ((a.name == "Other") != (b.name == "Other"))
			return b.name == "Other";
		return a.name < b.name;
	});
}

static int shownIn(const Tab& tab, const CountryGroups& country)
{
	int shown = 0;
	for (const int group : country.groups)
		if (!tab.hidden.count(tab.playlist.groups[(size_t)group]))
			shown++;
	return shown;
}

// Work on another library's lists for a moment (Settings can be looking at any of the three).
static void hideGroupsIn(int tabIndex, const std::vector<int>& groups, bool hide)
{
	Tab* const showing = T;
	T = &g_tabs[tabIndex];
	hideGroups(groups, hide);
	T = showing;
}

static void unhideChannel(int tabIndex, int place)
{
	Tab* const showing = T;
	T = &g_tabs[tabIndex];
	if (place >= 0 && place < (int)T->hiddenChannels.size())
	{
		T->hiddenUrls.erase(T->hiddenChannels[(size_t)place].url);
		T->hiddenChannels.erase(T->hiddenChannels.begin() + place);
		saveHiddenChannels();
		rebuildView(selectedChannel());
	}
	T = showing;
}

static int groupsMiddleCount(const Tab& tab)
{
	if (g_hideChannelList)
		return (int)tab.hiddenChannels.size();
	return g_hideCountry < (int)g_countries.size() ? (int)g_countries[(size_t)g_hideCountry].groups.size() : 0;
}

static void pressGroupsButton(int button)
{
	const Tab& tab = g_tabs[g_hideTab];
	if (button == 0 || button == 1)
		setHidden(g_hideTab, -1, button == 1);
	else if (button == 2)
	{
		g_hideChannelList = !g_hideChannelList;
		g_hideSel = g_hideTop = 0;
		if (g_hideChannelList && !tab.hiddenChannels.empty())
			g_hideColumn = 1;
	}
	else if (button == 3)
	{
		if (g_pin.empty())
		{
			g_pinTyped.clear();
			g_pinAsking = 2;
			openKeyboard(&g_pinTyped, 4, false, "New PIN (4 digits)");
		}
		else
		{
			g_pin.clear();
			saveSettings();
			g_pinNote = "The PIN was removed.";
		}
	}
}

static void drawGroupsTab()
{
	const float top = 208, rowH = 58;
	const Tab& tab = g_tabs[g_hideTab];
	const bool ready = g_haveBrowse && tab.state == 2 && !tab.playlist.groups.empty();

	// what was typed on the keyboard, once it closes
	if (g_pinAsking != 0 && !g_keyboard)
	{
		bool digits = g_pinTyped.size() == 4;
		for (const char c : g_pinTyped)
			if (c < '0' || c > '9')
				digits = false;
		if (g_pinAsking == 1)
		{
			g_pinOpen = g_pinTyped == g_pin;
			g_pinNote = g_pinOpen ? "" : (g_pinTyped.empty() ? "" : "That PIN is not right.");
		}
		else if (digits)
		{
			g_pin = g_pinTyped;
			g_pinOpen = true;
			saveSettings();
			g_pinNote = "PIN set. It will be asked for here from now on.";
		}
		else if (!g_pinTyped.empty())
			g_pinNote = "A PIN is four digits. Nothing was changed.";
		g_pinAsking = 0;
		g_pinTyped.clear();
	}

	if (!g_pin.empty() && !g_pinOpen)
	{
		// locked: nothing here can be seen or changed without the PIN
		gfx::roundRect(96, top, 1744, 1080 - top - 64, 16, kColumn);
		centredIn(96, 1744, top + 200, 40, kText, "Locked");
		centredIn(96, 1744, top + 262, 24, kMuted, "Hidden groups and channels are protected by a PIN.");
		const float bw = 360, bx = 960 - bw / 2, by = top + 340;
		const bool hot = !pointerOn() || hovering(bx, by, bw, 78);
		gfx::panel(bx, by, bw, 78, 18, 2, hot ? kBorder : kLine, hot ? kSelected : kChip);
		centredTight(bx, bw, by + 24, 27, hot ? kAccent : kText, "Enter the PIN");
		clickable(bx, by, bw, 78, [] { g_pinTyped.clear(); g_pinAsking = 1; openKeyboard(&g_pinTyped, 4, false, "PIN"); });
		if (!g_pinNote.empty())
			centredIn(96, 1744, by + 110, 22, kLive, g_pinNote);
		gfx::text(1250, 1040, 20, kMuted, "\xE2\x9C\x95 Enter the PIN    L1 / R1 Tab    \xE2\x97\x8B / OPTIONS Back", 590);
		return;
	}

	// ---- left: countries ----
	const float cx = 96, cw = 400;
	gfx::roundRect(cx, top, cw, 1080 - top - 64, 16, kColumn);
	gfx::text(cx + 24, top + 16, 24, kText, std::string(kTabName[g_hideTab]) + " by country");
	if (!ready)
		centredIn(cx, cw, top + 220, 22, kMuted, !g_haveBrowse ? "Open a playlist first" : tab.state == 2 ? "No groups here" : "Open this tab first");
	else
		buildCountries(tab);
	const int countries = ready ? (int)g_countries.size() : 0;
	if (g_hideCountry > countries - 1) g_hideCountry = countries > 0 ? countries - 1 : 0;
	keepVisible(g_hideCountry, g_hideCountryTop, countries, kHideRows);
	for (int row = 0; row < kHideRows; row++)
	{
		const int i = g_hideCountryTop + row;
		if (i >= countries)
			break;
		const CountryGroups& country = g_countries[(size_t)i];
		const float ry = top + 58 + (float)row * rowH;
		const int shown = shownIn(tab, country), total = (int)country.groups.size();
		const bool current = i == g_hideCountry;
		const bool focused = (!pointerOn() && g_hideColumn == 0 && current) || hovering(cx + 12, ry, cw - 24, rowH - 6);
		if (focused)
			gfx::panel(cx + 12, ry, cw - 24, rowH - 6, 12, 2, kBorder, kSelected);
		else if (current)
			gfx::panel(cx + 12, ry, cw - 24, rowH - 6, 12, 1, kBorderDim, kChip);
		// tick box: full when every group shows, a bar when some do, empty when none
		gfx::panel(cx + 28, ry + 11, 30, 30, 7, 2, shown ? kBorder : kTileLine, shown == total ? kSelected : kDeep);
		if (shown == total)
			centredTight(cx + 28, 30, ry + 13, 22, kAccent, "\xE2\x9C\x93");
		else if (shown > 0)
			gfx::roundRect(cx + 35, ry + 23, 16, 6, 3, kAccent);
		char count[32];
		snprintf(count, sizeof(count), "%d / %d", shown, total);
		const float nw = gfx::textWidth(19, count);
		gfx::text(cx + cw - 30 - nw, ry + 16, 19, kMuted, count);
		gfx::text(cx + 76, ry + 12, 25, shown ? kText : kMuted, country.name, cw - 130 - nw);
		const int tabIndex = g_hideTab;
		clickable(cx + 12, ry, cw - 24, rowH - 6, [i] { g_hideColumn = 0; g_hideCountry = i; g_hideSel = g_hideTop = 0; g_hideChannelList = false; });
		clickable(cx + 20, ry + 4, 48, 44, [i, tabIndex, shown] {
			g_hideColumn = 0;
			g_hideCountry = i;
			if (i < (int)g_countries.size())
				hideGroupsIn(tabIndex, g_countries[(size_t)i].groups, shown > 0);
		});
	}
	drawScrollbar(cx + cw - 10, top + 58, (float)kHideRows * rowH, g_hideCountryTop, countries, kHideRows);

	// ---- middle: the groups of that country, or the hidden channels ----
	const float mx = 516, mw = 720;
	gfx::roundRect(mx, top, mw, 1080 - top - 64, 16, kColumn);
	const int count = ready ? groupsMiddleCount(tab) : 0;
	if (g_hideSel > count - 1) g_hideSel = count > 0 ? count - 1 : 0;
	std::string heading = g_hideChannelList ? "Hidden channels" : countries > 0 ? g_countries[(size_t)g_hideCountry].name + " groups" : "Groups";
	gfx::text(mx + 24, top + 16, 24, kText, heading, mw - 48);
	if (ready && count == 0)
		centredIn(mx, mw, top + 220, 22, kMuted, g_hideChannelList ? "No single channels are hidden" : "Nothing here");
	keepVisible(g_hideSel, g_hideTop, count, kHideRows);
	for (int row = 0; row < kHideRows; row++)
	{
		const int i = g_hideTop + row;
		if (i >= count)
			break;
		const float ry = top + 58 + (float)row * rowH;
		const bool focused = (!pointerOn() && g_hideColumn == 1 && i == g_hideSel) || hovering(mx + 12, ry, mw - 24, rowH - 6);
		if (focused)
			gfx::panel(mx + 12, ry, mw - 24, rowH - 6, 12, 2, kBorder, kSelected);
		const int tabIndex = g_hideTab;
		if (g_hideChannelList)
		{
			const Channel& channel = tab.hiddenChannels[(size_t)i];
			gfx::text(mx + 34, ry + 12, 25, kText, channel.name, mw - 300);
			gfx::text(mx + mw - 250, ry + 16, 19, kMuted, focused ? "\xE2\x9C\x95 Show again" : channel.group, 220);
			clickable(mx + 12, ry, mw - 24, rowH - 6, [i, tabIndex] { g_hideColumn = 1; g_hideSel = i; unhideChannel(tabIndex, i); });
		}
		else
		{
			const int group = g_countries[(size_t)g_hideCountry].groups[(size_t)i];
			const std::string& name = tab.playlist.groups[(size_t)group];
			const bool hidden = tab.hidden.count(name) != 0;
			gfx::panel(mx + 28, ry + 11, 30, 30, 7, 2, hidden ? kTileLine : kBorder, hidden ? kDeep : kSelected);
			if (!hidden)
				centredTight(mx + 28, 30, ry + 13, 22, kAccent, "\xE2\x9C\x93");
			gfx::text(mx + 76, ry + 12, 25, hidden ? kMuted : kText, name, mw - 200);
			if (hidden)
				gfx::text(mx + mw - 100, ry + 16, 19, kMuted, "hidden");
			clickable(mx + 12, ry, mw - 24, rowH - 6, [i, group, tabIndex] { g_hideColumn = 1; g_hideSel = i; setHidden(tabIndex, group, false); });
		}
	}
	drawScrollbar(mx + mw - 10, top + 58, (float)kHideRows * rowH, g_hideTop, count, kHideRows);

	// ---- right: buttons ----
	const float bx = 1256, bw = 584;
	char hiddenLabel[64];
	snprintf(hiddenLabel, sizeof(hiddenLabel), g_hideChannelList ? "Back to groups" : "Hidden channels  (%d)", (int)tab.hiddenChannels.size());
	const std::string labels[4] = { "Show all groups", "Hide all groups", hiddenLabel, g_pin.empty() ? "Lock this page with a PIN" : "Remove the PIN" };
	for (int b = 0; b < 4; b++)
	{
		const float by = top + (float)b * 84;
		const bool focused = (!pointerOn() && g_hideColumn == 2 && g_hideButton == b) || hovering(bx, by, bw, 72);
		gfx::panel(bx, by, bw, 72, 16, 2, focused ? kBorder : kLine, focused ? kSelected : kChip);
		gfx::text(bx + 28, by + 21, 25, focused ? kAccent : kText, labels[b], bw - 56);
		clickable(bx, by, bw, 72, [b] { g_hideColumn = 2; g_hideButton = b; pressGroupsButton(b); });
	}
	const float iy = top + 4 * 84 + 8;
	gfx::panel(bx, iy, bw, 270, 16, 1, kLine, kCard);
	gfx::text(bx + 26, iy + 18, 23, kText, "How this works");
	gfx::text(bx + 26, iy + 56, 20, kMuted, "\xE2\x9C\x95 on a country shows or hides all its groups.", bw - 52);
	gfx::text(bx + 26, iy + 84, 20, kMuted, "Right, then \xE2\x9C\x95, for one group at a time.", bw - 52);
	gfx::text(bx + 26, iy + 124, 20, kMuted, "On the main screen, R3 on a channel or group gives", bw - 52);
	gfx::text(bx + 26, iy + 152, 20, kMuted, "Hide, Hide the country, and Show only this country.", bw - 52);
	gfx::text(bx + 26, iy + 192, 20, kMuted, "L2 / R2 here: Live TV, Movies or Series.", bw - 52);
	if (!g_pinNote.empty())
		gfx::text(bx + 26, iy + 232, 20, kAccent, g_pinNote, bw - 52);
	gfx::text(bx, 1040, 20, kMuted, "\xE2\x9C\x95 Show / Hide    Left / Right Lists    L1 / R1 Tab    \xE2\x97\x8B Back", bw);
}

static void groupsTabInput(Action action)
{
	const Tab& tab = g_tabs[g_hideTab];
	const bool leave = action == A_BACK || action == A_MENU;
	if (leave)
	{
		if (g_haveBrowse)
			g_screen = BROWSE;
		else
			g_mgrTab = 0;
		return;
	}
	if (!g_pin.empty() && !g_pinOpen)
	{
		if (action == A_OK)
		{
			g_pinTyped.clear();
			g_pinAsking = 1;
			openKeyboard(&g_pinTyped, 4, false, "PIN");
		}
		return;
	}
	if (action == A_TAB_PREV || action == A_TAB_NEXT)
	{
		g_hideTab = (g_hideTab + (action == A_TAB_PREV ? 2 : 1)) % 3;
		g_hideCountry = g_hideCountryTop = g_hideSel = g_hideTop = 0;
		g_hideChannelList = false;
		g_hideColumn = 0;
		return;
	}
	const bool ready = g_haveBrowse && tab.state == 2 && !tab.playlist.groups.empty();
	const int countries = ready ? (int)g_countries.size() : 0, middle = ready ? groupsMiddleCount(tab) : 0;
	if (g_hideColumn == 0 && countries == 0)
		g_hideColumn = 2;
	const int page = action == A_PAGE_UP ? -kHideRows : action == A_PAGE_DOWN ? kHideRows : 0;
	switch (g_hideColumn)
	{
	case 0:
		if (action == A_UP || action == A_DOWN || page)
		{
			moveSelection(g_hideCountry, countries, page ? page : action == A_UP ? -1 : 1);
			g_hideSel = g_hideTop = 0;
			g_hideChannelList = false;
		}
		else if (action == A_OK && g_hideCountry < countries)
			hideGroupsIn(g_hideTab, g_countries[(size_t)g_hideCountry].groups, shownIn(tab, g_countries[(size_t)g_hideCountry]) > 0);
		else if (action == A_RIGHT)
			g_hideColumn = middle > 0 ? 1 : 2;
		break;
	case 1:
		if (action == A_UP || action == A_DOWN || page)
			moveSelection(g_hideSel, middle, page ? page : action == A_UP ? -1 : 1);
		else if (action == A_OK && g_hideSel < middle)
		{
			if (g_hideChannelList)
				unhideChannel(g_hideTab, g_hideSel);
			else
				setHidden(g_hideTab, g_countries[(size_t)g_hideCountry].groups[(size_t)g_hideSel], false);
		}
		else if (action == A_LEFT)
			g_hideColumn = countries > 0 ? 0 : 1;
		else if (action == A_RIGHT)
			g_hideColumn = 2;
		break;
	default:
		if (action == A_UP)
			g_hideButton = (g_hideButton + 3) % 4;
		else if (action == A_DOWN)
			g_hideButton = (g_hideButton + 1) % 4;
		else if (action == A_OK)
			pressGroupsButton(g_hideButton);
		else if (action == A_LEFT)
			g_hideColumn = middle > 0 ? 1 : 0;
		break;
	}
}

static void chooseTheme(int index)
{
	g_themeSel = index;
	applyTheme(index);
	applyLook();                                    // (in the TV layout, the theme waits until Classic is chosen)
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
	static int themeTop = 0;
	keepVisible(g_themeSel, themeTop, kThemeCount, 4);
	drawScrollbar(lx + lw - 10, top + 14, 4 * rowH, themeTop, kThemeCount, 4);
	for (int row = 0; row < 4 && themeTop + row < kThemeCount; row++)
	{
		const int i = themeTop + row;
		const Theme& t = kThemes[(size_t)i];
		const float ry = top + 14 + (float)row * rowH;
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
	{
		// the layout: a bigger choice than colours
		static const char* const names[2] = { "Classic", "TV style" };
		static const char* const notes[2] = { "Bar, tabs and columns", "Full-screen, rail, white focus" };
		const float ly = top + 216;
		gfx::text(bx + 4, ly, 23, kText, "Layout");
		for (int i = 0; i < 2; i++)
		{
			const float by = ly + 44 + (float)i * 104;
			const bool focused = (!pointerOn() && g_themeOnLayout && g_layoutSel == i) || hovering(bx, by, bw, 92);
			gfx::panel(bx, by, bw, 92, 16, 2, focused ? kBorder : kLine, focused ? kSelected : kChip);
			gfx::text(bx + 28, by + 16, 27, i == g_layout ? kAccent : kText, names[i]);
			gfx::text(bx + 28, by + 54, 19, kMuted, notes[i], bw - 200);
			if (i == g_layout)
			{
				gfx::panel(bx + bw - 150, by + 28, 120, 34, 17, 1, kBorder, kSelected);
				centredTight(bx + bw - 150, 120, by + 35, 18, kAccent, "IN USE");
			}
			clickable(bx, by, bw, 92, [i] { g_themeOnLayout = true; g_layoutSel = i; g_layout = i; saveSettings(); applyLook(); });
		}
		gfx::text(bx + 4, ly + 44 + 2 * 104 + 6, 19, kMuted, "The layout changes every screen. Your playlists, favorites", bw);
		gfx::text(bx + 4, ly + 44 + 2 * 104 + 32, 19, kMuted, "and settings are the same in both.", bw);
	}
	gfx::panel(bx, top, bw, 196, 16, 1, kLine, kCard);
	gfx::text(bx + 26, top + 20, 23, kText, "Themes");
	gfx::text(bx + 26, top + 58, 20, kMuted, "A theme changes the colours and the shape of", bw - 52);
	gfx::text(bx + 26, top + 86, 20, kMuted, "the corners everywhere in the app.", bw - 52);
	gfx::text(bx + 26, top + 130, 20, kMuted, "More can be added as files: see the themes folder.", bw - 52);
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

