#pragma once
#include <functional>
// Starts a background thread with a large stack of its own (the console's default
// may be small), and with crash recording set up for it.
void startThread(const char* name, std::function<void()> work);
void logDefaultStackSize();
