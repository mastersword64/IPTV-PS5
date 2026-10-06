// ---- the guide grid: channels down the side, hours across ---------------------------------------
// Shows the channels of the list in use against a three-hour stretch of time. Programmes already
// shown that the provider can still play back (catch-up) are marked, and can be played from here.

static int g_gridRow = 0, g_gridTop = 0;
static long long g_gridTime = 0;                        // the moment in focus
static long long g_gridStart = 0;                       // the left edge of the stretch on screen
static float g_gridScroll = 0;
static const int kGridRows = 7;
static const long long kGridSpan = 3 * 3600;

static std::unordered_map<std::string, GuideEntry> g_gridGuide;      // the day's programmes, by channel address
static std::mutex g_gridMutex;
static bool g_gridBusy = false, g_gridArrived = false, g_gridOk = false;
static std::string g_gridFor;
static int g_gridRequest = 0;
static std::vector<Programme> g_gridItems;

// The time on the viewer's clock for a moment given in seconds since 1970.
static std::string clockAt(long long when)
{
	const time_t shifted = (time_t)when + (time_t)g_clockOffsetMinutes * 60;
	struct tm parts;
	if (when <= 0 || !gmtime_r(&shifted, &parts))
		return "";
	char text[16];
	snprintf(text, sizeof(text), "%d:%02d", parts.tm_hour, parts.tm_min);
	return text;
}

static bool sourceIsPlainPlaylist()
{
	return g_active >= 0 && (g_sources[(size_t)g_active].type == "url" || g_sources[(size_t)g_active].type == "file");
}

// A channel's programmes for the grid. One channel is asked for at a time; rows fill in as they arrive.
static const GuideEntry* gridGuide(const Channel& channel, bool mayAsk)
{
	if (g_gridGuideReset)
	{
		g_gridGuideReset = false;
		g_gridGuide.clear();
		std::lock_guard<std::mutex> lock(g_gridMutex);
		g_gridRequest++;
		g_gridBusy = g_gridArrived = false;
	}
	if (sourceIsPlainPlaylist())
		return guideFor(channel);                       // an M3U playlist's guide is all in memory already
	const double now = nowSeconds();
	{
		std::lock_guard<std::mutex> lock(g_gridMutex);
		if (g_gridArrived)
		{
			GuideEntry& entry = g_gridGuide[g_gridFor];
			entry.fetched = now;
			entry.failed = !g_gridOk;
			entry.items = std::move(g_gridItems);
			g_gridItems.clear();
			g_gridArrived = false;
			g_gridBusy = false;
		}
	}
	const auto found = g_gridGuide.find(channel.url);
	const bool fresh = found != g_gridGuide.end() && now - found->second.fetched < 900;
	if (!fresh && !g_gridBusy && mayAsk && g_active >= 0)
	{
		g_gridBusy = true;
		g_gridFor = channel.url;
		const int request = ++g_gridRequest;
		const Source source = g_sources[(size_t)g_active];
		const Channel asked = channel;
		startThread("guide-day", [source, asked, request] {
			std::vector<Programme> items;
			std::string error;
			const bool ok = fetchGuide(source, asked, items, error, true);
			std::lock_guard<std::mutex> lock(g_gridMutex);
			if (request != g_gridRequest)
				return;
			g_gridItems = std::move(items);
			g_gridOk = ok;
			g_gridArrived = true;
		});
	}
	return found != g_gridGuide.end() ? &found->second : nullptr;
}

static void openGrid()
{
	if (g_tab != 0 || T->state != 2)
		return;
	g_grid = true;
	g_gridTime = (long long)time(nullptr);
	g_gridStart = g_gridTime - g_gridTime % 1800 - 1800;
	g_gridRow = g_sel < (int)g_view.size() ? g_sel : 0;
	g_gridTop = g_gridRow > 2 ? g_gridRow - 2 : 0;
	g_gridScroll = (float)g_gridTop;
	g_focus = F_CHANNELS;
}

// The programme of a channel that covers a moment (or null).
static const Programme* gridProgrammeAt(const GuideEntry* guide, long long when)
{
	if (!guide)
		return nullptr;
	for (const Programme& programme : guide->items)
		if (programme.start <= when && when < programme.stop)
			return &programme;
	return nullptr;
}

static void gridKeepTimeInView()
{
	if (g_gridTime < g_gridStart + 600)
		g_gridStart = g_gridTime - g_gridTime % 1800 - 1800;
	if (g_gridTime > g_gridStart + kGridSpan - 600)
		g_gridStart = g_gridTime - g_gridTime % 1800 - kGridSpan + 3600;
}

static void gridPlay(int row)
{
	if (row < 0 || row >= (int)g_view.size())
		return;
	const int index = g_view[(size_t)row];
	const Channel channel = g_playlist.channels[(size_t)index];
	const long long now = (long long)time(nullptr);
	const Programme* programme = gridProgrammeAt(gridGuide(channel, false), g_gridTime);
	if (programme && programme->stop <= now)
	{
		// already shown: play it back if the provider keeps it
		const std::string address = g_active >= 0 ? archiveAddress(g_sources[(size_t)g_active], channel, *programme) : std::string();
		if (address.empty())
		{
			toast("This programme has finished and is not kept by the provider");
			return;
		}
		Channel past;
		past.name = programme->title + "  \xC2\xB7  " + channel.name;
		past.url = address;
		past.logo = channel.logo;
		past.group = channel.group;
		past.userAgent = channel.userAgent;
		past.referer = channel.referer;
		logLine("catch-up: %s", past.name.c_str());
		g_playTemp = true;
		startChannel(addChannel(past, false));
	}
	else if (programme && programme->start > now + 60)
	{
		toast("Starts at " + clockAt(programme->start) + "  \xC2\xB7  \xE2\x9C\x95 again on the current programme to watch now");
		return;
	}
	else
		startChannel(index);
	g_fullscreen = true;
	g_overlayUntil = 0;
	showControlsSoon();
}

static void drawGrid()
{
	const float keepScale = gfx::radiusScale();
	gfx::setRadiusScale(1.0f);
	gfx::rect(0, 0, 1920, 1080, kTvBack);
	gfx::roundRect(1180, -260, 1100, 1100, 550, 0xffffff06);
	const long long now = (long long)time(nullptr);
	const int count = (int)g_view.size();
	if (g_gridRow > count - 1) g_gridRow = count > 0 ? count - 1 : 0;
	const float x0 = 60, nameW = 330, gx = x0 + nameW + 12, gw = 1920 - gx - 40, top = 232, rowH = 100;
	const float perSecond = gw / (float)kGridSpan;

	gfx::bold(true);
	gfx::text(x0, 40, 40, kTvWhite, "Guide");
	gfx::bold(false);
	const std::string where = g_section == S_FAVOURITES ? "Favorites" : g_section == S_RECENT ? "Recently watched" : inGroups() ? g_playlist.groups[(size_t)g_catSel] : "All channels";
	gfx::text(x0 + 150, 56, 22, kTvDim, where, 700);
	const std::string clock = clockNow();
	gfx::text(1880 - gfx::textWidth(26, clock), 50, 26, kTvSoft, clock);

	// the focused programme, in words
	const Channel* focusChannel = g_gridRow < count ? &g_playlist.channels[(size_t)g_view[(size_t)g_gridRow]] : nullptr;
	const Programme* focusProgramme = focusChannel ? gridProgrammeAt(gridGuide(*focusChannel, false), g_gridTime) : nullptr;
	if (focusProgramme)
	{
		gfx::bold(true);
		gfx::text(x0, 104, 30, kTvWhite, focusProgramme->title, 1300);
		gfx::bold(false);
		std::string times = clockAt(focusProgramme->start) + " \xE2\x80\x93 " + clockAt(focusProgramme->stop);
		if (focusProgramme->stop <= now)
			times += focusProgramme->archive ? "     \xE2\x86\xBA  Catch-up: \xE2\x9C\x95 plays it" : "     Finished";
		else if (focusProgramme->start <= now)
			times += "     On now";
		gfx::text(x0, 146, 20, kTvSoft, times);
		if (!focusProgramme->description.empty())
			gfx::text(x0 + 420, 146, 20, kTvDim, focusProgramme->description, 1400);
	}
	else if (focusChannel)
		gfx::text(x0, 112, 24, kTvDim, focusChannel->name + "  \xC2\xB7  no programme information for this time", 1500);

	// the hours along the top
	for (long long tick = g_gridStart; tick <= g_gridStart + kGridSpan; tick += 1800)
	{
		const float x = gx + (float)(tick - g_gridStart) * perSecond;
		gfx::rect(x, top - 26, 2, 1080 - top - 20, 0xffffff14);
		if (tick < g_gridStart + kGridSpan)
			gfx::text(x + 8, top - 40, 19, kTvDim, clockAt(tick));
	}

	// the rows
	keepVisible(g_gridRow, g_gridTop, count, kGridRows);
	if (g_gridScroll - (float)g_gridTop > 20 || (float)g_gridTop - g_gridScroll > 20)
		g_gridScroll = (float)g_gridTop;
	glide(g_gridScroll, (float)g_gridTop, 18.0f);
	bool asked = false;
	gfx::clip(0, top - 6, 1920, (float)kGridRows * rowH + 12);
	for (int row = (int)g_gridScroll; row < (int)g_gridScroll + kGridRows + 1 && row < count; row++)
	{
		const int index = g_view[(size_t)row];
		const Channel& channel = g_playlist.channels[(size_t)index];
		const float y = top + ((float)row - g_gridScroll) * rowH;
		const bool here = row == g_gridRow;
		// the channel
		gfx::roundRect(x0, y, nameW, rowH - 8, 14, here ? 0xffffff26 : 0xffffff10);
		const int logo = channel.logo.empty() ? -1 : logoImage(channel.logo);
		int lw = 0, lh = 0;
		if (logo >= 0 && gfx::imageSize(logo, lw, lh) && lw > 0 && lh > 0)
		{
			const float fit = 56.0f / (float)(lw > lh ? lw : lh);
			gfx::image(logo, x0 + 46 - (float)lw * fit / 2, y + 46 - (float)lh * fit / 2, (float)lw * fit, (float)lh * fit);
		}
		else
		{
			gfx::roundRect(x0 + 14, y + 14, 64, 64, 14, (tvTint(channel.name) & 0xffffff00) | 0x90);
			gfx::bold(true);
			centredTight(x0 + 14, 64, y + 33, 22, kTvWhite, initials(channel.name));
			gfx::bold(false);
		}
		gfx::bold(here);
		gfx::text(x0 + 92, y + 20, 21, here ? kTvWhite : kTvSoft, channel.name, nameW - 104);
		gfx::bold(false);
		if (channel.archive)
			gfx::text(x0 + 92, y + 52, 16, kTvDim, "\xE2\x86\xBA catch-up");
		clickable(x0, y, nameW, rowH - 8, [row] { g_gridRow = row; gridPlay(row); });

		// its programmes
		const GuideEntry* guide = gridGuide(channel, !asked);
		if (!guide)
			asked = true;                               // one channel is asked for at a time
		{
			// this row's strip of the grid, kept inside the rows' own area
			const float areaTop = top - 6, areaBottom = top - 6 + (float)kGridRows * rowH + 12;
			const float from = y - 4 > areaTop ? y - 4 : areaTop, to = y - 4 + rowH < areaBottom ? y - 4 + rowH : areaBottom;
			gfx::clip(gx, from, gw, to > from ? to - from : 0);
		}
		if (!guide || guide->items.empty())
		{
			const bool focused = here && !pointerOn();
			gfx::roundRect(gx, y, gw, rowH - 8, 14, focused ? 0xffffff30 : 0xffffff0a);
			if (focused)
				gfx::panel(gx, y, gw, rowH - 8, 14, 3, kTvWhite, 0);
			gfx::text(gx + 24, y + 32, 20, kTvDim, !guide ? "Loading..." : guide->failed ? "No programme information" : "Nothing listed");
		}
		else
			for (const Programme& programme : guide->items)
			{
				if (programme.stop <= g_gridStart || programme.start >= g_gridStart + kGridSpan || programme.stop <= programme.start)
					continue;
				const float px = gx + (float)(programme.start - g_gridStart) * perSecond, pw = (float)(programme.stop - programme.start) * perSecond - 6;
				const float shownX = px < gx ? gx : px, shownW = px + pw - shownX;
				const bool focused = (here && !pointerOn() && programme.start <= g_gridTime && g_gridTime < programme.stop) || hovering(shownX, y, shownW, rowH - 8);
				const bool past = programme.stop <= now, onNow = programme.start <= now && now < programme.stop;
				if (focused)
					gfx::roundRect(px - 5, y - 5, pw + 10, rowH + 2, 18, 0xffffff30);
				gfx::roundRect(px, y, pw, rowH - 8, 14, focused ? kTvWhite : onNow ? 0xffffff2e : past ? 0xffffff0c : 0xffffff18);
				if (shownW > 60)
				{
					const uint32_t ink = focused ? kTvInk : past ? kTvDim : kTvWhite;
					gfx::bold(focused || onNow);
					gfx::text(shownX + 16, y + 18, 20, ink, programme.title, shownW - 28);
					gfx::bold(false);
					std::string under = clockAt(programme.start);
					if (past && programme.archive)
						under += "   \xE2\x86\xBA";
					gfx::text(shownX + 16, y + 52, 16, focused ? 0x0e121eb0 : kTvDim, under, shownW - 28);
				}
				const long long middle = (programme.start + programme.stop) / 2;
				clickable(shownX, y, shownW, rowH - 8, [row, middle] { g_gridRow = row; g_gridTime = middle; gridPlay(row); });
			}
		gfx::clip(0, top - 6, 1920, (float)kGridRows * rowH + 12);
	}
	gfx::unclip();
	// now
	if (now >= g_gridStart && now <= g_gridStart + kGridSpan)
	{
		const float x = gx + (float)(now - g_gridStart) * perSecond;
		gfx::rect(x - 1, top - 30, 3, (float)kGridRows * rowH + 34, 0xe23c46ff);
		gfx::roundRect(x - 7, top - 38, 15, 15, 7.5f, 0xe23c46ff);
	}
	if (count == 0)
		centredIn(0, 1920, 520, 26, kTvDim, "No channels in this list");
	gfx::text(x0, 1046, 18, kTvDim, "\xE2\x9C\x95 Watch (or play back a past programme marked \xE2\x86\xBA)     Left / Right Time     L1 / R1 Three hours     \xE2\x96\xB3 Now     \xE2\x97\x8B Back", 1800);
	gfx::setRadiusScale(keepScale);
}

static void gridInput(Action action)
{
	const int count = (int)g_view.size();
	const Channel* channel = g_gridRow < count ? &g_playlist.channels[(size_t)g_view[(size_t)g_gridRow]] : nullptr;
	const GuideEntry* guide = channel ? gridGuide(*channel, false) : nullptr;
	const Programme* programme = gridProgrammeAt(guide, g_gridTime);
	switch (action)
	{
	case A_BACK: g_grid = false; g_sel = g_gridRow < count ? g_gridRow : g_sel; break;
	case A_MENU: openManager(""); break;
	case A_UP: if (g_gridRow > 0) g_gridRow--; break;
	case A_DOWN: if (g_gridRow < count - 1) g_gridRow++; break;
	case A_LEFT:
		g_gridTime = programme ? programme->start - 60 : g_gridTime - 1800;
		{
			const Programme* before = gridProgrammeAt(guide, g_gridTime);
			if (before)
				g_gridTime = (before->start + before->stop) / 2;
		}
		gridKeepTimeInView();
		break;
	case A_RIGHT:
		g_gridTime = programme ? programme->stop + 60 : g_gridTime + 1800;
		{
			const Programme* after = gridProgrammeAt(guide, g_gridTime);
			if (after)
				g_gridTime = (after->start + after->stop) / 2;
		}
		gridKeepTimeInView();
		break;
	case A_PAGE_UP: g_gridTime -= kGridSpan; g_gridStart -= kGridSpan; break;
	case A_PAGE_DOWN: g_gridTime += kGridSpan; g_gridStart += kGridSpan; break;
	case A_SEARCH:
		g_gridTime = (long long)time(nullptr);
		g_gridStart = g_gridTime - g_gridTime % 1800 - 1800;
		break;
	case A_OK: gridPlay(g_gridRow); break;
	case A_FAVOURITE: if (channel) toggleFavourite(g_view[(size_t)g_gridRow]); break;
	default: break;
	}
}

