#pragma once
#include "sources.h"

#include <string>
#include <unordered_map>
#include <vector>

// Programme guides in the XMLTV format, which M3U playlists point to with "url-tvg".
struct XmltvGuide
{
	std::unordered_map<std::string, std::vector<Programme>> programmes;   // by channel id (lower case)
	std::unordered_map<std::string, std::string> idByName;               // channel name (lower case) -> id
};

// Unpacks gzip data (guides are usually sent packed). Returns false if it is not valid gzip.
bool gunzip(const std::string& packed, std::string& out, size_t limit);

// Reads a guide, keeping programmes that end after `from` and start before `to` (seconds since 1970).
bool parseXmltv(const std::string& xml, long long from, long long to, XmltvGuide& out);
