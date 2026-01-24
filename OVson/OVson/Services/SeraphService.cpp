#include "SeraphService.h"
#include "../Net/Http.h"
#include "../Config/Config.h"
#include "../Utils/Logger.h"
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace Seraph {
    struct CachedTags {
        PlayerTags data;
        std::chrono::steady_clock::time_point timestamp;
    };

    static std::unordered_map<std::string, CachedTags> g_cache;
    static std::mutex g_cacheMutex;
    static const size_t MAX_CACHE_SIZE = 200;
    static const int CACHE_EXPIRY_SECONDS = 300;

    static bool findJsonString(const std::string& json, const char* key, std::string& out) {
        std::string pat = std::string("\"") + key + "\"";
        size_t k = json.find(pat);
        if (k == std::string::npos) return false;
        size_t q1 = json.find('"', json.find(':', k));
        if (q1 == std::string::npos) return false;
        size_t q2 = json.find('"', q1 + 1);
        if (q2 == std::string::npos) return false;
        out = json.substr(q1 + 1, q2 - (q1 + 1));
        return true;
    }

    static void pruneCacheLocked() {
        auto now = std::chrono::steady_clock::now();
        for (auto it = g_cache.begin(); it != g_cache.end(); ) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp).count();
            if (age > CACHE_EXPIRY_SECONDS) {
                it = g_cache.erase(it);
            } else {
                ++it;
            }
        }
        while (g_cache.size() > MAX_CACHE_SIZE) {
            auto oldest = g_cache.begin();
            for (auto it = g_cache.begin(); it != g_cache.end(); ++it) {
                if (it->second.timestamp < oldest->second.timestamp) oldest = it;
            }
            g_cache.erase(oldest);
        }
    }

    static std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_pendingFetches;
    static std::mutex g_pendingMutex;

    std::optional<PlayerTags> getPlayerTags(const std::string& username, const std::string& uuid) {
        if (!Config::isSeraphEnabled()) return std::nullopt;
        if (uuid.empty()) return std::nullopt;

        auto now = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(g_cacheMutex);
            auto it = g_cache.find(uuid);
            if (it != g_cache.end()) {
                auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp).count();
                if (age < CACHE_EXPIRY_SECONDS) return it->second.data;
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_pendingMutex);
            if (g_pendingFetches.count(uuid)) {
                auto age = std::chrono::duration_cast<std::chrono::seconds>(now - g_pendingFetches[uuid]).count();
                if (age < 10) return std::nullopt;
            }
            g_pendingFetches[uuid] = now;
        }

        std::thread([username, uuid]() {
            std::string url = "https://api.seraph.si/" + uuid + "/blacklist";
            std::string apiKey = Config::getSeraphApiKey();
            if (apiKey.empty()) {
                Logger::log(Config::DebugCategory::Seraph, "!!! Seraph Error: API Key is missing !!!");
                std::lock_guard<std::mutex> lock(g_pendingMutex);
                g_pendingFetches.erase(uuid);
                return;
            }

            std::string body;
            Logger::log(Config::DebugCategory::Seraph, "=== Seraph Fetching: %s (%s) ===", username.c_str(), uuid.c_str());
            
            bool ok = Http::get(url, body, "seraph-api-key", apiKey);

            PlayerTags result;
            result.uuid = uuid;
            bool success = false;

            if (ok && !body.empty() && body.find("\"error\"") == std::string::npos) {                
                std::string type, reason;
                bool isBlacklisted = body.find("\"blacklisted\":true") != std::string::npos;
                
                if (isBlacklisted) {
                    findJsonString(body, "type", type);
                    findJsonString(body, "reason", reason);
                    if (type.empty()) type = "Seraph Blacklist";
                    result.tags.push_back({ type, reason });
                }
                success = true;
            }

            {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                pruneCacheLocked();
                g_cache[uuid] = { result, std::chrono::steady_clock::now() };
            }

            {
                std::lock_guard<std::mutex> lock(g_pendingMutex);
                g_pendingFetches.erase(uuid);
            }

            if (success) {
                Logger::log(Config::DebugCategory::Seraph, ">>> Seraph Success: %s (%d tags) <<<", username.c_str(), (int)result.tags.size());
            } else {
                Logger::log(Config::DebugCategory::Seraph, "!!! Seraph Failed for %s !!!", username.c_str());
            }
        }).detach();

        return std::nullopt;
    }

    void clearCache() {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_cache.clear();
    }

    bool hasAnyTags(const std::string& username, const std::string& uuid) {
        auto res = getPlayerTags(username, uuid);
        return res.has_value() && !res->tags.empty();
    }
}
