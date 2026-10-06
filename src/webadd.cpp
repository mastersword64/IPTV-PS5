#include "webadd.h"
#include "log.h"
#include "threads.h"

#include <arpa/inet.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

// These come from the console's system. An app is not guaranteed all of them, so each is looked
// for when the server starts, and the feature is simply unavailable if one is missing.
#pragma weak socket
#pragma weak bind
#pragma weak listen
#pragma weak accept
#pragma weak recv
#pragma weak send
#pragma weak setsockopt
#pragma weak getsockname
#pragma weak connect

namespace
{
	std::mutex& g_mutex = *new std::mutex;
	std::atomic<bool> g_run{ false }, g_alive{ false };
	std::atomic<int> g_visits{ 0 };
	int g_listen = -1;
	std::string g_dataDir, g_address, g_problem;
	std::deque<web::Request> g_requests;
	std::vector<std::string> g_playlists;

	std::string escapeHtml(const std::string& text)
	{
		std::string out;
		for (const char c : text)
		{
			if (c == '<') out += "&lt;";
			else if (c == '>') out += "&gt;";
			else if (c == '&') out += "&amp;";
			else if (c == '"') out += "&quot;";
			else out += c;
		}
		return out;
	}

	std::string unescape(const std::string& text)
	{
		std::string out;
		for (size_t i = 0; i < text.size(); i++)
		{
			if (text[i] == '+')
				out += ' ';
			else if (text[i] == '%' && i + 2 < text.size())
			{
				out += (char)strtol(text.substr(i + 1, 2).c_str(), nullptr, 16);
				i += 2;
			}
			else
				out += text[i];
		}
		return out;
	}

	std::map<std::string, std::string> formFields(const std::string& text)
	{
		std::map<std::string, std::string> fields;
		size_t pos = 0;
		while (pos < text.size())
		{
			size_t end = text.find('&', pos);
			if (end == std::string::npos)
				end = text.size();
			const std::string pair = text.substr(pos, end - pos);
			const size_t equals = pair.find('=');
			if (equals != std::string::npos)
				fields[unescape(pair.substr(0, equals))] = unescape(pair.substr(equals + 1));
			pos = end + 1;
		}
		return fields;
	}

	// The files that make up the viewer's setup: playlists, settings, favourites, history, hidden things.
	bool belongsInBackup(const std::string& name)
	{
		static const char* const starts[] = { "playlists.txt", "settings.txt", "theme.txt", "active.txt", "profiles.txt", "favourites", "recent", "resume", "hidden", "watched", "last" };
		if (name.size() < 5 || name.compare(name.size() - 4, 4, ".txt") != 0 || name.find("log") != std::string::npos)
			return false;
		for (const char* start : starts)
			if (name.compare(0, strlen(start), start) == 0)
				return true;
		return false;
	}

	std::string makeBackup()
	{
		// each file as: a line "FILE <name> <bytes>", then exactly that many bytes, then a line break
		std::string out = "IPTV-PS5-BACKUP 1\n";
		DIR* dir = opendir(g_dataDir.c_str());
		if (!dir)
			return out;
		while (const dirent* entry = readdir(dir))
		{
			const std::string name = entry->d_name;
			if (!belongsInBackup(name))
				continue;
			FILE* f = fopen((g_dataDir + "/" + name).c_str(), "rb");
			if (!f)
				continue;
			std::string content;
			char block[4096];
			size_t n;
			while ((n = fread(block, 1, sizeof(block), f)) > 0)
				content.append(block, n);
			fclose(f);
			out += "FILE " + name + " " + std::to_string(content.size()) + "\n" + content + "\n";
		}
		closedir(dir);
		return out;
	}

	const char* const kPage = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>IPTV on PS5</title><style>
body{font-family:system-ui,sans-serif;background:#0b0e14;color:#f1f3f8;margin:0;padding:20px;max-width:640px;margin:auto}
h1{font-size:24px}h2{font-size:18px;margin:26px 0 8px;color:#9fb7ff}
.card{background:#171b28;border:1px solid #2a3044;border-radius:14px;padding:16px;margin:12px 0}
label{display:block;font-size:13px;color:#8b93a7;margin:10px 0 4px}
input,select,button{width:100%;box-sizing:border-box;font-size:16px;padding:12px;border-radius:10px;border:1px solid #2a3044;background:#10131a;color:#f1f3f8}
button{background:#6f9cf5;color:#0b0e14;border:0;font-weight:600;margin-top:14px}
button.plain{background:#232a3c;color:#f1f3f8}
#note{position:sticky;top:8px;background:#1e2a47;border-radius:10px;padding:12px;display:none;margin-bottom:8px}
li{margin:4px 0}.hint{font-size:13px;color:#8b93a7}
</style></head><body>
<h1>IPTV on PS5</h1><div id="note"></div>
<div class="card"><h2 style="margin-top:0">Playlists on the console</h2><ul>%PLAYLISTS%</ul></div>
<div class="card"><h2 style="margin-top:0">Add a playlist</h2>
<label>Kind</label><select id="type" onchange="kind()"><option value="url">M3U web address</option><option value="xtream">Xtream Codes account</option><option value="stalker">Stalker / Ministra portal</option></select>
<label>Name (optional)</label><input id="name" placeholder="My TV">
<label id="la">Address of the M3U playlist</label><input id="a" placeholder="http://example.com/list.m3u" autocapitalize="off" autocorrect="off">
<div id="xb"><label id="lb">Username</label><input id="b" autocapitalize="off" autocorrect="off"></div>
<div id="xc"><label>Password</label><input id="c" autocapitalize="off" autocorrect="off"></div>
<div id="xm"><label>MAC address (optional)</label><input id="mac" placeholder="00:1A:79:12:34:56" autocapitalize="characters"></div>
<button onclick="add()">Add to the console</button></div>
<div class="card"><h2 style="margin-top:0">Send an M3U file</h2><p class="hint">Choose a .m3u or .m3u8 file from this device.</p>
<input type="file" id="file" accept=".m3u,.m3u8,audio/x-mpegurl,text/plain"><button class="plain" onclick="sendFile()">Send the file</button></div>
<div class="card"><h2 style="margin-top:0">Backup</h2><p class="hint">Playlists, settings, favorites, hidden groups and watch history, as one file.</p>
<button class="plain" onclick="location='/backup'">Save a backup to this device</button>
<label>Restore from a backup file</label><input type="file" id="backup"><button class="plain" onclick="restore()">Restore to the console</button></div>
<script>
function note(t){var n=document.getElementById('note');n.textContent=t;n.style.display='block';window.scrollTo(0,0)}
function v(i){return document.getElementById(i).value.trim()}
function kind(){var t=v('type');document.getElementById('xb').style.display=t=='url'?'none':'block';
document.getElementById('xc').style.display=t=='xtream'?'block':'none';document.getElementById('xm').style.display=t=='xtream'?'block':'none';
document.getElementById('la').textContent=t=='url'?'Address of the M3U playlist':t=='xtream'?'Server address':'Portal address';
document.getElementById('lb').textContent=t=='stalker'?'MAC address':'Username';
document.getElementById('a').placeholder=t=='url'?'http://example.com/list.m3u':'http://example.com:8080'}
function post(u,b,h){return fetch(u,{method:'POST',body:b,headers:h||{}}).then(function(r){return r.text()}).then(note).catch(function(){note('The console did not answer. Is the screen with the code still open?')})}
function add(){if(!v('a')){note('An address is needed.');return}
var f=['type','name','a','b','c','mac'].map(function(k){return k+'='+encodeURIComponent(v(k))}).join('&');
post('/add',f,{'Content-Type':'application/x-www-form-urlencoded'})}
function sendFile(){var f=document.getElementById('file').files[0];if(!f){note('Choose a file first.');return}
post('/file?name='+encodeURIComponent(f.name),f)}
function restore(){var f=document.getElementById('backup').files[0];if(!f){note('Choose a backup file first.');return}
if(confirm('Replace the playlists, settings and favorites on the console with this backup?'))post('/restore',f)}
kind()</script></body></html>)HTML";

	void reply(int client, const char* status, const char* type, const std::string& body, const char* extra = "")
	{
		char head[512];
		snprintf(head, sizeof(head), "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\n%s\r\n", status, type, body.size(), extra);
		std::string all = head + body;
		size_t sent = 0;
		while (sent < all.size())
		{
			const ssize_t n = send(client, all.data() + sent, all.size() - sent, 0);
			if (n <= 0)
				break;
			sent += (size_t)n;
		}
	}

	void serve(int client)
	{
		// the request: a heading, then possibly a body whose length the heading gives
		std::string request;
		char block[8192];
		size_t headerEnd = std::string::npos, length = 0;
		for (;;)
		{
			const ssize_t n = recv(client, block, sizeof(block), 0);
			if (n <= 0)
				break;
			request.append(block, (size_t)n);
			if (headerEnd == std::string::npos)
			{
				headerEnd = request.find("\r\n\r\n");
				if (headerEnd != std::string::npos)
				{
					std::string head = request.substr(0, headerEnd);
					for (char& c : head)
						if (c >= 'A' && c <= 'Z')
							c = (char)(c + 32);
					const size_t at = head.find("content-length:");
					length = at == std::string::npos ? 0 : (size_t)atol(head.c_str() + at + 15);
					if (length > 24u * 1024 * 1024)
					{
						reply(client, "413 Too Large", "text/plain", "That is too large for the console to take this way.");
						return;
					}
				}
				else if (request.size() > 65536)
					return;
			}
			if (headerEnd != std::string::npos && request.size() >= headerEnd + 4 + length)
				break;
		}
		if (headerEnd == std::string::npos)
			return;
		const size_t lineEnd = request.find("\r\n");
		const std::string line = request.substr(0, lineEnd);
		const size_t s1 = line.find(' '), s2 = line.rfind(' ');
		if (s1 == std::string::npos || s2 <= s1)
			return;
		const std::string method = line.substr(0, s1);
		std::string path = line.substr(s1 + 1, s2 - s1 - 1), query;
		const size_t mark = path.find('?');
		if (mark != std::string::npos)
		{
			query = path.substr(mark + 1);
			path = path.substr(0, mark);
		}
		const std::string body = request.substr(headerEnd + 4, length);

		if (method == "GET" && path == "/")
		{
			g_visits++;
			std::string list;
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				for (const std::string& name : g_playlists)
					list += "<li>" + escapeHtml(name) + "</li>";
			}
			if (list.empty())
				list = "<li class=\"hint\">None yet</li>";
			std::string page = kPage;
			const size_t at = page.find("%PLAYLISTS%");
			if (at != std::string::npos)
				page.replace(at, 11, list);
			reply(client, "200 OK", "text/html; charset=utf-8", page);
		}
		else if (method == "GET" && path == "/backup")
			reply(client, "200 OK", "application/octet-stream", makeBackup(), "Content-Disposition: attachment; filename=\"iptv-ps5-backup.txt\"\r\n");
		else if (method == "POST" && (path == "/add" || path == "/file" || path == "/restore"))
		{
			web::Request wanted;
			wanted.kind = path.substr(1);
			if (wanted.kind == "add")
				wanted.fields = formFields(body);
			else
			{
				wanted.fields = formFields(query);
				wanted.body = body;
			}
			std::string answer = "Sent to the console.";
			if (wanted.kind == "add" && wanted.fields["a"].empty())
				answer = "An address is needed.";
			else if (wanted.kind == "restore" && body.compare(0, 15, "IPTV-PS5-BACKUP") != 0)
				answer = "That file is not a backup made by this app.";
			else if (wanted.kind != "add" && body.empty())
				answer = "The file was empty.";
			else
			{
				if (wanted.kind == "add") answer = "Added. It is now in the list of playlists on the console.";
				if (wanted.kind == "file") answer = "The file is on the console and has been added as a playlist.";
				if (wanted.kind == "restore") answer = "Restored. Close and reopen the app on the console to use it.";
				std::lock_guard<std::mutex> lock(g_mutex);
				g_requests.push_back(std::move(wanted));
			}
			reply(client, "200 OK", "text/plain; charset=utf-8", answer);
		}
		else
			reply(client, "404 Not Found", "text/plain", "Nothing here.");
	}

	void loop(int listening)
	{
		g_alive = true;
		while (g_run)
		{
			sockaddr_in from;
			socklen_t size = sizeof(from);
			const int client = accept(listening, (sockaddr*)&from, &size);
			if (client < 0)
			{
				usleep(120000);                         // nobody yet (the listening socket does not wait)
				continue;
			}
			// the client's own socket waits for data, but not for ever
			const int flags = fcntl(client, F_GETFL, 0);
			if (flags >= 0)
				fcntl(client, F_SETFL, flags & ~O_NONBLOCK);
			if (setsockopt)
			{
				timeval patience = { 8, 0 };
				setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &patience, sizeof(patience));
				setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &patience, sizeof(patience));
			}
			serve(client);
			close(client);
		}
		close(listening);
		g_alive = false;
	}

	// The console's own address on the network: found by asking which address would be used to reach
	// the outside world (nothing is actually sent).
	std::string ownAddress()
	{
		if (!getsockname || !connect)
			return "";
		const int probe = socket(AF_INET, SOCK_DGRAM, 0);
		if (probe < 0)
			return "";
		sockaddr_in far;
		memset(&far, 0, sizeof(far));
		far.sin_family = AF_INET;
		far.sin_port = htons(53);
		far.sin_addr.s_addr = htonl(0x01010101);        // 1.1.1.1
		std::string found;
		if (connect(probe, (sockaddr*)&far, sizeof(far)) == 0)
		{
			sockaddr_in mine;
			socklen_t size = sizeof(mine);
			if (getsockname(probe, (sockaddr*)&mine, &size) == 0 && mine.sin_addr.s_addr != 0)
			{
				const uint32_t a = ntohl(mine.sin_addr.s_addr);
				char text[32];
				snprintf(text, sizeof(text), "%u.%u.%u.%u", (a >> 24) & 255, (a >> 16) & 255, (a >> 8) & 255, a & 255);
				found = text;
			}
		}
		close(probe);
		return found;
	}
}

bool web::start(const std::string& dataDir, int port, const std::string& addressHint)
{
	if (g_run)
		return true;
	g_problem.clear();
	if (!socket || !bind || !listen || !accept || !recv || !send)
	{
		g_problem = "This console's system does not let the app accept connections.";
		logLine("web: %s", g_problem.c_str());
		return false;
	}
	for (int waited = 0; g_alive && waited < 20; waited++)
		usleep(50000);                                  // a previous run is still closing
	g_dataDir = dataDir;
	const int listening = socket(AF_INET, SOCK_STREAM, 0);
	if (listening < 0)
	{
		g_problem = "The app could not open a network socket.";
		return false;
	}
	if (setsockopt)
	{
		int yes = 1;
		setsockopt(listening, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	}
	sockaddr_in here;
	memset(&here, 0, sizeof(here));
	here.sin_family = AF_INET;
	here.sin_port = htons((uint16_t)port);
	here.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(listening, (sockaddr*)&here, sizeof(here)) != 0 || listen(listening, 4) != 0)
	{
		close(listening);
		g_problem = "The app could not listen on port " + std::to_string(port) + ".";
		logLine("web: %s", g_problem.c_str());
		return false;
	}
	const int flags = fcntl(listening, F_GETFL, 0);
	fcntl(listening, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);
	std::string own = ownAddress();
	if (own.empty())
		own = addressHint;
	g_address = "http://" + (own.empty() ? std::string("the-console") : own) + ":" + std::to_string(port);
	g_listen = listening;
	g_run = true;
	g_visits = 0;
	logLine("web: listening at %s", g_address.c_str());
	startThread("web", [listening] { loop(listening); });
	return true;
}

void web::stop()
{
	if (!g_run)
		return;
	g_run = false;                                      // the loop notices within a moment and closes the socket
	logLine("web: stopped");
}

bool web::running() { return g_run; }
std::string web::address() { return g_address; }
std::string web::problem() { return g_problem; }
int web::visits() { return g_visits; }

bool web::take(Request& out)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_requests.empty())
		return false;
	out = std::move(g_requests.front());
	g_requests.pop_front();
	return true;
}

void web::setPlaylists(const std::vector<std::string>& names)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_playlists = names;
}
