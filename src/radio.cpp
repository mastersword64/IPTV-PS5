#include "radio.h"
#include "json.h"
#include "log.h"
#include "net.h"
#include "threads.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

// (looked for when used: a console's system is not guaranteed to offer them to an app)
#pragma weak socket
#pragma weak connect
#pragma weak send
#pragma weak recv

namespace
{
	// The directory is several servers holding the same data. Whichever answers is kept.
	const char* const kHosts[] = { "de1.api.radio-browser.info", "all.api.radio-browser.info", "de2.api.radio-browser.info", "fi1.api.radio-browser.info" };
	const int kHostCount = (int)(sizeof(kHosts) / sizeof(kHosts[0]));
	// Where the directory was when this was written: the last resort when no lookup works at all.
	const char* const kLastKnownAddress = "91.98.4.78";
	const char* const kAgent = "IPTV-for-PS5/2.0";          // the directory asks apps to say who they are
	std::mutex& g_mutex = *new std::mutex;
	std::string g_good;                                     // "scheme://host|pin" of the way that worked last

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

	// A download that is given up on after `seconds`, whatever the console's system is doing with
	// the connection (an attempt to connect to somewhere unreachable can otherwise sit for over a
	// minute). The attempt itself is left to finish on its own; nothing waits for it.
	bool timedGet(const std::string& url, std::string& out, std::string& error, int seconds, size_t limit, const std::string& pin = "", const std::string& header = "")
	{
		struct Attempt
		{
			std::mutex m;
			std::condition_variable done;
			bool finished = false, ok = false;
			std::string body, error;
		};
		const std::shared_ptr<Attempt> attempt = std::make_shared<Attempt>();
		startThread("radio-get", [attempt, url, seconds, limit, pin, header] {
			std::string body, why;
			const bool ok = httpGetOwn(url, body, why, kAgent, seconds, limit, pin, header);
			std::lock_guard<std::mutex> lock(attempt->m);
			attempt->ok = ok;
			attempt->body = std::move(body);
			attempt->error = why;
			attempt->finished = true;
			attempt->done.notify_all();
		});
		std::unique_lock<std::mutex> lock(attempt->m);
		if (!attempt->done.wait_for(lock, std::chrono::seconds(seconds + 1), [&attempt] { return attempt->finished; }))
		{
			out.clear();
			error = "no answer in time";
			return false;
		}
		out = std::move(attempt->body);
		error = attempt->error;
		return attempt->ok;
	}

	// Finds a host's addresses by asking a public name server directly, in the plain way name
	// servers are asked (one small packet out, one back). The console's own name service is not
	// involved, so one that is set up to answer only for some names does not matter.
	std::vector<std::string> lookUp(const std::string& host)
	{
		std::vector<std::string> found;
		if (!socket || !connect || !send || !recv)
			return found;
		static const char* const servers[] = { "8.8.8.8", "1.1.1.1", "9.9.9.9", "208.67.222.222" };
		// the question: "the ordinary address (A) of <host>?"
		unsigned char question[300];
		size_t size = 0;
		const unsigned char head[12] = { 0x52, 0x42, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0 };
		memcpy(question, head, 12);
		size = 12;
		size_t at = 0;
		while (at <= host.size())
		{
			size_t dot = host.find('.', at);
			if (dot == std::string::npos)
				dot = host.size();
			const size_t length = dot - at;
			if (length == 0 || length > 63 || size + length + 6 > sizeof(question))
				return found;
			question[size++] = (unsigned char)length;
			memcpy(question + size, host.data() + at, length);
			size += length;
			at = dot + 1;
		}
		question[size++] = 0;
		question[size++] = 0; question[size++] = 1;       // an address
		question[size++] = 0; question[size++] = 1;       // of the internet
		for (const char* server : servers)
		{
			const int s = socket(AF_INET, SOCK_DGRAM, 0);
			if (s < 0)
				continue;
			const int flags = fcntl(s, F_GETFL, 0);
			fcntl(s, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);
			sockaddr_in to;
			memset(&to, 0, sizeof(to));
			to.sin_family = AF_INET;
			to.sin_port = htons(53);
			to.sin_addr.s_addr = inet_addr(server);
			unsigned char answer[1500];
			ssize_t got = -1;
			if (connect(s, (sockaddr*)&to, sizeof(to)) == 0 && send(s, question, size, 0) == (ssize_t)size)
				for (int waited = 0; waited < 1500 && got < 0; waited += 10)
				{
					got = recv(s, answer, sizeof(answer), 0);
					if (got < 0)
						usleep(10000);
				}
			close(s);
			if (got < 12 || answer[0] != 0x52 || answer[1] != 0x42)
			{
				logLine("radio: name server %s did not answer", server);
				continue;
			}
			// past the question, then each answer: a name, its kind, and (for an address) four numbers
			size_t pos = 12;
			const size_t end = (size_t)got;
			const auto skipName = [&]() {
				while (pos < end)
				{
					const unsigned char length = answer[pos];
					if (length >= 0xC0) { pos += 2; return; }
					pos++;
					if (length == 0) return;
					pos += length;
				}
			};
			skipName();
			pos += 4;
			const int answers = (answer[6] << 8) | answer[7];
			for (int i = 0; i < answers && pos + 10 <= end; i++)
			{
				skipName();
				if (pos + 10 > end)
					break;
				const int kind = (answer[pos] << 8) | answer[pos + 1], length = (answer[pos + 8] << 8) | answer[pos + 9];
				pos += 10;
				if (kind == 1 && length == 4 && pos + 4 <= end)
				{
					char text[20];
					snprintf(text, sizeof(text), "%u.%u.%u.%u", answer[pos], answer[pos + 1], answer[pos + 2], answer[pos + 3]);
					found.push_back(text);
				}
				pos += (size_t)length;
			}
			logLine("radio: name server %s says %s is at %s%s", server, host.c_str(), found.empty() ? "nowhere" : found[0].c_str(), found.size() > 1 ? " (and others)" : "");
			if (!found.empty())
				break;
		}
		return found;
	}

	bool tryWay(const std::string& scheme, const std::string& host, const std::string& address, const std::string& path, std::string& body, std::string& why)
	{
		const std::string pin = address.empty() ? std::string() : host + (scheme == "https" ? ":443:" : ":80:") + address;
		logLine("radio: trying %s://%s%s", scheme.c_str(), host.c_str(), address.empty() ? "" : (" at " + address).c_str());
		std::string error;
		if (timedGet(scheme + "://" + host + path, body, error, 20, 6u * 1024 * 1024, pin) && !body.empty() && (body[0] == '[' || body[0] == '{'))
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_good = scheme + "|" + host + "|" + address;
			return true;
		}
		why = error.empty() ? "not a list" : error;
		logLine("radio: no (%s; %d bytes, starting \"%.60s\")", why.c_str(), (int)body.size(), body.c_str());
		return false;
	}

	bool ask(const std::string& path, std::string& body, std::string& error)
	{
		std::string good, why;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			good = g_good;
		}
		if (!good.empty())
		{
			// the way that worked last time
			const size_t a = good.find('|'), b = good.find('|', a + 1);
			if (tryWay(good.substr(0, a), good.substr(a + 1, b - a - 1), good.substr(b + 1), path, body, why))
				return true;
		}
		// 1. where the directory was when this app was built: no lookup, so the quickest when it is still there
		std::vector<std::string> tried;
		tried.push_back(kLastKnownAddress);
		if (tryWay("http", kHosts[0], kLastKnownAddress, path, body, why))
			return true;
		// 2. the directory's servers, found through a public name server, and asked plainly first
		//    (some consoles' secure connections do not get on with some servers)
		for (int i = 0; i < kHostCount; i++)
			for (const std::string& address : lookUp(kHosts[i]))
			{
				if (std::find(tried.begin(), tried.end(), address) != tried.end() || tried.size() >= 4)
					continue;
				tried.push_back(address);
				if (tryWay("http", kHosts[i], address, path, body, why) || tryWay("https", kHosts[i], address, path, body, why))
					return true;
			}
		// 2. by name, through the console's own name service
		bool nameService = false;
		if (tryWay("http", kHosts[0], "", path, body, why) || tryWay("https", kHosts[0], "", path, body, why))
			return true;
		if (why.find("resolve") != std::string::npos || why.find("ime") != std::string::npos)
			nameService = true;
		if (tryWay("https", kHosts[0], kLastKnownAddress, path, body, why))
			return true;
		error = nameService
			? "The radio directory could not be reached from this console. The log (iptv-log.txt) says what was tried."
			: "The radio directory could not be reached. Check the console's internet connection. (The log, iptv-log.txt, says what was tried.)";
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
	return stations("/json/stations/topclick/100?hidebroken=true", out, error);
}

bool radio::byCountry(const std::string& code, std::vector<Channel>& out, std::string& error)
{
	return stations("/json/stations/bycountrycodeexact/" + encode(code) + "?order=clickcount&reverse=true&hidebroken=true&limit=150", out, error);
}

bool radio::byTag(const std::string& tag, std::vector<Channel>& out, std::string& error)
{
	return stations("/json/stations/bytagexact/" + encode(tag) + "?order=clickcount&reverse=true&hidebroken=true&limit=120", out, error);
}

bool radio::search(const std::string& words, std::vector<Channel>& out, std::string& error)
{
	return stations("/json/stations/byname/" + encode(words) + "?order=clickcount&reverse=true&hidebroken=true&limit=120", out, error);
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
		std::string good;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			good = g_good;
		}
		if (good.empty())
			return;
		const size_t a = good.find('|'), b = good.find('|', a + 1);
		const std::string scheme = good.substr(0, a), host = good.substr(a + 1, b - a - 1), address = good.substr(b + 1);
		std::string body, error;
		httpGetOwn(scheme + "://" + host + "/json/url/" + encode(stationId), body, error, kAgent, 10, 65536,
			address.empty() ? std::string() : host + (scheme == "https" ? ":443:" : ":80:") + address);
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
