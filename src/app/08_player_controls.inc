// ---- the player's on-screen controls ------------------------------------------------
// Shown over the picture in full screen. Films get a time bar and skip buttons; live TV gets
// channel buttons. Both get sound-track, subtitle and favourite buttons.

enum ControlButton { B_BACK5, B_BACK30, B_PLAY, B_FWD30, B_FWD5, B_PREV, B_NEXT, B_NEXT_EPISODE, B_FIT, B_AUDIO, B_SUBTITLES, B_FAVOURITE };
static const char* const kFitName[3] = { "Fit", "Zoom", "Stretch" };
static int g_ctlRow = 0;                        // films: 0 the time bar, 1 the buttons
static int g_ctlFocus = 0;                      // which button
static int g_trackMenu = 0, g_trackSel = 0;     // the open list: 0 none, 1 sound tracks, 2 subtitles
static bool g_cleanScreen = false;              // the viewer put everything away (Square): nothing but the picture

static std::vector<int> controlButtons()
{
	std::vector<int> buttons;
	if (g_nowVod)
		buttons = { B_BACK5, B_BACK30, B_PLAY, B_FWD30, B_FWD5 };
	else
		buttons = { B_PREV, B_NEXT };
	if (g_nowVod && hasNextEpisode())
		buttons.push_back(B_NEXT_EPISODE);
	buttons.push_back(B_FIT);
	buttons.push_back(B_AUDIO);
	buttons.push_back(B_SUBTITLES);
	if (g_playing >= 0)
		buttons.push_back(B_FAVOURITE);
	return buttons;
}

static bool controlsShown()
{
	return nowSeconds() < g_overlayUntil || g_trackMenu != 0;
}

static void showControls(double seconds = 5)
{
	if (!controlsShown())
	{
		// coming back from hidden: start on the time bar (films) or the first button
		g_ctlRow = g_nowVod ? 0 : 1;
		g_ctlFocus = g_nowVod ? 2 : 0;
	}
	g_cleanScreen = false;
	g_overlayUntil = nowSeconds() + seconds;
}

static void changeChannel(int by)
{
	if (g_nowVod || g_nowTab != g_tab || g_tab >= 3 || g_view.empty())
		return;
	moveSelection(g_sel, (int)g_view.size(), by);
	if (!isSeriesItem(g_playlist.channels[(size_t)g_view[g_sel]].url))
		startChannel(g_view[g_sel]);
}

static void pressControl(int button)
{
	switch (button)
	{
	case B_BACK5: player::seekBy(-300); break;
	case B_BACK30: player::seekBy(-30); break;
	case B_FWD30: player::seekBy(30); break;
	case B_FWD5: player::seekBy(300); break;
	case B_PLAY: player::pause(!player::paused()); break;
	case B_PREV: changeChannel(-1); break;
	case B_NEXT: changeChannel(1); break;
	case B_NEXT_EPISODE: playNextEpisode(); break;
	case B_FIT:
		g_pictureFit = (g_pictureFit + 1) % 3;
		saveSettings();
		toast(std::string("Picture: ") + kFitName[g_pictureFit]);
		break;
	case B_AUDIO:
	{
		const std::vector<player::Track> tracks = player::audioTracks();
		if (tracks.size() < 2)
		{
			toast(tracks.empty() ? "No sound tracks listed for this video" : "This video has one sound track");
			break;
		}
		g_trackMenu = 1;
		g_trackSel = 0;
		for (int i = 0; i < (int)tracks.size(); i++)
			if (tracks[(size_t)i].id == player::audioTrack())
				g_trackSel = i;
		break;
	}
	case B_SUBTITLES:
	{
		const std::vector<player::Track> tracks = player::subtitleTracks();
		if (tracks.empty())
		{
			toast("This video has no text subtitles");
			break;
		}
		g_trackMenu = 2;
		g_trackSel = 0;                             // "Off" is the first row
		for (int i = 0; i < (int)tracks.size(); i++)
			if (tracks[(size_t)i].id == player::subtitleTrack())
				g_trackSel = i + 1;
		break;
	}
	case B_FAVOURITE: if (g_playing >= 0) toggleFavourite(g_playing); break;
	default: break;
	}
}

static void chooseTrack(int row)
{
	if (g_trackMenu == 1)
	{
		const std::vector<player::Track> tracks = player::audioTracks();
		if (row >= 0 && row < (int)tracks.size())
			player::setAudioTrack(tracks[(size_t)row].id);
	}
	else if (g_trackMenu == 2)
	{
		const std::vector<player::Track> tracks = player::subtitleTracks();
		if (row == 0)
			player::setSubtitleTrack(-1);
		else if (row - 1 < (int)tracks.size())
			player::setSubtitleTrack(tracks[(size_t)row - 1].id);
	}
	g_trackMenu = 0;
	showControls();
}

// The symbols on the buttons, drawn from simple shapes so they are crisp at any size.
static void drawControlIcon(int button, float cx, float cy, float s, uint32_t colour)
{
	const auto arrow = [&](float x, float direction) {
		// one triangle of a "skip" symbol, pointing right (1) or left (-1)
		gfx::triangle(x - 0.20f * s * direction, cy - 0.26f * s, x - 0.20f * s * direction, cy + 0.26f * s, x + 0.20f * s * direction, cy, colour);
	};
	switch (button)
	{
	case B_PLAY:
		if (player::paused())
			gfx::triangle(cx - 0.20f * s, cy - 0.30f * s, cx - 0.20f * s, cy + 0.30f * s, cx + 0.32f * s, cy, colour);
		else
		{
			gfx::roundRect(cx - 0.22f * s, cy - 0.28f * s, 0.15f * s, 0.56f * s, 0.05f * s, colour);
			gfx::roundRect(cx + 0.07f * s, cy - 0.28f * s, 0.15f * s, 0.56f * s, 0.05f * s, colour);
		}
		break;
	case B_FWD30: arrow(cx - 0.14f * s, 1); arrow(cx + 0.20f * s, 1); break;
	case B_BACK30: arrow(cx + 0.14f * s, -1); arrow(cx - 0.20f * s, -1); break;
	case B_FWD5:
		arrow(cx - 0.22f * s, 1); arrow(cx + 0.10f * s, 1);
		gfx::roundRect(cx + 0.30f * s, cy - 0.26f * s, 0.09f * s, 0.52f * s, 0.03f * s, colour);
		break;
	case B_BACK5:
		arrow(cx + 0.22f * s, -1); arrow(cx - 0.10f * s, -1);
		gfx::roundRect(cx - 0.39f * s, cy - 0.26f * s, 0.09f * s, 0.52f * s, 0.03f * s, colour);
		break;
	case B_PREV:
		gfx::triangle(cx - 0.30f * s, cy + 0.14f * s, cx + 0.30f * s, cy + 0.14f * s, cx, cy - 0.26f * s, colour);
		break;
	case B_NEXT:
		gfx::triangle(cx - 0.30f * s, cy - 0.14f * s, cx + 0.30f * s, cy - 0.14f * s, cx, cy + 0.26f * s, colour);
		break;
	case B_NEXT_EPISODE:
		gfx::triangle(cx - 0.24f * s, cy - 0.28f * s, cx - 0.24f * s, cy + 0.28f * s, cx + 0.16f * s, cy, colour);
		gfx::roundRect(cx + 0.18f * s, cy - 0.28f * s, 0.10f * s, 0.56f * s, 0.03f * s, colour);
		break;
	case B_FIT:
	{
		// the four corners of a frame
		const float a = 0.30f * s, t = 0.07f * s, arm = 0.17f * s;
		for (int corner = 0; corner < 4; corner++)
		{
			const float sx = corner % 2 ? 1.0f : -1.0f, sy = corner / 2 ? 1.0f : -1.0f;
			gfx::rect(sx > 0 ? cx + a - arm : cx - a, sy > 0 ? cy + a - t : cy - a, arm, t, colour);
			gfx::rect(sx > 0 ? cx + a - t : cx - a, sy > 0 ? cy + a - arm : cy - a, t, arm, colour);
		}
		break;
	}
	case B_AUDIO:
		// a loudspeaker with two sound bars
		gfx::rect(cx - 0.34f * s, cy - 0.11f * s, 0.16f * s, 0.22f * s, colour);
		gfx::triangle(cx - 0.20f * s, cy - 0.11f * s, cx + 0.02f * s, cy - 0.30f * s, cx + 0.02f * s, cy + 0.30f * s, colour);
		gfx::triangle(cx - 0.20f * s, cy - 0.11f * s, cx - 0.20f * s, cy + 0.11f * s, cx + 0.02f * s, cy + 0.30f * s, colour);
		gfx::roundRect(cx + 0.12f * s, cy - 0.13f * s, 0.07f * s, 0.26f * s, 0.03f * s, colour);
		gfx::roundRect(cx + 0.27f * s, cy - 0.24f * s, 0.07f * s, 0.48f * s, 0.03f * s, colour);
		break;
	case B_SUBTITLES:
		// a caption box with two lines of text
		gfx::panel(cx - 0.36f * s, cy - 0.25f * s, 0.72f * s, 0.50f * s, 0.09f * s, 0.07f * s, colour, 0);
		gfx::roundRect(cx - 0.23f * s, cy - 0.08f * s, 0.30f * s, 0.06f * s, 0.03f * s, colour);
		gfx::roundRect(cx + 0.11f * s, cy - 0.08f * s, 0.12f * s, 0.06f * s, 0.03f * s, colour);
		gfx::roundRect(cx - 0.23f * s, cy + 0.05f * s, 0.14f * s, 0.06f * s, 0.03f * s, colour);
		gfx::roundRect(cx - 0.05f * s, cy + 0.05f * s, 0.28f * s, 0.06f * s, 0.03f * s, colour);
		break;
	case B_FAVOURITE:
	{
		const bool on = g_playing >= 0 && g_isFavourite[(size_t)g_playing];
		const char* const star = on ? "\xE2\x98\x85" : "\xE2\x98\x86";
		gfx::text(cx - gfx::textWidth(0.62f * s, star) / 2, cy - 0.40f * s, 0.62f * s, colour, star);
		break;
	}
	default: break;
	}
}

// Subtitles: white words with a dark edge, centred, their last line resting on `bottom`.
// A subtitle that is a picture (from a film disc, or a broadcast): drawn where its maker put it,
// scaled to the area the video fills.
static void drawSubtitlePicture(float areaX, float areaY, float areaW, float areaH)
{
	static int image = -1, shown = 0, sw = 0, sh = 0, sx = 0, sy = 0, refW = 1920, refH = 1080;
	const int serial = player::subtitlePictureSerial();
	if (serial == 0)
		return;
	if (serial != shown)
	{
		std::vector<uint8_t> rgba;
		if (!player::subtitlePicture(serial, rgba, sw, sh, sx, sy, refW, refH) || rgba.empty())
			return;
		if (image < 0)
			image = gfx::imageCreate(rgba.data(), sw, sh, true);
		else
			gfx::imageUpdate(image, rgba.data(), sw, sh);
		shown = serial;
	}
	if (image < 0 || refW <= 0 || refH <= 0)
		return;
	// the video's own frame, fitted inside the area
	float w = areaW, h = areaW * (float)refH / (float)refW;
	if (h > areaH)
	{
		h = areaH;
		w = areaH * (float)refW / (float)refH;
	}
	const float left = areaX + (areaW - w) / 2, top = areaY + (areaH - h) / 2, scale = w / (float)refW;
	gfx::image(image, left + (float)sx * scale, top + (float)sy * scale, (float)sw * scale, (float)sh * scale);
}

static void drawSubtitles(float x, float w, float bottom, float size)
{
	const std::string words = player::subtitleText();
	if (words.empty())
		return;
	// split into lines, wrapping any that are too wide
	std::vector<std::string> lines;
	size_t pos = 0;
	while (pos <= words.size() && lines.size() < 6)
	{
		size_t end = words.find('\n', pos);
		if (end == std::string::npos)
			end = words.size();
		std::string line = words.substr(pos, end - pos);
		pos = end + 1;
		while (gfx::textWidth(size, line) > w * 0.86f)
		{
			size_t cut = line.size();
			while (cut > 0 && (line[cut - 1] != ' ' || gfx::textWidth(size, line.substr(0, cut)) > w * 0.86f))
				cut--;
			if (cut == 0)
				break;
			lines.push_back(line.substr(0, cut - 1));
			line = line.substr(cut);
		}
		if (!line.empty())
			lines.push_back(line);
	}
	const float lineH = size * 1.28f, edge = size / 18 < 1.5f ? 1.5f : size / 18;
	float y = bottom - lineH * (float)lines.size();
	for (const std::string& line : lines)
	{
		const float lx = x + (w - gfx::textWidth(size, line)) / 2;
		static const float around[8][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -0.7f, -0.7f }, { 0.7f, -0.7f }, { -0.7f, 0.7f }, { 0.7f, 0.7f } };
		for (const auto& offset : around)
			gfx::text(lx + offset[0] * edge, y + offset[1] * edge, size, 0x000000e0, line);
		gfx::text(lx, y + edge * 1.2f, size, 0x00000090, line);     // a little shadow underneath
		gfx::text(lx, y, size, 0xffffffff, line);
		y += lineH;
	}
}

static void drawControls(const std::string& status)
{
	const float keepScale = gfx::radiusScale();
	gfx::setRadiusScale(1.0f);                      // these are round whatever the theme
	const bool shown = controlsShown();
	const uint32_t white = 0xffffffff, soft = 0xffffffb0, dark = 0x0b0e14ff;

	gfx::rect(0, 0, 1920, 1080, 0x000000ff);
	gfx::videoDraw(0, 0, 1920, 1080, g_pictureFit);
	clickable(0, 0, 1920, 1080, [] {
		if (controlsShown())
		{
			g_overlayUntil = 0;                     // a click on the picture puts the controls away
			g_cleanScreen = true;
		}
		else
			showControls();
	});
	if (pointerOn() && !shown && !g_cleanScreen)
		showControls(3);

	static const float subtitleSizes[3] = { 34, 44, 58 };
	drawSubtitles(0, 1920, shown ? 800.0f : 1030.0f, subtitleSizes[g_subtitleSize]);
	drawSubtitlePicture(0, 0, 1920, 1080);

	// What is happening (connecting, paused, an error), unless the viewer asked for a clean screen
	// and it is only the pause notice.
	if (!status.empty() && !(g_cleanScreen && status == "Paused"))
	{
		const float w = gfx::textWidth(38, status) + 90;
		gfx::roundRect((1920 - w) / 2, 486, w, 84, 42, 0x000000b0);
		centredTight((1920 - w) / 2, w, 507, 38, white, status);
	}
	if (!shown)
	{
		gfx::setRadiusScale(keepScale);
		return;
	}

	// soft shading top and bottom, so the controls read over any picture
	for (int i = 0; i < 48; i++)
	{
		// many thin bands, each a little darker than the last, read as one smooth fade
		const float t = (float)i / 47.0f;
		gfx::rect(0, (float)i * 4, 1920, 4, (uint32_t)(150.0f * (1 - t) * (1 - t)));
		gfx::rect(0, 1080 - 336 + (float)i * 7, 1920, 7, (uint32_t)(225.0f * t * sqrtf(t)));
	}

	// top: back, title, where it is from
	const bool backHover = hovering(44, 30, 60, 60);
	gfx::panel(44, 30, 60, 60, 30, 2, backHover ? kBorder : 0xffffff50, backHover ? 0xffffff30 : 0xffffff18);
	gfx::triangle(82, 44, 82, 76, 62, 60, white);
	clickable(44, 30, 60, 60, [] { leaveFullscreen(); });
	float tx = 126;
	if (!g_nowVod)
	{
		gfx::roundRect(tx, 44, 76, 32, 16, 0xe5484dff);
		centredTight(tx, 76, 50, 19, white, "LIVE");
		tx += 92;
	}
	gfx::text(tx, 36, 38, white, g_now.name, 1700 - tx);
	gfx::text(tx, 84, 22, soft, g_now.group, 1700 - tx);

	const bool film = g_nowVod;
	if (film)
	{
		// the time bar
		const double length = player::duration(), at = player::position();
		const float bx = 200, bw = 1520, by = 884;
		const float done = length > 0 ? (float)(at / length > 1 ? 1 : at / length) : 0;
		const bool barFocus = (!pointerOn() && g_ctlRow == 0 && g_trackMenu == 0) || hovering(bx - 10, by - 22, bw + 20, 52);
		const float h = barFocus ? 12.0f : 8.0f;
		gfx::text(bx - 26 - gfx::textWidth(24, clockText(at)), by - 12, 24, white, clockText(at));
		gfx::text(bx + bw + 26, by - 12, 24, soft, clockText(length));
		gfx::roundRect(bx, by - h / 2 + 4, bw, h, h / 2, 0xffffff40);
		if (done > 0.002f)
			gfx::roundRect(bx, by - h / 2 + 4, bw * done, h, h / 2, kBorder);
		const float knob = barFocus ? 15.0f : 9.0f, kx = bx + bw * done;
		if (barFocus)
			gfx::roundRect(kx - knob - 9, by + 4 - knob - 9, (knob + 9) * 2, (knob + 9) * 2, knob + 9, (kBorder & 0xffffff00) | 0x48);
		gfx::roundRect(kx - knob, by + 4 - knob, knob * 2, knob * 2, knob, white);
		clickable(bx - 10, by - 22, bw + 20, 52, [bx, bw, length] {
			const float fraction = (g_pointerX - bx) / bw;
			player::seekTo(length * (double)(fraction < 0 ? 0 : fraction > 1 ? 1 : fraction));
			g_ctlRow = 0;
			showControls();
		});
	}

	// the buttons
	const std::vector<int> buttons = controlButtons();
	static const char* const labels[] = { "5 min", "30 s", "", "30 s", "5 min", "Previous", "Next", "Next episode", "Picture", "Audio", "Subtitles", "Favorite" };
	const float cy = 986;
	int centre = 0;
	for (const int button : buttons)
		if (button <= B_NEXT)
			centre++;
	float trackX[2] = { 0, 0 };
	for (int i = 0; i < (int)buttons.size(); i++)
	{
		const int button = buttons[(size_t)i];
		const bool hero = button == B_PLAY;
		const float size = hero ? 92.0f : 70.0f;
		float cx;
		if (button <= B_NEXT)
			cx = 960 + ((float)i - (float)(centre - 1) / 2) * (film ? 124.0f : 150.0f);     // the transport buttons, centred
		else
			cx = 1836 - (float)((int)buttons.size() - 1 - i) * 118;                        // the rest, at the right
		if (button == B_AUDIO) trackX[0] = cx;
		if (button == B_SUBTITLES) trackX[1] = cx;
		const bool focus = (!pointerOn() && g_trackMenu == 0 && (g_ctlRow == 1 || !film) && i == g_ctlFocus) || hovering(cx - size / 2, cy - size / 2, size, size);
		if (focus)
			gfx::roundRect(cx - size / 2 - 10, cy - size / 2 - 10, size + 20, size + 20, size / 2 + 10, (kBorder & 0xffffff00) | 0x50);   // glow
		const uint32_t fill = focus ? kBorder : hero ? white : 0xffffff22;
		gfx::panel(cx - size / 2, cy - size / 2, size, size, size / 2, 2, focus || hero ? 0 : 0xffffff55, fill);
		drawControlIcon(button, cx, cy, hero ? size * 0.80f : size * 0.72f, focus || hero ? dark : white);
		if (labels[button][0])
			centredTight(cx - 60, 120, cy + size / 2 + 8, 17, focus ? white : soft, labels[button]);
		clickable(cx - size / 2, cy - size / 2, size, size, [i, button] {
			g_ctlRow = 1;
			g_ctlFocus = i;
			showControls();
			pressControl(button);
		});
	}

	// the list of sound tracks or subtitles, above its button
	if (g_trackMenu != 0)
	{
		std::vector<std::string> rows;
		int current = 0;
		if (g_trackMenu == 1)
		{
			const std::vector<player::Track> tracks = player::audioTracks();
			for (int i = 0; i < (int)tracks.size(); i++)
			{
				rows.push_back(tracks[(size_t)i].label);
				if (tracks[(size_t)i].id == player::audioTrack())
					current = i;
			}
		}
		else
		{
			const std::vector<player::Track> tracks = player::subtitleTracks();
			rows.push_back("Off");
			for (int i = 0; i < (int)tracks.size(); i++)
			{
				rows.push_back(tracks[(size_t)i].label);
				if (tracks[(size_t)i].id == player::subtitleTrack())
					current = i + 1;
			}
		}
		const int count = (int)rows.size(), visible = count < 9 ? count : 9;
		if (g_trackSel > count - 1) g_trackSel = count - 1;
		const int first = g_trackSel < visible ? 0 : g_trackSel - visible + 1;
		const float w = 560, rowH = 56, h = 70 + (float)visible * rowH;
		float x = trackX[g_trackMenu - 1] - w / 2;
		if (x + w > 1890) x = 1890 - w;
		const float y = 880 - h;
		clickable(0, 0, 1920, 1080, [] { g_trackMenu = 0; });       // a click outside closes it
		gfx::panel(x, y, w, h, 20, 2, 0xffffff40, 0x12151cf4);
		clickable(x, y, w, h, [] {});
		gfx::text(x + 26, y + 18, 24, soft, g_trackMenu == 1 ? "Audio" : "Subtitles");
		for (int row = 0; row < visible; row++)
		{
			const int i = first + row;
			const float ry = y + 58 + (float)row * rowH;
			const bool selected = (!pointerOn() && i == g_trackSel) || hovering(x + 10, ry, w - 20, rowH - 6);
			if (selected)
				gfx::roundRect(x + 10, ry, w - 20, rowH - 6, 12, kBorder);
			if (i == current)
				gfx::text(x + 28, ry + 11, 26, selected ? dark : kBorder, "\xE2\x9C\x93");
			gfx::text(x + 66, ry + 11, 25, selected ? dark : white, rows[(size_t)i], w - 96);
			clickable(x + 10, ry, w - 20, rowH - 6, [i] { chooseTrack(i); });
		}
	}
	gfx::setRadiusScale(keepScale);
}

static void showControlsSoon()
{
	g_trackMenu = 0;
	g_cleanScreen = false;
	showControls(4);
}

static void controlsInput(Action action)
{
	if (g_trackMenu != 0)
	{
		const int count = g_trackMenu == 1 ? (int)player::audioTracks().size() : (int)player::subtitleTracks().size() + 1;
		switch (action)
		{
		case A_UP: moveSelection(g_trackSel, count, -1); break;
		case A_DOWN: moveSelection(g_trackSel, count, 1); break;
		case A_OK: chooseTrack(g_trackSel); break;
		case A_BACK:
		case A_LEFT:
		case A_RIGHT: g_trackMenu = 0; showControls(); break;
		default: break;
		}
		return;
	}

	if (g_nowVod && hasNextEpisode() && player::state() == player::ENDED)
	{
		if (action == A_OK)
		{
			playNextEpisode();
			return;
		}
		if (action == A_BACK && g_nextAt != 0 && !g_nextCancelled)
		{
			g_nextCancelled = true;                     // stay on the finished episode
			return;
		}
	}
	const bool film = g_nowVod, shown = controlsShown();
	const std::vector<int> buttons = controlButtons();
	const int count = (int)buttons.size();
	if (g_ctlFocus > count - 1)
		g_ctlFocus = count - 1;
	switch (action)
	{
	case A_BACK: leaveFullscreen(); return;
	case A_MENU: openManager(""); return;
	case A_FAVOURITE:
		// Square: everything off the screen at once, or back again
		if (shown)
		{
			g_overlayUntil = 0;
			g_trackMenu = 0;
			g_cleanScreen = true;
			g_toastUntil = 0;
			return;
		}
		break;
	case A_PAGE_UP: if (film) player::seekBy(-300); break;
	case A_PAGE_DOWN: if (film) player::seekBy(300); break;
	case A_LAST: if (!film) lastChannel(); break;
	case A_LEFT:
	case A_RIGHT:
	{
		const int by = action == A_LEFT ? -1 : 1;
		if (film && (!shown || g_ctlRow == 0))
			player::seekBy(30.0 * by);              // on the time bar (or with the controls away): skip
		else if (shown)
			g_ctlFocus = (g_ctlFocus + count + by) % count;
		break;
	}
	case A_UP:
		if (film)
			g_ctlRow = 0;
		else
			changeChannel(-1);
		break;
	case A_DOWN:
		if (film)
		{
			if (shown)
				g_ctlRow = 1;
		}
		else
			changeChannel(1);
		break;
	case A_OK:
		if (film && (!shown || g_ctlRow == 0))
			player::pause(!player::paused());
		else if (shown)
			pressControl(buttons[(size_t)g_ctlFocus]);
		break;
	default: break;
	}
	// A clean screen stays clean while skipping or pausing; Square (or the pointer) brings the controls back.
	if (!g_cleanScreen || action == A_FAVOURITE)
		showControls();
}

