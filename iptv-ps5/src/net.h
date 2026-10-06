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
