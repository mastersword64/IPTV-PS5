// ---- the TV layout: a title's own page -----------------------------------------------------------

static float g_tvEpisodeSlide = 0;

static void drawTvDetails()
{
	if (g_detailItem < 0 || g_detailItem >= (int)g_playlist.channels.size())
	{
		g_detail = false;
		return;
	}
	{
		std::lock_guard<std::mutex> lock(g_detailMutex);
		if (g_detailArrived)
		{
			g_detailArrived = false;
			Channel& item = g_playlist.channels[(size_t)g_detailItem];
			if (item.url == g_detailFound.url)
			{
				item.plot = g_detailFound.plot; item.year = g_detailFound.year; item.rating = g_detailFound.rating;
				item.genres = g_detailFound.genres; item.length = g_detailFound.length; item.cast = g_detailFound.cast;
				if (item.logo.empty()) item.logo = g_detailFound.logo;
			}
		}
	}
	const float keepScale = gfx::radiusScale();
	gfx::setRadiusScale(1.0f);
	const Channel& item = g_playlist.channels[(size_t)g_detailItem];
	const bool series = isSeriesItem(item.url);
	const int episodes = series && T->drillState == 2 ? (int)T->drillList.size() : 0;
	tvBackdrop(item, series ? 330.0f : 520.0f, 0.9f);
	clickable(0, 0, 1920, 1080, [] {});

	const float x0 = 120;
	const bool backHover = hovering(x0, 44, 140, 46);
	gfx::roundRect(x0, 44, 140, 46, 23, backHover ? kTvWhite : 0xffffff26);
	centredTight(x0, 140, 54, 21, backHover ? kTvInk : kTvWhite, "\xE2\x80\xB9  Back");
	clickable(x0, 44, 140, 46, [] { closeDetails(); });

	{
		// the poster, at a size where it is sharp
		const int poster = item.logo.empty() ? -1 : logoImage(item.logo, true);
		int pw = 0, ph = 0;
		if (poster >= 0 && gfx::imageSize(poster, pw, ph) && pw > 0 && ph > 0)
		{
			const float h = series ? 400.0f : 470.0f, w = h * (float)pw / (float)ph;
			const float px = 1800 - w, py = 110;
			gfx::roundRect(px - 8, py - 8, w + 16, h + 16, 24, 0x00000050);
			gfx::picture(poster, px, py, w, h, 16, 0, 0, 1, 1, artFade(poster));
		}
	}
	char kicker[64];
	if (series && episodes > 0)
		snprintf(kicker, sizeof(kicker), "SERIES   \xC2\xB7   %d EPISODES", episodes);
	else
		snprintf(kicker, sizeof(kicker), "%s", series ? "SERIES" : "MOVIE");
	gfx::bold(true);
	gfx::text(x0, 132, 18, kTvSoft, kicker);
	gfx::text(x0, 164, 78, kTvWhite, item.name, 1300);
	gfx::bold(false);
	tvFacts(item, x0, 276, 900);
	float y = drawWrapped(x0, 332, 1000, 23, kTvSoft, item.plot.empty() ? "No description is available for this title." : item.plot, series ? 2 : 5);
	if (!item.cast.empty())
		y = drawWrapped(x0, y + 8, 1000, 19, kTvDim, "With " + item.cast, 1);

	// buttons
	const auto resume = T->resume.find(item.url);
	const double resumeAt = !series && resume != T->resume.end() ? resume->second.first : 0;
	const std::vector<std::string> buttons = detailButtons(item, series, resumeAt);
	if (g_detailFocus > (int)buttons.size() - 1)
		g_detailFocus = (int)buttons.size() - 1;
	const float by = series ? 452.0f : (y + 30 > 560 ? y + 30 : 560.0f);
	float bx = x0;
	gfx::bold(true);
	for (int b = 0; b < (int)buttons.size(); b++)
	{
		const float bw = gfx::textWidth(22, buttons[(size_t)b]) + 68;
		const bool focused = (!pointerOn() && !g_detailOnEpisodes && g_detailFocus == b && g_askChannel < 0) || hovering(bx, by, bw, 66);
		const bool primary = b == 0 && !series;
		if (focused)
			gfx::roundRect(bx - 8, by - 8, bw + 16, 82, 41, 0xffffff40);
		gfx::roundRect(bx, by, bw, 66, 33, focused ? kTvWhite : primary ? 0xffffffd8 : 0xffffff2c);
		centredTight(bx, bw, by + 20, 22, focused || primary ? kTvInk : kTvWhite, buttons[(size_t)b]);
		clickable(bx, by, bw, 66, [b] { g_detailOnEpisodes = false; g_detailFocus = b; pressDetailButton(b); });
		bx += bw + 18;
	}
	gfx::bold(false);
	if (resumeAt > 0 && resume->second.second > 0)
	{
		gfx::roundRect(x0, by + 84, 460, 6, 3, 0xffffff40);
		gfx::roundRect(x0, by + 84, 460 * (float)(resumeAt / resume->second.second), 6, 3, kTvWhite);
	}

	// a series: its episodes, as a row of cards
	if (series)
	{
		const float ry = 580, cardW = 300, cardH = 169, cell = cardW + 24;
		gfx::bold(true);
		gfx::text(x0, ry, 26, kTvWhite, "Episodes");
		gfx::bold(false);
		if (T->drillState == 1)
		{
			spinner(x0 + 30, ry + 130, 20, kTvWhite);
			gfx::text(x0 + 74, ry + 116, 22, kTvDim, "Loading episodes...");
		}
		else if (T->drillState == 3)
			gfx::text(x0, ry + 110, 22, kTvSoft, "The episodes could not be loaded:  " + T->drillError, 1500);
		else
		{
			if (g_episodeSel > episodes - 1) g_episodeSel = episodes > 0 ? episodes - 1 : 0;
			const int visible = (int)((1920 - x0 - 30) / cell);
			float target = g_tvEpisodeSlide;
			if ((float)g_episodeSel < target) target = (float)g_episodeSel;
			if ((float)g_episodeSel > target + (float)visible - 1) target = (float)g_episodeSel - (float)visible + 1;
			glide(g_tvEpisodeSlide, (float)(int)(target + 0.5f));
			for (int pass = 0; pass < 2; pass++)
				for (int i = (int)g_tvEpisodeSlide - 1; i < (int)g_tvEpisodeSlide + visible + 2 && i < episodes; i++)
				{
					if (i < 0)
						continue;
					const Channel& episode = g_playlist.channels[(size_t)T->drillList[(size_t)i]];
					const float cx = x0 + ((float)i - g_tvEpisodeSlide) * cell, cy = ry + 52;
					const bool hot = (!pointerOn() && g_detailOnEpisodes && i == g_episodeSel && g_askChannel < 0) || hovering(cx, cy, cardW, cardH);
					if ((pass == 1) != hot)
						continue;
					const float pop = hot ? 1.06f : 1.0f, w = cardW * pop, h = cardH * pop, x = cx - (w - cardW) / 2, yy = cy - (h - cardH) / 2;
					if (hot)
						gfx::roundRect(x - 10, yy - 10, w + 20, h + 20, 24, 0xffffff38);
					// the series' own artwork, a different part of it for each episode, behind the number
					const int art = item.logo.empty() ? -1 : logoImage(item.logo, true);
					int aw = 0, ah = 0;
					if (art >= 0 && gfx::imageSize(art, aw, ah) && aw > 0 && ah > 0)
					{
						float u0, v0, u1, v1;
						coverCrop(aw, ah, w, h, u0, v0, u1, v1);
						const float shift = (1 - (v1 - v0)) * (float)(i % 5) / 5.0f - v0;
						gfx::picture(art, x, yy, w, h, 14, u0, v0 + shift, u1, v1 + shift, 0.75f);
					}
					else
						gfx::roundRect(x, yy, w, h, 14, (tvTint(item.name) & 0xffffff00) | 0xc0);
					gfx::roundRect(x, yy + h * 0.5f, w, h * 0.5f, 14, 0x00000060);
					gfx::bold(true);
					char number[16];
					snprintf(number, sizeof(number), "%d", i + 1);
					gfx::text(x + 18, yy + h - 62, 44, 0xffffffe6, number);
					gfx::bold(false);
					const auto left = T->resume.find(episode.url);
					if (left != T->resume.end() && left->second.second > 0)
					{
						gfx::roundRect(x + 12, yy + h - 16, w - 24, 6, 3, 0x000000a8);
						gfx::roundRect(x + 12, yy + h - 16, (w - 24) * (float)(left->second.first / left->second.second), 6, 3, kTvWhite);
					}
					const bool seen = T->watched.count(episode.url) != 0;
					if (seen)
					{
						gfx::roundRect(x + w - 46, yy + 10, 36, 36, 18, 0x000000a8);
						centredTight(x + w - 46, 36, yy + 14, 23, 0x7be0a0ff, "\xE2\x9C\x93");
					}
					if (hot)
						tvFocusAt(x, yy, w, h);
					gfx::bold(hot);
					gfx::text(cx, cy + cardH + (hot ? 20.0f : 12.0f), 19, hot ? kTvWhite : kTvSoft, episode.name, cardW);
					gfx::bold(false);
					if (seen)
						gfx::text(cx, cy + cardH + (hot ? 48.0f : 40.0f), 16, kTvDim, "\xE2\x9C\x93 watched");
					clickable(cx, cy, cardW, cardH + 40, [i] { g_detailOnEpisodes = true; g_episodeSel = i; playEpisode(i); });
				}
		}
	}
	tvDrawFocus();
	gfx::setRadiusScale(keepScale);
}

static void tvDetailsInput(Action action)
{
	const Channel& item = g_playlist.channels[(size_t)g_detailItem];
	const bool series = isSeriesItem(item.url);
	const auto resume = T->resume.find(item.url);
	const int buttons = (int)detailButtons(item, series, !series && resume != T->resume.end() ? resume->second.first : 0).size();
	const int episodes = series && T->drillState == 2 ? (int)T->drillList.size() : 0;
	if (g_detailOnEpisodes && episodes == 0)
		g_detailOnEpisodes = false;
	switch (action)
	{
	case A_BACK: closeDetails(); break;
	case A_MENU: openManager(""); break;
	case A_LEFT:
		if (g_detailOnEpisodes) { if (g_episodeSel > 0) g_episodeSel--; }
		else if (g_detailFocus > 0) g_detailFocus--;
		break;
	case A_RIGHT:
		if (g_detailOnEpisodes) { if (g_episodeSel < episodes - 1) g_episodeSel++; }
		else if (g_detailFocus < buttons - 1) g_detailFocus++;
		break;
	case A_DOWN: if (episodes > 0) g_detailOnEpisodes = true; break;
	case A_UP: g_detailOnEpisodes = false; break;
	case A_PAGE_UP: if (g_detailOnEpisodes) g_episodeSel = g_episodeSel > 5 ? g_episodeSel - 5 : 0; break;
	case A_PAGE_DOWN: if (g_detailOnEpisodes) g_episodeSel = g_episodeSel + 5 < episodes ? g_episodeSel + 5 : episodes - 1; break;
	case A_OK:
		if (g_detailOnEpisodes)
			playEpisode(g_episodeSel);
		else
			pressDetailButton(g_detailFocus);
		break;
	case A_FAVOURITE: toggleFavourite(g_detailItem); break;
	default: break;
	}
}

// ---- the TV layout: Account, and Home before there is anything to put on it --------------------------

static void drawTvAccount()
{
	const float keepScale = gfx::radiusScale();
	gfx::setRadiusScale(1.0f);
	gfx::rect(0, 0, 1920, 1080, kTvBack);
	gfx::roundRect(1180, -260, 1100, 1100, 550, 0xffffff08);
	const float x0 = 150;
	gfx::bold(true);
	gfx::text(x0, 60, 44, kTvWhite, "Account");
	gfx::bold(false);
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
	float y = 160;
	if (state <= 1)
	{
		spinner(x0 + 20, y + 20, 16, kTvWhite);
		gfx::text(x0 + 56, y + 8, 23, kTvDim, "Asking the server...");
	}
	for (const auto& row : rows)
	{
		if (y > 1000)
			break;
		gfx::roundRect(x0, y, 1180, 58, 16, 0xffffff12);
		gfx::text(x0 + 28, y + 17, 21, kTvDim, row.first, 300);
		gfx::bold(true);
		gfx::text(x0 + 360, y + 15, 24, kTvWhite, row.second, 780);
		gfx::bold(false);
		y += 68;
	}
	if (state == 3 && !error.empty())
		gfx::text(x0, y + 10, 21, kTvSoft, "The server could not be asked for more: " + error, 1500);
	const std::string clock = clockNow();
	gfx::text(1860 - gfx::textWidth(26, clock), 50, 26, kTvSoft, clock);
	drawTvRail();
	gfx::setRadiusScale(keepScale);
}

static void drawTvWelcome()
{
	const float keepScale = gfx::radiusScale();
	gfx::setRadiusScale(1.0f);
	gfx::rect(0, 0, 1920, 1080, kTvBack);
	gfx::roundRect(1180, -260, 1100, 1100, 550, 0xffffff08);
	gfx::bold(true);
	centredIn(92, 1828, 400, 56, kTvWhite, "Welcome");
	gfx::bold(false);
	centredIn(92, 1828, 486, 24, kTvSoft, "Your favorites and the things you are part-way through will appear here.");
	centredIn(92, 1828, 530, 22, kTvDim, "Press Left for Live TV, Movies and Series.");
	drawTvRail();
	gfx::setRadiusScale(keepScale);
}

// For the TV screens that only have the rail to move to.
static void tvPlainInput(Action action)
{
	if (tvRailInput(action))
		return;
	if (action == A_LEFT || action == A_BACK || action == A_OK)
		tvOpenRail();
}

