#include "ChatInterceptor.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include "Commands.h"
#include "ChatSDK.h"
#include "../Services/Hypixel.h"
#include "../Config/Config.h"
#include "../Utils/Logger.h"
#include "../Java.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>
#include <cstring>


static bool g_initialized = false;
static std::string g_lastOnlineLine;
static std::vector<std::string> g_onlinePlayers;
static size_t g_nextFetchIdx = 0;
static std::unordered_map<std::string, std::string> g_playerTeamColor; // name -> team (e.g., "Red")
static ULONGLONG g_lastTeamScanTick = 0;
static int g_mode = 0; // 0 bedwars, 1 skywars, 2 duels
static ULONGLONG g_bootstrapStartTick = 0;
static std::unordered_map<std::string, int> g_teamProbeTries; // name -> tries during bootstrap
static std::unordered_set<std::string> g_processedPlayers;

// exposed for GUI overlay
std::unordered_map<std::string, Hypixel::PlayerStats> ChatInterceptor::g_playerStatsMap;
std::mutex ChatInterceptor::g_statsMutex;

static const char *mcColorForTeam(const std::string &team)
{
    if (team == "Red")
        return "\xC2\xA7"
               "4"; // §4 dark red
    if (team == "Blue")
        return "\xC2\xA7"
               "9"; // §9
    if (team == "Green")
        return "\xC2\xA7"
               "2"; // §2 dark green
    if (team == "Yellow")
        return "\xC2\xA7"
               "e"; // §e
    if (team == "Aqua")
        return "\xC2\xA7"
               "b"; // §b
    if (team == "White")
        return "\xC2\xA7"
               "f"; // §f
    if (team == "Pink")
        return "\xC2\xA7"
               "d"; // §d
    if (team == "Gray" || team == "Grey")
        return "\xC2\xA7"
               "8"; // §8
    return "\xC2\xA7"
           "7"; // §7 default
}

static const char *colorForFkdr(double fkdr)
{
    // 0-1 gray, 1-2 white, 2-3 gold, 3-4 aqua, 4-5 dark green, 5-6 purple, 6-7 dark red (>=6 dark red)
    if (fkdr < 1.0)
        return "\xC2\xA7"
               "7"; // gray
    if (fkdr < 2.0)
        return "\xC2\xA7"
               "f"; // white
    if (fkdr < 3.0)
        return "\xC2\xA7"
               "6"; // gold
    if (fkdr < 4.0)
        return "\xC2\xA7"
               "b"; // aqua
    if (fkdr < 5.0)
        return "\xC2\xA7"
               "2"; // dark green
    if (fkdr < 6.0)
        return "\xC2\xA7"
               "5"; // purple
    return "\xC2\xA7"
           "4"; // dark red
}

// wlr color scale: 0-1 white, 1-3 gold, 3-5 red, 5+ purple
static const char *colorForWlr(double wlr)
{
    if (wlr < 1.0)
        return "\xC2\xA7"
               "f"; // white
    if (wlr < 3.0)
        return "\xC2\xA7"
               "6"; // gold
    if (wlr < 5.0)
        return "\xC2\xA7"
               "4"; // dark red
    return "\xC2\xA7"
           "d"; // purple
}

// wins color scale: 0-500 gray, 500-1000 white, 1000-2000 yellow, 2000-4000 red, 4000+ purple
static const char *colorForWins(int wins)
{
    if (wins < 500)
        return "\xC2\xA7"
               "7"; // gray
    if (wins < 1000)
        return "\xC2\xA7"
               "f"; // white
    if (wins < 2000)
        return "\xC2\xA7"
               "e"; // yellow
    if (wins < 4000)
        return "\xC2\xA7"
               "4"; // dark red
    return "\xC2\xA7"
           "d"; // purple
}

// Final Kills color scale: 0-1000 gray, 1000-2000 white, 2000-4000 gold, 4000-5000 aqua, 5000-10000 red, 10000+ purple
static const char *colorForFinalKills(int fk)
{
    if (fk < 1000)
        return "\xC2\xA7"
               "7"; // gray
    if (fk < 2000)
        return "\xC2\xA7"
               "f"; // white
    if (fk < 4000)
        return "\xC2\xA7"
               "6"; // gold
    if (fk < 5000)
        return "\xC2\xA7"
               "b"; // aqua
    if (fk < 10000)
        return "\xC2\xA7"
               "4"; // dark red
    return "\xC2\xA7"
           "d"; // purple
}

static const char *colorForStar(int star)
{
    if (star < 100)
        return "\xC2\xA7"
               "7"; // gray
    if (star < 200)
        return "\xC2\xA7"
               "f"; // white
    if (star < 300)
        return "\xC2\xA7"
               "6"; // gold
    if (star < 400)
        return "\xC2\xA7"
               "b"; // aqua
    if (star < 500)
        return "\xC2\xA7"
               "2"; // dark green
    if (star < 600)
        return "\xC2\xA7"
               "b"; // aqua (diamond tier)
    return "\xC2\xA7"
           "4"; // dark red
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
    if (team == "Gray" || team == "Grey")
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
        }
        std::string needle2 = std::string(" joined (") + t + ")";
        auto p2 = chat.find(needle2);
        if (p2 != std::string::npos)
        {
            auto s = chat.rfind(' ', p2);
            std::string name = (s == std::string::npos) ? std::string() : chat.substr(0, s);
            // take last word of name
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
        return "Red";
    case '9':
        return "Blue";
    case 'a':
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

static void updateTeamsFromScoreboard()
{
    if (!lc->env)
        return;
    jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
    if (!mcCls)
        return;
    jmethodID m_getMc = lc->env->GetStaticMethodID(mcCls, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
    jfieldID theMc = lc->env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
    jobject mcObj = nullptr;
    if (m_getMc)
        mcObj = lc->env->CallStaticObjectMethod(mcCls, m_getMc);
    if (!mcObj && theMc)
        mcObj = lc->env->GetStaticObjectField(mcCls, theMc);
    if (!mcObj)
        return;
    jfieldID f_world = lc->env->GetFieldID(mcCls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
    if (!f_world)
    {
        lc->env->DeleteLocalRef(mcObj);
        return;
    }
    jobject world = lc->env->GetObjectField(mcObj, f_world);
    if (!world)
    {
        lc->env->DeleteLocalRef(mcObj);
        return;
    }
    jclass worldCls = lc->GetClass("net.minecraft.client.multiplayer.WorldClient");
    if (!worldCls)
    {
        lc->env->DeleteLocalRef(world);
        lc->env->DeleteLocalRef(mcObj);
        return;
    }
    jmethodID m_getScoreboard = lc->env->GetMethodID(worldCls, "getScoreboard", "()Lnet/minecraft/scoreboard/Scoreboard;");
    if (!m_getScoreboard)
    {
        lc->env->DeleteLocalRef(world);
        lc->env->DeleteLocalRef(mcObj);
        return;
    }
    jobject scoreboard = lc->env->CallObjectMethod(world, m_getScoreboard);
    if (!scoreboard)
    {
        lc->env->DeleteLocalRef(world);
        lc->env->DeleteLocalRef(mcObj);
        return;
    }
    jclass sbCls = lc->GetClass("net.minecraft.scoreboard.Scoreboard");
    if (!sbCls)
    {
        lc->env->DeleteLocalRef(scoreboard);
        lc->env->DeleteLocalRef(world);
        lc->env->DeleteLocalRef(mcObj);
        return;
    }
    jmethodID m_getPlayersTeam = lc->env->GetMethodID(sbCls, "getPlayersTeam", "(Ljava/lang/String;)Lnet/minecraft/scoreboard/ScorePlayerTeam;");
    if (!m_getPlayersTeam)
    {
        lc->env->DeleteLocalRef(scoreboard);
        lc->env->DeleteLocalRef(world);
        lc->env->DeleteLocalRef(mcObj);
        return;
    }
    jclass teamCls = lc->GetClass("net.minecraft.scoreboard.ScorePlayerTeam");
    jmethodID m_getColorPrefixStatic = teamCls ? lc->env->GetStaticMethodID(teamCls, "getColorPrefix", "(Lnet/minecraft/scoreboard/ScorePlayerTeam;)Ljava/lang/String;") : nullptr;
    jmethodID m_getColorPrefixInst = teamCls ? lc->env->GetMethodID(teamCls, "getColorPrefix", "()Ljava/lang/String;") : nullptr;
    jmethodID m_getPrefix = teamCls ? lc->env->GetMethodID(teamCls, "getPrefix", "()Ljava/lang/String;") : nullptr;
    jmethodID m_getColorPrefixSrg = teamCls ? lc->env->GetMethodID(teamCls, "func_96668_e", "()Ljava/lang/String;") : nullptr;

    for (const std::string &name : g_onlinePlayers)
    {
        jstring jn = lc->env->NewStringUTF(name.c_str());
        jobject team = lc->env->CallObjectMethod(scoreboard, m_getPlayersTeam, jn);
        const char *tstr = "";
        if (team)
        {
            jstring pref = nullptr;
            if (m_getColorPrefixStatic)
                pref = (jstring)lc->env->CallStaticObjectMethod(teamCls, m_getColorPrefixStatic, team);
            if (!pref && m_getColorPrefixInst)
                pref = (jstring)lc->env->CallObjectMethod(team, m_getColorPrefixInst);
            if (!pref && m_getPrefix)
                pref = (jstring)lc->env->CallObjectMethod(team, m_getPrefix);
            if (!pref && m_getColorPrefixSrg)
                pref = (jstring)lc->env->CallObjectMethod(team, m_getColorPrefixSrg);
            if (pref)
            {
                const char *utf = lc->env->GetStringUTFChars(pref, 0);
                if (utf)
                {
                    const char *sect = strchr(utf, '\xC2'); // UTF-8 § is C2 A7; simple scan
                    char code = 0;
                    // naive: also check raw '§'
                    const char *raw = strchr(utf, '\xA7');
                    if (raw && raw[1])
                        code = raw[1];
                    if (!code && sect)
                    {
                        // try find 0xA7 following
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
                    lc->env->ReleaseStringUTFChars(pref, utf);
                }
                lc->env->DeleteLocalRef(pref);
            }
            lc->env->DeleteLocalRef(team);
        }
        lc->env->DeleteLocalRef(jn);
    }
    lc->env->DeleteLocalRef(scoreboard);
    lc->env->DeleteLocalRef(world);
    lc->env->DeleteLocalRef(mcObj);
}

static std::string resolveTeamForName(const std::string &name)
{
    if (!lc->env)
        return std::string();
    jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
    if (!mcCls)
        return std::string();
    jfieldID theMc = lc->env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
    if (!theMc)
        return std::string();
    jobject mcObj = lc->env->GetStaticObjectField(mcCls, theMc);
    if (!mcObj)
        return std::string();
    jfieldID f_world = lc->env->GetFieldID(mcCls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
    if (!f_world)
    {
        lc->env->DeleteLocalRef(mcObj);
        return std::string();
    }
    jobject world = lc->env->GetObjectField(mcObj, f_world);
    if (!world)
    {
        lc->env->DeleteLocalRef(mcObj);
        return std::string();
    }
    jclass worldCls = lc->GetClass("net.minecraft.client.multiplayer.WorldClient");
    if (!worldCls)
    {
        lc->env->DeleteLocalRef(world);
        lc->env->DeleteLocalRef(mcObj);
        return std::string();
    }
    jmethodID m_getScoreboard = lc->env->GetMethodID(worldCls, "getScoreboard", "()Lnet/minecraft/scoreboard/Scoreboard;");
    if (!m_getScoreboard)
    {
        lc->env->DeleteLocalRef(world);
        lc->env->DeleteLocalRef(mcObj);
        return std::string();
    }
    jobject scoreboard = lc->env->CallObjectMethod(world, m_getScoreboard);
    if (!scoreboard)
    {
        lc->env->DeleteLocalRef(world);
        lc->env->DeleteLocalRef(mcObj);
        return std::string();
    }
    jclass sbCls = lc->GetClass("net.minecraft.scoreboard.Scoreboard");
    jmethodID m_getPlayersTeam = sbCls ? lc->env->GetMethodID(sbCls, "getPlayersTeam", "(Ljava/lang/String;)Lnet/minecraft/scoreboard/ScorePlayerTeam;") : nullptr;
    jclass teamCls = lc->GetClass("net.minecraft.scoreboard.ScorePlayerTeam");
    jmethodID m_getColorPrefix = teamCls ? lc->env->GetStaticMethodID(teamCls, "getColorPrefix", "(Lnet/minecraft/scoreboard/ScorePlayerTeam;)Ljava/lang/String;") : nullptr;
    jmethodID m_getPrefix = teamCls ? lc->env->GetMethodID(teamCls, "getPrefix", "()Ljava/lang/String;") : nullptr;
    std::string result;
    if (m_getPlayersTeam)
    {
        jstring jn = lc->env->NewStringUTF(name.c_str());
        jobject team = lc->env->CallObjectMethod(scoreboard, m_getPlayersTeam, jn);
        if (team)
        {
            jstring pref = nullptr;
            if (m_getColorPrefix)
                pref = (jstring)lc->env->CallStaticObjectMethod(teamCls, m_getColorPrefix, team);
            if (!pref && m_getPrefix)
                pref = (jstring)lc->env->CallObjectMethod(team, m_getPrefix);
            if (pref)
            {
                const unsigned char *u = (const unsigned char *)lc->env->GetStringUTFChars(pref, 0);
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
                    lc->env->ReleaseStringUTFChars(pref, (const char *)u);
                }
                if (code)
                {
                    const char *tname = teamFromColorCode(code);
                    if (tname && *tname)
                        result = tname;
                }
                lc->env->DeleteLocalRef(pref);
            }
            lc->env->DeleteLocalRef(team);
        }
        lc->env->DeleteLocalRef(jn);
    }
    lc->env->DeleteLocalRef(scoreboard);
    lc->env->DeleteLocalRef(world);
    lc->env->DeleteLocalRef(mcObj);
    if (!result.empty())
        return result;

    jclass mcCls2 = lc->GetClass("net.minecraft.client.Minecraft");
    if (!mcCls2)
        return std::string();
    jfieldID theMc2 = lc->env->GetStaticFieldID(mcCls2, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
    if (!theMc2)
        return std::string();
    jobject mc2 = lc->env->GetStaticObjectField(mcCls2, theMc2);
    if (!mc2)
        return std::string();
    jmethodID m_getNetHandler = lc->env->GetMethodID(mcCls2, "getNetHandler", "()Lnet/minecraft/client/network/NetHandlerPlayClient;");
    if (!m_getNetHandler)
    {
        lc->env->DeleteLocalRef(mc2);
        return std::string();
    }
    jobject nh = lc->env->CallObjectMethod(mc2, m_getNetHandler);
    if (!nh)
    {
        lc->env->DeleteLocalRef(mc2);
        return std::string();
    }
    jclass nhCls = lc->GetClass("net.minecraft.client.network.NetHandlerPlayClient");
    if (!nhCls)
    {
        lc->env->DeleteLocalRef(nh);
        lc->env->DeleteLocalRef(mc2);
        return std::string();
    }
    jmethodID m_getMap = lc->env->GetMethodID(nhCls, "getPlayerInfoMap", "()Ljava/util/Collection;");
    if (!m_getMap)
    {
        lc->env->DeleteLocalRef(nh);
        lc->env->DeleteLocalRef(mc2);
        return std::string();
    }
    jobject col = lc->env->CallObjectMethod(nh, m_getMap);
    if (!col)
    {
        lc->env->DeleteLocalRef(nh);
        lc->env->DeleteLocalRef(mc2);
        return std::string();
    }
    jclass collCls = lc->env->FindClass("java/util/Collection");
    jmethodID m_iter = collCls ? lc->env->GetMethodID(collCls, "iterator", "()Ljava/util/Iterator;") : nullptr;
    jobject iter = m_iter ? lc->env->CallObjectMethod(col, m_iter) : nullptr;
    jclass iterCls = lc->env->FindClass("java/util/Iterator");
    jmethodID m_has = iterCls ? lc->env->GetMethodID(iterCls, "hasNext", "()Z") : nullptr;
    jmethodID m_next = iterCls ? lc->env->GetMethodID(iterCls, "next", "()Ljava/lang/Object;") : nullptr;
    std::string tabResult;
    jclass npiCls = lc->GetClass("net.minecraft.client.network.NetworkPlayerInfo");
    jmethodID m_disp = npiCls ? lc->env->GetMethodID(npiCls, "getDisplayName", "()Lnet/minecraft/util/IChatComponent;") : nullptr;
    jmethodID m_prof = npiCls ? lc->env->GetMethodID(npiCls, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;") : nullptr;
    jclass profCls = lc->env->FindClass("com/mojang/authlib/GameProfile");
    jmethodID m_getName = profCls ? lc->env->GetMethodID(profCls, "getName", "()Ljava/lang/String;") : nullptr;
    jclass iccCls = lc->GetClass("net.minecraft.util.IChatComponent");
    jmethodID m_fmt = iccCls ? lc->env->GetMethodID(iccCls, "getFormattedText", "()Ljava/lang/String;") : nullptr;
    if (iter && m_has && m_next)
    {
        while (lc->env->CallBooleanMethod(iter, m_has))
        {
            jobject info = lc->env->CallObjectMethod(iter, m_next);
            if (!info)
                continue;
            std::string ign;
            if (m_prof && m_getName)
            {
                jobject prof = lc->env->CallObjectMethod(info, m_prof);
                if (prof)
                {
                    jstring jn = (jstring)lc->env->CallObjectMethod(prof, m_getName);
                    if (jn)
                    {
                        const char *utf = lc->env->GetStringUTFChars(jn, 0);
                        if (utf)
                            ign = utf;
                        if (utf)
                            lc->env->ReleaseStringUTFChars(jn, utf);
                        lc->env->DeleteLocalRef(jn);
                    }
                    lc->env->DeleteLocalRef(prof);
                }
            }
            if (!ign.empty() && ign == name)
            {
                if (m_disp && m_fmt)
                {
                    jobject disp = lc->env->CallObjectMethod(info, m_disp);
                    if (disp)
                    {
                        jstring fmt = (jstring)lc->env->CallObjectMethod(disp, m_fmt);
                        if (fmt)
                        {
                            const unsigned char *u = (const unsigned char *)lc->env->GetStringUTFChars(fmt, 0);
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
                                lc->env->ReleaseStringUTFChars(fmt, (const char *)u);
                            }
                            if (code)
                            {
                                const char *tname = teamFromColorCode(code);
                                if (tname && *tname)
                                    tabResult = tname;
                            }
                            lc->env->DeleteLocalRef(fmt);
                        }
                        lc->env->DeleteLocalRef(disp);
                    }
                }
                lc->env->DeleteLocalRef(info);
                break;
            }
            lc->env->DeleteLocalRef(info);
        }
    }
    // cleanup
    // note: local refs for coll/iter classes not strictly required to delete
    lc->env->DeleteLocalRef(col);
    lc->env->DeleteLocalRef(nh);
    lc->env->DeleteLocalRef(mc2);
    return tabResult;
}
static ULONGLONG g_lastChatReadTick = 0;
static std::string g_logsDir;
static std::string g_logFilePath;
static HANDLE g_logHandle = INVALID_HANDLE_VALUE;
static long long g_logOffset = 0;
static std::string g_logBuf;
static ULONGLONG g_lastImmediateTeamProbeTick = 0;
static std::unordered_map<std::string, ULONGLONG> g_retryUntil; // name -> tick to retry after

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
    std::string listStr = joined.substr(joined.find("ONLINE:") + 7);
    while (!listStr.empty() && listStr.front() == ' ')
        listStr.erase(listStr.begin());
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
        
        // sync stats map: remove players who are no longer in the list
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

static void tailLogOnce()
{
    if (!ensureLogOpen())
        return;
    LARGE_INTEGER pos{};
    pos.QuadPart = g_logOffset;
    SetFilePointerEx(g_logHandle, pos, nullptr, FILE_BEGIN);
    char buf[4096];
    DWORD read = 0;
    if (!ReadFile(g_logHandle, buf, sizeof(buf), &read, nullptr) || read == 0)
        return;
    g_logOffset += read;
    Logger::info("log dosyası okundu");
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

        if (chat.find("ONLINE:") != std::string::npos)
        {
            if (line != g_lastOnlineLine)
            {
                g_lastOnlineLine = line;
                ChatSDK::showPrefixed("Detected ONLINE. Saving players...");
                parsePlayersFromOnlineLine(chat);
                g_nextFetchIdx = 0; // reset fetch pointer
                g_processedPlayers.clear();
            }
        }
        
        // auto-remove on Final Kill
        if (chat.find("FINAL KILL!") != std::string::npos) {
            std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
            
            std::string victimName;
            size_t minPos = std::string::npos;
            
            for (const auto& pair : ChatInterceptor::g_playerStatsMap) {
                const std::string& name = pair.first;
                size_t pos = chat.find(name);
                if (pos != std::string::npos) {
                    // get the earliest name
                    if (pos < minPos) {
                        minPos = pos;
                        victimName = name;
                    }
                }
            }
            
            if (!victimName.empty()) {
                Logger::info("Final Kill detected: %s. Removing from overlay.", victimName.c_str());
                ChatInterceptor::g_playerStatsMap.erase(victimName);
            }
        }
    }
}

void ChatInterceptor::initialize()
{
    g_initialized = true;
    g_bootstrapStartTick = (ULONGLONG)GetTickCount();
}

void ChatInterceptor::shutdown()
{
    g_initialized = false;
}

void ChatInterceptor::setMode(int mode)
{
    g_mode = mode;
}

void ChatInterceptor::poll()
{
    if (!g_initialized || !lc->env)
        return;
    ULONGLONG nowTickHead = (ULONGLONG)GetTickCount();
    if (g_lastChatReadTick == 0 || (nowTickHead - g_lastChatReadTick) >= 100)
    {
        g_lastChatReadTick = nowTickHead;
        tailLogOnce();
        ULONGLONG nowT = (ULONGLONG)GetTickCount();
        if (g_lastTeamScanTick == 0 || (nowT - g_lastTeamScanTick) >= 1000)
        {
            g_lastTeamScanTick = nowT;
            updateTeamsFromScoreboard();
        }
        if (nowT - g_bootstrapStartTick < 3000)
        {
            updateTeamsFromScoreboard();
        }
        if (!g_onlinePlayers.empty())
        {
            ULONGLONG nowMs = (ULONGLONG)GetTickCount();
            size_t size = g_onlinePlayers.size();
            
            bool allProcessed = true;
            for (const auto &player : g_onlinePlayers)
            {
                if (g_processedPlayers.find(player) == g_processedPlayers.end())
                {
                    allProcessed = false;
                    break;
                }
            }
            if (allProcessed)
            {
                return; // ok
            }

            size_t pickedIdx = size; // invalid
            for (size_t cnt = 0; cnt < size; ++cnt)
            {
                size_t idx = (g_nextFetchIdx + cnt) % size;
                const std::string &candidate = g_onlinePlayers[idx];
                
                if (g_processedPlayers.find(candidate) != g_processedPlayers.end())
                    continue;
                
                auto itCooldown = g_retryUntil.find(candidate);
                if (itCooldown != g_retryUntil.end() && nowMs < itCooldown->second)
                {
                    continue;
                }
                
                pickedIdx = idx;
                break;
            }
            if (pickedIdx == size)
            {
                bool allSuccessfullyProcessed = true;
                for (const auto &player : g_onlinePlayers)
                {
                    if (g_processedPlayers.find(player) == g_processedPlayers.end())
                    {
                        allSuccessfullyProcessed = false;
                        break;
                    }
                }
                if (allSuccessfullyProcessed)
                {
                    return; // ok
                }
                // wait for cooldown
                return;
            }
            g_nextFetchIdx = pickedIdx;
            const std::string apiKey = Config::getApiKey();
            if (!apiKey.empty())
            {
                std::string name = g_onlinePlayers[g_nextFetchIdx];
                auto uuid = Hypixel::getUuidByName(name);
                if (uuid)
                {
                    Logger::info("UUID %s -> %s", name.c_str(), uuid->c_str());
                }
                if (uuid)
                {
                    auto stats = Hypixel::getPlayerStats(apiKey, *uuid);
                    if (stats)
                    {
                        double fkdr = 0.0;
                        if (stats->bedwarsFinalDeaths == 0)
                            fkdr = (double)stats->bedwarsFinalKills;
                        else
                            fkdr = (double)stats->bedwarsFinalKills / (double)stats->bedwarsFinalDeaths;
                        std::ostringstream fkdrSs;
                        fkdrSs << std::fixed << std::setprecision(2) << fkdr;
                        double wlr = 0.0;
                        if (stats->bedwarsLosses == 0)
                            wlr = (double)stats->bedwarsWins;
                        else
                            wlr = (double)stats->bedwarsWins / (double)stats->bedwarsLosses;
                        std::ostringstream wlrSs;
                        wlrSs << std::fixed << std::setprecision(2) << wlr;
                        std::string team = "";
                        auto itT = g_playerTeamColor.find(name);
                        if (itT != g_playerTeamColor.end())
                            team = itT->second;
                        else
                            team = resolveTeamForName(name);
                        
                        // more aggressive team detection for first player
                        if (!g_onlinePlayers.empty() && name == g_onlinePlayers[0])
                        {
                            Logger::info("İlk oyuncu takım tespiti başlatılıyor: %s", name.c_str());
                            
                            updateTeamsFromScoreboard();
                            
                            for (int attempt = 0; attempt < 5 && team.empty(); ++attempt)
                            {
                                std::string scoreboardTeam = resolveTeamForName(name);
                                if (!scoreboardTeam.empty())
                                {
                                    team = scoreboardTeam;
                                    g_playerTeamColor[name] = team;
                                    Logger::info("İlk oyuncu takımı bulundu (scoreboard, deneme %d): %s -> %s", attempt + 1, name.c_str(), team.c_str());
                                    break;
                                }
                                
                                if (attempt < 3)
                                {
                                    Sleep(150);
                                    updateTeamsFromScoreboard();
                                }
                            }
                            
                            if (team.empty())
                            {
                                Sleep(200);
                                updateTeamsFromScoreboard();
                                std::string finalTeam = resolveTeamForName(name);
                                if (!finalTeam.empty())
                                {
                                    team = finalTeam;
                                    g_playerTeamColor[name] = team;
                                    Logger::info("İlk oyuncu takımı son denemede bulundu: %s -> %s", name.c_str(), team.c_str());
                                }
                            }
                            
                            if (team.empty())
                            {
                                Logger::info("İlk oyuncu takımı bulunamadı: %s", name.c_str());
                            }
                        }
                        
                        const char *tcol = mcColorForTeam(team);
                        const char *gray = "\xC2\xA7"
                                           "7";
                        const char *gold = "\xC2\xA7"
                                           "6";
                        const char *aqua = "\xC2\xA7"
                                           "b";
                        const char *greenC = "\xC2\xA7"
                                             "a";
                        const char *yellow = "\xC2\xA7"
                                             "e";
                        const char *white = "\xC2\xA7"
                                            "f";
                        const char *darkGray = "\xC2\xA7"
                                               "8";
                        const char *black = "\xC2\xA7"
                                            "0";
                        std::string msg;
                        msg += black;
                        msg += "[";
                        msg += colorForStar(stats->bedwarsStar);
                        msg += std::to_string(stats->bedwarsStar);
                        msg += "\xE2\x9C\xAB";
                        msg += black;
                        msg += "] ";
                        if (!g_onlinePlayers.empty() && name == g_onlinePlayers[0] && team.empty())
                        {
                            updateTeamsFromScoreboard();
                            std::string lastCheck = resolveTeamForName(name);
                            if (!lastCheck.empty())
                            {
                                team = lastCheck;
                                g_playerTeamColor[name] = team;
                                Logger::info("İlk oyuncu takımı chat yazdırmadan önce bulundu: %s -> %s", name.c_str(), team.c_str());
                            }
                        }
                        
                        if (!team.empty())
                        {
                            tcol = mcColorForTeam(team);
                        }
                        
                        if (team.empty())
                        {
                            ULONGLONG nowProbe = (ULONGLONG)GetTickCount();
                            if ((nowProbe - g_bootstrapStartTick) < 3000)
                            {
                                int tries = g_teamProbeTries[name];
                                if (tries < 100)
                                {
                                    g_teamProbeTries[name] = tries + 1;
                                    if (g_nextFetchIdx > 0)
                                        g_nextFetchIdx--; // retry same player next tick
                                    return;               // allow scoreboard/tablist to populate
                                }
                            }
                        }
                        
                        const char *tInit = teamInitial(team);
                        if (!team.empty())
                        {
                            msg += black;
                            msg += "[";
                            msg += tcol;
                            msg += tInit;
                            msg += black;
                            msg += "] ";
                        }
                        msg += (team.empty() ? white : tcol);
                        msg += name;
                        if (g_mode == 0)
                        {
                            // bedwars
                            msg += " ";
                            msg += black;
                            msg += "[";
                            msg += white;
                            msg += "FKDR";
                            msg += black;
                            msg += "]";
                            msg += " ";
                            msg += colorForFkdr(fkdr);
                            msg += fkdrSs.str();
                            msg += " ";
                            msg += black;
                            msg += "[";
                            msg += white;
                            msg += "FK";
                            msg += black;
                            msg += "]";
                            msg += " ";
                            msg += colorForFinalKills(stats->bedwarsFinalKills);
                            msg += std::to_string(stats->bedwarsFinalKills);
                            msg += " ";
                            msg += black;
                            msg += "[";
                            msg += white;
                            msg += "W";
                            msg += black;
                            msg += "]";
                            msg += " ";
                            msg += colorForWins(stats->bedwarsWins);
                            msg += std::to_string(stats->bedwarsWins);
                            msg += " ";
                            msg += black;
                            msg += "[";
                            msg += white;
                            msg += "WLR";
                            msg += black;
                            msg += "]";
                            msg += " ";
                            msg += colorForWlr(wlr);
                            msg += wlrSs.str();
                        }
                        else if (g_mode == 1)
                        {
                            // skywars: placeholder using bw data until skywars endpoint added
                            msg += " ";
                            msg += black;
                            msg += "[";
                            msg += white;
                            msg += "KDR";
                            msg += black;
                            msg += "]";
                            msg += " ";
                            msg += colorForFkdr(fkdr);
                            msg += fkdrSs.str();
                            msg += " ";
                            msg += black;
                            msg += "[";
                            msg += white;
                            msg += "K";
                            msg += black;
                            msg += "]";
                            msg += " ";
                            msg += colorForFinalKills(stats->bedwarsFinalKills);
                            msg += std::to_string(stats->bedwarsFinalKills);
                            msg += " ";
                            msg += black;
                            msg += "[";
                            msg += white;
                            msg += "W";
                            msg += black;
                            msg += "]";
                            msg += " ";
                            msg += colorForWins(stats->bedwarsWins);
                            msg += std::to_string(stats->bedwarsWins);
                            msg += " ";
                            msg += black;
                            msg += "[";
                            msg += white;
                            msg += "STAR";
                            msg += black;
                            msg += "]";
                            msg += " ";
                            msg += colorForStar(stats->bedwarsStar);
                            msg += std::to_string(stats->bedwarsStar);
                        }
                        else
                        {
                            // duels: same w skywars
                            msg += " ";
                            msg += black;
                            msg += "[";
                            msg += white;
                            msg += "duels";
                            msg += black;
                            msg += "]";
                            msg += " ";
                            msg += white;
                            msg += "wip";
                        }
                        
                        // store stats in map for GUI overlay
                        {
                            std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
                            stats->teamColor = team;
                            ChatInterceptor::g_playerStatsMap[name] = *stats;
                        }
                        
                        // only show in chat if mode is "chat"
                        if (Config::getOverlayMode() == "chat") {
                            ChatSDK::showClientMessage(ChatSDK::formatPrefix() + msg);
                        }
                        
                        Logger::info("statlar alındı: %s star=%d fkdr=%s finals=%d wins=%d", name.c_str(), stats->bedwarsStar, fkdrSs.str().c_str(), stats->bedwarsFinalKills, stats->bedwarsWins);
                        g_retryUntil.erase(name); // success clears cooldown
                        g_processedPlayers.insert(name);
                    }
                    else
                    {
                        Logger::info("statlar alınamadı: %s", name.c_str());
                        g_retryUntil[name] = nowMs + 5000ULL;
                        g_nextFetchIdx = (g_nextFetchIdx + 1) % size;
                        return;
                    }
                }
                else
                {
                    Logger::info("UUID bulunamadı: %s", name.c_str());
                    g_retryUntil[name] = nowMs + 5000ULL;
                    g_nextFetchIdx = (g_nextFetchIdx + 1) % size;
                    return;
                }
                g_nextFetchIdx = (g_nextFetchIdx + 1) % size;
            }
        }
    }
    jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
    if (!mcCls)
        return;
    jfieldID theMc = lc->env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
    if (!theMc)
        return;
    jobject mcObj = lc->env->GetStaticObjectField(mcCls, theMc);
    if (!mcObj)
        return;
    jfieldID f_screen = lc->env->GetFieldID(mcCls, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;");
    if (!f_screen)
    {
        lc->env->DeleteLocalRef(mcObj);
        return;
    }
    jobject screen = lc->env->GetObjectField(mcObj, f_screen);
    if (!screen)
    {
        lc->env->DeleteLocalRef(mcObj);
        return;
    }
    jclass guiChatCls = lc->GetClass("net.minecraft.client.gui.GuiChat");
    if (!guiChatCls || !lc->env->IsInstanceOf(screen, guiChatCls))
    {
        lc->env->DeleteLocalRef(screen);
        lc->env->DeleteLocalRef(mcObj);
        return;
    }
    jfieldID f_input = lc->env->GetFieldID(guiChatCls, "inputField", "Lnet/minecraft/client/gui/GuiTextField;");
    if (!f_input)
    {
        lc->env->DeleteLocalRef(screen);
        lc->env->DeleteLocalRef(mcObj);
        return;
    }
    jobject input = lc->env->GetObjectField(screen, f_input);
    if (!input)
    {
        lc->env->DeleteLocalRef(screen);
        lc->env->DeleteLocalRef(mcObj);
        return;
    }
    jclass tfCls = lc->env->GetObjectClass(input);
    if (!tfCls)
    {
        lc->env->DeleteLocalRef(input);
        lc->env->DeleteLocalRef(screen);
        lc->env->DeleteLocalRef(mcObj);
        return;
    }
    jmethodID getText = lc->env->GetMethodID(tfCls, "getText", "()Ljava/lang/String;");
    if (!getText)
    {
        lc->env->DeleteLocalRef(input);
        lc->env->DeleteLocalRef(screen);
        lc->env->DeleteLocalRef(mcObj);
        return;
    }
    jstring jtxt = (jstring)lc->env->CallObjectMethod(input, getText);
    std::string text;
    if (jtxt)
    {
        const char *utf = lc->env->GetStringUTFChars(jtxt, 0);
        text = utf ? utf : "";
        if (utf)
            lc->env->ReleaseStringUTFChars(jtxt, utf);
    }

    SHORT enterState = GetAsyncKeyState(VK_RETURN);
    static bool wasEnterDown = false;
    bool isEnterDown = (enterState & 0x8000) != 0;
    if (!wasEnterDown && isEnterDown && !text.empty() && text[0] == '.')
    {
        if (CommandRegistry::instance().tryDispatch(text))
        {
            jmethodID setText = lc->env->GetMethodID(tfCls, "setText", "(Ljava/lang/String;)V");
            if (setText)
            {
                jstring empty = lc->env->NewStringUTF("");
                if (empty)
                {
                    lc->env->CallVoidMethod(input, setText, empty);
                    lc->env->DeleteLocalRef(empty);
                }
            }
            jmethodID display = lc->env->GetMethodID(mcCls, "displayGuiScreen", "(Lnet/minecraft/client/gui/GuiScreen;)V");
            if (display)
            {
                lc->env->CallVoidMethod(mcObj, display, nullptr);
            }
        }
    }
    wasEnterDown = isEnterDown;

    // cleanup
    if (jtxt)
        lc->env->DeleteLocalRef(jtxt);
    lc->env->DeleteLocalRef(tfCls);
    lc->env->DeleteLocalRef(input);
    lc->env->DeleteLocalRef(screen);
    lc->env->DeleteLocalRef(mcObj);

    // already tailed above nothing else to do here
    return;
}
