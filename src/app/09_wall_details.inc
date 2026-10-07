// ---- small touches ----------------------------------------------------------------

// A ring of dots chasing round, for "working on it".
static void spinner(float cx, float cy, float radius, uint32_t colour)
{
	const float keep = gfx::radiusScale();
	gfx::setRadiusScale(1.0f);
	const double turn = nowSeconds() * 1.4;
	for (int i = 0; i < 10; i++)
	{
		const float angle = (float)i / 10.0f * 6.2831853f;
		float age = (float)(turn - floor(turn)) - (float)i / 10.0f;   // how long since the bright spot passed this dot
		if (age < 0)
			age += 1;
		const uint32_t alpha = (uint32_t)(40 + 215 * (1 - age) * (1 - age));
		const float size = radius * 0.30f;
		gfx::roundRect(cx + cosf(angle) * radius - size / 2, cy + sinf(angle) * radius - size / 2, size, size, size / 2, (colour & 0xffffff00) | alpha);
	}
	gfx::setRadiusScale(keep);
}

// The time of day for the top of the screen. The console's own time zone is not always available to
// an app, so the hours are worked out from world time plus an offset the viewer can set.
static std::string clockNow()
{
	const time_t now = time(nullptr) + (time_t)g_clockOffsetMinutes * 60;
	struct tm parts;
	if (now <= 0 || !gmtime_r(&now, &parts))
		return "";
	char text[16];
	snprintf(text, sizeof(text), "%d:%02d", parts.tm_hour, parts.tm_min);
	return text;
}

// ---- watching, from wherever it was chosen -----------------------------------------

static void saveResumeNow()
{
	if (!g_nowOn || !g_nowVod || g_nowTemp || g_nowTab < 0 || g_nowTab >= 3)
		return;
	const double at = player::position(), length = player::duration();
	if (length < 120)
		return;
	auto& resume = g_tabs[g_nowTab].resume;
	if (at > 45 && at < length - 90)
		resume[g_now.url] = { at, length };
	else
	{
		resume.erase(g_now.url);
		if (at >= length - 90)
			markWatched(g_nowTab, g_now.url);
	}
	saveResume(g_nowTab);
}

static void stopPlayback()
{
	saveResumeNow();
	cancelLink();
	player::stop();
	gfx::videoClear();
	g_nowOn = false;
	g_playing = -1;
	g_fullscreen = false;
	g_nextAt = 0;
}

// Leaving full screen. A live channel chosen from the Live TV list carries on in the small preview;
// anything else (a film, an episode, something started from Home) stops, remembering its place.
static void leaveFullscreen()
{
	g_fullscreen = false;
	const bool previewed = g_tab == 0 && g_nowTab == 0 && !g_nowVod && !g_grid;   // (the grid has no picture to go back to)
	if (!previewed)
		stopPlayback();
	if (g_tab == kHomeTab)
		buildHome();
	fadeIn();
}

// Starts an entry of the current library; outside the Live TV list it opens straight to full screen.
static void beginWatching(int channel, double startAt)
{
	startChannel(channel, startAt);
	if (g_tab != 0)
	{
		g_fullscreen = true;
		g_overlayUntil = 0;
		showControlsSoon();
	}
}

// ---- the poster wall (movies and series) ------------------------------------------------

static int g_gridColumns = 6;
static float g_cardPop = 1;                     // the focused card grows a little; this eases it in
static int g_popFor = -1;

static int g_detailFocus = 0;
static bool g_detailOnEpisodes = false;
static int g_episodeSel = 0, g_episodeTop = 0;
static float g_episodeScroll = 0;
static std::string g_detailUrl;
static std::mutex g_detailMutex;
static int g_detailRequest = 0;
static bool g_detailArrived = false;
static Channel g_detailFound;
static const int kEpisodeRows = 12;

static void closeDetails()
{
	if (!g_detail)
		return;
	g_detail = false;
	std::lock_guard<std::mutex> lock(g_detailMutex);
	g_detailRequest++;
	g_detailArrived = false;
	fadeIn();
}

static void openDetails(int index)
{
	if (index < 0 || index >= (int)g_playlist.channels.size())
		return;
	const Channel item = g_playlist.channels[(size_t)index];
	g_detail = true;
	g_detailItem = index;
	g_detailUrl = item.url;
	g_detailFocus = 0;
	g_detailOnEpisodes = false;
	g_episodeSel = g_episodeTop = 0;
	g_episodeScroll = 0;
	fadeIn();
	if (isSeriesItem(item.url))
		openSeries(index, false);                   // its episodes, without touching the wall behind
	int request;
	{
		std::lock_guard<std::mutex> lock(g_detailMutex);
		request = ++g_detailRequest;
		g_detailArrived = false;
	}
	const Source source = g_sources[(size_t)g_active];
	startThread("details", [source, item, request] {
		Channel found = item;
		std::string error;
		if (!fetchDetails(source, found, error))
			return;
		std::lock_guard<std::mutex> lock(g_detailMutex);
		if (request != g_detailRequest)
			return;
		g_detailFound = found;
		g_detailArrived = true;
	});
}

// One poster card: the picture (or the title's initials until it arrives), with progress and a star.
static void drawPosterCard(const Channel& info, float x, float y, float w, float h, bool focused, bool favourite, double at, double length, bool watched = false)
{
	const float pop = focused ? g_cardPop : 1.0f;
	const float cw = w * pop, ch = h * pop, cx = x - (cw - w) / 2, cy = y - (ch - h) / 2;
	if (focused)
		gfx::roundRect(cx - 9, cy - 9, cw + 18, ch + 18, 22, (kBorder & 0xffffff00) | 0x55);     // glow
	gfx::roundRect(cx, cy, cw, ch, 14, kTile);
	const int poster = info.logo.empty() ? -1 : logoImage(info.logo, true);
	int pw = 0, ph = 0;
	if (poster >= 0 && gfx::imageSize(poster, pw, ph) && pw > 0 && ph > 0)
	{
		// fitted inside the card, keeping the picture's shape
		const float fit = (cw / (float)pw) < (ch / (float)ph) ? cw / (float)pw : ch / (float)ph;
		gfx::image(poster, cx + (cw - (float)pw * fit) / 2, cy + (ch - (float)ph * fit) / 2, (float)pw * fit, (float)ph * fit);
	}
	else
	{
		centredTight(cx, cw, cy + ch * 0.30f, 54, kMuted, initials(info.name));
		centredIn(cx + 8, cw - 16, cy + ch * 0.58f, 20, kMuted, info.name);
	}
	if (length > 0)
	{
		gfx::roundRect(cx + 10, cy + ch - 16, cw - 20, 7, 3.5f, 0x000000a0);
		gfx::roundRect(cx + 10, cy + ch - 16, (cw - 20) * (float)(at / length > 1 ? 1 : at / length), 7, 3.5f, kBorder);
	}
	if (watched)
	{
		gfx::roundRect(cx + 10, cy + 10, 36, 36, 18, 0x000000a8);
		centredTight(cx + 10, 36, cy + 14, 23, 0x7be0a0ff, "\xE2\x9C\x93");
	}
	if (favourite)
	{
		gfx::roundRect(cx + cw - 46, cy + 10, 36, 36, 18, 0x000000a8);
		centredTight(cx + cw - 46, 36, cy + 13, 24, 0xffd75eff, "\xE2\x98\x85");
	}
	gfx::panel(cx, cy, cw, ch, 14, focused ? 3.0f : 1.0f, focused ? kBorder : kTileLine, 0);
}

static void drawGrid(float x, float w)
{
	const int media = (int)(T - g_tabs);
	gfx::rect(x, kColumnTop, w, 1080 - kColumnTop, kColumn);
	std::string title = sectionName(g_section);
	if (inGroups())
	{
		gfx::rect(x, kColumnTop, w, 58, kBand);
		title = g_playlist.groups[(size_t)g_catSel];
	}
	const char categoryState = inGroups() ? g_groupState[(size_t)g_catSel] : 2;
	char count[32];
	snprintf(count, sizeof(count), categoryState == 1 ? "%d..." : "%d", (int)g_view.size());
	const float cw = gfx::textWidth(22, count);
	gfx::text(x + w - 26 - cw, kColumnTop + 20, 22, kMuted, count);
	gfx::text(x + 22, kColumnTop + 15, 28, kText, title, w - 80 - cw);

	const bool filtering = !g_filter.empty() && !T->filterCats;
	if (T->state != 2)
	{
		if (T->state == 3)
		{
			centredIn(x, w, 400, 26, kLive, std::string("No ") + kNoun[media] + " to show");
			centredIn(x, w, 442, 20, kMuted, T->error);
		}
		else
		{
			spinner(x + w / 2, 400, 26, kAccent);
			centredIn(x, w, 450, 24, kMuted, "Loading...");
		}
		return;
	}
	if (g_view.empty())
	{
		if (!filtering && categoryState == 3)
		{
			centredIn(x, w, 400, 26, kLive, "This category could not be loaded");
			centredIn(x, w, 442, 20, kMuted, g_genreFailure);
			centredIn(x, w, 492, 22, kMuted, "Press \xE2\x9C\x95 to try again");
			clickable(x, 380, w, 150, [] { g_groupState[(size_t)g_catSel] = 0; });
		}
		else if (!filtering && categoryState != 2)
		{
			spinner(x + w / 2, 400, 26, kAccent);
			centredIn(x, w, 450, 24, kMuted, "Loading...");
		}
		else
		{
			const std::string why = filtering ? "Nothing matches the filter" :
				g_section == S_FAVOURITES ? "No favorites yet" :
				g_section == S_RECENT ? "Nothing started yet" :
				g_section == S_GROUPS && T->cats.empty() ? "" :
				g_section == S_ALL ? "Open a category first" : std::string("No ") + kNoun[media];
			centredIn(x, w, 420, 26, kMuted, why);
			if (g_section == S_ALL && !filtering)
				centredIn(x, w, 462, 20, kMuted, "This wall fills with the categories you open in Groups");
		}
		return;
	}

	const float cardW = 204, cardH = 300, cellW = 232, cellH = 384;
	const int columns = (int)((w - 28) / cellW) > 1 ? (int)((w - 28) / cellW) : 1;
	g_gridColumns = columns;
	const float left = x + (w - (float)columns * cellW) / 2 + (cellW - cardW) / 2;
	const int rows = ((int)g_view.size() + columns - 1) / columns, row = g_sel / columns;
	if (row < T->gridTop) T->gridTop = row;
	if (row > T->gridTop + 1) T->gridTop = row - 1;
	if (T->gridTop > rows - 2) T->gridTop = rows - 2 > 0 ? rows - 2 : 0;
	if (T->gridScroll - (float)T->gridTop > 6 || (float)T->gridTop - T->gridScroll > 6)
		T->gridScroll = (float)T->gridTop;              // a long jump is not animated
	glide(T->gridScroll, (float)T->gridTop);
	if (g_popFor != g_sel)
	{
		g_popFor = g_sel;
		g_cardPop = 1;
	}
	glide(g_cardPop, 1.045f, 18.0f);

	const float top = kListTop + 6;
	clipTo(x, kListTop - 12, w, 1080 - kListTop + 12);
	const int first = (int)T->gridScroll;
	for (int pass = 0; pass < 2; pass++)                  // the focused card last, so it sits above its neighbours
		for (int r = first; r < first + 4 && r < rows; r++)
			for (int c = 0; c < columns; c++)
			{
				const int i = r * columns + c;
				if (i >= (int)g_view.size())
					break;
				const bool focused = i == g_sel && g_focus == F_CHANNELS && !pointerOn() && !g_detail;
				const float px = left + (float)c * cellW, py = top + ((float)r - T->gridScroll) * cellH;
				const bool hot = focused || (hovering(px, py, cardW, cardH) && !g_detail);
				if ((pass == 1) != hot)
					continue;
				const int channel = g_view[(size_t)i];
				const Channel& info = g_playlist.channels[(size_t)channel];
				const auto resume = T->resume.find(info.url);
				drawPosterCard(info, px, py, cardW, cardH, hot, g_isFavourite[(size_t)channel] != 0,
					resume != T->resume.end() ? resume->second.first : 0, resume != T->resume.end() ? resume->second.second : 0,
					T->watched.count(info.url) != 0);
				gfx::text(px, py + cardH + 12, 22, hot ? kAccent : kText, info.name, cardW);
				const std::string under = isSeriesItem(info.url) ? (info.year.empty() ? "Series" : "Series  \xC2\xB7  " + info.year) : info.year;
				gfx::text(px, py + cardH + 42, 18, kMuted, under, cardW);
				if (!g_detail)
					clickable(px, py, cardW, cardH + 60, [i, channel] {
						g_focus = F_CHANNELS;
						g_sel = i;
						openDetails(channel);
					});
			}
	unclipAll();
	if (rows > 2)
	{
		const float track = 1080 - kListTop - 20, size = track * 2 / (float)rows < 30 ? 30 : track * 2 / (float)rows;
		gfx::roundRect(x + w - 8, kListTop + (track - size) * T->gridScroll / (float)(rows - 2), 5, size, 2, kScroll);
	}
}

// Text wrapped to a width; returns the y below the last line drawn.
static float drawWrapped(float x, float y, float w, float size, uint32_t colour, const std::string& words, int maxLines)
{
	std::string rest = words;
	for (int line = 0; line < maxLines && !rest.empty(); line++)
	{
		size_t cut = rest.size();
		const size_t newline = rest.find('\n');
		if (newline != std::string::npos)
			cut = newline;
		while (cut > 0 && gfx::textWidth(size, rest.substr(0, cut)) > w)
		{
			const size_t space = rest.rfind(' ', cut - 1);
			if (space == std::string::npos || space == 0)
				break;
			cut = space;
		}
		const bool last = line == maxLines - 1 && cut < rest.size();
		gfx::text(x, y, size, colour, last ? rest : rest.substr(0, cut), w);   // the last line is cut with "..."
		rest = cut < rest.size() ? rest.substr(cut + 1) : std::string();
		y += size * 1.38f;
	}
	return y;
}

static const char* const kTrailerLabel = "Trailer";

static std::vector<std::string> detailButtons(const Channel& item, bool series, double resumeAt)
{
	std::vector<std::string> buttons;
	if (!series)
	{
		buttons.push_back(resumeAt > 0 ? "\xE2\x96\xB6   Resume from " + clockText(resumeAt) : "\xE2\x96\xB6   Play");
		if (resumeAt > 0)
			buttons.push_back("Start from the beginning");
	}
	if (!item.trailer.empty())
		buttons.push_back(kTrailerLabel);
	buttons.push_back(g_isFavourite[(size_t)g_detailItem] ? "\xE2\x98\x85   In favorites" : "\xE2\x98\x86   Add to favorites");
	return buttons;
}

static void pressDetailButton(int button)
{
	const Channel& item = g_playlist.channels[(size_t)g_detailItem];
	const bool series = isSeriesItem(item.url);
	const auto resume = T->resume.find(item.url);
	const double resumeAt = !series && resume != T->resume.end() ? resume->second.first : 0;
	const std::vector<std::string> buttons = detailButtons(item, series, resumeAt);
	const int count = (int)buttons.size();
	if (button < 0 || button >= count)
		return;
	if (button == count - 1)
		toggleFavourite(g_detailItem);
	else if (buttons[(size_t)button] == kTrailerLabel)
	{
		// the trailer plays like a film, but is not remembered as one
		Channel trailer;
		trailer.name = "Trailer: " + item.name;
		trailer.url = item.trailer;
		trailer.logo = item.logo;
		trailer.group = item.group;
		const int index = addChannel(trailer, false);
		g_playTemp = true;
		beginWatching(index, 0);
	}
	else if (!series)
		beginWatching(g_detailItem, button == 0 ? resumeAt : 0);
}

static void playEpisode(int place)
{
	if (place < 0 || place >= (int)T->drillList.size())
		return;
	const int episode = T->drillList[(size_t)place];
	const auto resume = T->resume.find(g_playlist.channels[(size_t)episode].url);
	if (resume != T->resume.end() && resume->second.first > 45)
	{
		g_askChannel = episode;
		g_askPosition = resume->second.first;
		g_askSel = 0;
		return;
	}
	beginWatching(episode, 0);
}

static void drawDetails()
{
	if (g_detailItem < 0 || g_detailItem >= (int)g_playlist.channels.size())
	{
		g_detail = false;
		return;
	}
	{
		// the plot and the rest, when they had to be asked for separately
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
	const Channel& item = g_playlist.channels[(size_t)g_detailItem];
	const bool series = isSeriesItem(item.url);
	const float x = 90, top = kColumnTop, w = 1920 - x;
	gfx::rect(x, top, w, 1080 - top, kBackground);
	gfx::rect(x, top, w, 1080 - top, kColumn);
	clickable(x, top, w, 1080 - top, [] {});              // nothing behind the page can be clicked

	// back
	const bool backHover = hovering(x + 26, top + 22, 150, 46);
	gfx::panel(x + 26, top + 22, 150, 46, 23, backHover ? 2.0f : 1.0f, backHover ? kBorder : kLine, kChip);
	centredTight(x + 26, 150, top + 32, 22, kText, "\xE2\x80\xB9  Back");
	clickable(x + 26, top + 22, 150, 46, [] { closeDetails(); });

	// poster
	const float px = x + 40, py = top + 92, pw = 330, ph = 495;
	gfx::roundRect(px, py, pw, ph, 18, kTile);
	const int poster = item.logo.empty() ? -1 : logoImage(item.logo, true);
	int iw = 0, ih = 0;
	if (poster >= 0 && gfx::imageSize(poster, iw, ih) && iw > 0 && ih > 0)
	{
		const float fit = (pw / (float)iw) < (ph / (float)ih) ? pw / (float)iw : ph / (float)ih;
		gfx::image(poster, px + (pw - (float)iw * fit) / 2, py + (ph - (float)ih * fit) / 2, (float)iw * fit, (float)ih * fit);
	}
	else
		centredTight(px, pw, py + ph * 0.38f, 90, kMuted, initials(item.name));
	gfx::panel(px, py, pw, ph, 18, 1, kTileLine, 0);

	// title and facts
	const float tx = px + pw + 44, tw = series ? 700.0f : 1320.0f;
	float y = top + 92;
	gfx::text(tx, y, 46, kText, item.name, tw);
	y += 66;
	std::string facts;
	const auto fact = [&facts](const std::string& value) {
		if (!value.empty())
			facts += (facts.empty() ? "" : "   \xC2\xB7   ") + value;
	};
	fact(item.year);
	if (!item.rating.empty())
		fact("\xE2\x98\x85 " + item.rating);
	fact(item.length);
	fact(item.genres);
	if (facts.empty())
		facts = item.group;
	gfx::text(tx, y, 24, kAccent, facts, tw);
	y += 50;
	y = drawWrapped(tx, y, tw, 23, kMuted, item.plot.empty() ? "No description is available for this title." : item.plot, series ? 9 : 7);
	if (!item.cast.empty())
		y = drawWrapped(tx, y + 12, tw, 20, kMuted, "With " + item.cast, 2);

	// buttons
	const auto resume = T->resume.find(item.url);
	const double resumeAt = !series && resume != T->resume.end() ? resume->second.first : 0;
	const std::vector<std::string> buttons = detailButtons(item, series, resumeAt);
	if (g_detailFocus > (int)buttons.size() - 1)
		g_detailFocus = (int)buttons.size() - 1;
	float bx = tx;
	const float by = y + 26 < py + ph - 70 ? py + ph - 70 : y + 26;
	for (int b = 0; b < (int)buttons.size(); b++)
	{
		const float bw = gfx::textWidth(25, buttons[(size_t)b]) + 64;
		const bool focused = (!pointerOn() && !g_detailOnEpisodes && g_detailFocus == b && g_askChannel < 0) || hovering(bx, by, bw, 66);
		if (focused)
			gfx::roundRect(bx - 7, by - 7, bw + 14, 80, 40, (kBorder & 0xffffff00) | 0x48);
		const bool primary = b == 0 && !series;
		gfx::panel(bx, by, bw, 66, 33, 2, focused ? kBorder : primary ? kBorder : kLine, focused ? kBorder : primary ? kSelected : kChip);
		centredTight(bx, bw, by + 19, 25, focused ? 0x0b0e14ff : kText, buttons[(size_t)b]);
		clickable(bx, by, bw, 66, [b] { g_detailOnEpisodes = false; g_detailFocus = b; pressDetailButton(b); });
		bx += bw + 18;
	}

	// a series: its seasons and episodes, down the right
	if (series)
	{
		const float ex = tx + tw + 50, ew = 1920 - ex - 30, et = top + 92, rowH = 64;
		gfx::roundRect(ex, et - 8, ew, 1080 - et - 14, 16, kCard);
		char heading[64];
		snprintf(heading, sizeof(heading), T->drillState == 2 ? "Episodes  \xC2\xB7  %d" : "Episodes", (int)T->drillList.size());
		gfx::text(ex + 24, et + 6, 26, kText, heading);
		const float lt = et + 56;
		if (T->drillState == 1)
		{
			spinner(ex + ew / 2, lt + 150, 24, kAccent);
			centredIn(ex, ew, lt + 196, 22, kMuted, "Loading episodes...");
		}
		else if (T->drillState == 3)
		{
			centredIn(ex, ew, lt + 150, 24, kLive, "The episodes could not be loaded");
			centredIn(ex, ew, lt + 190, 19, kMuted, T->drillError);
		}
		else
		{
			const int count = (int)T->drillList.size();
			if (g_episodeSel > count - 1) g_episodeSel = count > 0 ? count - 1 : 0;
			keepVisible(g_episodeSel, g_episodeTop, count, kEpisodeRows);
			if (g_episodeScroll - (float)g_episodeTop > 20 || (float)g_episodeTop - g_episodeScroll > 20)
				g_episodeScroll = (float)g_episodeTop;
			glide(g_episodeScroll, (float)g_episodeTop);
			clipTo(ex, lt, ew, (float)kEpisodeRows * rowH);
			for (int i = (int)g_episodeScroll; i < (int)g_episodeScroll + kEpisodeRows + 1 && i < count; i++)
			{
				const int episode = T->drillList[(size_t)i];
				const Channel& info = g_playlist.channels[(size_t)episode];
				const float ry = lt + ((float)i - g_episodeScroll) * rowH;
				const bool focused = (!pointerOn() && g_detailOnEpisodes && i == g_episodeSel && g_askChannel < 0) || hovering(ex + 10, ry, ew - 20, rowH - 6);
				if (focused)
					gfx::panel(ex + 10, ry, ew - 20, rowH - 6, 12, 2, kBorder, kSelected);
				const auto left = T->resume.find(info.url);
				gfx::text(ex + 30, ry + (left != T->resume.end() ? 8.0f : 16.0f), 23, focused ? kAccent : kText, info.name, ew - 110);
				if (T->watched.count(info.url))
					gfx::text(ex + ew - 62, ry + 14, 26, 0x7be0a0ff, "\xE2\x9C\x93");        // seen
				if (left != T->resume.end() && left->second.second > 0)
				{
					gfx::roundRect(ex + 30, ry + 42, 160, 6, 3, kLine);
					gfx::roundRect(ex + 30, ry + 42, 160 * (float)(left->second.first / left->second.second), 6, 3, kBorder);
				}
				clickable(ex + 10, ry, ew - 20, rowH - 6, [i] { g_detailOnEpisodes = true; g_episodeSel = i; playEpisode(i); });
			}
			unclipAll();
			drawScrollbar(ex + ew - 10, lt, (float)kEpisodeRows * rowH, (int)(g_episodeScroll + 0.5f), count, kEpisodeRows);
		}
	}
	gfx::text(x + 40, 1046, 20, kMuted, series ? "\xE2\x9C\x95 Choose   Left / Right Buttons or episodes   \xE2\x97\x8B Back" : "\xE2\x9C\x95 Choose   Left / Right Buttons   \xE2\x97\x8B Back");
}

static void detailsInput(Action action)
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
		if (g_detailOnEpisodes)
			g_detailOnEpisodes = false;
		else if (g_detailFocus > 0)
			g_detailFocus--;
		break;
	case A_RIGHT:
		if (!g_detailOnEpisodes)
		{
			if (g_detailFocus < buttons - 1)
				g_detailFocus++;
			else if (episodes > 0)
				g_detailOnEpisodes = true;
		}
		break;
	case A_UP: if (g_detailOnEpisodes) moveSelection(g_episodeSel, episodes, -1); break;
	case A_DOWN:
		if (g_detailOnEpisodes)
			moveSelection(g_episodeSel, episodes, 1);
		else if (episodes > 0)
			g_detailOnEpisodes = true;
		break;
	case A_PAGE_UP: if (g_detailOnEpisodes) moveSelection(g_episodeSel, episodes, -kEpisodeRows); break;
	case A_PAGE_DOWN: if (g_detailOnEpisodes) moveSelection(g_episodeSel, episodes, kEpisodeRows); break;
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

