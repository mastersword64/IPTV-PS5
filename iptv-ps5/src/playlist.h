#pragma once
#include <string>
#include <vector>

struct Channel
{
	std::string name, url, group, logo, tvgId, userAgent, referer;
};

struct Playlist
{
	std::vector<Channel> channels;
	std::vector<std::string> groups;            // in order of first appearance
	std::string epgUrl;                         // url-tvg / x-tvg-url from the header, if any
	// Portals with hundreds of categories are not downloaded whole: `lazy` means only the
	// category names are here, and each category's channels are fetched when it is opened.
	bool lazy = false;
	std::vector<std::string> groupIds;          // the portal's id for each entry of `groups`
};

// Parses M3U / M3U8 playlist text (the format IPTVnator imports).
// Returns false if no channel was found.
bool parseM3U(const std::string& text, Playlist& out);

bool readTextFile(const std::string& path, std::string& out);
std::string trim(const std::string& text);
