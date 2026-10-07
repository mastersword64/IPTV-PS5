// ---- the TV layout: Live TV -----------------------------------------------------------------
// The channel plays across the whole screen; the list floats over its left side. Chips along the
// top narrow things down (favourites, recent, a country); the list is the groups, then the
// channels of the group chosen. Resting on a channel shows it.

static bool g_tvOnChips = false;
static int g_tvChip = 0;
static float g_tvChipScroll = 0;
static std::vector<std::string> g_tvCountries;         // the countries that have groups on show
static size_t g_tvCountriesKey = (size_t)-1;

static void tvBuildCountries()
{
	const size_t key = g_playlist.groups.size() * 131 + T->hidden.size();
	if (key == g_tvCountriesKey)
		return;
	g_tvCountriesKey = key;
	g_tvCountries.clear();
	bool other = false;
	for (const std::string& group : g_playlist.groups)
	{
		if (T->hidden.count(group))
			continue;
		const std::string country = countryOf(group);
		if (country == "Other")
			other = true;
		else if (std::find(g_tvCountries.begin(), g_tvCountries.end(), country) == g_tvCountries.end())
			g_tvCountries.push_back(country);
	}
	std::sort(g_tvCountries.begin(), g_tvCountries.end());
	if (other && !g_tvCountries.empty())
		g_tvCountries.push_back("Other");
}

// The chips, left to right: favourites, recent, all groups, then a chip per country.
static std::vector<std::string> tvChips()
{
	std::vector<std::string> chips = { "\xE2\x96\xA4  Guide", "\xE2\x98\x85  Favorites", "Recent", g_playlist.groups.empty() ? "All channels" : "All groups" };
	chips.insert(chips.end(), g_tvCountries.begin(), g_tvCountries.end());
	return chips;
}

static int tvCurrentChip()
{
	// (chip 0 is the Guide button, which is never "the one in use")
	if (g_section == S_FAVOURITES) return 1;
	if (g_section == S_RECENT) return 2;
	if (!g_tvCountry.empty())
		for (int i = 0; i < (int)g_tvCountries.size(); i++)
			if (g_tvCountries[(size_t)i] == g_tvCountry)
				return 4 + i;
	return 3;
}

static void openGrid();

static void tvPressChip(int chip)
{
	g_tvOnChips = false;
	if (chip == 0)
	{
		openGrid();
		return;
	}
	chip--;                                             // the rest are numbered as before the Guide chip existed
	if (chip == 0 || chip == 1)
	{
		g_tvCountry.clear();
		chooseSection(chip == 0 ? S_FAVOURITES : S_RECENT);
		g_focus = F_CHANNELS;
		return;
	}
	g_tvCountry = chip >= 3 && chip - 3 < (int)g_tvCountries.size() ? g_tvCountries[(size_t)chip - 3] : std::string();
	const bool groups = !g_playlist.groups.empty();
	if ((int)g_section != (groups ? S_GROUPS : S_ALL))
		chooseSection(groups ? S_GROUPS : S_ALL);
	computeCats();
	g_catTop = 0;
	T->catScroll = 0;
	rebuildView(g_playing);
	g_focus = groups ? F_CATEGORIES : F_CHANNELS;
}

static void drawTvLive(const std::string& status)
{
	const float keepScale = gfx::radiusScale();
	gfx::setRadiusScale(1.0f);
	tvBuildCountries();

	// the picture, everywhere
	gfx::rect(0, 0, 1920, 1080, kTvBack);
	const bool playing = g_nowOn && g_nowTab == 0;
	if (playing)
		gfx::videoDraw(0, 0, 1920, 1080, g_pictureFit);
	else
	{
		gfx::roundRect(1100, -300, 1200, 1200, 600, 0xffffff0a);
		gfx::bold(true);
		centredIn(700, 1220, 470, 34, kTvSoft, "Choose a channel");
		gfx::bold(false);
		if (g_nowOn && g_nowTab == kRadioTab)
			centredIn(700, 1220, 524, 22, kTvDim, "Radio is playing (" + g_now.name + ").  Press \xE2\x9C\x95 on a channel to watch it instead");
		else
			centredIn(700, 1220, 524, 22, kTvDim, g_autoPreview ? "Rest on one for a moment and it starts playing here" : "Press \xE2\x9C\x95 on a channel to play it here");
	}
	// shading under the list, and along the bottom for the programme card
	for (int i = 0; i < 50; i++)
	{
		const float t = 1.0f - (float)i / 49.0f;
		gfx::rect(92 + 600 + (float)i * 12, 0, 12, 1080, (kTvBack & 0xffffff00) | (uint32_t)(215.0f * t * t));
	}
	const float px = 92, pw = 600;
	gfx::rect(px, 0, pw, 1080, 0x080a12d8);
	if (!status.empty() && playing)
	{
		const float w = gfx::textWidth(30, status) + 80;
		gfx::roundRect(1300 - w / 2, 480, w, 72, 36, 0x000000b8);
		centredTight(1300 - w / 2, w, 500, 30, kTvWhite, status);
	}
	const std::string clock = clockNow();
	gfx::text(1860 - gfx::textWidth(26, clock), 50, 26, kTvSoft, clock);

	// heading and chips
	gfx::bold(true);
	gfx::text(px + 36, 40, 36, kTvWhite, "Live TV");
	gfx::bold(false);
	const std::vector<std::string> chips = tvChips();
	const int current = tvCurrentChip();
	if (g_tvChip > (int)chips.size() - 1)
		g_tvChip = (int)chips.size() - 1;
	std::vector<float> chipX(chips.size() + 1);
	float total = 0;
	for (size_t i = 0; i < chips.size(); i++)
	{
		chipX[i] = total;
		total += gfx::textWidth(19, chips[i]) + 30 + 10;
	}
	chipX[chips.size()] = total;
	{
		// the row slides so the chip in use stays in view
		const int watch = g_tvOnChips ? g_tvChip : current;
		float want = g_tvChipScroll;
		const float room = pw - 60;
		if (chipX[(size_t)watch] < want) want = chipX[(size_t)watch];
		if (chipX[(size_t)watch + 1] > want + room) want = chipX[(size_t)watch + 1] - room;
		glide(g_tvChipScroll, want < 0 ? 0 : want, 16.0f);
	}
	gfx::clip(px + 20, 96, pw - 30, 54);
	for (int i = 0; i < (int)chips.size(); i++)
	{
		const float x = px + 36 + chipX[(size_t)i] - g_tvChipScroll, w = chipX[(size_t)i + 1] - chipX[(size_t)i] - 10;
		const bool focused = (g_tvOnChips && !pointerOn() && !g_tvOnRail && i == g_tvChip) || hovering(x, 104, w, 38);
		const bool on = i == current;
		gfx::roundRect(x, 104, w, 38, 19, focused ? kTvWhite : on ? 0xffffffd0 : 0xffffff26);
		gfx::bold(on || focused);
		gfx::text(x + 15, 113, 19, focused || on ? kTvInk : kTvWhite, chips[(size_t)i]);
		gfx::bold(false);
		clickable(x, 104, w, 38, [i] { g_tvChip = i; tvPressChip(i); });
	}
	gfx::unclip();

	const bool groupList = g_section == S_GROUPS && g_focus != F_CHANNELS && !g_playlist.groups.empty();
	const float listTop = 216;
	if (groupList)
	{
		// the groups (of the country chosen, if one is)
		gfx::text(px + 36, 170, 21, kTvDim, g_tvCountry.empty() ? "Groups" : g_tvCountry + " groups");
		const int count = (int)T->cats.size(), rows = 12;
		const float rowH = 70;
		const int place = catPlace();
		keepVisible(place, g_catTop, count, rows);
		if (T->catScroll - (float)g_catTop > 30 || (float)g_catTop - T->catScroll > 30)
			T->catScroll = (float)g_catTop;
		glide(T->catScroll, (float)g_catTop, 20.0f);
		if (count == 0)
			centredIn(px, pw, 420, 22, kTvDim, T->hidden.empty() ? "No groups" : "Every group here is hidden");
		gfx::clip(px, listTop - 4, pw, (float)rows * rowH + 8);
		for (int at = (int)T->catScroll; at < (int)T->catScroll + rows + 1 && at < count; at++)
		{
			const int i = T->cats[(size_t)at];
			const float y = listTop + ((float)at - T->catScroll) * rowH;
			const bool focused = (i == g_catSel && g_focus == F_CATEGORIES && !pointerOn() && !g_tvOnChips && !g_tvOnRail) || hovering(px + 20, y, pw - 40, rowH - 8);
			if (focused)
			{
				gfx::roundRect(px + 14, y - 6, pw - 28, rowH + 4, 22, 0xffffff30);
				gfx::roundRect(px + 20, y, pw - 40, rowH - 8, 18, kTvWhite);
			}
			else if (i == g_catSel)
				gfx::roundRect(px + 20, y, pw - 40, rowH - 8, 18, 0xffffff1e);
			char number[16];
			const char state = g_groupState[(size_t)i];
			snprintf(number, sizeof(number), state == 2 ? "%d" : state == 1 ? "..." : "", (int)g_groupChannels[(size_t)i + 1].size());
			const float nw = gfx::textWidth(19, number);
			gfx::text(px + pw - 44 - nw, y + 21, 19, focused ? 0x0e121ea0 : kTvDim, number);
			gfx::bold(focused);
			gfx::text(px + 44, y + 17, 24, focused ? kTvInk : kTvWhite, g_playlist.groups[(size_t)i], pw - 120 - nw);
			gfx::bold(false);
			clickable(px + 20, y, pw - 40, rowH - 8, [i] {
				g_tvOnChips = g_tvOnRail = false;
				const bool same = i == g_catSel;
				selectCategory(i);
				g_focus = same ? F_CHANNELS : F_CATEGORIES;
			});
		}
		gfx::unclip();
	}
	else
	{
		// the channels
		std::string heading = g_section == S_FAVOURITES ? "Favorites" : g_section == S_RECENT ? "Recently watched" : inGroups() ? "\xE2\x80\xB9  " + g_playlist.groups[(size_t)g_catSel] : "All channels";
		if (!g_filter.empty() && !T->filterCats)
			heading += "   \xC2\xB7   \"" + g_filter + "\"";
		gfx::text(px + 36, 170, 21, kTvDim, heading, pw - 72);
		if (inGroups())
			clickable(px + 20, 160, pw - 40, 44, [] { g_focus = F_CATEGORIES; g_tvOnChips = false; });
		const int count = (int)g_view.size(), rows = 8;
		const float rowH = 106;
		const char categoryState = inGroups() ? g_groupState[(size_t)g_catSel] : 2;
		if (count == 0)
		{
			if (categoryState == 1 || categoryState == 0)
			{
				spinner(px + pw / 2, 400, 24, kTvWhite);
				centredIn(px, pw, 446, 22, kTvDim, "Loading channels...");
			}
			else if (categoryState == 3)
			{
				centredIn(px, pw, 400, 24, kTvWhite, "This group could not be loaded");
				centredIn(px, pw, 440, 19, kTvDim, g_genreFailure);
				centredIn(px, pw, 480, 20, kTvDim, "Press \xE2\x9C\x95 to try again");
			}
			else
				centredIn(px, pw, 420, 22, kTvDim, g_section == S_FAVOURITES ? "No favorites yet (\xE2\x96\xA1 on a channel adds one)" : g_section == S_RECENT ? "Nothing watched yet" : "No channels here");
		}
		keepVisible(g_sel, g_top, count, rows);
		if (T->listScroll - (float)g_top > 24 || (float)g_top - T->listScroll > 24)
			T->listScroll = (float)g_top;
		glide(T->listScroll, (float)g_top, 20.0f);
		const long long clockSeconds = (long long)time(nullptr);
		gfx::clip(px, listTop - 8, pw, (float)rows * rowH + 16);
		for (int i = (int)T->listScroll; i < (int)T->listScroll + rows + 1 && i < count; i++)
		{
			const int channel = g_view[(size_t)i];
			const Channel& info = g_playlist.channels[(size_t)channel];
			const float y = listTop + ((float)i - T->listScroll) * rowH;
			const bool focused = (i == g_sel && g_focus == F_CHANNELS && !pointerOn() && !g_tvOnChips && !g_tvOnRail) || hovering(px + 20, y, pw - 40, rowH - 10);
			const bool onAir = channel == g_playing;
			if (focused)
			{
				gfx::roundRect(px + 14, y - 6, pw - 28, rowH + 2, 24, 0xffffff30);
				gfx::roundRect(px + 20, y, pw - 40, rowH - 10, 20, kTvWhite);
			}
			else if (onAir)
				gfx::roundRect(px + 20, y, pw - 40, rowH - 10, 20, 0xffffff1e);
			const uint32_t ink = focused ? kTvInk : kTvWhite, faint = focused ? 0x0e121eb0 : kTvDim;
			// logo, or the channel's colour and initials
			gfx::roundRect(px + 36, y + 14, 68, 68, 14, focused ? 0x0e121e20 : 0xffffff1c);
			const int logo = info.logo.empty() ? -1 : logoImage(info.logo);
			int lw = 0, lh = 0;
			if (logo >= 0 && gfx::imageSize(logo, lw, lh) && lw > 0 && lh > 0)
			{
				const float fit = 58.0f / (float)(lw > lh ? lw : lh);
				gfx::image(logo, px + 70 - (float)lw * fit / 2, y + 48 - (float)lh * fit / 2, (float)lw * fit, (float)lh * fit);
			}
			else
			{
				gfx::roundRect(px + 36, y + 14, 68, 68, 14, (tvTint(info.name) & 0xffffff00) | (focused ? 0xd0 : 0x90));
				gfx::bold(true);
				centredTight(px + 36, 68, y + 34, 24, kTvWhite, initials(info.name));
				gfx::bold(false);
			}
			char number[16];
			snprintf(number, sizeof(number), "%d", i + 1);
			const float nw = gfx::textWidth(17, number);
			gfx::text(px + pw - 44 - nw, y + 18, 17, faint, number);
			gfx::bold(true);
			gfx::text(px + 122, y + 12, 23, ink, info.name, pw - 190 - nw);
			gfx::bold(false);
			const Programme* now = onNow(info.url);
			if (onAir && !now)
			{
				gfx::roundRect(px + 122, y + 48, 60, 24, 12, 0xe23c46ff);
				gfx::bold(true);
				centredTight(px + 122, 60, y + 52, 14, kTvWhite, "LIVE");
				gfx::bold(false);
				gfx::text(px + 194, y + 49, 17, faint, info.group, pw - 260);
			}
			else
				gfx::text(px + 122, y + 46, 17, faint, now ? now->title : info.group, pw - 190);
			if (now && now->stop > now->start)
			{
				const float done = (float)(clockSeconds - now->start) / (float)(now->stop - now->start);
				gfx::roundRect(px + 122, y + 76, pw - 190, 4, 2, focused ? 0x0e121e30 : 0xffffff30);
				gfx::roundRect(px + 122, y + 76, (pw - 190) * (done < 0 ? 0 : done > 1 ? 1 : done), 4, 2, onAir || focused ? 0xe23c46ff : 0xffffffa0);
			}
			if (g_isFavourite[(size_t)channel])
				gfx::text(px + pw - 62, y + 48, 20, focused ? 0xc08a00ff : 0xffd75eff, "\xE2\x98\x85");
			clickable(px + 20, y, pw - 40, rowH - 10, [i] {
				g_tvOnChips = g_tvOnRail = false;
				g_focus = F_CHANNELS;
				g_sel = i;
				activateSelected();
			});
		}
		gfx::unclip();
	}

	// what is on: the channel playing (or the one in focus, before anything plays)
	const int focusChannel = g_focus == F_CHANNELS ? selectedChannel() : -1;
	if (playing || focusChannel >= 0)
	{
		const Channel& shown = playing ? g_now : g_playlist.channels[(size_t)focusChannel];
		const GuideEntry* guide = guideFor(shown);
		const long long clockSeconds = (long long)time(nullptr);
		const Programme* current = nullptr;
		const Programme* next[2] = { nullptr, nullptr };
		if (guide)
			for (const Programme& programme : guide->items)
			{
				if (programme.stop > 0 && programme.stop <= clockSeconds)
					continue;
				if (!current && programme.start <= clockSeconds)
					current = &programme;
				else if (!next[0])
					next[0] = &programme;
				else if (!next[1])
					next[1] = &programme;
			}
		const float cx = 740, cy = 800, cw = 1130, ch = 232;
		gfx::roundRect(cx, cy, cw, ch, 26, 0x080a12b0);
		float tx = cx + 34;
		if (playing)
		{
			gfx::roundRect(tx, cy + 28, 72, 30, 15, 0xe23c46ff);
			gfx::bold(true);
			centredTight(tx, 72, cy + 34, 16, kTvWhite, "LIVE");
			gfx::bold(false);
			tx += 88;
		}
		gfx::text(tx, cy + 31, 21, kTvSoft, shown.name, cx + cw - 40 - tx);
		gfx::bold(true);
		gfx::text(cx + 34, cy + 72, 40, kTvWhite, current ? current->title : shown.name, cw - 68);
		gfx::bold(false);
		if (current && current->stop > current->start)
		{
			const std::string times = clockOfDay(*current, false) + " \xE2\x80\x93 " + clockOfDay(*current, true);
			const float tw = gfx::text(cx + 34, cy + 134, 20, kTvSoft, times);
			const float done = (float)(clockSeconds - current->start) / (float)(current->stop - current->start);
			gfx::roundRect(cx + 54 + tw, cy + 143, 560, 6, 3, 0xffffff40);
			gfx::roundRect(cx + 54 + tw, cy + 143, 560 * (done < 0 ? 0 : done > 1 ? 1 : done), 6, 3, 0xe23c46ff);
		}
		else if (!current)
			gfx::text(cx + 34, cy + 134, 20, kTvDim, guide && (guide->failed || !guide->items.empty()) ? shown.group : "Looking up the programme...", cw - 68);
		if (next[0])
		{
			std::string later = "Next   " + clockOfDay(*next[0], false) + "   " + next[0]->title;
			if (next[1])
				later += "        " + clockOfDay(*next[1], false) + "   " + next[1]->title;
			gfx::text(cx + 34, cy + 180, 19, kTvDim, later, cw - 68);
		}
	}
	gfx::text(px + 36, 1046, 17, kTvDim, "L1 / R1 Country   \xE2\x9C\x95 Play   \xE2\x96\xA1 Favorite   \xE2\x96\xB3 Filter   R3 More", pw - 60);

	// resting on a channel shows it
	static int restingOn = -1;
	static double restingSince = 0;
	const int resting = g_focus == F_CHANNELS && !g_tvOnChips && !g_tvOnRail && !g_keyboard && !g_more ? selectedChannel() : -1;
	if (resting != restingOn)
	{
		restingOn = resting;
		restingSince = nowSeconds();
	}
	else if (g_autoPreview && resting >= 0 && resting != g_playing && nowSeconds() - restingSince > 1.0 && !isSeriesItem(g_playlist.channels[(size_t)resting].url)
		&& !(g_nowOn && g_nowTab == kRadioTab))         // (a radio station playing is not cut off just by looking through the channels)
		startChannel(resting);

	drawTvRail();
	gfx::setRadiusScale(keepScale);
}

static bool tvRailInput(Action action)
{
	if (!g_tvOnRail)
		return false;
	switch (action)
	{
	case A_UP: g_tvRailSel = (g_tvRailSel + kTvRailCount - 1) % kTvRailCount; break;
	case A_DOWN: g_tvRailSel = (g_tvRailSel + 1) % kTvRailCount; break;
	case A_OK: tvRailGo(g_tvRailSel); break;
	case A_RIGHT:
	case A_BACK: g_tvOnRail = false; break;
	default: break;
	}
	return true;
}

static void tvOpenRail()
{
	g_tvOnRail = true;
	g_tvOnChips = false;
	for (int i = 0; i < kTvRailCount; i++)
		if (kTvRail[i].tab == g_tab)
			g_tvRailSel = i;
}

static void tvLiveInput(Action action)
{
	if (tvRailInput(action))
		return;
	if (action == A_SEARCH) { openFilter(); return; }
	if (action == A_MORE) { openMore(); return; }
	if (action == A_LAST) { lastChannel(); return; }
	if (g_focus == F_RAIL)
		g_focus = F_CHANNELS;                       // the classic rail has no place here
	if (action == A_PAGE_UP || action == A_PAGE_DOWN)
	{
		// L1 / R1: the chip before or after the one in use - in practice, the next country
		const int count = (int)tvChips().size() - 1;    // (not counting the Guide chip, which is not a list)
		const int chip = 1 + (tvCurrentChip() - 1 + count + (action == A_PAGE_UP ? -1 : 1)) % count;
		g_tvChip = chip;
		tvPressChip(chip);
		return;
	}

	if (g_tvOnChips)
	{
		const int count = (int)tvChips().size();
		switch (action)
		{
		case A_LEFT:
			if (g_tvChip > 0)
				g_tvChip--;
			else
				tvOpenRail();
			break;
		case A_RIGHT: if (g_tvChip < count - 1) g_tvChip++; break;
		case A_OK: tvPressChip(g_tvChip); break;
		case A_DOWN:
		case A_BACK: g_tvOnChips = false; break;
		default: break;
		}
		return;
	}

	const bool groupList = g_section == S_GROUPS && g_focus != F_CHANNELS && !g_playlist.groups.empty();
	if (groupList)
	{
		const int count = (int)T->cats.size();
		int place = catPlace();
		const int before = place;
		switch (action)
		{
		case A_UP:
			if (place == 0 || count == 0)
			{
				g_tvOnChips = true;
				g_tvChip = tvCurrentChip();
			}
			else
				place--;
			break;
		case A_DOWN: if (place < count - 1) place++; break;
		case A_RIGHT:
		case A_OK:
			if (count > 0)
			{
				if (g_groupState[(size_t)g_catSel] == 3)
					g_groupState[(size_t)g_catSel] = 0;        // try again
				g_focus = F_CHANNELS;
			}
			break;
		case A_FAVOURITE: if (count > 0) hideCategory(g_catSel); break;
		case A_LEFT:
		case A_BACK: tvOpenRail(); break;
		default: break;
		}
		if (count > 0 && place != before)
			selectCategory(T->cats[(size_t)place]);
		return;
	}

	const int count = (int)g_view.size();
	switch (action)
	{
	case A_UP:
		if (g_sel == 0 || count == 0)
		{
			g_tvOnChips = true;
			g_tvChip = tvCurrentChip();
		}
		else
			g_sel--;
		break;
	case A_DOWN: if (g_sel < count - 1) g_sel++; break;
	case A_LEFT:
	case A_BACK:
		if (g_section == S_GROUPS && !g_playlist.groups.empty())
			g_focus = F_CATEGORIES;
		else
			tvOpenRail();
		break;
	case A_OK:
		if (count == 0 && inGroups() && g_groupState[(size_t)g_catSel] == 3)
			g_groupState[(size_t)g_catSel] = 0;
		else
			activateSelected();
		break;
	case A_FAVOURITE: toggleFavourite(selectedChannel()); break;
	default: break;
	}
}

