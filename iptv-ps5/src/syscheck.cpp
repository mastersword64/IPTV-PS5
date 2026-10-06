// System functions the console does not provide to a title.
#include "log.h"
#include <cstddef>
#include <cstdio>
#include <netdb.h>
#include <pthread.h>

// Turns a network address into text ("1.2.3.4" and "80"). FFmpeg calls it before every connection.
extern "C" int iptv_getnameinfo(const void*, unsigned, char*, size_t, char*, size_t, int) __asm__("getnameinfo");
extern "C" int iptv_getnameinfo(const void* address, unsigned length, char* host, size_t hostSize, char* service, size_t serviceSize, int flags)
{
	(void)length; (void)flags;
	const unsigned char* b = static_cast<const unsigned char*>(address);
	if (!b)
		return 4;
	if (host && hostSize > 0)
		snprintf(host, hostSize, "%u.%u.%u.%u", b[4], b[5], b[6], b[7]);
	if (service && serviceSize > 0)
		snprintf(service, serviceSize, "%u", (unsigned)((b[2] << 8) | b[3]));
	return 0;
}

// FFmpeg decides whether an address is a bare number (like 1.2.3.4) by asking for a lookup that
// is only allowed to succeed for numbers. The console's lookup ignores that restriction and
// resolves names too, so FFmpeg then treats every host as a number and leaves the host name out
// of secure connections - and many servers refuse those ("handshake failure"). This puts the
// restriction back. (The build reroutes every lookup through here.)
static bool isNumericAddress(const char* text)
{
	int dots = 0, digits = 0;
	for (const char* c = text; *c; c++)
	{
		if (*c == ':')
			return true;                       // an IPv6 address
		if (*c == '.')
			dots++;
		else if (*c >= '0' && *c <= '9')
			digits++;
		else
			return false;
	}
	return dots == 3 && digits >= 4;
}

extern "C" int __real_getaddrinfo(const char*, const char*, const struct addrinfo*, struct addrinfo**);
extern "C" int __wrap_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** result)
{
	if (node && hints && (hints->ai_flags & AI_NUMERICHOST) && !isNumericAddress(node))
		return EAI_NONAME;
	return __real_getaddrinfo(node, service, hints, result);
}

// The console gives a new thread only 64 KB of stack unless told otherwise. That is too little for
// video decoding, so FFmpeg's extra decoder threads could not be used (and playback was limited to
// one thread). Every thread started without an explicit stack size now gets 1 MB. (The build
// reroutes thread creation through here.)
extern "C" int iptv_real_pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*) __asm__("__real_pthread_create");
extern "C" int iptv_wrap_pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*) __asm__("__wrap_pthread_create");
extern "C" int iptv_wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attributes, void* (*entry)(void*), void* argument)
{
	if (attributes)
		return iptv_real_pthread_create(thread, attributes, entry, argument);
	pthread_attr_t larger;
	if (pthread_attr_init(&larger) != 0)
		return iptv_real_pthread_create(thread, nullptr, entry, argument);
	pthread_attr_setstacksize(&larger, 1024 * 1024);
	const int result = iptv_real_pthread_create(thread, &larger, entry, argument);
	pthread_attr_destroy(&larger);
	return result;
}

#define LIST X(poll) X(select) X(getsockopt) X(setsockopt) X(getpeername) X(getsockname) X(recv) X(send) X(recvfrom) X(sendto) \
	X(shutdown) X(connect) X(socket) X(nanosleep) X(usleep) X(gettimeofday) X(clock_gettime) X(sched_yield) X(sysconf) \
	X(pthread_cancel) X(pthread_setcancelstate) X(pthread_cond_timedwait) X(pthread_once) X(strerror_r) X(gmtime) X(time)
#define X(n) extern "C" void chk_##n() __asm__(#n) __attribute__((weak));
LIST
#undef X

void logMissingSystemFunctions()
{
	struct Entry { const char* name; void (*function)(); };
#define X(n) { #n, chk_##n },
	static const Entry entries[] = { LIST };
#undef X
	int missing = 0;
	for (const Entry& entry : entries)
	{
		void (*volatile function)() = entry.function;
		if (!function)
		{
			logLine("system: %s is NOT available on the console", entry.name);
			missing++;
		}
	}
	logLine("system: checked %d functions, %d missing", (int)(sizeof(entries) / sizeof(entries[0])), missing);
}
