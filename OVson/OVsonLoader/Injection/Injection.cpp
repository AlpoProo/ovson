#include "Injection.h"
#include "Utils.h"
#include <TlHelp32.h>
#include <stdio.h>

extern HANDLE g_loaderMutex;
extern HANDLE g_loaderMutex; 

bool getEmbeddedDllBytes(std::vector<uint8_t>& outBytes)
{
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(10));
    if (!res) return false;
    HGLOBAL h = LoadResource(nullptr, res);
    if (!h) return false;
    DWORD sz = SizeofResource(nullptr, res);
    void* data = LockResource(h);
    if (!data || sz == 0) return false;
    outBytes.resize(sz);
    memcpy(outBytes.data(), data, sz);
    return true;
}

static DWORD rvaToFileOffset(DWORD rva, const IMAGE_NT_HEADERS64* nt, size_t imageSize)
{
    if (rva == 0) return 0;
    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        if (rva >= sections[i].VirtualAddress && rva < sections[i].VirtualAddress + sections[i].Misc.VirtualSize)
        {
            DWORD offsetInSection = rva - sections[i].VirtualAddress;
            DWORD fileOffset = sections[i].PointerToRawData + offsetInSection;
            if (fileOffset < imageSize) return fileOffset;
        }
    }
    return 0;
}

bool simpleLoadLibraryInject(DWORD pid, const wchar_t* dllPath)
{
    HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return false;
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC pLoadLibrary = GetProcAddress(hKernel32, "LoadLibraryW");
    if (!pLoadLibrary) { CloseHandle(hProc); return false; }
    SIZE_T pathLen = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    LPVOID remotePath = VirtualAllocEx(hProc, nullptr, pathLen, MEM_COMMIT, PAGE_READWRITE);
    if (!remotePath) { CloseHandle(hProc); return false; }
    SIZE_T written = 0;
    if (!WriteProcessMemory(hProc, remotePath, dllPath, pathLen, &written))
    {
        VirtualFreeEx(hProc, remotePath, 0, MEM_RELEASE); CloseHandle(hProc); return false;
    }
    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, (LPTHREAD_START_ROUTINE)pLoadLibrary, remotePath, 0, nullptr);
    if (!hThread) { VirtualFreeEx(hProc, remotePath, 0, MEM_RELEASE); CloseHandle(hProc); return false; }
    WaitForSingleObject(hThread, 5000);
    HMODULE hRemoteMod = nullptr;
    GetExitCodeThread(hThread, (LPDWORD)&hRemoteMod);
    CloseHandle(hThread);
    VirtualFreeEx(hProc, remotePath, 0, MEM_RELEASE);
    CloseHandle(hProc);
    return hRemoteMod != nullptr;
}

bool manualMapInject(DWORD pid, const uint8_t* image, size_t imageSize)
{    
    if (!image || imageSize < 0x1000) return false;
    HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return false;

    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)image;
    if (dos->e_magic != 0x5A4D) { CloseHandle(hProc); return false; }
    const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)(image + dos->e_lfanew);
    if (nt->Signature != 0x4550) { CloseHandle(hProc); return false; }

    SIZE_T imageSizeAligned = (nt->OptionalHeader.SizeOfImage + 0xFFF) & ~0xFFF;
    LPVOID remoteBase = VirtualAllocEx(hProc, nullptr, imageSizeAligned, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remoteBase) { CloseHandle(hProc); return false; }

    SIZE_T written = 0;
    WriteProcessMemory(hProc, remoteBase, image, nt->OptionalHeader.SizeOfHeaders, &written);

    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        if (sec[i].SizeOfRawData == 0) continue;
        LPVOID secDest = (BYTE*)remoteBase + sec[i].VirtualAddress;
        const void* secSrc = image + sec[i].PointerToRawData;
        WriteProcessMemory(hProc, secDest, secSrc, sec[i].SizeOfRawData, &written);
    }

    DWORD relocRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
    if (relocRva) {
        DWORD_PTR delta = (DWORD_PTR)remoteBase - nt->OptionalHeader.ImageBase;
        if (delta) {
             const IMAGE_BASE_RELOCATION* reloc = (const IMAGE_BASE_RELOCATION*)(image + relocRva);
             while (reloc->VirtualAddress) {
                 WORD* relocData = (WORD*)((BYTE*)reloc + sizeof(IMAGE_BASE_RELOCATION));
                 DWORD count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                 for (DWORD i=0; i<count; ++i) {
                     if ((relocData[i] >> 12) == IMAGE_REL_BASED_DIR64) {
                         LPVOID addr = (BYTE*)remoteBase + reloc->VirtualAddress + (relocData[i] & 0xFFF);
                         DWORD_PTR val = 0; SIZE_T rd=0;
                         if (ReadProcessMemory(hProc, addr, &val, sizeof(val), &rd)) {
                             val += delta;
                             WriteProcessMemory(hProc, addr, &val, sizeof(val), &written);
                         }
                     }
                 }
                 reloc = (const IMAGE_BASE_RELOCATION*)((BYTE*)reloc + reloc->SizeOfBlock);
                 if (!reloc->SizeOfBlock) break;
             }
        }
    }

    unsigned char shellcode[] = {
        0x48, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rcx, hInstance
        0x48, 0xC7, 0xC2, 0x01, 0x00, 0x00, 0x00,                   // mov rdx, 1
        0x49, 0xC7, 0xC0, 0x00, 0x00, 0x00, 0x00,                   // mov r8, 0
        0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rax, entry
        0xFF, 0xD0, 0xC3
    };
    *(DWORD_PTR*)(shellcode + 2) = (DWORD_PTR)remoteBase;
    *(DWORD_PTR*)(shellcode + 19) = (DWORD_PTR)remoteBase + nt->OptionalHeader.AddressOfEntryPoint;
    
    LPVOID scMem = VirtualAllocEx(hProc, nullptr, sizeof(shellcode), MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    WriteProcessMemory(hProc, scMem, shellcode, sizeof(shellcode), &written);
    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, (LPTHREAD_START_ROUTINE)scMem, nullptr, 0, nullptr);
    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    VirtualFreeEx(hProc, scMem, 0, MEM_RELEASE);
    CloseHandle(hProc);
    return true;
}

bool injectDll(DWORD pid, const std::wstring& dllPath)
{
    return simpleLoadLibraryInject(pid, dllPath.c_str());
}

DWORD WINAPI InjectThread(LPVOID param)
{
    InjectContext* ctx = (InjectContext*)param;
    
    PostMessageW(ctx->hwnd, WM_APP_PROGRESS, 5, 0);
    PostMessageW(ctx->hwnd, (WM_APP + 5), 0, (LPARAM)L"Searching for Minecraft..."); 

    for (int i = 0; i < 100; ++i) {
        if (findProcessId(L"javaw.exe")) break;
        Sleep(50);
        if (i % 10 == 0) PostMessageW(ctx->hwnd, WM_APP_PROGRESS, 5 + i/5, 0);
    }
    DWORD pid = findProcessId(L"javaw.exe");
    if (!pid) {
        if (g_loaderMutex) { ReleaseMutex(g_loaderMutex); CloseHandle(g_loaderMutex); g_loaderMutex = nullptr; }
        PostMessageW(ctx->hwnd, WM_APP + 4, 0, 0);
        return 0;
    }
    
    PostMessageW(ctx->hwnd, WM_APP_PROGRESS, 30, 0);
    PostMessageW(ctx->hwnd, (WM_APP + 5), 0, (LPARAM)L"Process Found"); 

    if (isAlreadyInjected(pid)) {
        if (g_loaderMutex) { ReleaseMutex(g_loaderMutex); CloseHandle(g_loaderMutex); g_loaderMutex = nullptr; }
        PostMessageW(ctx->hwnd, WM_APP + 3, 0, 0);
        return 0;
    }

    PostMessageW(ctx->hwnd, WM_APP_PROGRESS, 40, 0); 
    PostMessageW(ctx->hwnd, (WM_APP + 5), 0, (LPARAM)L"Preparing Payload..."); 

    bool ok = false;
    if (!ctx->dllBytes.empty()) {
        PostMessageW(ctx->hwnd, WM_APP_PROGRESS, 50, 0);
        patchPerUserWatermark(ctx->dllBytes, L"");
        
        wchar_t tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);
        std::wstring dllPath = std::wstring(tempPath) + L"OVson_" + std::to_wstring(GetTickCount()) + L".dll";
        
        FILE* f = nullptr;
        _wfopen_s(&f, dllPath.c_str(), L"wb");
        if (f) {
            fwrite(ctx->dllBytes.data(), 1, ctx->dllBytes.size(), f);
            fclose(f);
            
            PostMessageW(ctx->hwnd, WM_APP_PROGRESS, 60, 0); 
            PostMessageW(ctx->hwnd, (WM_APP + 5), 0, (LPARAM)L"Injecting DLL..."); 
            
            ok = simpleLoadLibraryInject(pid, dllPath.c_str());
            
            if (ok) { 
                PostMessageW(ctx->hwnd, WM_APP_PROGRESS, 80, 0); 
                PostMessageW(ctx->hwnd, (WM_APP + 5), 0, (LPARAM)L"Cleaning up..."); 
                Sleep(2000); 
                DeleteFileW(dllPath.c_str()); 
            }
        }
    }
    
    if (!ok) {
        if (g_loaderMutex) { ReleaseMutex(g_loaderMutex); CloseHandle(g_loaderMutex); g_loaderMutex = nullptr; }
        PostMessageW(ctx->hwnd, WM_APP + 1, 0, 0);
        return 0;
    }
    
    PostMessageW(ctx->hwnd, WM_APP_PROGRESS, 90, 0); 
    PostMessageW(ctx->hwnd, (WM_APP + 5), 0, (LPARAM)L"Waiting for Initialization..."); 

    if (g_loaderMutex) { ReleaseMutex(g_loaderMutex); CloseHandle(g_loaderMutex); g_loaderMutex = nullptr; }
    
    HANDLE evInjected = CreateEventW(nullptr, TRUE, FALSE, L"Local\\OVsonInjected");
    if (evInjected) {
        WaitForSingleObject(evInjected, 3000);
        PostMessageW(ctx->hwnd, WM_APP_PROGRESS, 100, 0);
        PostMessageW(ctx->hwnd, WM_APP + 1, 1, 0);
        CloseHandle(evInjected);
    } else {
        Sleep(3500);
        PostMessageW(ctx->hwnd, WM_APP_PROGRESS, 100, 0);
        PostMessageW(ctx->hwnd, WM_APP + 1, 1, 0);
    }
    return 0;
}
