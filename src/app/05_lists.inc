// ---- the browser (laid out like IPTVnator) -------------------------------------
//
//   top bar:  playlist name and channel count | filter field
//   rail:     sections (all channels, groups, favourites, recently watched)
//   columns:  [categories, in the groups section] | channels | player and programme panel


enum Screen { LOADING, BROWSE, MANAGER };

static const char* const kSectionName[S_COUNT] = { "All channels", "Groups", "Favorites", "Recently watched" };
static const char* const kSectionIcon[S_COUNT] = { "\xE2\x96\xB6", "\xE2\x96\xA4", "\xE2\x99\xA5", "\xE2\x86\xBA" }; // play, list, heart, history

static Screen g_screen = LOADING;


static int g_playing = -1;                     // what is playing, as an entry of the current tab (-1: none of them)
static Channel g_now;                          // what is playing, whichever tab it came from
static bool g_nowOn = false, g_nowVod = false;
static int g_nowTab = 0, g_nowIndex = -1;
static bool g_fullscreen = false;
static double g_overlayUntil = 0, g_retryAt = 0;
static int g_retries = 0;
static bool g_gotPicture = false;

static bool g_railOnPlaylists = false;         // the rail's last button, which opens the playlist manager
static bool g_haveBrowse = false;              // a playlist is loaded and can be browsed

// on-screen keyboard (defined further down)
static bool g_keyboard = false;
static void openKeyboard(std::string* target, size_t limit, bool isFilter, const std::string& label);
static void openManager(const std::string& notice);
static void reloadPlaylist();
static void cancelLoading();

// Favourites, recently watched and the last channel are kept per playlist.
static std::string listPathFor(int tab, const char* kind)
{
	static const char* const part[3] = { "", "-movies", "-series" };
	return g_dataDir + "/" + kind + part[tab] + "-" + g_activeId + profileSuffix() + ".txt";
}

static std::string listPath(const char* kind)
{
	return listPathFor((int)(T - g_tabs), kind);
}

// Where each film and episode was left, so it can be picked up again: "seconds, length, address" per line.
static void saveResume(int tab)
{
	std::string text;
	char numbers[64];
	for (const auto& entry : g_tabs[tab].resume)
	{
		snprintf(numbers, sizeof(numbers), "%.0f\t%.0f\t", entry.second.first, entry.second.second);
		text += numbers + entry.first + "\n";
	}
	writeTextFile(listPathFor(tab, "resume"), text);
}

static void loadResume()
{
	T->resume.clear();
	std::string text;
	if (!readTextFile(listPath("resume"), text))
		return;
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t end = text.find('\n', pos);
		if (end == std::string::npos)
			end = text.size();
		const std::string line = text.substr(pos, end - pos);
		pos = end + 1;
		const size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
		if (b == std::string::npos)
			continue;
		const double at = atof(line.substr(0, a).c_str()), length = atof(line.substr(a + 1, b - a - 1).c_str());
		if (at > 0 && length > 0)
			T->resume[line.substr(b + 1)] = { at, length };
	}
}

static const int kChannelRows = 11, kCategoryRows = 16;
static const float kRowH = 76, kCatRowH = 53, kColumnTop = 146, kListTop = 212;

static std::string lower(const std::string& text)
{
	std::string out = text;
	for (char& c : out)
		if (c >= 'A' && c <= 'Z')
			c = (char)(c + 32);
	return out;
}

// ---- lists, categories fetched on demand ------------------------------------------
// For a "lazy" library only the category names are known at first. A category's entries are
// fetched in the background when it is opened, and join the lists as they arrive.

static std::mutex g_genreMutex;
static std::atomic<int> g_genreRequest{ 0 };           // changes when the fetch in progress is no longer wanted
static std::vector<Channel> g_genreArrived;            // entries fetched but not yet in the lists
static int g_genreFinished = 0;                        // 0 running, 1 done, 2 failed
static std::string g_genreError, g_genreFailure;
static int g_genreLoading = -1;                        // the category being fetched, or -1
static const std::vector<int> kNothing;

static std::string g_toast;                            // a short message at the foot of the screen
static double g_toastUntil = 0;

static void toast(const std::string& text)
{
	g_toast = text;
	g_toastUntil = nowSeconds() + 4;
}

// Adds an entry to the current library (or finds it, if already known). Returns its number.
// Episodes are kept out of the "All" list: they belong to their series.
static int addChannel(const Channel& channel, bool listInAll = true)
{
	const auto found = g_byUrl.find(channel.url);
	if (found != g_byUrl.end())
		return found->second;
	const int index = (int)g_playlist.channels.size();
	g_playlist.channels.push_back(channel);
	g_isFavourite.push_back(0);
	if (listInAll)
		g_groupChannels[0].push_back(index);
	g_byUrl[channel.url] = index;
	return index;
}

static void addToGroup(int index, int groupPlace)
{
	std::vector<int>& list = g_groupChannels[(size_t)groupPlace];
	for (const int existing : list)
		if (existing == index)
			return;
	list.push_back(index);
}

// Favourites and recently watched are saved with their names, so they work before the
// category they came from has been opened.
static void saveList(const char* file, const std::vector<int>& list)
{
	std::vector<Channel> channels;
	for (const int channel : list)
		channels.push_back(g_playlist.channels[(size_t)channel]);
	writeTextFile(listPath(file), channelsToM3U(channels));
}

static void loadList(const char* file, std::vector<int>& list)
{
	list.clear();
	std::string text;
	Playlist saved;
	if (!readTextFile(listPath(file), text) || !parseM3U(text, saved))
		return;
	for (const Channel& channel : saved.channels)
	{
		const auto found = g_byUrl.find(channel.url);
		if (found != g_byUrl.end())
			list.push_back(found->second);
		else if (g_playlist.lazy)
		{
			// its category has not been opened yet: the saved copy stands in
			const auto group = g_groupIndex.find(channel.group);
			const int index = addChannel(channel, group != g_groupIndex.end());
			if (group != g_groupIndex.end())
				addToGroup(index, group->second);
			list.push_back(index);
		}
	}
}

// ---- hidden categories ---------------------------------------------------------------

static void saveHidden()
{
	std::string text;
	for (const std::string& name : g_playlist.groups)
		if (T->hidden.count(name))
			text += name + "\n";
	writeTextFile(listPath("hidden"), text);
}

static void loadHidden()
{
	T->hidden.clear();
	std::string text;
	if (!readTextFile(listPath("hidden"), text))
		return;
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t end = text.find('\n', pos);
		if (end == std::string::npos)
			end = text.size();
		const std::string name = trim(text.substr(pos, end - pos));
		if (!name.empty())
			T->hidden.insert(name);
		pos = end + 1;
	}
}

// Single hidden channels are kept with their names, so Settings can list them for un-hiding.
static void saveHiddenChannels()
{
	writeTextFile(listPath("hiddenchannels"), channelsToM3U(T->hiddenChannels));
}

static void loadHiddenChannels()
{
	T->hiddenUrls.clear();
	T->hiddenChannels.clear();
	std::string text;
	Playlist saved;
	if (readTextFile(listPath("hiddenchannels"), text) && parseM3U(text, saved))
		for (const Channel& channel : saved.channels)
			if (T->hiddenUrls.insert(channel.url).second)
				T->hiddenChannels.push_back(channel);
}

static void saveWatched(int tab)
{
	std::string text;
	for (const std::string& url : g_tabs[tab].watched)
		text += url + "\n";
	writeTextFile(listPathFor(tab, "watched"), text);
}

// Seen to the end (or near enough): ticked from now on.
static void markWatched(int tab, const std::string& url)
{
	if (tab >= 0 && tab < 3 && g_tabs[tab].watched.insert(url).second)
		saveWatched(tab);
}

static void loadWatched()
{
	T->watched.clear();
	std::string text;
	if (!readTextFile(listPath("watched"), text))
		return;
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t end = text.find('\n', pos);
		if (end == std::string::npos)
			end = text.size();
		const std::string url = trim(text.substr(pos, end - pos));
		if (!url.empty())
			T->watched.insert(url);
		pos = end + 1;
	}
}

// Works out which categories are shown: not hidden, and matching the filter when the filter
// is being applied to category names.
static void computeCats()
{
	T->cats.clear();
	const std::string needle = T->filterCats ? lower(g_filter) : std::string();
	for (int i = 0; i < (int)g_playlist.groups.size(); i++)
	{
		const std::string& name = g_playlist.groups[(size_t)i];
		if (T->hidden.count(name))
			continue;
		if (!needle.empty() && lower(name).find(needle) == std::string::npos)
			continue;
		if (g_layout == 1 && T == &g_tabs[0] && !g_tvCountry.empty() && countryOf(name) != g_tvCountry)
			continue;                                  // the TV layout's country chip
		T->cats.push_back(i);
	}
	bool found = false;
	for (const int shown : T->cats)
		if (shown == g_catSel)
			found = true;
	if (!found)
	{
		g_catSel = T->cats.empty() ? 0 : T->cats[0];
		g_catTop = 0;
	}
}

static void rebuildView(int keepChannel);
static int selectedChannel();

static void stopGenreLoading()
{
	std::lock_guard<std::mutex> lock(g_genreMutex);
	g_genreRequest++;
	g_genreArrived.clear();
	g_genreFinished = 0;
	if (g_genreLoading >= 0 && g_genreLoading < (int)g_groupState.size() && g_groupState[(size_t)g_genreLoading] == 1)
		g_groupState[(size_t)g_genreLoading] = 0;
	g_genreLoading = -1;
}

static std::vector<int> g_genreFresh;                  // what the server has listed for the category being fetched

static std::string categoryCachePath(int category)
{
	unsigned long long hash = 1469598103934665603ULL;
	for (const char c : g_playlist.groupIds[(size_t)category])
		hash = (hash ^ (unsigned char)c) * 1099511628211ULL;
	char name[64];
	snprintf(name, sizeof(name), "cat%d-%016llx", (int)(T - g_tabs), hash);
	return cachePath(g_activeId, name);
}

static void startGenreLoading(int category)
{
	stopGenreLoading();
	g_groupState[(size_t)category] = 1;
	g_genreLoading = category;
	g_genreFresh.clear();
	if (g_groupChannels[(size_t)category + 1].empty())
	{
		// what was in this category last time, at once; the server's answer replaces it when it comes
		std::string text;
		Playlist saved;
		if (readTextFile(categoryCachePath(category), text) && parseM3U(text, saved))
		{
			for (Channel& channel : saved.channels)
			{
				channel.group = g_playlist.groups[(size_t)category];
				addToGroup(addChannel(channel), category + 1);
			}
			if (!T->drill && ((inGroups() && g_catSel == category) || g_section == S_ALL))
				rebuildView(selectedChannel());
		}
	}
	const int request = g_genreRequest.load();
	const int media = (int)(T - g_tabs);
	const Source source = g_sources[(size_t)g_active];
	const std::string id = g_playlist.groupIds[(size_t)category], group = g_playlist.groups[(size_t)category];
	logLine("category: fetching \"%s\"", group.c_str());
	startThread("category", [source, media, id, group, request] {
		std::string error;
		const bool ok = fetchCategory(source, media, id, [&](std::vector<Channel>& batch) {
			std::lock_guard<std::mutex> lock(g_genreMutex);
			if (request != g_genreRequest.load())
				return false;
			for (Channel& channel : batch)
			{
				channel.group = group;
				g_genreArrived.push_back(std::move(channel));
			}
			return true;
		}, error);
		std::lock_guard<std::mutex> lock(g_genreMutex);
		if (request != g_genreRequest.load())
			return;
		g_genreFinished = ok ? 1 : 2;
		g_genreError = error;
	});
}

// Called every frame while browsing: starts fetching the open category, and files what has arrived.
static void pumpGenres()
{
	if (!g_playlist.lazy || g_playlist.groups.empty() || T->state != 2)
		return;

	std::vector<Channel> arrived;
	int finished = 0;
	std::string error;
	{
		std::lock_guard<std::mutex> lock(g_genreMutex);
		arrived.swap(g_genreArrived);
		finished = g_genreFinished;
		error = g_genreError;
		if (finished)
			g_genreFinished = 0;
	}
	if (!arrived.empty() && g_genreLoading >= 0)
	{
		const int keep = selectedChannel();
		const int top = g_top, selected = g_sel;
		for (const Channel& channel : arrived)
		{
			const int index = addChannel(channel);
			addToGroup(index, g_genreLoading + 1);
			g_genreFresh.push_back(index);
			// a title already known from the saved copy gains what only the server says about it
			Channel& known = g_playlist.channels[(size_t)index];
			if (known.plot.empty()) known.plot = channel.plot;
			if (known.year.empty()) known.year = channel.year;
			if (known.rating.empty()) known.rating = channel.rating;
			if (known.genres.empty()) known.genres = channel.genres;
			if (known.length.empty()) known.length = channel.length;
			if (known.cast.empty()) known.cast = channel.cast;
			if (known.logo.empty()) known.logo = channel.logo;
			if (known.trailer.empty()) known.trailer = channel.trailer;
			known.archive = known.archive || channel.archive;
		}
		const bool showing = !T->drill && ((inGroups() && g_catSel == g_genreLoading) || g_section == S_ALL);
		if (showing)
		{
			rebuildView(keep);
			if (keep < 0)
				g_sel = selected;
			g_top = top;                          // the list grows without jumping
		}
	}
	if (finished && g_genreLoading >= 0)
	{
		g_groupState[(size_t)g_genreLoading] = finished == 1 ? 2 : 3;
		if (finished == 2)
		{
			g_genreFailure = error;
			logLine("category: failed: %s", error.c_str());
		}
		else
		{
			// the server's list is the truth: anything only in the saved copy has gone
			std::vector<int>& list = g_groupChannels[(size_t)g_genreLoading + 1];
			if (list.size() != g_genreFresh.size())
			{
				const int keep = selectedChannel();
				list = g_genreFresh;
				if (!T->drill && inGroups() && g_catSel == g_genreLoading)
					rebuildView(keep);
			}
			std::vector<Channel> entries;
			for (const int index : list)
				entries.push_back(g_playlist.channels[(size_t)index]);
			writeTextFile(categoryCachePath(g_genreLoading), channelsToM3U(entries));
			logLine("category: \"%s\" has %d entries", g_playlist.groups[(size_t)g_genreLoading].c_str(), (int)list.size());
		}
		g_genreFresh.clear();
		g_genreLoading = -1;
	}

	// Fetch the open category once the selection has rested on it for a moment
	// (so scrolling through the list does not fetch every category passed).
	static int restingOn = -1;
	static const Tab* restingTab = nullptr;
	static double restingSince = 0;
	if (!inGroups())
		return;
	if (g_catSel != restingOn || restingTab != T)
	{
		restingOn = g_catSel;
		restingTab = T;
		restingSince = nowSeconds();
	}
	if (g_groupState[(size_t)g_catSel] == 0 && g_genreLoading != g_catSel && nowSeconds() - restingSince > 0.45)
		startGenreLoading(g_catSel);
}

// Fills the middle column for the current section, category and filter.
static void rebuildView(int keepChannel)
{
	const std::vector<int>* base = &kNothing;
	if (T->drill)
		base = &T->drillList;
	else if (g_groupChannels.empty())
		base = &kNothing;
	else if (g_section == S_GROUPS)
		base = T->cats.empty() ? &kNothing : &g_groupChannels[(size_t)g_catSel + 1];
	else if (g_section == S_FAVOURITES)
		base = &g_favourites;
	else if (g_section == S_RECENT)
		base = &g_recent;
	else
		base = &g_groupChannels[0];

	const bool skipHidden = !T->drill && g_section == S_ALL && !T->hidden.empty();
	const bool skipChannels = !T->hiddenUrls.empty();
	const std::string needle = T->filterCats ? std::string() : lower(g_filter);
	g_view.clear();
	if (!skipHidden && !skipChannels && needle.empty())
		g_view = *base;
	else
		for (const int channel : *base)
		{
			const Channel& info = g_playlist.channels[(size_t)channel];
			if (skipHidden && T->hidden.count(info.group))
				continue;
			if (skipChannels && T->hiddenUrls.count(info.url))
				continue;
			if (!needle.empty() && lower(info.name).find(needle) == std::string::npos)
				continue;
			g_view.push_back(channel);
		}
	if (T != &g_tabs[0] && g_wallSort != 0 && !T->drill)
	{
		// the poster wall in the order the viewer chose
		const std::vector<Channel>& all = g_playlist.channels;
		const int mode = g_wallSort;
		std::stable_sort(g_view.begin(), g_view.end(), [&all, mode](int a, int b) {
			const Channel& x = all[(size_t)a];
			const Channel& y = all[(size_t)b];
			if (mode == 1)
				return lower(x.name) < lower(y.name);
			if (mode == 2)
				return atoi(x.year.c_str()) > atoi(y.year.c_str());
			return atof(x.rating.c_str()) > atof(y.rating.c_str());
		});
	}
	g_sel = 0;
	g_top = 0;
	for (int i = 0; i < (int)g_view.size(); i++)
	{
		if (g_view[i] == keepChannel)
		{
			g_sel = i;
			break;
		}
	}
}

static int selectedChannel()
{
	return g_sel >= 0 && g_sel < (int)g_view.size() ? g_view[g_sel] : -1;
}

// ---- a series and its episodes ---------------------------------------------------------

static std::mutex g_drillMutex;
static int g_drillRequest = 0, g_drillDone = 0;        // done: 1 listed, 2 failed
static std::vector<Channel> g_drillArrived;
static std::string g_drillFailure;

static void closeDrill()
{
	if (!T->drill)
		return;
	{
		std::lock_guard<std::mutex> lock(g_drillMutex);
		g_drillRequest++;
		g_drillDone = 0;
	}
	T->drill = false;
	T->drillState = 0;
	T->drillList.clear();
	rebuildView(T->drillItem);
}

static void openSeries(int index, bool takeOverList = true)
{
	const Channel series = g_playlist.channels[(size_t)index];
	T->drill = takeOverList;
	T->drillState = 1;
	T->drillItem = index;
	T->drillTitle = series.name;
	T->drillError.clear();
	T->drillList.clear();
	if (takeOverList)
	{
		g_focus = F_CHANNELS;
		rebuildView(-1);
	}
	int request;
	{
		std::lock_guard<std::mutex> lock(g_drillMutex);
		request = ++g_drillRequest;
		g_drillDone = 0;
	}
	const Source source = g_sources[(size_t)g_active];
	logLine("series: opening \"%s\"", series.name.c_str());
	startThread("series", [source, series, request] {
		std::vector<Channel> episodes;
		std::string error;
		const bool ok = fetchEpisodes(source, series.url, episodes, error);
		std::lock_guard<std::mutex> lock(g_drillMutex);
		if (request != g_drillRequest)
			return;
		g_drillArrived = std::move(episodes);
		g_drillFailure = error;
		g_drillDone = ok ? 1 : 2;
	});
}

static void pumpDrill()
{
	if (T->drillState != 1)
		return;
	std::vector<Channel> episodes;
	int done;
	std::string error;
	{
		std::lock_guard<std::mutex> lock(g_drillMutex);
		done = g_drillDone;
		if (!done)
			return;
		g_drillDone = 0;
		episodes.swap(g_drillArrived);
		error = g_drillFailure;
	}
	if (done == 2)
	{
		T->drillState = 3;
		T->drillError = error;
		return;
	}
	for (Channel& episode : episodes)
	{
		episode.group = T->drillTitle;
		T->drillList.push_back(addChannel(episode, false));
	}
	T->drillState = 2;
	if (T->drill)
		rebuildView(-1);
}

