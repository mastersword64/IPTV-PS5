#pragma once
#include "playlist.h"
#include <string>
#include <vector>

// Public radio stations, from the community-run Radio Browser directory (radio-browser.info).
// Nothing is built into the app: lists are asked for when the Radio screen wants them.
// A station is held as a Channel: name, url (the stream), logo, group (its country),
// genres (its tags), length (codec and bit rate, as words) and tvgId (the directory's id for it).
namespace radio
{
	struct Country
	{
		std::string name, code;                     // "Germany", "DE"
		int stations = 0;
	};

	// Each of these asks the directory and waits for the answer: call them from a background thread.
	bool popular(std::vector<Channel>& out, std::string& error);
	bool byCountry(const std::string& code, std::vector<Channel>& out, std::string& error);
	bool byTag(const std::string& tag, std::vector<Channel>& out, std::string& error);
	bool search(const std::string& words, std::vector<Channel>& out, std::string& error);
	bool countries(std::vector<Country>& out, std::string& error);
	// Tells the directory a station was played (it ranks stations by this). Nothing is waited for.
	void played(const std::string& stationId);

	// Stations kept on the console (favourites, recently played): one per line, fields apart by tabs.
	std::string toText(const std::vector<Channel>& stations);
	std::vector<Channel> fromText(const std::string& text);
}
