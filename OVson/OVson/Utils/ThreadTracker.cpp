#include "ThreadTracker.h"

namespace ThreadTracker {
    std::atomic<int> g_activeThreads{0};
}
