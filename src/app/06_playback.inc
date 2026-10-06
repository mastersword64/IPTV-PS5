// ---- libraries: setting one up, and loading movies and series when their tab is opened ---

// Prepares the current tab once its playlist is in place.
static void initTab()
{
	g_byUrl.clear();
	for (int i = (int)g_playlist.channels.size() - 1; i >= 0; i--)
		g_byUrl[g_playlist.channels[(size_t)i].url] = i;
	g_groupState.assign(g_playlist.groups.size(), g_playlist.lazy ? 0 : 2);
	g_isFavourite.assign(g_playlist.channels.size(), 0);
	loadHidden();
	loadHiddenChannels();
	loadWatched();
	loadResume();
	loadList("favourites", g_favourites);
	loadList("recent", g_recent);
	for (const int channel : g_favourites)
		g_isFavourite[(size_t)channel] = 1;

	// A library that is fetched a category at a time opens on its categories.
	g_section = g_playlist.lazy ? S_GROUPS : S_ALL;
	g_focus = g_playlist.lazy ? F_CATEGORIES : F_CHANNELS;
	g_catSel = g_catTop = 0;
	g_filter.clear();
	T->filterCats = false;
	T->drill = false;
	T->drillState = 0;
	computeCats();
	T->state = 2;
}

static std::mutex g_libMutex;
static int g_libRequest = 0, g_libKind = -1, g_libDone = 0;
static Playlist g_libArrived;
static std::string g_libError;

static void startLibrary(int kind)
{
	if (g_libKind > 0 && g_libKind != kind && g_tabs[g_libKind].state == 1)
		g_tabs[g_libKind].state = 0;                   // its load is being replaced by this one
	g_tabs[kind].state = 1;
	g_tabs[kind].error.clear();
	int request;
	{
		std::lock_guard<std::mutex> lock(g_libMutex);
		request = ++g_libRequest;
		g_libDone = 0;
	}
	g_libKind = kind;
	const Source source = g_sources[(size_t)g_active];
	logLine("library: loading %s", kTabName[kind]);
	startThread("library", [source, kind, request] {
		// the saved list of categories first, so the screen fills at once
		const std::string id = sourceId(source);
		Playlist saved;
		bool shown = false;
		if (loadGroupsCache(id, kind, saved))
		{
			std::lock_guard<std::mutex> lock(g_libMutex);
			if (request == g_libRequest)
			{
				g_libArrived = saved;
				g_libError.clear();
				g_libDone = 1;
				shown = true;
			}
		}
		Playlist fresh;
		std::string error;
		const bool ok = fetchLibrary(source, kind, g_dataDir, fresh, error);
		if (ok)
			saveGroupsCache(id, kind, fresh);
		std::lock_guard<std::mutex> lock(g_libMutex);
		if (request != g_libRequest || shown)
			return;                                    // (when shown from the saved copy, the new list is for next time)
		g_libArrived = std::move(fresh);
		g_libError = error;
		g_libDone = ok ? 1 : 2;
	});
}

static void buildGroups();
static void libraryArrived(bool onScreen);

static void pumpLibrary()
{
	Playlist fresh;
	std::string error;
	int done;
	{
		std::lock_guard<std::mutex> lock(g_libMutex);
		done = g_libDone;
		if (!done)
			return;
		g_libDone = 0;
		fresh = std::move(g_libArrived);
		g_libArrived = Playlist();
		error = g_libError;
	}
	Tab* const showing = T;
	T = &g_tabs[g_libKind];
	if (done == 1)
	{
		g_playlist = std::move(fresh);
		buildGroups();
		initTab();
		rebuildView(-1);
		libraryArrived(g_tab == g_libKind);
	}
	else
	{
		T->state = 3;
		T->error = error;
		logLine("library: %s", error.c_str());
	}
	T = showing;
}

static std::mutex g_accountMutex;
static int g_accountState = 0, g_accountRequest = 0;   // state: 1 fetching, 2 shown, 3 failed
static std::vector<std::pair<std::string, std::string>> g_accountRows;
static std::string g_accountError;

static void startAccount()
{
	int request;
	{
		std::lock_guard<std::mutex> lock(g_accountMutex);
		request = ++g_accountRequest;
		g_accountState = 1;
	}
	const Source source = g_sources[(size_t)g_active];
	startThread("account", [source, request] {
		std::vector<std::pair<std::string, std::string>> rows;
		std::string error;
		const bool ok = fetchAccount(source, rows, error);
		std::lock_guard<std::mutex> lock(g_accountMutex);
		if (request != g_accountRequest)
			return;
		g_accountRows = std::move(rows);
		g_accountError = error;
		g_accountState = ok ? 2 : 3;
	});
}

static void buildHome();
static void closeDetails();

static void switchTab(int tab)
{
	if (tab < 0 || tab >= kTabCount || tab == g_tab)
		return;
	stopGenreLoading();
	closeDetails();
	g_grid = false;
	if (T->drill && T->drillState == 1)
		closeDrill();
	g_tab = tab;
	fadeIn();
	if (tab == kHomeTab)
	{
		buildHome();
		return;
	}
	if (tab == kSearchTab)
	{
		if (g_searchText.empty())
		{
			// straight to typing
			g_searchTyping = true;
			openKeyboard(&g_searchText, 40, false, "Search");
		}
		return;
	}
	if (tab == kAccountTab)
	{
		if (g_accountState == 0 || g_accountState == 3)
			startAccount();
		return;
	}
	T = &g_tabs[tab];
	g_playing = g_nowOn && g_nowTab == tab ? g_nowIndex : -1;
	if (tab > 0 && (T->state == 0 || T->state == 3 || (T->state == 1 && g_libKind != tab)))
		startLibrary(tab);
}

static void switchTabBy(int by)
{
	int place = 0;
	for (int i = 0; i < kTabCount; i++)
		if (kTabOrder[i] == g_tab)
			place = i;
	switchTab(kTabOrder[(place + kTabCount + by) % kTabCount]);
}

static void forgetPlaybackState();

// Called when a playlist has finished loading: live TV is in tab 0, the others start empty.
static void onPlaylistLoaded()
{
	initTab();
	g_keyboard = false;
	g_playing = -1;
	g_nowOn = false;
	g_fullscreen = false;
	g_railOnPlaylists = false;
	g_haveBrowse = true;
	forgetPlaybackState();
	{
		std::lock_guard<std::mutex> lock(g_accountMutex);
		g_accountRequest++;
		g_accountState = 0;
		g_accountRows.clear();
	}

	// Start on the channel watched last time, if it is still in the playlist.
	int last = -1;
	std::string url;
	if (readTextFile(listPath("last"), url))
	{
		const auto found = g_byUrl.find(trim(url));
		if (found != g_byUrl.end())
			last = found->second;
	}
	rebuildView(last);
}

static void toggleFavourite(int channel)
{
	if (channel < 0)
		return;
	if (g_isFavourite[channel])
	{
		g_isFavourite[channel] = 0;
		for (size_t i = 0; i < g_favourites.size(); i++)
			if (g_favourites[i] == channel)
			{
				g_favourites.erase(g_favourites.begin() + (long)i);
				break;
			}
	}
	else
	{
		g_isFavourite[channel] = 1;
		g_favourites.push_back(channel);
	}
	saveList("favourites", g_favourites);
	if (g_section == S_FAVOURITES)
	{
		const int position = g_sel;
		rebuildView(-1);
		g_sel = position < (int)g_view.size() ? position : (int)g_view.size() - 1;
		if (g_sel < 0)
			g_sel = 0;
	}
}

// A Stalker channel's real address has to be asked for from the portal first, in the background.
static std::mutex g_linkMutex;
static int g_linkRequest = 0, g_linkState = 0;        // state: 0 none, 1 asking, 2 ready, 3 failed
static std::string g_linkUrl, g_linkError;

static void cancelLink()
{
	std::lock_guard<std::mutex> lock(g_linkMutex);
	g_linkRequest++;
	g_linkState = 0;
}

static std::string userAgentFor(const Channel& channel)
{
	const Source& source = g_sources[(size_t)g_active];
	if (!channel.userAgent.empty())
		return channel.userAgent;
	if (!source.ua.empty())
		return source.ua;
	return source.type == "stalker" || !source.mac.empty() ? kStalkerUserAgent : "";
}

static int g_lastLive = -1;                     // the live channel watched before the current one (for L3)
static bool g_playTemp = false, g_nowTemp = false;     // a trailer or a caught-up programme: not history, not resumed
static double g_startAt = 0;                    // where the next start should begin (resuming)
static double g_lastPosition = 0;               // how far the film playing has got
static std::vector<int> g_queue;                // the episodes of the series being watched, in order
static int g_queueAt = -1, g_queueTab = 0;      // which of them is playing, and the tab they belong to
static double g_nextAt = 0;                     // when the next episode starts by itself (0: not counting down)
static bool g_nextCancelled = false;
static int g_askChannel = -1, g_askSel = 0;     // the "resume or start again?" question
static double g_askPosition = 0;

static void beginPlayback(const Channel& channel)
{
	if (channel.url.compare(0, 10, "stalker://") != 0)
	{
		cancelLink();
		player::play(channel.url, userAgentFor(channel), channel.referer, g_startAt);
		return;
	}
	player::stop();
	int request;
	{
		std::lock_guard<std::mutex> lock(g_linkMutex);
		request = ++g_linkRequest;
		g_linkState = 1;
	}
	const Source source = g_sources[(size_t)g_active];
	const std::string command = channel.url.substr(10);
	startThread("link", [source, command, request] {
		std::string url, error;
		const bool ok = stalkerResolve(source, command, url, error);
		std::lock_guard<std::mutex> lock(g_linkMutex);
		if (request != g_linkRequest)
			return;                                   // another channel was chosen meanwhile
		g_linkState = ok ? 2 : 3;
		g_linkUrl = url;
		g_linkError = error;
	});
}

static void startChannel(int channel, double startAt = 0, bool keepQueue = false)
{
	if (channel < 0)
		return;
	const Channel info = g_playlist.channels[(size_t)channel];
	logLine("watch: %s", info.name.c_str());
	const bool temp = g_playTemp;
	g_playTemp = false;
	if (g_tab == 0 && g_nowOn && g_nowTab == 0 && !g_nowVod && !g_nowTemp && g_nowIndex >= 0 && g_nowIndex != channel)
		g_lastLive = g_nowIndex;
	g_nowTemp = temp;
	g_startAt = startAt;
	g_lastPosition = startAt;
	g_nextAt = 0;
	g_nextCancelled = false;
	if (!keepQueue)
	{
		// an episode opened from its series: remember the rest, for "next episode"
		g_queue.clear();
		g_queueAt = -1;
		if (T->drillState == 2)
			for (int i = 0; i < (int)T->drillList.size(); i++)
				if (T->drillList[(size_t)i] == channel)
				{
					g_queue = T->drillList;
					g_queueAt = i;
					g_queueTab = g_tab;
				}
	}
	// Changing from one live channel to another keeps the last picture on screen until the new
	// one arrives, instead of going black in between.
	const bool keepPicture = g_nowOn && !g_nowVod && g_nowTab == 0 && g_tab == 0 && g_gotPicture;
	g_playing = channel;
	g_now = info;
	g_nowOn = true;
	g_nowVod = false;
	g_nowTab = g_tab;
	g_nowIndex = channel;
	if (!keepPicture)
		gfx::videoClear();
	g_gotPicture = false;
	g_retries = 0;
	g_retryAt = 0;
	g_overlayUntil = nowSeconds() + 6;
	beginPlayback(info);
	if (temp)
		return;                                     // nothing about it is remembered
	writeTextFile(listPath("last"), info.url);

	for (size_t i = 0; i < g_recent.size(); i++)
		if (g_recent[i] == channel)
		{
			g_recent.erase(g_recent.begin() + (long)i);
			break;
		}
	g_recent.insert(g_recent.begin(), channel);
	if (g_recent.size() > 60)
		g_recent.resize(60);
	saveList("recent", g_recent);
}

static bool hasNextEpisode()
{
	return g_queueAt >= 0 && g_queueAt + 1 < (int)g_queue.size();
}

static void playNextEpisode()
{
	if (!hasNextEpisode())
		return;
	// the episodes belong to the series tab, which may not be the one on screen
	Tab* const showingTab = T;
	const int showing = g_tab;
	T = &g_tabs[g_queueTab];
	g_tab = g_queueTab;
	const int episode = g_queue[(size_t)++g_queueAt];
	if (episode >= 0 && episode < (int)g_playlist.channels.size())
		startChannel(episode, 0, true);
	T = showingTab;
	g_tab = showing;
	g_playing = g_nowTab == g_tab ? g_nowIndex : -1;
}

static bool g_sawInterlaced = false;
static double g_uploadSeconds = 0;              // time spent handing pictures to the graphics chip, for the log
static int g_uploads = 0;

// Keeps the stream going (retrying a few times when it drops) and returns what to tell the viewer.
static std::string updatePlayback()
{
	if (!g_nowOn)
		return "";
	const Channel& channel = g_now;
	const double now = nowSeconds();

	player::Picture picture;
	if (player::nextPicture(picture))
	{
		const double before = nowSeconds();
		gfx::videoUpload(picture.planes, picture.linesize, picture.width, picture.height, picture.fullRange, picture.matrix, picture.transfer);
		gfx::videoInterlaced(g_deinterlace && picture.interlaced);
		if (picture.interlaced != g_sawInterlaced)
		{
			g_sawInterlaced = picture.interlaced;
			logLine("player: the picture is %s", picture.interlaced ? "interlaced (its combing is being smoothed)" : "not interlaced");
		}
		g_uploadSeconds += nowSeconds() - before;
		g_uploads++;
		g_gotPicture = true;
	}
	if (player::duration() > 0)
		g_nowVod = true;                           // a film or an episode: it has a length
	if (g_nowVod && !g_nowTemp && g_nowTab >= 0 && g_nowTab < 3)
	{
		// Remember how far it has got, every few seconds, so it can be resumed another day.
		static double savedAt = 0;
		const double at = player::position(), length = player::duration();
		if (length > 0 && at > 0)
			g_lastPosition = at;
		if (length > 120 && now - savedAt > 10 && player::state() == player::PLAYING)
		{
			savedAt = now;
			auto& resume = g_tabs[g_nowTab].resume;
			if (at > 45 && at < length - 90)
				resume[channel.url] = { at, length };
			else
			{
				resume.erase(channel.url);             // barely started, or as good as finished
				if (at >= length - 90)
					markWatched(g_nowTab, channel.url);
			}
			saveResume(g_nowTab);
		}
	}

	int linkState;
	std::string linkUrl, linkError;
	{
		std::lock_guard<std::mutex> lock(g_linkMutex);
		linkState = g_linkState;
		linkUrl = g_linkUrl;
		linkError = g_linkError;
		if (linkState == 2)
			g_linkState = 0;
	}
	if (linkState == 1)
		return "Connecting...";
	if (linkState == 2)
	{
		player::play(linkUrl, userAgentFor(channel), "", g_startAt);
		return "Connecting...";
	}

	const player::State state = player::state();
	if (state == player::ENDED && g_nowVod)
	{
		// a film that reached its end is not restarted; an episode can lead to the next
		if (!g_nowTemp && g_nowTab >= 0 && g_nowTab < 3 && g_tabs[g_nowTab].resume.erase(channel.url))
			saveResume(g_nowTab);
		if (!g_nowTemp)
			markWatched(g_nowTab, channel.url);
		if (hasNextEpisode() && g_autoNext && !g_nextCancelled)
		{
			if (g_nextAt == 0)
				g_nextAt = now + 8;
			if (now >= g_nextAt)
			{
				playNextEpisode();
				return "Connecting...";
			}
			char text[96];
			snprintf(text, sizeof(text), "Next episode in %d s     \xE2\x9C\x95 Play now     \xE2\x97\x8B Cancel", (int)(g_nextAt - now) + 1);
			return text;
		}
		return hasNextEpisode() ? "Finished     \xE2\x9C\x95 Next episode" : "Finished";
	}
	if (linkState == 3 || state == player::FAILED || state == player::ENDED)
	{
		if (g_retryAt == 0)
			g_retryAt = now + 3;
		if (g_retries < 5 && now >= g_retryAt)
		{
			g_retries++;
			g_retryAt = 0;
			logLine("watch: retry %d", g_retries);
			g_startAt = g_nowVod ? g_lastPosition : 0;   // a film carries on from where it stopped
			beginPlayback(channel);
		}
		std::string status = linkState == 3 ? linkError : state == player::FAILED ? player::message() : "The stream ended";
		if (g_retries < 5)
			status += "  (trying again)";
		return status;
	}
	if (state == player::OPENING)
		return "Connecting...";
	if (state == player::PLAYING && player::paused())
		return "Paused";
	if (state == player::PLAYING && !player::hasVideo())
		return "Sound only";
	if (state == player::PLAYING && !g_gotPicture)
		return "Loading...";
	return "";
}

static void keepVisible(int selected, int& top, int count, int rows)
{
	if (selected < top) top = selected;
	if (selected >= top + rows) top = selected - rows + 1;
	if (top > count - rows) top = count - rows;
	if (top < 0) top = 0;
}

static void moveSelection(int& selected, int count, int by)
{
	if (count <= 0)
	{
		selected = 0;
		return;
	}
	if (by == 1 && selected == count - 1) selected = 0;          // wrap around at the ends
	else if (by == -1 && selected == 0) selected = count - 1;
	else
	{
		selected += by;
		if (selected < 0) selected = 0;
		if (selected > count - 1) selected = count - 1;
	}
}

static void centredIn(float x, float w, float y, float size, uint32_t colour, const std::string& text)
{
	const float width = gfx::textWidth(size, text);
	gfx::text(width > w - 24 ? x + 12 : x + (w - width) / 2, y, size, colour, text, w - 24);
}

// Centred with no width limit: for icons, initials and labels on buttons.
static void centredTight(float x, float w, float y, float size, uint32_t colour, const std::string& text)
{
	gfx::text(x + (w - gfx::textWidth(size, text)) / 2, y, size, colour, text);
}

// Two letters to stand in for a channel logo, as IPTVnator shows when there is none.
static std::string initials(const std::string& name)
{
	std::string out;
	bool wordStart = true;
	for (const char c : name)
	{
		const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
		if (letter && wordStart && out.size() < 2)
			out += (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
		wordStart = !letter;
	}
	return out.empty() ? "TV" : out;
}

static void drawScrollbar(float x, float y, float height, int top, int count, int rows)
{
	if (count <= rows)
		return;
	float size = height * (float)rows / (float)count;
	if (size < 28)
		size = 28;
	gfx::roundRect(x, y + (height - size) * (float)top / (float)(count - rows), 5, size, 2, kScroll);
}

