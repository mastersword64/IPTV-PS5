#pragma once
// One line per call, appended to iptv-log.txt in the app's data folder.
void logLine(const char* format, ...);
void logMissingSystemFunctions();
