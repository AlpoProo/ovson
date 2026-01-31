#pragma once
#include <string>
#include <optional>

namespace Hypixel {
	struct PlayerStats {
		std::string uuid;
		std::string displayName;
		int networkLevel = 0;
		int bedwarsStar = 0;
		int bedwarsFinalKills = 0;
		int bedwarsFinalDeaths = 0;
		int bedwarsWins = 0;
		int bedwarsLosses = 0;
		int winstreak = 0;
		std::string teamColor;
		bool isNicked = false;
	};

	std::optional<std::string> getUuidByName(const std::string& name);
	std::optional<PlayerStats> getPlayerStats(const std::string& apiKey, const std::string& uuid);
}


