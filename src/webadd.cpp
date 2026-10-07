#include "webadd.h"
#include "log.h"
#include "threads.h"
#include "webpage.h"

#include <arpa/inet.h>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <cerrno>
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
	// (never destroyed: the worker threads may still be using them as the app closes)
	std::mutex& g_mutex = *new std::mutex;
	std::condition_variable& g_answered = *new std::condition_variable;
	std::atomic<bool> g_run{ false };
	std::atomic<int> g_alive{ 0 }, g_visits{ 0 };
	const int kWorkers = 5;                                 // how many requests can be dealt with at once
	std::string g_dataDir, g_address, g_problem, g_key;
	std::string g_status = "{}";
	std::deque<web::Request> g_requests;
	std::deque<web::Query> g_queries;
	std::map<int, std::string> g_answers;
	int g_nextQuery = 1;

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
		static const char* const starts[] = { "playlists.txt", "settings.txt", "theme.txt", "active.txt", "profiles.txt", "favourites", "recent", "resume", "hidden", "watched", "last", "radio-" };
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

	void reply(int client, const char* status, const char* type, const std::string& body, const char* extra = "")
	{
		char head[512];
		snprintf(head, sizeof(head), "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\n%s\r\n", status, type, body.size(), extra);
		std::string all = head + body;
		size_t sent = 0;
		int waited = 0;                                 // milliseconds with nothing accepted
		while (sent < all.size() && waited < 10000)
		{
			const ssize_t n = send(client, all.data() + sent, all.size() - sent, 0);
			if (n > 0)
			{
				sent += (size_t)n;
				waited = 0;
			}
			else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
			{
				usleep(2000);                           // the phone is not ready for more yet
				waited += 2;
			}
			else
				break;
		}
	}

	// Reads what has arrived, waiting no longer than `milliseconds` for something to. The socket
	// itself never waits: the waiting is done here, so it cannot go on longer than intended
	// whatever the console's system makes of a socket's own time limits.
	ssize_t receive(int client, char* into, size_t room, int milliseconds)
	{
		for (int waited = 0;; waited += 3)
		{
			const ssize_t n = recv(client, into, room, 0);
			if (n >= 0)
				return n;
			if ((errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) || waited >= milliseconds || !g_run)
				return -1;
			usleep(3000);
		}
	}

	// Hands a question to the app and waits for its answer (the app looks once per picture drawn).
	std::string askApp(const std::string& what, const std::map<std::string, std::string>& fields)
	{
		std::unique_lock<std::mutex> lock(g_mutex);
		if (g_queries.size() > 16)
			return "{\"err\":\"The console is busy. Try again in a moment.\"}";
		web::Query query;
		const int id = query.id = g_nextQuery++;
		query.what = what;
		query.fields = fields;
		g_queries.push_back(std::move(query));
		const bool answered = g_answered.wait_for(lock, std::chrono::seconds(8), [id] { return g_answers.count(id) != 0 || !g_run; });
		std::string json = "{\"err\":\"The console did not answer.\"}";
		if (answered && g_answers.count(id))
			json = g_answers[id];
		g_answers.erase(id);
		for (auto it = g_queries.begin(); it != g_queries.end(); ++it)
			if (it->id == id)
			{
				g_queries.erase(it);                    // never looked at: take the question back
				break;
			}
		return json;
	}

	void serve(int client)
	{
		// the request: a heading, then possibly a body whose length the heading gives.
		// (Browsers open spare connections and say nothing on them: those are dropped quickly.)
		std::string request;
		char block[8192];
		size_t headerEnd = std::string::npos, length = 0;
		for (;;)
		{
			const ssize_t n = receive(client, block, sizeof(block), request.empty() ? 700 : 10000);
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
		if (headerEnd == std::string::npos || request.size() < headerEnd + 4 + length)
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
		std::map<std::string, std::string> asked = formFields(query);
		const bool get = method == "GET", post = method == "POST";
		const char* const json = "application/json; charset=utf-8";

		// nothing is done, and nothing is told, without the key that the console shows
		if (asked["k"] != g_key)
		{
			if (get && path == "/")
				reply(client, "403 Forbidden", "text/html; charset=utf-8",
					"<!doctype html><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
					"<body style=\"font-family:system-ui,sans-serif;background:#0b0e14;color:#f1f3f8;padding:28px\"><h2>IPTV on PS5</h2>"
					"<p>This address needs the key that the console shows.</p><p>On the console, open <b>Settings &rarr; Remote</b> and scan the code there, "
					"or type the whole address shown under it.</p></body>");
			else
				reply(client, "403 Forbidden", "text/plain", "The key is missing or wrong.");
			return;
		}
		asked.erase("k");

		if (get && path == "/")
		{
			g_visits++;
			reply(client, "200 OK", "text/html; charset=utf-8", kWebPage);
		}
		else if (get && path == "/status")
		{
			std::string status;
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				status = g_status;
			}
			reply(client, "200 OK", json, status);
		}
		else if (get && path == "/list")
			reply(client, "200 OK", json, askApp("list", asked));
		else if (post && path == "/play")
			reply(client, "200 OK", json, askApp("play", asked));
		else if (post && (path == "/key" || path == "/cmd" || path == "/text"))
		{
			web::Request wanted;
			wanted.kind = path.substr(1);
			wanted.fields = asked;
			wanted.body = body.substr(0, 400);
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				if (g_requests.size() < 64)
					g_requests.push_back(std::move(wanted));
			}
			reply(client, "200 OK", json, "{}");
		}
		else if (get && path == "/backup")
			reply(client, "200 OK", "application/octet-stream", makeBackup(), "Content-Disposition: attachment; filename=\"iptv-ps5-backup.txt\"\r\n");
		else if (post && (path == "/add" || path == "/file" || path == "/restore"))
		{
			web::Request wanted;
			wanted.kind = path.substr(1);
			if (wanted.kind == "add")
				wanted.fields = formFields(body);
			else
			{
				wanted.fields = asked;
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

	// One of a few workers, all taking connections from the same listening socket, so that a slow
	// upload or a question waiting on the app does not hold up a button press.
	void loop(int listening, bool last)
	{
		while (g_run)
		{
			sockaddr_in from;
			socklen_t size = sizeof(from);
			const int client = accept(listening, (sockaddr*)&from, &size);
			if (client < 0)
			{
				usleep(8000);                           // nobody yet (the listening socket does not wait)
				continue;
			}
			// the client's own socket does not wait either; serve() does its own, limited, waiting
			const int flags = fcntl(client, F_GETFL, 0);
			fcntl(client, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);
			serve(client);
			close(client);
		}
		if (last)
		{
			// the others have had time to notice and leave before the socket goes
			usleep(60000);
			close(listening);
		}
		g_alive--;
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

bool web::start(const std::string& dataDir, int port, const std::string& addressHint, const std::string& key)
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
	for (int waited = 0; g_alive > 0 && waited < 40; waited++)
		usleep(50000);                                  // a previous run is still closing
	if (g_alive > 0)
	{
		g_problem = "The page is still closing. Try again in a moment.";
		return false;
	}
	g_dataDir = dataDir;
	g_key = key;
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
	if (bind(listening, (sockaddr*)&here, sizeof(here)) != 0 || listen(listening, 8) != 0)
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
	g_address = "http://" + (own.empty() ? std::string("the-console") : own) + ":" + std::to_string(port) + "/?k=" + key;
	g_run = true;
	g_visits = 0;
	logLine("web: listening on port %d", port);
	g_alive = kWorkers;
	for (int worker = 0; worker < kWorkers; worker++)
		startThread("web", [listening, worker] { loop(listening, worker == 0); });
	return true;
}

void web::stop()
{
	if (!g_run)
		return;
	g_run = false;                                      // the workers notice within a moment and close the socket
	g_answered.notify_all();
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

bool web::takeQuery(Query& out)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_queries.empty())
		return false;
	out = std::move(g_queries.front());
	g_queries.pop_front();
	return true;
}

void web::answer(int id, const std::string& json)
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_answers.size() > 32)
			g_answers.clear();                          // (answers nobody came back for)
		g_answers[id] = json;
	}
	g_answered.notify_all();
}

void web::setStatus(const std::string& json)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_status = json;
}
