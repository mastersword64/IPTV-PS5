// ---- the remote: a phone or computer as the controller ---------------------------------------
// The console serves a small web page on the home network (webadd.cpp). Scanning the code in
// Settings > Remote opens it. From there the app can be steered with the same buttons as the
// controller, channels can be browsed and played, the TV keyboard typed into, and playlists added.
// Everything the page asks for arrives here and is carried out on the app's own thread, between
// one picture and the next.

static const int kWebPort = 8080;
static bool g_webTried = false;                         // starting was attempted (so a failure is not retried every frame)
static std::string g_webNote;                           // the last thing that arrived, for the Remote tab
static std::vector<uint8_t> g_qrModules;
static int g_qrSize = 0;
static std::string g_qrFor;

// A short key that every address of the page carries. It is made once and kept.
static void remoteNewKey()
{
	static const char letters[] = "abcdefghjkmnpqrstuvwxyz23456789";    // (none that look alike)
	uint64_t state = (uint64_t)time(nullptr) * 6364136223846793005ULL ^ (uint64_t)SDL_GetPerformanceCounter() ^ (uint64_t)(uintptr_t)&state;
	g_remoteKey.clear();
	for (int i = 0; i < 6; i++)
	{
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		g_remoteKey += letters[(state >> 11) % (sizeof(letters) - 1)];
	}
}

static std::string jsonText(const std::string& text)
{
	std::string out = "\"";
	for (const char c : text)
	{
		if (c == '"' || c == '\\')
		{
			out += '\\';
			out += c;
		}
		else if ((unsigned char)c < 0x20)
			out += ' ';
		else
			out += c;
	}
	return out + "\"";
}

// What the page can list and play. The numbers the page sends back are places in a table,
// good only for the list they came with (each list sent has a number of its own, "gen").
struct RemoteItem
{
	int kind = 0;                                       // 0 an entry of a library, 1 something found by Search, 2 a radio station, 3 an episode
	Channel channel;
	int tab = 0, section = S_ALL, group = -1;           // where it was listed
	double at = 0, length = 0;                          // a part-watched film: where it was left
};
// There is one table for each of the page's lists: 0 channels, films or series, 1 search results,
// 2 radio stations, 3 the episodes of a series.
static std::vector<RemoteItem> g_remoteTable[4];
static int g_remoteGens[4] = { 0, 0, 0, 0 };

// One row of a list, as JSON. `extra` is more fields, each starting with a comma: the picture's
// address ("p"), whether it is a favourite ("f"), whether it is shown as a poster ("w"), how much
// of a film has been watched ("r", 0 to 1).
static void remoteRow(std::string& rows, char kind, int index, const std::string& name, const std::string& sub, const std::string& extra = "")
{
	if (!rows.empty())
		rows += ',';
	rows += "{\"k\":\"";
	rows += kind;
	rows += "\",\"i\":" + std::to_string(index) + ",\"n\":" + jsonText(name);
	if (!sub.empty())
		rows += ",\"s\":" + jsonText(sub);
	rows += extra;
	rows += '}';
}

static std::string remoteError(const std::string& words)
{
	return "{\"err\":" + jsonText(words) + "}";
}

// Whatever is open over the main screens is put away, so that what the page asked for can be seen.
static void remoteToBrowse()
{
	g_keyboard = false;
	g_form = false;
	g_more = false;
	g_askChannel = -1;
	g_grid = false;
	g_tvOnRail = g_tvOnChips = false;
	g_pointerUntil = 0;
	if (g_screen == MANAGER && g_haveBrowse)
	{
		g_screen = BROWSE;
		fadeIn();
	}
}

// A library (0 live, 1 movies, 2 series) on its groups, with this group chosen: which is what
// makes a portal's group load.
static void remoteOpenGroup(int library, int group)
{
	remoteToBrowse();
	if (g_tab != library)
		switchTab(library);
	if (g_detail)
		closeDetails();
	if (T->drill)
		closeDrill();
	if (g_section != S_GROUPS)
		chooseSection(S_GROUPS);
	bool listed = false;
	for (const int shown : T->cats)
		if (shown == group)
			listed = true;
	if (!listed)
	{
		// narrowed to another country, or by a filter typed for the groups: show them all
		g_tvCountry.clear();
		g_filter.clear();
		T->filterCats = false;
		computeCats();
	}
	if (!T->filterCats && !g_filter.empty())
	{
		g_filter.clear();
		rebuildView(-1);
	}
	selectCategory(group);
	if (g_focus == F_CHANNELS)
		g_focus = F_CATEGORIES;                         // (resting on a channel would start it playing)
	if (g_groupState[(size_t)group] == 3)
		g_groupState[(size_t)group] = 0;                // try again
	// fetched at once, rather than after the moment's rest the controller's browsing is given
	if (g_playlist.lazy && T->state == 2 && g_groupState[(size_t)group] == 0 && group < (int)g_playlist.groupIds.size())
		startGenreLoading(group);
}

static std::string remoteChannels(int library, const std::string& title, const std::vector<int>& list, int section, int group, const char* none, bool more = false)
{
	std::vector<RemoteItem>& items = g_remoteTable[0];
	int& gen = g_remoteGens[0];
	const Tab& tab = g_tabs[library];
	items.clear();
	gen++;
	std::string rows;
	for (const int channel : list)
	{
		if (channel < 0 || channel >= (int)tab.playlist.channels.size())
			continue;
		const Channel& info = tab.playlist.channels[(size_t)channel];
		if (tab.hiddenUrls.count(info.url) || (section == S_ALL && tab.hidden.count(info.group)))
			continue;
		RemoteItem item;
		item.channel = info;
		item.tab = library;
		item.section = section;
		item.group = group;
		std::string sub;
		if (library == 0)
		{
			const Programme* now = onNow(info.url);
			sub = now ? now->title : section == S_GROUPS ? std::string() : info.group;
		}
		else
		{
			sub = info.year;
			if (!info.rating.empty())
				sub += (sub.empty() ? "" : "  \xC2\xB7  ") + std::string("\xE2\x98\x85 ") + info.rating;
			const auto left = tab.resume.find(info.url);
			if (left != tab.resume.end() && left->second.second > 0)
			{
				item.at = left->second.first;
				item.length = left->second.second;
				sub += (sub.empty() ? "" : "  \xC2\xB7  ") + clockText(item.length - item.at) + " left";
			}
		}
		std::string extra = ",\"f\":" + std::string(channel < (int)tab.isFavourite.size() && tab.isFavourite[(size_t)channel] ? "1" : "0");
		if (info.logo.compare(0, 4, "http") == 0)
			extra += ",\"p\":" + jsonText(info.logo);
		if (library != 0)
			extra += ",\"w\":1";
		if (item.length > 0)
		{
			char done[32];
			snprintf(done, sizeof(done), ",\"r\":%.2f", item.at / item.length > 1 ? 1.0 : item.at / item.length);
			extra += done;
		}
		// a series is opened (its episodes are listed on the page); anything else is played
		remoteRow(rows, isSeriesItem(info.url) ? 'e' : 'c', (int)items.size(), info.name, sub, extra);
		items.push_back(item);
		if (items.size() >= 6000)
			break;
	}
	return "{\"slot\":0,\"gen\":" + std::to_string(gen) + ",\"t\":" + jsonText(title)
		+ (section == S_GROUPS ? ",\"back\":\"/list?what=groups&lib=" + std::to_string(library) + "\"" : "")
		+ (more ? ",\"more\":\"/list?what=group&lib=" + std::to_string(library) + "&i=" + std::to_string(group) + "\"" : "")
		+ ",\"none\":" + jsonText(none) + ",\"rows\":[" + rows + "]}";
}

// The episodes of a series that was listed on the page: its page is opened on the TV, which
// fetches them, and they are then listed for the phone.
static std::string remoteEpisodes(std::map<std::string, std::string>& fields)
{
	const int table = atoi(fields["s"].c_str()), index = atoi(fields["i"].c_str());
	if (table < 0 || table > 1 || atoi(fields["g"].c_str()) != g_remoteGens[table] || index < 0 || index >= (int)g_remoteTable[table].size())
		return remoteError("That list has changed. Go back and open it again.");
	const RemoteItem series = g_remoteTable[table][(size_t)index];
	if (series.tab < 1 || series.tab > 2 || !isSeriesItem(series.channel.url))
		return remoteError("That has no episodes.");
	remoteToBrowse();
	if (g_tab != series.tab)
		switchTab(series.tab);
	ensureLists();
	const int entry = addChannel(series.channel, false);
	if (!(g_detail && g_detailItem == entry && T->drillItem == entry && T->drillState != 0))
	{
		g_fullscreen = false;
		openDetails(entry);
	}
	pumpDrill();
	if (T->drillState == 3)
	{
		const std::string why = T->drillError;
		closeDetails();
		T->drillState = 0;                              // the next ask tries again
		return remoteError("The episodes could not be fetched: " + why);
	}
	if (T->drillState != 2)
		return "{\"wait\":1,\"msg\":" + jsonText("Fetching the episodes of \"" + series.channel.name + "\"...") + "}";
	std::vector<RemoteItem>& items = g_remoteTable[3];
	items.clear();
	g_remoteGens[3]++;
	std::string rows;
	for (const int episode : T->drillList)
	{
		if (episode < 0 || episode >= (int)g_playlist.channels.size())
			continue;
		const Channel& info = g_playlist.channels[(size_t)episode];
		RemoteItem item;
		item.kind = 3;
		item.channel = info;
		item.tab = series.tab;
		item.group = entry;
		std::string sub = T->watched.count(info.url) ? "watched" : "";
		const auto left = T->resume.find(info.url);
		if (left != T->resume.end() && left->second.second > 0)
		{
			item.at = left->second.first;
			sub = clockText(left->second.second - left->second.first) + " left";
		}
		remoteRow(rows, 'c', (int)items.size(), info.name, sub);
		items.push_back(item);
	}
	return "{\"slot\":3,\"gen\":" + std::to_string(g_remoteGens[3]) + ",\"t\":" + jsonText(series.channel.name)
		+ (series.channel.logo.compare(0, 4, "http") == 0 ? ",\"art\":" + jsonText(series.channel.logo) : std::string())
		+ (table == 0 ? ",\"back\":\"/list?what=groups&lib=" + std::to_string(series.tab) + "\"" : "")
		+ ",\"none\":\"No episodes were listed.\",\"rows\":[" + rows + "]}";
}

static std::string remoteSearchRows()
{
	std::vector<RemoteItem>& items = g_remoteTable[1];
	int& gen = g_remoteGens[1];
	pumpSearch();
	items.clear();
	gen++;
	std::string rows;
	for (const HomeRow& row : g_searchRows)
	{
		remoteRow(rows, 'h', 0, row.title, "");
		for (const HomeItem& entry : row.items)
		{
			RemoteItem item;
			item.kind = 1;
			item.channel = entry.channel;
			item.tab = entry.tab;
			item.at = entry.at;
			item.length = entry.length;
			std::string sub = entry.tab == 0 ? entry.channel.group : entry.channel.year;
			if (entry.tab != 0 && !entry.channel.genres.empty())
				sub += (sub.empty() ? "" : "  \xC2\xB7  ") + entry.channel.genres;
			std::string extra = entry.tab != 0 ? ",\"w\":1" : "";
			if (entry.channel.logo.compare(0, 4, "http") == 0)
				extra += ",\"p\":" + jsonText(entry.channel.logo);
			remoteRow(rows, isSeriesItem(entry.channel.url) ? 'e' : 'c', (int)items.size(), entry.channel.name, sub, extra);
			items.push_back(item);
		}
	}
	return "{\"slot\":1,\"gen\":" + std::to_string(gen) + ",\"t\":" + jsonText("Results for \"" + g_searchDone + "\"")
		+ (g_searchState == 1 ? ",\"more\":\"/list?what=searchmore\"" : "") + ",\"none\":\"Nothing found.\",\"rows\":[" + rows + "]}";
}

static std::string remoteRadio(std::map<std::string, std::string>& fields)
{
	std::vector<RemoteItem>& items = g_remoteTable[2];
	int& gen = g_remoteGens[2];
	const std::string mode = fields["m"], words = trim(fields["q"]);
	std::string key = mode == "fav" ? "favourites" : mode == "recent" ? "recent" : mode == "search" ? "search:" + words : "popular";
	if (mode == "search" && words.size() < 2)
		return remoteError("Type at least two letters.");
	if (g_tab != kRadioTab || g_radioShown != key || mode == "fav" || mode == "recent")
	{
		remoteToBrowse();
		if (g_tab != kRadioTab)
			switchTab(kRadioTab);
		g_radioOnChips = false;
		g_radioLevel = 0;
		g_radioMode = mode == "fav" ? R_FAVOURITES : mode == "recent" ? R_RECENT : mode == "search" ? R_SEARCH : R_POPULAR;
		if (mode == "search")
			g_radioWords = words;
		g_radioTyping = false;
		radioShow();
	}
	radioPump();
	if (g_radioState == 1)
		return "{\"wait\":1,\"msg\":\"Asking the radio directory...\"}";
	if (g_radioState == 3)
	{
		g_radioShown.clear();                           // the next ask tries again
		return remoteError(g_radioError);
	}
	items.clear();
	gen++;
	std::string rows;
	for (const Channel& station : g_radioList)
	{
		RemoteItem item;
		item.kind = 2;
		item.channel = station;
		std::string sub = station.group;
		if (!station.genres.empty())
			sub += (sub.empty() ? "" : "  \xC2\xB7  ") + station.genres;
		std::string extra = ",\"f\":" + std::string(radioIsFavourite(station.url) ? "1" : "0");
		if (!station.logo.empty())
			extra += ",\"p\":" + jsonText(station.logo);
		remoteRow(rows, 'c', (int)items.size(), station.name, sub, extra);
		items.push_back(item);
	}
	const char* const none = mode == "fav" ? "No favorite stations yet." : mode == "recent" ? "Nothing played yet." : "No stations found.";
	return "{\"slot\":2,\"gen\":" + std::to_string(gen) + ",\"none\":" + jsonText(none) + ",\"rows\":[" + rows + "]}";
}

// "What is in this list?" from the page.
static std::string remoteList(std::map<std::string, std::string>& fields)
{
	const std::string what = fields["what"];
	if (g_screen == LOADING)
		return "{\"wait\":1,\"msg\":\"The console is loading a playlist...\"}";
	if (!g_haveBrowse || g_active < 0)
		return remoteError("There is no playlist open on the console yet. Add one on the Setup tab.");
	if (what == "radio")
		return remoteRadio(fields);
	if (what == "search")
	{
		const std::string words = trim(fields["q"]);
		if (words.size() < 2)
			return remoteError("Type at least two letters.");
		remoteToBrowse();
		g_searchText = words.substr(0, 40);
		g_searchTyping = false;
		if (g_tab != kSearchTab)
			switchTab(kSearchTab);
		startSearch();
		return remoteSearchRows();
	}
	if (what == "searchmore")
		return remoteSearchRows();

	if (what == "episodes")
		return remoteEpisodes(fields);

	// a library: 0 live TV, 1 movies, 2 series
	const int library = atoi(fields["lib"].c_str());
	if (library < 0 || library > 2)
		return remoteError("There is no such list.");
	static const char* const kinds[3] = { "channels", "movies", "series" };
	if (!sourceHasLibrary(g_sources[(size_t)g_active], library))
		return remoteError(std::string("This playlist has no ") + kinds[library] + ".");
	Tab& tab = g_tabs[library];
	if (tab.state != 2 || tab.groupChannels.empty())
	{
		// not opened yet on the console: open it there, which fetches it
		if (library == 0)
			return remoteError("Live TV is not ready on the console.");
		if (tab.state == 3)
		{
			const std::string why = tab.error;
			tab.state = 0;                              // the next ask tries again
			return remoteError(std::string("The ") + kinds[library] + " could not be loaded: " + why);
		}
		remoteToBrowse();
		if (g_tab != library)
			switchTab(library);
		else if (tab.state == 0)
			startLibrary(library);
		pumpLibrary();
		if (tab.state != 2)
			return "{\"wait\":1,\"msg\":" + jsonText(std::string("Loading the ") + kinds[library] + " on the console...") + "}";
	}
	if (what == "favs")
		return remoteChannels(library, "Favorites", tab.favourites, S_FAVOURITES, -1, "No favorites yet.");
	if (what == "recent")
		return remoteChannels(library, library == 0 ? "Recently watched" : "Continue watching", tab.recent, S_RECENT, -1, "Nothing watched yet.");
	if (what == "group")
	{
		const int group = atoi(fields["i"].c_str());
		if (group < 0 || group >= (int)tab.playlist.groups.size() || group >= (int)tab.groupState.size() || group + 1 >= (int)tab.groupChannels.size())
			return remoteError("That group is no longer there.");
		const std::string& name = tab.playlist.groups[(size_t)group];
		const char state = tab.groupState[(size_t)group];
		if (state == 3 && g_tab == library && tab.catSel == group)
		{
			tab.groupState[(size_t)group] = 0;          // the next ask tries again
			return remoteError("The group could not be loaded: " + g_genreFailure);
		}
		if (state != 2)
		{
			remoteOpenGroup(library, group);
			pumpGenres();
			// what has arrived so far (or was kept from last time) is shown at once; the rest follows
			if (tab.groupState[(size_t)group] != 2 && !tab.groupChannels[(size_t)group + 1].empty())
				return remoteChannels(library, name, tab.groupChannels[(size_t)group + 1], S_GROUPS, group, "This group is empty.", true);
			if (tab.groupState[(size_t)group] != 2)
				return "{\"wait\":1,\"msg\":" + jsonText("Loading \"" + name + "\" on the console...") + "}";
		}
		return remoteChannels(library, name, tab.groupChannels[(size_t)group + 1], S_GROUPS, group, "This group is empty.");
	}
	// the groups (or, for a playlist without any, everything)
	if (tab.playlist.groups.empty())
		return remoteChannels(library, library == 0 ? "All channels" : library == 1 ? "All movies" : "All series", tab.groupChannels[0], S_ALL, -1, "There is nothing here.");
	std::string rows;
	for (int i = 0; i < (int)tab.playlist.groups.size(); i++)
	{
		const std::string& name = tab.playlist.groups[(size_t)i];
		if (tab.hidden.count(name))
			continue;
		const bool known = i < (int)tab.groupState.size() && tab.groupState[(size_t)i] == 2 && i + 1 < (int)tab.groupChannels.size();
		remoteRow(rows, 'g', i, name, known ? std::to_string(tab.groupChannels[(size_t)i + 1].size()) + " " + kinds[library] : std::string());
	}
	return "{\"slot\":0,\"gen\":0,\"lib\":" + std::to_string(library) + ",\"t\":\"Groups\",\"none\":\"Every group is hidden.\",\"rows\":[" + rows + "]}";
}

// "Play this" from the page.
static std::string remotePlay(std::map<std::string, std::string>& fields)
{
	if (g_screen == LOADING || !g_haveBrowse || g_active < 0)
		return remoteError("The console is not ready.");
	const int index = atoi(fields["i"].c_str()), table = atoi(fields["s"].c_str());
	if (table < 0 || table > 3)
		return remoteError("That cannot be played.");
	const std::vector<RemoteItem>& items = g_remoteTable[table];
	if (atoi(fields["g"].c_str()) != g_remoteGens[table] || index < 0 || index >= (int)items.size())
		return "{\"stale\":1,\"err\":\"That list has changed. It is being loaded again.\"}";
	RemoteItem item = items[(size_t)index];
	if (fields["act"] == "fav")
	{
		// the star beside a row: nothing is played, and the TV stays where it is
		if (item.kind == 2)
		{
			radioToggleFavourite(item.channel);
			return std::string("{\"ok\":1,\"f\":") + (radioIsFavourite(item.channel.url) ? "1" : "0") + "}";
		}
		if (item.kind != 0 || item.tab < 0 || item.tab > 2)
			return remoteError("That cannot be made a favorite from here.");
		Tab* const showing = T;
		T = &g_tabs[item.tab];
		const auto found = g_byUrl.find(item.channel.url);
		bool now = false, known = false;
		if (found != g_byUrl.end() && found->second < (int)g_isFavourite.size())
		{
			known = true;
			toggleFavourite(found->second);
			now = g_isFavourite[(size_t)found->second] != 0;
		}
		T = showing;
		if (!known)
			return remoteError("That is no longer in the list.");
		return std::string("{\"ok\":1,\"f\":") + (now ? "1" : "0") + "}";
	}
	if (fields["fresh"] == "1")
		item.at = 0;                                    // from the beginning, not from where it was left
	remoteToBrowse();
	saveResumeNow();                                    // a film being watched keeps its place
	if (item.kind == 2)
	{
		if (g_tab != kRadioTab)
			switchTab(kRadioTab);
		for (int i = 0; i < (int)g_radioList.size(); i++)
			if (g_radioList[(size_t)i].url == item.channel.url && !radioPicking())
				g_radioSel = i;
		playRadio(item.channel);
		return "{\"ok\":1,\"msg\":" + jsonText("Playing: " + item.channel.name) + "}";
	}
	if (item.kind == 3)
	{
		// an episode: played from its series, so that the next one can follow it
		if (g_tab != item.tab)
			switchTab(item.tab);
		const auto found = g_byUrl.find(item.channel.url);
		bool listed = false;
		if (found != g_byUrl.end())
			for (const int episode : T->drillList)
				if (episode == found->second)
					listed = true;
		if (!listed || T->drillState != 2)
			return "{\"stale\":1,\"err\":\"The episodes have changed. They are being listed again.\"}";
		g_fullscreen = false;
		beginWatching(found->second, item.at > 45 ? item.at : 0);
		return "{\"ok\":1,\"msg\":" + jsonText("Playing: " + item.channel.name) + "}";
	}
	if (item.kind == 1 || (item.kind == 0 && item.tab != 0))
	{
		HomeItem entry;
		entry.channel = item.channel;
		entry.tab = item.tab;
		entry.at = item.at;
		entry.length = item.length;
		const bool series = isSeriesItem(item.channel.url);
		g_fullscreen = false;
		playHomeItem(entry);
		return "{\"ok\":1,\"msg\":" + jsonText((series ? "Opened on the TV: " : "Playing: ") + item.channel.name) + "}";
	}
	// a live channel: Live TV is put on the list it came from, so that next and previous follow that list
	if (g_tab != 0)
		switchTab(0);
	if (item.section == S_GROUPS && item.group >= 0 && item.group < (int)g_playlist.groups.size())
		remoteOpenGroup(0, item.group);
	else
	{
		if ((int)g_section != item.section)
			chooseSection(item.section);
		if (!g_filter.empty())
		{
			g_filter.clear();
			T->filterCats = false;
			computeCats();
			rebuildView(-1);
		}
	}
	const auto found = g_byUrl.find(item.channel.url);
	if (found == g_byUrl.end())
		return "{\"stale\":1,\"err\":\"That channel is no longer in the list.\"}";
	g_focus = F_CHANNELS;
	for (int i = 0; i < (int)g_view.size(); i++)
		if (g_view[(size_t)i] == found->second)
			g_sel = i;
	startChannel(found->second);
	g_fullscreen = true;
	g_overlayUntil = 0;
	showControlsSoon();
	return "{\"ok\":1,\"msg\":" + jsonText("Playing: " + item.channel.name) + "}";
}

// The buttons along the page's now-playing bar.
static void remoteCommand(const std::string& command, int by)
{
	if (g_screen == LOADING || !g_nowOn)
		return;
	if (command == "stop")
	{
		stopPlayback();
		if (g_tab == kHomeTab)
			buildHome();
	}
	else if (command == "pause")
	{
		if (g_nowVod)
			player::pause(!player::paused());
	}
	else if (command == "seek")
	{
		if (g_nowVod && by >= -43200 && by <= 43200)
			player::seekBy(by);
	}
	else if (command == "next" || command == "prev")
	{
		const int step = command == "next" ? 1 : -1;
		if (g_nowTab == kRadioTab)
			radioStep(step);
		else if (g_nowVod)
		{
			if (step > 0 && hasNextEpisode())
				playNextEpisode();
		}
		else if (g_nowTab == g_tab && !g_grid)
			changeChannel(step);
		else
			toast("Next and previous follow the Live TV list: play a channel from it first");
	}
	else if (command == "full")
	{
		if (g_nowTab == kRadioTab || g_screen != BROWSE)
			return;
		if (g_fullscreen)
			leaveFullscreen();
		else
		{
			g_fullscreen = true;
			g_overlayUntil = 0;
			showControlsSoon();
		}
	}
	else if (command == "fav")
	{
		if (g_nowTab == kRadioTab)
		{
			const Channel station = g_now;
			radioToggleFavourite(station);
		}
		else if (g_nowTab >= 0 && g_nowTab < 3)
		{
			Tab* const showing = T;
			T = &g_tabs[g_nowTab];
			const auto found = g_byUrl.find(g_now.url);
			if (found != g_byUrl.end())
			{
				toggleFavourite(found->second);
				toast(std::string(g_isFavourite[(size_t)found->second] ? "Added to favorites: " : "Removed from favorites: ") + g_now.name);
			}
			T = showing;
		}
	}
}

// A button of the page's remote, as the same button on the controller.
static void remoteKey(const std::string& button)
{
	static const struct { const char* name; Action action; } keys[] = {
		{ "up", A_UP }, { "down", A_DOWN }, { "left", A_LEFT }, { "right", A_RIGHT }, { "ok", A_OK }, { "back", A_BACK },
		{ "menu", A_MENU }, { "fav", A_FAVOURITE }, { "search", A_SEARCH }, { "l1", A_PAGE_UP }, { "r1", A_PAGE_DOWN },
		{ "l2", A_TAB_PREV }, { "r2", A_TAB_NEXT }, { "more", A_MORE }, { "last", A_LAST } };
	for (const auto& key : keys)
		if (button == key.name)
		{
			g_pointerUntil = 0;                         // the buttons steer the focus, not the pointer
			if (g_actions.size() < 32)
				g_actions.push_back(key.action);
		}
}

// Typed on the page while the TV's keyboard is open: it goes where the keyboard's letters would.
static void remoteText(const std::string& typed, bool done)
{
	if (!g_keyboard || !g_keyTarget)
		return;
	std::string text;
	for (const char c : typed)
		if ((unsigned char)c >= 0x20 && text.size() < g_keyLimit)
			text += c;
	const int keep = g_keyIsFilter ? selectedChannel() : -1;
	*g_keyTarget = text;
	if (done)
		pressKey(5, 4);                                 // the keyboard's own Done
	else if (g_keyIsFilter)
		applyFilter(keep);
}

// Playlists added, files sent and backups restored from the page's Setup tab.
static void remoteSetup(web::Request& wanted)
{
	if (wanted.kind == "add")
	{
		const std::string type = wanted.fields["type"], address = trim(wanted.fields["a"]), name = trim(wanted.fields["name"]);
		Source source;
		if (type == "xtream")
			source = Source{ "xtream", name.empty() ? hostOf(address) : name, address, trim(wanted.fields["b"]), trim(wanted.fields["c"]), "", "", normalizeMac(wanted.fields["mac"]) };
		else if (type == "stalker")
		{
			const std::string mac = normalizeMac(wanted.fields["b"]);
			if (mac.empty())
			{
				g_webNote = "Not added: the MAC address needs six pairs of digits, like 00:1A:79:12:34:56";
				return;
			}
			source = Source{ "stalker", name.empty() ? hostOf(address) : name, address, mac, "", "", "" };
		}
		else
			source = Source{ "url", name.empty() ? hostOf(address) : name, address, "", "", "", "" };
		source.id = sourceId(source);
		int index = -1;
		for (int i = 0; i < (int)g_sources.size(); i++)
			if (sourceId(g_sources[(size_t)i]) == source.id)
				index = i;
		if (index < 0)
		{
			g_sources.push_back(source);
			index = (int)g_sources.size() - 1;
		}
		else
			g_sources[(size_t)index] = source;
		saveSources(g_dataDir, g_sources);
		g_webNote = "Added: " + source.name;
		logLine("web: added the playlist \"%s\" (%s)", source.name.c_str(), source.type.c_str());
		if (!g_haveBrowse)
		{
			startLoading(index);                    // the first playlist is opened straight away
			return;
		}
	}
	else if (wanted.kind == "file")
	{
		// keep the name harmless: letters, digits and a few marks only
		std::string name;
		for (const char c : wanted.fields["name"])
			name += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' ? c : '_';
		if (name.empty() || name[0] == '.')
			name = "sent-" + name + ".m3u";
		if (name.find(".m3u") == std::string::npos)
			name += ".m3u";
		const std::string folder = g_dataDir + "/playlists";
		mkdir(folder.c_str(), 0777);
		writeTextFile(folder + "/" + name, wanted.body);
		std::string check;
		if (readTextFile(folder + "/" + name, check) && check.size() == wanted.body.size())
		{
			const std::string activeId = g_active >= 0 ? sourceId(g_sources[(size_t)g_active]) : std::string();
			g_sources = loadSources(g_dataDir);      // finds the new file
			saveSources(g_dataDir, g_sources);
			for (int i = 0; i < (int)g_sources.size(); i++)
				if (sourceId(g_sources[(size_t)i]) == activeId)
					g_active = i;
			g_webNote = "Received the file: " + name;
			logLine("web: received %s (%d bytes)", name.c_str(), (int)wanted.body.size());
		}
		else
			g_webNote = "The file could not be saved on the console";
	}
	else if (wanted.kind == "restore")
	{
		// each part is "FILE <name> <bytes>", a line break, that many bytes, a line break
		int restored = 0;
		size_t pos = wanted.body.find('\n');
		while (pos != std::string::npos && pos + 1 < wanted.body.size())
		{
			const size_t lineEnd = wanted.body.find('\n', pos + 1);
			if (lineEnd == std::string::npos)
				break;
			const std::string line = wanted.body.substr(pos + 1, lineEnd - pos - 1);
			const size_t gap = line.rfind(' ');
			if (line.compare(0, 5, "FILE ") != 0 || gap == std::string::npos || gap <= 5)
				break;
			const std::string name = line.substr(5, gap - 5);
			const size_t bytes = (size_t)atol(line.c_str() + gap + 1);
			if (lineEnd + 1 + bytes > wanted.body.size())
				break;
			const bool safe = name.find('/') == std::string::npos && name.find("..") == std::string::npos
				&& name.size() > 4 && name.compare(name.size() - 4, 4, ".txt") == 0 && name.find("log") == std::string::npos;
			if (safe)
			{
				writeTextFile(g_dataDir + "/" + name, wanted.body.substr(lineEnd + 1, bytes));
				restored++;
			}
			pos = lineEnd + bytes + 1;
		}
		g_webNote = restored ? "Backup restored (" + std::to_string(restored) + " files). Close and reopen the app to use it." : "That backup held nothing usable";
		logLine("web: restored %d files from a backup", restored);
	}
}

// What the page shows about the console: asked for every second or two.
static void remotePublish()
{
	std::string json = "{\"on\":";
	json += g_nowOn ? "true" : "false";
	if (g_nowOn)
	{
		std::string sub;
		if (g_nowTab == kRadioTab)
		{
			sub = trim(player::streamTitle());
			if (sub.empty())
				sub = "Radio" + (g_now.group.empty() ? std::string() : "  \xC2\xB7  " + g_now.group);
		}
		else if (!g_nowVod)
		{
			const Programme* now = onNow(g_now.url);
			sub = now ? now->title : g_now.group;
		}
		char numbers[96];
		const bool film = g_nowVod && player::duration() > 0;
		snprintf(numbers, sizeof(numbers), ",\"vod\":%s,\"paused\":%s,\"pos\":%.0f,\"dur\":%.0f", g_nowVod ? "true" : "false",
			player::paused() ? "true" : "false", film ? player::position() : 0.0, film ? player::duration() : 0.0);
		json += ",\"name\":" + jsonText(g_now.name) + ",\"sub\":" + jsonText(sub) + numbers;
		json += ",\"kind\":" + std::to_string(g_nowTab);
		if (g_now.logo.compare(0, 4, "http") == 0)
			json += ",\"art\":" + jsonText(g_now.logo);
	}
	json += ",\"screen\":" + jsonText(g_screen == LOADING ? "Loading a playlist..." : g_screen == MANAGER ? "Settings" : g_tab >= 0 && g_tab < kTabCount ? kTabName[g_tab] : "");
	if (g_active >= 0 && g_active < (int)g_sources.size())
		json += ",\"playlist\":" + jsonText(g_sources[(size_t)g_active].name);
	if (g_keyboard && g_keyTarget)
		json += ",\"kb\":true,\"kbl\":" + jsonText(g_keyLabel) + ",\"kbt\":" + jsonText(*g_keyTarget);
	json += ",\"lists\":[";
	for (size_t i = 0; i < g_sources.size(); i++)
		json += (i ? "," : "") + jsonText(g_sources[i].name + "  (" + sourceKind(g_sources[i]) + ")");
	json += "]}";
	web::setStatus(json);
}

// Called once per picture drawn: keeps the page's server running (when it is switched on) and
// carries out whatever has been asked for since last time.
static void pumpRemote()
{
	if (!g_remoteOn)
	{
		if (web::running())
			web::stop();
		g_webTried = false;
		return;
	}
	if (!web::running())
	{
		if (g_webTried)
			return;                                     // it could not be started; Settings > Remote says why
		g_webTried = true;
		if (g_remoteKey.size() != 6)
		{
			remoteNewKey();
			saveSettings();
		}
#ifdef IPTV_HOST_HINT
		web::start(g_dataDir, kWebPort, IPTV_HOST_HINT, g_remoteKey);
#else
		web::start(g_dataDir, kWebPort, "", g_remoteKey);
#endif
		if (!web::running())
			return;
	}
	web::Request wanted;
	while (web::take(wanted))
	{
		if (wanted.kind == "key")
			remoteKey(wanted.fields["b"]);
		else if (wanted.kind == "cmd")
			remoteCommand(wanted.fields["c"], atoi(wanted.fields["by"].c_str()));
		else if (wanted.kind == "text")
			remoteText(wanted.body, wanted.fields["done"] == "1");
		else
		{
			remoteSetup(wanted);
			if (g_screen == LOADING)
				break;                                  // a first playlist is being opened; the rest waits
		}
	}
	web::Query query;
	while (web::takeQuery(query))
		web::answer(query.id, query.what == "play" ? remotePlay(query.fields) : remoteList(query.fields));
	static double publishedAt = -10;
	if (nowSeconds() - publishedAt > 0.25)
	{
		publishedAt = nowSeconds();
		remotePublish();
	}
}
