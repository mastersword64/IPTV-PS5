// ---- playlist manager -----------------------------------------------------------

static int g_mgrSel = 0, g_mgrButton = 0, g_mgrTop = 0, g_mgrConfirm = -1;
static bool g_mgrOnButtons = false;
static std::string g_notice;
static const int kMgrRows = 7, kMgrButtons = 3;
static int g_mgrTab = 0;                        // 0 playlists, 1 themes, 2 groups
static int g_themeSel = 0;
static const int kMgrTabs = 8;
static const char* const kMgrTabName[kMgrTabs] = { "Playlists", "Themes", "Groups", "Pointer", "Playback", "Remote", "Profiles", "About" };
static int g_playSel = 0;                       // the Playback tab's rows
static const int kPlayRows = 10;
static int g_ptrSel = 0;                        // the Pointer tab's rows
static const int kPtrRows = 5;
// the Groups tab: which categories are shown
static bool g_themeOnLayout = false;            // Themes tab: focus is on the layout choice
static int g_layoutSel = 0;
static int g_hideTab = 0, g_hideSel = 0, g_hideTop = 0, g_hideButton = 0;
static bool g_hideOnButtons = false;
static bool g_hideCountriesReset = false;
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
	g_hideCountriesReset = true;
	g_screen = MANAGER;
	fadeIn();
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
		// the clock moves in half-hour steps (some places are on the half hour)
		g_clockOffsetMinutes += 30 * by;
		if (g_clockOffsetMinutes > 840) g_clockOffsetMinutes = -720;
		if (g_clockOffsetMinutes < -720) g_clockOffsetMinutes = 840;
		g_clockSet = true;
	}
	saveSettings();
}

// The Playback tab: how video, subtitles and sound are presented.
static void changePlayback(int row, int by)
{
	if (row == 0)
	{
		g_videoDetail = (g_videoDetail + 3 + by) % 3;
		applyVideoBudget();
	}
	else if (row == 1)
		g_pictureFit = (g_pictureFit + 3 + by) % 3;
	else if (row == 2)
		g_subtitleSize = (g_subtitleSize + 3 + by) % 3;
	else if (row == 3)
	{
		g_subtitleDelay += 0.25 * by;
		if (g_subtitleDelay < -10) g_subtitleDelay = -10;
		if (g_subtitleDelay > 10) g_subtitleDelay = 10;
		if (g_subtitleDelay > -0.01 && g_subtitleDelay < 0.01) g_subtitleDelay = 0;
		player::setSubtitleDelay(g_subtitleDelay);
	}
	else if (row == 4)
	{
		g_audioDelay += 0.05 * by;
		if (g_audioDelay < -1) g_audioDelay = -1;
		if (g_audioDelay > 1) g_audioDelay = 1;
		if (g_audioDelay > -0.01 && g_audioDelay < 0.01) g_audioDelay = 0;
		player::setAudioDelay(g_audioDelay);
	}
	else if (row == 5)
		g_autoNext = !g_autoNext;
	else if (row == 6)
	{
		// off, then a few useful lengths
		static const int steps[7] = { 0, 15, 30, 45, 60, 90, 120 };
		int at = 0;
		for (int i = 0; i < 7; i++)
			if (steps[i] == g_sleepMinutes)
				at = i;
		g_sleepMinutes = steps[(at + 7 + by) % 7];
		g_sleepAt = g_sleepMinutes ? nowSeconds() + g_sleepMinutes * 60.0 : 0;
	}
	else if (row == 7)
		g_autoPreview = !g_autoPreview;
	else if (row == 8)
		g_deinterlace = !g_deinterlace;
	else if (row == 9)
	{
		g_menuSounds = !g_menuSounds;
		if (g_menuSounds)
			player::menuSound(2);
	}
	saveSettings();
}

// ---- Settings > Remote: a phone or computer as the controller -------------------------------
// Shows the code and address of the page the console serves (see the part before this one), and
// lets it be switched off, or given a new key.

static void remoteToggle()
{
	g_remoteOn = !g_remoteOn;
	g_webTried = false;
	g_webNote.clear();
	saveSettings();
}

static void remoteChangeKey()
{
	if (!g_remoteOn)
		return;
	remoteNewKey();
	saveSettings();
	web::stop();                                        // it starts again, with the new key, on the next frame
	g_webTried = false;
	g_webNote = "A new key was made. Devices that used the old address must scan the code again.";
}

static void drawPhoneTab()
{
	const float top = 208;
	gfx::roundRect(96, top, 1744, 1080 - top - 64, 16, kColumn);
	const float bx = 760, by = 1080 - 64 - 96;
	const auto button = [](float x, float y, float w, const std::string& label, void (*press)()) {
		const bool hover = hovering(x, y, w, 62);
		gfx::panel(x, y, w, 62, 14, hover ? 2.0f : 1.0f, hover ? kBorder : kLine, hover ? kSelected : kChip);
		centredTight(x, w, y + 18, 23, hover ? kAccent : kText, label);
		clickable(x, y, w, 62, press);
	};
	if (!g_remoteOn)
	{
		centredIn(96, 1744, top + 200, 34, kText, "The remote page is switched off");
		centredIn(96, 1744, top + 258, 23, kMuted, "Switched on, a phone or computer on your network can be the remote control, browse and");
		centredIn(96, 1744, top + 290, 23, kMuted, "play channels, type into the TV keyboard, and add playlists.");
		button(810, top + 360, 300, "\xE2\x9C\x95   Switch on", [] { remoteToggle(); });
		gfx::text(1250, 1040, 20, kMuted, "\xE2\x9C\x95 Switch on    L1 / R1 Tab    \xE2\x97\x8B / OPTIONS Back", 590);
		return;
	}
	if (!web::running())
	{
		centredIn(96, 1744, top + 220, 30, kText, "The remote page is not available");
		centredIn(96, 1744, top + 274, 22, kMuted, web::problem().empty() ? "Starting..." : web::problem());
		centredIn(96, 1744, top + 330, 22, kMuted, "Playlists can still be added on the Playlists tab, or sent to the console by FTP.");
		button(810, top + 400, 300, "\xE2\x9C\x95   Switch off", [] { remoteToggle(); });
		return;
	}
	const std::string address = web::address();
	if (g_qrFor != address)
	{
		g_qrFor = address;
		if (!qrEncode(address, g_qrModules, g_qrSize))
			g_qrSize = 0;
	}
	// the code, dark squares on a white card with the light border a code needs
	const float card = 520, qx = 150, qy = top + 60;
	const float keep = gfx::radiusScale();
	gfx::setRadiusScale(1.0f);
	gfx::roundRect(qx, qy, card, card, 24, 0xffffffff);
	if (g_qrSize > 0)
	{
		const float cell = (float)(int)((card - 40) / (float)(g_qrSize + 4));
		const float start = qx + (card - cell * (float)g_qrSize) / 2, startY = qy + (card - cell * (float)g_qrSize) / 2;
		for (int y = 0; y < g_qrSize; y++)
		{
			// each run of dark squares in a row is one rectangle
			int x = 0;
			while (x < g_qrSize)
			{
				if (!g_qrModules[(size_t)y * g_qrSize + x])
				{
					x++;
					continue;
				}
				int end = x;
				while (end < g_qrSize && g_qrModules[(size_t)y * g_qrSize + end])
					end++;
				gfx::rect(start + (float)x * cell, startY + (float)y * cell, (float)(end - x) * cell, cell, 0x000000ff);
				x = end;
			}
		}
	}
	gfx::setRadiusScale(keep);
	centredIn(qx, card, qy + card + 18, 20, kMuted, "Scan with a phone's camera");

	const float tx = 760, tw = 1020;
	gfx::text(tx, top + 44, 36, kText, "Your phone as the remote control");
	gfx::text(tx, top + 104, 23, kMuted, "1.  Connect the phone or computer to the same network as the console.", tw);
	gfx::text(tx, top + 140, 23, kMuted, "2.  Point its camera at the code, or type this address in a browser:", tw);
	const float aw = gfx::textWidth(34, address) + 56;
	gfx::panel(tx, top + 184, aw > tw ? tw : aw, 70, 16, 2, kBorder, kSelected);
	gfx::text(tx + 28, top + 201, 34, kAccent, address, tw - 56);
	gfx::text(tx, top + 280, 23, kText, "On that page:", tw);
	static const char* const can[5] = {
		"Remote  \xC2\xB7  the controller's buttons: arrows, OK, Back, Settings, L1 / R1 and the rest",
		"Live TV, Movies, Series  \xC2\xB7  browse groups, favorites and episodes; tap one to play it here",
		"Search  \xC2\xB7  channels, films and series by name, played with a tap",
		"Radio  \xC2\xB7  favorite, recent and popular stations, and a search by name",
		"Setup  \xC2\xB7  add a playlist with a real keyboard, send an M3U file, save or restore a backup" };
	for (int i = 0; i < 5; i++)
	{
		gfx::roundRect(tx + 6, top + 331 + (float)i * 34, 8, 8, 4, kAccent);
		gfx::text(tx + 30, top + 320 + (float)i * 34, 21, kMuted, can[i], tw - 30);
	}
	gfx::text(tx, top + 498, 21, kMuted, "When the keyboard is open on the TV, the page offers a box to type into instead.", tw);

	const bool visited = web::visits() > 0;
	gfx::panel(tx, top + 546, tw, 70, 16, 1, kLine, kCard);
	if (!g_webNote.empty())
		gfx::text(tx + 28, top + 567, 23, kAccent, g_webNote, tw - 56);
	else
	{
		if (!visited)
			spinner(tx + 46, top + 581, 13, kMuted);
		gfx::text(tx + (visited ? 28.0f : 78.0f), top + 567, 23, visited ? kText : kMuted, visited ? "A device has opened the page" : "Waiting for a device to open the page...", tw - 110);
	}
	gfx::text(tx, top + 630, 19, kMuted, "The address ends in a key. Only devices on your network that have it can use the page;", tw);
	gfx::text(tx, top + 656, 19, kMuted, "a new key shuts out any device that had the old one.", tw);
	button(bx, by, 330, "\xE2\x9C\x95   Switch the page off", [] { remoteToggle(); });
	button(bx + 350, by, 280, "\xE2\x96\xA1   Make a new key", [] { remoteChangeKey(); });
	gfx::text(1250, 1040, 20, kMuted, "\xE2\x9C\x95 On / Off    \xE2\x96\xA1 New key    L1 / R1 Tab    \xE2\x97\x8B Back", 590);
}

static void phoneTabInput(Action action)
{
	if (action == A_OK)
		remoteToggle();
	else if (action == A_FAVOURITE)
		remoteChangeKey();
	else if (action == A_BACK || action == A_MENU)
	{
		if (g_haveBrowse)
			g_screen = BROWSE;
		else
			g_mgrTab = 0;
	}
}

// ---- Settings > About ------------------------------------------------------------------------

static void drawAboutTab()
{
	const float lx = 96, lw = 1120, top = 208;
	gfx::roundRect(lx, top, lw, 1080 - top - 64, 16, kColumn);
	// the name, as on the console's home screen
	gfx::panel(lx + 40, top + 36, 112, 112, 24, 2, kBorderDim, kChip);
	centredTight(lx + 40, 112, top + 62, 56, kAccent, "\xE2\x96\xB6");
	gfx::bold(true);
	gfx::text(lx + 180, top + 40, 44, kText, "IPTV for PS5");
	gfx::bold(false);
	gfx::text(lx + 180, top + 100, 24, kAccent, std::string("Version ") + kAppVersion + "   \xC2\xB7   built " + __DATE__);
	float y = top + 186;
	y = drawWrapped(lx + 40, y, lw - 80, 23, kText,
		"A native player for IPTV playlists on the PlayStation 5: M3U and M3U8 playlists, Xtream Codes accounts and Stalker / Ministra portals, "
		"with live TV, a programme guide and catch-up, movies and series with resume, search, radio, and a remote control page for your phone.", 4) + 22;
	y = drawWrapped(lx + 40, y, lw - 80, 21, kMuted,
		"The app comes with no playlists, channels or other content: you add your own. The stations on the Radio screen are not part of the app either; "
		"they are looked up, when you open that screen, in Radio Browser (radio-browser.info), a public directory kept by volunteers.", 4) + 30;
	gfx::text(lx + 40, y, 24, kText, "Made with");
	y += 42;
	static const char* const credits[6][2] = {
		{ "IPTVnator", "the desktop app this one is modelled on (a separate project, by 4gray)" },
		{ "FFmpeg", "reading and decoding video and sound" },
		{ "SDL2", "sound output and the controller" },
		{ "FreeType", "lettering" },
		{ "curl", "downloads" },
		{ "Radio Browser", "the directory of public radio stations" } };
	for (const auto& credit : credits)
	{
		gfx::text(lx + 40, y, 21, kAccent, credit[0]);
		gfx::text(lx + 240, y, 21, kMuted, credit[1], lw - 290);
		y += 34;
	}
	y += 20;
	drawWrapped(lx + 40, y, lw - 80, 19, kMuted,
		"An independent homebrew project, released under the MIT License. It is not affiliated with, endorsed by, or supported by Sony Interactive Entertainment, "
		"the IPTVnator project or Radio Browser. What you watch or listen to with it, and your right to, are your own responsibility.", 4);

	const float bx = 1250, bw = 590;
	gfx::panel(bx, top, bw, 372, 16, 1, kLine, kCard);
	gfx::text(bx + 26, top + 18, 23, kText, "On this console");
	char line[160];
	float iy = top + 62;
	const auto row = [&](const char* label, const std::string& value, uint32_t colour) {
		gfx::text(bx + 26, iy, 20, kMuted, label);
		gfx::text(bx + 200, iy, 20, colour, value, bw - 226);
		iy += 34;
	};
	snprintf(line, sizeof(line), "%d", (int)g_sources.size());
	row("Playlists", line, kText);
	row("Open now", g_active >= 0 && g_active < (int)g_sources.size() && g_haveBrowse ? g_sources[(size_t)g_active].name : std::string("none"), kText);
	row("Profile", g_profile >= 0 && g_profile < (int)g_profiles.size() ? g_profiles[(size_t)g_profile].name : std::string("Main"), kText);
	row("Layout", g_layout == 1 ? "TV style" : "Classic", kText);
	snprintf(line, sizeof(line), "%d x %d, %.0f frames per second", g_drawW, g_drawH, g_fps);
	row("Drawing at", line, kText);
	row("Remote page", !g_remoteOn ? std::string("switched off") : web::running() ? "on, port " + std::to_string(kWebPort) : std::string("not available"), kText);
	row("Data folder", g_dataDir, kAccent);
	row("Log", g_dataDir + "/iptv-log.txt", kAccent);
	row("Pictures", imagesSupported() ? "PNG and JPEG pointers supported" : "cursor files only", kText);

	const float hy = top + 392;
	gfx::panel(bx, hy, bw, 208, 16, 1, kLine, kCard);
	gfx::text(bx + 26, hy + 18, 23, kText, "If something goes wrong");
	drawWrapped(bx + 26, hy + 58, bw - 52, 20, kMuted,
		"The log records what was asked of your provider and what came back. Fetch it by FTP from the data folder; "
		"the log of the run before this one is kept beside it as iptv-log-previous.txt.", 5);
	gfx::text(bx, 1040, 20, kMuted, "L1 / R1 Tab    \xE2\x97\x8B / OPTIONS Back", bw);
}

static void aboutTabInput(Action action)
{
	if (action == A_BACK || action == A_MENU)
	{
		if (g_haveBrowse)
			g_screen = BROWSE;
		else
			g_mgrTab = 0;
	}
}

// ---- Settings > Profiles ---------------------------------------------------------------------
// Each profile has its own favourites, recently watched, resume points, watched ticks and hidden
// groups. Playlists and settings are shared.

static int g_profileSel = 0;
static std::string g_profileTyped;
static bool g_profileNaming = false;

static void useProfile(int index)
{
	if (index < 0 || index >= (int)g_profiles.size() || index == g_profile)
		return;
	g_profile = index;
	saveSettings();
	logLine("profile: %s", g_profiles[(size_t)index].name.c_str());
	if (g_active >= 0)
		startLoading(g_active);                     // everything personal is read again for this profile
}

static void drawProfilesTab()
{
	const float lx = 96, lw = 1120, top = 208, rowH = 92;
	// a name was typed for a new profile
	if (g_profileNaming && !g_keyboard)
	{
		g_profileNaming = false;
		const std::string name = trim(g_profileTyped);
		if (!name.empty() && g_profiles.size() < 6)
		{
			int id = 1;
			for (const Profile& profile : g_profiles)
				if (profile.id >= id)
					id = profile.id + 1;
			g_profiles.push_back({ id, name });
			saveProfiles();
			g_profileSel = (int)g_profiles.size() - 1;
		}
	}
	gfx::roundRect(lx, top, lw, 1080 - top - 64, 16, kColumn);
	const int count = (int)g_profiles.size(), rows = count + (count < 6 ? 1 : 0);
	if (g_profileSel > rows - 1)
		g_profileSel = rows - 1;
	for (int i = 0; i < rows; i++)
	{
		const float ry = top + 14 + (float)i * rowH;
		const bool selected = (!pointerOn() && i == g_profileSel) || hovering(lx + 14, ry, lw - 28, rowH - 10);
		if (selected)
			gfx::panel(lx + 14, ry, lw - 28, rowH - 10, 14, 2, kBorder, kSelected);
		if (i >= count)
		{
			gfx::text(lx + 44, ry + 24, 28, selected ? kAccent : kMuted, "+   New profile");
			clickable(lx + 14, ry, lw - 28, rowH - 10, [i] { g_profileSel = i; g_profileTyped.clear(); g_profileNaming = true; openKeyboard(&g_profileTyped, 16, false, "Name"); });
			continue;
		}
		const Profile& profile = g_profiles[(size_t)i];
		gfx::roundRect(lx + 36, ry + 13, 56, 56, 28, i == g_profile ? kAccent : kTile);
		centredTight(lx + 36, 56, ry + 26, 26, i == g_profile ? kDeep : kText, initials(profile.name).substr(0, 1));
		gfx::text(lx + 116, ry + 24, 28, i == g_profile ? kAccent : kText, profile.name, lw - 420);
		if (i == g_profile)
		{
			gfx::panel(lx + lw - 170, ry + 24, 120, 34, 17, 1, kBorder, kSelected);
			centredTight(lx + lw - 170, 120, ry + 31, 18, kAccent, "IN USE");
		}
		else if (selected)
			gfx::text(lx + lw - 330, ry + 28, 20, kMuted, i == 0 ? "\xE2\x9C\x95  Use" : "\xE2\x9C\x95  Use     \xE2\x96\xA1  Remove");
		clickable(lx + 14, ry, lw - 28, rowH - 10, [i] { g_profileSel = i; useProfile(i); });
	}
	const float bx = 1250, bw = 590;
	gfx::panel(bx, top, bw, 290, 16, 1, kLine, kCard);
	gfx::text(bx + 26, top + 20, 23, kText, "Profiles");
	gfx::text(bx + 26, top + 58, 20, kMuted, "Each profile keeps its own favorites, recently", bw - 52);
	gfx::text(bx + 26, top + 86, 20, kMuted, "watched, resume points, watched ticks and hidden", bw - 52);
	gfx::text(bx + 26, top + 114, 20, kMuted, "groups and channels.", bw - 52);
	gfx::text(bx + 26, top + 158, 20, kMuted, "Playlists and settings are shared by everyone.", bw - 52);
	gfx::text(bx + 26, top + 202, 20, kMuted, "Removing a profile leaves its saved lists on the", bw - 52);
	gfx::text(bx + 26, top + 230, 20, kMuted, "console, but takes it off this list.", bw - 52);
	gfx::text(bx, 1040, 20, kMuted, "\xE2\x9C\x95 Use    \xE2\x96\xA1 Remove    L1 / R1 Tab    \xE2\x97\x8B Back", bw);
}

static void profilesTabInput(Action action)
{
	const int count = (int)g_profiles.size(), rows = count + (count < 6 ? 1 : 0);
	switch (action)
	{
	case A_UP: g_profileSel = (g_profileSel + rows - 1) % rows; break;
	case A_DOWN: g_profileSel = (g_profileSel + 1) % rows; break;
	case A_OK:
		if (g_profileSel >= count)
		{
			g_profileTyped.clear();
			g_profileNaming = true;
			openKeyboard(&g_profileTyped, 16, false, "Name");
		}
		else
			useProfile(g_profileSel);
		break;
	case A_FAVOURITE:
		if (g_profileSel > 0 && g_profileSel < count && g_profileSel != g_profile)
		{
			const int inUse = g_profiles[(size_t)g_profile].id;
			g_profiles.erase(g_profiles.begin() + g_profileSel);
			for (int i = 0; i < (int)g_profiles.size(); i++)
				if (g_profiles[(size_t)i].id == inUse)
					g_profile = i;
			saveProfiles();
		}
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

static void drawPlaybackTab()
{
	static const char* const labels[kPlayRows] = { "Video picture", "Picture shape", "Subtitle size", "Subtitle timing", "Sound timing", "Next episode", "Sleep timer", "Live preview", "Broadcast combing", "Menu sounds" };
	static const char* const details[3] = { "Sharpest", "Balanced", "Smoothest" };
	static const char* const fits[3] = { "Fit (whole picture)", "Zoom (fill the screen)", "Stretch" };
	static const char* const sizes[3] = { "Small", "Medium", "Large" };
	static const char* const notes[kPlayRows] = {
		"Sharpest keeps the most detail; Smoothest keeps motion and the menus fluid.",
		"In full screen. Zoom removes black bars by cutting off the picture's edges. Also on the Picture button while watching.",
		"How large subtitles are drawn.",
		"Later (+) or earlier (\xE2\x88\x92), if the words do not match the speech.",
		"Raise it if lips move before you hear the words (a TV or sound bar adding delay); lower it for the opposite.",
		"When an episode ends, the next one starts after a few seconds.",
		"Stops whatever is playing after this long. It is off again each time the app starts. (An app cannot switch the console off.)",
		"TV layout only. Resting on a live channel for a second starts it playing behind the list; or choose to play only when you press \xE2\x9C\x95.",
		"Many live channels are broadcast interlaced, which shows as fine comb lines on anything moving. Smoothing removes them, at a small cost in sharpness on those channels.",
		"A soft tick when moving, and a short note when choosing or going back. They are quiet, and silent during full-screen playback." };
	const float lx = 96, lw = 1120, top = 208, rowH = 78;
	gfx::roundRect(lx, top, lw, 1080 - top - 64, 16, kColumn);
	for (int row = 0; row < kPlayRows; row++)
	{
		const float ry = top + 18 + (float)row * rowH;
		const bool selected = (!pointerOn() && row == g_playSel) || hovering(lx + 14, ry, lw - 28, rowH - 10);
		if (selected)
			gfx::panel(lx + 14, ry, lw - 28, rowH - 10, 14, 2, kBorder, kSelected);
		gfx::text(lx + 40, ry + 28, 27, kText, labels[row]);
		const float vx = lx + 470, vw = 520;
		const bool leftHover = hovering(vx, ry + 14, 60, 58), rightHover = hovering(vx + vw - 60, ry + 14, 60, 58);
		gfx::panel(vx, ry + 14, 60, 58, 12, leftHover ? 2.0f : 1.0f, leftHover ? kBorder : kLine, kChip);
		gfx::panel(vx + vw - 60, ry + 14, 60, 58, 12, rightHover ? 2.0f : 1.0f, rightHover ? kBorder : kLine, kChip);
		centredTight(vx, 60, ry + 22, 34, kText, "\xE2\x80\xB9");
		centredTight(vx + vw - 60, 60, ry + 22, 34, kText, "\xE2\x80\xBA");
		clickable(lx + 14, ry, lw - 28, rowH - 10, [row] { g_playSel = row; });
		clickable(vx, ry + 14, 60, 58, [row] { g_playSel = row; changePlayback(row, -1); });
		clickable(vx + vw - 60, ry + 14, 60, 58, [row] { g_playSel = row; changePlayback(row, 1); });

		char number[48];
		std::string value;
		if (row == 0) value = details[g_videoDetail];
		else if (row == 1) value = fits[g_pictureFit];
		else if (row == 2) value = sizes[g_subtitleSize];
		else if (row == 3)
		{
			if (g_subtitleDelay == 0) value = "In step";
			else { snprintf(number, sizeof(number), "%s%.2f s", g_subtitleDelay > 0 ? "+" : "\xE2\x88\x92", g_subtitleDelay < 0 ? -g_subtitleDelay : g_subtitleDelay); value = number; }
		}
		else if (row == 4)
		{
			if (g_audioDelay == 0) value = "In step";
			else { snprintf(number, sizeof(number), "%s%.2f s", g_audioDelay > 0 ? "+" : "\xE2\x88\x92", g_audioDelay < 0 ? -g_audioDelay : g_audioDelay); value = number; }
		}
		else if (row == 5) value = g_autoNext ? "Plays by itself" : "Off";
		else if (row == 7) value = g_autoPreview ? "As you browse" : "Only when chosen";
		else if (row == 8) value = g_deinterlace ? "Smoothed" : "Left as it is";
		else if (row == 9) value = g_menuSounds ? "On" : "Off";
		else if (g_sleepMinutes == 0) value = "Off";
		else
		{
			const int left = g_sleepAt > nowSeconds() ? (int)((g_sleepAt - nowSeconds()) / 60) + 1 : 0;
			snprintf(number, sizeof(number), "%d min  (%d left)", g_sleepMinutes, left);
			value = number;
		}
		centredIn(vx + 70, vw - 140, ry + 29, 26, kAccent, value);
	}

	const float bx = 1250, bw = 590;
	gfx::panel(bx, top, bw, 260, 16, 1, kLine, kCard);
	gfx::text(bx + 26, top + 18, 23, kText, labels[g_playSel]);
	// the note for the row in use, wrapped to the card
	{
		std::string rest = notes[g_playSel];
		float y = top + 58;
		while (!rest.empty() && y < top + 240)
		{
			size_t cut = rest.size();
			while (cut > 0 && gfx::textWidth(20, rest.substr(0, cut)) > bw - 52)
			{
				const size_t space = rest.rfind(' ', cut - 1);
				if (space == std::string::npos || space == 0)
					break;
				cut = space;
			}
			gfx::text(bx + 26, y, 20, kMuted, rest.substr(0, cut), bw - 52);
			rest = cut < rest.size() ? rest.substr(cut + 1) : std::string();
			y += 28;
		}
	}
	gfx::text(bx, 1040, 20, kMuted, "Left / Right Change    L1 / R1 Tab    \xE2\x97\x8B / OPTIONS Back", bw);
}

static void playbackTabInput(Action action)
{
	switch (action)
	{
	case A_UP: g_playSel = (g_playSel + kPlayRows - 1) % kPlayRows; break;
	case A_DOWN: g_playSel = (g_playSel + 1) % kPlayRows; break;
	case A_LEFT: changePlayback(g_playSel, -1); break;
	case A_RIGHT:
	case A_OK: changePlayback(g_playSel, 1); break;
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

static void drawPointerTab()
{
	static const char* const labels[kPtrRows] = { "Pointer speed", "Pointer size", "Pointer", "Show frame rate", "Clock" };
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
		{
			char zone[48];
			const int minutes = g_clockOffsetMinutes < 0 ? -g_clockOffsetMinutes : g_clockOffsetMinutes;
			snprintf(zone, sizeof(zone), "%s   (UTC%s%d:%02d)", clockNow().c_str(), g_clockOffsetMinutes < 0 ? "\xE2\x88\x92" : "+", minutes / 60, minutes % 60);
			centredTight(cx, cw, ry + 29, 26, kAccent, zone);
		}
	}
	gfx::text(lx + 40, top + 18 + (float)kPtrRows * rowH + 6, 20, kMuted, "Clock: Left / Right moves it half an hour at a time until it shows your local time.", lw - 80);

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

