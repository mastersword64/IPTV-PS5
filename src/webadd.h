#pragma once
#include <map>
#include <string>
#include <vector>

// A small web page served by the console itself, so a phone or computer on the same network can
// add playlists (typing long addresses on a controller is slow), send an M3U file, and save or
// restore a backup. It only runs while its screen in Settings is open.
namespace web
{
	// Starts listening. Returns false when this console's system does not offer what is needed.
	bool start(const std::string& dataDir, int port, const std::string& addressHint);
	void stop();
	bool running();
	std::string address();                          // what to type or scan: "http://10.0.0.5:8080"
	std::string problem();                          // why start() failed, in words

	// What someone asked for on the page, for the app to carry out.
	struct Request
	{
		std::string kind;                           // "add", "file" or "restore"
		std::map<std::string, std::string> fields;  // add: type, name, a, b, c, mac | file: name
		std::string body;                           // file: the playlist | restore: the backup
	};
	bool take(Request& out);
	void setPlaylists(const std::vector<std::string>& names);   // shown on the page
	int visits();                                   // how many times the page has been opened
}
