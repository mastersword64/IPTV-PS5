#include "sources.h"
#include "json.h"
#include "log.h"
#include "net.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <initializer_list>
#include <mutex>
#include <unordered_map>

static void writeFile(const std::string& path, const std::string& text)
{
	FILE* f = fopen(path.c_str(), "wb");
	if (!f)
		return;
	fwrite(text.data(), 1, text.size(), f);
	fclose(f);
}

static bool fileExists(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	fclose(f);
	return true;
}

const char* const kStalkerUserAgent = "Mozilla/5.0 (QtEmbedded; U; Linux; C) AppleWebKit/533.3 (KHTML, like Gecko) MAG250";

std::string sourceId(const Source& source)
{
	if (!source.id.empty())
		return source.id;
	unsigned long long hash = 1469598103934665603ULL;
	const std::string key = source.type + "\n" + source.a + "\n" + source.b + "\n" + source.c;
	for (const char c : key)
	{
		hash ^= (unsigned char)c;
		hash *= 1099511628211ULL;
	}
	char text[20];
	snprintf(text, sizeof(text), "%016llx", hash);
	return text;
}

const char* sourceKind(const Source& source)
{
	return source.type == "xtream" ? "Xtream Code" : source.type == "stalker" ? "Stalker portal" : source.type == "url" ? "M3U URL" : "M3U file";
}

std::string sourceDetail(const Source& source)
{
	if (source.type == "xtream" && !source.mac.empty())
		return source.a + "   (" + (source.b.empty() ? source.mac : source.b + ", MAC " + source.mac) + ")";
	if (source.type == "xtream" || source.type == "stalker")
		return source.a + "   (" + source.b + ")";
	return source.a;
}

static std::string baseName(const std::string& path)
{
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

static bool endsWith(const std::string& text, const char* suffix)
{
	const std::string s = suffix;
	if (text.size() < s.size())
		return false;
	for (size_t i = 0; i < s.size(); i++)
	{
		char c = text[text.size() - s.size() + i];
		if (c >= 'A' && c <= 'Z')
			c = (char)(c + 32);
		if (c != s[i])
			return false;
	}
	return true;
}

static void addIfNew(std::vector<Source>& list, const Source& source)
{
	const std::string id = sourceId(source);
	for (const Source& existing : list)
		if (sourceId(existing) == id)
			return;
	list.push_back(source);
}

std::vector<Source> loadSources(const std::string& dataDir)
{
	std::vector<Source> list;
	std::string text;
	if (readTextFile(dataDir + "/playlists.txt", text))
	{
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
			for (;;)
			{
				const size_t tab = line.find('\t', at);
				parts.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
				if (tab == std::string::npos)
					break;
				at = tab + 1;
			}
			if (parts.size() < 3 || parts[0].empty())
				continue;
			parts.resize(8);
			Source source{ parts[0], parts[1], parts[2], parts[3], parts[4], parts[5], parts[6], parts[7] };
			source.id = sourceId(source);
			if (source.type == "file" && !fileExists(source.a))
				continue; // the file was removed from the console
			addIfNew(list, source);
		}
	}

	// Files uploaded by FTP, and the playlist the app was built with.
	const std::string folder = dataDir + "/playlists";
	if (DIR* dir = opendir(folder.c_str()))
	{
		std::vector<std::string> names;
		while (const dirent* entry = readdir(dir))
		{
			const std::string name = entry->d_name;
			if (endsWith(name, ".m3u") || endsWith(name, ".m3u8"))
				names.push_back(name);
		}
		closedir(dir);
		for (const std::string& name : names)
			addIfNew(list, Source{ "file", name, folder + "/" + name, "", "" });
	}
	const std::string roots[2] = { dataDir, "/app0/assets" };
	for (const std::string& root : roots)
	{
		if (fileExists(root + "/playlist.m3u"))
			addIfNew(list, Source{ "file", "playlist.m3u", root + "/playlist.m3u", "", "" });
		std::string url;
		if (readTextFile(root + "/playlist-url.txt", url) && !trim(url).empty())
			addIfNew(list, Source{ "url", baseName(trim(url)), trim(url), "", "" });
	}
	return list;
}

void saveSources(const std::string& dataDir, const std::vector<Source>& sources)
{
	std::string text;
	for (const Source& s : sources)
		text += s.type + "\t" + s.name + "\t" + s.a + "\t" + s.b + "\t" + s.c + "\t" + sourceId(s) + "\t" + s.ua + "\t" + s.mac + "\n";
	writeFile(dataDir + "/playlists.txt", text);
}

// ---- Xtream Codes ------------------------------------------------------------

static std::string urlEncode(const std::string& text)
{
	std::string out;
	for (const char ch : text)
	{
		const unsigned char c = (unsigned char)ch;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
			out += ch;
		else
		{
			char hex[4];
			snprintf(hex, sizeof(hex), "%%%02X", c);
			out += hex;
		}
	}
	return out;
}

static std::string serverBase(const std::string& typed)
{
	std::string base = trim(typed);
	if (base.find("://") == std::string::npos)
		base = "http://" + base;
	// people often paste the whole address of the portal page: keep only scheme://host:port
	const size_t host = base.find("://") + 3;
	const size_t slash = base.find('/', host);
	if (slash != std::string::npos)
		base = base.substr(0, slash);
	return base;
}

static std::string attributeSafe(std::string text)
{
	for (char& c : text)
		if (c == '"' || c == '\n' || c == '\r')
			c = '\'';
	return text;
}

std::string channelsToM3U(const std::vector<Channel>& channels)
{
	std::string out = "#EXTM3U\n";
	for (const Channel& c : channels)
	{
		out += "#EXTINF:-1 tvg-id=\"" + attributeSafe(c.tvgId) + "\" tvg-logo=\"" + attributeSafe(c.logo) + "\" group-title=\"" + attributeSafe(c.group) + "\"," + attributeSafe(c.name) + "\n";
		out += c.url + "\n";
	}
	return out;
}

static bool fetchXtream(const Source& source, Playlist& out, std::string& error)
{
	const std::string base = serverBase(source.a);
	const std::string user = trim(source.b), pass = trim(source.c);
	const std::string api = base + "/player_api.php?username=" + urlEncode(user) + "&password=" + urlEncode(pass);
	const auto nothing = [](const JsonFields&) {};
	// With a MAC address set, the server is spoken to the way a MAG set-top box would.
	std::vector<std::string> headers;
	std::string agent = source.ua;
	if (!source.mac.empty())
	{
		headers = { "Cookie: mac=" + source.mac + "; stb_lang=en; timezone=Europe/Berlin", std::string("X-User-Agent: ") + kStalkerUserAgent };
		if (agent.empty())
			agent = kStalkerUserAgent;
	}

	std::string body;
	if (!httpGet(api, body, error, headers, agent, source.epoch))
		return false;
	JsonFields account;
	if (!jsonRead(body, "-", account, nothing) || !account.has("user_info.auth"))
	{
		error = "That address did not answer like an Xtream Codes server";
		return false;
	}
	if (account.get("user_info.auth") == "0")
	{
		error = "The server did not accept that username and password";
		return false;
	}
	// Plain transport streams unless the account only allows the other kind.
	const std::string& formats = account.get("user_info.allowed_output_formats");
	const char* format = formats.find("ts") == std::string::npos && formats.find("m3u8") != std::string::npos ? "m3u8" : "ts";
	logLine("xtream: signed in (status %s); stream format %s", account.get("user_info.status").c_str(), format);

	std::unordered_map<std::string, std::string> categoryNames;
	std::vector<std::string> categoryOrder;
	if (!httpGet(api + "&action=get_live_categories", body, error, headers, agent, source.epoch))
		return false;
	JsonFields ignored;
	jsonRead(body, "", ignored, [&](const JsonFields& item) {
		const std::string& id = item.get("category_id");
		if (categoryNames.emplace(id, item.get("category_name")).second)
			categoryOrder.push_back(id);
	});

	if (!httpGet(api + "&action=get_live_streams", body, error, headers, agent, source.epoch))
		return false;
	out = Playlist();
	std::unordered_map<std::string, std::vector<Channel>> byCategory;
	std::vector<std::string> extraOrder;
	const bool ok = jsonRead(body, "", ignored, [&](const JsonFields& item) {
		const std::string& id = item.get("stream_id");
		if (id.empty())
			return;
		Channel channel;
		channel.name = item.get("name").empty() ? "Channel " + id : item.get("name");
		channel.logo = item.get("stream_icon");
		channel.tvgId = item.get("epg_channel_id");
		channel.url = base + "/live/" + user + "/" + pass + "/" + id + "." + format;
		channel.archive = item.get("tv_archive") == "1";
		const std::string& category = item.get("category_id");
		const auto named = categoryNames.find(category);
		channel.group = named != categoryNames.end() && !named->second.empty() ? named->second : "Uncategorized";
		if (named == categoryNames.end() && byCategory.find(category) == byCategory.end())
			extraOrder.push_back(category);
		byCategory[category].push_back(std::move(channel));
	});
	if (!ok && byCategory.empty())
	{
		error = "The server's channel list could not be read";
		return false;
	}
	// Channels are listed category by category, in the server's own category order.
	categoryOrder.insert(categoryOrder.end(), extraOrder.begin(), extraOrder.end());
	for (const std::string& id : categoryOrder)
	{
		const auto found = byCategory.find(id);
		if (found == byCategory.end() || found->second.empty())
			continue;
		bool listed = false;
		for (const std::string& group : out.groups)
			if (group == found->second[0].group)
				listed = true;
		if (!listed)
			out.groups.push_back(found->second[0].group);
		for (Channel& channel : found->second)
			out.channels.push_back(std::move(channel));
	}
	if (out.channels.empty())
	{
		error = "This account has no live channels";
		return false;
	}
	return true;
}

// ---- Stalker / Ministra portals ----------------------------------------------

std::string normalizeMac(const std::string& typed)
{
	std::string digits;
	for (const char ch : typed)
	{
		char c = ch;
		if (c >= 'a' && c <= 'f')
			c = (char)(c - 32);
		if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))
			digits += c;
		else if (c != ':' && c != '-' && c != ' ')
			return "";
	}
	if (digits.size() != 12)
		return "";
	std::string mac;
	for (size_t i = 0; i < 12; i += 2)
	{
		if (i)
			mac += ':';
		mac += digits.substr(i, 2);
	}
	return mac;
}

namespace
{
	struct StalkerSession { std::string endpoint, token; };
	std::mutex g_stalkerMutex;
	std::unordered_map<std::string, StalkerSession> g_stalkerSessions;   // by portal + MAC

	// An Xtream account that has a MAC address can also be reached as a portal on the same server.
	Source asPortal(const Source& source)
	{
		if (source.type != "xtream")
			return source;
		Source portal = source;
		portal.type = "stalker";
		portal.b = source.mac;
		portal.c.clear();
		return portal;
	}

	std::string stalkerKey(const Source& source)
	{
		return source.a + "\n" + source.b;
	}

	// The page a set-top box would be showing: the address as typed, ending in a slash.
	std::string portalPage(const std::string& typed)
	{
		std::string page = trim(typed);
		if (page.find("://") == std::string::npos)
			page = "http://" + page;
		if (page.size() > 4 && page.compare(page.size() - 4, 4, ".php") == 0)
			return page;
		if (page.empty() || page.back() != '/')
			page += '/';
		return page;
	}

	// The start of an answer, on one line, for the log.
	std::string excerpt(const std::string& body)
	{
		std::string text = body.substr(0, 160);
		for (char& ch : text)
			if (ch == '\n' || ch == '\r' || ch == '\t')
				ch = ' ';
		return text;
	}

	std::vector<std::string> stalkerHeaders(const Source& source, const std::string& token)
	{
		std::vector<std::string> headers = {
			"Cookie: mac=" + source.b + "; stb_lang=en; timezone=Europe/Berlin",
			std::string("X-User-Agent: ") + kStalkerUserAgent,
			"Referer: " + portalPage(source.a),
			"Accept: */*" };
		if (!token.empty())
			headers.push_back("Authorization: Bearer " + token);
		return headers;
	}

	bool stalkerCall(const Source& source, const StalkerSession& session, const std::string& query, std::string& body, std::string& error)
	{
		return httpGet(session.endpoint + "?" + query + "&JsHttpRequest=1-xml", body, error, stalkerHeaders(source, session.token),
		               source.ua.empty() ? kStalkerUserAgent : source.ua, source.epoch);
	}

	// The places a portal's API can live, most likely first, worked out from the address typed.
	std::vector<std::string> stalkerEndpoints(const std::string& typed)
	{
		std::string address = trim(typed);
		if (address.find("://") == std::string::npos)
			address = "http://" + address;
		while (!address.empty() && address.back() == '/')
			address.pop_back();
		if (address.size() > 4 && address.compare(address.size() - 4, 4, ".php") == 0)
			return { address };
		const size_t hostStart = address.find("://") + 3;
		const size_t slash = address.find('/', hostStart);
		const std::string root = slash == std::string::npos ? address : address.substr(0, slash);
		std::string prefix = address;                       // the address without a trailing "/c"
		if (prefix.size() >= 2 && prefix.compare(prefix.size() - 2, 2, "/c") == 0)
			prefix.resize(prefix.size() - 2);

		std::vector<std::string> list;
		const auto add = [&list](const std::string& endpoint) {
			for (const std::string& existing : list)
				if (existing == endpoint)
					return;
			list.push_back(endpoint);
		};
		if (address.find("stalker_portal") != std::string::npos)
			add(root + "/stalker_portal/server/load.php");
		add(prefix + "/portal.php");
		add(prefix + "/server/load.php");
		add(root + "/portal.php");
		add(root + "/server/load.php");
		add(root + "/stalker_portal/server/load.php");
		return list;
	}

	// Signs in: finds the API, gets a token, and announces the box, as a MAG set-top box does.
	bool stalkerConnect(const Source& source, StalkerSession& session, std::string& error)
	{
		const auto nothing = [](const JsonFields&) {};
		std::string lastError = "The portal did not answer";
		for (const std::string& endpoint : stalkerEndpoints(source.a))
		{
			StalkerSession attempt{ endpoint, "" };
			std::string body, why;
			if (source.epoch >= 0 && source.epoch != httpEpoch())
			{
				lastError = "cancelled";
				break;
			}
			logLine("stalker: trying %s", endpoint.c_str());
			if (!stalkerCall(source, attempt, "type=stb&action=handshake&token=", body, why))
			{
				logLine("stalker:   no answer: %s", why.c_str());
				lastError = why;
				continue;
			}
			JsonFields fields;
			if (!jsonRead(body, "-", fields, nothing))
			{
				logLine("stalker:   not a portal answer (%d bytes): %s", (int)body.size(), excerpt(body).c_str());
				lastError = "That address did not answer like a Stalker portal";
				continue;
			}
			attempt.token = fields.get("js.token");
			logLine("stalker: connected through %s%s", endpoint.c_str(), attempt.token.empty() ? " (no token needed)" : "");
			if (stalkerCall(source, attempt, "type=stb&action=get_profile&hd=1&num_banks=2&stb_type=MAG250&client_type=STB&image_version=218&auth_second_step=1&hw_version=1.7-BD-00&not_valid_token=0", body, why))
				logLine("stalker:   profile answer (%d bytes): %s", (int)body.size(), excerpt(body).c_str());
			else
				logLine("stalker:   profile request failed: %s", why.c_str());
			session = attempt;
			std::lock_guard<std::mutex> lock(g_stalkerMutex);
			g_stalkerSessions[stalkerKey(source)] = session;
			return true;
		}
		error = lastError;
		return false;
	}

	bool stalkerSession(const Source& source, StalkerSession& session, std::string& error)
	{
		{
			std::lock_guard<std::mutex> lock(g_stalkerMutex);
			const auto found = g_stalkerSessions.find(stalkerKey(source));
			if (found != g_stalkerSessions.end())
			{
				session = found->second;
				return true;
			}
		}
		return stalkerConnect(source, session, error);
	}

	// Some portals hand back a link with the channel number left out ("...&stream=&extension=ts...")
	// and then drop the connection when it is used. The number is the one in the channel's own
	// command ("ffmpeg http://localhost/ch/12345_"), so it is put back in.
	void fillStreamNumber(std::string& url, const std::string& command, const std::string& channelId)
	{
		const size_t key = url.find("stream=");
		if (key == std::string::npos || (key > 0 && url[key - 1] != '?' && url[key - 1] != '&'))
			return;
		const size_t value = key + 7;
		if (value < url.size() && url[value] != '&')
			return;                                   // already has a number
		std::string number;
		const size_t marker = command.find("/ch/");
		if (marker != std::string::npos)
		{
			size_t end = marker + 4;
			while (end < command.size() && command[end] >= '0' && command[end] <= '9')
				end++;
			number = command.substr(marker + 4, end - marker - 4);
		}
		if (number.empty())
			number = channelId;                       // the id the portal lists the channel under
		if (number.empty())
			return;
		url.insert(value, number);
		logLine("stalker: the portal left the channel number out of the link; filled in %s", number.c_str());
	}

	// "ffmpeg http://host/stream" -> "http://host/stream"
	std::string streamFromCommand(const std::string& command)
	{
		const std::string text = trim(command);
		const size_t space = text.find_last_of(' ');
		return space == std::string::npos ? text : text.substr(space + 1);
	}
}

static bool fetchStalker(const Source& source, Playlist& out, std::string& error)
{
	if (normalizeMac(source.b).empty())
	{
		error = "The MAC address should look like 00:1A:79:12:34:56";
		return false;
	}
	StalkerSession session;
	if (!stalkerConnect(source, session, error))           // always a fresh sign-in when loading
		return false;

	// Only the categories are fetched now. Big portals have hundreds of them and tens of
	// thousands of channels, and asking for everything at once can take minutes or never finish.
	std::string body;
	if (!stalkerCall(source, session, "type=itv&action=get_genres", body, error))
		return false;
	out = Playlist();
	out.lazy = true;
	JsonFields ignored;
	jsonRead(body, "js", ignored, [&](const JsonFields& item) {
		const std::string& id = item.get("id");
		if (id.empty() || id == "*")
			return;
		out.groups.push_back(item.get("title").empty() ? "Category " + id : item.get("title"));
		out.groupIds.push_back(id);
	});
	logLine("stalker: %d categories", (int)out.groups.size());
	if (out.groups.empty())
	{
		logLine("stalker: category answer (%d bytes): %s", (int)body.size(), excerpt(body).c_str());
		error = "The portal listed no categories for this MAC address (is it registered with the provider?)";
		return false;
	}
	return true;
}

// Reads a paged portal list. `query` is everything but the page number; `convert` turns one
// entry into zero or more list items; `page` is given each batch (false from it stops early).
static bool portalPages(const Source& source, const std::string& query, const char* what,
                        const std::function<void(const JsonFields&, std::vector<Channel>&)>& convert,
                        const std::function<bool(std::vector<Channel>&)>& page, std::string& error)
{
	StalkerSession session;
	if (!stalkerSession(source, session, error))
		return false;

	int total = -1, entries = 0;
	bool reconnected = false;
	for (int number = 1; number <= 600; number++)
	{
		std::string body;
		if (!stalkerCall(source, session, query + "&p=" + std::to_string(number), body, error))
			return false;

		std::vector<Channel> batch;
		int seen = 0;
		JsonFields fields;
		const bool readable = jsonRead(body, "js.data", fields, [&](const JsonFields& item) {
			if (number == 1 && seen == 0)
			{
				// what the portal says about the first entry, for working out its format
				std::string all;
				for (const auto& field : item.items)
					if (all.size() < 700 && !field.second.empty())
						all += field.first + "=" + field.second.substr(0, 90) + "  ";
				logLine("stalker: first %s as listed: %s", what, all.c_str());
			}
			seen++;
			convert(item, batch);
		});
		if ((!readable || !fields.has("js.total_items")) && seen == 0)
		{
			// not a list page: usually the sign-in has expired, so sign in again once
			if (!reconnected && number == 1)
			{
				reconnected = true;
				if (!stalkerConnect(source, session, error))
					return false;
				number = 0;
				continue;
			}
			logLine("stalker: unexpected answer for %s page %d (%d bytes): %s", what, number, (int)body.size(), excerpt(body).c_str());
			if (entries > 0)
				return true;
			error = "The portal did not answer with a list";
			return false;
		}
		if (total < 0)
		{
			total = atoi(fields.get("js.total_items").c_str());
			logLine("stalker: %d %s entries (%s per page)", total, what, fields.get("js.max_page_items").c_str());
		}
		if (seen == 0)
			break;
		entries += seen;
		if (!batch.empty() && !page(batch))
			return true;                              // the viewer moved on
		if (total >= 0 && entries >= total)
			break;
	}
	return true;
}

// ---- Xtream Codes: shared request details ----------------------------------------

namespace
{
	struct Xtream
	{
		std::string base, user, pass, api, agent;
		std::vector<std::string> headers;
	};

	Xtream xtreamFor(const Source& source)
	{
		Xtream x;
		x.base = serverBase(source.a);
		x.user = trim(source.b);
		x.pass = trim(source.c);
		x.api = x.base + "/player_api.php?username=" + urlEncode(x.user) + "&password=" + urlEncode(x.pass);
		x.agent = source.ua;
		if (!source.mac.empty())
		{
			x.headers = { "Cookie: mac=" + source.mac + "; stb_lang=en; timezone=Europe/Berlin", std::string("X-User-Agent: ") + kStalkerUserAgent };
			if (x.agent.empty())
				x.agent = kStalkerUserAgent;
		}
		return x;
	}

	// An Xtream entry with only a MAC address is reached through the server's portal instead.
	bool usesPortal(const Source& source)
	{
		return source.type == "stalker" || (source.type == "xtream" && trim(source.b).empty() && trim(source.c).empty() && !source.mac.empty());
	}

	// "1735689600" -> "2025-01-01"; anything else is returned as it is.
	std::string readableDate(const std::string& value)
	{
		if (value.size() < 9 || value.size() > 10)
			return value;
		for (const char c : value)
			if (c < '0' || c > '9')
				return value;
		const time_t when = (time_t)atoll(value.c_str());
		struct tm parts;
		if (!gmtime_r(&when, &parts))
			return value;
		char text[32];
		snprintf(text, sizeof(text), "%04d-%02d-%02d", parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday);
		return text;
	}
}

// What a portal says about a film or series, in whichever of its usual field names it used.
static void describe(const JsonFields& item, Channel& channel)
{
	const auto first = [&item](std::initializer_list<const char*> names) {
		for (const char* name : names)
		{
			const std::string& value = item.get(name);
			if (!value.empty() && value != "0" && value != "N/A" && value != "null")
				return value;
		}
		return std::string();
	};
	channel.plot = first({ "description", "descr", "plot" });
	channel.year = first({ "year", "releasedate", "release_date" }).substr(0, 4);
	channel.rating = first({ "rating_imdb", "rating", "rating_kinopoisk" });
	channel.genres = first({ "genres_str", "genre" });
	channel.cast = first({ "actors", "cast" });
	// a trailer the player can open itself (addresses on video-sharing sites cannot be played here)
	const std::string trailer = first({ "trailer_url", "trailer" });
	if (trailer.compare(0, 4, "http") == 0 && trailer.find("youtu") == std::string::npos)
		channel.trailer = trailer;
	const std::string minutes = first({ "time", "duration" });
	if (!minutes.empty() && minutes.find(':') == std::string::npos && atoi(minutes.c_str()) > 0)
		channel.length = std::to_string(atoi(minutes.c_str())) + " min";
	else
		channel.length = minutes;
	if (channel.rating.size() > 4)
		channel.rating.resize(4);
}

// ---- movies, series and account details ------------------------------------------

bool sourceHasLibrary(const Source& source, int media)
{
	return media == MEDIA_LIVE || source.type == "xtream" || source.type == "stalker";
}

bool isSeriesItem(const std::string& url)
{
	return url.compare(0, 10, "xseries://") == 0 || url.compare(0, 10, "sseries://") == 0;
}

bool fetchLibrary(const Source& original, int media, const std::string& dataDir, Playlist& out, std::string& error)
{
	if (media == MEDIA_LIVE)
		return fetchSource(original, dataDir, out, error);
	Source source = original;
	source.epoch = -1;
	const char* const what = media == MEDIA_MOVIES ? "movie" : "series";
	out = Playlist();
	out.lazy = true;
	std::string body;
	JsonFields ignored;

	if (usesPortal(source))
	{
		const Source portal = asPortal(source);
		StalkerSession session;
		if (!stalkerSession(portal, session, error))
			return false;
		if (!stalkerCall(portal, session, std::string("type=") + (media == MEDIA_MOVIES ? "vod" : "series") + "&action=get_categories", body, error))
			return false;
		jsonRead(body, "js", ignored, [&](const JsonFields& item) {
			const std::string& id = item.get("id");
			if (id.empty() || id == "*")
				return;
			out.groups.push_back(item.get("title").empty() ? "Category " + id : item.get("title"));
			out.groupIds.push_back(id);
		});
		if (out.groups.empty())
			logLine("stalker: %s categories answer (%d bytes): %s", what, (int)body.size(), excerpt(body).c_str());
	}
	else if (source.type == "xtream")
	{
		const Xtream x = xtreamFor(source);
		if (!httpGet(x.api + (media == MEDIA_MOVIES ? "&action=get_vod_categories" : "&action=get_series_categories"), body, error, x.headers, x.agent))
			return false;
		jsonRead(body, "", ignored, [&](const JsonFields& item) {
			const std::string& id = item.get("category_id");
			if (id.empty())
				return;
			out.groups.push_back(item.get("category_name").empty() ? "Category " + id : item.get("category_name"));
			out.groupIds.push_back(id);
		});
	}
	else
	{
		error = "M3U playlists carry live channels only";
		return false;
	}
	logLine("library: %d %s categories", (int)out.groups.size(), what);
	if (out.groups.empty())
	{
		error = std::string("This account has no ") + (media == MEDIA_MOVIES ? "movies" : "series");
		return false;
	}
	return true;
}

bool fetchCategory(const Source& original, int media, const std::string& categoryId,
                   const std::function<bool(std::vector<Channel>&)>& page, std::string& error, const std::string& search)
{
	if (usesPortal(original))
	{
		const Source source = asPortal(original);
		// a search asks the same lists for titles containing some words, across every category
		const std::string wanted = search.empty() ? std::string() : "&search=" + urlEncode(search);
		if (media == MEDIA_LIVE)
			return portalPages(source, "type=itv&action=get_ordered_list&genre=" + urlEncode(categoryId) + "&force_ch_link_check=&fav=0&sortby=number&hd=0" + wanted, "channel",
				[](const JsonFields& item, std::vector<Channel>& batch) {
					if (item.get("cmd").empty())
						return;
					Channel channel;
					channel.name = item.get("name").empty() ? "Channel " + item.get("number") : item.get("name");
					channel.tvgId = item.get("xmltv_id");
					channel.logo = item.get("logo");
					channel.url = "stalker://id=" + item.get("id") + ";" + item.get("cmd");
					channel.archive = item.get("enable_tv_archive") == "1" || item.get("tv_archive") == "1";
					batch.push_back(std::move(channel));
				}, page, error);
		if (media == MEDIA_MOVIES)
			return portalPages(source, "type=vod&action=get_ordered_list&category=" + urlEncode(categoryId) + "&genre=0&sortby=added&fav=0" + wanted, "movie",
				[](const JsonFields& item, std::vector<Channel>& batch) {
					if (item.get("cmd").empty() || item.get("name").empty())
						return;
					Channel channel;
					channel.name = item.get("name");
					if (!item.get("year").empty() && item.get("year") != "0" && item.get("year").size() <= 10)
						channel.name += "  (" + item.get("year").substr(0, 4) + ")";
					channel.logo = item.get("screenshot_uri");
					channel.url = "stalker://k=vod;id=" + item.get("id") + ";" + item.get("cmd");
					describe(item, channel);
					batch.push_back(std::move(channel));
				}, page, error);
		return portalPages(source, "type=series&action=get_ordered_list&category=" + urlEncode(categoryId) + "&genre=0&sortby=added&fav=0" + wanted, "series",
			[](const JsonFields& item, std::vector<Channel>& batch) {
				if (item.get("id").empty() || item.get("name").empty())
					return;
				Channel channel;
				channel.name = item.get("name");
				channel.logo = item.get("screenshot_uri");
				channel.url = "sseries://id=" + item.get("id");
				describe(item, channel);
				batch.push_back(std::move(channel));
			}, page, error);
	}

	if (original.type != "xtream" || media == MEDIA_LIVE)
	{
		error = "Nothing to fetch for this kind of playlist";
		return false;
	}
	const Xtream x = xtreamFor(original);
	std::string body;
	if (!httpGet(x.api + (media == MEDIA_MOVIES ? "&action=get_vod_streams" : "&action=get_series") + "&category_id=" + urlEncode(categoryId), body, error, x.headers, x.agent))
		return false;
	std::vector<Channel> batch;
	JsonFields ignored;
	jsonRead(body, "", ignored, [&](const JsonFields& item) {
		Channel channel;
		channel.name = item.get("name");
		if (media == MEDIA_MOVIES)
		{
			const std::string& id = item.get("stream_id");
			if (id.empty())
				return;
			const std::string& extension = item.get("container_extension");
			channel.logo = item.get("stream_icon");
			channel.url = x.base + "/movie/" + x.user + "/" + x.pass + "/" + id + "." + (extension.empty() ? "mp4" : extension);
			describe(item, channel);
		}
		else
		{
			const std::string& id = item.get("series_id");
			if (id.empty())
				return;
			channel.logo = item.get("cover");
			channel.url = "xseries://" + id;
			describe(item, channel);
		}
		if (channel.name.empty())
			channel.name = "Untitled";
		batch.push_back(std::move(channel));
	});
	logLine("xtream: category %s has %d entries", categoryId.c_str(), (int)batch.size());
	if (!batch.empty())
		page(batch);
	return true;
}

bool fetchEpisodes(const Source& original, const std::string& seriesUrl, std::vector<Channel>& out, std::string& error)
{
	out.clear();
	if (seriesUrl.compare(0, 10, "xseries://") == 0)
	{
		const Xtream x = xtreamFor(original);
		std::string body;
		if (!httpGet(x.api + "&action=get_series_info&series_id=" + urlEncode(seriesUrl.substr(10)), body, error, x.headers, x.agent))
			return false;
		JsonFields ignored;
		jsonRead(body, "episodes.*", ignored, [&](const JsonFields& item) {
			const std::string& id = item.get("id");
			if (id.empty())
				return;
			const std::string& extension = item.get("container_extension");
			const int season = atoi((item.get("season").empty() ? item.get("@key") : item.get("season")).c_str());
			char label[48];
			snprintf(label, sizeof(label), "S%02d E%02d", season, atoi(item.get("episode_num").c_str()));
			Channel episode;
			episode.name = item.get("title").empty() ? label : std::string(label) + "   " + item.get("title");
			episode.url = x.base + "/series/" + x.user + "/" + x.pass + "/" + id + "." + (extension.empty() ? "mp4" : extension);
			out.push_back(std::move(episode));
		});
	}
	else if (seriesUrl.compare(0, 13, "sseries://id=") == 0)
	{
		// A portal lists a series as seasons, each with its episode numbers and one play command.
		const Source source = asPortal(original);
		const bool ok = portalPages(source, "type=series&action=get_ordered_list&movie_id=" + urlEncode(seriesUrl.substr(13)) + "&season_id=0&episode_id=0", "season",
			[](const JsonFields& item, std::vector<Channel>& batch) {
				const std::string& command = item.get("cmd");
				if (command.empty())
					return;
				const std::string season = item.get("name").empty() ? "Season" : item.get("name");
				const std::string& numbers = item.get("series");
				if (numbers.empty())
				{
					Channel whole;
					whole.name = season;
					whole.url = "stalker://k=vod;id=" + item.get("id") + ";" + command;
					batch.push_back(std::move(whole));
					return;
				}
				size_t at = 0;
				while (at < numbers.size())
				{
					size_t comma = numbers.find(',', at);
					if (comma == std::string::npos)
						comma = numbers.size();
					const std::string number = numbers.substr(at, comma - at);
					at = comma + 1;
					if (number.empty())
						continue;
					Channel episode;
					episode.name = season + "   Episode " + number;
					episode.url = "stalker://k=vod;ep=" + number + ";id=" + item.get("id") + ";" + command;
					batch.push_back(std::move(episode));
				}
			},
			[&](std::vector<Channel>& batch) {
				for (Channel& episode : batch)
					out.push_back(std::move(episode));
				return true;
			}, error);
		if (!ok)
			return false;
	}
	logLine("library: %d episodes", (int)out.size());
	if (out.empty())
	{
		if (error.empty())
			error = "No episodes were listed for this series";
		return false;
	}
	return true;
}

bool fetchAccount(const Source& original, std::vector<std::pair<std::string, std::string>>& rows, std::string& error)
{
	rows.clear();
	rows.emplace_back("Playlist", original.name);
	rows.emplace_back("Type", sourceKind(original));
	const auto nothing = [](const JsonFields&) {};
	const auto add = [&rows](const char* label, const std::string& value) {
		if (!value.empty() && value != "null")
			rows.emplace_back(label, value);
	};

	if (usesPortal(original))
	{
		const Source source = asPortal(original);
		rows.emplace_back("Portal", source.a);
		rows.emplace_back("MAC address", source.b);
		StalkerSession session;
		if (!stalkerSession(source, session, error))
			return false;
		std::string body, why;
		JsonFields info;
		if (stalkerCall(source, session, "type=account_info&action=get_main_info", body, why) && jsonRead(body, "-", info, nothing))
		{
			logLine("stalker: account answer (%d bytes): %s", (int)body.size(), excerpt(body).c_str());
			add("Expires", readableDate(info.get("js.end_date").empty() ? info.get("js.phone") : info.get("js.end_date")));
			add("Plan", info.get("js.tariff_plan"));
			add("Balance", info.get("js.account_balance"));
			add("Account", info.get("js.fname").empty() ? info.get("js.login") : info.get("js.fname"));
		}
		JsonFields profile;
		if (stalkerCall(source, session, "type=stb&action=get_profile&hd=1&num_banks=2&stb_type=MAG250&client_type=STB&image_version=218&auth_second_step=1&hw_version=1.7-BD-00&not_valid_token=0", body, why)
			&& jsonRead(body, "-", profile, nothing))
		{
			add("Account id", profile.get("js.id"));
			add("Name", profile.get("js.name"));
			add("Status", profile.get("js.status") == "0" ? "Active" : profile.get("js.status") == "1" ? "Not active" : profile.get("js.status"));
			add("Expires (billing)", readableDate(profile.get("js.expire_billing_date")));
			add("Tariff ends", readableDate(profile.get("js.tariff_expired_date")));
			add("Created", readableDate(profile.get("js.created")));
			add("Last seen", readableDate(profile.get("js.last_active")));
			add("Server time zone", profile.get("js.default_timezone"));
		}
		return true;
	}
	if (original.type == "xtream")
	{
		const Xtream x = xtreamFor(original);
		rows.emplace_back("Server", x.base);
		std::string body;
		if (!httpGet(x.api, body, error, x.headers, x.agent))
			return false;
		JsonFields info;
		if (!jsonRead(body, "-", info, nothing))
		{
			error = "The server's answer could not be read";
			return false;
		}
		add("Username", info.get("user_info.username"));
		add("Status", info.get("user_info.status"));
		add("Expires", readableDate(info.get("user_info.exp_date")));
		add("Trial", info.get("user_info.is_trial") == "1" ? "Yes" : info.get("user_info.is_trial") == "0" ? "No" : "");
		add("Connections in use", info.get("user_info.active_cons"));
		add("Connections allowed", info.get("user_info.max_connections"));
		add("Created", readableDate(info.get("user_info.created_at")));
		add("Formats", info.get("user_info.allowed_output_formats"));
		add("Server time zone", info.get("server_info.timezone"));
		if (!original.mac.empty())
			rows.emplace_back("MAC address", original.mac);
		return true;
	}
	rows.emplace_back(original.type == "url" ? "Address" : "File", original.a);
	return true;
}

bool stalkerResolve(const Source& original, const std::string& listed, std::string& url, std::string& error)
{
	const Source source = asPortal(original);
	// "k=vod;ep=<episode>;id=<id>;<command>": each part before the command is optional
	// (a bare command is a live channel saved by an earlier version)
	std::string command = listed, channelId, kind = "itv", episode;
	for (;;)
	{
		const size_t end = command.find(';');
		if (end == std::string::npos)
			break;
		if (command.compare(0, 3, "id=") == 0)
			channelId = command.substr(3, end - 3);
		else if (command.compare(0, 2, "k=") == 0)
			kind = command.substr(2, end - 2);
		else if (command.compare(0, 3, "ep=") == 0)
			episode = command.substr(3, end - 3);
		else
			break;
		command = command.substr(end + 1);
	}
	logLine("stalker: asking for a %s link; id %s, command: %s", kind.c_str(), channelId.c_str(), command.c_str());
	const auto nothing = [](const JsonFields&) {};
	for (int attempt = 0; attempt < 2; attempt++)
	{
		StalkerSession session;
		if (attempt == 0 ? !stalkerSession(source, session, error) : !stalkerConnect(source, session, error))
			break;
		std::string body, why;
		if (stalkerCall(source, session, "type=" + kind + "&action=create_link&cmd=" + urlEncode(command) + "&series=" + episode + "&forced_storage=undefined&disable_ad=0&download=0", body, why))
		{
			JsonFields fields;
			logLine("stalker: link answer: %s", excerpt(body).c_str());
			if (jsonRead(body, "-", fields, nothing) && !fields.get("js.cmd").empty())
			{
				url = streamFromCommand(fields.get("js.cmd"));
				if (url.find("://") != std::string::npos)
				{
					fillStreamNumber(url, command, channelId);
					return true;
				}
			}
		}
		// the token may have expired: sign in again and try once more
	}
	// Some portals list the real address directly.
	const std::string direct = streamFromCommand(command);
	if (direct.find("://") != std::string::npos && direct.find("localhost") == std::string::npos)
	{
		url = direct;
		return true;
	}
	if (error.empty())
		error = "The portal did not give an address for this channel";
	return false;
}

bool fetchSource(const Source& original, const std::string& dataDir, Playlist& out, std::string& error)
{
	Source source = original;
	source.epoch = httpEpoch();   // every request of this load stops if the load is cancelled
	const std::string cache = dataDir + "/cache-" + sourceId(source) + ".m3u";
	std::string text;

	if (source.type == "file")
	{
		if (!readTextFile(source.a, text))
		{
			error = "The file could not be opened: " + source.a;
			return false;
		}
	}
	else if (source.type == "url")
	{
		logLine("playlist: downloading %s", source.a.c_str());
		if (httpGet(source.a, text, error, {}, source.ua, source.epoch))
			writeFile(cache, text);
		else if (readTextFile(cache, text))
			logLine("playlist: download failed (%s); using the copy saved last time", error.c_str());
		else
		{
			error = "The playlist could not be downloaded: " + error;
			return false;
		}
	}
	else if (source.type == "xtream")
	{
		logLine("playlist: asking the Xtream server %s", source.a.c_str());
		const bool macOnly = trim(source.b).empty() && trim(source.c).empty() && !source.mac.empty();
		if (macOnly)
			logLine("playlist: no username or password, so signing in with the MAC address");
		if (macOnly ? fetchStalker(asPortal(source), out, error) : fetchXtream(source, out, error))
		{
			writeFile(cache, channelsToM3U(out.channels));
			return true;
		}
		if (!readTextFile(cache, text))
			return false;
		logLine("playlist: server unavailable (%s); using the copy saved last time", error.c_str());
	}
	else if (source.type == "stalker")
	{
		logLine("playlist: asking the Stalker portal %s", source.a.c_str());
		if (fetchStalker(source, out, error))
		{
			writeFile(cache, channelsToM3U(out.channels));
			return true;
		}
		if (!readTextFile(cache, text))
			return false;
		logLine("playlist: portal unavailable (%s); using the copy saved last time", error.c_str());
	}
	else
	{
		error = "Unknown kind of playlist";
		return false;
	}

	if (!parseM3U(text, out))
	{
		error = "The playlist has no channels in it (is it an M3U file?)";
		return false;
	}
	return true;
}

// ---- programme guide -------------------------------------------------------------

static std::string fromBase64(const std::string& text)
{
	std::string out;
	unsigned buffer = 0;
	int bits = 0;
	for (const char ch : text)
	{
		int value;
		if (ch >= 'A' && ch <= 'Z') value = ch - 'A';
		else if (ch >= 'a' && ch <= 'z') value = ch - 'a' + 26;
		else if (ch >= '0' && ch <= '9') value = ch - '0' + 52;
		else if (ch == '+' || ch == '-') value = 62;
		else if (ch == '/' || ch == '_') value = 63;
		else continue;                                  // padding and line breaks
		buffer = (buffer << 6) | (unsigned)value;
		bits += 6;
		if (bits >= 8)
		{
			bits -= 8;
			out += (char)((buffer >> bits) & 0xff);
		}
	}
	return out;
}

// "2026-10-06 20:30:00" -> "20:30"
static std::string clockOf(const std::string& dateTime)
{
	const size_t space = dateTime.find(' ');
	if (space == std::string::npos || dateTime.size() < space + 6)
		return "";
	return dateTime.substr(space + 1, 5);
}

// "2026-10-06 09:00:00" as the provider's catch-up addresses want it: "2026-10-06:09-00"
static std::string timeshiftStamp(const std::string& start)
{
	if (start.size() < 16)
		return "";
	return start.substr(0, 10) + ":" + start.substr(11, 2) + "-" + start.substr(14, 2);
}

// Today's programmes for a channel, the ones already shown included, saying which the provider
// can still play back. Used by the guide grid. Returns false if the server has no such list.
static bool fetchGuideDay(const Source& original, const Channel& channel, std::vector<Programme>& out, std::string& error)
{
	std::string body;
	JsonFields ignored;
	if (channel.url.compare(0, 10, "stalker://") == 0)
	{
		const size_t at = channel.url.find("id=");
		const size_t end = channel.url.find(';', at == std::string::npos ? 0 : at);
		if (at == std::string::npos || end == std::string::npos)
			return false;
		const Source source = asPortal(original);
		StalkerSession session;
		if (!stalkerSession(source, session, error))
			return false;
		const time_t now = time(nullptr);
		struct tm day;
		gmtime_r(&now, &day);
		char date[16];
		snprintf(date, sizeof(date), "%04d-%02d-%02d", day.tm_year + 1900, day.tm_mon + 1, day.tm_mday);
		for (int page = 1; page <= 8; page++)
		{
			if (!stalkerCall(source, session, "type=epg&action=get_simple_data_table&ch_id=" + urlEncode(channel.url.substr(at + 3, end - at - 3)) + "&date=" + date + "&p=" + std::to_string(page), body, error))
				return !out.empty();
			size_t before = out.size();
			jsonRead(body, "js.data", ignored, [&](const JsonFields& item) {
				Programme programme;
				programme.title = item.get("name");
				programme.description = item.get("descr");
				programme.start = atoll(item.get("start_timestamp").c_str());
				programme.stop = atoll(item.get("stop_timestamp").c_str());
				programme.from = item.get("t_time");
				programme.to = item.get("t_time_to");
				programme.id = item.get("id");
				programme.archive = item.get("mark_archive") == "1" && !programme.id.empty();
				if (!programme.title.empty() && programme.start > 0)
					out.push_back(std::move(programme));
			});
			if (out.size() == before)
				break;
		}
		return !out.empty();
	}
	if (original.type == "xtream")
	{
		const size_t slash = channel.url.find_last_of('/');
		const size_t dot = channel.url.find_last_of('.');
		if (slash == std::string::npos || dot == std::string::npos || dot < slash)
			return false;
		const Xtream x = xtreamFor(original);
		if (!httpGet(x.api + "&action=get_simple_data_table&stream_id=" + urlEncode(channel.url.substr(slash + 1, dot - slash - 1)), body, error, x.headers, x.agent))
			return false;
		const long long now = (long long)time(nullptr);
		jsonRead(body, "epg_listings", ignored, [&](const JsonFields& item) {
			Programme programme;
			programme.title = fromBase64(item.get("title"));
			programme.description = fromBase64(item.get("description"));
			programme.start = atoll(item.get("start_timestamp").c_str());
			programme.stop = atoll(item.get("stop_timestamp").c_str());
			programme.from = clockOf(item.get("start"));
			programme.to = clockOf(item.get("end"));
			programme.id = timeshiftStamp(item.get("start"));
			programme.archive = item.get("has_archive") == "1" && !programme.id.empty();
			// a week of listings is more than the grid wants: yesterday to tomorrow is plenty
			if (!programme.title.empty() && programme.stop > now - 26 * 3600 && programme.start < now + 26 * 3600)
				out.push_back(std::move(programme));
		});
		return !out.empty();
	}
	return false;
}

bool fetchGuide(const Source& original, const Channel& channel, std::vector<Programme>& out, std::string& error, bool wholeDay)
{
	out.clear();
	if (wholeDay)
	{
		std::string dayError;
		if (fetchGuideDay(original, channel, out, dayError))
			return true;
		out.clear();                                   // no day list: fall back to "now and next"
	}
	std::string body;
	JsonFields ignored;

	if (channel.url.compare(0, 10, "stalker://") == 0)
	{
		// the portal's own number for the channel is in its address: "stalker://id=<number>;..."
		const size_t at = channel.url.find("id=");
		const size_t end = channel.url.find(';', at == std::string::npos ? 0 : at);
		if (at == std::string::npos || end == std::string::npos)
		{
			error = "No programme information for this channel";
			return false;
		}
		const Source source = asPortal(original);
		StalkerSession session;
		if (!stalkerSession(source, session, error))
			return false;
		if (!stalkerCall(source, session, "type=itv&action=get_short_epg&ch_id=" + urlEncode(channel.url.substr(at + 3, end - at - 3)) + "&size=10", body, error))
			return false;
		jsonRead(body, "js", ignored, [&](const JsonFields& item) {
			Programme programme;
			programme.title = item.get("name");
			programme.description = item.get("descr");
			programme.start = atoll(item.get("start_timestamp").c_str());
			programme.stop = atoll(item.get("stop_timestamp").c_str());
			programme.from = item.get("t_time");
			programme.to = item.get("t_time_to");
			if (!programme.title.empty())
				out.push_back(std::move(programme));
		});
	}
	else if (original.type == "xtream")
	{
		// .../live/<user>/<pass>/<number>.ts
		const size_t slash = channel.url.find_last_of('/');
		const size_t dot = channel.url.find_last_of('.');
		if (slash == std::string::npos || dot == std::string::npos || dot < slash)
		{
			error = "No programme information for this channel";
			return false;
		}
		const Xtream x = xtreamFor(original);
		if (!httpGet(x.api + "&action=get_short_epg&stream_id=" + urlEncode(channel.url.substr(slash + 1, dot - slash - 1)) + "&limit=10", body, error, x.headers, x.agent))
			return false;
		jsonRead(body, "epg_listings", ignored, [&](const JsonFields& item) {
			Programme programme;
			programme.title = fromBase64(item.get("title"));
			programme.description = fromBase64(item.get("description"));
			programme.start = atoll(item.get("start_timestamp").c_str());
			programme.stop = atoll(item.get("stop_timestamp").c_str());
			programme.from = clockOf(item.get("start"));
			programme.to = clockOf(item.get("end"));
			if (!programme.title.empty())
				out.push_back(std::move(programme));
		});
	}
	else
	{
		error = "Programme information is not available for M3U playlists yet";
		return false;
	}
	if (out.empty())
	{
		error = "No program information available";
		return false;
	}
	return true;
}

// ---- details page -----------------------------------------------------------------

bool fetchDetails(const Source& original, Channel& channel, std::string& error)
{
	// Portals list everything they know with the film itself. Xtream servers keep the plot and the
	// rest behind a request per film or series.
	if (original.type != "xtream" || usesPortal(original))
		return true;
	const Xtream x = xtreamFor(original);
	std::string body, request;
	if (channel.url.compare(0, 10, "xseries://") == 0)
		request = "&action=get_series_info&series_id=" + urlEncode(channel.url.substr(10));
	else
	{
		const size_t slash = channel.url.find_last_of('/'), dot = channel.url.find_last_of('.');
		if (slash == std::string::npos || dot == std::string::npos || dot < slash)
			return true;
		request = "&action=get_vod_info&vod_id=" + urlEncode(channel.url.substr(slash + 1, dot - slash - 1));
	}
	if (!httpGet(x.api + request, body, error, x.headers, x.agent))
		return false;
	JsonFields fields;
	jsonRead(body, "-", fields, [](const JsonFields&) {});
	JsonFields info;
	for (const auto& field : fields.items)
		if (field.first.compare(0, 5, "info.") == 0)
			info.items.emplace_back(field.first.substr(5), field.second);
	Channel found;
	describe(info, found);
	if (!found.plot.empty()) channel.plot = found.plot;
	if (!found.year.empty()) channel.year = found.year;
	if (!found.rating.empty()) channel.rating = found.rating;
	if (!found.genres.empty()) channel.genres = found.genres;
	if (!found.cast.empty()) channel.cast = found.cast;
	if (!found.length.empty()) channel.length = found.length;
	if (channel.logo.empty())
		channel.logo = info.get("movie_image").empty() ? info.get("cover") : info.get("movie_image");
	return true;
}

// ---- search ----------------------------------------------------------------------------

bool sourceCanSearch(const Source& source)
{
	return usesPortal(source);
}

bool searchSource(const Source& source, int media, const std::string& words, std::vector<Channel>& out, std::string& error)
{
	out.clear();
	if (!usesPortal(source))
		return true;                                   // nothing to ask: the caller searches what it already holds
	int pages = 0;
	return fetchCategory(source, media, "*", [&](std::vector<Channel>& batch) {
		for (Channel& channel : batch)
			out.push_back(std::move(channel));
		return ++pages < 3 && out.size() < 40;         // the first few dozen matches are plenty
	}, error, words);
}

// The address that plays a past programme back, for providers that keep them.
std::string archiveAddress(const Source& source, const Channel& channel, const Programme& programme)
{
	if (!programme.archive || programme.id.empty())
		return "";
	if (channel.url.compare(0, 10, "stalker://") == 0)
		return "stalker://k=tv_archive;id=" + programme.id + ";auto /media/" + programme.id + ".mpg";
	if (source.type == "xtream")
	{
		// .../live/<user>/<pass>/<number>.ts  ->  .../timeshift/<user>/<pass>/<minutes>/<start>/<number>.ts
		const size_t live = channel.url.find("/live/");
		const size_t slash = channel.url.find_last_of('/');
		const size_t dot = channel.url.find_last_of('.');
		if (live == std::string::npos || slash == std::string::npos || dot == std::string::npos || dot < slash)
			return "";
		const long long minutes = programme.stop > programme.start ? (programme.stop - programme.start + 59) / 60 : 60;
		return channel.url.substr(0, live) + "/timeshift/" + channel.url.substr(live + 6, slash - live - 6) + "/" + std::to_string(minutes) + "/" + programme.id + "/"
			+ channel.url.substr(slash + 1, dot - slash - 1) + ".ts";
	}
	return "";
}
