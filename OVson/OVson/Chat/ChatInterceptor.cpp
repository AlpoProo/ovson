#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include "ChatInterceptor.h"
#include "../Utils/BedwarsPrestiges.h"
#include "../Render/NotificationManager.h"
#include "Commands.h"
#include "ChatSDK.h"
#include "../Services/Hypixel.h"
#include "../Services/DiscordManager.h"
#include "../Config/Config.h"
#include "../Utils/Logger.h"
#include "../Java.h"
#include "../Logic/BedDefense/BedDefenseManager.h"
#include "../Logic/AutoGG.h"
#include "../Services/UrchinService.h"
#include "../Services/SeraphService.h"
#include "../Utils/ChatBypasser.h"
using namespace ChatInterceptor;
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <cctype>


// gosh so many messy messy messy
static bool g_initialized = false;
static std::string g_lastOnlineLine;
static std::vector<std::string> g_onlinePlayers;
static size_t g_nextFetchIdx = 0;
static std::unordered_map<std::string, std::string> g_pendingTabNames;
static bool g_lastInGameStatus = false;
static std::string g_lastDetectedModeName = "";
static std::string g_logsDir;
static std::string g_logFilePath;
static HANDLE g_logHandle = INVALID_HANDLE_VALUE;
static long long g_logOffset = 0;
static std::string g_logBuf;
static ULONGLONG g_lastImmediateTeamProbeTick = 0;
static int g_lobbyGraceTicks = 0;
static bool g_explicitLobbySignal = false;
static std::unordered_map<std::string, std::string> g_stableRankMap; // Map to freeze positions
static std::mutex g_stableRankMutex;
static std::string g_localTeam; // NEW: track local player's team
static std::string g_localName; // Track local player's name globally
std::unordered_map<std::string, std::string> ChatInterceptor::g_playerTeamColor;
static ULONGLONG g_lastTeamScanTick = 0;
static ULONGLONG g_lastChatReadTick = 0;
static ULONGLONG g_lastResetTick = 0;
static ULONGLONG g_lastDetectionLogTick = 0;
static int g_mode = 0; // 0 bedwars, 1 skywars, 2 duels
static ULONGLONG g_bootstrapStartTick = 0;
static std::unordered_map<std::string, int> g_teamProbeTries;
static std::unordered_set<std::string> g_processedPlayers;
// static std::queue<std::string> g_fetchQueue; // Removed
static std::unordered_set<std::string> g_queuedPlayers; // kept for logic tracking but might be redundant with activeFetches
static std::unordered_set<std::string> g_alertedPlayers;
static std::mutex g_alertedMutex;
static std::mutex g_queueMutex;
// static std::condition_variable g_queueCV; // Removed
// static std::vector<std::thread> g_workers; // Removed
// static std::atomic<bool> g_stopWorkers{false}; // Removed

static std::unordered_set<std::string> g_activeFetches;
static std::mutex g_activeFetchesMutex;
static bool g_inHypixelGame = false;
static std::unordered_map<std::string, Hypixel::PlayerStats> g_pendingStatsMap;
static std::mutex g_pendingStatsMutex;
static std::unordered_map<std::string, ULONGLONG> g_retryUntil;
static std::mutex g_retryMutex;
static std::unordered_map<std::string, int> g_playerFetchRetries;
std::unordered_map<std::string, Hypixel::PlayerStats> ChatInterceptor::g_playerStatsMap;
std::mutex ChatInterceptor::g_statsMutex;

struct CachedStats {
    Hypixel::PlayerStats stats;
    ULONGLONG timestamp = 0;
    CachedStats() : timestamp(0) {}
    CachedStats(const Hypixel::PlayerStats& s, ULONGLONG t) : stats(s), timestamp(t) {}
};
static std::unordered_map<std::string, CachedStats> g_persistentStatsCache;
static std::mutex g_cacheMutex;
static const size_t MAX_STATS_CACHE_SIZE = 500;
static const ULONGLONG STATS_CACHE_EXPIRY_MS = 600000;

static std::unordered_map<std::string, std::string> g_playerUuidMap;
static std::mutex g_uuidMapMutex;

static void pruneStatsCache() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    ULONGLONG now = GetTickCount64();
    
    for (auto it = g_persistentStatsCache.begin(); it != g_persistentStatsCache.end(); ) {
        if ((now - it->second.timestamp) > STATS_CACHE_EXPIRY_MS) {
            it = g_persistentStatsCache.erase(it);
        } else {
            ++it;
        }
    }
    
    while (g_persistentStatsCache.size() > MAX_STATS_CACHE_SIZE) {
        auto oldest = g_persistentStatsCache.begin();
        for (auto it = g_persistentStatsCache.begin(); it != g_persistentStatsCache.end(); ++it) {
            if (it->second.timestamp < oldest->second.timestamp) {
                oldest = it;
            }
        }
        g_persistentStatsCache.erase(oldest);
    }
}

static const char *mcColorForTeam(const std::string &team)
{
    if (team == "Red")
        return "\xC2\xA7"
               "c";
    if (team == "Blue")
        return "\xC2\xA7"
               "9";
    if (team == "Green")
        return "\xC2\xA7"
               "a";
    if (team == "Yellow")
        return "\xC2\xA7"
               "e";
    if (team == "Aqua")
        return "\xC2\xA7"
               "b";
    if (team == "White")
        return "\xC2\xA7"
               "f";
    if (team == "Pink")
        return "\xC2\xA7"
               "d";
    if (team == "Gray" || team == "Grey")
        return "\xC2\xA7"
               "8";
    return "\xC2\xA7"
           "8";
}

static void resetGameCache();
static void syncTeamColors();

struct JCache {
    jclass worldCls = nullptr;
    jmethodID m_getScoreboard = nullptr;

    jclass sbCls = nullptr;
    jmethodID m_getPlayersTeam = nullptr;
    jmethodID m_getObjectiveInDisplaySlot = nullptr;
    jmethodID m_getObjective = nullptr;
    jmethodID m_getValueFromObjective = nullptr;

    jclass teamCls = nullptr;
    jmethodID m_getPrefix = nullptr;

    jclass scoreCls = nullptr;
    jmethodID m_getScorePoints = nullptr;
    jmethodID m_setScorePoints = nullptr;

    jfieldID f_gpName = nullptr;

    std::atomic<bool> initialized{false};
    std::mutex initMutex;

    void init(JNIEnv* env) {
        if (initialized) return;
        std::lock_guard<std::mutex> lock(initMutex);
        if (initialized) return;
        
        if (!env) return;
        
        if (!worldCls) {
            jclass local = lc->GetClass("net.minecraft.client.multiplayer.WorldClient");
            if (local) worldCls = (jclass)env->NewGlobalRef(local);
        }
        if (worldCls) {
            m_getScoreboard = env->GetMethodID(worldCls, "getScoreboard", "()Lnet/minecraft/scoreboard/Scoreboard;");
            if (env->ExceptionCheck()) { env->ExceptionClear(); m_getScoreboard = env->GetMethodID(worldCls, "func_72883_A", "()Lnet/minecraft/scoreboard/Scoreboard;"); }
            if (env->ExceptionCheck()) env->ExceptionClear();
        }

        if (!sbCls) {
            jclass local = lc->GetClass("net.minecraft.scoreboard.Scoreboard");
            if (local) sbCls = (jclass)env->NewGlobalRef(local);
        }
        if (sbCls) {
            m_getPlayersTeam = env->GetMethodID(sbCls, "getPlayersTeam", "(Ljava/lang/String;)Lnet/minecraft/scoreboard/ScorePlayerTeam;");
            if (env->ExceptionCheck()) { env->ExceptionClear(); m_getPlayersTeam = env->GetMethodID(sbCls, "func_96509_i", "(Ljava/lang/String;)Lnet/minecraft/scoreboard/ScorePlayerTeam;"); }
            if (env->ExceptionCheck()) env->ExceptionClear();

            m_getObjectiveInDisplaySlot = env->GetMethodID(sbCls, "getObjectiveInDisplaySlot", "(I)Lnet/minecraft/scoreboard/ScoreObjective;");
            if (env->ExceptionCheck()) { env->ExceptionClear(); m_getObjectiveInDisplaySlot = env->GetMethodID(sbCls, "func_96539_a", "(I)Lnet/minecraft/scoreboard/ScoreObjective;"); }
            if (env->ExceptionCheck()) env->ExceptionClear();

            m_getObjective = env->GetMethodID(sbCls, "getObjective", "(Ljava/lang/String;)Lnet/minecraft/scoreboard/ScoreObjective;");
            if (env->ExceptionCheck()) { env->ExceptionClear(); m_getObjective = env->GetMethodID(sbCls, "func_96518_b", "(Ljava/lang/String;)Lnet/minecraft/scoreboard/ScoreObjective;"); }
            if (env->ExceptionCheck()) env->ExceptionClear();

            m_getValueFromObjective = env->GetMethodID(sbCls, "getValueFromObjective", "(Ljava/lang/String;Lnet/minecraft/scoreboard/ScoreObjective;)Lnet/minecraft/scoreboard/Score;");
            if (env->ExceptionCheck()) { env->ExceptionClear(); m_getValueFromObjective = env->GetMethodID(sbCls, "func_96529_a", "(Ljava/lang/String;Lnet/minecraft/scoreboard/ScoreObjective;)Lnet/minecraft/scoreboard/Score;"); }
            if (env->ExceptionCheck()) env->ExceptionClear();
        }

        if (!teamCls) {
            jclass local = lc->GetClass("net.minecraft.scoreboard.ScorePlayerTeam");
            if (local) teamCls = (jclass)env->NewGlobalRef(local);
        }
        if (teamCls) {
            m_getPrefix = env->GetMethodID(teamCls, "getPrefix", "()Ljava/lang/String;");
            if (env->ExceptionCheck()) { env->ExceptionClear(); m_getPrefix = env->GetMethodID(teamCls, "func_96668_e", "()Ljava/lang/String;"); }
            if (env->ExceptionCheck()) env->ExceptionClear();
        }

        if (!scoreCls) {
            jclass local = lc->GetClass("net.minecraft.scoreboard.Score");
            if (local) scoreCls = (jclass)env->NewGlobalRef(local);
        }
        if (scoreCls) {
            m_getScorePoints = env->GetMethodID(scoreCls, "getScorePoints", "()I");
            if (env->ExceptionCheck()) { env->ExceptionClear(); m_getScorePoints = env->GetMethodID(scoreCls, "func_96652_c", "()I"); }
            if (env->ExceptionCheck()) env->ExceptionClear();

            m_setScorePoints = env->GetMethodID(scoreCls, "setScorePoints", "(I)V");
            if (env->ExceptionCheck()) { env->ExceptionClear(); m_setScorePoints = env->GetMethodID(scoreCls, "func_96647_c", "(I)V"); }
            if (env->ExceptionCheck()) env->ExceptionClear();
        }

        if (!f_gpName) {
            jclass gpCls = lc->GetClass("com.mojang.authlib.GameProfile");
            if (gpCls) {
                f_gpName = env->GetFieldID(gpCls, "name", "Ljava/lang/String;");
                if (env->ExceptionCheck()) { env->ExceptionClear(); f_gpName = env->GetFieldID(gpCls, "field_109761_d", "Ljava/lang/String;"); }
                if (env->ExceptionCheck()) env->ExceptionClear();
            }
        }
        
        initialized = true;
    }

    void cleanup(JNIEnv* env) {
        std::lock_guard<std::mutex> lock(initMutex);
        if (!initialized) return;
        if (env) {
            if (worldCls) env->DeleteGlobalRef(worldCls);
            if (sbCls) env->DeleteGlobalRef(sbCls);
            if (teamCls) env->DeleteGlobalRef(teamCls);
            if (scoreCls) env->DeleteGlobalRef(scoreCls);
        }
        worldCls = nullptr;
        sbCls = nullptr;
        teamCls = nullptr;
        scoreCls = nullptr;
        f_gpName = nullptr;
        initialized = false;
    }
};

static JCache g_jCache;

static std::string resolveTeamForNameEx(JNIEnv* env, const std::string& name, jobject scoreboard, jmethodID m_getPlayersTeam, jclass teamCls, jmethodID m_getPrefix);

static const char *colorForFkdr(double fkdr)
{
    if (fkdr < 1.0)
        return "\xC2\xA7"
               "7";
    if (fkdr < 2.0)
        return "\xC2\xA7"
               "f";
    if (fkdr < 3.0)
        return "\xC2\xA7"
               "6";
    if (fkdr < 4.0)
        return "\xC2\xA7"
               "b";
    if (fkdr < 5.0)
        return "\xC2\xA7"
               "a";
    if (fkdr < 6.0)
        return "\xC2\xA7"
               "5";
    return "\xC2\xA7"
           "4";
}
// im gonna change these colors when im available
static const char *colorForWlr(double wlr)
{
    if (wlr < 1.0)
        return "\xC2\xA7"
               "f";
    if (wlr < 3.0)
        return "\xC2\xA7"
               "6";
    if (wlr < 5.0)
        return "\xC2\xA7"
               "4";
    return "\xC2\xA7"
           "4";
}

static const char *colorForWins(int wins)
{
    if (wins < 500)
        return "\xC2\xA7"
               "7";
    if (wins < 1000)
        return "\xC2\xA7"
               "f";
    if (wins < 2000)
        return "\xC2\xA7"
               "e";
    if (wins < 4000)
        return "\xC2\xA7"
               "4";
    return "\xC2\xA7"
           "4";
}

static const char *colorForFinalKills(int fk)
{
    if (fk < 1000)
        return "\xC2\xA7"
               "7";
    if (fk < 2000)
        return "\xC2\xA7"
               "f";
    if (fk < 4000)
        return "\xC2\xA7"
               "6";
    if (fk < 5000)
        return "\xC2\xA7"
               "b";
    if (fk < 10000)
        return "\xC2\xA7"
               "4";
    return "\xC2\xA7"
           "4";
}

static const char *colorForStar(int star)
{
    if (star < 100)
        return "\xC2\xA7"
               "7";
    if (star < 200)
        return "\xC2\xA7"
               "f";
    if (star < 300)
        return "\xC2\xA7"
               "6";
    if (star < 400)
        return "\xC2\xA7"
               "b";
    if (star < 500)
        return "\xC2\xA7"
               "a";
    if (star < 600)
        return "\xC2\xA7"
               "b";
    return "\xC2\xA7"
           "4";
}

static const char *teamInitial(const std::string &team)
{
    if (team == "Red")
        return "R";
    if (team == "Blue")
        return "B";
    if (team == "Green")
        return "G";
    if (team == "Yellow")
        return "Y";
    if (team == "Aqua")
        return "A";
    if (team == "White")
        return "W";
    if (team == "Pink")
        return "P";
    if (team == "Gray" || team == "Grey") // haha grey
        return "G";
    return "?";
}

static void detectTeamsFromLine(const std::string &chat)
{
    static const char *teams[] = {"Red", "Blue", "Green", "Yellow", "Aqua", "White", "Pink", "Gray", "Grey"};
    for (const char *t : teams)
    {
        std::string needle1 = std::string("You are on the ") + t + " Team!";
        if (chat.find(needle1) != std::string::npos)
        {
            Logger::info("Local team detected: %s", t);
            g_localTeam = t;
            if (!g_localName.empty() && !g_localTeam.empty()) {
                g_playerTeamColor[g_localName] = g_localTeam;
            }
        }
        std::string needle2 = std::string(" joined (") + t + ")";
        auto p2 = chat.find(needle2);
        if (p2 != std::string::npos)
        {
            auto s = chat.rfind(' ', p2);
            std::string name = (s == std::string::npos) ? std::string() : chat.substr(0, s);
            auto sp = name.find_last_of(' ');
            if (sp != std::string::npos)
                name = name.substr(sp + 1);
            if (!name.empty())
            {
                g_playerTeamColor[name] = t;
                Logger::info("Team detected: %s -> %s", name.c_str(), t);
            }
        }
    }
}

static const char *teamFromColorCode(char code)
{
    switch (code)
    {
    case 'c':
    case '4':
        return "Red";
    case '9':
    case '1':
        return "Blue";
    case 'a':
    case '2':
        return "Green";
    case 'e':
        return "Yellow";
    case 'b':
        return "Aqua";
    case 'f':
        return "White";
    case 'd':
        return "Pink";
    case '8':
        return "Gray";
    default:
        return "";
    }
}

static void detectFinalKillsFromLine(const std::string &chat)
{
    if (chat.find(" FINAL KILL!") == std::string::npos)
        return;


    // "gamerboy80 was killed by sekerbenimkedim. FINAL KILL!"
    
    std::string clean = chat;
    while (!clean.empty() && isspace(clean[0])) clean.erase(0, 1);
    if (clean.empty()) return;

    std::string victim;
    if (clean[0] == '[')
    {
        size_t firstSpace = clean.find(' ');
        if (firstSpace != std::string::npos)
        {
            std::string afterRank = clean.substr(firstSpace + 1);
            size_t endOfName = afterRank.find(' ');
            if (endOfName != std::string::npos)
            {
                victim = afterRank.substr(0, endOfName);
            }
        }
    }
    else
    {
        size_t firstSpace = clean.find(' ');
        if (firstSpace != std::string::npos)
        {
            victim = clean.substr(0, firstSpace);
        }
    }

    if (!victim.empty())
    {
        std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
        if (ChatInterceptor::g_playerStatsMap.find(victim) != ChatInterceptor::g_playerStatsMap.end())
        {
            ChatInterceptor::g_playerStatsMap.erase(victim);
            Logger::info("Player removed from GUI due to FINAL KILL: %s", victim.c_str());
        }
    }
}

static void updateTeamsFromScoreboard()
{
    JNIEnv *env = lc->getEnv();
    if (!env)
        return;
    jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
    if (!mcCls)
        return;
    jmethodID m_getMc = env->GetStaticMethodID(mcCls, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
    jfieldID theMc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
    jobject mcObj = nullptr;
    if (m_getMc)
        mcObj = env->CallStaticObjectMethod(mcCls, m_getMc);
    if (!mcObj && theMc)
        mcObj = env->GetStaticObjectField(mcCls, theMc);
    if (!mcObj)
        return;
    jfieldID f_world = env->GetFieldID(mcCls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
    if (!f_world)
    {
        env->DeleteLocalRef(mcObj);
        return;
    }
    jobject world = env->GetObjectField(mcObj, f_world);
    if (!world)
    {
        env->DeleteLocalRef(mcObj);
        return;
    }
    jclass worldCls = lc->GetClass("net.minecraft.client.multiplayer.WorldClient");
    if (!worldCls)
    {
        env->DeleteLocalRef(world);
        env->DeleteLocalRef(mcObj);
        return;
    }
    jmethodID m_getScoreboard = env->GetMethodID(worldCls, "getScoreboard", "()Lnet/minecraft/scoreboard/Scoreboard;");
    if (!m_getScoreboard) m_getScoreboard = env->GetMethodID(worldCls, "func_96441_U", "()Lnet/minecraft/scoreboard/Scoreboard;");
    
    if (!m_getScoreboard)
    {
        env->DeleteLocalRef(world);
        env->DeleteLocalRef(mcObj);
        return;
    }
    jobject scoreboard = env->CallObjectMethod(world, m_getScoreboard);
    if (!scoreboard)
    {
        env->DeleteLocalRef(world);
        env->DeleteLocalRef(mcObj);
        return;
    }
    jclass sbCls = lc->GetClass("net.minecraft.scoreboard.Scoreboard");
    if (!sbCls)
    {
        env->DeleteLocalRef(scoreboard);
        env->DeleteLocalRef(world);
        env->DeleteLocalRef(mcObj);
        return;
    }
    jmethodID m_getPlayersTeam = env->GetMethodID(sbCls, "getPlayersTeam", "(Ljava/lang/String;)Lnet/minecraft/scoreboard/ScorePlayerTeam;");
    if (!m_getPlayersTeam) m_getPlayersTeam = env->GetMethodID(sbCls, "func_96509_i", "(Ljava/lang/String;)Lnet/minecraft/scoreboard/ScorePlayerTeam;");
    
    if (!m_getPlayersTeam)
    {
        env->DeleteLocalRef(scoreboard);
        env->DeleteLocalRef(world);
        env->DeleteLocalRef(mcObj);
        return;
    }
    jclass teamCls = lc->GetClass("net.minecraft.scoreboard.ScorePlayerTeam");
    jmethodID m_getColorPrefixStatic = teamCls ? env->GetStaticMethodID(teamCls, "getColorPrefix", "(Lnet/minecraft/scoreboard/ScorePlayerTeam;)Ljava/lang/String;") : nullptr;
    jmethodID m_getColorPrefixInst = teamCls ? env->GetMethodID(teamCls, "getColorPrefix", "()Ljava/lang/String;") : nullptr;
    jmethodID m_getPrefix = teamCls ? env->GetMethodID(teamCls, "getPrefix", "()Ljava/lang/String;") : nullptr;
    jmethodID m_getColorPrefixSrg = teamCls ? env->GetMethodID(teamCls, "func_96668_e", "()Ljava/lang/String;") : nullptr;

    for (const std::string &name : g_onlinePlayers)
    {
        jstring jn = env->NewStringUTF(name.c_str());
        jobject team = env->CallObjectMethod(scoreboard, m_getPlayersTeam, jn);
        const char *tstr = "";
        if (team)
        {
            jstring pref = nullptr;
            if (m_getColorPrefixStatic)
                pref = (jstring)env->CallStaticObjectMethod(teamCls, m_getColorPrefixStatic, team);
            if (!pref && m_getColorPrefixInst)
                pref = (jstring)env->CallObjectMethod(team, m_getColorPrefixInst);
            if (!pref && m_getPrefix)
                pref = (jstring)env->CallObjectMethod(team, m_getPrefix);
            if (!pref && m_getColorPrefixSrg)
                pref = (jstring)env->CallObjectMethod(team, m_getColorPrefixSrg);
            if (pref)
            {
                const char *utf = env->GetStringUTFChars(pref, 0);
                if (utf)
                {
                    const char *sect = strchr(utf, '\xC2');
                    char code = 0;
                    const char *raw = strchr(utf, '\xA7');
                    if (raw && raw[1])
                        code = raw[1];
                    if (!code && sect)
                    {
                        const unsigned char *u = (const unsigned char *)utf;
                        for (size_t i = 0; u[i]; ++i)
                        {
                            if (u[i] == 0xC2 && u[i + 1] == 0xA7 && u[i + 2])
                            {
                                code = (char)u[i + 2];
                                break;
                            }
                        }
                    }
                    if (code)
                    {
                        const char *tname = teamFromColorCode(code);
                        if (tname && *tname)
                            g_playerTeamColor[name] = tname;
                    }
                    env->ReleaseStringUTFChars(pref, utf);
                }
                env->DeleteLocalRef(pref);
            }
            env->DeleteLocalRef(team);
        }
        env->DeleteLocalRef(jn);
    }
    env->DeleteLocalRef(scoreboard);
    env->DeleteLocalRef(world);
    env->DeleteLocalRef(mcObj);
}

static std::string resolveTeamForNameEx(JNIEnv* env, const std::string& name, jobject scoreboard, jmethodID m_getPlayersTeam, jclass teamCls, jmethodID m_getPrefix)
{
    if (!env || !scoreboard || !m_getPlayersTeam || !teamCls || !m_getPrefix) return std::string();

    jstring jname = env->NewStringUTF(name.c_str());
    jobject teamObj = env->CallObjectMethod(scoreboard, m_getPlayersTeam, jname);
    env->ExceptionClear();
    env->DeleteLocalRef(jname);

    std::string result;
    if (teamObj)
    {
        jstring pref = (jstring)env->CallObjectMethod(teamObj, m_getPrefix);
        env->ExceptionClear();
        if (pref)
        {
            const unsigned char *u = (const unsigned char *)env->GetStringUTFChars(pref, 0);
            char code = 0;
            if (u)
            {
                for (size_t i = 0; u[i]; ++i)
                {
                    if (u[i] == 0xC2 && u[i + 1] == 0xA7 && u[i + 2])
                    {
                        code = (char)u[i + 2];
                        break;
                    }
                }
                env->ReleaseStringUTFChars(pref, (const char *)u);
            }
            if (code)
            {
                const char *tname = teamFromColorCode(code);
                if (tname && *tname) result = tname;
            }
            env->DeleteLocalRef(pref);
        }
        env->DeleteLocalRef(teamObj);
    }
    return result;
}

static std::string resolveTeamForName(const std::string &name)
{
    JNIEnv *env = lc->getEnv();
    if (!g_initialized || !env) return std::string();

    if (!g_localName.empty() && name == g_localName) {
        if (!g_localTeam.empty()) return g_localTeam;
    }

    {
        std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
        auto itT = g_playerTeamColor.find(name);
        if (itT != g_playerTeamColor.end() && !itT->second.empty()) {
            return itT->second;
        }
    }

    g_jCache.init(env);
    
    jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
    if (!mcCls) return std::string();
    jfieldID theMc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
    jobject mcObj = theMc ? env->GetStaticObjectField(mcCls, theMc) : nullptr;
    if (!mcObj) return std::string();

    jfieldID f_world = env->GetFieldID(mcCls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
    if (!f_world) f_world = env->GetFieldID(mcCls, "field_71441_e", "Lnet/minecraft/client/multiplayer/WorldClient;");
    jobject world = f_world ? env->GetObjectField(mcObj, f_world) : nullptr;
    
    std::string result;
    if (world)
    {
        jmethodID m_getScoreboard = g_jCache.m_getScoreboard;
        jobject scoreboard = m_getScoreboard ? env->CallObjectMethod(world, m_getScoreboard) : nullptr;
        env->ExceptionClear();
        if (scoreboard)
        {
            result = resolveTeamForNameEx(env, name, scoreboard, g_jCache.m_getPlayersTeam, g_jCache.teamCls, g_jCache.m_getPrefix);
            env->DeleteLocalRef(scoreboard);
        }
        env->DeleteLocalRef(world);
    }
    env->DeleteLocalRef(mcObj);
    return result;
}


static void updateTabListStats()
{
    JNIEnv *env = lc->getEnv();
    if (!g_initialized || !env) return;

    jobject iter = nullptr;

    static ULONGLONG lastUpdate = 0;
    ULONGLONG now = GetTickCount64();

    static bool s_wasTabEnabled = false;
    bool isTabEnabled = Config::isTabEnabled();
    bool forceReset = s_wasTabEnabled && !isTabEnabled;
    s_wasTabEnabled = isTabEnabled;

    bool doTabUpdate = (isTabEnabled && (now - lastUpdate >= 150)) || forceReset;
    if (doTabUpdate && isTabEnabled) lastUpdate = now;

    if (!env) return;
    jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
    if (!mcCls) return;
    jfieldID theMc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
    jobject mcObj = theMc ? env->GetStaticObjectField(mcCls, theMc) : nullptr;
    if (!mcObj) return;

    jmethodID m_getNet = env->GetMethodID(mcCls, "getNetHandler", "()Lnet/minecraft/client/network/NetHandlerPlayClient;");
    jobject nh = m_getNet ? env->CallObjectMethod(mcObj, m_getNet) : nullptr;
    if (!nh) { env->DeleteLocalRef(mcObj); return; }

    jclass nhCls = lc->GetClass("net.minecraft.client.network.NetHandlerPlayClient");
    jmethodID m_getMap = nhCls ? env->GetMethodID(nhCls, "getPlayerInfoMap", "()Ljava/util/Collection;") : nullptr;
    jobject col = m_getMap ? env->CallObjectMethod(nh, m_getMap) : nullptr;
    if (!col) { env->DeleteLocalRef(nh); env->DeleteLocalRef(mcObj); return; }

    jclass localCollCls = env->FindClass("java/util/Collection");
    jmethodID m_size = localCollCls ? env->GetMethodID(localCollCls, "size", "()I") : nullptr;
    int playerCount = m_size ? env->CallIntMethod(col, m_size) : 0;

    bool appearsToBeLobby = true;
    bool hasStrictGameKeywords = false;
    std::string detectedServer = "unknown";
    
    jfieldID f_currServer = env->GetFieldID(mcCls, "currentServerData", "Lnet/minecraft/client/multiplayer/ServerData;");
    if (!f_currServer) f_currServer = env->GetFieldID(mcCls, "field_71422_O", "Lnet/minecraft/client/multiplayer/ServerData;");
    
    jobject serverData = f_currServer ? env->GetObjectField(mcObj, f_currServer) : nullptr;
    if (serverData) {
        jclass serverDataCls = lc->GetClass("net.minecraft.client.multiplayer.ServerData");
        jfieldID f_serverMOTD = serverDataCls ? env->GetFieldID(serverDataCls, "serverMOTD", "Ljava/lang/String;") : nullptr;
        if (!f_serverMOTD) f_serverMOTD = env->GetFieldID(serverDataCls, "field_78847_f", "Ljava/lang/String;");
        
        jstring motd = f_serverMOTD ? (jstring)env->GetObjectField(serverData, f_serverMOTD) : nullptr;
        if (motd) {
            const char* motdUtf = env->GetStringUTFChars(motd, 0);
            if (motdUtf) {
                std::string motdStr = motdUtf;
                if (Config::isDebugging() && (now - g_lastDetectionLogTick >= 10000)) {
                    if (motdStr.find("Portal") != std::string::npos || motdStr.find("Lobby") != std::string::npos) {
                        if (g_lastDetectedModeName != "LOBBY (MOTD)") {
                            ChatSDK::showPrefixed("\xC2\xA7\x65[DEBUG] MOTD: LOBBY detected");
                            g_lastDetectedModeName = "LOBBY (MOTD)";
                            g_lastDetectionLogTick = now;
                        }
                    } else {
                        if (g_lastDetectedModeName != "GAME (MOTD)") {
                            ChatSDK::showPrefixed("\xC2\xA7\x65[DEBUG] MOTD: GAME detected");
                            g_lastDetectedModeName = "GAME (MOTD)";
                            g_lastDetectionLogTick = now;
                        }
                    }
                }
                if (motdStr.find("Portal") != std::string::npos || motdStr.find("Lobby") != std::string::npos) {
                    appearsToBeLobby = true;
                    g_explicitLobbySignal = true;
                }
                env->ReleaseStringUTFChars(motd, motdUtf);
            }
            env->DeleteLocalRef(motd);
        }
        env->DeleteLocalRef(serverData);
    }
    
    jfieldID f_gui = env->GetFieldID(mcCls, "ingameGUI", "Lnet/minecraft/client/gui/GuiIngame;");
    if (!f_gui) f_gui = env->GetFieldID(mcCls, "field_71456_v", "Lnet/minecraft/client/gui/GuiIngame;");
    jobject gui = f_gui ? env->GetObjectField(mcObj, f_gui) : nullptr;
    if (gui) {
        jclass guiCls = lc->GetClass("net.minecraft.client.gui.GuiIngame");
        jfieldID f_tab = env->GetFieldID(guiCls, "overlayPlayerList", "Lnet/minecraft/client/gui/GuiPlayerTabOverlay;");
        if (!f_tab) f_tab = env->GetFieldID(guiCls, "field_175181_C", "Lnet/minecraft/client/gui/GuiPlayerTabOverlay;");
        jobject tab = f_tab ? env->GetObjectField(gui, f_tab) : nullptr;
        if (tab) {
            jclass tabCls = lc->GetClass("net.minecraft.client.gui.GuiPlayerTabOverlay");
            jfieldID f_footer = env->GetFieldID(tabCls, "footer", "Lnet/minecraft/util/IChatComponent;");
            if (!f_footer) f_footer = env->GetFieldID(tabCls, "field_175245_I", "Lnet/minecraft/util/IChatComponent;");
            jobject footerComp = f_footer ? env->GetObjectField(tab, f_footer) : nullptr;
            if (footerComp) {
                jclass compCls = lc->GetClass("net.minecraft.util.IChatComponent");
                jmethodID m_getUnf = compCls ? env->GetMethodID(compCls, "getUnformattedText", "()Ljava/lang/String;") : nullptr;
                if (!m_getUnf) m_getUnf = env->GetMethodID(compCls, "func_150260_c", "()Ljava/lang/String;");
                jstring footerJ = m_getUnf ? (jstring)env->CallObjectMethod(footerComp, m_getUnf) : nullptr;
                if (footerJ) {
                    const char* utf = env->GetStringUTFChars(footerJ, 0);
                    if (utf) {
                        std::string footerStr = utf;
                        if (g_lastDetectedModeName != "FOOTER") {
                            Logger::log(Config::DebugCategory::GameDetection, "Raw Footer: %s", footerStr.c_str());
                            g_lastDetectedModeName = "FOOTER";
                        }
                        
                        if (footerStr.find("Final Kills:") != std::string::npos && 
                            footerStr.find("Beds Broken:") != std::string::npos &&
                            footerStr.find("Kills:") != std::string::npos) {
                            hasStrictGameKeywords = true;
                        }

                        size_t srvPos = footerStr.find("Server: ");
                        if (srvPos != std::string::npos) {
                            std::string srv = footerStr.substr(srvPos + 8);
                            size_t space = srv.find_first_of(" \n\r");
                            if (space != std::string::npos) srv = srv.substr(0, space);
                            detectedServer = srv;
                        }
                        env->ReleaseStringUTFChars(footerJ, utf);
                    }
                    env->DeleteLocalRef(footerJ);
                }
                env->DeleteLocalRef(footerComp);
            }
            env->DeleteLocalRef(tab);
        }
        env->DeleteLocalRef(gui);
    }

    jfieldID f_world = env->GetFieldID(mcCls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");

    jobject world = f_world ? env->GetObjectField(mcObj, f_world) : nullptr;
    if (world) {
        jclass worldCls = lc->GetClass("net.minecraft.client.multiplayer.WorldClient");
        jmethodID m_getSB = worldCls ? env->GetMethodID(worldCls, "getScoreboard", "()Lnet/minecraft/scoreboard/Scoreboard;") : nullptr;
        jobject sb = m_getSB ? env->CallObjectMethod(world, m_getSB) : nullptr;
        if (sb) {
            jclass sbCls = lc->GetClass("net.minecraft.scoreboard.Scoreboard");
            jmethodID m_getObj = sbCls ? env->GetMethodID(sbCls, "getObjectiveInDisplaySlot", "(I)Lnet/minecraft/scoreboard/ScoreObjective;") : nullptr;
            if (!m_getObj) m_getObj = env->GetMethodID(sbCls, "func_96539_a", "(I)Lnet/minecraft/scoreboard/ScoreObjective;");
            
            jobject obj = m_getObj ? env->CallObjectMethod(sb, m_getObj, 1) : nullptr;
            if (obj) {
                jclass objCls = lc->GetClass("net.minecraft.scoreboard.ScoreObjective");
                jmethodID m_getDisp = objCls ? env->GetMethodID(objCls, "getDisplayName", "()Ljava/lang/String;") : nullptr;
                if (!m_getDisp) m_getDisp = env->GetMethodID(objCls, "func_96678_d", "()Ljava/lang/String;");
                jstring dispJ = m_getDisp ? (jstring)env->CallObjectMethod(obj, m_getDisp) : nullptr;
                if (dispJ) {
                    const char* utf = env->GetStringUTFChars(dispJ, 0);
                    if (utf) {
                        std::string sbTitle = utf;
                        if (now - g_lastDetectionLogTick >= 10000) {
                            if (g_lastDetectedModeName != "SCOREBOARD") {
                                Logger::log(Config::DebugCategory::GameDetection, "Raw Scoreboard: %s", sbTitle.c_str());
                                g_lastDetectedModeName = "SCOREBOARD";
                                g_lastDetectionLogTick = now;
                            }
                        }
                        std::string sbClean;
                        for (size_t i = 0; i < sbTitle.length(); ++i) {
                            if ((unsigned char)sbTitle[i] == 0xC2 && i + 1 < sbTitle.length() && (unsigned char)sbTitle[i+1] == 0xA7) { 
                                i += 2;
                                continue; 
                            }
                            if ((unsigned char)sbTitle[i] == 0xA7) {
                                i += 1;
                                continue;
                            }
                            sbClean += (char)toupper(sbTitle[i]);
                        }
                        
                        if (sbClean.find("BED WARS") != std::string::npos || sbClean.find("SKYWARS") != std::string::npos ||
                            sbClean.find("DUELS") != std::string::npos || sbClean.find("WARS") != std::string::npos ||
                            sbClean.find("THE BRIDGE") != std::string::npos || sbClean.find("TNT") != std::string::npos ||
                            sbClean.find("MURDER") != std::string::npos || sbClean.find("GAMES") != std::string::npos) {
                            if (hasStrictGameKeywords) {
                                appearsToBeLobby = false;
                                if ((now - g_lastDetectionLogTick >= 10000) && g_lastDetectedModeName != "GAME (Scoreboard + Footer)") {
                                    Logger::log(Config::DebugCategory::GameDetection, "State: GAME (Scoreboard + Footer match)");
                                    g_lastDetectedModeName = "GAME (Scoreboard + Footer)";
                                    g_lastDetectionLogTick = now;
                                }
                            } else {
                                appearsToBeLobby = true;
                                if ((now - g_lastDetectionLogTick >= 10000) && g_lastDetectedModeName != "LOBBY (Scoreboard, Footer missing)") {
                                    Logger::log(Config::DebugCategory::GameDetection, "State: LOBBY (Scoreboard found, but Footer keywords missing)");
                                    g_lastDetectedModeName = "LOBBY (Scoreboard, Footer missing)";
                                    g_lastDetectionLogTick = now;
                                }
                            }
                        } else if (sbClean == "HYPIXEL" || sbClean.find("LOBBY") != std::string::npos) {
                            appearsToBeLobby = true;
                            if ((now - g_lastDetectionLogTick >= 10000) && g_lastDetectedModeName != "LOBBY (Scoreboard)") {
                                Logger::log(Config::DebugCategory::GameDetection, "Mode: %s (LOBBY)", sbClean.c_str());
                                g_lastDetectedModeName = "LOBBY (Scoreboard)";
                                g_lastDetectionLogTick = now;
                            }
                        } else {
                            if ((now - g_lastDetectionLogTick >= 10000) && g_lastDetectedModeName != "UNKNOWN (Scoreboard)") {
                                Logger::log(Config::DebugCategory::GameDetection, "Mode: %s (UNKNOWN)", sbClean.c_str());
                                g_lastDetectedModeName = "UNKNOWN (Scoreboard)";
                                g_lastDetectionLogTick = now;
                            }
                        }
                        env->ReleaseStringUTFChars(dispJ, utf);
                    }
                    env->DeleteLocalRef(dispJ);
                }
                env->DeleteLocalRef(obj);
            }
            env->DeleteLocalRef(sb);
        }
        env->DeleteLocalRef(world);
    }

    if (detectedServer != "unknown") {
        std::string srvLower = detectedServer;
        std::transform(srvLower.begin(), srvLower.end(), srvLower.begin(), ::tolower);
        if (srvLower.find("lobby") != std::string::npos || srvLower.find("mega") != std::string::npos) {
            appearsToBeLobby = true;
            if ((now - g_lastDetectionLogTick >= 10000) && g_lastDetectedModeName != "LOBBY (Server ID)") {
                Logger::log(Config::DebugCategory::GameDetection, "Server: %s (LOBBY)", detectedServer.c_str());
                g_lastDetectedModeName = "LOBBY (Server ID)";
                g_lastDetectionLogTick = now;
            }
        } else if (srvLower.find("mini") != std::string::npos || srvLower.find("bed") != std::string::npos) {
            appearsToBeLobby = false;
            if ((now - g_lastDetectionLogTick >= 10000) && g_lastDetectedModeName != "GAME (Server ID)") {
                Logger::log(Config::DebugCategory::GameDetection, "Server: %s (GAME)", detectedServer.c_str());
                g_lastDetectedModeName = "GAME (Server ID)";
                g_lastDetectionLogTick = now;
            }
        } else {
            if ((now - g_lastDetectionLogTick >= 10000) && g_lastDetectedModeName != "UNKNOWN (Server ID)") {
                Logger::log(Config::DebugCategory::GameDetection, "Server: %s (UNKNOWN)", detectedServer.c_str());
                g_lastDetectedModeName = "UNKNOWN (Server ID)";
                g_lastDetectionLogTick = now;
            }
        }
    }
    
    bool detectedLobby = appearsToBeLobby;
    if (detectedLobby) {
        g_lobbyGraceTicks++;
    } else {
        g_lobbyGraceTicks = 0;
    }

    bool shouldBeInGame = g_inHypixelGame;
    if (g_lobbyGraceTicks >= 40 || g_explicitLobbySignal) {
        shouldBeInGame = false;
        g_explicitLobbySignal = false;
    } else if (g_lobbyGraceTicks == 0) {
        shouldBeInGame = true;
    }

    if (shouldBeInGame != g_inHypixelGame)
    {
        g_inHypixelGame = shouldBeInGame;
        if (g_inHypixelGame)
        {
            Logger::log(Config::DebugCategory::GameDetection, "Detected Hypixel GAME session (Confirmed)");
            if (Config::isGlobalDebugEnabled()) {
                 Render::NotificationManager::getInstance()->add("System", "Game Session Detected", Render::NotificationType::Success);
            }
            syncTeamColors();
        }
        else
        {
            Logger::log(Config::DebugCategory::GameDetection, "Detected Hypixel LOBBY session (Confirmed)");
            if (Config::isGlobalDebugEnabled()) {
                 Render::NotificationManager::getInstance()->add("System", "Lobby Session Detected", Render::NotificationType::Warning);
            }
            resetGameCache();
        }
    }

    static jclass iterCls = nullptr, npiCls = nullptr, profCls = nullptr, uuidCls = nullptr, cctCls = nullptr, collCls = nullptr;
    static jmethodID m_iter = nullptr, m_has = nullptr, m_next = nullptr, m_setDisp = nullptr;
    static jmethodID m_getProf = nullptr, m_getName = nullptr, m_getId = nullptr, m_uuidToString = nullptr, cctInit = nullptr;
    static jfieldID f_gpName = nullptr;

    if (!iterCls) {
        collCls = (jclass)env->NewGlobalRef(lc->GetClass("java.util.Collection"));
        iterCls = (jclass)env->NewGlobalRef(lc->GetClass("java.util.Iterator"));
        npiCls = (jclass)env->NewGlobalRef(lc->GetClass("net.minecraft.client.network.NetworkPlayerInfo"));
        profCls = (jclass)env->NewGlobalRef(lc->GetClass("com.mojang.authlib.GameProfile"));
        uuidCls = (jclass)env->NewGlobalRef(lc->GetClass("java.util.UUID"));
        cctCls = (jclass)env->NewGlobalRef(lc->GetClass("net.minecraft.util.ChatComponentText"));

        m_iter = env->GetMethodID(collCls, "iterator", "()Ljava/util/Iterator;");
        m_has = env->GetMethodID(iterCls, "hasNext", "()Z");
        m_next = env->GetMethodID(iterCls, "next", "()Ljava/lang/Object;");

        m_setDisp = env->GetMethodID(npiCls, "setDisplayName", "(Lnet/minecraft/util/IChatComponent;)V");
        if (!m_setDisp) m_setDisp = env->GetMethodID(npiCls, "func_178859_a", "(Lnet/minecraft/util/IChatComponent;)V");
        m_getProf = env->GetMethodID(npiCls, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;");
        if (!m_getProf) m_getProf = env->GetMethodID(npiCls, "func_178845_a", "()Lcom/mojang/authlib/GameProfile;");

        m_getName = env->GetMethodID(profCls, "getName", "()Ljava/lang/String;");
        m_getId = env->GetMethodID(profCls, "getId", "()Ljava/util/UUID;");
        f_gpName = env->GetFieldID(profCls, "name", "Ljava/lang/String;");

        m_uuidToString = env->GetMethodID(uuidCls, "toString", "()Ljava/lang/String;");
        cctInit = env->GetMethodID(cctCls, "<init>", "(Ljava/lang/String;)V");
    }

    if (!iterCls || !m_iter || !m_has || !m_next) { env->DeleteLocalRef(col); env->DeleteLocalRef(nh); env->DeleteLocalRef(mcObj); return; }

    f_world = env->GetFieldID(mcCls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
    if (mcCls && !f_world) f_world = env->GetFieldID(mcCls, "field_71441_e", "Lnet/minecraft/client/multiplayer/WorldClient;");
    world = (mcObj && f_world) ? env->GetObjectField(mcObj, f_world) : nullptr;

    g_jCache.init(env);

    jclass worldCls = g_jCache.worldCls;
    jmethodID m_getSB = g_jCache.m_getScoreboard;
    jclass sbCls = g_jCache.sbCls;
    jmethodID m_getObj = g_jCache.m_getObjectiveInDisplaySlot;
    jmethodID m_getObjByName = g_jCache.m_getObjective;
    jmethodID m_getScore = g_jCache.m_getValueFromObjective;
    jclass scoreCls = g_jCache.scoreCls;
    jmethodID m_getVal = g_jCache.m_getScorePoints;
    jmethodID m_setVal = g_jCache.m_setScorePoints;
    f_gpName = g_jCache.f_gpName;

    std::string currentSortMode = Config::getSortMode();
    std::vector<std::string> currentNames;

    if (m_has && m_next)
    {
        int processedCount = 0;
    jobject iter = env->CallObjectMethod(col, m_iter);
    int extractionsThisFrame = 0;
    if (iter)
    {
        while (env->CallBooleanMethod(iter, m_has))
        {
            if (lc->CheckException()) break;
            if (env->PushLocalFrame(50) < 0) break;
            
            jobject info = env->CallObjectMethod(iter, m_next);
            if (info)
            {
                jobject prof = env->CallObjectMethod(info, m_getProf);
                if (prof)
                {
                    jstring jname = (jstring)env->CallObjectMethod(prof, m_getName);
                    if (jname)
                    {
                            const char* nameUtf = env->GetStringUTFChars(jname, 0);
                             std::string name(nameUtf);
                             while (true) {
                                 size_t pos = name.find("\xC2\xA7");
                                 if (pos == std::string::npos) break;
                                 if (pos + 3 <= name.length()) {
                                     name.erase(pos, 3);
                                 } else {
                                     name.erase(pos);
                                     break;
                                 }
                             }
                            currentNames.push_back(name);
                            env->ReleaseStringUTFChars(jname, nameUtf);

                            if (Config::isNickedBypass() && extractionsThisFrame < 4) {
                                bool needsUuid = false;
                                {
                                    std::lock_guard<std::mutex> lock(g_uuidMapMutex);
                                    needsUuid = (g_playerUuidMap.find(name) == g_playerUuidMap.end());
                                }
                                if (needsUuid) {
                                    jobject guid = env->CallObjectMethod(prof, m_getId);
                                    if (guid) {
                                        jstring jUuid = (jstring)env->CallObjectMethod(guid, m_uuidToString);
                                        if (jUuid) {
                                            const char* uUtf = env->GetStringUTFChars(jUuid, 0);
                                            if (uUtf) {
                                                std::string uuidStr = uUtf;
                                                env->ReleaseStringUTFChars(jUuid, uUtf);
                                                {
                                                    std::lock_guard<std::mutex> lock(g_uuidMapMutex);
                                                    g_playerUuidMap[name] = uuidStr;
                                                }
                                                extractionsThisFrame++;
                                            }
                                            env->DeleteLocalRef(jUuid);
                                        }
                                        env->DeleteLocalRef(guid);
                                    }
                                }
                            }
                    }
                }
            }
            
            processedCount++;
            env->PopLocalFrame(nullptr);
            if (processedCount > 500) break; // sanity
        }
        if (iter) env->DeleteLocalRef(iter);
        }
    }

    {
        std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
        g_onlinePlayers = currentNames;
    }

    if (doTabUpdate && m_has && m_next)
    {
        iter = env->CallObjectMethod(col, m_iter);
        if (!iter) { env->DeleteLocalRef(col); env->DeleteLocalRef(nh); env->DeleteLocalRef(mcObj); return; }

        jobject scoreboard = (world && m_getSB) ? env->CallObjectMethod(world, m_getSB) : nullptr;
        env->ExceptionClear();
        jobject tabObj = (scoreboard && m_getObj) ? env->CallObjectMethod(scoreboard, m_getObj, 0) : nullptr;
        env->ExceptionClear();

        int processedTab = 0;
        while (env->CallBooleanMethod(iter, m_has))
        {
            if (lc->CheckException()) break;
            
            if (env->PushLocalFrame(100) < 0) break;

            jobject info = env->CallObjectMethod(iter, m_next);
            if (info && m_getProf && m_getName)
            {
                jobject prof = env->CallObjectMethod(info, m_getProf);
                if (prof)
                {
                    jstring jn = (jstring)env->CallObjectMethod(prof, m_getName);
                    if (jn)
                    {
                        const char *utf = env->GetStringUTFChars(jn, 0);
                        std::string name(utf ? utf : "");
                        if (utf) env->ReleaseStringUTFChars(jn, utf);
                        
                        while (true) {
                            size_t pos = name.find("\xC2\xA7");
                            if (pos == std::string::npos) break;
                            if (pos + 3 <= name.length()) {
                                name.erase(pos, 3);
                            } else {
                                name.erase(pos);
                                break;
                            }
                        }

                        if (forceReset || !Config::isTabEnabled())
                        {
                            if (m_setDisp) env->CallVoidMethod(info, m_setDisp, nullptr);
                            if (f_gpName) {
                                jstring orig = env->NewStringUTF(name.c_str());
                                env->SetObjectField(prof, f_gpName, orig);
                                env->DeleteLocalRef(orig);
                            }
                        }
                        else if (cctInit && m_setDisp)
                        {
                            Hypixel::PlayerStats stats;
                            bool hasStats = false;
                            {
                                std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
                                auto itS = ChatInterceptor::g_playerStatsMap.find(name);
                                if (itS != ChatInterceptor::g_playerStatsMap.end())
                                {
                                    stats = itS->second;
                                    hasStats = true;
                                }
                            }

                            std::string teamColorCode = "\xC2\xA7" "f";
                            std::string currentTeam;
                            std::string cName = name;

                            if (scoreboard) {
                                currentTeam = resolveTeamForNameEx(env, name, scoreboard, g_jCache.m_getPlayersTeam, g_jCache.teamCls, g_jCache.m_getPrefix);
                            }

                            // SELF TEAM FIX
                            if (currentTeam.empty()) {
                                static jfieldID f_thePlayer = nullptr;
                                if (!f_thePlayer) {
                                    f_thePlayer = env->GetFieldID(mcCls, "thePlayer", "Lnet/minecraft/client/entity/EntityPlayerSP;");
                                    if (!f_thePlayer) f_thePlayer = env->GetFieldID(mcCls, "field_71439_g", "Lnet/minecraft/client/entity/EntityPlayerSP;");
                                }
                                jobject lp = (mcObj && f_thePlayer) ? env->GetObjectField(mcObj, f_thePlayer) : nullptr;
                                if (lp) {
                                    jmethodID m_getLPName = env->GetMethodID(env->GetObjectClass(lp), "getName", "()Ljava/lang/String;");
                                    if (!m_getLPName) m_getLPName = env->GetMethodID(env->GetObjectClass(lp), "func_70005_c_", "()Ljava/lang/String;");
                                    jstring lpNameJ = (jstring)env->CallObjectMethod(lp, m_getLPName);
                                    if (lpNameJ) {
                                        const char* lpUtf = env->GetStringUTFChars(lpNameJ, 0);
                                        if (lpUtf) {
                                            g_localName = lpUtf;
                                            if (name == lpUtf) {
                                                currentTeam = resolveTeamForNameEx(env, lpUtf, scoreboard, g_jCache.m_getPlayersTeam, g_jCache.teamCls, g_jCache.m_getPrefix);
                                            }
                                            env->ReleaseStringUTFChars(lpNameJ, lpUtf);
                                        }
                                        env->DeleteLocalRef(lpNameJ);
                                    }
                                    env->DeleteLocalRef(lp);
                                }
                            }

                            if (!currentTeam.empty()) {
                                teamColorCode = mcColorForTeam(currentTeam);
                                std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
                                g_playerTeamColor[name] = currentTeam;
                            } else {
                                std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
                                auto itTC = g_playerTeamColor.find(name);
                                if (itTC != g_playerTeamColor.end()) teamColorCode = mcColorForTeam(itTC->second);
                            }

                            std::string sortMetric = Config::getSortMode();
                            std::transform(sortMetric.begin(), sortMetric.end(), sortMetric.begin(), ::tolower);
                            double sortVal = 0;
                            if (hasStats) {
                                if (sortMetric == "fk") sortVal = (double)stats.bedwarsFinalKills;
                                else if (sortMetric == "fkdr") sortVal = (stats.bedwarsFinalDeaths == 0) ? (double)stats.bedwarsFinalKills : (double)stats.bedwarsFinalKills / stats.bedwarsFinalDeaths;
                                else if (sortMetric == "wins") sortVal = (double)stats.bedwarsWins;
                                else if (sortMetric == "wlr") sortVal = (stats.bedwarsLosses == 0) ? (double)stats.bedwarsWins : (double)stats.bedwarsWins / stats.bedwarsLosses;
                                else if (sortMetric == "star") sortVal = (double)stats.bedwarsStar;
                                else if (sortMetric == "ws") sortVal = (double)stats.winstreak;
                            }
                            if (sortMetric == "team") {
                                if (currentTeam == "Red") sortVal = 100; else if (currentTeam == "Blue") sortVal = 200; else if (currentTeam == "Green") sortVal = 300; else if (currentTeam == "Yellow") sortVal = 400; else if (currentTeam == "Aqua") sortVal = 500; else if (currentTeam == "White") sortVal = 600; else if (currentTeam == "Pink") sortVal = 700; else if (currentTeam == "Gray" || currentTeam == "Grey") sortVal = 800; else sortVal = 999;
                            }

                            long rank = (long)(sortVal * 10.0);
                            if (Config::isTabSortDescending()) rank = 9999L - rank;
                            if (rank < 0) rank = 0; if (rank > 9999) rank = 9999;
                            char rankBuf[8]; sprintf_s(rankBuf, "%04ld", rank);
                            std::string calculatedPrefix = ""; 
                            for(int i=0; i<4; ++i) { calculatedPrefix += "\xC2\xA7"; calculatedPrefix += rankBuf[i]; }

                            std::string finalPrefix = calculatedPrefix;
                            {
                                std::lock_guard<std::mutex> lockR(g_stableRankMutex);
                                auto itR = g_stableRankMap.find(name);
                                if (hasStats) {
                                    g_stableRankMap[name] = calculatedPrefix;
                                    finalPrefix = calculatedPrefix;
                                } else if (itR != g_stableRankMap.end()) {
                                    finalPrefix = itR->second;
                                } else if (processedTab > 5 && !currentTeam.empty()) {
                                    g_stableRankMap[name] = calculatedPrefix;
                                    finalPrefix = calculatedPrefix;
                                }
                            }

                                    g_stableRankMap[name] = calculatedPrefix;
                                    finalPrefix = calculatedPrefix;
                                }
                            }

                            std::string internalName = finalPrefix + teamColorCode + cName;
                            if (f_gpName) {
                                if (internalName.length() > 40) {
                                    internalName = teamColorCode + cName;
                                    if (internalName.length() > 40) internalName = internalName.substr(0, 40);
                                }
                                jstring newNameObj = env->NewStringUTF(internalName.c_str());
                                if (newNameObj) {
                                    env->SetObjectField(prof, f_gpName, newNameObj);
                                    if (env->ExceptionCheck()) env->ExceptionClear();
                                    env->DeleteLocalRef(newNameObj);
                                }
                            }

                            std::string fullTabString;
                            if (hasStats)
                            {
                                if (stats.isNicked) {
                                    fullTabString = teamColorCode + name + " \xC2\xA7" "4[NICKED]";
                                } else {
                                    fullTabString = BedwarsStars::GetFormattedLevel(stats.bedwarsStar) + " " + teamColorCode + name;
                                    if (Config::isTagsEnabled()) fullTabString += stats.tagsDisplay;
                                    fullTabString += " \xC2\xA7" "7: ";
                                    
                                    std::string dMode = Config::getTabDisplayMode();
                                    std::transform(dMode.begin(), dMode.end(), dMode.begin(), ::tolower);
                                    if (dMode == "fk") fullTabString += colorForFinalKills(stats.bedwarsFinalKills) + std::to_string(stats.bedwarsFinalKills);
                                    else if (dMode == "fkdr") {
                                        double fkdr = (stats.bedwarsFinalDeaths == 0) ? (double)stats.bedwarsFinalKills : (double)stats.bedwarsFinalKills / stats.bedwarsFinalDeaths;
                                        std::ostringstream ss_fkdr; ss_fkdr << std::fixed << std::setprecision(2) << fkdr;
                                        fullTabString += colorForFkdr(fkdr) + ss_fkdr.str();
                                    } else if (dMode == "wins") fullTabString += colorForWins(stats.bedwarsWins) + std::to_string(stats.bedwarsWins);
                                    else if (dMode == "wlr") {
                                        double wlr = (stats.bedwarsLosses == 0) ? (double)stats.bedwarsWins : (double)stats.bedwarsWins / stats.bedwarsLosses;
                                        std::ostringstream ss_wlr; ss_wlr << std::fixed << std::setprecision(2) << wlr;
                                        fullTabString += colorForWlr(wlr) + ss_wlr.str();
                                    } else if (dMode == "star" || dMode == "lvl") fullTabString += "\xC2\xA7" "6" + std::to_string(stats.bedwarsStar) + "\xC2\xA7" "e\xE2\x9C\xAF";
                                    else if (dMode == "ws") fullTabString += "\xC2\xA7" "d" + std::to_string(stats.winstreak) + " WS";
                                    else if (dMode == "team") fullTabString += currentTeam.empty() ? "\xC2\xA7" "7None" : teamColorCode + currentTeam;
                                }
                            } else {
                                fullTabString = teamColorCode + name;
                            }

                            jstring jf = env->NewStringUTF(fullTabString.c_str());
                            jobject component = (jf) ? env->NewObject(cctCls, cctInit, jf) : nullptr;
                            if (component && m_setDisp) env->CallVoidMethod(info, m_setDisp, component);
                            if (jf) env->DeleteLocalRef(jf);
                            if (component) env->DeleteLocalRef(component);

                            if (hasStats && scoreboard && tabObj && m_getScore && m_getVal && m_setVal) {
                                jstring oldNameJ = env->NewStringUTF(name.c_str());
                                int scoreVal = 0; bool scoreFound = false;
                                if (oldNameJ) {
                                    jobject oldScore = env->CallObjectMethod(scoreboard, m_getScore, oldNameJ, tabObj);
                                    if (oldScore) {
                                        scoreVal = env->CallIntMethod(oldScore, m_getVal);
                                        if (scoreVal > 0) scoreFound = true;
                                        env->DeleteLocalRef(oldScore);
                                    }
                                    env->DeleteLocalRef(oldNameJ);
                                }
                                if (scoreFound) {
                                    jstring newNameJ = env->NewStringUTF(internalName.c_str());
                                    if (newNameJ) {
                                        jobject newScore = env->CallObjectMethod(scoreboard, m_getScore, newNameJ, tabObj);
                                        if (newScore) {
                                            env->CallVoidMethod(newScore, m_setVal, scoreVal);
                                            env->ExceptionClear();
                                            env->DeleteLocalRef(newScore);
                                        }
                                        env->DeleteLocalRef(newNameJ);
                                    }
                                }
                            }
                        } else {
                            if (m_setDisp) env->CallVoidMethod(info, m_setDisp, nullptr);
                        }
                    }
                }
            }
            
            if (lc->CheckException()) {
                env->ExceptionClear();
            }

            env->PopLocalFrame(nullptr);
            processedTab++;
            if (processedTab > 500) break; // sanity
        }
        if (scoreboard) env->DeleteLocalRef(scoreboard);
        if (tabObj) env->DeleteLocalRef(tabObj);
        if (processedTab > 0) Logger::log(Config::DebugCategory::General, "Tab: Updated %d players", processedTab);
    }

    if (iter) env->DeleteLocalRef(iter);
    env->DeleteLocalRef(col);
    env->DeleteLocalRef(nh);
    if (world) env->DeleteLocalRef(world);
    env->DeleteLocalRef(mcObj);
}


static std::string getUserProfileDir()
{
    char *up = nullptr;
    size_t sz = 0;
    std::string out;
    if (_dupenv_s(&up, &sz, "USERPROFILE") == 0 && up)
        out = up;
    if (up)
        free(up);
    return out;
}

static std::string buildLogsDir()
{
    std::string base = getUserProfileDir();
    if (base.empty())
        return std::string();
    return base + "\\.lunarclient\\profiles\\lunar\\1.8\\logs";
}

static std::string findNewestLogFile(const std::string &dir)
{
    WIN32_FIND_DATAA fd{};
    std::string pattern = dir + "\\*.log";
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return std::string();
    FILETIME best{};
    std::string bestName;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            if (CompareFileTime(&fd.ftLastWriteTime, &best) > 0)
            {
                best = fd.ftLastWriteTime;
                bestName = fd.cFileName;
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (bestName.empty())
        return std::string();
    return dir + "\\" + bestName;
}

static bool ensureLogOpen()
{
    if (g_logsDir.empty())
        g_logsDir = buildLogsDir();
    if (g_logsDir.empty())
        return false;
    std::string latest = findNewestLogFile(g_logsDir);
    if (latest.empty())
        return false;
    if (g_logFilePath != latest)
    {
        if (g_logHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_logHandle);
            g_logHandle = INVALID_HANDLE_VALUE;
        }
        g_logFilePath = latest;
        g_logOffset = 0;
        g_logBuf.clear();
    }
    if (g_logHandle == INVALID_HANDLE_VALUE)
    {
        g_logHandle = CreateFileA(g_logFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g_logHandle == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER sz{};
        if (GetFileSizeEx(g_logHandle, &sz))
            g_logOffset = (long long)sz.QuadPart; // tail from end
    }
    return true;
}

static void parsePlayersFromOnlineLine(const std::string &joined)
{
    if (!g_inHypixelGame) 
    {
        g_onlinePlayers.clear();
        return;
    }
    std::string listStr = joined.substr(joined.find("ONLINE:") + 7);
    while (!listStr.empty() && listStr.front() == ' ')
        listStr.erase(listStr.begin());
    while (!listStr.empty() && listStr.back() == ' ')
        listStr.pop_back();
    std::vector<std::string> names;
    size_t start = 0;
    for (;;)
    {
        size_t comma = listStr.find(',', start);
        std::string token = listStr.substr(start, comma == std::string::npos ? std::string::npos : (comma - start));
        while (!token.empty() && token.front() == ' ')
            token.erase(token.begin());
        while (!token.empty() && token.back() == ' ')
            token.pop_back();
        if (!token.empty())
            names.push_back(token);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    if (!names.empty())
    {
        std::vector<std::string> sorted = names;
        std::sort(sorted.begin(), sorted.end());
        std::vector<std::string> prev = g_onlinePlayers;
        std::sort(prev.begin(), prev.end());
        if (sorted == prev)
            return;
        g_onlinePlayers = names;
        
        {
            std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
            for (auto it = ChatInterceptor::g_playerStatsMap.begin(); it != ChatInterceptor::g_playerStatsMap.end(); ) {
                bool found = false;
                for (const auto& p : g_onlinePlayers) {
                    if (p == it->first) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    it = ChatInterceptor::g_playerStatsMap.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
}


static void resetGameCache()
{
    {
        std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
        ChatInterceptor::g_playerStatsMap.clear();
        ChatInterceptor::g_playerTeamColor.clear();
    }
    {
        std::lock_guard<std::mutex> lockR(g_stableRankMutex);
        g_stableRankMap.clear();
    }
    g_processedPlayers.clear();
    g_onlinePlayers.clear();
    g_playerFetchRetries.clear();
    {
        std::lock_guard<std::mutex> lock(g_alertedMutex);
        g_alertedPlayers.clear();
    }
    {
        std::lock_guard<std::mutex> qlock(g_queueMutex);
        g_queuedPlayers.clear();
    }
    {
        std::lock_guard<std::mutex> aLock(g_activeFetchesMutex);
        g_activeFetches.clear();
    }
    g_lastResetTick = GetTickCount64();
    Logger::log(Config::DebugCategory::GameDetection, "Game cache reset performed");
}

void ChatInterceptor::clearAllCaches()
{
    {
        std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
        ChatInterceptor::g_playerStatsMap.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_persistentStatsCache.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(g_uuidMapMutex);
        g_playerUuidMap.clear();
    }
    
    g_playerTeamColor.clear();
    g_processedPlayers.clear();
    g_onlinePlayers.clear();
    g_playerFetchRetries.clear();
    
    {
        std::lock_guard<std::mutex> lock(g_alertedMutex);
        g_alertedPlayers.clear();
    }
    {
        std::lock_guard<std::mutex> qlock(g_queueMutex);
        g_queuedPlayers.clear();
    }
    {
        std::lock_guard<std::mutex> aLock(g_activeFetchesMutex);
        g_activeFetches.clear();
    }
    
    Urchin::clearCache();
    Seraph::clearCache();
    
    Logger::log(Config::DebugCategory::General, "All caches cleared!");
}

static void cleanupStaleStats()
{
    std::lock_guard<std::mutex> statsLock(ChatInterceptor::g_statsMutex);
    
    if (g_inHypixelGame)
    {
        std::lock_guard<std::mutex> qlock(g_queueMutex);
        std::lock_guard<std::mutex> clock(g_cacheMutex);
        for (auto &pair : ChatInterceptor::g_playerStatsMap)
        {
            if (pair.second.isNicked)
            {
                std::string name = pair.first;
                g_processedPlayers.erase(name);
                g_persistentStatsCache.erase(name);
                g_playerFetchRetries[name] = 0;
                g_queuedPlayers.erase(name);
            }
        }
    }

    for (auto it = ChatInterceptor::g_playerStatsMap.begin(); it != ChatInterceptor::g_playerStatsMap.end(); )
    {
        bool found = false;
        for (const auto& p : g_onlinePlayers)
        {
            if (p == it->first) { found = true; break; }
        }

        bool isNicked = it->second.isNicked;
        bool shouldPrune = !found && (!g_inHypixelGame || isNicked);

        if (shouldPrune)
        {
            std::string name = it->first;
            it = ChatInterceptor::g_playerStatsMap.erase(it);
            
            std::lock_guard<std::mutex> qlock(g_queueMutex);
            g_processedPlayers.erase(name);
            g_playerFetchRetries.erase(name);
            g_queuedPlayers.erase(name);
        }
        else
        {
            ++it;
        }
    }
    
    Logger::log(Config::DebugCategory::General, "Stale stats cleanup performed");
}

static void syncTeamColors()
{
    JNIEnv *env = lc->getEnv();
    if (!env) return;

    g_jCache.init(env);

    jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
    if (!mcCls) return;
    jfieldID theMc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
    jobject mcObj = theMc ? env->GetStaticObjectField(mcCls, theMc) : nullptr;
    if (!mcObj) return;

    jfieldID f_world = env->GetFieldID(mcCls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
    if (!f_world) f_world = env->GetFieldID(mcCls, "field_71441_e", "Lnet/minecraft/client/multiplayer/WorldClient;");
    jobject world = f_world ? env->GetObjectField(mcObj, f_world) : nullptr;

    if (world) {
        jmethodID m_getScoreboard = g_jCache.m_getScoreboard;
        jobject scoreboard = m_getScoreboard ? env->CallObjectMethod(world, m_getScoreboard) : nullptr;
        env->ExceptionClear();
        if (scoreboard) {
            if (g_jCache.m_getPlayersTeam && g_jCache.m_getPrefix) {
                std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
                for (auto &pair : ChatInterceptor::g_playerStatsMap) {
                    std::string team = resolveTeamForNameEx(env, pair.first, scoreboard, g_jCache.m_getPlayersTeam, g_jCache.teamCls, g_jCache.m_getPrefix);
                    if (team.empty()) {
                        auto itT = g_playerTeamColor.find(pair.first);
                        if (itT != g_playerTeamColor.end()) team = itT->second;
                    }

                    if (!team.empty()) {
                        pair.second.teamColor = team;
                        g_playerTeamColor[pair.first] = team;
                    }
                }
            }
            env->DeleteLocalRef(scoreboard);
        }
        env->DeleteLocalRef(world);
    }
    env->DeleteLocalRef(mcObj);
}

static void syncTags()
{
    if (!Config::isTagsEnabled()) return;
    
    std::string activeS = Config::getActiveTagService(); 
    auto getAbbr = [](const std::string& raw) -> std::string {
             std::string t = raw;
             for (auto & c: t) c = toupper(c);
             if (t.find("BLATANT") != std::string::npos) return "\xC2\xA7" "4[BC]";
             if (t.find("CLOSET") != std::string::npos) return "\xC2\xA7" "4[CC]";
             if (t.find("CHEATER") != std::string::npos) return "\xC2\xA7" "4[C]";
             if (t.find("CONFIRMED") != std::string::npos) return "\xC2\xA7" "4[C]";
             if (t.find("CAUTION") != std::string::npos) return "\xC2\xA7" "e[!]";
             if (t.find("SUSPICIOUS") != std::string::npos) return "\xC2\xA7" "6[?]";
             if (t.find("SNIPER") != std::string::npos) return "\xC2\xA7" "6[S]";
             return "";
    };

    std::vector<std::pair<std::string, std::string>> playersNeedingTags;
    {
        std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
        for (auto &pair : ChatInterceptor::g_playerStatsMap) {
            if (pair.second.tagsDisplay.empty() && pair.second.rawTags.empty()) {
                playersNeedingTags.push_back({pair.first, pair.second.uuid});
            }
        }
    }

    if (playersNeedingTags.empty()) return;

    std::vector<std::tuple<std::string, std::string, std::vector<std::string>>> updates;
    for (const auto& p : playersNeedingTags) {
        std::string tagStr;
        std::vector<std::string> rTags;
        bool foundAny = false;

        if (activeS == "Urchin" || activeS == "Both") {
            auto uT = Urchin::getPlayerTags(p.first);
            if (uT && !uT->tags.empty()) {
                std::string a = getAbbr(uT->tags[0].type);
                tagStr += " " + (a.empty() ? "\xC2\xA7" "4[U]" : a);
                for(const auto& t : uT->tags) rTags.push_back("URCHIN:" + t.type);
                foundAny = true;
            }
        }
        if ((activeS == "Seraph" || activeS == "Both") && !p.second.empty()) {
            auto sT = Seraph::getPlayerTags(p.first, p.second);
            if (sT && !sT->tags.empty()) {
                std::string a = getAbbr(sT->tags[0].type);
                tagStr += " " + (a.empty() ? "\xC2\xA7" "4[S]" : a);
                for(const auto& t : sT->tags) rTags.push_back("SERAPH:" + t.type);
                foundAny = true;
            }
        }
        
        if (foundAny) {
            updates.push_back({p.first, tagStr, rTags});
        }
    }

    if (!updates.empty()) {
        std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
        for (const auto& u : updates) {
            auto it = ChatInterceptor::g_playerStatsMap.find(std::get<0>(u));
            if (it != ChatInterceptor::g_playerStatsMap.end()) {
                it->second.tagsDisplay = std::get<1>(u);
                it->second.rawTags = std::get<2>(u);
            }
        }
    }
}

static void tailLogOnce()
{
    if (!ensureLogOpen())
        return;
    LARGE_INTEGER pos{};
    pos.QuadPart = g_logOffset;
    SetFilePointerEx(g_logHandle, pos, nullptr, FILE_BEGIN);
    char buf[4096]; // this shit crashed the whole thing
    DWORD read = 0;
    if (!ReadFile(g_logHandle, buf, sizeof(buf), &read, nullptr) || read == 0)
        return;
    g_logOffset += read;
    g_logBuf.append(buf, buf + read);

    size_t nl;
    while ((nl = g_logBuf.find('\n')) != std::string::npos)
    {
        std::string line = g_logBuf.substr(0, nl);
        g_logBuf.erase(0, nl + 1);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.find("[CHAT]") == std::string::npos)
            continue;
        size_t p = line.find("[CHAT]");
        std::string chat = (p != std::string::npos) ? line.substr(p + 6) : line;
        detectTeamsFromLine(chat);
        detectFinalKillsFromLine(chat);
        Logic::AutoGG::handleChat(chat);

        if (chat.find("ONLINE:") != std::string::npos)
        {
            if (line != g_lastOnlineLine)
            {
                g_lastOnlineLine = line;
                Logger::log(Config::DebugCategory::GameDetection, "Detected ONLINE list, parsing players...");
                parsePlayersFromOnlineLine(chat);
                g_nextFetchIdx = 0; 
                g_processedPlayers.clear();
            }
        }
    }
}

static void fetchWorker(std::string name, std::string forcedUuid = "")
{
    const std::string apiKey = Config::getApiKey();
    if (apiKey.empty()) return;

    bool cacheFound = false;
    Hypixel::PlayerStats cachedData;
    ULONGLONG now = GetTickCount64();

    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        auto it = g_persistentStatsCache.find(name);
        if (it != g_persistentStatsCache.end()) {
            if (now - it->second.timestamp < 600000) {
                cachedData = it->second.stats;
                cacheFound = true;
            }
        }
    }

     if (cacheFound) {
        std::lock_guard<std::mutex> lock(g_pendingStatsMutex);
        g_pendingStatsMap[name] = cachedData;
    } 
    
    bool fetchError = false;
    std::string uuidToF;
    Hypixel::PlayerStats fetchedStats;

    if (!cacheFound) {
        std::optional<std::string> uuid = forcedUuid.empty() ? Hypixel::getUuidByName(name) : std::optional<std::string>(forcedUuid);
        if (uuid)
        {
            auto statsOpt = Hypixel::getPlayerStats(apiKey, *uuid);
            if (statsOpt)
            {
                uuidToF = *uuid;
                fetchedStats = *statsOpt;
                fetchError = false;
            } else fetchError = true;
        } else fetchError = true;
    }

    bool shouldFetchTags = false;
    if (Config::isTagsEnabled()) {
            if (cacheFound && (cachedData.tagsDisplay.empty() && cachedData.rawTags.empty())) shouldFetchTags = true;
            if (!cacheFound && !fetchError) shouldFetchTags = true;
    }

    if (shouldFetchTags) {
            Hypixel::PlayerStats& targetStats = cacheFound ? cachedData : fetchedStats;
            std::string currentUuid = cacheFound ? targetStats.uuid : uuidToF;
            
            if (cacheFound && currentUuid.empty()) {
                auto u = Hypixel::getUuidByName(name);
                if (u) currentUuid = *u;
            }

            std::string tagStr;
            std::vector<std::string> rTags;

            std::string activeS = Config::getActiveTagService();
            auto getAbbr = [](const std::string& raw) -> std::string {
            std::string t = raw;
            for (auto & c: t) c = toupper(c);
            if (t.find("BLATANT") != std::string::npos) return "\xC2\xA7" "4[BC]";
            if (t.find("CLOSET") != std::string::npos) return "\xC2\xA7" "4[CC]";
            if (t.find("CONFIRMED") != std::string::npos) return "\xC2\xA7" "4[C]";
            if (t.find("CHEATER") != std::string::npos) return "\xC2\xA7" "4[C]";
            if (t.find("SNIPER") != std::string::npos) return "\xC2\xA7" "6[S]";
            return "";
            };

            if (activeS == "Urchin" || activeS == "Both") {
            auto uT = Urchin::getPlayerTags(name);
            if (uT && !uT->tags.empty()) {
                std::string a = getAbbr(uT->tags[0].type);
                tagStr += " " + (a.empty() ? "\xC2\xA7" "4[U]" : a);
                for(const auto& t : uT->tags) rTags.push_back("URCHIN:" + t.type);
            }
            }
            if ((activeS == "Seraph" || activeS == "Both") && !currentUuid.empty()) {
            auto sT = Seraph::getPlayerTags(name, currentUuid);
            if (sT && !sT->tags.empty()) {
                std::string a = getAbbr(sT->tags[0].type);
                tagStr += " " + (a.empty() ? "\xC2\xA7" "4[S]" : a);
                for(const auto& t : sT->tags) rTags.push_back("SERAPH:" + t.type);
            }
            }
            
            targetStats.tagsDisplay = tagStr;
            targetStats.rawTags = rTags;
            // Removed blocking logging here to prevent potential lags, relying on eventual display
            // if (!targetStats.tagsDisplay.empty()) {
            //    Logger::info("Tags found for %s: %s", name.c_str(), targetStats.tagsDisplay.c_str());
            // }

            if (cacheFound) {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                g_persistentStatsCache[name] = { cachedData, now };
            }
    }

    if (cacheFound) {
        std::lock_guard<std::mutex> lock(g_pendingStatsMutex);
        g_pendingStatsMap[name] = cachedData;
        std::lock_guard<std::mutex> lockQ(g_queueMutex);
        g_processedPlayers.insert(name);
    } else if (!fetchError) {
            {
            std::lock_guard<std::mutex> lock(g_cacheMutex);
            g_persistentStatsCache[name] = { fetchedStats, now };
            }
            std::lock_guard<std::mutex> lock(g_pendingStatsMutex);
            g_pendingStatsMap[name] = fetchedStats;
        
            std::lock_guard<std::mutex> lockQ(g_queueMutex);
            g_processedPlayers.insert(name);
    }

    if (fetchError && !cacheFound)
    {
        std::lock_guard<std::mutex> lock(g_retryMutex);
        int count = ++g_playerFetchRetries[name];
        if (count < 5)
        {
            g_retryUntil[name] = now + 2000;
        }
        else
        {
            Hypixel::PlayerStats nickedStats;
            nickedStats.isNicked = true;
            {
                std::lock_guard<std::mutex> lock(g_pendingStatsMutex);
                g_pendingStatsMap[name] = nickedStats;
            }
            // cache
            {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                g_persistentStatsCache[name] = { nickedStats, now };
            }
            std::lock_guard<std::mutex> lockQ(g_queueMutex);
            g_processedPlayers.insert(name);
            fetchError = false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_activeFetchesMutex);
        g_activeFetches.erase(name);
    }
}

void ChatInterceptor::initialize()
{
    g_initialized = true;
    g_bootstrapStartTick = (ULONGLONG)GetTickCount64();
}

void ChatInterceptor::shutdown()
{
    g_initialized = false;
    {
        std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
        ChatInterceptor::g_playerStatsMap.clear();
    }
    g_onlinePlayers.clear();
    g_processedPlayers.clear();
    {
        std::lock_guard<std::mutex> qlock(g_queueMutex);
        g_queuedPlayers.clear();
    }
    g_activeFetches.clear();
    g_playerTeamColor.clear();
    
    JNIEnv* env = lc->getEnv();
    if (env) {
        g_jCache.cleanup(env);
    }
}

void ChatInterceptor::setMode(int mode)
{
    g_mode = mode;
}

static void queuePlayersForFetching()
{
    ULONGLONG now = GetTickCount64();
    if (!g_inHypixelGame || g_lobbyGraceTicks > 0) return;
    if (g_onlinePlayers.empty()) return;

    std::string mode = Config::getOverlayMode();
    if (mode == "invisible" && !Config::isTabEnabled()) return;

    std::lock_guard<std::mutex> qLock(g_queueMutex);
    std::lock_guard<std::mutex> rLock(g_retryMutex);
    std::lock_guard<std::mutex> aLock(g_activeFetchesMutex);

    for (const auto &name : g_onlinePlayers)
    {
        if (g_processedPlayers.find(name) != g_processedPlayers.end()) continue;
        
        if (g_activeFetches.find(name) != g_activeFetches.end()) continue;
        
        auto it = g_retryUntil.find(name);
        if (it != g_retryUntil.end() && now < it->second) continue;

        std::string uuidToUse = "";
        if (Config::isNickedBypass()) {
            std::lock_guard<std::mutex> lock(g_uuidMapMutex);
            auto itU = g_playerUuidMap.find(name);
            if (itU != g_playerUuidMap.end()) uuidToUse = itU->second;
        }

        g_activeFetches.insert(name);
        std::thread(fetchWorker, name, uuidToUse).detach();
    }
}

static void processPendingStats()
{
    std::string name;
    Hypixel::PlayerStats stats;
    bool found = false;

    {
        std::lock_guard<std::mutex> lock(g_pendingStatsMutex);
        if (!g_pendingStatsMap.empty())
        {
            auto it = g_pendingStatsMap.begin();
            name = it->first;
            stats = it->second;
            found = true;
            g_pendingStatsMap.erase(it);
        }
    }

    if (found)
    {
        bool online = false;
        for (const auto &p : g_onlinePlayers) { if (p == name) { online = true; break; } }
        if (!online) return;

        double fkdr = (stats.bedwarsFinalDeaths == 0) ? (double)stats.bedwarsFinalKills : (double)stats.bedwarsFinalKills / stats.bedwarsFinalDeaths;
        std::ostringstream fkdrSs; fkdrSs << std::fixed << std::setprecision(2) << fkdr;
        
        double wlr = (stats.bedwarsLosses == 0) ? (double)stats.bedwarsWins : (double)stats.bedwarsWins / stats.bedwarsLosses;
        std::ostringstream wlrSs; wlrSs << std::fixed << std::setprecision(2) << wlr;

        std::string team;
        auto itT = g_playerTeamColor.find(name);
        if (itT != g_playerTeamColor.end()) team = itT->second;
        else
        {
            team = resolveTeamForName(name);
            if (!team.empty()) g_playerTeamColor[name] = team;
        }
        
        const char *tcol = mcColorForTeam(team);
        const char *black = "\xC2\xA7" "0";
        const char *white = "\xC2\xA7" "f";
        
        std::string msg;
        if (stats.isNicked) {
            const char *nameColor = (team == "Gray") ? "\xC2\xA7" "8" : (team.empty() ? white : tcol);
            msg = nameColor + name + " \xC2\xA7" "4[NICKED]";
        } else {
            msg += BedwarsStars::GetFormattedLevel(stats.bedwarsStar);
            msg += " ";

            const char *tInit = teamInitial(team);
            if (!team.empty())
            {
                msg += black + std::string("[") + tcol + tInit + black + std::string("] ");
            }
            
            const char *nameColor = (team == "Gray") ? "\xC2\xA7" "8" : (team.empty() ? white : tcol);
            msg += nameColor + name;

            if (g_mode == 0) {
                msg += std::string(" ") + black + "[" + white + "FKDR" + black + "] " + colorForFkdr(fkdr) + fkdrSs.str();
                msg += std::string(" ") + black + "[" + white + "FK" + black + "] " + colorForFinalKills(stats.bedwarsFinalKills) + std::to_string(stats.bedwarsFinalKills);
                msg += std::string(" ") + black + "[" + white + "W" + black + "] " + colorForWins(stats.bedwarsWins) + std::to_string(stats.bedwarsWins);
                msg += std::string(" ") + black + "[" + white + "WLR" + black + "] " + colorForWlr(wlr) + wlrSs.str();
            } else {
                msg += " STATS FOUND";
            }
        }

        // REMOVED: Inaccurate heuristic marking 0-stat players as "NICKED"
        /*
        if (stats.bedwarsStar == 0 && stats.bedwarsFinalKills == 0 && stats.bedwarsWins == 0) {
            stats.isNicked = true;
        }
        */

        {
            std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
            stats.teamColor = team;
            ChatInterceptor::g_playerStatsMap[name] = stats;
        }

        if (Config::getOverlayMode() == "chat") {
            ChatSDK::showClientMessage(ChatSDK::formatPrefix() + msg);
        }

        if (Config::isTagsEnabled() && !stats.rawTags.empty()) {
             for (const auto& tag : stats.rawTags) {
                 if (tag.find("URCHIN:") == 0) {
                     std::string type = tag.substr(7);
                     std::string check = type; for(auto& c : check) c = toupper(c);
                     if (check.find("BLATANT") != std::string::npos || check.find("SNIPER") != std::string::npos || check.find("CHEATER") != std::string::npos) {
                        ChatSDK::showClientMessage(ChatSDK::formatPrefix() + "\xC2\xA7" "cALERT: \xC2\xA7" "f" + name + " is tagged as \xC2\xA7" "l" + type + "\xC2\xA7" "r!");
                        Render::NotificationManager::getInstance()->add("Urchin Alert", name + " is a " + type, Render::NotificationType::Warning);
                        break; 
                     }
                 }
                 else if (tag.find("SERAPH:") == 0) {
                     std::string type = tag.substr(7);
                     ChatSDK::showClientMessage(ChatSDK::formatPrefix() + "\xC2\xA7" "4SERAPH ALERT: \xC2\xA7" "f" + name + " is blacklisted: \xC2\xA7" "l" + type + "\xC2\xA7" "r!");
                     Render::NotificationManager::getInstance()->add("Seraph Alert", name + " is blacklisted (" + type + ")", Render::NotificationType::Error);
                 }
             }
        }

        Logger::info("Stats processed for %s", name.c_str());
        g_processedPlayers.insert(name);
    }
}



void ChatInterceptor::poll()
{
    JNIEnv *env = lc->getEnv();
    if (!g_initialized || !env)
        return;

    Services::DiscordManager::getInstance()->update();

    if (lc->CheckException()) return;

    ULONGLONG now = GetTickCount64();
    if (g_lastChatReadTick == 0 || (now - g_lastChatReadTick) >= 50)
    {
        g_lastChatReadTick = now;
        tailLogOnce();
        
        updateTabListStats();
        
        if (g_lastTeamScanTick == 0 || (now - g_lastTeamScanTick) >= (g_inHypixelGame && (now - g_lastResetTick < 10000) ? 200 : 1000))
        {
            g_lastTeamScanTick = now;
            updateTeamsFromScoreboard();
        }

        static ULONGLONG lastCleanup = 0;
        if (lastCleanup == 0 || (now - lastCleanup) >= 10000)
        {
            lastCleanup = now;
            cleanupStaleStats();
            pruneStatsCache();
        }

        static ULONGLONG lastSync = 0;
        if (lastSync == 0 || (now - lastSync) >= 5000)
        {
            lastSync = now;
            syncTeamColors();
        }

        static ULONGLONG lastTagSync = 0;
        if (lastTagSync == 0 || (now - lastTagSync) >= 200) 
        {
            lastTagSync = now;
            syncTags();
        }

        if (Config::isBedDefenseEnabled()) {
            BedDefense::BedDefenseManager::getInstance()->tick();
        }

        queuePlayersForFetching();
        processPendingStats();
    }

    jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
    if (!mcCls) return;
    jfieldID theMc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
    if (!theMc) return;
    jobject mcObj = env->GetStaticObjectField(mcCls, theMc);
    if (!mcObj) return;
    
    jfieldID f_screen = env->GetFieldID(mcCls, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;");
    if (!f_screen) { env->DeleteLocalRef(mcObj); return; }
    jobject screen = env->GetObjectField(mcObj, f_screen);
    if (!screen) { env->DeleteLocalRef(mcObj); return; }

    jclass guiChatCls = lc->GetClass("net.minecraft.client.gui.GuiChat");
    if (guiChatCls && env->IsInstanceOf(screen, guiChatCls))
    {
        jfieldID f_input = env->GetFieldID(guiChatCls, "inputField", "Lnet/minecraft/client/gui/GuiTextField;");
        if (f_input)
        {
            jobject input = env->GetObjectField(screen, f_input);
            if (input)
            {
                jclass tfCls = env->GetObjectClass(input);
                jmethodID getText = env->GetMethodID(tfCls, "getText", "()Ljava/lang/String;");
                jstring jtxt = (jstring)env->CallObjectMethod(input, getText);
                if (jtxt)
                {
                    const char *utf = env->GetStringUTFChars(jtxt, 0);
                    std::string text = utf ? utf : "";
                    if (utf) env->ReleaseStringUTFChars(jtxt, utf);

                    SHORT enterState = GetAsyncKeyState(VK_RETURN);
                    static bool wasEnterDown = false;
                    bool isEnterDown = (enterState & 0x8000) != 0;
                    if (!wasEnterDown && isEnterDown && !text.empty())
                    {
                        if (text[0] == '.') {
                            jmethodID setText = env->GetMethodID(tfCls, "setText", "(Ljava/lang/String;)V");
                            if (!setText) setText = env->GetMethodID(tfCls, "func_146180_a", "(Ljava/lang/String;)V");
                            
                            if (setText)
                            {
                                jstring empty = env->NewStringUTF("");
                                env->CallVoidMethod(input, setText, empty);
                                env->DeleteLocalRef(empty);
                                
                                CommandRegistry::instance().tryDispatch(text);

                                jmethodID m_display = env->GetMethodID(mcCls, "displayGuiScreen", "(Lnet/minecraft/client/gui/GuiScreen;)V");
                                if (!m_display) m_display = env->GetMethodID(mcCls, "func_147108_a", "(Lnet/minecraft/client/gui/GuiScreen;)V");
                                if (m_display) env->CallVoidMethod(mcObj, m_display, nullptr);
                            }
                        }
                        else if (Config::isChatBypasserEnabled()) {
                            std::string bypassed = ChatBypasser::process(text);
                            
                            jmethodID setText = env->GetMethodID(tfCls, "setText", "(Ljava/lang/String;)V");
                            if (!setText) setText = env->GetMethodID(tfCls, "func_146180_a", "(Ljava/lang/String;)V");

                            if (setText) {
                                jstring empty = env->NewStringUTF("");
                                env->CallVoidMethod(input, setText, empty);
                                env->DeleteLocalRef(empty);

                                ChatSDK::sendClientChat(bypassed);

                                jmethodID m_display = env->GetMethodID(mcCls, "displayGuiScreen", "(Lnet/minecraft/client/gui/GuiScreen;)V");
                                if (!m_display) m_display = env->GetMethodID(mcCls, "func_147108_a", "(Lnet/minecraft/client/gui/GuiScreen;)V");
                                if (m_display) env->CallVoidMethod(mcObj, m_display, nullptr);
                            }
                        }
                    }
                    wasEnterDown = isEnterDown;
                    env->DeleteLocalRef(jtxt);
                }
                env->DeleteLocalRef(tfCls);
                env->DeleteLocalRef(input);
            }
        }
    }

    env->DeleteLocalRef(mcObj);
}

bool ChatInterceptor::isInGame(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_statsMutex);
    return g_playerStatsMap.count(name) > 0;
}

bool ChatInterceptor::shouldAlert(const std::string& name) {
    if (!isInGame(name)) return false;
    std::lock_guard<std::mutex> lock(g_alertedMutex);
    if (g_alertedPlayers.count(name)) return false;
    g_alertedPlayers.insert(name);
    return true;
}
bool ChatInterceptor::isInHypixelGame() {
    return g_inHypixelGame;
}

int ChatInterceptor::getGameMode() {
    return g_mode;
}
