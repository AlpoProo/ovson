#pragma once
#include <Windows.h>
#include <vector>
#include <string>

#define ID_TIMER_PROGRESS 1001
#define ID_TIMER_CLOSE 1002
#define WM_APP_PROGRESS (WM_APP + 2)

struct InjectContext
{
    HWND hwnd;
    std::wstring dllPath;
    std::vector<uint8_t> dllBytes;
    std::wstring cpNames[6];
};

bool getEmbeddedDllBytes(std::vector<uint8_t>& outBytes);
DWORD WINAPI InjectThread(LPVOID param);
bool manualMapInject(DWORD pid, const uint8_t* image, size_t imageSize);
bool simpleLoadLibraryInject(DWORD pid, const wchar_t* dllPath);
bool injectDll(DWORD pid, const std::wstring& dllPath);
