#include "RenderHook.h"
#include "StatsOverlay.h"
#include "../Utils/Logger.h"
#include <fstream>
#include <Windows.h>
#include <gl/GL.h>

static std::ofstream g_debugLog;
static bool g_hookInstalled = false;

static HMODULE g_MinHookModule = nullptr;

typedef enum {
    MH_OK = 0,
    MH_ERROR_ALREADY_INITIALIZED = 1,
    MH_ERROR_NOT_INITIALIZED = 2,
    MH_ERROR_ALREADY_CREATED = 3,
    MH_ERROR_NOT_CREATED = 4,
    MH_ERROR_ENABLED = 5,
    MH_ERROR_DISABLED = 6,
} MH_STATUS_LOCAL;

#define MH_ALL_HOOKS NULL

typedef MH_STATUS_LOCAL (WINAPI *MH_Initialize_t)(VOID);
typedef MH_STATUS_LOCAL (WINAPI *MH_Uninitialize_t)(VOID);
typedef MH_STATUS_LOCAL (WINAPI *MH_CreateHook_t)(LPVOID, LPVOID, LPVOID*);
typedef MH_STATUS_LOCAL (WINAPI *MH_EnableHook_t)(LPVOID);
typedef MH_STATUS_LOCAL (WINAPI *MH_DisableHook_t)(LPVOID);
typedef const char* (WINAPI *MH_StatusToString_t)(MH_STATUS_LOCAL);

static MH_Initialize_t pMH_Initialize = nullptr;
static MH_Uninitialize_t pMH_Uninitialize = nullptr;
static MH_CreateHook_t pMH_CreateHook = nullptr;
static MH_EnableHook_t pMH_EnableHook = nullptr;
static MH_DisableHook_t pMH_DisableHook = nullptr;
static MH_StatusToString_t pMH_StatusToString = nullptr;

typedef BOOL(WINAPI* wglSwapBuffers_t)(HDC hdc);
static wglSwapBuffers_t originalSwapBuffers = nullptr;

static void writeDebugLog(const char* msg) {
	if (!g_debugLog.is_open()) {
		char path[MAX_PATH];
		GetModuleFileNameA(NULL, path, MAX_PATH);
		std::string exePath(path);
		size_t lastSlash = exePath.find_last_of("\\/");
		std::string dir = exePath.substr(0, lastSlash);
		std::string logPath = dir + "\\ovson_render_debug.txt";
		g_debugLog.open(logPath, std::ios::app);
	}
	if (g_debugLog.is_open()) {
		SYSTEMTIME st;
		GetLocalTime(&st);
		char timeStr[64];
		sprintf_s(timeStr, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
		g_debugLog << timeStr << msg << std::endl;
		g_debugLog.flush();
	}
}

HMODULE GetCurrentModule() {
    HMODULE hModule = NULL;
    GetModuleHandleEx(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | 
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCTSTR)GetCurrentModule,
        &hModule);
    return hModule;
}
// load minhook
bool LoadMinHook() {
	writeDebugLog("Loading MinHook from embedded resources...");
	
	HMODULE hModule = GetCurrentModule();
	if (!hModule) {
		writeDebugLog("ERROR: Failed to get current module handle");
		return false;
	}
	
	char buf[256];
	sprintf_s(buf, "Current module handle: 0x%p", hModule);
	writeDebugLog(buf);

	HRSRC hRes = FindResourceA(hModule, MAKEINTRESOURCEA(101), "MINHOOK_DLL");
	if (!hRes) {
		DWORD err = GetLastError();
		sprintf_s(buf, "ERROR: MinHook resource not found (ID 101, Type MINHOOK_DLL). Error: %lu", err);
		writeDebugLog(buf);
		return false;
	}
	
	HGLOBAL hResData = LoadResource(hModule, hRes);
	if (!hResData) {
		writeDebugLog("ERROR: Failed to load MinHook resource");
		return false;
	}
	
	LPVOID pResData = LockResource(hResData);
	DWORD resSize = SizeofResource(hModule, hRes);
	
	if (!pResData || resSize == 0) {
		writeDebugLog("ERROR: Invalid resource data");
		return false;
	}
	
	sprintf_s(buf, "MinHook resource found, size: %lu bytes", resSize);
	writeDebugLog(buf);
	
	char tempPath[MAX_PATH];
	GetTempPathA(MAX_PATH, tempPath);
	std::string dllPath = std::string(tempPath) + "MinHook_" + std::to_string(GetTickCount()) + ".dll";
	
	HANDLE hFile = CreateFileA(dllPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		writeDebugLog("ERROR: Failed to create temp file");
		return false;
	}
	
	DWORD written;
	if (!WriteFile(hFile, pResData, resSize, &written, NULL) || written != resSize) {
		CloseHandle(hFile);
		writeDebugLog("ERROR: Failed to write DLL to temp");
		return false;
	}
	CloseHandle(hFile);
	
	sprintf_s(buf, "MinHook extracted to: %s", dllPath.c_str());
	writeDebugLog(buf);
	
	g_MinHookModule = LoadLibraryA(dllPath.c_str());
	if (!g_MinHookModule) {
		DWORD err = GetLastError();
		sprintf_s(buf, "ERROR: Failed to load MinHook DLL. Error: %lu", err);
		writeDebugLog(buf);
		return false;
	}
	writeDebugLog("MinHook.x64.dll loaded successfully");
	
	pMH_Initialize = (MH_Initialize_t)GetProcAddress(g_MinHookModule, "MH_Initialize");
	pMH_Uninitialize = (MH_Uninitialize_t)GetProcAddress(g_MinHookModule, "MH_Uninitialize");
	pMH_CreateHook = (MH_CreateHook_t)GetProcAddress(g_MinHookModule, "MH_CreateHook");
	pMH_EnableHook = (MH_EnableHook_t)GetProcAddress(g_MinHookModule, "MH_EnableHook");
	pMH_DisableHook = (MH_DisableHook_t)GetProcAddress(g_MinHookModule, "MH_DisableHook");
	pMH_StatusToString = (MH_StatusToString_t)GetProcAddress(g_MinHookModule, "MH_StatusToString");
	
	if (!pMH_Initialize || !pMH_CreateHook || !pMH_EnableHook) {
		writeDebugLog("ERROR: Failed to load MinHook functions");
		FreeLibrary(g_MinHookModule);
		g_MinHookModule = nullptr;
		return false;
	}
	
	writeDebugLog("All MinHook functions loaded");
	return true;
}

BOOL WINAPI hookedSwapBuffers(HDC hdc) {
	static int frameCount = 0;
	frameCount++;
	
	if (frameCount == 1) {
		writeDebugLog("wglSwapBuffers hook triggered!");
	}
	
	if (frameCount % 100 == 0) {
		char buf[128];
		sprintf_s(buf, "Rendered %d frames", frameCount);
		writeDebugLog(buf);
	}
	
	StatsOverlay::render(hdc);
	return originalSwapBuffers(hdc);
}

bool RenderHook::install()
{
	writeDebugLog("RenderHook::install() called (dynamic MinHook.dll loading)");
	Logger::info("RenderHook: Loading MinHook.dll dynamically");
	
	if (!LoadMinHook()) {
		Logger::error("RenderHook: Failed to load MinHook.dll, overlay disabled");
		return false;
	}
	
	MH_STATUS_LOCAL status = pMH_Initialize();
	if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
		char buf[256];
		sprintf_s(buf, "MH_Initialize failed: %d", (int)status);
		writeDebugLog(buf);
		Logger::error(buf);
		return false;
	}
	writeDebugLog("MinHook initialized successfully");
	
	HMODULE hOpenGL = GetModuleHandleA("opengl32.dll");
	if (!hOpenGL) {
		writeDebugLog("ERROR: opengl32.dll not found");
		return false;
	}
	writeDebugLog("opengl32.dll found");
	
	FARPROC pSwapBuffers = GetProcAddress(hOpenGL, "wglSwapBuffers");
	if (!pSwapBuffers) {
		writeDebugLog("ERROR: wglSwapBuffers not found");
		return false;
	}
	
	char buf[256];
	sprintf_s(buf, "wglSwapBuffers found at 0x%p", pSwapBuffers);
	writeDebugLog(buf);
	
	status = pMH_CreateHook(pSwapBuffers, &hookedSwapBuffers, reinterpret_cast<LPVOID*>(&originalSwapBuffers));
	if (status != MH_OK) {
		sprintf_s(buf, "MH_CreateHook failed: %d", (int)status);
		writeDebugLog(buf);
		Logger::error(buf);
		return false;
	}
	writeDebugLog("Hook created successfully");
	
	status = pMH_EnableHook(pSwapBuffers);
	if (status != MH_OK) {
		sprintf_s(buf, "MH_EnableHook failed: %d", (int)status);
		writeDebugLog(buf);
		Logger::error(buf);
		return false;
	}
	writeDebugLog("wglSwapBuffers hooked successfully!");
	g_hookInstalled = true;
	
	StatsOverlay::init();
	writeDebugLog("StatsOverlay initialized");
	
	Logger::info("RenderHook: wglSwapBuffers hook installed successfully!");
	return true;
}

void RenderHook::uninstall()
{
	writeDebugLog("RenderHook::uninstall() called");
	
	if (g_hookInstalled) {
		if (pMH_DisableHook) {
			pMH_DisableHook(MH_ALL_HOOKS);
			writeDebugLog("MinHook disabled");
		}
		
		Sleep(100);
		
		if (pMH_Uninitialize) {
			pMH_Uninitialize();
		}
		
		g_hookInstalled = false;
	}
	
	if (g_MinHookModule) {
		FreeLibrary(g_MinHookModule);
		g_MinHookModule = nullptr;
		writeDebugLog("MinHook.x64.dll unloaded");
	}
	
	StatsOverlay::shutdown();
}

void RenderHook::poll()
{
	// not used when hook is active
}
