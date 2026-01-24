#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include "../Services/Hypixel.h"

namespace ChatInterceptor {
	void initialize();
	void poll();
	void shutdown();

	// 0 = bedwars, 1 = skywars, 2 = duels
	void setMode(int mode);

	// stats map access for GUI overlay
	extern std::unordered_map<std::string, Hypixel::PlayerStats> g_playerStatsMap;
	extern std::mutex g_statsMutex;
    extern std::unordered_map<std::string, std::string> g_playerTeamColor;
}


