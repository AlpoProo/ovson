#pragma once
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <chrono>

namespace Seraph {
    struct Tag {
        std::string type;
        std::string reason;
    };

    struct PlayerTags {
        std::string uuid;
        std::vector<Tag> tags;
    };

    std::optional<PlayerTags> getPlayerTags(const std::string& username, const std::string& uuid);

    void clearCache();

    bool hasAnyTags(const std::string& username, const std::string& uuid);
}
