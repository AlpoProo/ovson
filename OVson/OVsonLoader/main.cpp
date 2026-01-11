#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#include <Windows.h>
#include <iptypes.h>
#include <iphlpapi.h>
#include <ipifcons.h>
#ifndef AF_UNSPEC
#define AF_UNSPEC 0
#endif
#include <TlHelp32.h>
#include <CommCtrl.h>
#pragma comment(lib, "Comctl32.lib")
#include <bcrypt.h>
#pragma comment(lib, "Bcrypt.lib")
#include <WinInet.h>
#pragma comment(lib, "WinInet.lib")
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

static const char kExpectedDllSha256Hex[] = ""; // removed

#include <string>
#include <vector>
#include <stdint.h>
#include <intrin.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <conio.h>

#ifdef wprintf
#undef wprintf
#endif
#ifdef fflush
#undef fflush
#endif
#ifdef _getch
#undef _getch
#endif
#define wprintf(...) ((void)0)
#define fflush(...) ((void)0)
#define _getch() (0)
// login.h removed
// extended anti-debug checks using ntdll removed
typedef NTSTATUS(NTAPI *PFN_NtQueryInformationProcess)(HANDLE, ULONG, PVOID, ULONG, PULONG);
static bool isDebuggedExtended()
{
    return false; // removed
}

static std::wstring decodeXorW(const uint16_t *enc, size_t n, uint8_t key)
{
    std::wstring s;
    s.resize(n);
    for (size_t i = 0; i < n; ++i)
        s[i] = (wchar_t)(enc[i] ^ key);
    return s;
}

static bool postJsonW(const std::wstring &url, const std::string &json, std::string &out)
{
    std::wstring host, path;
    INTERNET_PORT port = 0;
    bool https = false;
    auto parse = [&]()
    {
        std::string u(url.begin(), url.end());
        if (u.rfind("https://",0)==0){ https=true; u=u.substr(8); port=443; }
        else if (u.rfind("http://",0)==0){ https=false; u=u.substr(7); port=80; }
        else return false;
        size_t slash=u.find('/'); std::string hp = (slash==std::string::npos)?u:u.substr(0,slash);
        std::string pth = (slash==std::string::npos)?"/":u.substr(slash);
        size_t colon=hp.find(':'); if(colon!=std::string::npos){ port=(INTERNET_PORT)atoi(hp.substr(colon+1).c_str()); hp=hp.substr(0,colon);} 
        host.assign(hp.begin(), hp.end()); path.assign(pth.begin(), pth.end()); return !host.empty(); };
    if (!parse())
        return false;
    HINTERNET s = WinHttpOpen(L"OVson/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s)
        return false;
    HINTERNET c = WinHttpConnect(s, host.c_str(), port, 0);
    if (!c)
    {
        WinHttpCloseHandle(s);
        return false;
    }
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET r = WinHttpOpenRequest(c, L"POST", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!r)
    {
        WinHttpCloseHandle(c);
        WinHttpCloseHandle(s);
        return false;
    }
    std::wstring hdr = L"Content-Type: application/json\r\n";
    BOOL ok = WinHttpSendRequest(r, hdr.c_str(), (DWORD)-1L, (LPVOID)json.data(), (DWORD)json.size(), (DWORD)json.size(), 0);
    if (!ok)
    {
        WinHttpCloseHandle(r);
        WinHttpCloseHandle(c);
        WinHttpCloseHandle(s);
        return false;
    }
    ok = WinHttpReceiveResponse(r, NULL);
    if (!ok)
    {
        WinHttpCloseHandle(r);
        WinHttpCloseHandle(c);
        WinHttpCloseHandle(s);
        return false;
    }
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    if (status != 200)
    {
        WinHttpCloseHandle(r);
        WinHttpCloseHandle(c);
        WinHttpCloseHandle(s);
        return false;
    }
    out.clear();
    for (;;)
    {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(r, &avail) || avail == 0)
            break;
        std::string ch;
        ch.resize(avail);
        DWORD rd = 0;
        if (!WinHttpReadData(r, &ch[0], avail, &rd) || rd == 0)
            break;
        out.append(ch.data(), rd);
    }
    WinHttpCloseHandle(r);
    WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return true;
}
static bool sha256Cng(const uint8_t *data, ULONG len, uint8_t out[32]);

#define ID_TIMER_PROGRESS 1001
#define ID_TIMER_CLOSE 1002
#define WM_APP_PROGRESS (WM_APP + 2)

static DWORD findProcessId(const std::wstring &exeName)
{
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0)
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static bool isModuleLoaded(DWORD pid, const std::wstring &moduleName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE)
        return false;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snap, &me))
    {
        do
        {
            if (_wcsicmp(me.szModule, moduleName.c_str()) == 0)
            {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

static bool isOVsonModuleLoaded(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE)
        return false;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snap, &me))
    {
        do
        {
            if (_wcsnicmp(me.szModule, L"OVson", 5) == 0)
            {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

static bool getEmbeddedDllBytes(std::vector<uint8_t> &outBytes)
{
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(10));
    if (!res)
        return false;
    HGLOBAL h = LoadResource(nullptr, res);
    if (!h)
        return false;
    DWORD sz = SizeofResource(nullptr, res);
    void *data = LockResource(h);
    if (!data || sz == 0)
        return false;
    outBytes.resize(sz);
    memcpy(outBytes.data(), data, sz);
    return true;
}

static void verifyEmbeddedDllHashOrExit()
{
    return;
}

static bool extractEmbeddedDll(std::wstring &outPath)
{
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(10));
    if (!res)
        return false;
    HGLOBAL h = LoadResource(nullptr, res);
    if (!h)
        return false;
    DWORD sz = SizeofResource(nullptr, res);
    void *data = LockResource(h);
    if (!data || sz == 0)
        return false;
    wchar_t tempPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tempPath))
        return false;
    std::wstring dir = std::wstring(tempPath) + L"OVson";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring finalPath = dir + L"\\OVson.dll";
    HANDLE f = CreateFileW(finalPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    BOOL ok = WriteFile(f, data, sz, &written, nullptr);
    CloseHandle(f);
    if (!ok || written != sz)
    {
        DeleteFileW(finalPath.c_str());
        return false;
    }
    if (kExpectedDllSha256Hex[0])
    {
        HANDLE rf = CreateFileW(finalPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (rf == INVALID_HANDLE_VALUE)
        {
            DeleteFileW(finalPath.c_str());
            return false;
        }
        LARGE_INTEGER fsz{};
        if (!GetFileSizeEx(rf, &fsz))
        {
            CloseHandle(rf);
            DeleteFileW(finalPath.c_str());
            return false;
        }
        const size_t BUFSZ = 1 << 16; // 64KB
        uint8_t *buf = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, BUFSZ);
        if (!buf)
        {
            CloseHandle(rf);
            DeleteFileW(finalPath.c_str());
            return false;
        }
        // minimal sha256 (same as dll)
        struct ShaCtx
        {
            uint32_t s[8];
            uint8_t b[64];
            size_t filled;
            uint64_t bits;
        };
        auto rotr = [&](uint32_t x, uint32_t n)
        { return (x >> n) | (x << (32 - n)); };
        auto ch = [&](uint32_t x, uint32_t y, uint32_t z)
        { return (x & y) ^ (~x & z); };
        auto maj = [&](uint32_t x, uint32_t y, uint32_t z)
        { return (x & y) ^ (x & z) ^ (y & z); };
        auto bs0 = [&](uint32_t x)
        { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); };
        auto bs1 = [&](uint32_t x)
        { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); };
        auto ss0 = [&](uint32_t x)
        { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); };
        auto ss1 = [&](uint32_t x)
        { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); };
        const uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        auto tf = [&](uint32_t s[8], const uint8_t blk[64])
        {
            uint32_t w[64];
            for(int i=0;i<16;++i){w[i]=(uint32_t)blk[i*4]<<24|(uint32_t)blk[i*4+1]<<16|(uint32_t)blk[i*4+2]<<8|(uint32_t)blk[i*4+3];}
            for(int i=16;i<64;++i)w[i]=ss1(w[i-2])+w[i-7]+ss0(w[i-15])+w[i-16];
            uint32_t a=s[0],b=s[1],c=s[2],d=s[3],e=s[4],f=s[5],g=s[6],h=s[7];
            for(int i=0;i<64;++i){uint32_t t1=h+bs1(e)+ch(e,f,g)+K[i]+w[i];uint32_t t2=bs0(a)+maj(a,b,c);h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
            s[0]+=a; s[1]+=b; s[2]+=c; s[3]+=d; s[4]+=e; s[5]+=f; s[6]+=g; s[7]+=h; };
        ShaCtx C{{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}, {0}, 0, 0};
        DWORD rd = 0;
        while (ReadFile(rf, buf, BUFSZ, &rd, nullptr) && rd)
        {
            const uint8_t *p = buf;
            DWORD n = rd;
            while (n)
            {
                size_t tc = n;
                if (tc > 64 - C.filled)
                    tc = 64 - C.filled;
                memcpy(C.b + C.filled, p, tc);
                C.filled += tc;
                p += tc;
                n -= (DWORD)tc;
                C.bits += (uint64_t)tc * 8ULL;
                if (C.filled == 64)
                {
                    tf(C.s, C.b);
                    C.filled = 0;
                }
            }
        }
        // pad
        C.b[C.filled++] = 0x80;
        if (C.filled > 56)
        {
            while (C.filled < 64)
                C.b[C.filled++] = 0;
            tf(C.s, C.b);
            C.filled = 0;
        }
        while (C.filled < 56)
            C.b[C.filled++] = 0;
        for (int i = 7; i >= 0; --i)
            C.b[C.filled++] = (uint8_t)((C.bits >> (i * 8)) & 0xFF);
        tf(C.s, C.b);
        uint8_t dig[32];
        for (int i = 0; i < 8; ++i)
        {
            dig[i * 4] = (C.s[i] >> 24) & 0xFF;
            dig[i * 4 + 1] = (C.s[i] >> 16) & 0xFF;
            dig[i * 4 + 2] = (C.s[i] >> 8) & 0xFF;
            dig[i * 4 + 3] = (C.s[i]) & 0xFF;
        }
        HeapFree(GetProcessHeap(), 0, buf);
        CloseHandle(rf);
        char hex[65];
        static const char *hexd = "0123456789abcdef";
        for (int i = 0; i < 32; ++i)
        {
            hex[i * 2] = hexd[(dig[i] >> 4) & 0xF];
            hex[i * 2 + 1] = hexd[dig[i] & 0xF];
        }
        hex[64] = '\0';
    }
    outPath = finalPath;
    return true;
}

static bool simpleLoadLibraryInject(DWORD pid, const wchar_t *dllPath)
{
    HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!hProc)
    {
        return false;
    }

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC pLoadLibrary = GetProcAddress(hKernel32, "LoadLibraryW");
    if (!pLoadLibrary)
    {
        CloseHandle(hProc);
        return false;
    }

    SIZE_T pathLen = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    LPVOID remotePath = VirtualAllocEx(hProc, nullptr, pathLen, MEM_COMMIT, PAGE_READWRITE);
    if (!remotePath)
    {
        CloseHandle(hProc);
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(hProc, remotePath, dllPath, pathLen, &written))
    {
        VirtualFreeEx(hProc, remotePath, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, (LPTHREAD_START_ROUTINE)pLoadLibrary, remotePath, 0, nullptr);
    if (!hThread)
    {
        VirtualFreeEx(hProc, remotePath, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    WaitForSingleObject(hThread, 5000);
    HMODULE hRemoteMod = nullptr;
    GetExitCodeThread(hThread, (LPDWORD)&hRemoteMod);
    CloseHandle(hThread);
    VirtualFreeEx(hProc, remotePath, 0, MEM_RELEASE);
    CloseHandle(hProc);

    if (!hRemoteMod)
    {
        return false;
    }

    return true;
}

static DWORD rvaToFileOffset(DWORD rva, const IMAGE_NT_HEADERS64 *nt, size_t imageSize)
{
    if (rva == 0)
        return 0;

    const IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        if (rva >= sections[i].VirtualAddress &&
            rva < sections[i].VirtualAddress + sections[i].Misc.VirtualSize)
        {
            DWORD offsetInSection = rva - sections[i].VirtualAddress;
            DWORD fileOffset = sections[i].PointerToRawData + offsetInSection;
            if (fileOffset < imageSize)
            {
                return fileOffset;
            }
        }
    }
    return 0;
}

static bool manualMapInject(DWORD pid, const uint8_t *image, size_t imageSize)
{
    if (!image || imageSize < 0x1000)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!hProc)
    {
        wprintf(L"[manualMapInject] ERROR: OpenProcess failed for PID %lu, GetLastError()=%lu\n", pid, GetLastError());
        return false;
    }

    // Parse PE headers
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
    if (dos->e_magic != 0x5A4D)
    { // 'MZ'
        wprintf(L"[manualMapInject] ERROR: Invalid DOS header\n");
        CloseHandle(hProc);
        SetLastError(ERROR_BAD_FORMAT);
        return false;
    }

    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(image + dos->e_lfanew);
    if (nt->Signature != 0x00004550)
    { // 'PE\0\0'
        wprintf(L"[manualMapInject] ERROR: Invalid PE signature\n");
        CloseHandle(hProc);
        SetLastError(ERROR_BAD_FORMAT);
        return false;
    }

    bool is64bit = (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    if (!is64bit && nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        wprintf(L"[manualMapInject] ERROR: Unsupported PE format (magic=0x%x)\n", nt->OptionalHeader.Magic);
        CloseHandle(hProc);
        SetLastError(ERROR_BAD_FORMAT);
        return false;
    }

    SIZE_T imageSizeAligned = (nt->OptionalHeader.SizeOfImage + 0xFFF) & ~0xFFF;
    wprintf(L"[manualMapInject] PE parsed: ImageBase=0x%llx, SizeOfImage=0x%x, aligned=%zu\n",
            (unsigned long long)nt->OptionalHeader.ImageBase, nt->OptionalHeader.SizeOfImage, imageSizeAligned);

    LPVOID remoteBase = VirtualAllocEx(hProc, nullptr, imageSizeAligned, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remoteBase)
    {
        DWORD err = GetLastError();
        wprintf(L"[manualMapInject] ERROR: VirtualAllocEx failed, GetLastError()=%lu (0x%lx)\n", err, err);
        CloseHandle(hProc);
        return false;
    }
    wprintf(L"[manualMapInject] Allocated memory in target: %p, size=%zu\n", remoteBase, imageSizeAligned);

    SIZE_T written = 0;
    if (!WriteProcessMemory(hProc, remoteBase, image, nt->OptionalHeader.SizeOfHeaders, &written))
    {
        DWORD err = GetLastError();
        wprintf(L"[manualMapInject] ERROR: WriteProcessMemory (headers) failed, GetLastError()=%lu (0x%lx)\n", err, err);
        VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }
    wprintf(L"[manualMapInject] Headers written: %zu bytes\n", written);

    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        if (sec[i].SizeOfRawData == 0)
            continue;

        LPVOID secDest = (BYTE *)remoteBase + sec[i].VirtualAddress;
        const void *secSrc = image + sec[i].PointerToRawData;

        if (!WriteProcessMemory(hProc, secDest, secSrc, sec[i].SizeOfRawData, &written))
        {
            DWORD err = GetLastError();
            wprintf(L"[manualMapInject] ERROR: WriteProcessMemory (section %d) failed, GetLastError()=%lu (0x%lx)\n", i, err, err);
            VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return false;
        }
        wprintf(L"[manualMapInject] Section %d (%hs) written: %zu bytes to %p\n", i, sec[i].Name, written, secDest);
    }

    DWORD relocRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
    DWORD relocSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;

    if (relocRva && relocSize)
    {
        DWORD_PTR delta = (DWORD_PTR)remoteBase - nt->OptionalHeader.ImageBase;
        if (delta)
        {
            const IMAGE_BASE_RELOCATION *reloc = (const IMAGE_BASE_RELOCATION *)(image + relocRva);
            while (reloc->VirtualAddress && reloc->SizeOfBlock)
            {
                WORD *relocData = (WORD *)((BYTE *)reloc + sizeof(IMAGE_BASE_RELOCATION));
                DWORD relocCount = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);

                for (DWORD i = 0; i < relocCount; ++i)
                {
                    WORD type = relocData[i] >> 12;
                    WORD offset = relocData[i] & 0xFFF;

                    if (type == IMAGE_REL_BASED_DIR64 || type == IMAGE_REL_BASED_HIGHLOW || type == IMAGE_REL_BASED_HIGH)
                    {
                        LPVOID relocAddr = (BYTE *)remoteBase + reloc->VirtualAddress + offset;
                        DWORD_PTR value = 0;
                        SIZE_T read = 0;

                        if (ReadProcessMemory(hProc, relocAddr, &value, sizeof(value), &read) && read == sizeof(value))
                        {
                            value += delta;
                            if (!WriteProcessMemory(hProc, relocAddr, &value, sizeof(value), &written))
                            {
                                wprintf(L"[manualMapInject] WARNING: Failed to write relocation at %p\n", relocAddr);
                            }
                        }
                    }
                }

                reloc = (const IMAGE_BASE_RELOCATION *)((BYTE *)reloc + reloc->SizeOfBlock);
                if ((BYTE *)reloc - (image + relocRva) >= (ptrdiff_t)relocSize)
                    break;
            }
            wprintf(L"[manualMapInject] Relocations processed (delta=0x%llx)\n", (unsigned long long)delta);
        }
    }

    DWORD importRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    DWORD importSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;

    if (importRva && importSize)
    {
        wprintf(L"[manualMapInject] Processing imports (RVA=0x%x, Size=0x%x)...\n", importRva, importSize);
        fflush(stdout);
        DWORD importFileOffset = 0;
        bool foundSection = false;
        const IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(nt);

        wprintf(L"[manualMapInject] Converting import RVA 0x%x to file offset...\n", importRva);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            if (importRva >= sections[i].VirtualAddress &&
                importRva < sections[i].VirtualAddress + sections[i].Misc.VirtualSize)
            {
                DWORD offsetInSection = importRva - sections[i].VirtualAddress;
                importFileOffset = sections[i].PointerToRawData + offsetInSection;
                wprintf(L"[manualMapInject] Found in section %hs: VA=0x%x, Raw=0x%x, offset=0x%x, fileOffset=0x%x\n",
                        sections[i].Name, sections[i].VirtualAddress, sections[i].PointerToRawData,
                        offsetInSection, importFileOffset);
                fflush(stdout);
                foundSection = true;
                break;
            }
        }

        if (!foundSection || importFileOffset >= imageSize){
            wprintf(L"[manualMapInject] ERROR: Cannot map import RVA to file offset\n");
            wprintf(L"[manualMapInject]   foundSection=%d, fileOffset=0x%x, imageSize=0x%llx\n",
                    foundSection, importFileOffset, (unsigned long long)imageSize);
            wprintf(L"[manualMapInject] CRITICAL: Import table inaccessible. Failing to trigger LoadLibrary fallback.\n");
            fflush(stdout);
            VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
            CloseHandle(hProc);
            SetLastError(ERROR_BAD_FORMAT);
            return false;
        }

        HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
        if (!hKernel32)
        {
            wprintf(L"[manualMapInject] ERROR: kernel32.dll not found in loader process\n");
            fflush(stdout);
            VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return false;
        }

        FARPROC pLoadLibrary = GetProcAddress(hKernel32, "LoadLibraryA");
        FARPROC pGetProcAddress = GetProcAddress(hKernel32, "GetProcAddress");

        if (!pLoadLibrary || !pGetProcAddress)
        {
            wprintf(L"[manualMapInject] ERROR: Failed to get LoadLibraryA/GetProcAddress\n");
            fflush(stdout);
            VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return false;
        }

        const IMAGE_IMPORT_DESCRIPTOR *impDesc = (const IMAGE_IMPORT_DESCRIPTOR *)(image + importFileOffset);
        DWORD importCount = 0;

        wprintf(L"[manualMapInject] Import descriptor at offset 0x%x in image buffer\n", importRva);
        wprintf(L"[manualMapInject] First descriptor: Name=0x%x, FirstThunk=0x%x, OriginalFirstThunk=0x%x\n",
                impDesc->Name, impDesc->FirstThunk, impDesc->OriginalFirstThunk);
        fflush(stdout);

        if (impDesc->Name != 0 && (impDesc->Name >= imageSize || impDesc->FirstThunk >= imageSize))
        {
            wprintf(L"[manualMapInject] ERROR: First import descriptor has invalid RVAs\n");
            wprintf(L"[manualMapInject]   Name=0x%x (should be < 0x%llx)\n", impDesc->Name, (unsigned long long)imageSize);
            wprintf(L"[manualMapInject]   FirstThunk=0x%x (should be < 0x%llx)\n", impDesc->FirstThunk, (unsigned long long)imageSize);
            wprintf(L"[manualMapInject] CRITICAL: Import table corrupt, cannot resolve imports. Failing to force LoadLibrary fallback.\n");
            fflush(stdout);
            VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
            CloseHandle(hProc);
            SetLastError(ERROR_BAD_FORMAT);
            return false;
        }

        wprintf(L"[manualMapInject] Starting import resolution loop...\n");
        fflush(stdout);

        while (impDesc->Name && impDesc->FirstThunk)
        {
            if ((BYTE *)impDesc - image >= (ptrdiff_t)imageSize)
            {
                wprintf(L"[manualMapInject] ERROR: impDesc out of bounds (offset=0x%llx, imageSize=0x%llx)\n",
                        (unsigned long long)((BYTE *)impDesc - image), (unsigned long long)imageSize);
                fflush(stdout);
                break;
            }
            if (impDesc->Name >= imageSize)
            {
                wprintf(L"[manualMapInject] ERROR: DLL Name RVA out of bounds (0x%x >= 0x%llx), breaking loop\n",
                        impDesc->Name, (unsigned long long)imageSize);
                fflush(stdout);
                break;
            }

            DWORD dllNameOffset = rvaToFileOffset(impDesc->Name, nt, imageSize);
            if (!dllNameOffset)
            {
                wprintf(L"[manualMapInject] ERROR: Cannot convert DLL Name RVA 0x%x to file offset, skipping\n", impDesc->Name);
                fflush(stdout);
                impDesc++;
                continue;
            }

            const char *dllName = (const char *)(image + dllNameOffset);
            wprintf(L"[manualMapInject] Processing import DLL %lu: %hs (Name RVA=0x%x, file offset=0x%x)\n",
                    importCount + 1, dllName, impDesc->Name, dllNameOffset);
            fflush(stdout);

            SIZE_T dllNameLen = strlen(dllName) + 1;
            wprintf(L"[manualMapInject] Allocating %zu bytes for DLL name in remote process...\n", dllNameLen);
            fflush(stdout);
            LPVOID remoteDllName = VirtualAllocEx(hProc, nullptr, dllNameLen, MEM_COMMIT, PAGE_READWRITE);
            if (!remoteDllName)
            {
                DWORD err = GetLastError();
                wprintf(L"[manualMapInject] ERROR: Failed to allocate memory for DLL name, GetLastError()=%lu\n", err);
                fflush(stdout);
                VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
                CloseHandle(hProc);
                return false;
            }
            wprintf(L"[manualMapInject] Allocated memory at %p\n", remoteDllName);
            fflush(stdout);

            // Write DLL name
            SIZE_T written = 0;
            wprintf(L"[manualMapInject] Writing DLL name to remote memory...\n");
            fflush(stdout);
            if (!WriteProcessMemory(hProc, remoteDllName, dllName, dllNameLen, &written))
            {
                DWORD err = GetLastError();
                wprintf(L"[manualMapInject] ERROR: Failed to write DLL name, GetLastError()=%lu\n", err);
                fflush(stdout);
                VirtualFreeEx(hProc, remoteDllName, 0, MEM_RELEASE);
                VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
                CloseHandle(hProc);
                return false;
            }
            wprintf(L"[manualMapInject] Wrote %zu bytes\n", written);
            fflush(stdout);

            wprintf(L"[manualMapInject] Calling LoadLibraryA in remote process (pLoadLibrary=0x%llx)...\n", (unsigned long long)pLoadLibrary);
            fflush(stdout);
            HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, (LPTHREAD_START_ROUTINE)pLoadLibrary, remoteDllName, 0, nullptr);
            if (!hThread)
            {
                DWORD err = GetLastError();
                wprintf(L"[manualMapInject] ERROR: CreateRemoteThread failed for LoadLibraryA, GetLastError()=%lu\n", err);
                fflush(stdout);
                VirtualFreeEx(hProc, remoteDllName, 0, MEM_RELEASE);
                VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
                CloseHandle(hProc);
                return false;
            }
            wprintf(L"[manualMapInject] Remote thread created, waiting for LoadLibraryA...\n");
            fflush(stdout);

            DWORD waitResult = WaitForSingleObject(hThread, 5000);
            if (waitResult != WAIT_OBJECT_0)
            {
                wprintf(L"[manualMapInject] WARNING: WaitForSingleObject returned %lu (timeout or error)\n", waitResult);
                fflush(stdout);
            }
            HMODULE hRemoteMod = nullptr;
            GetExitCodeThread(hThread, (LPDWORD)&hRemoteMod);
            CloseHandle(hThread);
            VirtualFreeEx(hProc, remoteDllName, 0, MEM_RELEASE);

            if (!hRemoteMod)
            {
                wprintf(L"[manualMapInject] WARNING: LoadLibraryA failed for %hs (module handle=null), skipping...\n", dllName);
                fflush(stdout);
                impDesc++;
                continue;
            }
            wprintf(L"[manualMapInject] LoadLibraryA succeeded, module base=0x%llx\n", (unsigned long long)hRemoteMod);
            fflush(stdout);

            HMODULE hKernel32Local = GetModuleHandleW(L"kernel32.dll");
            DWORD_PTR kernel32BaseLocal = (DWORD_PTR)hKernel32Local;

            FARPROC pGetProcLocal = GetProcAddress(hKernel32Local, "GetProcAddress");
            DWORD_PTR getProcOffset = (DWORD_PTR)pGetProcLocal - kernel32BaseLocal;

            DWORD_PTR kernel32BaseRemote = 0;
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
            if (snap != INVALID_HANDLE_VALUE)
            {
                MODULEENTRY32W me{};
                me.dwSize = sizeof(me);
                if (Module32FirstW(snap, &me))
                {
                    do
                    {
                        if (_wcsicmp(me.szModule, L"kernel32.dll") == 0)
                        {
                            kernel32BaseRemote = (DWORD_PTR)me.modBaseAddr;
                            break;
                        }
                    } while (Module32NextW(snap, &me));
                }
                CloseHandle(snap);
            }

            if (!kernel32BaseRemote)
            {
                wprintf(L"[manualMapInject] ERROR: Cannot find kernel32.dll in remote process\n");
                VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
                CloseHandle(hProc);
                return false;
            }

            FARPROC pGetProcRemote = (FARPROC)(kernel32BaseRemote + getProcOffset);
            wprintf(L"[manualMapInject] kernel32.dll: local=0x%llx, remote=0x%llx, GetProcAddress offset=0x%llx\n",
                    (unsigned long long)kernel32BaseLocal, (unsigned long long)kernel32BaseRemote, (unsigned long long)getProcOffset);

            DWORD thunkRva = impDesc->OriginalFirstThunk ? impDesc->OriginalFirstThunk : impDesc->FirstThunk;
            DWORD_PTR thunkAddr = (DWORD_PTR)remoteBase + impDesc->FirstThunk;

            wprintf(L"[manualMapInject] Resolving functions from %hs (thunkRva=0x%x, thunkAddr=0x%llx)...\n",
                    dllName, thunkRva, (unsigned long long)thunkAddr);
            fflush(stdout);

            DWORD thunkOffset = rvaToFileOffset(thunkRva, nt, imageSize);
            if (!thunkOffset)
            {
                wprintf(L"[manualMapInject] ERROR: Cannot convert thunk RVA 0x%x to file offset, skipping DLL %hs\n",
                        thunkRva, dllName);
                fflush(stdout);
                importCount++;
                impDesc++;
                continue;
            }

            wprintf(L"[manualMapInject] Thunk RVA 0x%x -> file offset 0x%x\n", thunkRva, thunkOffset);
            fflush(stdout);

            const IMAGE_THUNK_DATA *thunk = (const IMAGE_THUNK_DATA *)(image + thunkOffset);
            DWORD funcCount = 0;
            DWORD funcIndex = 0;

            while (thunk->u1.AddressOfData)
            {
                if ((BYTE *)thunk - image >= (ptrdiff_t)imageSize)
                {
                    wprintf(L"[manualMapInject] WARNING: thunk out of bounds, stopping function resolution for %hs\n", dllName);
                    fflush(stdout);
                    break;
                }
                funcIndex++;
                wprintf(L"[manualMapInject] Processing function %lu from %hs...\n", funcIndex, dllName);
                fflush(stdout);
                if (thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG64)
                {
                    WORD ordinal = IMAGE_ORDINAL64(thunk->u1.Ordinal);

                    LPVOID remoteOrdinal = VirtualAllocEx(hProc, nullptr, sizeof(ordinal), MEM_COMMIT, PAGE_READWRITE);
                    if (remoteOrdinal)
                    {
                        WriteProcessMemory(hProc, remoteOrdinal, &ordinal, sizeof(ordinal), &written);

                        typedef FARPROC(WINAPI * GetProcAddressProc)(HMODULE, LPCSTR);
                        HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0,
                                                            (LPTHREAD_START_ROUTINE)pGetProcRemote,
                                                            hRemoteMod, 0, nullptr);
                        VirtualFreeEx(hProc, remoteOrdinal, 0, MEM_RELEASE);
                        CloseHandle(hThread);
                    }
                    wprintf(L"[manualMapInject] WARNING: Ordinal imports not fully supported, skipping ordinal %d\n", ordinal);
                }
                else
                {
                    DWORD funcNameRva = (DWORD)thunk->u1.AddressOfData;
                    DWORD funcNameOffset = rvaToFileOffset(funcNameRva, nt, imageSize);
                    if (!funcNameOffset)
                    {
                        wprintf(L"[manualMapInject] WARNING: Cannot convert function name RVA 0x%x to file offset, skipping\n", funcNameRva);
                        fflush(stdout);
                        thunkAddr += sizeof(DWORD_PTR);
                        thunk++;
                        continue;
                    }

                    const IMAGE_IMPORT_BY_NAME *importByName = (const IMAGE_IMPORT_BY_NAME *)(image + funcNameOffset);
                    const char *funcName = importByName->Name;

                    wprintf(L"[manualMapInject] Found import by name: %hs (RVA=0x%x, offset=0x%x)\n",
                            funcName, funcNameRva, funcNameOffset);
                    fflush(stdout);

                    SIZE_T funcNameLen = strlen(funcName) + 1;
                    LPVOID remoteFuncName = VirtualAllocEx(hProc, nullptr, funcNameLen, MEM_COMMIT, PAGE_READWRITE);
                    if (!remoteFuncName)
                    {
                        wprintf(L"[manualMapInject] WARNING: Failed to allocate memory for function name: %hs\n", funcName);
                        thunkAddr += sizeof(DWORD_PTR);
                        thunk++;
                        continue;
                    }

                    WriteProcessMemory(hProc, remoteFuncName, funcName, funcNameLen, &written);

                    typedef FARPROC(WINAPI * GetProcAddressProc)(HMODULE, LPCSTR);
                    wprintf(L"[manualMapInject] Loading %hs locally to resolve %hs...\n", dllName, funcName);
                    fflush(stdout);
                    HMODULE hLocalMod = LoadLibraryA(dllName);
                    if (!hLocalMod)
                    {
                        wprintf(L"[manualMapInject] WARNING: Failed to load %hs locally, GetLastError()=%lu\n", dllName, GetLastError());
                        fflush(stdout);
                        VirtualFreeEx(hProc, remoteFuncName, 0, MEM_RELEASE);
                        thunkAddr += sizeof(DWORD_PTR);
                        thunk++;
                        continue;
                    }

                    FARPROC funcAddrLocal = GetProcAddress(hLocalMod, funcName);
                    if (!funcAddrLocal)
                    {
                        wprintf(L"[manualMapInject] WARNING: GetProcAddress failed for %hs from %hs, GetLastError()=%lu\n",
                                funcName, dllName, GetLastError());
                        fflush(stdout);
                        FreeLibrary(hLocalMod);
                        VirtualFreeEx(hProc, remoteFuncName, 0, MEM_RELEASE);
                        thunkAddr += sizeof(DWORD_PTR);
                        thunk++;
                        continue;
                    }

                    wprintf(L"[manualMapInject] Found %hs at local address 0x%llx\n", funcName, (unsigned long long)funcAddrLocal);
                    fflush(stdout);
                    DWORD_PTR funcOffset = (DWORD_PTR)funcAddrLocal - kernel32BaseLocal;
                    HMODULE hFuncMod = nullptr;
                    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           (LPCSTR)funcAddrLocal, &hFuncMod))
                    {
                        if (hFuncMod == hKernel32Local)
                        {
                            DWORD_PTR funcAddrRemote = kernel32BaseRemote + funcOffset;
                            if (!WriteProcessMemory(hProc, (LPVOID)thunkAddr, &funcAddrRemote, sizeof(DWORD_PTR), &written))
                            {
                                wprintf(L"[manualMapInject] WARNING: Failed to write function address (%hs)\n", funcName);
                            }
                            else
                            {
                                funcCount++;
                            }
                        }
                        else
                        {
                            DWORD_PTR funcOffset2 = (DWORD_PTR)funcAddrLocal - (DWORD_PTR)hLocalMod;
                            DWORD_PTR funcAddrRemote = (DWORD_PTR)hRemoteMod + funcOffset2;
                            wprintf(L"[manualMapInject] Calculating remote address: localMod=0x%llx, func=0x%llx, offset=0x%llx, remoteMod=0x%llx, remoteFunc=0x%llx\n",
                                    (unsigned long long)hLocalMod, (unsigned long long)funcAddrLocal, (unsigned long long)funcOffset2,
                                    (unsigned long long)hRemoteMod, (unsigned long long)funcAddrRemote);
                            fflush(stdout);
                            if (!WriteProcessMemory(hProc, (LPVOID)thunkAddr, &funcAddrRemote, sizeof(DWORD_PTR), &written))
                            {
                                DWORD err = GetLastError();
                                wprintf(L"[manualMapInject] WARNING: Failed to write function address (%hs), GetLastError()=%lu\n", funcName, err);
                                fflush(stdout);
                            }
                            else
                            {
                                funcCount++;
                                wprintf(L"[manualMapInject] Successfully resolved %hs: remote address=0x%llx\n",
                                        funcName, (unsigned long long)funcAddrRemote);
                                fflush(stdout);
                            }
                            FreeLibrary(hLocalMod);
                        }
                    }
                    else
                    {
                        wprintf(L"[manualMapInject] WARNING: GetModuleHandleExA failed for %hs, GetLastError()=%lu\n", funcName, GetLastError());
                        fflush(stdout);
                        FreeLibrary(hLocalMod);
                    }

                    if (remoteFuncName)
                    {
                        VirtualFreeEx(hProc, remoteFuncName, 0, MEM_RELEASE);
                    }
                }

                thunkAddr += sizeof(DWORD_PTR);
                thunk++;
            }

            wprintf(L"[manualMapInject] Finished resolving functions from %hs: %lu functions resolved\n", dllName, funcCount);
            fflush(stdout);
            importCount++;
            impDesc++;

            wprintf(L"[manualMapInject] Moving to next import DLL (importCount=%lu)...\n", importCount);
            fflush(stdout);
        }

        wprintf(L"[manualMapInject] Import resolution loop ended. Total DLLs processed: %lu\n", importCount);
        fflush(stdout);

        if (importCount == 0)
        {
            wprintf(L"[manualMapInject] CRITICAL ERROR: No imports were resolved!\n");
            wprintf(L"[manualMapInject] DLL cannot function without imports. Failing injection to trigger LoadLibrary fallback.\n");
            fflush(stdout);
            VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
            CloseHandle(hProc);
            SetLastError(ERROR_BAD_FORMAT);
            return false;
        }

        wprintf(L"[manualMapInject] Import resolution complete (%lu DLLs processed successfully)\n", importCount);
        fflush(stdout);
    }
    else
    {
        wprintf(L"[manualMapInject] No imports to process (importRva=%lu, importSize=%lu)\n", importRva, importSize);
        fflush(stdout);
    }

    DWORD oldProt = 0;
    VirtualProtectEx(hProc, remoteBase, imageSizeAligned, PAGE_EXECUTE_READ, &oldProt);
    wprintf(L"[manualMapInject] Memory protection updated\n");

    DWORD_PTR entryPoint = (DWORD_PTR)remoteBase + nt->OptionalHeader.AddressOfEntryPoint;

    // 64-bit shellcode to call DllMain(hInstance, DLL_PROCESS_ATTACH, nullptr)
    // mov rcx, hInstance
    // mov rdx, 1 (DLL_PROCESS_ATTACH)
    // mov r8, 0
    // mov rax, entryPoint
    // call rax
    // ret
    unsigned char shellcode[] = {
        0x48, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rcx, hInstance (will be patched)
        0x48, 0xC7, 0xC2, 0x01, 0x00, 0x00, 0x00,                   // mov rdx, 1 (DLL_PROCESS_ATTACH)
        0x49, 0xC7, 0xC0, 0x00, 0x00, 0x00, 0x00,                   // mov r8, 0
        0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rax, entryPoint (will be patched)
        0xFF, 0xD0,                                                 // call rax
        0xC3                                                        // ret
    };

    *(DWORD_PTR *)(shellcode + 2) = (DWORD_PTR)remoteBase;
    *(DWORD_PTR *)(shellcode + 19) = entryPoint;

    LPVOID shellcodeMem = VirtualAllocEx(hProc, nullptr, sizeof(shellcode), MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!shellcodeMem){
        DWORD err = GetLastError();
        wprintf(L"[manualMapInject] ERROR: VirtualAllocEx failed for shellcode, GetLastError()=%lu (0x%lx)\n", err, err);
        VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    SIZE_T shellcodeWritten = 0;
    if (!WriteProcessMemory(hProc, shellcodeMem, shellcode, sizeof(shellcode), &shellcodeWritten)){
        DWORD err = GetLastError();
        wprintf(L"[manualMapInject] ERROR: WriteProcessMemory failed for shellcode, GetLastError()=%lu (0x%lx)\n", err, err);
        VirtualFreeEx(hProc, shellcodeMem, 0, MEM_RELEASE);
        VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    wprintf(L"[manualMapInject] Calling DllMain via shellcode (entryPoint=0x%llx)...\n", (unsigned long long)entryPoint);
    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, (LPTHREAD_START_ROUTINE)shellcodeMem, nullptr, 0, nullptr);
    if (!hThread){
        DWORD err = GetLastError();
        wprintf(L"[manualMapInject] ERROR: CreateRemoteThread failed, GetLastError()=%lu (0x%lx)\n", err, err);
        VirtualFreeEx(hProc, shellcodeMem, 0, MEM_RELEASE);
        VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    wprintf(L"[manualMapInject] Remote thread created, waiting for DllMain...\n");
    WaitForSingleObject(hThread, 5000);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);

    VirtualFreeEx(hProc, shellcodeMem, 0, MEM_RELEASE);

    if (exitCode == 0)
    {
        wprintf(L"[manualMapInject] ERROR: DllMain returned FALSE (exitCode=0)\n");
        VirtualFreeEx(hProc, remoteBase, 0, MEM_RELEASE);
        CloseHandle(hProc);
        SetLastError(ERROR_DLL_INIT_FAILED);
        return false;
    }

    wprintf(L"[manualMapInject] SUCCESS: DLL mapped and initialized at %p\n", remoteBase);
    CloseHandle(hProc);
    return true;
}

static void scrubModuleHeadersIfPresent(DWORD pid, const wchar_t *moduleName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE)
        return;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me))
    {
        do
        {
            if (_wcsicmp(me.szModule, moduleName) == 0)
            {
                HANDLE hProc = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, pid);
                if (hProc)
                {
                    SIZE_T wiped = 0;
                    BYTE zeros[0x1000] = {0};
                    WriteProcessMemory(hProc, me.modBaseAddr, zeros, sizeof(zeros), &wiped);
                    CloseHandle(hProc);
                }
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
}

static LPVOID findMappedBaseByMarker(DWORD pid)
{
    static const char marker[] = "OVSON_BUILD/";
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc)
        return nullptr;
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    BYTE *addr = (BYTE *)si.lpMinimumApplicationAddress;
    BYTE *maxA = (BYTE *)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    LPVOID found = nullptr;
    while (addr < maxA)
    {
        if (!VirtualQueryEx(hProc, addr, &mbi, sizeof(mbi)))
            break;
        bool readable = (mbi.State == MEM_COMMIT) && (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE));
        if (readable && mbi.RegionSize >= sizeof(marker))
        {
            SIZE_T sz = mbi.RegionSize;
            std::vector<char> buf;
            buf.resize((size_t)sz);
            SIZE_T rd = 0;
            if (ReadProcessMemory(hProc, addr, buf.data(), sz, &rd) && rd > 0)
            {
                for (size_t i = 0; i + sizeof(marker) - 1 < rd; ++i)
                {
                    if (memcmp(buf.data() + i, marker, sizeof(marker) - 1) == 0)
                    {
                        BYTE *p = (BYTE *)addr + i;
                        BYTE *page = (BYTE *)((ULONG_PTR)p & ~((ULONG_PTR)0xFFF));
                        for (int back = 0; back < 16; ++back)
                        {
                            IMAGE_DOS_HEADER dos{};
                            SIZE_T rr = 0;
                            if (!ReadProcessMemory(hProc, page, &dos, sizeof(dos), &rr) || rr != sizeof(dos))
                                break;
                            if (dos.e_magic == 0x5A4D && dos.e_lfanew > 0 && dos.e_lfanew < 0x1000)
                            {
                                DWORD peSig = 0;
                                rr = 0;
                                if (ReadProcessMemory(hProc, (BYTE *)page + dos.e_lfanew, &peSig, sizeof(peSig), &rr) && rr == sizeof(peSig) && peSig == 0x4550)
                                {
                                    found = page;
                                    break;
                                }
                            }
                            page = page - 0x1000;
                        }
                        if (found)
                            break;
                    }
                }
            }
        }
        addr = (BYTE *)mbi.BaseAddress + mbi.RegionSize;
    }
    CloseHandle(hProc);
    return found;
}

static void patchPerUserWatermark(std::vector<uint8_t> &bytes, const std::wstring &tokenW)
{
    int tlen = WideCharToMultiByte(CP_UTF8, 0, tokenW.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (tlen <= 1)
        return;
    std::string tUtf8;
    tUtf8.resize(tlen - 1);
    WideCharToMultiByte(CP_UTF8, 0, tokenW.c_str(), -1, &tUtf8[0], tlen, nullptr, nullptr);
    uint8_t dig[32];
    if (!sha256Cng((const uint8_t *)tUtf8.data(), (ULONG)tUtf8.size(), dig))
        return;
    static const char hexd[] = "0123456789abcdef";
    char id16[17];
    for (int i = 0; i < 8; ++i)
    {
        id16[i * 2] = hexd[(dig[i] >> 4) & 0xF];
        id16[i * 2 + 1] = hexd[dig[i] & 0xF];
    }
    id16[16] = '\0';
    // find placeholder "OVSON_UWM:________________"
    const char *ph = "OVSON_UWM:";
    for (size_t i = 0; i + 24 < bytes.size(); ++i)
    {
        if (memcmp(bytes.data() + i, ph, 11) == 0)
        {
            memcpy(bytes.data() + i + 11, id16, 16);
            break;
        }
    }
}

static void remoteWipeHeadersAndIat(DWORD pid, LPVOID base)
{
    if (!base)
        return;
    HANDLE hProc = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProc)
        return;
    BYTE zeros[0x1000] = {0};
    SIZE_T wr = 0;
    DWORD oldProt = 0, ignore = 0;
    VirtualProtectEx(hProc, base, 0x1000, PAGE_READWRITE, &oldProt);
    WriteProcessMemory(hProc, base, zeros, sizeof(zeros), &wr);
    if (oldProt)
        VirtualProtectEx(hProc, base, 0x1000, oldProt, &ignore);
    IMAGE_DOS_HEADER dos{};
    SIZE_T rr = 0;
    if (ReadProcessMemory(hProc, base, &dos, sizeof(dos), &rr) && rr == sizeof(dos) && dos.e_magic == 0x5A4D)
    {
        IMAGE_NT_HEADERS64 nt64{};
        if (ReadProcessMemory(hProc, (BYTE *)base + dos.e_lfanew, &nt64, sizeof(nt64), &rr) && rr >= offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + sizeof(nt64.OptionalHeader))
        {
            if (nt64.Signature == 0x4550)
            {
                IMAGE_DATA_DIRECTORY iat = nt64.OptionalHeader.DataDirectory[12];
                if (iat.VirtualAddress && iat.Size && iat.Size < (1u << 20))
                {
                    std::vector<BYTE> z;
                    z.resize(iat.Size, 0);
                    DWORD iatOld = 0, iatIgn = 0;
                    VirtualProtectEx(hProc, (BYTE *)base + iat.VirtualAddress, iat.Size, PAGE_READWRITE, &iatOld);
                    WriteProcessMemory(hProc, (BYTE *)base + iat.VirtualAddress, z.data(), z.size(), &wr);
                    if (iatOld)
                        VirtualProtectEx(hProc, (BYTE *)base + iat.VirtualAddress, iat.Size, iatOld, &iatIgn);
                }
            }
        }
    }
    CloseHandle(hProc);
}

static bool decryptEmbeddedIfEncrypted(std::vector<uint8_t> &bytes)
{
    if (bytes.size() < 4)
        return false;
    if (!(bytes[0] == 'O' && bytes[1] == 'V' && bytes[2] == 'S' && bytes[3] == 'G'))
        return false;
    if (bytes.size() < 4 + 4 * 4)
        return false;
    const uint8_t *p = bytes.data();
    uint32_t ver = *(const uint32_t *)(p + 4);
    uint32_t ivLen = *(const uint32_t *)(p + 8);
    uint32_t tagLen = *(const uint32_t *)(p + 12);
    uint32_t plainSize = *(const uint32_t *)(p + 16);
    if (ver != 1)
        return false;
    size_t off = 20;
    if (bytes.size() < off + ivLen + tagLen)
        return false;
    const uint8_t *iv = p + off;
    off += ivLen;
    const uint8_t *tag = p + off;
    off += tagLen;
    size_t ctLen = bytes.size() - off;
    if (plainSize > ctLen)
        return false;
    const uint8_t *ct = p + off;

    static const uint8_t obfKey[32] = {
        0xC1, 0xC7, 0xC5, 0xC4, 0xC3, 0xC2, 0xC9, 0xCB, 0xCD, 0xCF, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5,
        0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xD0, 0xD1, 0xD2, 0xD3, 0xE1, 0xE2, 0xE3, 0xE4, 0xAB, 0xAC};
    uint8_t key[32];
    for (int i = 0; i < 32; ++i)
        key[i] = obfKey[i] ^ 0xAA;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    NTSTATUS st;
    ULONG objLen = 0;
    ULONG cb = 0;
    std::vector<uint8_t> keyObj;
    std::vector<uint8_t> plain;
    ULONG out = 0;

    st = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (st < 0)
        goto cleanup;
    st = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, (ULONG)sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    if (st < 0)
        goto cleanup;
    st = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &cb, 0);
    if (st < 0)
        goto cleanup;
    keyObj.resize(objLen);
    st = BCryptGenerateSymmetricKey(hAlg, &hKey, keyObj.data(), (ULONG)keyObj.size(), (PUCHAR)key, (ULONG)sizeof(key), 0);
    if (st < 0)
        goto cleanup;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO ainfo;
    BCRYPT_INIT_AUTH_MODE_INFO(ainfo);
    ainfo.pbNonce = (PUCHAR)iv;
    ainfo.cbNonce = (ULONG)ivLen;
    ainfo.pbTag = (PUCHAR)tag;
    ainfo.cbTag = (ULONG)tagLen;
    ainfo.pbAuthData = nullptr;
    ainfo.cbAuthData = 0;
    plain.resize(plainSize ? plainSize : ctLen);
    st = BCryptDecrypt(hKey, (PUCHAR)ct, (ULONG)ctLen, &ainfo, nullptr, 0, plain.data(), (ULONG)plain.size(), &out, 0);
    if (st < 0)
        goto cleanup;
    plain.resize(out);
    bytes.swap(plain);
    // zero sensitive
    SecureZeroMemory(key, sizeof(key));
    return true;
cleanup:
    SecureZeroMemory(key, sizeof(key));
    if (hKey)
        BCryptDestroyKey(hKey);
    if (hAlg)
        BCryptCloseAlgorithmProvider(hAlg, 0);
    return false;
}

static bool injectDll(DWORD pid, const std::wstring &dllPath)
{
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE, FALSE, pid);
    if (!hProc)
    {
        return false;
    }
    SIZE_T size = (dllPath.size() + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(hProc, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote)
    {
        CloseHandle(hProc);
        return false;
    }
    if (!WriteProcessMemory(hProc, remote, dllPath.c_str(), size, nullptr))
    {
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    LPTHREAD_START_ROUTINE loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW");
    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!hThread)
    {
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }
    WaitForSingleObject(hThread, INFINITE);
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProc);
    return true;
}

static int g_targetProgress = 10;
static double g_animProgressF = 10.0;
static int g_animProgress = 10;
static int g_tick = 0;
static HFONT g_titleFont = nullptr;
static HFONT g_smallFont = nullptr;
static std::wstring g_statusText = L"Injecting...";
static COLORREF g_statusColor = RGB(200, 200, 200);
static bool g_consoleReady = false;
static HANDLE g_nameMapHandle = nullptr;
static const DWORD kBuildId = 0x20251023;
static std::wstring g_readyName;          
static std::wstring g_injectedName;
static HANDLE g_loaderMutex = nullptr;

// removed
static volatile bool g_loggedIn = true;
static volatile bool g_loginVerified = true;
static HINSTANCE g_hInst = nullptr;

static bool isDebugged()
{
    return false; // removed
}

static bool isVirtualized()
{
    return false; // removed
}

static void ensureConsole()
{
    // console disabled - no debug output
}

struct InjectContext
{
    HWND hwnd;
    std::wstring dllPath;
    std::vector<uint8_t> dllBytes;
    std::wstring cpNames[6];
};

static bool isAlreadyInjected(DWORD pid)
{
    HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Global\\OVsonShared");
    if (map)
    {
        volatile LONG *flag = (volatile LONG *)MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof(LONG));
        if (flag)
        {
            LONG val = *flag;
            UnmapViewOfFile((LPCVOID)flag);
            CloseHandle(map);
            if (val == 1)
            {
                return true; // injected=1 = DLL is injected
            }
        }
        else
        {
            CloseHandle(map);
        }
    }

    wchar_t name[64];
    wsprintfW(name, L"Global\\OVsonAlive_%lu", pid);
    HANDLE alive = OpenEventW(SYNCHRONIZE, FALSE, name);
    if (alive)
    {
        CloseHandle(alive);
        return true;
    }

    HANDLE evLoaded = OpenEventW(SYNCHRONIZE, FALSE, L"Global\\OVsonLoaded");
    if (evLoaded)
    {
        CloseHandle(evLoaded);
        return true;
    }

    if (isOVsonModuleLoaded(pid))
    {
        return true;
    }

    return false;
}

static DWORD WINAPI InjectThread(LPVOID param)
{
    InjectContext *ctx = (InjectContext *)param;

    for (int i = 0; i < 100; ++i)
    {
        if (findProcessId(L"javaw.exe"))
        {
            break;
        }
        Sleep(50);
    }
    DWORD pid = findProcessId(L"javaw.exe");
    if (!pid)
    {
        if (g_loaderMutex)
        {
            ReleaseMutex(g_loaderMutex);
            CloseHandle(g_loaderMutex);
            g_loaderMutex = nullptr;
        }
        PostMessageW(ctx->hwnd, WM_APP + 4, 0, 0);
        return 0;
    }

    if (isAlreadyInjected(pid))
    {
        if (g_loaderMutex)
        {
            ReleaseMutex(g_loaderMutex);
            CloseHandle(g_loaderMutex);
            g_loaderMutex = nullptr;
        }
        PostMessageW(ctx->hwnd, WM_APP + 3, 0, 0);
        return 0;
    }

    bool ok = false;
    if (!ctx->dllBytes.empty())
    {
        patchPerUserWatermark(ctx->dllBytes, L"");

        wchar_t tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);
        std::wstring dllPath = std::wstring(tempPath) + L"OVson_" + std::to_wstring(GetTickCount()) + L".dll";

        FILE *f = nullptr;
        errno_t ferr = _wfopen_s(&f, dllPath.c_str(), L"wb");
        if (f && ferr == 0)
        {
            size_t written = fwrite(ctx->dllBytes.data(), 1, ctx->dllBytes.size(), f);
            fclose(f);

            if (written == ctx->dllBytes.size())
            {
                ok = simpleLoadLibraryInject(pid, dllPath.c_str());

                if (ok)
                {
                    Sleep(2000);
                    DeleteFileW(dllPath.c_str());
                }
            }
        }
    }

    if (!ok)
    {
        if (g_loaderMutex)
        {
            ReleaseMutex(g_loaderMutex);
            CloseHandle(g_loaderMutex);
            g_loaderMutex = nullptr;
        }
        PostMessageW(ctx->hwnd, WM_APP + 1, 0, 0);
        return 0;
    }

    if (g_loaderMutex)
    {
        ReleaseMutex(g_loaderMutex);
        CloseHandle(g_loaderMutex);
        g_loaderMutex = nullptr;
    }

    HANDLE evReady = CreateEventW(nullptr, TRUE, FALSE, L"Local\\OVsonReadyForBanner");
    if (evReady)
    {
        SetEvent(evReady);
        CloseHandle(evReady);
    }

    HANDLE evInjected = CreateEventW(nullptr, TRUE, FALSE, L"Local\\OVsonInjected");
    if (evInjected)
    {
        WaitForSingleObject(evInjected, 2000);
        PostMessageW(ctx->hwnd, WM_APP_PROGRESS, 100, 0);
        PostMessageW(ctx->hwnd, WM_APP + 1, 1, 0);
        CloseHandle(evInjected);
    }
    else
    {
        Sleep(500);
        PostMessageW(ctx->hwnd, WM_APP_PROGRESS, 100, 0);
        PostMessageW(ctx->hwnd, WM_APP + 1, 1, 0);
    }

    return 0;
}

// HMAC-SHA256 via CNG
static bool hmacSha256(const uint8_t *key, ULONG keyLen, const uint8_t *data, ULONG dataLen, uint8_t out[32])
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    NTSTATUS st;
    st = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (st < 0)
        return false;
    DWORD cbHash = 32, cbObj = 0, cbRes = 0;
    st = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbObj, sizeof(cbObj), &cbRes, 0);
    if (st < 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }
    std::vector<uint8_t> obj(cbObj);
    st = BCryptCreateHash(hAlg, &hHash, obj.data(), (ULONG)obj.size(), (PUCHAR)key, keyLen, 0);
    if (st < 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }
    st = BCryptHashData(hHash, (PUCHAR)data, dataLen, 0);
    if (st < 0)
    {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }
    st = BCryptFinishHash(hHash, (PUCHAR)out, cbHash, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return st >= 0;
}

static std::wstring hexOf(const uint8_t *b, size_t n)
{
    static const wchar_t *hexd = L"0123456789abcdef";
    std::wstring s;
    s.resize(n * 2);
    for (size_t i = 0; i < n; ++i)
    {
        s[i * 2] = hexd[(b[i] >> 4) & 0xF];
        s[i * 2 + 1] = hexd[b[i] & 0xF];
    }
    return s;
}

static bool aesGcmEncrypt(const std::vector<uint8_t> &key, const std::vector<uint8_t> &plain, std::vector<uint8_t> &out)
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    NTSTATUS st;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0)
        return false;
    if (BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, (ULONG)sizeof(BCRYPT_CHAIN_MODE_GCM), 0) < 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }
    DWORD objLen = 0, cb = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &cb, 0);
    std::vector<uint8_t> obj(objLen);
    if (BCryptGenerateSymmetricKey(hAlg, &hKey, obj.data(), (ULONG)obj.size(), (PUCHAR)key.data(), (ULONG)key.size(), 0) < 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }
    std::vector<uint8_t> iv(12);
    BCryptGenRandom(nullptr, iv.data(), (ULONG)iv.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    std::vector<uint8_t> tag(16);
    std::vector<uint8_t> ct(plain.size());
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO a;
    BCRYPT_INIT_AUTH_MODE_INFO(a);
    a.pbNonce = iv.data();
    a.cbNonce = (ULONG)iv.size();
    a.pbTag = tag.data();
    a.cbTag = (ULONG)tag.size();
    ULONG outSize = 0;
    st = BCryptEncrypt(hKey, (PUCHAR)plain.data(), (ULONG)plain.size(), &a, nullptr, 0, ct.data(), (ULONG)ct.size(), &outSize, 0);
    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (st < 0)
        return false;
    ct.resize(outSize);
    out.clear();
    const char magic[4] = {'O', 'V', 'A', 'T'};
    out.insert(out.end(), magic, magic + 4);
    auto w32 = [&](uint32_t v)
    { out.push_back((uint8_t)(v&0xFF)); out.push_back((uint8_t)((v>>8)&0xFF)); out.push_back((uint8_t)((v>>16)&0xFF)); out.push_back((uint8_t)((v>>24)&0xFF)); };
    w32(1);
    w32((uint32_t)iv.size());
    w32((uint32_t)tag.size());
    w32((uint32_t)plain.size());
    out.insert(out.end(), iv.begin(), iv.end());
    out.insert(out.end(), tag.begin(), tag.end());
    out.insert(out.end(), ct.begin(), ct.end());
    return true;
}

static std::vector<uint8_t> deriveKeyFromSecretAndName(const std::wstring &dynName)
{
    static const uint8_t obf[32] = {0xB1, 0xB2, 0xB3, 0xB4, 0xC1, 0xC2, 0xC3, 0xC4, 0xD1, 0xD2, 0xD3, 0xD4, 0xE1, 0xE2, 0xE3, 0xE4, 0x91, 0x92, 0x93, 0x94, 0x81, 0x82, 0x83, 0x84, 0xA1, 0xA2, 0xA3, 0xA4, 0xF1, 0xF2, 0xF3, 0xF4};
    uint8_t secret[32];
    for (int i = 0; i < 32; ++i)
        secret[i] = obf[i] ^ 0xAA;
    const uint8_t *bid = (const uint8_t *)&kBuildId;
    for (int i = 0; i < 32; ++i)
        secret[i] ^= bid[i % 4] ^ (uint8_t)((kBuildId >> ((i % 4) * 8)) & 0xFF);
    int len = WideCharToMultiByte(CP_UTF8, 0, dynName.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string nameUtf8;
    if (len > 0)
    {
        nameUtf8.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, dynName.c_str(), -1, &nameUtf8[0], len, nullptr, nullptr);
    }
    std::vector<uint8_t> data;
    data.reserve(32 + nameUtf8.size());
    data.insert(data.end(), secret, secret + 32);
    data.insert(data.end(), nameUtf8.begin(), nameUtf8.end());
    uint8_t dig[32];
    sha256Cng(data.data(), (ULONG)data.size(), dig);
    return std::vector<uint8_t>(dig, dig + 32);
}

static bool sha256Cng(const uint8_t *data, ULONG len, uint8_t out[32])
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    NTSTATUS st;
    st = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (st < 0)
        return false;
    DWORD cbObj = 0, cbRes = 0;
    st = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbObj, sizeof(cbObj), &cbRes, 0);
    if (st < 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }
    std::vector<uint8_t> obj(cbObj);
    st = BCryptCreateHash(hAlg, &hHash, obj.data(), (ULONG)obj.size(), nullptr, 0, 0);
    if (st < 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }
    st = BCryptHashData(hHash, (PUCHAR)data, len, 0);
    if (st < 0)
    {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }
    st = BCryptFinishHash(hHash, (PUCHAR)out, 32, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return st >= 0;
}

static std::wstring sha256HexOfWide(const std::wstring &w)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s;
    if (len > 0)
    {
        s.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], len, nullptr, nullptr);
    }
    uint8_t dig[32];
    if (!sha256Cng((const uint8_t *)s.data(), (ULONG)s.size(), dig))
        return L"";
    return hexOf(dig, 32);
}

struct IpcNames
{
    wchar_t cp[6][64];
    wchar_t ready[64];
    wchar_t injected[64];
};

static void deriveIpcNames(IpcNames &out)
{
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    DWORD pid = GetCurrentProcessId();
    // obfuscated secret
    static const uint8_t obf[32] = {0x91, 0x92, 0x93, 0x94, 0xA1, 0xA2, 0xA3, 0xA4, 0xB1, 0xB2, 0xB3, 0xB4, 0xC1, 0xC2, 0xC3, 0xC4, 0xD1, 0xD2, 0xD3, 0xD4, 0xE1, 0xE2, 0xE3, 0xE4, 0xF1, 0xF2, 0xF3, 0xF4, 0x81, 0x82, 0x83, 0x84};
    uint8_t key[32];
    for (int i = 0; i < 32; ++i)
        key[i] = obf[i] ^ 0xAA;
    auto makeTag = [&](const char *label) -> std::wstring
    {
        char buf[128];
        _snprintf_s(buf, _TRUNCATE, "%s|%lu|%llu", label, (unsigned long)pid, (unsigned long long)qpc.QuadPart);
        uint8_t mac[32];
        hmacSha256(key, (ULONG)sizeof(key), (const uint8_t *)buf, (ULONG)strlen(buf), mac);
        std::wstring name = L"Local\\OVN_" + hexOf(mac, 12);
        return name;
    };
    const char *labels[6] = {"Cp1", "Cp2", "Cp3", "Cp4", "Cp5", "Cp6"};
    for (int i = 0; i < 6; ++i)
    {
        std::wstring nm = makeTag(labels[i]);
        wcsncpy_s(out.cp[i], nm.c_str(), _TRUNCATE);
    }
    std::wstring r = makeTag("Ready");
    wcsncpy_s(out.ready, r.c_str(), _TRUNCATE);
    std::wstring inj = makeTag("Injected");
    wcsncpy_s(out.injected, inj.c_str(), _TRUNCATE);
    SecureZeroMemory(key, sizeof(key));
}

static void PaintUI(HWND hWnd, HDC hdc)
{
    RECT rc;
    GetClientRect(hWnd, &rc);

    HBRUSH bg = CreateSolidBrush(RGB(18, 18, 24));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HBRUSH border = CreateSolidBrush(RGB(60, 60, 70));
    FrameRect(hdc, &rc, border);
    DeleteObject(border);

    SelectObject(hdc, g_titleFont);
    SetBkMode(hdc, TRANSPARENT);
    int cx = (rc.right - rc.left) / 2;
    int cy = (rc.bottom - rc.top) / 2;

    SetTextColor(hdc, RGB(0, 0, 0));
    TextOutW(hdc, cx - 32 + 2, cy - 48 + 2, L"OVson", 5);
    SetTextColor(hdc, RGB(180, 180, 190));
    TextOutW(hdc, cx - 32, cy - 48, L"OVson", 5);

    SelectObject(hdc, g_smallFont);
    SetTextColor(hdc, g_statusColor);
    const wchar_t *txt = g_statusText.c_str();
    SIZE ts{};
    GetTextExtentPoint32W(hdc, txt, (int)wcslen(txt), &ts);
    TextOutW(hdc, cx - ts.cx / 2, cy - 18, txt, (int)wcslen(txt));

    int barWidth = (rc.right - rc.left) * 4 / 5; // 80% width
    int barHeight = 12;
    int left = cx - barWidth / 2;
    int right = cx + barWidth / 2;
    int top = cy + 10;
    int bottom = top + barHeight;

    HBRUSH trackBg = CreateSolidBrush(RGB(35, 35, 45));
    RECT rTrackBg = {left - 2, top - 2, right + 2, bottom + 2};
    FillRect(hdc, &rTrackBg, trackBg);
    DeleteObject(trackBg);

    HBRUSH track = CreateSolidBrush(RGB(28, 28, 38));
    RECT rTrack = {left, top, right, bottom};
    FillRect(hdc, &rTrack, track);
    DeleteObject(track);

    int width = right - left;
    int fillX = left + (width * g_animProgress) / 100;
    if (fillX > left)
    {
        COLORREF barColor = RGB(140 + (g_animProgress * 20 / 100), 140 + (g_animProgress * 20 / 100), 140 + (g_animProgress * 20 / 100));
        HBRUSH bar = CreateSolidBrush(barColor);
        RECT rFill = {left, top, fillX, bottom};
        FillRect(hdc, &rFill, bar);
        DeleteObject(bar);

        HPEN hiPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
        HPEN oldPen = (HPEN)SelectObject(hdc, hiPen);
        MoveToEx(hdc, left, top, nullptr);
        LineTo(hdc, fillX, top);
        SelectObject(hdc, oldPen);
        DeleteObject(hiPen);

        if (g_animProgress > 0 && g_animProgress < 100)
        {
            int span = 50;
            int pos = left + ((g_tick * 5) % (width + span)) - span;
            RECT rShim = {pos, top, pos + span, bottom};
            if (rShim.right > left && rShim.left < fillX)
            {
                if (rShim.left < left)
                    rShim.left = left;
                if (rShim.right > fillX)
                    rShim.right = fillX;
                HBRUSH shim = CreateSolidBrush(RGB(170, 170, 170));
                FillRect(hdc, &rShim, shim);
                DeleteObject(shim);
            }
        }
    }
}


static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static InjectContext *ctx = nullptr;
    switch (msg)
    {
    case WM_CREATE:
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        HRGN rgn = CreateRoundRectRgn(0, 0, rc.right, rc.bottom, 16, 16);
        SetWindowRgn(hWnd, rgn, TRUE);
        // Fonts
        g_titleFont = CreateFontW(22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g_smallFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SetTimer(hWnd, ID_TIMER_PROGRESS, 8, nullptr);
        ctx = new InjectContext{hWnd, L""};
        std::vector<uint8_t> bytes;
        if (!getEmbeddedDllBytes(bytes))
        {
            PostMessageW(hWnd, WM_APP + 1, 0, 0);
            break;
        }
        (void)decryptEmbeddedIfEncrypted(bytes);
        ctx->dllBytes.swap(bytes);
        static const uint16_t encName1[] = {'G' ^ 0x23, 'l' ^ 0x23, 'o' ^ 0x23, 'b' ^ 0x23, 'a' ^ 0x23, 'l' ^ 0x23, '\\' ^ 0x23, 'O' ^ 0x23, 'V' ^ 0x23, 's' ^ 0x23, 'o' ^ 0x23, 'n' ^ 0x23, 'B' ^ 0x23, 'u' ^ 0x23, 'i' ^ 0x23, 'l' ^ 0x23, 'd' ^ 0x23, 'I' ^ 0x23, 'd' ^ 0x23, 0};
        std::wstring nameBuild = decodeXorW(encName1, 19, 0x23);
        HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(DWORD), nameBuild.c_str());
        if (map)
        {
            DWORD *idp = (DWORD *)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DWORD));
            if (idp)
            {
                *idp = kBuildId;
                UnmapViewOfFile(idp);
            }
            CloseHandle(map);
        }
        IpcNames names{};
        deriveIpcNames(names);
        for (int i = 0; i < 6; ++i)
            ctx->cpNames[i] = names.cp[i];
        g_readyName = names.ready;
        g_injectedName = names.injected;
        static const uint16_t encName2[] = {'G' ^ 0x35, 'l' ^ 0x35, 'o' ^ 0x35, 'b' ^ 0x35, 'a' ^ 0x35, 'l' ^ 0x35, '\\' ^ 0x35, 'O' ^ 0x35, 'V' ^ 0x35, 's' ^ 0x35, 'o' ^ 0x35, 'n' ^ 0x35, 'I' ^ 0x35, 'P' ^ 0x35, 'C' ^ 0x35, 0};
        std::wstring nameIPC = decodeXorW(encName2, 15, 0x35);
        HANDLE mapIpc = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(IpcNames), nameIPC.c_str());
        if (mapIpc)
        {
            void *p = MapViewOfFile(mapIpc, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(IpcNames));
            if (p)
            {
                memcpy(p, &names, sizeof(IpcNames));
                UnmapViewOfFile(p);
            }
            CloseHandle(mapIpc);
        }
        DWORD pid = findProcessId(L"javaw.exe");
        if (pid)
        {
            if (isAlreadyInjected(pid))
            {
                g_statusText = L"Already injected";
                g_statusColor = RGB(220, 60, 60);
                g_targetProgress = 100;
                InvalidateRect(hWnd, nullptr, FALSE);
                SetTimer(hWnd, ID_TIMER_CLOSE, 1500, nullptr);
                break;
            }
        }

        g_loaderMutex = CreateMutexW(nullptr, FALSE, L"Global\\OVsonLoaderMutex");
        if (g_loaderMutex)
        {
            DWORD mutexResult = WaitForSingleObject(g_loaderMutex, 0);
            if (mutexResult == WAIT_OBJECT_0 || mutexResult == WAIT_ABANDONED)
            {
                if (pid)
                {
                    if (isAlreadyInjected(pid))
                    {
                        ReleaseMutex(g_loaderMutex);
                        CloseHandle(g_loaderMutex);
                        g_loaderMutex = nullptr;
                        g_statusText = L"Already injected";
                        g_statusColor = RGB(220, 60, 60);
                        g_targetProgress = 100;
                        InvalidateRect(hWnd, nullptr, FALSE);
                        SetTimer(hWnd, ID_TIMER_CLOSE, 1500, nullptr);
                        break;
                    }
                }

                HANDLE hThread = CreateThread(nullptr, 0, InjectThread, ctx, 0, nullptr);
                if (!hThread)
                {
                    ReleaseMutex(g_loaderMutex);
                    CloseHandle(g_loaderMutex);
                    g_loaderMutex = nullptr;
                }
            }
            else
            {
                CloseHandle(g_loaderMutex);
                g_loaderMutex = nullptr;
                g_statusText = L"Already injecting";
                g_statusColor = RGB(220, 60, 60);
                g_targetProgress = 100;
                InvalidateRect(hWnd, nullptr, FALSE);
                SetTimer(hWnd, ID_TIMER_CLOSE, 1500, nullptr);
            }
        }
        else
        {
            DWORD err = GetLastError();
            if (err == ERROR_ALREADY_EXISTS)
            {
                if (pid)
                {
                    bool alreadyInjected = false;
                    for (int retry = 0; retry < 3; ++retry)
                    {
                        if (isAlreadyInjected(pid))
                        {
                            alreadyInjected = true;
                            break;
                        }
                        Sleep(200);
                    }
                    if (alreadyInjected)
                    {
                        g_statusText = L"Already injected";
                        g_statusColor = RGB(220, 60, 60);
                    }
                    else
                    {
                        g_statusText = L"Already injecting";
                        g_statusColor = RGB(220, 60, 60);
                    }
                }
                else
                {
                    g_statusText = L"Already injecting";
                    g_statusColor = RGB(220, 60, 60);
                }
                g_targetProgress = 100;
                InvalidateRect(hWnd, nullptr, FALSE);
                SetTimer(hWnd, ID_TIMER_CLOSE, 1500, nullptr);
            }
            else
            {
                HANDLE hThread = CreateThread(nullptr, 0, InjectThread, ctx, 0, nullptr);
                if (hThread)
                {
                    CloseHandle(hThread);
                }
            }
        }
        break;
    }
    case WM_TIMER:
    {
        if (wParam == ID_TIMER_PROGRESS)
        {
            static double ap = 10.0;
            ap += (g_targetProgress - ap) * 0.2;
            if (ap < 0)
                ap = 0;
            if (ap > 100)
                ap = 100;
            g_animProgress = (int)(ap + 0.5);
            g_tick++;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (wParam == ID_TIMER_CLOSE)
        {
            KillTimer(hWnd, ID_TIMER_CLOSE);
            AnimateWindow(hWnd, 400, AW_BLEND | AW_HIDE);
            DestroyWindow(hWnd);
        }
        break;
    }
    case WM_APP_PROGRESS:
    {
        int val = (int)wParam;
        if (val < 0)
            val = 0;
        if (val > 100)
            val = 100;
        g_targetProgress = val;
        break;
    }
    case (WM_APP + 1):
    {
        bool ok = (wParam == 1);
        if (ok)
        {
            // Injection successful
            g_targetProgress = 100;
            g_statusText = L"Injected!";
            g_statusColor = RGB(100, 200, 100);
            InvalidateRect(hWnd, nullptr, FALSE);
            SetTimer(hWnd, ID_TIMER_CLOSE, 1000, nullptr);
        }
        else
        {
            // Injection failed
            g_statusText = L"Injection failed";
            g_statusColor = RGB(220, 60, 60);
            g_targetProgress = 100;
            InvalidateRect(hWnd, nullptr, FALSE);
            SetTimer(hWnd, ID_TIMER_CLOSE, 1500, nullptr);
        }
        break;
    }
    case (WM_APP + 3):
    {
        g_statusText = L"Already injected";
        g_statusColor = RGB(220, 60, 60);
        g_targetProgress = 100;
        g_animProgress = 100;
        SetTimer(hWnd, ID_TIMER_CLOSE, 1500, nullptr);
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }
    case (WM_APP + 4):
    {
        g_statusText = L"Injection failed";
        g_statusColor = RGB(220, 60, 60);
        g_targetProgress = 100;
        g_animProgress = 100;
        SetTimer(hWnd, ID_TIMER_CLOSE, 5000, nullptr);
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }
    case WM_NCHITTEST:
    {
        LRESULT hit = DefWindowProcW(hWnd, msg, wParam, lParam);
        if (hit == HTCLIENT)
            return HTCAPTION; // drag anywhere
        return hit;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        PaintUI(hWnd, mem);
        HBRUSH br = CreateSolidBrush(RGB(50, 50, 60));
        FrameRect(mem, &rc, br);
        DeleteObject(br);
        BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        if (g_titleFont)
        {
            DeleteObject(g_titleFont);
            g_titleFont = nullptr;
        }
        if (g_smallFont)
        {
            DeleteObject(g_smallFont);
            g_smallFont = nullptr;
        }
        if (g_loaderMutex)
        {
            ReleaseMutex(g_loaderMutex);
            CloseHandle(g_loaderMutex);
            g_loaderMutex = nullptr;
        }
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    g_hInst = hInst;
    g_loggedIn = true;
    g_loginVerified = true;

    const wchar_t *cls = L"OVSON_LOADER_WIN";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(CreateSolidBrush(RGB(16, 16, 20)));
    RegisterClassW(&wc);

    HWND mainWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW, cls, L"OVson", WS_POPUP | WS_VISIBLE,
                                   (GetSystemMetrics(SM_CXSCREEN) - 450) / 2, (GetSystemMetrics(SM_CYSCREEN) - 160) / 2, 450, 160, nullptr, nullptr, hInst, nullptr);
    if (mainWnd)
    {
        SetLayeredWindowAttributes(mainWnd, 0, 255, LWA_ALPHA);
        AnimateWindow(mainWnd, 300, AW_BLEND);
        ShowWindow(mainWnd, SW_SHOW);
        UpdateWindow(mainWnd);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
