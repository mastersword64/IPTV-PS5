// ---- the TV layout: shared pieces for the remaining screens ----------------------------------

// The whole-screen picture behind a title: its poster enlarged until it is colour and light, or a
// wash of its own colour; then shading, dark on the left for words and from `fadeFrom` downwards.
static void tvBackdrop(const Channel& info, float fadeFrom, float strength = 0.85f)
{
	// (An enlarged poster used to go here. Posters are small pictures; stretched over a whole
	// screen they were only a blur, so the screen takes the title's colour instead.)
	(void)strength;
	tvWash(info.name, fadeFrom);
}

static void tvToast(float centre)
{
	if (nowSeconds() >= g_toastUntil)
		return;
	const float keep = gfx::radiusScale();
	gfx::setRadiusScale(1.0f);
	const float w = gfx::textWidth(24, g_toast) + 60;
	gfx::roundRect(centre - w / 2, 60, w, 56, 28, 0xffffffee);
	centredTight(centre - w / 2, w, 75, 24, kTvInk, g_toast);
	gfx::setRadiusScale(keep);
}

// The facts of a title as chips: year, rating, length, genres.
static void tvFacts(const Channel& info, float x, float y, float limit)
{
	float cx = x;
	if (!info.year.empty()) cx += tvChip(cx, y, info.year);
	if (!info.rating.empty()) cx += tvChip(cx, y, "\xE2\x98\x85 " + info.rating);
	if (!info.length.empty()) cx += tvChip(cx, y, info.length);
	if (!info.genres.empty() && cx < x + limit && info.genres.size() < 40) cx += tvChip(cx, y, info.genres);
	else if (cx == x && !info.group.empty()) tvChip(cx, y, info.group);
}

// ---- the TV layout: Movies and Series ----------------------------------------------------------
// A row of posters per category, one under another, with the title in focus shown large above.
// Favourites and part-watched titles come first, as rows of their own.

struct TvWallRow { int kind; int cat; std::string title; };       // kind: 0 favourites, 1 continue watching, 2 a category
static int g_tvWallRow[3] = { 0, 0, 0 };
static float g_tvWallScroll[3] = { 0, 0, 0 };
static std::unordered_map<int, int> g_tvWallColumn[3];             // where each row was left
static std::unordered_map<int, float> g_tvWallSlide[3];            // how far each row has slid sideways
static bool g_tvWallOnChips = false;
static int g_tvWallChip = 0;
static float g_tvWallChipScroll = 0;
static const int kTvNoRow = -1000000;
static int g_tvWallOnly[3] = { kTvNoRow, kTvNoRow, kTvNoRow };     // the one row being shown by itself (its key), if any
static int g_tvGridTop[3] = { 0, 0, 0 };
static float g_tvGridScroll[3] = { 0, 0, 0 };
static const int kTvGridColumns = 8;

static std::vector<TvWallRow> tvWallRows()
{
	std::vector<TvWallRow> rows;
	if (!T->favourites.empty())
		rows.push_back({ 0, -1, "Favorites" });
	if (!T->recent.empty())
		rows.push_back({ 1, -1, "Continue watching" });
	for (const int cat : T->cats)
		rows.push_back({ 2, cat, g_playlist.groups[(size_t)cat] });
	return rows;
}

static int tvWallKey(const TvWallRow& row)
{
	return row.kind == 2 ? row.cat : -1 - row.kind;
}

// Makes the app's own idea of "the list on show" match the row in focus, so that everything else
// (fetching a category, the filter, the More menu, favourites) acts on that row.
static void tvWallApply(const TvWallRow& row)
{
	const Section wanted = row.kind == 0 ? S_FAVOURITES : row.kind == 1 ? S_RECENT : S_GROUPS;
	if (g_section == wanted && (row.kind != 2 || g_catSel == row.cat))
		return;
	closeDrill();
	g_section = wanted;
	if (row.kind == 2)
		g_catSel = row.cat;
	leaveFilter();
	rebuildView(-1);
	const int tab = (int)(T - g_tabs);
	const auto left = g_tvWallColumn[tab].find(tvWallKey(row));
	g_sel = left != g_tvWallColumn[tab].end() ? left->second : 0;
	if (g_sel > (int)g_view.size() - 1)
		g_sel = g_view.empty() ? 0 : (int)g_view.size() - 1;
}

// Which row is being shown by itself (-1: none, every row is on show).
static int tvWallOnlyIndex(const std::vector<TvWallRow>& rows)
{
	const int tab = (int)(T - g_tabs);
	for (int i = 0; i < (int)rows.size(); i++)
		if (tvWallKey(rows[(size_t)i]) == g_tvWallOnly[tab])
			return i;
	g_tvWallOnly[tab] = kTvNoRow;
	return -1;
}

static void tvWallGoTo(int rowIndex, const std::vector<TvWallRow>& rows);

// The chips: "All" first, then every row. Choosing a row shows it by itself; "All" brings every row back.
static void tvWallPressChip(int chip, const std::vector<TvWallRow>& rows)
{
	const int tab = (int)(T - g_tabs);
	g_tvWallOnChips = false;
	if (chip <= 0 || chip - 1 >= (int)rows.size())
	{
		g_tvWallOnly[tab] = kTvNoRow;
		return;
	}
	tvWallGoTo(chip - 1, rows);
	g_tvWallOnly[tab] = tvWallKey(rows[(size_t)chip - 1]);
	g_tvGridTop[tab] = 0;
	g_tvGridScroll[tab] = 0;
	g_sel = 0;
}

static void tvWallGoTo(int rowIndex, const std::vector<TvWallRow>& rows)
{
	const int tab = (int)(T - g_tabs);
	if (rowIndex < 0 || rowIndex >= (int)rows.size())
		return;
	if (g_tvWallRow[tab] < (int)rows.size())
		g_tvWallColumn[tab][tvWallKey(rows[(size_t)g_tvWallRow[tab]])] = g_sel;     // remember this row's place
	g_tvWallRow[tab] = rowIndex;
	tvWallApply(rows[(size_t)rowIndex]);
}

// A row's titles in the order chosen with "Order" (the row in focus is already in that order;
// this keeps the rows around it in step).
static const std::vector<int>& tvWallOrdered(const std::vector<int>& list, int key)
{
	if (g_wallSort == 0)
		return list;
	struct Kept { size_t size = (size_t)-1; int sort = -1; std::vector<int> order; };
	static std::unordered_map<long long, Kept> kept;
	Kept& k = kept[((long long)(T - g_tabs) << 40) ^ (long long)(unsigned)key];
	if (k.size != list.size() || k.sort != g_wallSort)
	{
		k.order = list;
		k.size = list.size();
		k.sort = g_wallSort;
		const std::vector<Channel>& all = g_playlist.channels;
		const int mode = g_wallSort;
		std::stable_sort(k.order.begin(), k.order.end(), [&all, mode](int a, int b) {
			if (a < 0 || b < 0 || a >= (int)all.size() || b >= (int)all.size())
				return a < b;
			const Channel& x = all[(size_t)a];
			const Channel& y = all[(size_t)b];
			if (mode == 1)
				return lower(x.name) < lower(y.name);
			if (mode == 2)
				return atoi(x.year.c_str()) > atoi(y.year.c_str());
			return atof(x.rating.c_str()) > atof(y.rating.c_str());
		});
	}
	return k.order;
}

static void drawTvWall()
{
	const float keepScale = gfx::radiusScale();
	gfx::setRadiusScale(1.0f);
	const int tab = (int)(T - g_tabs);
	const float x0 = 150;

	if (T->state != 2)
	{
		gfx::rect(0, 0, 1920, 1080, kTvBack);
		gfx::bold(true);
		gfx::text(x0, 60, 36, kTvWhite, kTabName[tab]);
		gfx::bold(false);
		if (T->state == 3)
		{
			centredIn(92, 1828, 460, 30, kTvWhite, std::string("No ") + kNoun[tab] + " to show");
			centredIn(92, 1828, 512, 21, kTvDim, T->error);
		}
		else
		{
			spinner(1006, 470, 30, kTvWhite);
			centredIn(92, 1828, 530, 24, kTvDim, "Loading...");
		}
		drawTvRail();
		gfx::setRadiusScale(keepScale);
		return;
	}

	const std::vector<TvWallRow> rows = tvWallRows();
	if (rows.empty())
	{
		gfx::rect(0, 0, 1920, 1080, kTvBack);
		centredIn(92, 1828, 480, 28, kTvSoft, T->hidden.empty() ? "Nothing here yet" : "Every category is hidden (Settings > Groups)");
		drawTvRail();
		gfx::setRadiusScale(keepScale);
		return;
	}
	if (g_tvWallRow[tab] > (int)rows.size() - 1)
		g_tvWallRow[tab] = (int)rows.size() - 1;
	const TvWallRow& focusRow = rows[(size_t)g_tvWallRow[tab]];
	tvWallApply(focusRow);
	if (g_sel > (int)g_view.size() - 1)
		g_sel = g_view.empty() ? 0 : (int)g_view.size() - 1;

	// the title in focus, large, over its own artwork
	const int focusChannel = selectedChannel();
	static const Channel nothing;
	const Channel& info = focusChannel >= 0 ? g_playlist.channels[(size_t)focusChannel] : nothing;
	if (focusChannel >= 0)
		tvBackdrop(info, 150);
	else
		gfx::rect(0, 0, 1920, 1080, kTvBack);
	if (focusChannel >= 0)
	{
		const float in = tvHeroFade(info.url);
		gfx::bold(true);
		gfx::text(x0, 110 + (1 - in) * 10, 58, faded(kTvWhite, in), info.name, 1150);
		gfx::bold(false);
		tvFacts(info, x0, 196, 700);
		if (!info.plot.empty())
			drawWrapped(x0, 248, 980, 22, faded(kTvSoft, in), info.plot, 2);
		else if (isSeriesItem(info.url))
			gfx::text(x0, 248, 22, kTvDim, "Series  \xC2\xB7  \xE2\x9C\x95 for its episodes");
	}
	const std::string clock = clockNow();
	gfx::text(1860 - gfx::textWidth(26, clock), 50, 26, kTvSoft, clock);

	// chips: "All", then every row by name; choosing one shows that row by itself
	const int only = tvWallOnlyIndex(rows);
	const int chipCount = (int)rows.size() + 1;
	const auto chipName = [&rows](int i) { return i == 0 ? std::string("All") : rows[(size_t)i - 1].title; };
	std::vector<float> chipX((size_t)chipCount + 1);
	float total = 0;
	for (int i = 0; i < chipCount; i++)
	{
		chipX[(size_t)i] = total;
		total += gfx::textWidth(19, chipName(i)) + 30 + 10;
	}
	chipX[(size_t)chipCount] = total;
	if (g_tvWallChip > chipCount - 1)
		g_tvWallChip = chipCount - 1;
	const int chipInUse = only >= 0 ? only + 1 : 0;
	{
		const int watch = g_tvWallOnChips ? g_tvWallChip : chipInUse;
		const float room = 1920 - x0 - 260;
		float want = g_tvWallChipScroll;
		if (chipX[(size_t)watch] < want) want = chipX[(size_t)watch];
		if (chipX[(size_t)watch + 1] > want + room) want = chipX[(size_t)watch + 1] - room;
		glide(g_tvWallChipScroll, want < 0 ? 0 : want, 16.0f);
	}
	clipTo(x0 - 10, 34, 1920 - x0 - 230, 56);
	for (int i = 0; i < chipCount; i++)
	{
		const float x = x0 + chipX[(size_t)i] - g_tvWallChipScroll, w = chipX[(size_t)i + 1] - chipX[(size_t)i] - 10;
		if (x > 1920 || x + w < 0)
			continue;
		const bool focused = (g_tvWallOnChips && !pointerOn() && !g_tvOnRail && i == g_tvWallChip) || hovering(x, 42, w, 38);
		const bool on = i == chipInUse;
		gfx::roundRect(x, 42, w, 38, 19, focused ? kTvWhite : on ? 0xffffffd0 : 0xffffff26);
		gfx::bold(on || focused);
		gfx::text(x + 15, 51, 19, focused || on ? kTvInk : kTvWhite, chipName(i));
		gfx::bold(false);
		clickable(x, 42, w, 38, [i] { g_tvWallChip = i; tvWallPressChip(i, tvWallRows()); });
	}
	unclipAll();

	if (only >= 0)
	{
		// one category by itself: all of it, as a wall
		const float gridTop = 364, cardW = 180, cardH = 270, cell = cardW + 24, rowH = 344;
		const int columns = kTvGridColumns, count = (int)g_view.size(), gridRows = (count + columns - 1) / columns, row = count > 0 ? g_sel / columns : 0;
		const char state = focusRow.kind == 2 ? g_groupState[(size_t)focusRow.cat] : 2;
		gfx::bold(true);
		char heading[160];
		snprintf(heading, sizeof(heading), state == 1 ? "%s   \xC2\xB7   %d..." : "%s   \xC2\xB7   %d", focusRow.title.c_str(), count);
		gfx::text(x0, 314, 24, kTvWhite, heading, 1200);
		gfx::bold(false);
		gfx::text(1860 - gfx::textWidth(18, "\xE2\x97\x8B All categories     L1 / R1 Next category"), 318, 18, kTvDim, "\xE2\x97\x8B All categories     L1 / R1 Next category");
		if (count == 0)
		{
			if (state == 2)
				gfx::text(x0, 480, 24, kTvDim, !g_filter.empty() ? "Nothing matches the filter" : "Nothing here");
			else if (state == 3)
				gfx::text(x0, 480, 24, kTvSoft, "This category could not be loaded  \xC2\xB7  \xE2\x9C\x95 to try again");
			else
			{
				spinner(x0 + 20, 494, 16, kTvWhite);
				gfx::text(x0 + 56, 480, 24, kTvDim, "Loading...");
			}
		}
		if (row < g_tvGridTop[tab]) g_tvGridTop[tab] = row;
		if (row > g_tvGridTop[tab] + 1) g_tvGridTop[tab] = row - 1;
		if (g_tvGridScroll[tab] - (float)g_tvGridTop[tab] > 5 || (float)g_tvGridTop[tab] - g_tvGridScroll[tab] > 5)
			g_tvGridScroll[tab] = (float)g_tvGridTop[tab];
		glide(g_tvGridScroll[tab], (float)g_tvGridTop[tab], 13.0f);
		const bool gridFocus = !pointerOn() && !g_tvOnRail && !g_tvWallOnChips && !g_detail;
		clipTo(0, gridTop - 14, 1920, 1080 - gridTop + 14);
		for (int pass = 0; pass < 2; pass++)
			for (int r = (int)g_tvGridScroll[tab]; r < (int)g_tvGridScroll[tab] + 4 && r < gridRows; r++)
				for (int c = 0; c < columns; c++)
				{
					const int i = r * columns + c;
					if (i >= count)
						break;
					const int channel = g_view[(size_t)i];
					const Channel& entry = g_playlist.channels[(size_t)channel];
					const float cx = x0 + (float)c * cell, cy = gridTop + ((float)r - g_tvGridScroll[tab]) * rowH;
					const bool hot = (gridFocus && i == g_sel) || (!g_detail && hovering(cx, cy, cardW, cardH));
					if ((pass == 1) != hot)
						continue;
					const auto resume = T->resume.find(entry.url);
					tvCard(entry, 0, cx, cy, cardW, cardH, hot, resume != T->resume.end() ? resume->second.first : 0, resume != T->resume.end() ? resume->second.second : 0);
					if (T->watched.count(entry.url))
					{
						gfx::roundRect(cx + 10, cy + 10, 34, 34, 17, 0x000000a8);
						centredTight(cx + 10, 34, cy + 14, 21, 0x7be0a0ff, "\xE2\x9C\x93");
					}
					if (g_isFavourite[(size_t)channel])
					{
						gfx::roundRect(cx + cardW - 44, cy + 10, 34, 34, 17, 0x000000a8);
						centredTight(cx + cardW - 44, 34, cy + 13, 22, 0xffd75eff, "\xE2\x98\x85");
					}
					gfx::bold(hot);
					gfx::text(cx, cy + cardH + (hot ? 22.0f : 12.0f), 19, hot ? kTvWhite : kTvSoft, entry.name, cardW);
					gfx::bold(false);
					if (!g_detail)
						clickable(cx, cy, cardW, cardH + 40, [i, channel] {
							g_tvOnRail = g_tvWallOnChips = false;
							if (i < (int)g_view.size() && g_view[(size_t)i] == channel)
								g_sel = i;
							openDetails(channel);
						});
				}
		unclipAll();
		if (g_popFor != 7000000 + g_sel)
		{
			g_popFor = 7000000 + g_sel;
			g_cardPop = 1;
		}
		glide(g_cardPop, 1.08f, 18.0f);
		if (!g_detail)
			tvDrawFocus();
		drawTvRail();
		gfx::setRadiusScale(keepScale);
		return;
	}

	// the rows: the one in focus sits at a fixed height, the next shows below it
	const float rowsTop = 346, rowH = 384, cardW = 180, cardH = 270, cell = cardW + 24;
	const int visible = (int)((1920 - x0 - 30) / cell);
	if (g_tvWallScroll[tab] - (float)g_tvWallRow[tab] > 4 || (float)g_tvWallRow[tab] - g_tvWallScroll[tab] > 4)
		g_tvWallScroll[tab] = (float)g_tvWallRow[tab];
	glide(g_tvWallScroll[tab], (float)g_tvWallRow[tab], 13.0f);
	clipTo(0, rowsTop - 26, 1920, 1080 - rowsTop + 26);
	for (int r = g_tvWallRow[tab] - 1; r <= g_tvWallRow[tab] + 2; r++)
	{
		if (r < 0 || r >= (int)rows.size())
			continue;
		const TvWallRow& row = rows[(size_t)r];
		const bool here = r == g_tvWallRow[tab];
		const float ry = rowsTop + ((float)r - g_tvWallScroll[tab]) * rowH;
		const std::vector<int>& list = here ? g_view : row.kind == 0 ? T->favourites : row.kind == 1 ? T->recent : tvWallOrdered(g_groupChannels[(size_t)row.cat + 1], row.cat);
		const char state = row.kind == 2 ? g_groupState[(size_t)row.cat] : 2;
		gfx::bold(true);
		gfx::text(x0, ry, 26, here ? kTvWhite : kTvDim, row.title, 900);
		gfx::bold(false);
		if (list.empty())
		{
			// not fetched yet (or empty): outlines where the posters will be
			for (int i = 0; i < visible; i++)
				gfx::roundRect(x0 + (float)i * cell, ry + 50, cardW, cardH, 14, 0xffffff0c);
			const std::string note = state == 3 ? "Could not be loaded  \xC2\xB7  \xE2\x9C\x95 to try again" : state == 2 ? (!g_filter.empty() && here ? "Nothing matches the filter" : "Nothing here") : here ? "Loading..." : "";
			if (!note.empty())
				gfx::text(x0 + 24, ry + 50 + cardH / 2 - 14, 22, kTvDim, note);
			if (here && state != 2 && state != 3)
				spinner(x0 + 190, ry + 50 + cardH / 2, 14, kTvWhite);
			continue;
		}
		float& slide = g_tvWallSlide[tab][tvWallKey(row)];
		if (here)
		{
			float target = slide;
			if ((float)g_sel < target) target = (float)g_sel;
			if ((float)g_sel > target + (float)visible - 1) target = (float)g_sel - (float)visible + 1;
			glide(slide, (float)(int)(target + 0.5f));
		}
		const bool rowFocus = here && !pointerOn() && !g_tvOnRail && !g_tvWallOnChips && !g_detail;
		for (int pass = 0; pass < 2; pass++)
			for (int i = (int)slide - 1; i < (int)slide + visible + 2 && i < (int)list.size(); i++)
			{
				if (i < 0)
					continue;
				const int channel = list[(size_t)i];
				if (channel < 0 || channel >= (int)g_playlist.channels.size())
					continue;
				const Channel& entry = g_playlist.channels[(size_t)channel];
				const float cx = x0 + ((float)i - slide) * cell, cy = ry + 50;
				const bool hot = (rowFocus && i == g_sel) || (!g_detail && hovering(cx, cy, cardW, cardH));
				if ((pass == 1) != hot)
					continue;
				const auto resume = T->resume.find(entry.url);
				tvCard(entry, 0, cx, cy, cardW, cardH, hot, resume != T->resume.end() ? resume->second.first : 0, resume != T->resume.end() ? resume->second.second : 0);
				if (T->watched.count(entry.url))
				{
					gfx::roundRect(cx + 10, cy + 10, 34, 34, 17, 0x000000a8);
					centredTight(cx + 10, 34, cy + 14, 21, 0x7be0a0ff, "\xE2\x9C\x93");
				}
				if (g_isFavourite[(size_t)channel])
				{
					gfx::roundRect(cx + cardW - 44, cy + 10, 34, 34, 17, 0x000000a8);
					centredTight(cx + cardW - 44, 34, cy + 13, 22, 0xffd75eff, "\xE2\x98\x85");
				}
				gfx::bold(hot);
				gfx::text(cx, cy + cardH + (hot ? 22.0f : 12.0f), 19, hot ? kTvWhite : kTvSoft, entry.name, cardW);
				gfx::bold(false);
				if (!g_detail)
					clickable(cx, cy, cardW, cardH + 40, [r, i, channel] {
						g_tvOnRail = g_tvWallOnChips = false;
						const std::vector<TvWallRow> now = tvWallRows();
						if (r < (int)now.size())
						{
							tvWallGoTo(r, now);
							if (i < (int)g_view.size() && g_view[(size_t)i] == channel)
								g_sel = i;
							openDetails(channel);
						}
					});
			}
	}
	unclipAll();
	if (g_popFor != g_tvWallRow[tab] * 100000 + g_sel)
	{
		g_popFor = g_tvWallRow[tab] * 100000 + g_sel;
		g_cardPop = 1;
	}
	glide(g_cardPop, 1.08f, 18.0f);

	// fetch the row below as soon as this one is in, so it is there before it is reached
	if ((focusRow.kind != 2 || g_groupState[(size_t)focusRow.cat] == 2) && g_genreLoading < 0 && g_tvWallRow[tab] + 1 < (int)rows.size())
	{
		const TvWallRow& below = rows[(size_t)g_tvWallRow[tab] + 1];
		if (below.kind == 2 && g_groupState[(size_t)below.cat] == 0 && g_playlist.lazy)
			startGenreLoading(below.cat);
	}

	if (!g_detail)
		tvDrawFocus();
	drawTvRail();
	gfx::setRadiusScale(keepScale);
}

static void tvWallInput(Action action)
{
	if (tvRailInput(action))
		return;
	if (action == A_SEARCH) { openFilter(); return; }
	if (action == A_MORE) { g_focus = F_CHANNELS; openMore(); return; }
	if (T->state != 2)
	{
		if (action == A_LEFT || action == A_BACK)
			tvOpenRail();
		return;
	}
	const int tab = (int)(T - g_tabs);
	const std::vector<TvWallRow> rows = tvWallRows();
	if (rows.empty())
	{
		if (action == A_LEFT || action == A_BACK)
			tvOpenRail();
		return;
	}
	g_focus = F_CHANNELS;
	const int only = tvWallOnlyIndex(rows);
	if (g_tvWallOnChips)
	{
		switch (action)
		{
		case A_LEFT:
			if (g_tvWallChip > 0)
				g_tvWallChip--;
			else
			{
				g_tvWallOnChips = false;
				tvOpenRail();
			}
			break;
		case A_RIGHT: if (g_tvWallChip < (int)rows.size()) g_tvWallChip++; break;
		case A_OK: tvWallPressChip(g_tvWallChip, rows); break;
		case A_DOWN:
		case A_BACK: g_tvWallOnChips = false; break;
		default: break;
		}
		return;
	}
	const int count = (int)g_view.size();
	const TvWallRow& row = rows[(size_t)(g_tvWallRow[tab] < (int)rows.size() ? g_tvWallRow[tab] : 0)];
	if (only >= 0)
	{
		// one category shown by itself: a wall to move around in; back for every row again
		const int columns = kTvGridColumns;
		switch (action)
		{
		case A_BACK: g_tvWallOnly[tab] = kTvNoRow; break;
		case A_UP:
			if (g_sel - columns >= 0)
				g_sel -= columns;
			else
			{
				g_tvWallOnChips = true;
				g_tvWallChip = only + 1;
			}
			break;
		case A_DOWN:
			if (g_sel + columns < count)
				g_sel += columns;
			else if (count > 0 && g_sel / columns < (count - 1) / columns)
				g_sel = count - 1;
			break;
		case A_LEFT:
			if (count > 0 && g_sel % columns != 0)
				g_sel--;
			else
				tvOpenRail();
			break;
		case A_RIGHT: if (g_sel + 1 < count) g_sel++; break;
		case A_PAGE_UP:
		case A_PAGE_DOWN:
		{
			// L1 / R1: the category before or after this one, also by itself
			const int next = (only + (int)rows.size() + (action == A_PAGE_UP ? -1 : 1)) % (int)rows.size();
			tvWallPressChip(next + 1, rows);
			break;
		}
		case A_OK:
			if (count == 0 && row.kind == 2 && g_groupState[(size_t)row.cat] == 3)
				g_groupState[(size_t)row.cat] = 0;
			else if (selectedChannel() >= 0)
				openDetails(selectedChannel());
			break;
		case A_FAVOURITE: toggleFavourite(selectedChannel()); break;
		default: break;
		}
		return;
	}
	switch (action)
	{
	case A_UP:
		if (g_tvWallRow[tab] == 0)
		{
			g_tvWallOnChips = true;
			g_tvWallChip = 1;                           // on this row's own chip: Cross shows it by itself
		}
		else
			tvWallGoTo(g_tvWallRow[tab] - 1, rows);
		break;
	case A_DOWN: tvWallGoTo(g_tvWallRow[tab] + 1, rows); break;
	case A_PAGE_UP: tvWallGoTo(g_tvWallRow[tab] > 3 ? g_tvWallRow[tab] - 3 : 0, rows); break;
	case A_PAGE_DOWN: tvWallGoTo(g_tvWallRow[tab] + 3 < (int)rows.size() ? g_tvWallRow[tab] + 3 : (int)rows.size() - 1, rows); break;
	case A_LEFT:
		if (g_sel > 0)
			g_sel--;
		else
			tvOpenRail();
		break;
	case A_RIGHT: if (g_sel < count - 1) g_sel++; break;
	case A_BACK: tvOpenRail(); break;
	case A_OK:
		if (count == 0 && row.kind == 2 && g_groupState[(size_t)row.cat] == 3)
			g_groupState[(size_t)row.cat] = 0;          // try again
		else if (selectedChannel() >= 0)
			openDetails(selectedChannel());
		break;
	case A_FAVOURITE: toggleFavourite(selectedChannel()); break;
	default: break;
	}
}

