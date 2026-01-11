#pragma once
#include <vector>
#include <string>

namespace ChatInterceptor {
	void initialize();
	void poll();
	void shutdown();

	// 0 = bedwars, 1 = skywars, 2 = duels
	void setMode(int mode);

    // who-related helpers removed
}

// who-related data access removed


