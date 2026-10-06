#include "playlist.h"
#include <cstdio>
#include <unordered_set>

std::string trim(const std::string& text)
{
	size_t a = 0, b = text.size();
	while (a < b && (unsigned char)text[a] <= ' ') a++;
	while (b > a && (unsigned char)text[b - 1] <= ' ') b--;
	return text.substr(a, b - a);
}

bool readTextFile(const std::string& path, std::string& out)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	out.clear();
	std::vector<char> block(65536); // not on the stack: background threads may have a small one
	size_t n;
	while ((n = fread(block.data(), 1, block.size(), f)) > 0)
		out.append(block.data(), n);
	fclose(f);
	return true;
}

static bool startsWith(const std::string& s, const char* prefix)
{
	size_t i = 0;
	for (; prefix[i]; i++)
	{
		if (i >= s.size())
			return false;
		char a = s[i], b = prefix[i];
		if (a >= 'a' && a <= 'z') a = (char)(a - 32);
		if (b >= 'a' && b <= 'z') b = (char)(b - 32);
		if (a != b)
			return false;
	}
	return true;
}

// Reads key="value" attributes from an #EXTINF or #EXTM3U line.
static std::string attribute(const std::string& line, const char* key)
{
	const std::string needle = std::string(key) + "=";
	size_t at = 0;
	while ((at = line.find(needle, at)) != std::string::npos)
	{
		// must be the start of a word, so "tvg-name" does not match inside "x-tvg-name"
		if (at > 0 && line[at - 1] != ' ' && line[at - 1] != '\t' && line[at - 1] != ':')
		{
			at += needle.size();
			continue;
		}
		size_t v = at + needle.size();
		if (v < line.size() && line[v] == '"')
		{
			const size_t end = line.find('"', v + 1);
			if (end == std::string::npos)
				return line.substr(v + 1);
			return line.substr(v + 1, end - v - 1);
		}
		size_t end = v;
		while (end < line.size() && line[end] != ' ' && line[end] != ',')
			end++;
		return line.substr(v, end - v);
	}
	return "";
}

// The display name is whatever follows the first comma that is not inside quotes.
static std::string displayName(const std::string& line)
{
	bool quoted = false;
	for (size_t i = 0; i < line.size(); i++)
	{
		if (line[i] == '"')
			quoted = !quoted;
		else if (line[i] == ',' && !quoted)
			return trim(line.substr(i + 1));
	}
	const size_t comma = line.rfind(',');   // unbalanced quotes: fall back to the last comma
	return comma == std::string::npos ? "" : trim(line.substr(comma + 1));
}

bool parseM3U(const std::string& text, Playlist& out)
{
	out = Playlist();
	std::unordered_set<std::string> seenGroups;
	Channel pending;
	bool havePending = false;

	size_t pos = 0;
	if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
		pos = 3; // UTF-8 byte order mark

	while (pos < text.size())
	{
		size_t end = text.find('\n', pos);
		if (end == std::string::npos)
			end = text.size();
		const std::string line = trim(text.substr(pos, end - pos));
		pos = end + 1;
		if (line.empty())
			continue;

		if (line[0] == '#')
		{
			if (startsWith(line, "#EXTINF:"))
			{
				pending = Channel();
				havePending = true;
				pending.name = displayName(line);
				pending.tvgId = attribute(line, "tvg-id");
				pending.logo = attribute(line, "tvg-logo");
				pending.group = attribute(line, "group-title");
				pending.userAgent = attribute(line, "user-agent");
				if (pending.name.empty())
					pending.name = attribute(line, "tvg-name");
			}
			else if (startsWith(line, "#EXTGRP:"))
			{
				if (havePending && pending.group.empty())
					pending.group = trim(line.substr(8));
			}
			else if (startsWith(line, "#EXTVLCOPT:"))
			{
				const std::string option = trim(line.substr(11));
				if (startsWith(option, "http-user-agent="))
					pending.userAgent = option.substr(16);
				else if (startsWith(option, "http-referrer="))
					pending.referer = option.substr(14);
			}
			else if (startsWith(line, "#EXTM3U"))
			{
				out.epgUrl = attribute(line, "url-tvg");
				if (out.epgUrl.empty())
					out.epgUrl = attribute(line, "x-tvg-url");
			}
			continue;
		}

		// Anything else is the address of the channel described just above.
		if (line.find("://") == std::string::npos)
			continue;
		Channel channel = havePending ? pending : Channel();
		channel.url = line;
		if (channel.name.empty())
			channel.name = line;
		if (channel.group.empty())
			channel.group = "Ungrouped";
		if (seenGroups.insert(channel.group).second)
			out.groups.push_back(channel.group);
		out.channels.push_back(channel);
		pending = Channel();
		havePending = false;
	}
	return !out.channels.empty();
}
