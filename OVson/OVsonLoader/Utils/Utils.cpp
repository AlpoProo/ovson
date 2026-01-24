#include "Utils.h"
#include <TlHelp32.h>
#include <bcrypt.h>
#include <stdio.h>
#include <intrin.h>
#include <algorithm>

#pragma comment(lib, "Bcrypt.lib")

std::wstring decodeXorW(const uint16_t* enc, size_t n, uint8_t key)
{
    std::wstring s;
    s.resize(n);
    for (size_t i = 0; i < n; ++i)
        s[i] = (wchar_t)(enc[i] ^ key);
    return s;
}

std::wstring hexOf(const uint8_t* b, size_t n)
{
    static const wchar_t* hexd = L"0123456789abcdef";
    std::wstring s;
    s.resize(n * 2);
    for (size_t i = 0; i < n; ++i)
    {
        s[i * 2] = hexd[(b[i] >> 4) & 0xF];
        s[i * 2 + 1] = hexd[b[i] & 0xF];
    }
    return s;
}

bool sha256Cng(const uint8_t* data, ULONG len, uint8_t out[32])
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    NTSTATUS st;
    st = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (st < 0) return false;
    DWORD cbObj = 0, cbRes = 0;
    st = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbObj, sizeof(cbObj), &cbRes, 0);
    if (st < 0) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    std::vector<uint8_t> obj(cbObj);
    st = BCryptCreateHash(hAlg, &hHash, obj.data(), (ULONG)obj.size(), nullptr, 0, 0);
    if (st < 0) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    st = BCryptHashData(hHash, (PUCHAR)data, len, 0);
    if (st < 0) { BCryptDestroyHash(hHash); BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    st = BCryptFinishHash(hHash, (PUCHAR)out, 32, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return st >= 0;
}

bool hmacSha256(const uint8_t* key, ULONG keyLen, const uint8_t* data, ULONG dataLen, uint8_t out[32])
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    NTSTATUS st;
    st = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (st < 0) return false;
    DWORD cbObj = 0, cbRes = 0;
    st = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbObj, sizeof(cbObj), &cbRes, 0);
    if (st < 0) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    std::vector<uint8_t> obj(cbObj);
    st = BCryptCreateHash(hAlg, &hHash, obj.data(), (ULONG)obj.size(), (PUCHAR)key, keyLen, 0);
    if (st < 0) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    st = BCryptHashData(hHash, (PUCHAR)data, dataLen, 0);
    if (st < 0) { BCryptDestroyHash(hHash); BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    st = BCryptFinishHash(hHash, (PUCHAR)out, 32, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return st >= 0;
}

bool aesGcmEncrypt(const std::vector<uint8_t>& key, const std::vector<uint8_t>& plain, std::vector<uint8_t>& out)
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    NTSTATUS st;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) return false;
    if (BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, (ULONG)sizeof(BCRYPT_CHAIN_MODE_GCM), 0) < 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0); return false;
    }
    DWORD objLen = 0, cb = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &cb, 0);
    std::vector<uint8_t> obj(objLen);
    if (BCryptGenerateSymmetricKey(hAlg, &hKey, obj.data(), (ULONG)obj.size(), (PUCHAR)key.data(), (ULONG)key.size(), 0) < 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0); return false;
    }
    std::vector<uint8_t> iv(12);
    BCryptGenRandom(nullptr, iv.data(), (ULONG)iv.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    std::vector<uint8_t> tag(16);
    std::vector<uint8_t> ct(plain.size());
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO a;
    BCRYPT_INIT_AUTH_MODE_INFO(a);
    a.pbNonce = iv.data(); a.cbNonce = (ULONG)iv.size();
    a.pbTag = tag.data(); a.cbTag = (ULONG)tag.size();
    ULONG outSize = 0;
    st = BCryptEncrypt(hKey, (PUCHAR)plain.data(), (ULONG)plain.size(), &a, nullptr, 0, ct.data(), (ULONG)ct.size(), &outSize, 0);
    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (st < 0) return false;
    ct.resize(outSize);
    out.clear();
    const char magic[4] = { 'O', 'V', 'A', 'T' };
    out.insert(out.end(), magic, magic + 4);
    auto w32 = [&](uint32_t v) { out.push_back((uint8_t)(v & 0xFF)); out.push_back((uint8_t)((v >> 8) & 0xFF)); out.push_back((uint8_t)((v >> 16) & 0xFF)); out.push_back((uint8_t)((v >> 24) & 0xFF)); };
    w32(1); w32((uint32_t)iv.size()); w32((uint32_t)tag.size()); w32((uint32_t)plain.size());
    out.insert(out.end(), iv.begin(), iv.end());
    out.insert(out.end(), tag.begin(), tag.end());
    out.insert(out.end(), ct.begin(), ct.end());
    return true;
}

std::vector<uint8_t> deriveKeyFromSecretAndName(const std::wstring& dynName)
{
    static const uint8_t secret[32] = { 
        0x1B, 0x18, 0x19, 0x1E, 0x6B, 0x68, 0x69, 0x6E, 
        0x7B, 0x78, 0x79, 0x7E, 0x4B, 0x48, 0x49, 0x4E, 
        0x3B, 0x38, 0x39, 0x3E, 0x2B, 0x28, 0x29, 0x2E, 
        0x0B, 0x08, 0x09, 0x0E, 0x5B, 0x58, 0x59, 0x5E 
    };

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

std::wstring sha256HexOfWide(const std::wstring& w)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s;
    if (len > 0)
    {
        s.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], len, nullptr, nullptr);
    }
    uint8_t dig[32];
    if (!sha256Cng((const uint8_t*)s.data(), (ULONG)s.size(), dig)) return L"";
    return hexOf(dig, 32);
}

void patchPerUserWatermark(std::vector<uint8_t>& bytes, const std::wstring& tokenW)
{
    int tlen = WideCharToMultiByte(CP_UTF8, 0, tokenW.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (tlen <= 1) return;
    std::string tUtf8;
    tUtf8.resize(tlen - 1);
    WideCharToMultiByte(CP_UTF8, 0, tokenW.c_str(), -1, &tUtf8[0], tlen, nullptr, nullptr);
    uint8_t dig[32];
    if (!sha256Cng((const uint8_t*)tUtf8.data(), (ULONG)tUtf8.size(), dig)) return;
    static const char hexd[] = "0123456789abcdef";
    char id16[17];
    for (int i = 0; i < 8; ++i)
    {
        id16[i * 2] = hexd[(dig[i] >> 4) & 0xF];
        id16[i * 2 + 1] = hexd[dig[i] & 0xF];
    }
    id16[16] = '\0';
    const char* ph = "OVSON_UWM:";
    for (size_t i = 0; i + 24 < bytes.size(); ++i)
    {
        if (memcmp(bytes.data() + i, ph, 11) == 0)
        {
            memcpy(bytes.data() + i + 11, id16, 16);
            break;
        }
    }
}

bool decryptEmbeddedIfEncrypted(std::vector<uint8_t>& bytes)
{
    if (bytes.size() < 4) return false;
    if (!(bytes[0] == 'O' && bytes[1] == 'V' && bytes[2] == 'S' && bytes[3] == 'G')) return false;
    const uint8_t* p = bytes.data();
    uint32_t ver = *(const uint32_t*)(p + 4);
    uint32_t ivLen = *(const uint32_t*)(p + 8);
    uint32_t tagLen = *(const uint32_t*)(p + 12);
    uint32_t plainSize = *(const uint32_t*)(p + 16);
    if (ver != 1) return false;
    size_t off = 20;
    if (bytes.size() < off + ivLen + tagLen) return false;
    const uint8_t* iv = p + off; off += ivLen;
    const uint8_t* tag = p + off; off += tagLen;
    size_t ctLen = bytes.size() - off;
    if (plainSize > ctLen) return false;
    const uint8_t* ct = p + off;

    static const uint8_t key[32] = {
        0x6B, 0x6D, 0x6F, 0x6E, 0x69, 0x68, 0x63, 0x61, 0x67, 0x65, 0x0A, 0x0B, 0x08, 0x09, 0x0E, 0x0F, 
        0x1B, 0x18, 0x19, 0x1E, 0x1F, 0x1C, 0x7A, 0x7B, 0x78, 0x79, 0x4B, 0x48, 0x49, 0x4E, 0x01, 0x06 
    };

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    NTSTATUS st;
    ULONG objLen = 0, cb = 0;
    std::vector<uint8_t> keyObj;
    std::vector<uint8_t> plain;
    ULONG out = 0;

    st = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (st < 0) goto cleanup;
    st = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, (ULONG)sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    if (st < 0) goto cleanup;
    st = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &cb, 0);
    if (st < 0) goto cleanup;
    keyObj.resize(objLen);
    st = BCryptGenerateSymmetricKey(hAlg, &hKey, keyObj.data(), (ULONG)keyObj.size(), (PUCHAR)key, (ULONG)sizeof(key), 0);
    if (st < 0) goto cleanup;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO ainfo;
    BCRYPT_INIT_AUTH_MODE_INFO(ainfo);
    ainfo.pbNonce = (PUCHAR)iv; ainfo.cbNonce = (ULONG)ivLen;
    ainfo.pbTag = (PUCHAR)tag; ainfo.cbTag = (ULONG)tagLen;
    plain.resize(plainSize ? plainSize : ctLen);
    st = BCryptDecrypt(hKey, (PUCHAR)ct, (ULONG)ctLen, &ainfo, nullptr, 0, plain.data(), (ULONG)plain.size(), &out, 0);
    if (st < 0) goto cleanup;
    plain.resize(out);
    bytes.swap(plain);
    if (hKey) BCryptDestroyKey(hKey);
    if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    return true;

cleanup:
    if (hKey) BCryptDestroyKey(hKey);
    if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    return false;
}

DWORD findProcessId(const std::wstring& exeName)
{
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
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

bool isModuleLoaded(DWORD pid, const std::wstring& moduleName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
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

bool isOVsonModuleLoaded(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
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

bool isAlreadyInjected(DWORD pid)
{
    HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Global\\OVsonShared");
    if (map)
    {
        volatile LONG* flag = (volatile LONG*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof(LONG));
        if (flag)
        {
            LONG val = *flag;
            UnmapViewOfFile((LPCVOID)flag);
            CloseHandle(map);
            if (val == 1) return true;
        }
        else CloseHandle(map);
    }
    wchar_t name[64];
    wsprintfW(name, L"Global\\OVsonAlive_%lu", pid);
    HANDLE alive = OpenEventW(SYNCHRONIZE, FALSE, name);
    if (alive) { CloseHandle(alive); return true; }
    HANDLE evLoaded = OpenEventW(SYNCHRONIZE, FALSE, L"Global\\OVsonLoaded");
    if (evLoaded) { CloseHandle(evLoaded); return true; }
    if (isOVsonModuleLoaded(pid)) return true;
    return false;
}

void deriveIpcNames(IpcNames& out)
{
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    DWORD pid = GetCurrentProcessId();

    static const uint8_t key[32] = { 
        0x3B, 0x38, 0x39, 0x3E, 0x0B, 0x08, 0x09, 0x0E, 
        0x1B, 0x18, 0x19, 0x1E, 0x6B, 0x68, 0x69, 0x6E, 
        0x7B, 0x78, 0x79, 0x7E, 0x4B, 0x48, 0x49, 0x4E, 
        0x5B, 0x58, 0x59, 0x5E, 0x2B, 0x28, 0x29, 0x2E 
    };

    auto makeTag = [&](const char* label) -> std::wstring
        {
            char buf[128];
            _snprintf_s(buf, _TRUNCATE, "%s|%lu|%llu", label, (unsigned long)pid, (unsigned long long)qpc.QuadPart);
            uint8_t mac[32];
            hmacSha256(key, (ULONG)sizeof(key), (const uint8_t*)buf, (ULONG)strlen(buf), mac);
            std::wstring name = L"Local\\OVN_" + hexOf(mac, 12);
            return name;
        };
    const char* labels[6] = { "Cp1", "Cp2", "Cp3", "Cp4", "Cp5", "Cp6" };
    for (int i = 0; i < 6; ++i)
    {
        std::wstring nm = makeTag(labels[i]);
        wcsncpy_s(out.cp[i], nm.c_str(), _TRUNCATE);
    }
    std::wstring r = makeTag("Ready");
    wcsncpy_s(out.ready, r.c_str(), _TRUNCATE);
    std::wstring inj = makeTag("Injected");
    wcsncpy_s(out.injected, inj.c_str(), _TRUNCATE);
}
