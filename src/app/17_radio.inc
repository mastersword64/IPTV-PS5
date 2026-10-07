// ---- Radio: public stations from the Radio Browser directory --------------------------------
// The stations are not part of the app or of any playlist: they are looked up, when this screen
// asks, in a directory that volunteers keep (radio-browser.info). Chips along the top choose what
// is listed: favourites, recently played, the most played everywhere, a country, a genre, or a
// search by name. A station plays as sound only, and carries on while other screens are browsed.
// The screen is drawn the same way in both layouts, with the rail at its left.

enum RadioMode { R_FAVOURITES, R_RECENT, R_POPULAR, R_COUNTRY, R_GENRE, R_SEARCH, R_MODES };
static const char* const kRadioChip[R_MODES] = { "\xE2\x98\x85  Favorites", "Recent", "Popular", "Countries", "Genres", "Search" };
static const char* const kRadioTags[] = { "pop", "rock", "news", "talk", "jazz", "classical", "dance", "electronic", "hits", "oldies",
	"80s", "90s", "country", "hiphop", "rnb", "metal", "reggae", "latin", "soul", "blues", "folk", "ambient", "chillout", "lounge",
	"sports", "christian", "gospel", "kids", "bollywood", "hindi", "punjabi", "arabic", "world music", "soundtrack", "comedy" };
static const int kRadioTagCount = (int)(sizeof(kRadioTags) / sizeof(kRadioTags[0]));

static int g_radioMode = -1;                            // which chip is in use (-1: not opened yet)
static int g_radioLevel = 0;                            // by country / by genre: 0 choosing one, 1 its stations
static bool g_radioOnChips = false;
static int g_radioChipSel = 0;
static float g_radioChipScroll = 0;
static std::string g_radioPicked, g_radioPickedName;    // the country code or the tag chosen, and its name
static int g_radioPickSel[2] = { 0, 0 };                // where the country and genre lists were left
static std::vector<Channel> g_radioList;                // the stations on show
static std::vector<radio::Country> g_radioCountries;
static std::vector<int> g_radioCountryView;             // the countries that match what was typed
static std::string g_radioFilter, g_radioFilterDone = "\x01";
static int g_radioSel = 0, g_radioTop = 0;
static float g_radioScroll = 0;
static int g_radioState = 0;                            // 0 nothing asked, 1 asking, 2 listed, 3 failed
static std::string g_radioError, g_radioShown;          // (shown: the key of the list on screen)
static std::string g_radioWords, g_radioWordsDone;      // search: what is typed, and what was last asked for
static bool g_radioTyping = false;
static std::vector<Channel> g_radioFavourites, g_radioRecent;
static std::string g_radioListsFor = "\x01";            // which profile's favourites are loaded
static std::unordered_map<std::string, std::vector<Channel>> g_radioCache;   // lists already fetched this run

static std::mutex g_radioMutex;
static int g_radioRequest = 0;
static bool g_radioArrived = false, g_radioArrivedOk = false;
static std::string g_radioArrivedKey, g_radioArrivedError;
static std::vector<Channel> g_radioArrivedStations;
static std::vector<radio::Country> g_radioArrivedCountries;

static std::string radioFile(const char* kind)
{
	return g_dataDir + "/radio-" + kind + profileSuffix() + ".txt";
}

static void radioLoadLists()
{
	if (g_radioListsFor == profileSuffix())
		return;
	g_radioListsFor = profileSuffix();
	std::string text;
	g_radioFavourites = readTextFile(radioFile("favourites"), text) ? radio::fromText(text) : std::vector<Channel>();
	text.clear();
	g_radioRecent = readTextFile(radioFile("recent"), text) ? radio::fromText(text) : std::vector<Channel>();
}

static bool radioIsFavourite(const std::string& url)
{
	for (const Channel& station : g_radioFavourites)
		if (station.url == url)
			return true;
	return false;
}

static bool radioPlaying()
{
	return g_nowOn && g_nowTab == kRadioTab;
}

// Choosing a country or a genre, as opposed to looking at stations.
static bool radioPicking()
{
	return (g_radioMode == R_COUNTRY || g_radioMode == R_GENRE) && g_radioLevel == 0;
}

static int radioCount()
{
	if (!radioPicking())
		return (int)g_radioList.size();
	return g_radioMode == R_COUNTRY ? (int)g_radioCountryView.size() : kRadioTagCount;
}

static void radioFilterCountries()
{
	g_radioFilterDone = g_radioFilter;
	g_radioCountryView.clear();
	const std::string needle = lower(trim(g_radioFilter));
	for (int i = 0; i < (int)g_radioCountries.size(); i++)
		if (needle.empty() || lower(g_radioCountries[(size_t)i].name).find(needle) != std::string::npos || lower(g_radioCountries[(size_t)i].code) == needle)
			g_radioCountryView.push_back(i);
}

// Asks the directory for a list in the background. `key` says which: "popular", "countries",
// "country:DE", "tag:jazz" or "search:words".
static void radioAsk(const std::string& key)
{
	int request;
	{
		std::lock_guard<std::mutex> lock(g_radioMutex);
		request = ++g_radioRequest;
		g_radioArrived = false;
	}
	g_radioState = 1;
	g_radioError.clear();
	logLine("radio: asking for %s", key.c_str());
	startThread("radio", [key, request] {
		std::vector<Channel> stations;
		std::vector<radio::Country> countries;
		std::string error;
		bool ok;
		if (key == "countries")
			ok = radio::countries(countries, error);
		else if (key == "popular")
			ok = radio::popular(stations, error);
		else if (key.compare(0, 8, "country:") == 0)
			ok = radio::byCountry(key.substr(8), stations, error);
		else if (key.compare(0, 4, "tag:") == 0)
			ok = radio::byTag(key.substr(4), stations, error);
		else
			ok = radio::search(key.substr(7), stations, error);
		std::lock_guard<std::mutex> lock(g_radioMutex);
		if (request != g_radioRequest)
			return;                                     // something else was asked for meanwhile
		g_radioArrived = true;
		g_radioArrivedOk = ok;
		g_radioArrivedKey = key;
		g_radioArrivedError = error;
		g_radioArrivedStations = std::move(stations);
		g_radioArrivedCountries = std::move(countries);
	});
}

static void radioStopAsking()
{
	std::lock_guard<std::mutex> lock(g_radioMutex);
	g_radioRequest++;
	g_radioArrived = false;
}

// Puts on screen whatever the chip and level in use call for, asking the directory if need be.
static void radioShow(bool keepPlace = false)
{
	radioStopAsking();
	if (!keepPlace)
	{
		g_radioSel = g_radioTop = 0;
		g_radioScroll = 0;
	}
	g_radioError.clear();
	g_radioState = 2;
	std::string key;
	if (g_radioMode == R_FAVOURITES)
	{
		g_radioList = g_radioFavourites;
		g_radioShown = "favourites";
	}
	else if (g_radioMode == R_RECENT)
	{
		g_radioList = g_radioRecent;
		g_radioShown = "recent";
	}
	else if (g_radioMode == R_POPULAR)
		key = "popular";
	else if (g_radioMode == R_SEARCH)
	{
		g_radioWordsDone = trim(g_radioWords);
		if (g_radioWordsDone.size() < 2)
		{
			g_radioList.clear();
			g_radioShown = "search";
			g_radioState = 0;
		}
		else
			key = "search:" + g_radioWordsDone;
	}
	else if (g_radioLevel == 1)
		key = (g_radioMode == R_COUNTRY ? "country:" : "tag:") + g_radioPicked;
	else if (g_radioMode == R_COUNTRY)
	{
		g_radioShown = "countries";
		if (g_radioCountries.empty())
			radioAsk("countries");
		else
			radioFilterCountries();
	}
	else
		g_radioShown = "tags";
	if (key.empty())
		return;
	g_radioShown = key;
	const auto cached = g_radioCache.find(key);
	if (cached != g_radioCache.end())
		g_radioList = cached->second;
	else
	{
		g_radioList.clear();
		radioAsk(key);
	}
}

static void radioPump()
{
	if (g_radioTyping && !g_keyboard)
	{
		// a search was typed
		g_radioTyping = false;
		if (g_radioMode == R_SEARCH && trim(g_radioWords) != g_radioWordsDone)
			radioShow();
	}
	if (g_radioFilter != g_radioFilterDone && g_radioMode == R_COUNTRY)
	{
		radioFilterCountries();
		g_radioSel = g_radioTop = 0;
		g_radioScroll = 0;
	}
	std::lock_guard<std::mutex> lock(g_radioMutex);
	if (!g_radioArrived)
		return;
	g_radioArrived = false;
	if (!g_radioArrivedOk)
	{
		g_radioState = 3;
		g_radioError = g_radioArrivedError;
		return;
	}
	g_radioState = 2;
	if (g_radioArrivedKey == "countries")
	{
		g_radioCountries = std::move(g_radioArrivedCountries);
		radioFilterCountries();
		return;
	}
	if (g_radioCache.size() > 40)
		g_radioCache.clear();                           // (a long evening of browsing: start the store afresh)
	g_radioCache[g_radioArrivedKey] = g_radioArrivedStations;
	if (g_radioArrivedKey == g_radioShown)
		g_radioList = std::move(g_radioArrivedStations);
}

static void radioSetMode(int mode)
{
	g_radioOnChips = false;
	if (mode == R_SEARCH && trim(g_radioWords).empty())
	{
		g_radioMode = mode;
		g_radioLevel = 0;
		radioShow();
		g_radioTyping = true;
		openKeyboard(&g_radioWords, 40, false, "Search stations");
		return;
	}
	if (mode == g_radioMode && !((mode == R_COUNTRY || mode == R_GENRE) && g_radioLevel == 1))
	{
		if (g_radioState == 3)
			radioShow();                                // try again
		return;
	}
	g_radioMode = mode;
	g_radioLevel = 0;
	g_radioFilter.clear();
	radioShow();
	if (mode == R_COUNTRY || mode == R_GENRE)
	{
		g_radioSel = g_radioPickSel[mode == R_COUNTRY ? 0 : 1];
		if (g_radioSel > radioCount() - 1)
			g_radioSel = radioCount() > 0 ? radioCount() - 1 : 0;
	}
}

// Called when the Radio screen is opened.
static void openRadio()
{
	radioLoadLists();
	g_radioOnChips = false;
	if (g_radioMode < 0)
	{
		g_radioMode = g_radioFavourites.empty() ? R_POPULAR : R_FAVOURITES;
		radioShow();
	}
	else if (g_radioMode == R_FAVOURITES || g_radioMode == R_RECENT || g_radioState == 3)
		radioShow(true);
}

static void playRadio(const Channel& station)
{
	logLine("radio: %s", station.name.c_str());
	g_playTemp = false;
	g_nowTemp = true;                                   // not part of any library's history
	g_startAt = 0;
	g_lastPosition = 0;
	g_nextAt = 0;
	g_queue.clear();
	g_queueAt = -1;
	g_playing = -1;
	g_now = station;
	g_nowOn = true;
	g_nowVod = false;
	g_nowTab = kRadioTab;
	g_nowIndex = -1;
	g_fullscreen = false;
	gfx::videoClear();
	g_gotPicture = false;
	g_retries = 0;
	g_retryAt = 0;
	beginPlayback(station);
	radio::played(station.tvgId);

	radioLoadLists();
	for (size_t i = 0; i < g_radioRecent.size(); i++)
		if (g_radioRecent[i].url == station.url)
		{
			g_radioRecent.erase(g_radioRecent.begin() + (long)i);
			break;
		}
	g_radioRecent.insert(g_radioRecent.begin(), station);
	if (g_radioRecent.size() > 40)
		g_radioRecent.resize(40);
	writeTextFile(radioFile("recent"), radio::toText(g_radioRecent));
}

static void radioToggleFavourite(const Channel& station)
{
	radioLoadLists();
	bool removed = false;
	for (size_t i = 0; i < g_radioFavourites.size(); i++)
		if (g_radioFavourites[i].url == station.url)
		{
			g_radioFavourites.erase(g_radioFavourites.begin() + (long)i);
			removed = true;
			break;
		}
	if (!removed)
		g_radioFavourites.push_back(station);
	writeTextFile(radioFile("favourites"), radio::toText(g_radioFavourites));
	toast(removed ? "Removed from favorites: " + station.name : "Added to favorites: " + station.name);
	if (g_radioMode == R_FAVOURITES)
	{
		g_radioList = g_radioFavourites;
		if (g_radioSel > (int)g_radioList.size() - 1)
			g_radioSel = g_radioList.empty() ? 0 : (int)g_radioList.size() - 1;
	}
}

// The station before or after the one playing, in the list on show.
static void radioStep(int by)
{
	if (radioPicking() || g_radioList.empty())
		return;
	int place = g_radioSel;
	if (radioPlaying())
		for (int i = 0; i < (int)g_radioList.size(); i++)
			if (g_radioList[(size_t)i].url == g_now.url)
				place = i;
	place = (place + (int)g_radioList.size() + by) % (int)g_radioList.size();
	g_radioSel = place;
	const Channel station = g_radioList[(size_t)place];
	playRadio(station);
}

static void radioPick(int place)
{
	if (g_radioMode == R_COUNTRY)
	{
		if (place < 0 || place >= (int)g_radioCountryView.size())
			return;
		const radio::Country& country = g_radioCountries[(size_t)g_radioCountryView[(size_t)place]];
		g_radioPicked = country.code;
		g_radioPickedName = country.name;
		g_radioPickSel[0] = place;
	}
	else
	{
		if (place < 0 || place >= kRadioTagCount)
			return;
		g_radioPicked = g_radioPickedName = kRadioTags[place];
		g_radioPickSel[1] = place;
	}
	g_radioLevel = 1;
	radioShow();
}

// Cross on a row: choose the country or genre, or play the station (or stop it, if it is the one playing).
static void radioActivate(int place)
{
	if (radioPicking())
	{
		radioPick(place);
		return;
	}
	if (place < 0 || place >= (int)g_radioList.size())
	{
		if (g_radioMode == R_SEARCH)
		{
			g_radioTyping = true;
			openKeyboard(&g_radioWords, 40, false, "Search stations");
		}
		else if (g_radioState == 3)
			radioShow();
		return;
	}
	const Channel station = g_radioList[(size_t)place];
	if (radioPlaying() && g_now.url == station.url)
		stopPlayback();
	else
		playRadio(station);
}

static void radioBackLevel()
{
	g_radioLevel = 0;
	radioShow();
	g_radioSel = g_radioPickSel[g_radioMode == R_COUNTRY ? 0 : 1];
	if (g_radioSel > radioCount() - 1)
		g_radioSel = radioCount() > 0 ? radioCount() - 1 : 0;
}

static void radioScroll(int steps)
{
	const int count = radioCount();
	if (count <= 0)
		return;
	g_radioOnChips = false;
	g_radioSel += steps;
	if (g_radioSel < 0) g_radioSel = 0;
	if (g_radioSel > count - 1) g_radioSel = count - 1;
}

// A station's picture on a square tile: its logo, or its colour and initials.
static void radioTile(const Channel& station, float x, float y, float size, bool large)
{
	const uint32_t tint = tvTint(station.name);
	gfx::roundRect(x, y, size, size, large ? 34.0f : 14.0f, 0x1b1f2cff);
	gfx::roundRect(x, y, size, size, large ? 34.0f : 14.0f, (tint & 0xffffff00) | (large ? 0x66 : 0x90));
	const int logo = station.logo.empty() ? -1 : logoImage(station.logo, large);
	int lw = 0, lh = 0;
	if (logo >= 0 && gfx::imageSize(logo, lw, lh) && lw > 0 && lh > 0)
	{
		const float room = large ? size - 80 : size - 12;
		const float fit = room / (float)(lw > lh ? lw : lh);
		gfx::image(logo, x + (size - (float)lw * fit) / 2, y + (size - (float)lh * fit) / 2, (float)lw * fit, (float)lh * fit, faded(0xffffffff, artFade(logo)));
	}
	else
	{
		gfx::bold(true);
		const float letters = large ? 96.0f : 24.0f;
		centredTight(x, size, y + size / 2 - letters * 0.62f, letters, kTvWhite, initials(station.name));
		gfx::bold(false);
	}
}

static void drawRadio(const std::string& status)
{
	radioPump();
	const float keepScale = gfx::radiusScale();
	gfx::setRadiusScale(1.0f);

	const bool playing = radioPlaying(), picking = radioPicking();
	const int count = radioCount();
	if (g_radioSel > count - 1)
		g_radioSel = count > 0 ? count - 1 : 0;
	const bool listFocus = !pointerOn() && !g_radioOnChips && !g_tvOnRail;
	// the station the right-hand side is about: the one playing, or else the one in focus
	const Channel* shown = playing ? &g_now : (!picking && g_radioSel < (int)g_radioList.size()) ? &g_radioList[(size_t)g_radioSel] : nullptr;
	tvWash(shown ? shown->name : "Radio", 760);

	const float px = 92, pw = 720;
	gfx::rect(px, 0, pw, 1080, 0x080a12c8);
	const std::string clock = clockNow();
	gfx::text(1860 - gfx::textWidth(26, clock), 50, 26, kTvSoft, clock);

	// heading and chips
	gfx::bold(true);
	gfx::text(px + 36, 40, 36, kTvWhite, "Radio");
	gfx::bold(false);
	gfx::text(px + 160, 54, 19, kTvDim, "Public stations from radio-browser.info");
	if (g_radioChipSel > R_MODES - 1)
		g_radioChipSel = R_MODES - 1;
	float chipX[R_MODES + 1];
	float total = 0;
	for (int i = 0; i < R_MODES; i++)
	{
		chipX[i] = total;
		total += gfx::textWidth(18, kRadioChip[i]) + 26 + 8;
	}
	chipX[R_MODES] = total;
	{
		const int watch = g_radioOnChips ? g_radioChipSel : g_radioMode < 0 ? 0 : g_radioMode;
		float want = g_radioChipScroll;
		const float room = pw - 60;
		if (chipX[watch] < want) want = chipX[watch];
		if (chipX[watch + 1] > want + room) want = chipX[watch + 1] - room;
		glide(g_radioChipScroll, want < 0 ? 0 : want, 16.0f);
	}
	gfx::clip(px + 20, 96, pw - 30, 54);
	for (int i = 0; i < R_MODES; i++)
	{
		const float x = px + 36 + chipX[i] - g_radioChipScroll, w = chipX[i + 1] - chipX[i] - 8;
		const bool focused = (g_radioOnChips && !pointerOn() && !g_tvOnRail && i == g_radioChipSel) || hovering(x, 104, w, 38);
		const bool on = i == g_radioMode;
		gfx::roundRect(x, 104, w, 38, 19, focused ? kTvWhite : on ? 0xffffffd0 : 0xffffff26);
		gfx::bold(on || focused);
		gfx::text(x + 13, 114, 18, focused || on ? kTvInk : kTvWhite, kRadioChip[i]);
		gfx::bold(false);
		clickable(x, 104, w, 38, [i] { g_radioChipSel = i; radioSetMode(i); });
	}
	gfx::unclip();

	// what the list is
	std::string heading;
	if (g_radioMode == R_FAVOURITES) heading = "Favorite stations";
	else if (g_radioMode == R_RECENT) heading = "Recently played";
	else if (g_radioMode == R_POPULAR) heading = "Most played around the world";
	else if (g_radioMode == R_SEARCH) heading = g_radioWordsDone.empty() ? "Search by name" : "Search:  " + g_radioWordsDone;
	else if (picking) heading = g_radioMode == R_COUNTRY ? (g_radioFilter.empty() ? "Countries" : "Countries   \xC2\xB7   \"" + g_radioFilter + "\"") : "Genres";
	else heading = "\xE2\x80\xB9  " + g_radioPickedName;
	if (!picking && g_radioState == 2 && count > 0)
		heading += "   \xC2\xB7   " + std::to_string(count);
	gfx::text(px + 36, 170, 21, kTvDim, heading, pw - 72);
	if ((g_radioMode == R_COUNTRY || g_radioMode == R_GENRE) && g_radioLevel == 1)
		clickable(px + 20, 160, pw - 40, 44, [] { radioBackLevel(); });
	else if (g_radioMode == R_SEARCH)
		clickable(px + 20, 160, pw - 40, 44, [] { g_radioTyping = true; openKeyboard(&g_radioWords, 40, false, "Search stations"); });

	const float listTop = 216;
	if (count == 0)
	{
		if (g_radioState == 1)
		{
			spinner(px + pw / 2, 400, 24, kTvWhite);
			centredIn(px, pw, 446, 22, kTvDim, "Asking the radio directory...");
		}
		else if (g_radioState == 3)
		{
			centredIn(px, pw, 380, 24, kTvWhite, "The list could not be fetched");
			drawWrapped(px + 80, 424, pw - 160, 19, kTvDim, g_radioError, 3);
			centredIn(px, pw, 520, 20, kTvDim, "Press \xE2\x9C\x95 to try again");
			clickable(px + 20, 360, pw - 40, 200, [] { radioShow(); });
		}
		else if (g_radioMode == R_FAVOURITES)
			centredIn(px, pw, 420, 22, kTvDim, "No favorites yet (\xE2\x96\xA1 on a station adds one)");
		else if (g_radioMode == R_RECENT)
			centredIn(px, pw, 420, 22, kTvDim, "Nothing played yet");
		else if (g_radioMode == R_SEARCH)
			centredIn(px, pw, 420, 22, kTvDim, g_radioWordsDone.size() < 2 ? "\xE2\x9C\x95  to type a station's name" : "Nothing found for \"" + g_radioWordsDone + "\"");
		else if (g_radioMode == R_COUNTRY && picking)
			centredIn(px, pw, 420, 22, kTvDim, "No country matches \"" + g_radioFilter + "\"");
		else
			centredIn(px, pw, 420, 22, kTvDim, "No stations here that this app can play");
	}

	const int rows = picking ? 12 : 8;
	const float rowH = picking ? 70.0f : 104.0f;
	keepVisible(g_radioSel, g_radioTop, count, rows);
	if (g_radioScroll - (float)g_radioTop > 24 || (float)g_radioTop - g_radioScroll > 24)
		g_radioScroll = (float)g_radioTop;
	glide(g_radioScroll, (float)g_radioTop, 20.0f);
	gfx::clip(px, listTop - 8, pw, (float)rows * rowH + 16);
	for (int i = (int)g_radioScroll; i < (int)g_radioScroll + rows + 1 && i < count; i++)
	{
		const float y = listTop + ((float)i - g_radioScroll) * rowH;
		const bool focused = (i == g_radioSel && listFocus) || hovering(px + 20, y, pw - 40, rowH - 10);
		if (picking)
		{
			std::string name, extra;
			if (g_radioMode == R_COUNTRY)
			{
				const radio::Country& country = g_radioCountries[(size_t)g_radioCountryView[(size_t)i]];
				name = country.name;
				extra = std::to_string(country.stations);
			}
			else
			{
				name = kRadioTags[i];
				name[0] = (char)(name[0] >= 'a' && name[0] <= 'z' ? name[0] - 32 : name[0]);
			}
			if (focused)
			{
				gfx::roundRect(px + 14, y - 6, pw - 28, rowH + 2, 22, 0xffffff30);
				gfx::roundRect(px + 20, y, pw - 40, rowH - 10, 18, kTvWhite);
			}
			const float ew = gfx::textWidth(19, extra);
			gfx::text(px + pw - 44 - ew, y + 20, 19, focused ? 0x0e121ea0 : kTvDim, extra);
			gfx::bold(focused);
			gfx::text(px + 44, y + 16, 24, focused ? kTvInk : kTvWhite, name, pw - 120 - ew);
			gfx::bold(false);
		}
		else
		{
			const Channel& station = g_radioList[(size_t)i];
			const bool onAir = playing && station.url == g_now.url;
			if (focused)
			{
				gfx::roundRect(px + 14, y - 6, pw - 28, rowH + 2, 24, 0xffffff30);
				gfx::roundRect(px + 20, y, pw - 40, rowH - 10, 20, kTvWhite);
			}
			else if (onAir)
				gfx::roundRect(px + 20, y, pw - 40, rowH - 10, 20, 0xffffff1e);
			const uint32_t ink = focused ? kTvInk : kTvWhite, faint = focused ? 0x0e121eb0 : kTvDim;
			radioTile(station, px + 36, y + 13, 68, false);
			gfx::bold(true);
			gfx::text(px + 122, y + 14, 23, ink, station.name, pw - 240);
			gfx::bold(false);
			float sx = px + 122;
			if (onAir)
			{
				gfx::roundRect(sx, y + 50, 74, 24, 12, 0xe23c46ff);
				gfx::bold(true);
				centredTight(sx, 74, y + 54, 14, kTvWhite, "ON AIR");
				gfx::bold(false);
				sx += 86;
			}
			std::string about = station.group;
			if (!station.genres.empty())
				about += (about.empty() ? "" : "  \xC2\xB7  ") + station.genres;
			gfx::text(sx, y + 51, 17, faint, about, px + pw - 110 - sx);
			if (radioIsFavourite(station.url))
				gfx::text(px + pw - 76, y + 30, 22, focused ? 0xc08a00ff : 0xffd75eff, "\xE2\x98\x85");
		}
		clickable(px + 20, y, pw - 40, rowH - 10, [i] {
			g_radioOnChips = g_tvOnRail = false;
			g_radioSel = i;
			radioActivate(i);
		});
	}
	gfx::unclip();
	if (count > rows)
	{
		// how far down the list this is
		const float trackH = (float)rows * rowH - 20, size = trackH * (float)rows / (float)count < 40 ? 40.0f : trackH * (float)rows / (float)count;
		gfx::roundRect(px + pw - 12, listTop + (trackH - size) * g_radioScroll / (float)(count - rows), 4, size, 2, 0xffffff40);
	}
	gfx::text(px + 36, 1046, 17, kTvDim, picking ? (g_radioMode == R_COUNTRY ? "L1 / R1 List   \xE2\x9C\x95 Open   \xE2\x96\xB3 Filter countries" : "L1 / R1 List   \xE2\x9C\x95 Open   \xE2\x96\xB3 Search")
		: "L1 / R1 List   \xE2\x9C\x95 Play / Stop   \xE2\x96\xA1 Favorite   \xE2\x96\xB3 Search", pw - 60);

	// the right-hand side: the station, large
	const float ax = px + pw, aw = 1920 - ax, mid = ax + aw / 2;
	if (!shown)
	{
		gfx::bold(true);
		centredIn(ax, aw, 450, 40, kTvSoft, "Radio");
		gfx::bold(false);
		centredIn(ax, aw, 516, 22, kTvDim, picking ? "Choose a country or a genre, then a station" : "Choose a station and press \xE2\x9C\x95 to listen");
		centredIn(ax, aw, 552, 20, kTvDim, "A station carries on playing while you look at other screens.");
	}
	else
	{
		const Channel& station = *shown;
		radioTile(station, mid - 150, 150, 300, true);
		const float in = tvHeroFade(station.url);
		gfx::bold(true);
		centredIn(ax + 40, aw - 80, 486, 50, faded(kTvWhite, in), station.name);
		gfx::bold(false);
		// its facts, as chips, centred
		std::vector<std::string> facts;
		if (!station.group.empty()) facts.push_back(station.group);
		if (!station.genres.empty()) facts.push_back(station.genres);
		if (!station.length.empty()) facts.push_back(station.length);
		float factsW = 0;
		for (const std::string& fact : facts)
			factsW += gfx::textWidth(19, fact) + 38;
		float fx = mid - (factsW - 10) / 2;
		if (factsW < aw - 60)
			for (const std::string& fact : facts)
				fx += tvChip(fx, 566, fact);
		else if (!facts.empty())
			centredIn(ax + 40, aw - 80, 572, 19, kTvSoft, facts[0]);

		// the song, when the station says; otherwise how things stand
		std::string line, small;
		if (playing)
		{
			const std::string song = trim(player::streamTitle());
			const bool sounding = status.empty() || status == "Sound only";
			if (!sounding)
				line = status;
			else if (!song.empty())
			{
				line = song;
				small = "NOW PLAYING";
			}
			else
				line = "On air";
			// sound bars: moving while there is sound, still otherwise
			const int bars = 28;
			const float bw = 10, gap = 8, bx = mid - ((float)bars * (bw + gap) - gap) / 2, base = 800;
			const double t = nowSeconds();
			for (int b = 0; b < bars; b++)
			{
				float level = 0.08f;
				if (sounding && !player::paused())
				{
					const float a = (float)sin(t * 5.1 + b * 0.9), c = (float)sin(t * 3.3 + b * 1.7), d = (float)sin(t * 7.7 + b * 0.37);
					level = 0.18f + 0.82f * (0.5f + 0.5f * (a * 0.5f + c * 0.3f + d * 0.2f));
					level *= 0.55f + 0.45f * (float)sin((float)b / (float)(bars - 1) * 3.14159f);
				}
				const float h = 6 + 74 * level;
				gfx::roundRect(bx + (float)b * (bw + gap), base - h, bw, h, 5, sounding ? 0xffffffd8 : 0xffffff50);
			}
		}
		else
			line = "\xE2\x9C\x95  Listen";
		if (!small.empty())
		{
			gfx::bold(true);
			centredTight(ax, aw, 632, 15, kTvDim, small);
			gfx::bold(false);
		}
		centredIn(ax + 60, aw - 120, small.empty() ? 646.0f : 658.0f, 28, playing ? kTvWhite : kTvSoft, line);

		// two buttons, for the pointer (and to say what the buttons on the controller do)
		const bool favourite = radioIsFavourite(station.url);
		const std::string labels[2] = { playing ? "\xE2\x96\xA0   Stop" : "\xE2\x96\xB6   Listen", favourite ? "\xE2\x98\x85   In favorites" : "\xE2\x98\x86   Add to favorites" };
		float widths[2], all = 18;
		for (int b = 0; b < 2; b++)
		{
			widths[b] = gfx::textWidth(21, labels[b]) + 64;
			all += widths[b];
		}
		float bx = mid - all / 2;
		const Channel copy = station;
		gfx::bold(true);
		for (int b = 0; b < 2; b++)
		{
			const bool hot = hovering(bx, 860, widths[b], 62);
			gfx::roundRect(bx, 860, widths[b], 62, 31, hot ? kTvWhite : b == 0 ? 0xffffffd8 : 0xffffff2c);
			centredTight(bx, widths[b], 879, 21, hot || b == 0 ? kTvInk : kTvWhite, labels[b]);
			clickable(bx, 860, widths[b], 62, [b, copy, playing] {
				if (b == 1)
					radioToggleFavourite(copy);
				else if (playing)
					stopPlayback();
				else
					playRadio(copy);
			});
			bx += widths[b] + 18;
		}
		gfx::bold(false);
	}

	drawTvRail();
	gfx::setRadiusScale(keepScale);
}

static void radioInput(Action action)
{
	if (tvRailInput(action))
		return;
	if (action == A_SEARCH)
	{
		if (radioPicking() && g_radioMode == R_COUNTRY)
			openKeyboard(&g_radioFilter, 20, false, "Filter countries");
		else
		{
			g_radioOnChips = false;
			if (g_radioMode != R_SEARCH)
			{
				g_radioMode = R_SEARCH;
				g_radioLevel = 0;
				radioShow();
			}
			g_radioTyping = true;
			openKeyboard(&g_radioWords, 40, false, "Search stations");
		}
		return;
	}
	if (action == A_PAGE_UP || action == A_PAGE_DOWN)
	{
		// L1 / R1: the list before or after this one
		int mode = (g_radioMode + R_MODES + (action == A_PAGE_UP ? -1 : 1)) % R_MODES;
		if (mode == R_SEARCH && trim(g_radioWords).empty())
			mode = action == A_PAGE_UP ? R_GENRE : R_FAVOURITES;    // (Search with nothing typed is not a list)
		g_radioChipSel = mode;
		radioSetMode(mode);
		return;
	}
	if (g_radioOnChips)
	{
		switch (action)
		{
		case A_LEFT:
			if (g_radioChipSel > 0)
				g_radioChipSel--;
			else
			{
				g_radioOnChips = false;
				tvOpenRail();
			}
			break;
		case A_RIGHT: if (g_radioChipSel < R_MODES - 1) g_radioChipSel++; break;
		case A_OK: radioSetMode(g_radioChipSel); break;
		case A_DOWN:
		case A_BACK: g_radioOnChips = false; break;
		default: break;
		}
		return;
	}
	const int count = radioCount();
	switch (action)
	{
	case A_UP:
		if (g_radioSel == 0 || count == 0)
		{
			g_radioOnChips = true;
			g_radioChipSel = g_radioMode < 0 ? 0 : g_radioMode;
		}
		else
			g_radioSel--;
		break;
	case A_DOWN: if (g_radioSel < count - 1) g_radioSel++; break;
	case A_LEFT:
	case A_BACK:
		if ((g_radioMode == R_COUNTRY || g_radioMode == R_GENRE) && g_radioLevel == 1)
			radioBackLevel();
		else if (g_radioMode == R_COUNTRY && !g_radioFilter.empty() && action == A_BACK)
			g_radioFilter.clear();
		else
			tvOpenRail();
		break;
	case A_OK: radioActivate(count > 0 ? g_radioSel : -1); break;
	case A_FAVOURITE:
		if (!radioPicking() && g_radioSel < (int)g_radioList.size())
		{
			const Channel station = g_radioList[(size_t)g_radioSel];
			radioToggleFavourite(station);
		}
		else if (radioPlaying())
		{
			const Channel station = g_now;
			radioToggleFavourite(station);
		}
		break;
	default: break;
	}
}
