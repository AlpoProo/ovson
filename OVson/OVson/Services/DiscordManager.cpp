#include "DiscordManager.h"
#include "../Config/Config.h"
#include "../Chat/ChatInterceptor.h"
#include "../Utils/Logger.h"
#include "../resource.h"
#include <chrono>
#include <cstring>
#include <windows.h>
#include <cstdio>

typedef void (*Discord_Initialize_Func)(const char*, Services::DiscordEventHandlers*, int, const char*);
typedef void (*Discord_Shutdown_Func)();
typedef void (*Discord_RunCallbacks_Func)();
typedef void (*Discord_UpdatePresence_Func)(const Services::DiscordRichPresence*);
typedef void (*Discord_ClearPresence_Func)();

static Discord_Initialize_Func pDiscord_Initialize = nullptr;
static Discord_Shutdown_Func pDiscord_Shutdown = nullptr;
static Discord_RunCallbacks_Func pDiscord_RunCallbacks = nullptr;
static Discord_UpdatePresence_Func pDiscord_UpdatePresence = nullptr;
static Discord_ClearPresence_Func pDiscord_ClearPresence = nullptr;
static std::string g_dllPath;

static void extractDll() {
    HMODULE hMod = Config::getModuleHandle();
    if (!hMod) return;

    char path[MAX_PATH];
    if (GetModuleFileNameA(hMod, path, MAX_PATH) == 0) return;
    
    std::string fullPath(path);
    size_t lastSlash = fullPath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        g_dllPath = fullPath.substr(0, lastSlash + 1) + "discord-rpc.dll";
    } else {
        g_dllPath = "discord-rpc.dll";
    }

    FILE* f = nullptr;
    if (fopen_s(&f, g_dllPath.c_str(), "r") == 0 && f) {
        fclose(f);
        return; 
    }

    HRSRC hRes = FindResource(hMod, MAKEINTRESOURCE(IDR_DISCORD_DLL), RT_RCDATA);
    if (!hRes) {
        Logger::error("Failed to find discord-rpc.dll resource!");
        return;
    }

    HGLOBAL hMem = LoadResource(hMod, hRes);
    if (!hMem) return;

    void* pData = LockResource(hMem);
    DWORD size = SizeofResource(hMod, hRes);

    if (fopen_s(&f, g_dllPath.c_str(), "wb") == 0 && f) {
        fwrite(pData, 1, size, f);
        fclose(f);
        Logger::info(("Extracted discord-rpc.dll to: " + g_dllPath).c_str());
    } else {
        Logger::error(("Failed to write discord-rpc.dll to: " + g_dllPath).c_str());
    }
}

namespace Services {
    DiscordManager* DiscordManager::getInstance() {
        static DiscordManager instance;
        return &instance;
    }

    void DiscordManager::init() {
        if (m_initialized || !Config::isDiscordRpcEnabled()) return;

        Logger::info("Initializing Discord RPC (Dynamic)...");

        extractDll();

        if (!m_discordDll) {
            if (g_dllPath.empty()) g_dllPath = "discord-rpc.dll";
            m_discordDll = LoadLibraryA(g_dllPath.c_str());
            if (!m_discordDll) {
                Logger::error(("Failed to load discord-rpc.dll from: " + g_dllPath).c_str());
                return;
            } else {
                Logger::info(("Loaded discord-rpc.dll successfully from: " + g_dllPath).c_str());
            }

            pDiscord_Initialize = (Discord_Initialize_Func)GetProcAddress((HMODULE)m_discordDll, "Discord_Initialize");
            pDiscord_Shutdown = (Discord_Shutdown_Func)GetProcAddress((HMODULE)m_discordDll, "Discord_Shutdown");
            pDiscord_RunCallbacks = (Discord_RunCallbacks_Func)GetProcAddress((HMODULE)m_discordDll, "Discord_RunCallbacks");
            pDiscord_UpdatePresence = (Discord_UpdatePresence_Func)GetProcAddress((HMODULE)m_discordDll, "Discord_UpdatePresence");
            pDiscord_ClearPresence = (Discord_ClearPresence_Func)GetProcAddress((HMODULE)m_discordDll, "Discord_ClearPresence");

            if (!pDiscord_Initialize || !pDiscord_UpdatePresence) {
                Logger::error("Failed to GetProcAddress for Discord functions!");
                FreeLibrary((HMODULE)m_discordDll);
                m_discordDll = nullptr;
                return;
            }
        }

        memset(&m_handlers, 0, sizeof(m_handlers));
        m_handlers.ready = [](const DiscordUser* user) { 
            DiscordManager::getInstance()->onReady(user);
        };
        m_handlers.errored = [](int errorCode, const char* message) {
            DiscordManager::getInstance()->onError(errorCode, message);
        };
        m_handlers.disconnected = [](int errorCode, const char* message) {
            DiscordManager::getInstance()->onDisconnected(errorCode, message);
        };

        std::string appId = Config::getDiscordAppId();
        const char* ws = " \t\n\r\f\v";
        appId.erase(appId.find_last_not_of(ws) + 1);
        appId.erase(0, appId.find_first_not_of(ws));

        Logger::info(("Using App ID: '" + appId + "' (Length: " + std::to_string(appId.length()) + ")").c_str());
        
        if (pDiscord_Initialize) {
            pDiscord_Initialize(appId.c_str(), &m_handlers, 1, nullptr);
        }

        m_startTime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        m_lastInitTime = GetTickCount64();
        m_initialized = true;
        m_connected = false;
        
        DiscordRichPresence presence;
        memset(&presence, 0, sizeof(presence));
        presence.state = "Loading...";
        presence.largeImageKey = "logo";
        
        if (pDiscord_UpdatePresence)
            pDiscord_UpdatePresence(&presence);

        if (pDiscord_RunCallbacks)
            pDiscord_RunCallbacks();
            
        Logger::info("Discord RPC Initialized (Dynamic).");
    }

    void DiscordManager::update() {
        if (!Config::isDiscordRpcEnabled()) {
            if (m_initialized) shutdown();
            return;
        }

        if (!m_initialized) {
            init();
            return;
        }

        if (!m_discordDll) return;

        static bool s_firstHit = false;
        if (!s_firstHit) {
            s_firstHit = true;
        }

        ULONGLONG now = GetTickCount64();
        static ULONGLONG lastPoll = 0;

        if (now - lastPoll > 100) {
            if (pDiscord_RunCallbacks) pDiscord_RunCallbacks();
            lastPoll = now;
        }

        if (!m_connected && (now - m_lastInitTime > 30000)) {
            static ULONGLONG lastWarn = 0;
            if (now - lastWarn > 30000) {
                 Logger::info("Still waiting for Discord RPC connection...");
                 lastWarn = now;
            }
        }

        static ULONGLONG lastUpdate = 0;
        if (now - lastUpdate < 2000) {
            return;
        }
        lastUpdate = now;

        DiscordRichPresence presence;
        memset(&presence, 0, sizeof(presence));
        
        static std::string stateStr;
        static std::string detailsStr;

        stateStr = "Hypixel Lobby";
        detailsStr = "Chilling";

        if (ChatInterceptor::isInHypixelGame()) {
            stateStr = "Playing Hypixel";
            int mode = ChatInterceptor::getGameMode();
            if (mode == 0) detailsStr = "Playing Bedwars";
            else if (mode == 1) detailsStr = "Playing Skywars";
            else if (mode == 2) detailsStr = "Playing Duels";
            else detailsStr = "In a Game";
        }

        presence.state = stateStr.c_str();
        presence.details = detailsStr.c_str();
        presence.startTimestamp = m_startTime;
        presence.largeImageKey = "logo";
        presence.largeImageText = "OVson Client";

        if (pDiscord_UpdatePresence)
            pDiscord_UpdatePresence(&presence);
    }

    void DiscordManager::shutdown() {
        if (!m_initialized) return;
        Logger::info("Shutting down Discord RPC...");
        
        if (pDiscord_ClearPresence) pDiscord_ClearPresence();
        if (pDiscord_Shutdown) pDiscord_Shutdown();
        
        m_initialized = false;
        m_connected = false;
    }

    void DiscordManager::onReady(const DiscordUser* user) {
        m_connected = true;
        Logger::info(("Discord RPC Connected: " + std::string(user->username)).c_str());
    }

    void DiscordManager::onDisconnected(int errorCode, const char* message) {
        m_connected = false;
        Logger::info(("Discord RPC Disconnected: " + std::string(message)).c_str());
    }

    void DiscordManager::onError(int errorCode, const char* message) {
        Logger::error(("Discord RPC Error: " + std::string(message)).c_str());
    }
}
