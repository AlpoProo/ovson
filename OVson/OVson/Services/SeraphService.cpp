#include "SeraphService.h"
#include "../Net/Http.h"
#include "../Config/Config.h"
#include "../Chat/ChatSDK.h"
#include "../Render/NotificationManager.h"
#include "../Chat/ChatInterceptor.h"
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
        if (!Config::isTagsEnabled()) return std::nullopt;
        if (Config::getActiveTagService() != "Seraph" && Config::getActiveTagService() != "Both") return std::nullopt;
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
                std::lock_guard<std::mutex> lock(g_pendingMutex);
                g_pendingFetches.erase(uuid);
                return;
            }

            std::string body;
            bool ok = Http::get(url, body, "seraph-api-key", apiKey);

            PlayerTags result;
            result.uuid = uuid;

            if (ok && !body.empty() && body.find("\"success\":true") != std::string::npos) {
                size_t blacklistPos = body.find("\"blacklist\"");
                if (blacklistPos != std::string::npos) {
                    std::string blacklistSection = body.substr(blacklistPos);
                    size_t endPos = blacklistSection.find("},");
                    if (endPos == std::string::npos) endPos = blacklistSection.find("}");
                    if (endPos != std::string::npos) {
                        blacklistSection = blacklistSection.substr(0, endPos + 1);
                    }

                    if (blacklistSection.find("\"tagged\":true") != std::string::npos) {
                        std::string reportType, tooltip;
                        findJsonString(blacklistSection, "report_type", reportType);
                        findJsonString(blacklistSection, "tooltip", tooltip);
                        
                        size_t parenPos = tooltip.rfind('(');
                        if (parenPos != std::string::npos && tooltip.find("by", parenPos) != std::string::npos) {
                            tooltip = tooltip.substr(0, parenPos);
                            while (!tooltip.empty() && isspace(tooltip.back())) tooltip.pop_back();
                        }

                        if (reportType.empty()) reportType = "Seraph Blacklist";
                        result.tags.push_back({ reportType, tooltip });
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                pruneCacheLocked();
                g_cache[uuid] = { result, std::chrono::steady_clock::now() };
            }

            if (!result.tags.empty() && ChatInterceptor::shouldAlert(username)) {
                std::string type = result.tags[0].type;
                std::string alert = ChatSDK::formatPrefix() + "\xC2\xA7" "4SERAPH ALERT: \xC2\xA7" "f" + username + " is blacklisted: \xC2\xA7" "l" + type + "\xC2\xA7" "r!";
                ChatSDK::showClientMessage(alert);
                Render::NotificationManager::getInstance()->add("Seraph Alert", username + " is blacklisted (" + type + ")", Render::NotificationType::Error);
            }

            {
                std::lock_guard<std::mutex> lock(g_pendingMutex);
                g_pendingFetches.erase(uuid);
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
