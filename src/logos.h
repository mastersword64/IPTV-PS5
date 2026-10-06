#pragma once
#include <string>

// Channel logos and film posters: fetched in the background, shrunk, kept on the console so each
// is only downloaded once, and handed to the screen as small pictures.
void logosStart(const std::string& dataDir);
// The picture for an address, or -1 if it is not ready (it is then fetched, and will be there on
// a later frame) or cannot be had. Call from the drawing thread.
// `poster` asks for the larger size used on the poster wall and details pages.
int logoImage(const std::string& url, bool poster = false);
// Call once per frame from the drawing thread: turns finished downloads into pictures.
void logosPump();
