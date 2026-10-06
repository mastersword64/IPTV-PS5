static void drawManager()
{
	// heading, as on IPTVnator's start page
	gfx::panel(96, 40, 92, 92, 18, 2, kBorderDim, kChip);
	centredTight(96, 92, 58, 50, kAccent, "\xE2\x9A\x99");
	gfx::text(212, 46, 40, kText, "Settings");
	gfx::text(212, 98, 24, kMuted, "Playlists, themes, groups, pointer, playback, remote setup and profiles");

	// tabs
	for (int tab = 0; tab < kMgrTabs; tab++)
	{
		const float tx = 96 + (float)tab * 212, tw = 200;
		const bool current = tab == g_mgrTab, hover = hovering(tx, 148, tw, 46);
		gfx::panel(tx, 148, tw, 46, 23, current || hover ? 2.0f : 1.0f, current || hover ? kBorder : kLine, current ? kSelected : kChip);
		centredTight(tx, tw, 158, 22, current ? kAccent : kText, kMgrTabName[tab]);
		clickable(tx, 148, tw, 46, [tab] { g_mgrTab = tab; g_mgrConfirm = -1; });
	}
	gfx::text(96 + (float)kMgrTabs * 212 + 14, 160, 20, kMuted, "L1 / R1  Switch tab");
	if (g_mgrTab == 1)
	{
		drawThemes();
		return;
	}
	if (g_mgrTab == 2)
	{
		drawGroupsTab();
		return;
	}
	if (g_mgrTab == 3)
	{
		drawPointerTab();
		return;
	}
	if (g_mgrTab == 4)
	{
		drawPlaybackTab();
		return;
	}
	if (g_mgrTab == 5)
	{
		drawPhoneTab();
		return;
	}
	if (g_mgrTab == 6)
	{
		drawProfilesTab();
		return;
	}

	const float lx = 96, lw = 1120, top = 252, rowH = 96;
	gfx::roundRect(lx, top - 44, lw, 1080 - top - 20, 16, kColumn);
	gfx::text(lx + 26, top - 30, 24, kText, "Playlists");
	char count[32];
	snprintf(count, sizeof(count), "%d", (int)g_sources.size());
	gfx::text(lx + lw - 30 - gfx::textWidth(22, count), top - 28, 22, kMuted, count);

	float y = top + 14;
	if (!g_notice.empty())
	{
		gfx::panel(lx + 14, y, lw - 28, 52, 12, 1, kLive, kLiveBack);
		gfx::text(lx + 34, y + 14, 22, kText, g_notice, lw - 70);
		y += 64;
	}
	if (g_sources.empty())
	{
		centredIn(lx, lw, y + 180, 30, kMuted, "No playlists yet");
		centredIn(lx, lw, y + 230, 22, kMuted, "Add one with the buttons on the right");
	}
	keepVisible(g_mgrSel, g_mgrTop, (int)g_sources.size(), kMgrRows);
	for (int row = 0; row < kMgrRows; row++)
	{
		const int i = g_mgrTop + row;
		if (i >= (int)g_sources.size())
			break;
		const Source& source = g_sources[(size_t)i];
		const float ry = y + (float)row * rowH;
		const bool selected = (!pointerOn() && !g_mgrOnButtons && i == g_mgrSel) || hovering(lx + 14, ry, lw - 28, rowH - 8);
		if (selected)
			gfx::panel(lx + 14, ry, lw - 28, rowH - 8, 14, 2, kBorder, kSelected);
		gfx::panel(lx + 30, ry + 16, 56, 56, 12, 1, kTileLine, kTile);
		centredTight(lx + 30, 56, ry + 33, 19, kText, source.type == "xtream" ? "XC" : source.type == "stalker" ? "STB" : source.type == "url" ? "URL" : "M3U");
		gfx::text(lx + 106, ry + 12, 28, i == g_active ? kAccent : kText, source.name, lw - 440);
		gfx::text(lx + 106, ry + 50, 20, kMuted, std::string(sourceKind(source)) + "  \xC2\xB7  " + sourceDetail(source), lw - 440);
		if (g_mgrConfirm == i)
			gfx::text(lx + lw - 300, ry + 30, 22, kLive, "\xE2\x96\xA1 again to remove");
		else if (i == g_active && g_haveBrowse)
		{
			gfx::panel(lx + lw - 150, ry + 28, 104, 30, 15, 1, kBorder, kSelected);
			centredTight(lx + lw - 150, 104, ry + 33, 18, kAccent, "OPEN");
		}
		clickable(lx + 14, ry, lw - 28, rowH - 8, [i] { g_mgrOnButtons = false; g_mgrSel = i; startLoading(i); });
		if (source.type != "file" && g_mgrConfirm != i)
		{
			// an Edit button on each row, for the pointer
			const float ex = lx + lw - 290, ew = 110;
			const bool editHover = hovering(ex, ry + 22, ew, 42);
			gfx::panel(ex, ry + 22, ew, 42, 12, editHover ? 2.0f : 1.0f, editHover ? kBorder : kTileLine, kTile);
			centredTight(ex, ew, ry + 32, 20, editHover ? kAccent : kText, "Edit");
			clickable(ex, ry + 22, ew, 42, [i] { editSource(i); });
		}
	}

	// actions
	const float bx = 1250, bw = 590;
	for (int b = 0; b < kMgrButtons; b++)
	{
		const float by = top - 44 + (float)b * 88;
		const bool selected = (!pointerOn() && g_mgrOnButtons && g_mgrButton == b) || hovering(bx, by, bw, 74);
		gfx::panel(bx, by, bw, 74, 16, 2, selected ? kBorder : kLine, selected ? kSelected : kChip);
		gfx::text(bx + 30, by + 22, 26, selected ? kAccent : kText, kMgrButtonLabel[b]);
		clickable(bx, by, bw, 74, [b] { managerButton(b); });
	}
	const float iy = top - 44 + (float)kMgrButtons * 88 + 10;
	gfx::panel(bx, iy, bw, 252, 16, 1, kLine, kCard);
	gfx::text(bx + 26, iy + 20, 23, kText, "M3U files from your computer");
	gfx::text(bx + 26, iy + 58, 20, kMuted, "Upload .m3u or .m3u8 files by FTP into:");
	gfx::text(bx + 26, iy + 88, 20, kAccent, g_dataDir + "/playlists", bw - 52);
	gfx::text(bx + 26, iy + 118, 20, kMuted, "They appear in this list at the next start.");
	gfx::text(bx + 26, iy + 166, 23, kText, "Automatic updates");
	gfx::text(bx + 26, iy + 204, 20, kMuted, "A playlist is fetched fresh each time it is opened.", bw - 52);

	gfx::text(bx, 1040, 20, kMuted, g_haveBrowse ? "\xE2\x9C\x95 Open    \xE2\x96\xB3 Edit    \xE2\x96\xA1 Remove    \xE2\x97\x8B / OPTIONS Back" : "\xE2\x9C\x95 Open    \xE2\x96\xB3 Edit    \xE2\x96\xA1 Remove", bw);
}

static void managerInput(Action action)
{
	const int count = (int)g_sources.size();
	if (action != A_FAVOURITE)
		g_mgrConfirm = -1;
	if (action == A_PAGE_UP || action == A_PAGE_DOWN)
	{
		g_mgrTab = (g_mgrTab + (action == A_PAGE_UP ? kMgrTabs - 1 : 1)) % kMgrTabs;
		return;
	}
	if (g_mgrTab == 2)
	{
		groupsTabInput(action);
		return;
	}
	if (g_mgrTab == 3)
	{
		pointerTabInput(action);
		return;
	}
	if (g_mgrTab == 4)
	{
		playbackTabInput(action);
		return;
	}
	if (g_mgrTab == 5)
	{
		phoneTabInput(action);
		return;
	}
	if (g_mgrTab == 6)
	{
		profilesTabInput(action);
		return;
	}
	if (g_mgrTab == 1)
	{
		switch (action)
		{
		case A_UP:
			if (g_themeOnLayout) g_layoutSel = 1 - g_layoutSel;
			else g_themeSel = (g_themeSel + kThemeCount - 1) % kThemeCount;
			break;
		case A_DOWN:
			if (g_themeOnLayout) g_layoutSel = 1 - g_layoutSel;
			else g_themeSel = (g_themeSel + 1) % kThemeCount;
			break;
		case A_RIGHT: g_themeOnLayout = true; g_layoutSel = g_layout; break;
		case A_LEFT: g_themeOnLayout = false; break;
		case A_OK:
			if (g_themeOnLayout)
			{
				g_layout = g_layoutSel;
				saveSettings();
				applyLook();
			}
			else
				chooseTheme(g_themeSel);
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
		return;
	}
	switch (action)
	{
	case A_UP:
	case A_DOWN:
		if (g_mgrOnButtons)
			g_mgrButton = (g_mgrButton + (action == A_UP ? kMgrButtons - 1 : 1)) % kMgrButtons;
		else
			moveSelection(g_mgrSel, count, action == A_UP ? -1 : 1);
		break;
	case A_RIGHT: g_mgrOnButtons = true; break;
	case A_LEFT: if (count > 0) g_mgrOnButtons = false; break;
	case A_OK:
		if (g_mgrOnButtons)
			managerButton(g_mgrButton);
		else if (count > 0)
			startLoading(g_mgrSel);
		break;
	case A_SEARCH: if (!g_mgrOnButtons && count > 0) editSource(g_mgrSel); break;
	case A_FAVOURITE:
		if (g_mgrOnButtons || count == 0)
			break;
		if (g_mgrConfirm == g_mgrSel)
			deleteSource(g_mgrSel);
		else
			g_mgrConfirm = g_mgrSel;
		break;
	case A_BACK:
	case A_MENU:                                 // Options again closes settings
		if (g_haveBrowse)
			g_screen = BROWSE;
		break;
	default: break;
	}
}

