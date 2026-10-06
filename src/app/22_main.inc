// ---- start-up --------------------------------------------------------------

static SDL_Window* createWindow(int& width, int& height)
{
	SDL_DisplayMode mode = {};
	if (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0)
	{
		width = mode.w;
		height = mode.h;
	}
	logLine("video: display reported as %dx%d at %d Hz", width, height, mode.refresh_rate);
	const int modes = SDL_GetNumDisplayModes(0);
	for (int i = 0; i < modes && i < 12; i++)
	{
		SDL_DisplayMode offered;
		if (SDL_GetDisplayMode(0, i, &offered) == 0)
			logLine("video: the display offers %dx%d at %d Hz", offered.w, offered.h, offered.refresh_rate);
	}
	if (g_resolution == 1)
	{
		width = 1920;
		height = 1080;
	}
	else if (g_resolution == 2)
	{
		width = 3840;
		height = 2160;
	}
	if (g_resolution != 0)
		logLine("video: asking for %dx%d (chosen in Settings)", width, height);

	// The PS5 OpenGL layer offers desktop OpenGL; other requests are fallbacks.
	static const int attempts[4][3] = {
		{ SDL_GL_CONTEXT_PROFILE_CORE, 4, 6 }, { SDL_GL_CONTEXT_PROFILE_CORE, 3, 3 }, { SDL_GL_CONTEXT_PROFILE_ES, 3, 0 }, { 0, 0, 0 } };
	for (const auto& attempt : attempts)
	{
		SDL_GL_ResetAttributes();
		if (attempt[1] != 0)
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, attempt[0]);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, attempt[1]);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, attempt[2]);
		}
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_Window* window = SDL_CreateWindow("IPTV", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
		if (!window)
		{
			logLine("video: no window with OpenGL %d.%d: %s", attempt[1], attempt[2], SDL_GetError());
			continue;
		}
		if (SDL_GL_CreateContext(window))
		{
			logLine("video: OpenGL %d.%d context created", attempt[1], attempt[2]);
			return window;
		}
		logLine("video: no OpenGL %d.%d context: %s", attempt[1], attempt[2], SDL_GetError());
		SDL_DestroyWindow(window);
	}
	return nullptr;
}

static void scrollBy(int steps)
{
	if (steps == 0 || g_keyboard || g_form || g_screen == LOADING)
		return;
	const auto move = [steps](int& selected, int count) {
		if (count <= 0)
			return;
		selected += steps;
		if (selected < 0) selected = 0;
		if (selected > count - 1) selected = count - 1;
	};
	if (g_screen == MANAGER)
	{
		if (g_mgrTab == 4)
		{
			g_playSel += steps > 0 ? 1 : -1;
			if (g_playSel < 0) g_playSel = 0;
			if (g_playSel > kPlayRows - 1) g_playSel = kPlayRows - 1;
		}
		else if (g_mgrTab == 3)
		{
			g_ptrSel += steps > 0 ? 1 : -1;
			if (g_ptrSel < 0) g_ptrSel = 0;
			if (g_ptrSel > kPtrRows - 1) g_ptrSel = kPtrRows - 1;
		}
		else if (g_mgrTab == 2)
		{
			if (g_hideColumn == 0)
			{
				move(g_hideCountry, (int)g_countries.size());
				g_hideSel = g_hideTop = 0;
			}
			else
			{
				g_hideColumn = 1;
				move(g_hideSel, groupsMiddleCount(g_tabs[g_hideTab]));
			}
		}
		else if (g_mgrTab == 1)
			move(g_themeSel, kThemeCount);
		else if (!g_sources.empty())
		{
			g_mgrOnButtons = false;
			move(g_mgrSel, (int)g_sources.size());
		}
		return;
	}
	if (g_fullscreen)
		return;
	if (g_tab == kHomeTab)
	{
		if (!g_home.empty())
		{
			int row = g_homeRow + (steps > 0 ? 1 : -1);
			if (row < 0) row = 0;
			if (row > (int)g_home.size() - 1) row = (int)g_home.size() - 1;
			g_homeRow = row;
		}
		return;
	}
	if (g_detail)
	{
		if (T->drillState == 2 && !T->drillList.empty())
		{
			g_detailOnEpisodes = true;
			move(g_episodeSel, (int)T->drillList.size());
		}
		return;
	}
	if (g_tab == kAccountTab)
		return;
	if ((g_tab == 1 || g_tab == 2) && !(inGroups() && (pointerOn() ? g_pointerX >= 90 && g_pointerX < 490 : g_focus == F_CATEGORIES)))
	{
		// the poster wall moves a row at a time
		const int count = (int)g_view.size(), columns = g_gridColumns > 0 ? g_gridColumns : 1;
		int target = g_sel + (steps > 0 ? columns : -columns);
		if (target >= 0 && target < count)
			g_sel = target;
		else if (target >= count && count > 0 && g_sel / columns < (count - 1) / columns)
			g_sel = count - 1;
		return;
	}
	const bool categories = inGroups() && (pointerOn() ? g_pointerX >= 90 && g_pointerX < 490 : g_focus == F_CATEGORIES);
	if (categories)
	{
		int place = catPlace();
		move(place, (int)T->cats.size());
		selectCategory(T->cats[(size_t)place]);
	}
	else
		move(g_sel, (int)g_view.size());
}

int es_main(int, char**)
{
	g_dataDir = usableFolder("/data/iptv") ? "/data/iptv" : "/app0";
	{
		// keep the log of the run before this one, for looking into problems afterwards
		std::string previous;
		if (readTextFile(g_dataDir + "/iptv-log.txt", previous) && !previous.empty())
			writeTextFile(g_dataDir + "/iptv-log-previous.txt", previous);
	}
	g_log = fopen((g_dataDir + "/iptv-log.txt").c_str(), "wb");
	logLine("IPTV for PS5 starting; data folder: %s", g_dataDir.c_str());
	logMissingSystemFunctions();
	mkdir((g_dataDir + "/playlists").c_str(), 0777);

	if (SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		logLine("SDL video did not start: %s", SDL_GetError());
		return 1;
	}
	if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0)
		logLine("input: joysticks unavailable: %s", SDL_GetError());
	if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
		logLine("input: controller layouts unavailable: %s", SDL_GetError());

	loadSettings();
	loadProfiles();
	loadThemeFiles(g_dataDir);
	g_resolution = 0;       // the graphics layer on this console draws at 1080p only; the choice was removed
	// A resolution chosen in Settings is tried once. If the app did not get as far as showing
	// pictures with it last time, it goes back to automatic so it cannot get stuck failing to start.
	const std::string trying = g_dataDir + "/resolution-trying.txt";
	std::string marker;
	if (readTextFile(trying, marker))
	{
		logLine("video: the resolution chosen in Settings did not work last time; back to automatic");
		g_resolution = 0;
		saveSettings();
		remove(trying.c_str());
	}
	if (g_resolution != 0)
		writeTextFile(trying, "1");

	int width = 1920, height = 1080;
	SDL_Window* window = createWindow(width, height);
	if (!window && g_resolution != 0)
	{
		logLine("video: that resolution was refused; using the display's own");
		g_resolution = 0;
		saveSettings();
		width = 1920;
		height = 1080;
		window = createWindow(width, height);
	}
	if (!window)
	{
		logLine("video: no window could be created");
		return 1;
	}
	if (SDL_GL_SetSwapInterval(1) != 0)
		logLine("video: could not tie drawing to the screen's refresh: %s", SDL_GetError());
	int drawW = width, drawH = height;
	SDL_GL_GetDrawableSize(window, &drawW, &drawH);
	g_drawW = drawW;
	g_drawH = drawH;
	player::setDisplaySize(drawW, drawH);
	logLine("video: drawing at %dx%d", drawW, drawH);

	{
		// the theme chosen last time
		std::string saved;
		int index = 0;
		if (readTextFile(g_dataDir + "/theme.txt", saved))
			for (int i = 0; i < kThemeCount; i++)
				if (trim(saved) == kThemes[i].name)
					index = i;
		applyTheme(index);
		logLine("theme: %s", kThemes[index].name);
		applyLook();                                // the TV layout, if chosen, has its own look
	}

	std::string error;
	if (!gfx::init(drawW, drawH, "/app0/assets/font.ttf", error))
	{
		logLine("gfx: %s", error.c_str());
		return 1;
	}

	logosStart(g_dataDir);
	// How fast this console's graphics layer takes in pictures decides how large video pictures can be.
	g_uploadRate = gfx::measureUploads();
	applyVideoBudget();

	for (int i = 0; i < SDL_NumJoysticks(); i++)
		openPad(i);
	player::init();

	// The saved playlists, plus any files found on the console. The one used last opens by itself.
	g_sources = loadSources(g_dataDir);
	saveSources(g_dataDir, g_sources);
	logLine("playlists: %d known", (int)g_sources.size());
	if (g_sources.empty())
		openManager("");
	else
	{
		int index = 0;
		std::string last;
		if (readTextFile(g_dataDir + "/active.txt", last))
			for (int i = 0; i < (int)g_sources.size(); i++)
				if (sourceId(g_sources[(size_t)i]) == trim(last))
					index = i;
		startLoading(index);
	}
	logLine("start-up finished; entering the main loop");

	bool running = true;
	double before = nowSeconds();
	// frame timing: measured every second for the display in Settings, logged every ten
	int framesThisSecond = 0, framesLogged = 0, shown = 0;
	double secondStart = before, logStart = before, slowest = 0, slowestLogged = 0, frameStart = before;
	while (running)
	{
		{
			const double t = nowSeconds();
			const double took = (t - frameStart) * 1000.0;
			frameStart = t;
			if (took > slowest) slowest = took;
			if (took > slowestLogged) slowestLogged = took;
			framesThisSecond++;
			framesLogged++;
			if (t - secondStart >= 1.0)
			{
				g_fps = framesThisSecond / (t - secondStart);
				g_slowestMs = slowest;
				framesThisSecond = 0;
				slowest = 0;
				secondStart = t;
			}
			if (t - logStart >= 10.0)
			{
				int decoded = 0, picturesShown = 0, picturesSkipped = 0;
				player::takeCounts(decoded, picturesShown, picturesSkipped);
				logLine("frames: %.1f per second, slowest %.0f ms, %d draw batches, drawing at %dx%d; video: %.1f decoded, %.1f shown, %.1f skipped per second, %.1f ms to hand over each picture",
					framesLogged / (t - logStart), slowestLogged, gfx::drawCalls(), g_drawW, g_drawH,
					decoded / (t - logStart), picturesShown / (t - logStart), picturesSkipped / (t - logStart),
					g_uploads ? g_uploadSeconds * 1000.0 / g_uploads : 0.0);
				g_uploadSeconds = 0;
				g_uploads = 0;
				framesLogged = 0;
				slowestLogged = 0;
				logStart = t;
			}
			if (shown < 200 && ++shown == 200 && g_resolution != 0)
				remove(trying.c_str());                 // the chosen resolution works
		}
		SDL_Event event;
		while (SDL_PollEvent(&event))
			onEvent(event, running);
		repeatDirections();
		const double now = nowSeconds();
		const double elapsed = now - before > 0.1 ? 0.1 : now - before;
		g_frameSeconds = elapsed > 0.0005 ? elapsed : 0.0005;
		movePointer(elapsed);
		scrollBy(scrollSteps(elapsed));
		before = now;

		if (g_screen == LOADING && g_loadState != 0)
		{
			if (g_loadState == 1)
			{
				stopGenreLoading();
				for (Tab& tab : g_tabs)
					tab = Tab();
				T = &g_tabs[0];
				g_tab = 0;
				{
					std::lock_guard<std::mutex> lock(g_loadMutex);
					g_playlist = std::move(g_loadedPlaylist);
					g_loadedPlaylist = Playlist();
				}
				buildGroups();
				onPlaylistLoaded();
				g_grid = false;
				{
					// an M3U playlist that names a guide: fetch it in the background
					const std::string& kind = g_sources[(size_t)g_active].type;
					if ((kind == "url" || kind == "file") && !g_tabs[0].playlist.epgUrl.empty())
						startXmltv(g_tabs[0].playlist.epgUrl);
					else
					{
						std::lock_guard<std::mutex> lock(g_xmltvMutex);
						g_xmltvRequest++;
						g_xmltv = XmltvGuide();
						g_xmltvState = 0;
					}
				}
				g_screen = BROWSE;
				// open on Home when there is something of the viewer's to show there
				buildHome();
				if (!g_home.empty())
					g_tab = kHomeTab;
				g_homeRow = 0;
				fadeIn();
			}
			else
				openManager(g_loadError);
		}
		if (g_screen == BROWSE)
		{
			// The server's list of groups arrived after the saved copy was put on screen, and differs.
			// It replaces the saved one if nothing has been opened yet; otherwise it waits for next time.
			Playlist fresh;
			bool ready = false;
			{
				std::lock_guard<std::mutex> lock(g_loadMutex);
				if (g_refreshedReady)
				{
					g_refreshedReady = false;
					ready = true;
					fresh = std::move(g_refreshedPlaylist);
					g_refreshedPlaylist = Playlist();
				}
			}
			if (ready)
			{
				bool touched = g_nowOn && g_nowTab == 0;
				for (const char state : g_tabs[0].groupState)
					if (state != 0)
						touched = true;
				if (touched)
					logLine("playlist: the new groups will be used from the next start");
				else
				{
					Tab* const showing = T;
					T = &g_tabs[0];
					g_playlist = std::move(fresh);
					buildGroups();
					initTab();
					rebuildView(-1);
					T = showing;
					logLine("playlist: groups updated from the server");
				}
			}
		}

		for (const Action action : g_actions)
		{
			if (g_menuSounds && !g_fullscreen && g_screen != LOADING)
				player::menuSound(action == A_OK ? 2 : action == A_BACK ? 3 : 1);
			if (g_screen == LOADING)
			{
				// Only one thing can be done while waiting: give up and go to Settings.
				// (Ignored for the first moment, so the press that started the load cannot cancel it.)
				if (nowSeconds() - g_loadStarted > 0.4)
				{
					if (action == A_BACK || action == A_MENU)
						cancelLoading();
					else if (action == A_OK && pointerOn())
					{
						clickPointer();
						g_hits.clear();
					}
				}
				continue;
			}
			if (action == A_OK && pointerOn())
			{
				clickPointer();
				g_hits.clear(); // what was on screen may have changed
			}
			else if (g_keyboard)
				keyboardInput(action);
			else if (g_form)
				formInput(action);
			else if (g_askChannel >= 0 && g_screen == BROWSE)
				resumeInput(action);
			else if (g_more && g_screen == BROWSE)
				moreInput(action);
			else if (g_screen == BROWSE)
				browseInput(action);
			else if (g_screen == MANAGER)
				managerInput(action);
		}
		g_actions.clear();

		g_hits.clear();
		logosPump();
		if (!(g_screen == MANAGER && g_mgrTab == 5))
		{
			if (web::running())
				web::stop();
			g_webTried = false;
		}
		gfx::begin(kBackground);
		drawBackground();
		if (g_screen == LOADING)
		{
			const int waited = (int)(nowSeconds() - g_loadStarted);
			char title[64];
			if (waited >= 3)
				snprintf(title, sizeof(title), "Loading the playlist...  %d s", waited);
			else
				snprintf(title, sizeof(title), "Loading the playlist...");
			spinner(960, 350, 34, kAccent);
			centredIn(0, 1920, 440, 44, kText, title);
			if (g_active >= 0)
				centredIn(0, 1920, 510, 26, kMuted, g_sources[(size_t)g_active].name);
			const long long received = httpReceivedBytes();
			if (received > 0)
			{
				char progress[64];
				snprintf(progress, sizeof(progress), "%.1f MB received", (double)received / 1048576.0);
				centredIn(0, 1920, 552, 22, kAccent, progress);
			}
			// the way out, always available
			const float bx = 720, by = 600, bw = 480, bh = 72;
			const bool hover = hovering(bx, by, bw, bh);
			gfx::panel(bx, by, bw, bh, 16, 2, hover ? kBorder : kBorderDim, hover ? kSelected : kChip);
			centredTight(bx, bw, by + 20, 26, hover ? kAccent : kText, "\xE2\x9A\x99   Cancel and open Settings");
			clickable(bx, by, bw, bh, [] { cancelLoading(); });
			centredIn(0, 1920, 700, 22, kMuted, "\xE2\x97\x8B or OPTIONS  Cancel and open Settings");
		}
		else if (g_screen == MANAGER)
			drawManager();
		else
			drawBrowse();
		if (g_screen != LOADING)
		{
			if (g_askChannel >= 0 && g_screen == BROWSE)
				drawResumeQuestion();
			if (g_more && g_screen == BROWSE && !g_fullscreen)
				drawMore();
			if (g_form)
				drawForm();
			if (g_keyboard)
				drawKeyboard();
		}
		{
			// a brief fade whenever the screen changes, so nothing snaps in
			const double since = nowSeconds() - g_fadeFrom;
			if (since >= 0 && since < 0.24 && !(g_fullscreen && g_nowOn))
			{
				const float left = 1.0f - (float)(since / 0.24);
				gfx::rect(0, 82, 1920, 1080 - 82, (kBackground & 0xffffff00) | (uint32_t)(150.0f * left * left));
			}
		}
		if (g_showFps)
		{
			char text[64];
			snprintf(text, sizeof(text), "%.0f fps   %dx%d", g_fps, g_drawW, g_drawH);
			const float w = gfx::textWidth(20, text) + 24;
			gfx::roundRect(1920 - w - 6, 4, w, 30, 8, 0x000000b0);
			gfx::text(1920 - w + 6, 9, 20, 0xffffffff, text);
		}
		drawPointer();
		gfx::end();
		SDL_GL_SwapWindow(window);
	}

	player::stop();
	return 0;
}

// A function the start-up file asks for that the console does not offer to a title.
extern "C" mode_t umask(mode_t mask) { (void)mask; return 0; }
