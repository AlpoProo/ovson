#include "UrchinService.h"
#include "../Net/Http.h"
#include "../Config/Config.h"
#include "../Utils/Logger.h"
#include "../Chat/ChatSDK.h"
#include "../Render/NotificationManager.h"
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace Urchin {
    struct CachedTags {
        PlayerTags data;
        std::chrono::steady_clock::time_point timestamp;
    };

    static std::unordered_map<std::string, CachedTags> g_cache;
    static std::mutex g_cacheMutex;
    static const size_t MAX_CACHE_SIZE = 200;
    static const int CACHE_EXPIRY_SECONDS = 300;

    static bool findJsonArray(const std::string& json, const char* key, size_t& start, size_t& end) {
        std::string pat = std::string("\"") + key + "\"";
        size_t k = json.find(pat);
        if (k == std::string::npos) return false;
        size_t arrStart = json.find('[', k);
        if (arrStart == std::string::npos) return false;
        int depth = 1;
        size_t i = arrStart + 1;
        while (i < json.size() && depth > 0) {
            if (json[i] == '[') depth++;
            else if (json[i] == ']') depth--;
            i++;
        }
        if (depth != 0) return false;
        start = arrStart;
        end = i;
        return true;
    }

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

    static std::vector<Tag> parseTags(const std::string& arrJson) {
        std::vector<Tag> tags;
        size_t pos = 0;
        while ((pos = arrJson.find('{', pos)) != std::string::npos) {
            size_t objEnd = arrJson.find('}', pos);
            if (objEnd == std::string::npos) break;
            std::string obj = arrJson.substr(pos, objEnd - pos + 1);
            Tag tag;
            findJsonString(obj, "type", tag.type);
            findJsonString(obj, "reason", tag.reason);
            if (!tag.type.empty()) {
                tags.push_back(tag);
            }
            pos = objEnd + 1;
        }
        return tags;
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
                if (it->second.timestamp < oldest->second.timestamp) {
                    oldest = it;
                }
            }
            g_cache.erase(oldest);
        }
    }

    static std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_pendingFetches;
    static std::mutex g_pendingMutex;

    std::optional<PlayerTags> getPlayerTags(const std::string& username) {
        if (!Config::isTagsEnabled()) return std::nullopt;
        if (Config::getActiveTagService() != "Urchin" && Config::getActiveTagService() != "Both") return std::nullopt;

        auto now = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(g_cacheMutex);
            auto it = g_cache.find(username);
            if (it != g_cache.end()) {
                auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp).count();
                if (age < CACHE_EXPIRY_SECONDS) {
                    Logger::log(Config::DebugCategory::Urchin, "--- Urchin Cache Hit: %s ---", username.c_str());
                    return it->second.data;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_pendingMutex);
            auto it = g_pendingFetches.find(username);
            if (it != g_pendingFetches.end()) {
                auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
                if (age < 10) return std::nullopt; 
            }
            g_pendingFetches[username] = now;
        }

        std::thread([username, now]() {
            std::string url = "https://urchin.ws/player/" + username + "?sources=MANUAL";
            std::string apiKey = Config::getUrchinApiKey();
            if (!apiKey.empty()) {
                url += "&key=" + apiKey;
            }

            std::string body;
            Logger::log(Config::DebugCategory::Urchin, "=== Urchin Fetching Started: %s ===", username.c_str());
            bool ok = Http::get(url, body);
            
            PlayerTags result;
            bool success = false;
            std::string failReason = "Unknown";

            if (ok && !body.empty() && body.find("\"error\"") == std::string::npos) {
                findJsonString(body, "uuid", result.uuid);
                size_t arrStart, arrEnd;
                if (findJsonArray(body, "tags", arrStart, arrEnd)) {
                    std::string arrJson = body.substr(arrStart, arrEnd - arrStart);
                    result.tags = parseTags(arrJson);
                }
                success = true;
            } else {
                if (!ok) failReason = "HTTP request failed";
                else if (body.empty()) failReason = "Empty response";
                else if (body.find("\"error\"") != std::string::npos) failReason = "API error response";
                else failReason = "JSON parse failed";
            }

            {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                pruneCacheLocked();
                g_cache[username] = { result, std::chrono::steady_clock::now() };
            }

            {
                std::lock_guard<std::mutex> lock(g_pendingMutex);
                g_pendingFetches.erase(username);
            }
            
            if (success) {
                Logger::log(Config::DebugCategory::Urchin, ">>> Urchin Success: %s Found %d tags <<<", username.c_str(), (int)result.tags.size());
                
                if (!result.tags.empty()) {
                    for (const auto& t : result.tags) {
                        std::string type = t.type;
                        for (auto& c : type) c = toupper(c);
                        if (type.find("BLATANT") != std::string::npos || type.find("SNIPER") != std::string::npos || type.find("CHEATER") != std::string::npos) {
                            std::string alert = ChatSDK::formatPrefix() + "\xC2\xA7" "cALERT: \xC2\xA7" "f" + username + " is tagged as \xC2\xA7" "l" + t.type + "\xC2\xA7" "r!";
                            ChatSDK::showClientMessage(alert);
                            Render::NotificationManager::getInstance()->add("Urchin Alert", username + " is a " + t.type, Render::NotificationType::Warning);
                            break;
                        }
                    }
                }
            } else {
                Logger::log(Config::DebugCategory::Urchin, "!!! Urchin Failed: %s - Reason: %s !!!", username.c_str(), failReason.c_str());
            }
        }).detach();

        return std::nullopt;
    }

    void clearCache() {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_cache.clear();
    }

    bool hasAnyTags(const std::string& username) {
        auto result = getPlayerTags(username);
        return result.has_value() && !result->tags.empty();
    }
}
