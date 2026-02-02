#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include "Java.h"
#include "Net/Http.h"
#include <iptypes.h>
#include <iphlpapi.h>
#include <ipifcons.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")
#include <string>
#include "Utils/Logger.h"
#include "Chat/ChatInterceptor.h"
#include "Chat/ChatHook.h"
#include "Chat/ChatSDK.h"
#include "Chat/Commands.h"
#include "Config/Config.h"
#include "Render/RenderHook.h"
#include "Render/TextureLoader.h"
#include "Services/DiscordManager.h"
#include <stdio.h>
#include <stdint.h>


FILE* file = nullptr;
static HANDLE g_loadedEvent = nullptr;
static HANDLE g_sharedMap = nullptr;
static volatile LONG* g_sharedFlag = nullptr;
static HANDLE g_injectedMutex = nullptr;
static HANDLE g_aliveEvent = nullptr;

void init(void* instance) {
    { HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\OVsonCp1"); if (ev) { SetEvent(ev); CloseHandle(ev); } }
    
    jsize count = 0;
    for (int retry = 0; retry < 20; ++retry) {
        if (JNI_GetCreatedJavaVMs(&lc->vm, 1, &count) == JNI_OK && count > 0) {
            break;
        }
        Sleep(100);
    }
    
    if (count == 0 || !lc->vm) {
        return;
    }
    
    if (lc->getEnv() != nullptr) {
        Logger::initialize();
        Logger::info("OVson initialized");

        lc->GetLoadedClasses();
        Config::initialize(static_cast<HMODULE>(instance));
        BedDefense::TextureLoader::setModule(static_cast<HMODULE>(instance));
        RegisterDefaultCommands();
        ChatInterceptor::initialize();
        Logger::info("ChatHook disabled (safe mode)");

        {
            HANDLE evReady = CreateEventW(nullptr, TRUE, FALSE, L"Local\\OVsonReadyForBanner");
            if (evReady) {
                WaitForSingleObject(evReady, 10000);
                CloseHandle(evReady);
            }
            
            const char* S = "\xC2\xA7";
            std::string banner = std::string(S) + "0[" + S + "r" + S + "cO" + S + "6V" + S + "es" + S + "ao" + S + "bn" + S + "0]" + S + "r " + S + "finjected. made by sekerbenimkedim.";
            ChatSDK::showClientMessage(banner);
            HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, L"Local\\OVsonInjected");
            if (ev) {
                SetEvent(ev);
                CloseHandle(ev);
            }
        }

        Sleep(1000);
        Logger::info("Installing RenderHook after delay...");
        try {
            if (!RenderHook::install()) {
                Logger::error("RenderHook: Failed to install, overlay disabled");
            } else {
                Logger::info("RenderHook: Successfully installed!");
            }
        } catch (...) {
            Logger::error("RenderHook: Exception during installation, overlay disabled");
        }

        bool wasEndDown = false;
        while (true) {
            SHORT endState = GetAsyncKeyState(VK_END);
            bool isEndDown = (endState & 0x8000) != 0;
            if (!wasEndDown && isEndDown) {
                ChatSDK::showClientMessage(ChatSDK::formatPrefix() + std::string("quitting..."));
                break;
            }
            wasEndDown = isEndDown;
            ChatInterceptor::poll();
            RenderHook::poll();
            Services::DiscordManager::getInstance()->update();
            Sleep(5);
        }
    }

    RenderHook::uninstall();
    Sleep(200);

    ChatInterceptor::shutdown();
    
    try {
        Services::DiscordManager::getInstance()->shutdown();
    } catch (...) {}
    Sleep(100);

    Logger::shutdown();
    if (file) { fclose(file); file = nullptr; }

    FreeLibraryAndExitThread(static_cast<HMODULE>(instance), 0);
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    DisableThreadLibraryCalls(hModule);

    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        g_loadedEvent = CreateEventW(nullptr, TRUE, TRUE, L"Global\\OVsonLoaded");
        g_sharedMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(LONG), L"Global\\OVsonShared");
        if (g_sharedMap) {
            g_sharedFlag = (volatile LONG*)MapViewOfFile(g_sharedMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LONG));
            if (g_sharedFlag) { InterlockedExchange((LONG*)g_sharedFlag, 1); }
        }
        g_injectedMutex = CreateMutexW(nullptr, FALSE, L"Global\\OVsonMutex");
        {
            wchar_t name[64];
            wsprintfW(name, L"Global\\OVsonAlive_%lu", GetCurrentProcessId());
            g_aliveEvent = CreateEventW(nullptr, TRUE, TRUE, name);
        }
        
        {
        HANDLE hThread = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(init), hModule, 0, nullptr);
        if (hThread) {
            CloseHandle(hThread);
            }
        }
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        if (g_sharedFlag) { UnmapViewOfFile((LPCVOID)g_sharedFlag); g_sharedFlag = nullptr; }
        if (g_sharedMap) { CloseHandle(g_sharedMap); g_sharedMap = nullptr; }
        if (g_loadedEvent) { CloseHandle(g_loadedEvent); g_loadedEvent = nullptr; }
        if (g_injectedMutex) { CloseHandle(g_injectedMutex); g_injectedMutex = nullptr; }
        if (g_aliveEvent) { CloseHandle(g_aliveEvent); g_aliveEvent = nullptr; }
        break;
    }
    return TRUE;
}

