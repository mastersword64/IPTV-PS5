#include "net.h"
#include "log.h"
#include <curl/curl.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cstdio>

// Downloads in progress stop when this number changes (see httpCancel).
static std::atomic<int> g_epoch{ 0 };

void httpCancel()
{
	g_epoch++;
}

static int onProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return g_epoch.load() != *static_cast<int*>(user) ? 1 : 0;   // non-zero stops the transfer
}

static std::atomic<long long> g_received{ 0 };

long long httpReceivedBytes()
{
	return g_received.load();
}

// The steps of a connection (looking up the name, connecting, the secure handshake) go to the log,
// so a download that hangs shows where it stopped.
static int onDebug(CURL*, curl_infotype type, char* data, size_t size, void* user)
{
	int* lines = static_cast<int*>(user);
	if (type != CURLINFO_TEXT || *lines >= 40)
		return 0;
	(*lines)++;
	std::string text(data, size);
	while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
		text.pop_back();
	logLine("curl: %s", text.c_str());
	return 0;
}

static size_t onData(char* data, size_t size, size_t count, void* user)
{
	g_received += (long long)(size * count);
	static_cast<std::string*>(user)->append(data, size * count);
	return size * count;
}

int httpEpoch()
{
	return g_epoch.load();
}

bool httpGet(const std::string& url, std::string& out, std::string& error, const std::vector<std::string>& headers, const std::string& userAgent, int onlyIfEpoch)
{
	// One connection handle is kept and reused for every request. Requests to the same server
	// then share a connection (much quicker for portals, which need several in a row), and the
	// handle is never torn down between them.
	static std::mutex mutex;
	static CURL* curl = nullptr;
	std::lock_guard<std::mutex> lock(mutex);

	out.clear();
	if (onlyIfEpoch >= 0 && onlyIfEpoch != g_epoch.load())
	{
		error = "cancelled";       // the load this request belongs to was abandoned
		return false;
	}
	if (!curl)
	{
		curl_global_init(CURL_GLOBAL_DEFAULT);
		curl = curl_easy_init();
	}
	else
		curl_easy_reset(curl);
	if (!curl)
	{
		error = "the download library could not start";
		return false;
	}
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.empty() ? "VLC/3.0.20 LibVLC/3.0.20" : userAgent.c_str());
	curl_slist* list = nullptr;
	for (const std::string& header : headers)
		list = curl_slist_append(list, header.c_str());
	if (list)
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
	// give up on a connection that has gone quiet (a portal can take a while to put a big
	// channel list together), and stop at once when the viewer cancels
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
	int epoch = g_epoch.load();
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &epoch);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
	// Secure addresses are checked against /app0/cacert.pem when that file is on the console.
	// Without it there is nothing to check against, so the connection is encrypted but not verified.
	static int haveCertificates = -1;
	if (haveCertificates < 0)
	{
		FILE* f = fopen("/app0/cacert.pem", "rb");
		haveCertificates = f ? 1 : 0;
		if (f)
			fclose(f);
	}
	if (!haveCertificates)
	{
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	}
	int debugLines = 0;
	curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, onDebug);
	curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &debugLines);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	g_received = 0;
	const auto began = std::chrono::steady_clock::now();
	const CURLcode result = curl_easy_perform(curl);
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	logLine("download: %lld bytes in %.1f s, status %ld: %s", (long long)out.size(), seconds, status, result == CURLE_OK ? "finished" : curl_easy_strerror(result));
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, nullptr);
	if (list)
		curl_slist_free_all(list);
	if (result != CURLE_OK)
	{
		error = epoch != g_epoch.load() ? "cancelled" : curl_easy_strerror(result);
		return false;
	}
	if (status >= 400)
	{
		error = "the server answered with error " + std::to_string(status);
		return false;
	}
	return true;
}

namespace
{
	struct Limited { std::string* out; size_t limit; };
	size_t onLimitedData(char* data, size_t size, size_t count, void* user)
	{
		Limited* limited = static_cast<Limited*>(user);
		if (limited->out->size() + size * count > limited->limit)
			return 0;                                  // too large: stop
		limited->out->append(data, size * count);
		return size * count;
	}
}

// Used for channel logos and posters: its own connection (so it never holds up the portal),
// short patience, a size limit, and nothing written to the log.
bool httpGetQuiet(const std::string& url, std::string& out, size_t limit, long timeoutSeconds, int lane)
{
	// one connection per lane, so several pictures can be fetched side by side
	static std::mutex mutexes[4];
	static CURL* handles[4] = { nullptr, nullptr, nullptr, nullptr };
	lane = lane < 0 ? 0 : lane > 3 ? 3 : lane;
	std::lock_guard<std::mutex> lock(mutexes[lane]);
	CURL*& curl = handles[lane];
	out.clear();
	if (!curl)
		curl = curl_easy_init();
	else
		curl_easy_reset(curl);
	if (!curl)
		return false;
	Limited limited{ &out, limit };
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "VLC/3.0.20 LibVLC/3.0.20");
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onLimitedData);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &limited);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	const CURLcode result = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	return result == CURLE_OK && status < 400 && !out.empty();
}
