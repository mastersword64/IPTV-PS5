#include "radio.h"
#include "json.h"
#include "log.h"
#include "net.h"
#include "threads.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>

namespace
{
	// The directory is several servers holding the same data. The first name hands out any of
	// them; the others are tried by name if it cannot be reached. Whichever answers is kept.
	const char* const kServers[] = {
		"https://all.api.radio-browser.info", "http://all.api.radio-browser.info",
		"https://de1.api.radio-browser.info", "http://de1.api.radio-browser.info",
		"https://de2.api.radio-browser.info", "http://de2.api.radio-browser.info",
		"https://fi1.api.radio-browser.info", "http://fi1.api.radio-browser.info" };
	const int kServerCount = (int)(sizeof(kServers) / sizeof(kServers[0]));
	const char* const kAgent = "IPTV-for-PS5/1.1";          // the directory asks apps to say who they are
	std::mutex& g_mutex = *new std::mutex;
	int g_server = 0;                                       // the one that answered last

	std::string encode(const std::string& text)
	{
		static const char hex[] = "0123456789ABCDEF";
		std::string out;
		for (const unsigned char c : text)
		{
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
				out += (char)c;
			else
			{
				out += '%';
				out += hex[c >> 4];
				out += hex[c & 15];
			}
		}
		return out;
	}

	bool ask(const std::string& path, std::string& body, std::string& error)
	{
		int first;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			first = g_server;
		}
		for (int attempt = 0; attempt < kServerCount; attempt++)
		{
			const int server = (first + attempt) % kServerCount;
			if (httpGetOwn(std::string(kServers[server]) + path, body, error, kAgent, 20, 6u * 1024 * 1024) && !body.empty() && (body[0] == '[' || body[0] == '{'))
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_server = server;
				return true;
			}
			logLine("radio: %s did not answer (%s; %d bytes, starting \"%.60s\")", kServers[server], error.empty() ? "not a list" : error.c_str(), (int)body.size(), body.c_str());
			error.clear();
		}
		error = "The radio directory could not be reached. Check the console's internet connection. (The log, iptv-log.txt, says what each server answered.)";
		return false;
	}

	std::string tidy(const std::string& text)
	{
		// one line, no tabs, no leading or trailing spaces
		std::string out;
		for (const char c : text)
			out += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
		return trim(out);
	}

	std::string upper(std::string text)
	{
		for (char& c : text)
			if (c >= 'a' && c <= 'z')
				c = (char)(c - 32);
		return text;
	}

	bool stations(const std::string& path, std::vector<Channel>& out, std::string& error)
	{
		out.clear();
		std::string body;
		if (!ask(path, body, error))
			return false;
		JsonFields ignored;
		const bool ok = jsonRead(body, "", ignored, [&](const JsonFields& item) {
			Channel station;
			station.name = tidy(item.get("name"));
			station.url = trim(item.get("url_resolved"));
			if (station.url.empty())
				station.url = trim(item.get("url"));
			if (station.name.empty() || station.url.compare(0, 4, "http") != 0)
				return;
			// only the kinds of sound this app can play
			const std::string codec = upper(item.get("codec"));
			if (codec != "MP3" && codec != "AAC" && codec != "AAC+")
				return;
			for (const Channel& have : out)
				if (have.url == station.url)
					return;                             // some stations are listed twice
			station.logo = trim(item.get("favicon"));
			if (station.logo.compare(0, 4, "http") != 0)
				station.logo.clear();
			station.group = tidy(item.get("country"));
			station.tvgId = item.get("stationuuid");
			station.userAgent = "VLC/3.0.20 LibVLC/3.0.20";
			// up to three tags, as words
			const std::string tags = tidy(item.get("tags"));
			int taken = 0;
			size_t pos = 0;
			while (pos < tags.size() && taken < 3)
			{
				size_t end = tags.find(',', pos);
				if (end == std::string::npos)
					end = tags.size();
				const std::string tag = trim(tags.substr(pos, end - pos));
				if (!tag.empty() && tag.size() <= 24)
				{
					station.genres += (station.genres.empty() ? "" : ", ") + tag;
					taken++;
				}
				pos = end + 1;
			}
			const int rate = atoi(item.get("bitrate").c_str());
			station.length = codec + (rate > 0 ? " \xC2\xB7 " + std::to_string(rate) + " kbps" : std::string());
			out.push_back(station);
		});
		if (!ok)
		{
			error = "The radio directory sent something that could not be read.";
			return false;
		}
		logLine("radio: %d stations for %s", (int)out.size(), path.c_str());
		return true;
	}
}

bool radio::popular(std::vector<Channel>& out, std::string& error)
{
	return stations("/json/stations/search?order=clickcount&reverse=true&hidebroken=true&limit=150", out, error);
}

bool radio::byCountry(const std::string& code, std::vector<Channel>& out, std::string& error)
{
	return stations("/json/stations/search?countrycode=" + encode(code) + "&order=clickcount&reverse=true&hidebroken=true&limit=300", out, error);
}

bool radio::byTag(const std::string& tag, std::vector<Channel>& out, std::string& error)
{
	return stations("/json/stations/search?tag=" + encode(tag) + "&order=clickcount&reverse=true&hidebroken=true&limit=200", out, error);
}

bool radio::search(const std::string& words, std::vector<Channel>& out, std::string& error)
{
	return stations("/json/stations/search?name=" + encode(words) + "&order=clickcount&reverse=true&hidebroken=true&limit=200", out, error);
}

bool radio::countries(std::vector<Country>& out, std::string& error)
{
	out.clear();
	std::string body;
	if (!ask("/json/countries?hidebroken=true", body, error))
		return false;
	JsonFields ignored;
	const bool ok = jsonRead(body, "", ignored, [&](const JsonFields& item) {
		Country country;
		country.name = tidy(item.get("name"));
		country.code = upper(trim(item.get("iso_3166_1")));
		country.stations = atoi(item.get("stationcount").c_str());
		if (country.name.empty() || country.code.size() != 2 || country.stations <= 0)
			return;
		if (country.name.size() > 40)
			country.name = country.name.substr(0, 40);  // a few have a full official title
		out.push_back(country);
	});
	if (!ok || out.empty())
	{
		error = "The radio directory sent something that could not be read.";
		return false;
	}
	std::sort(out.begin(), out.end(), [](const Country& a, const Country& b) { return a.name < b.name; });
	return true;
}

void radio::played(const std::string& stationId)
{
	if (stationId.empty())
		return;
	startThread("radio-count", [stationId] {
		int server;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			server = g_server;
		}
		std::string body, error;
		httpGetOwn(std::string(kServers[server]) + "/json/url/" + encode(stationId), body, error, kAgent, 10, 65536);
	});
}

std::string radio::toText(const std::vector<Channel>& stations)
{
	std::string text;
	for (const Channel& station : stations)
		text += tidy(station.name) + "\t" + tidy(station.url) + "\t" + tidy(station.logo) + "\t" + tidy(station.group) + "\t"
			+ tidy(station.genres) + "\t" + tidy(station.length) + "\t" + tidy(station.tvgId) + "\n";
	return text;
}

std::vector<Channel> radio::fromText(const std::string& text)
{
	std::vector<Channel> stations;
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t end = text.find('\n', pos);
		if (end == std::string::npos)
			end = text.size();
		const std::string line = text.substr(pos, end - pos);
		pos = end + 1;
		std::vector<std::string> parts;
		size_t at = 0;
		while (at <= line.size())
		{
			size_t tab = line.find('\t', at);
			if (tab == std::string::npos)
				tab = line.size();
			parts.push_back(line.substr(at, tab - at));
			at = tab + 1;
		}
		if (parts.size() < 2 || parts[0].empty() || parts[1].compare(0, 4, "http") != 0)
			continue;
		parts.resize(7);
		Channel station;
		station.name = parts[0];
		station.url = parts[1];
		station.logo = parts[2];
		station.group = parts[3];
		station.genres = parts[4];
		station.length = parts[5];
		station.tvgId = parts[6];
		station.userAgent = "VLC/3.0.20 LibVLC/3.0.20";
		stations.push_back(station);
	}
	return stations;
}
