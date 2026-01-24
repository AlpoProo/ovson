#pragma once
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <chrono>

namespace Urchin {
    struct Tag {
        std::string type;        // e.g., "closet_cheater", "blatant_cheater"
        std::string reason;
    };

    struct PlayerTags {
        std::string uuid;
        std::vector<Tag> tags;
    };

    std::optional<PlayerTags> getPlayerTags(const std::string& username);
    void clearCache();
    bool hasAnyTags(const std::string& username);
}
