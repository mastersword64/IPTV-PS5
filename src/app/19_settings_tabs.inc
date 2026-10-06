// ---- playlist manager -----------------------------------------------------------

static int g_mgrSel = 0, g_mgrButton = 0, g_mgrTop = 0, g_mgrConfirm = -1;
static bool g_mgrOnButtons = false;
static std::string g_notice;
static const int kMgrRows = 7, kMgrButtons = 3;
static int g_mgrTab = 0;                        // 0 playlists, 1 themes, 2 groups
static int g_themeSel = 0;
static const int kMgrTabs = 7;
static const char* const kMgrTabName[kMgrTabs] = { "Playlists", "Themes", "Groups", "Pointer", "Playback", "Remote", "Profiles" };
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

// ---- Settings > Remote: add playlists from another device -----------------------------------
// The console serves a small web page on the home network. Scanning the code (or typing the
// address) on a phone or computer opens it; playlists can be added there with a real keyboard,
// an M3U file sent across, and a backup saved or restored. It runs only while this tab is open.

static const int kWebPort = 8080;
static bool g_webTried = false;
static std::string g_webNote;                           // the last thing that arrived
static std::vector<uint8_t> g_qrModules;
static int g_qrSize = 0;
static std::string g_qrFor;

// Carries out what was asked for on the page.
static void pumpWeb()
{
	web::Request wanted;
	while (web::take(wanted))
	{
		if (wanted.kind == "add")
		{
			const std::string type = wanted.fields["type"], address = trim(wanted.fields["a"]), name = trim(wanted.fields["name"]);
			Source source;
			if (type == "xtream")
				source = Source{ "xtream", name.empty() ? hostOf(address) : name, address, trim(wanted.fields["b"]), trim(wanted.fields["c"]), "", "", normalizeMac(wanted.fields["mac"]) };
			else if (type == "stalker")
			{
				const std::string mac = normalizeMac(wanted.fields["b"]);
				if (mac.empty())
				{
					g_webNote = "Not added: the MAC address needs six pairs of digits, like 00:1A:79:12:34:56";
					continue;
				}
				source = Source{ "stalker", name.empty() ? hostOf(address) : name, address, mac, "", "", "" };
			}
			else
				source = Source{ "url", name.empty() ? hostOf(address) : name, address, "", "", "", "" };
			source.id = sourceId(source);
			int index = -1;
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
			saveSources(g_dataDir, g_sources);
			g_webNote = "Added: " + source.name;
			logLine("web: added the playlist \"%s\" (%s)", source.name.c_str(), source.type.c_str());
			if (!g_haveBrowse)
			{
				startLoading(index);                    // the first playlist is opened straight away
				return;
			}
		}
		else if (wanted.kind == "file")
		{
			// keep the name harmless: letters, digits and a few marks only
			std::string name;
			for (const char c : wanted.fields["name"])
				name += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' ? c : '_';
			if (name.empty() || name[0] == '.')
				name = "sent-" + name + ".m3u";
			if (name.find(".m3u") == std::string::npos)
				name += ".m3u";
			const std::string folder = g_dataDir + "/playlists";
			mkdir(folder.c_str(), 0777);
			writeTextFile(folder + "/" + name, wanted.body);
			std::string check;
			if (readTextFile(folder + "/" + name, check) && check.size() == wanted.body.size())
			{
				const std::string activeId = g_active >= 0 ? sourceId(g_sources[(size_t)g_active]) : std::string();
				g_sources = loadSources(g_dataDir);      // finds the new file
				saveSources(g_dataDir, g_sources);
				for (int i = 0; i < (int)g_sources.size(); i++)
					if (sourceId(g_sources[(size_t)i]) == activeId)
						g_active = i;
				g_webNote = "Received the file: " + name;
				logLine("web: received %s (%d bytes)", name.c_str(), (int)wanted.body.size());
			}
			else
				g_webNote = "The file could not be saved on the console";
		}
		else if (wanted.kind == "restore")
		{
			// each part is "FILE <name> <bytes>", a line break, that many bytes, a line break
			int restored = 0;
			size_t pos = wanted.body.find('\n');
			while (pos != std::string::npos && pos + 1 < wanted.body.size())
			{
				const size_t lineEnd = wanted.body.find('\n', pos + 1);
				if (lineEnd == std::string::npos)
					break;
				const std::string line = wanted.body.substr(pos + 1, lineEnd - pos - 1);
				const size_t gap = line.rfind(' ');
				if (line.compare(0, 5, "FILE ") != 0 || gap == std::string::npos || gap <= 5)
					break;
				const std::string name = line.substr(5, gap - 5);
				const size_t bytes = (size_t)atol(line.c_str() + gap + 1);
				if (lineEnd + 1 + bytes > wanted.body.size())
					break;
				const bool safe = name.find('/') == std::string::npos && name.find("..") == std::string::npos
					&& name.size() > 4 && name.compare(name.size() - 4, 4, ".txt") == 0 && name.find("log") == std::string::npos;
				if (safe)
				{
					writeTextFile(g_dataDir + "/" + name, wanted.body.substr(lineEnd + 1, bytes));
					restored++;
				}
				pos = lineEnd + bytes + 1;
			}
			g_webNote = restored ? "Backup restored (" + std::to_string(restored) + " files). Close and reopen the app to use it." : "That backup held nothing usable";
			logLine("web: restored %d files from a backup", restored);
		}
	}
}

static void drawPhoneTab()
{
	const float top = 208;
	if (!web::running() && !g_webTried)
	{
		g_webTried = true;
		g_webNote.clear();
		std::vector<std::string> names;
		for (const Source& source : g_sources)
			names.push_back(source.name + "  (" + sourceKind(source) + ")");
		web::setPlaylists(names);
#ifdef IPTV_HOST_HINT
		web::start(g_dataDir, kWebPort, IPTV_HOST_HINT);
#else
		web::start(g_dataDir, kWebPort, "");
#endif
	}
	pumpWeb();
	if (g_screen != MANAGER)
		return;                                         // a first playlist was added and is being opened

	gfx::roundRect(96, top, 1744, 1080 - top - 64, 16, kColumn);
	if (!web::running())
	{
		centredIn(96, 1744, top + 220, 30, kText, "Adding from another device is not available");
		centredIn(96, 1744, top + 274, 22, kMuted, web::problem());
		centredIn(96, 1744, top + 330, 22, kMuted, "Playlists can still be added on the Playlists tab, or sent to the console by FTP.");
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

	const float tx = 760, tw = 1020;
	gfx::text(tx, top + 60, 36, kText, "Add playlists from your phone or computer");
	gfx::text(tx, top + 124, 24, kMuted, "1.  Connect the phone to the same Wi-Fi as the console.", tw);
	gfx::text(tx, top + 164, 24, kMuted, "2.  Point its camera at the code, or type this address in a browser:", tw);
	gfx::panel(tx, top + 214, gfx::textWidth(40, address) + 60, 76, 16, 2, kBorder, kSelected);
	gfx::text(tx + 30, top + 230, 40, kAccent, address);
	gfx::text(tx, top + 316, 24, kMuted, "3.  Fill in the playlist there. It appears on the Playlists tab here.", tw);
	gfx::text(tx, top + 372, 22, kMuted, "The page can also send an M3U file, and save or restore a backup of", tw);
	gfx::text(tx, top + 402, 22, kMuted, "your playlists, settings, favorites and watch history.", tw);

	const bool visited = web::visits() > 0;
	gfx::panel(tx, top + 460, tw, 92, 16, 1, kLine, kCard);
	if (!g_webNote.empty())
		gfx::text(tx + 28, top + 490, 26, kAccent, g_webNote, tw - 56);
	else
	{
		if (!visited)
			spinner(tx + 46, top + 506, 14, kMuted);
		gfx::text(tx + (visited ? 28.0f : 78.0f), top + 490, 26, visited ? kText : kMuted, visited ? "A device has opened the page" : "Waiting for a device to open the page...", tw - 110);
	}
	gfx::text(tx, top + 580, 20, kMuted, "Only devices on your own network can reach this page, and only while this tab is open.", tw);
	gfx::text(1250, 1040, 20, kMuted, "L1 / R1 Tab    \xE2\x97\x8B / OPTIONS Back", 590);
}

static void phoneTabInput(Action action)
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

