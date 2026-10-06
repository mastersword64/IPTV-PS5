#include "threads.h"
#include "log.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <pthread.h>
#include <signal.h>

extern "C" int sigaltstack(const stack_t*, stack_t*) __attribute__((weak));

namespace
{
	struct Start
	{
		const char* name;
		std::function<void()> work;
	};

	void* run(void* argument)
	{
		Start* start = static_cast<Start*>(argument);
		// A stack for the crash handler, so that a crash on this thread (even
		// running out of stack) still leaves a record.
		if (sigaltstack != nullptr)
		{
			stack_t alternate;
			memset(&alternate, 0, sizeof(alternate));
			alternate.ss_size = 64 * 1024;
			alternate.ss_sp = static_cast<decltype(alternate.ss_sp)>(malloc(alternate.ss_size));
			if (alternate.ss_sp)
				sigaltstack(&alternate, nullptr);
		}
		try
		{
			start->work();
		}
		catch (const std::exception& error)
		{
			logLine("thread %s: stopped by an error: %s", start->name, error.what());
		}
		catch (...)
		{
			logLine("thread %s: stopped by an unknown error", start->name);
		}
		delete start;
		return nullptr;
	}
}

void startThread(const char* name, std::function<void()> work)
{
	Start* start = new Start{ name, std::move(work) };
	pthread_attr_t attributes;
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, 4 * 1024 * 1024);
	pthread_t thread;
	const int result = pthread_create(&thread, &attributes, run, start);
	pthread_attr_destroy(&attributes);
	if (result != 0)
	{
		logLine("thread %s: could not be started (error %d)", name, result);
		delete start;
		return;
	}
	pthread_detach(thread);
}

void logDefaultStackSize()
{
	pthread_attr_t attributes;
	size_t size = 0;
	pthread_attr_init(&attributes);
	pthread_attr_getstacksize(&attributes, &size);
	pthread_attr_destroy(&attributes);
	logLine("threads: the console's default stack size is %lu KB", (unsigned long)(size / 1024));
}
