#pragma once
#include <map>
#include <string>
#include <vector>

// A small web page served by the console itself, for a phone or computer on the same network:
// a virtual remote control, the channel lists to browse and play from, search, radio, and the
// set-up page (add a playlist with a real keyboard, send an M3U file, save or restore a backup).
// Every address carries a short key, shown on the console as a QR code, so that only someone who
// can see the television can use it.
namespace web
{
	// Starts listening. Returns false when this console's system does not offer what is needed.
	bool start(const std::string& dataDir, int port, const std::string& addressHint, const std::string& key);
	void stop();
	bool running();
	std::string address();                          // what to type or scan: "http://10.0.0.5:8080/?k=abc234"
	std::string problem();                          // why start() failed, in words

	// Something the page asked to be done; the app carries it out when it next looks.
	struct Request
	{
		std::string kind;                           // "add", "file", "restore", "key", "cmd" or "text"
		std::map<std::string, std::string> fields;  // add: type, name, a, b, c, mac | file: name | key: b | cmd: c, by | text: done
		std::string body;                           // file: the playlist | restore: the backup | text: what was typed
	};
	bool take(Request& out);

	// Something the page asked and is waiting to hear back about (a list, a search, "play this").
	// The app answers with JSON text; the page's connection is held open until it does.
	struct Query
	{
		int id = 0;
		std::string what;                           // "list" or "play"
		std::map<std::string, std::string> fields;
	};
	bool takeQuery(Query& out);
	void answer(int id, const std::string& json);

	void setStatus(const std::string& json);        // what is playing and so on, handed to the page when it asks
	int visits();                                   // how many times the page has been opened
}
