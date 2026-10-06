#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A pointer picture: one frame, or several for an animated one.
struct CursorFrame
{
	std::vector<uint8_t> rgba;
	int width = 0, height = 0;
	int hotX = 0, hotY = 0;          // the pixel that does the pointing
};

// Reads a pointer file: Windows cursors (.cur, .ico), animated cursors (.ani), or an ordinary
// picture (.png and the like, when the build has the picture decoder; it points with its top-left
// corner). `frameSeconds` is how long each frame of an animated cursor is shown.
bool loadCursorFile(const std::string& path, std::vector<CursorFrame>& frames, double& frameSeconds);
