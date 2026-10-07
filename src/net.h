#pragma once
#include <string>
#include <vector>
// Downloads a URL into memory (used for playlists). Returns false and fills `error` on failure.
// Extra request headers ("Name: value") and a custom User-Agent are optional.
bool httpGet(const std::string& url, std::string& out, std::string& error,
             const std::vector<std::string>& headers = {}, const std::string& userAgent = "", int onlyIfEpoch = -1);
// Stops every download that is in progress (used when the viewer cancels a load).
void httpCancel();
// How much the download in progress has received so far (for the loading screen).
long long httpReceivedBytes();
// A number that changes each time httpCancel() is called. A request given the number it started
// under (onlyIfEpoch) refuses to run once that load has been cancelled.
int httpEpoch();
// A small download on its own connection, for channel logos and posters: nothing is logged, it
// gives up quickly, and it refuses anything larger than `limit` bytes.
// `lane` (0 to 3) picks one of four separate connections, so up to four can run at once.
bool httpGetQuiet(const std::string& url, std::string& out, size_t limit, long timeoutSeconds, int lane = 0);
// A download on a connection of its own, apart from the playlist's: it is not stopped by
// httpCancel() and never waits behind a portal. Used for the radio directory.
bool httpGetOwn(const std::string& url, std::string& out, std::string& error, const std::string& userAgent, long timeoutSeconds, size_t limit);
