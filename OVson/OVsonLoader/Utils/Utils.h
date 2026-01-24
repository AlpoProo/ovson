#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <cstdint>

const DWORD kBuildId = 0x20251023;

std::wstring decodeXorW(const uint16_t* enc, size_t n, uint8_t key);
std::wstring hexOf(const uint8_t* b, size_t n);

bool sha256Cng(const uint8_t* data, ULONG len, uint8_t out[32]);
bool hmacSha256(const uint8_t* key, ULONG keyLen, const uint8_t* data, ULONG dataLen, uint8_t out[32]);
bool aesGcmEncrypt(const std::vector<uint8_t>& key, const std::vector<uint8_t>& plain, std::vector<uint8_t>& out);
std::vector<uint8_t> deriveKeyFromSecretAndName(const std::wstring& dynName);
std::wstring sha256HexOfWide(const std::wstring& w);
void patchPerUserWatermark(std::vector<uint8_t>& bytes, const std::wstring& tokenW);
bool decryptEmbeddedIfEncrypted(std::vector<uint8_t>& bytes);

DWORD findProcessId(const std::wstring& exeName);
bool isModuleLoaded(DWORD pid, const std::wstring& moduleName);
bool isOVsonModuleLoaded(DWORD pid);
bool isAlreadyInjected(DWORD pid);

struct IpcNames
{
    wchar_t cp[6][64];
    wchar_t ready[64];
    wchar_t injected[64];
};
void deriveIpcNames(IpcNames& out);
