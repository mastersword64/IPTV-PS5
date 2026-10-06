#pragma once
#include "playlist.h"
#include <functional>
#include <utility>
#include <string>
#include <vector>

// A playlist the viewer has added: an M3U file, an M3U web address, or an Xtream Codes account.
struct Source
{
	std::string type;     // "file", "url", "xtream" or "stalker"
	std::string name;
	std::string a, b, c;  // file: path | url: address | xtream: server, username, password | stalker: portal, MAC address
	std::string id;       // fixed when the playlist is added, so editing it keeps its favourites
	std::string ua;       // optional User-Agent to use for this playlist
	std::string mac;      // xtream only, optional: a MAC address to present to the server
	int epoch = -1;       // not saved: set while loading, so an abandoned load stops asking the server
};

// What a MAG set-top box calls itself; Stalker portals expect it.
extern const char* const kStalkerUserAgent;

std::string sourceId(const Source& source);              // stable, usable in file names
std::string sourceDetail(const Source& source);          // one line describing where it comes from
const char* sourceKind(const Source& source);            // "M3U file", "M3U URL", "Xtream Code", "Stalker portal"

// The saved list, plus anything found on the console: files uploaded to <data>/playlists
// and the playlist built into the app.
std::vector<Source> loadSources(const std::string& dataDir);
void saveSources(const std::string& dataDir, const std::vector<Source>& sources);

// Fetches a fresh copy (falling back to the copy saved last time when the network fails).
bool fetchSource(const Source& source, const std::string& dataDir, Playlist& out, std::string& error);

// Stalker channels are listed as "stalker://<command>"; the real stream address is asked for
// from the portal each time one is played (the addresses it hands out expire).
bool stalkerResolve(const Source& source, const std::string& command, std::string& url, std::string& error);
// What a playlist can hold: live channels, and for accounts and portals also movies and series.
enum { MEDIA_LIVE = 0, MEDIA_MOVIES = 1, MEDIA_SERIES = 2 };
bool sourceHasLibrary(const Source& source, int media);

// The categories of one kind of content. Live TV may come back complete; movies and series come
// back "lazy": category names only, with each category's entries fetched when it is opened.
bool fetchLibrary(const Source& source, int media, const std::string& dataDir, Playlist& out, std::string& error);

// Fetches one category, a page at a time. `page` is given each batch as it arrives and returns
// false to stop early.
bool fetchCategory(const Source& source, int media, const std::string& categoryId,
                   const std::function<bool(std::vector<Channel>&)>& page, std::string& error);

// A series is listed as one entry; its episodes are fetched when it is opened.
bool isSeriesItem(const std::string& url);
bool fetchEpisodes(const Source& source, const std::string& seriesUrl, std::vector<Channel>& out, std::string& error);

// Label / value pairs describing the account behind a playlist.
bool fetchAccount(const Source& source, std::vector<std::pair<std::string, std::string>>& rows, std::string& error);

// Channels written out as M3U text (used to save favourites with their names).
std::string channelsToM3U(const std::vector<Channel>& channels);
std::string normalizeMac(const std::string& typed);     // "" unless it is six pairs of hex digits
