// ---- playlists (loaded in the background) --------------------------------------

static std::vector<Source> g_sources;                  // every playlist the viewer has
static int g_active = -1;                              // which one is open
static std::string g_activeId;

// ---- the three libraries: live TV, movies, series ---------------------------------
// Each has its own categories, lists, favourites and place in the screen. The rest of the code
// works on "the current one" through the names below, so switching tab is switching `T`.

enum Section { S_ALL, S_GROUPS, S_FAVOURITES, S_RECENT, S_COUNT };
enum Focus { F_RAIL, F_CATEGORIES, F_CHANNELS };

struct Tab
{
	Playlist playlist;
	std::vector<std::string> groupNames;               // [0] is "All"
	std::vector<std::vector<int>> groupChannels;       // entry numbers per group
	std::unordered_map<std::string, int> groupIndex;   // group name -> place in groupChannels
	std::unordered_map<std::string, int> byUrl;        // every known entry, by address
	std::vector<char> groupState;                      // per category: 0 not fetched, 1 fetching, 2 done, 3 failed
	std::vector<char> isFavourite;
	std::vector<int> favourites, recent;               // entry numbers, newest first for recent
	std::vector<int> view;                             // what the middle column shows
	std::vector<int> cats;                             // the categories shown (not hidden, matching the filter)
	std::unordered_set<std::string> hidden;            // category names the viewer has hidden
	std::string filter;
	bool filterCats = false;                           // the filter applies to category names, not entries
	Section section = S_ALL;
	Focus focus = F_CHANNELS;
	int catSel = 0, catTop = 0, sel = 0, top = 0;
	// a series that has been opened: its episodes take over the middle column
	bool drill = false;
	int drillState = 0, drillItem = -1;                // state: 1 fetching, 2 listed, 3 failed
	std::vector<int> drillList;
	std::string drillTitle, drillError;
	int state = 0;                                     // 0 not loaded, 1 loading, 2 ready, 3 failed
	std::string error;
	std::unordered_map<std::string, std::pair<double, double>> resume;   // address -> (seconds watched, length)
	std::unordered_set<std::string> hiddenUrls;        // single channels (or titles) the viewer has hidden, by address
	std::vector<Channel> hiddenChannels;               // the same, with their names, for the list in Settings
	std::unordered_set<std::string> watched;           // films and episodes seen to the end
	// smooth scrolling: where each list is drawn from, easing towards where it should be
	float listScroll = 0, catScroll = 0, gridScroll = 0;
	int gridTop = 0;                                    // first row of posters shown
};

static const int kTabCount = 7;                         // live, movies, series (the libraries), then account, home, search and radio
static const int kAccountTab = 3, kHomeTab = 4, kSearchTab = 5, kRadioTab = 6;
static const char* const kTabName[kTabCount] = { "Live TV", "Movies", "Series", "Account", "Home", "Search", "Radio" };
static const int kTabOrder[kTabCount] = { 4, 5, 0, 1, 2, 3, 6 };   // left to right on screen

// Motion: how long the last frame took, and a short fade used whenever the screen changes.
static double g_frameSeconds = 1.0 / 60.0;
static double g_fadeFrom = -10;

static void fadeIn()
{
	g_fadeFrom = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

// Moves a value most of the way to its target each frame: quick at first, settling gently.
static void glide(float& value, float target, float speed = 14.0f)
{
	const float step = 1.0f - expf(-speed * (float)g_frameSeconds);
	value += (target - value) * step;
	if (value - target < 0.002f && target - value < 0.002f)
		value = target;
}
static Tab g_tabs[3];
static Tab* T = &g_tabs[0];
static int g_tab = 0;
static bool g_detail = false;                   // a film's or series' own page is open
static int g_detailItem = -1;
static bool g_grid = false;                     // the guide grid is open (Live TV)
static std::string g_searchText;                // what is typed in Search
static bool g_searchTyping = false;             // the keyboard is open for it

#define g_playlist (T->playlist)
#define g_groupNames (T->groupNames)
#define g_groupChannels (T->groupChannels)
#define g_groupIndex (T->groupIndex)
#define g_byUrl (T->byUrl)
#define g_groupState (T->groupState)
#define g_isFavourite (T->isFavourite)
#define g_favourites (T->favourites)
#define g_recent (T->recent)
#define g_view (T->view)
#define g_filter (T->filter)
#define g_section (T->section)
#define g_focus (T->focus)
#define g_catSel (T->catSel)
#define g_catTop (T->catTop)
#define g_sel (T->sel)
#define g_top (T->top)

// Which country or region a group belongs to, read from the way providers name their groups:
// "UK | Sports", "US: News", "[DE] Kino", "AR - beIN". Groups that do not start that way are "Other".
static std::string countryOf(const std::string& group)
{
	const auto clean = [](std::string text) {
		size_t start = 0, end = text.size();
		while (start < end && (text[start] == ' ' || text[start] == '#' || text[start] == '*' || text[start] == '-' || text[start] == '=' || text[start] == '[' || text[start] == '('))
			start++;
		while (end > start && (text[end - 1] == ' ' || text[end - 1] == ']' || text[end - 1] == ')'))
			end--;
		text = text.substr(start, end - start);
		for (char& c : text)
			if (c >= 'a' && c <= 'z')
				c = (char)(c - 32);
		return text;
	};
	// a short label made of letters and digits (and at most one space): "UK", "USA", "4K", "LATIN AMERICA" is too long
	const auto label = [](const std::string& text) {
		if (text.size() < 2 || text.size() > 14)
			return false;
		int spaces = 0;
		for (const char c : text)
		{
			if (c == ' ')
				spaces++;
			else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '&'))
				return false;
		}
		return spaces <= 1;
	};

	// "[UK] Sports" / "(UK) Sports"
	size_t first = 0;
	while (first < group.size() && group[first] == ' ')
		first++;
	if (first < group.size() && (group[first] == '[' || group[first] == '('))
	{
		const size_t close = group.find(group[first] == '[' ? ']' : ')', first);
		if (close != std::string::npos)
		{
			const std::string inside = clean(group.substr(first + 1, close - first - 1));
			if (label(inside))
				return inside;
		}
	}
	// "UK | Sports", "UK: Sports", "UK - Sports"
	size_t cut = std::string::npos;
	for (size_t i = 0; i < group.size() && i < 16; i++)
	{
		const char c = group[i];
		if (c == '|' || c == ':' || (c == '-' && i > 0 && group[i - 1] == ' ' && i + 1 < group.size() && group[i + 1] == ' '))
		{
			cut = i;
			break;
		}
		// some providers use a decorative arrow or bar made of other bytes
		if ((unsigned char)c >= 0x80)
		{
			cut = i;
			break;
		}
	}
	if (cut != std::string::npos && cut > 0)
	{
		const std::string before = clean(group.substr(0, cut));
		if (label(before))
			return before;
	}
	// "UK Sports": two or three capital letters, then a space
	size_t letters = first;
	while (letters < group.size() && group[letters] >= 'A' && group[letters] <= 'Z')
		letters++;
	if ((letters - first == 2 || letters - first == 3) && letters < group.size() && group[letters] == ' ')
		return group.substr(first, letters - first);
	return "Other";
}

// The middle column is the category list's partner only when there are categories to show.
static bool inGroups()
{
	return g_section == S_GROUPS && !T->cats.empty();
}

// Where the selected category sits in the list of shown categories.
static int catPlace()
{
	for (int i = 0; i < (int)T->cats.size(); i++)
		if (T->cats[(size_t)i] == g_catSel)
			return i;
	return 0;
}
static std::atomic<int> g_loadState{ 0 };              // 0 loading, 1 done, 2 failed
static std::string g_loadError;

static void buildGroups()
{
	g_groupNames.clear();
	g_groupChannels.clear();
	g_groupNames.push_back("All channels");
	g_groupChannels.emplace_back();
	std::unordered_map<std::string, int>& index = g_groupIndex;
	index.clear();
	for (const std::string& name : g_playlist.groups)
	{
		index[name] = (int)g_groupNames.size();
		g_groupNames.push_back(name);
		g_groupChannels.emplace_back();
	}
	for (int i = 0; i < (int)g_playlist.channels.size(); i++)
	{
		g_groupChannels[0].push_back(i);
		g_groupChannels[index[g_playlist.channels[i].group]].push_back(i);
	}
}

// A load can be abandoned (the viewer cancels, or picks another playlist), so the background
// thread hands its result over only if it is still the one being waited for.
static std::mutex g_loadMutex;
static int g_loadRequest = 0;
static Playlist g_loadedPlaylist;
static double g_loadStarted = 0;

// Runs on its own thread: fetches a fresh copy of the playlist (this is the update on every start).
// ---- saved copies, so the app has something to show at once ------------------------------------
// A portal's list of categories is saved each time it is fetched. Next time, the saved list is
// shown immediately and the server is asked again in the background.

static std::string cachePath(const std::string& sourceIdText, const std::string& what)
{
	const std::string folder = g_dataDir + "/cache";
	mkdir(folder.c_str(), 0777);
	return folder + "/" + what + "-" + sourceIdText + ".txt";
}

static void saveGroupsCache(const std::string& sourceIdText, int tab, const Playlist& playlist)
{
	if (!playlist.lazy || playlist.groups.size() != playlist.groupIds.size())
		return;
	std::string text;
	for (size_t i = 0; i < playlist.groups.size(); i++)
		text += playlist.groupIds[i] + "\t" + playlist.groups[i] + "\n";
	writeTextFile(cachePath(sourceIdText, "groups" + std::to_string(tab)), text);
}

static bool loadGroupsCache(const std::string& sourceIdText, int tab, Playlist& out)
{
	std::string text;
	if (!readTextFile(cachePath(sourceIdText, "groups" + std::to_string(tab)), text))
		return false;
	out = Playlist();
	out.lazy = true;
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t end = text.find('\n', pos);
		if (end == std::string::npos)
			end = text.size();
		const std::string line = text.substr(pos, end - pos);
		pos = end + 1;
		const size_t tabAt = line.find('\t');
		if (tabAt == std::string::npos || tabAt == 0 || tabAt + 1 >= line.size())
			continue;
		out.groupIds.push_back(line.substr(0, tabAt));
		out.groups.push_back(line.substr(tabAt + 1));
	}
	return !out.groups.empty();
}

static bool g_loadUseCache = true;                     // false for a Reload, which must ask the server
static Playlist g_refreshedPlaylist;                   // what the server said, when it differs from the saved copy
static bool g_refreshedReady = false;

static void loadPlaylist(const Source& source, int request, bool useCache)
{
	logLine("playlist: loading \"%s\" (%s)", source.name.c_str(), sourceKind(source));
	const std::string id = sourceId(source);
	Playlist saved;
	bool shown = false;
	if (useCache && loadGroupsCache(id, 0, saved))
	{
		std::lock_guard<std::mutex> lock(g_loadMutex);
		if (request == g_loadRequest)
		{
			g_loadedPlaylist = saved;
			g_loadState = 1;
			shown = true;
			logLine("playlist: %d groups shown from the saved copy; asking the server in the background", (int)saved.groups.size());
		}
	}
	Playlist fresh;
	std::string error;
	const bool ok = fetchSource(source, g_dataDir, fresh, error);
	std::lock_guard<std::mutex> lock(g_loadMutex);
	if (request != g_loadRequest)
	{
		logLine("playlist: \"%s\" finished after it was no longer wanted", source.name.c_str());
		return;
	}
	if (!ok)
	{
		logLine("playlist: %s", error.c_str());
		if (!shown)
		{
			g_loadError = error;
			g_loadState = 2;
		}
		return;
	}
	logLine("playlist: %d channels in %d groups", (int)fresh.channels.size(), (int)fresh.groups.size());
	saveGroupsCache(id, 0, fresh);
	if (shown)
	{
		// already on screen from the saved copy: only a changed list needs anything more
		if (fresh.groups != saved.groups || fresh.groupIds != saved.groupIds)
		{
			g_refreshedPlaylist = std::move(fresh);
			g_refreshedReady = true;
			logLine("playlist: the server's groups differ from the saved copy");
		}
		return;
	}
	g_loadedPlaylist = std::move(fresh);
	g_loadState = 1;
}

