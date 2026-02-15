#pragma once
#include <atomic>
#include <windows.h>

namespace ThreadTracker {
    extern std::atomic<int> g_activeThreads;

    inline void increment() { g_activeThreads++; }
    inline void decrement() { g_activeThreads--; }
    
    inline void waitForAll() {
        int retries = 0;
        while (g_activeThreads > 0 && retries < 150) {
            Sleep(10);
            retries++;
        }
    }
}
