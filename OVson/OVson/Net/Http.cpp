#include "Http.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

static bool parseUrl(const std::string &url, std::wstring &host, INTERNET_PORT &port, std::wstring &path, bool &https)
{
	https = false;
	port = 80;
	host.clear();
	path = L"/";
	std::string u = url;
	const std::string httpsPref = "https://";
	const std::string httpPref = "http://";
	if (u.rfind(httpsPref, 0) == 0)
	{
		https = true;
		u = u.substr(httpsPref.size());
		port = 443;
	}
	else if (u.rfind(httpPref, 0) == 0)
	{
		https = false;
		u = u.substr(httpPref.size());
		port = 80;
	}
	size_t slash = u.find('/');
	std::string hostPort = (slash == std::string::npos) ? u : u.substr(0, slash);
	std::string pathStr = (slash == std::string::npos) ? "/" : u.substr(slash);
	size_t colon = hostPort.find(':');
	if (colon != std::string::npos)
	{
		port = static_cast<INTERNET_PORT>(atoi(hostPort.substr(colon + 1).c_str()));
		hostPort = hostPort.substr(0, colon);
	}
	host.assign(hostPort.begin(), hostPort.end());
	path.assign(pathStr.begin(), pathStr.end());
	return !host.empty();
}

bool Http::get(const std::string &url, std::string &responseBody, const std::string &headerName, const std::string &headerValue)
{
	std::wstring host, path;
	INTERNET_PORT port;
	bool https;
	if (!parseUrl(url, host, port, path, https))
		return false;
	HINTERNET hSession = WinHttpOpen(L"OVson/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession)
		return false;

	// Force TLS 1.2/1.3
	DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | 0x00000800; // 0x00000800 is TLS 1.3
	WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

	HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
	if (!hConnect)
	{
		WinHttpCloseHandle(hSession);
		return false;
	}
	DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (!hRequest)
	{
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	if (!headerName.empty())
	{
		std::wstring hname(headerName.begin(), headerName.end());
		std::wstring hval(headerValue.begin(), headerValue.end());
		std::wstring header = hname + L": " + hval + L"\r\n";
		WinHttpAddRequestHeaders(hRequest, header.c_str(), (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
	}
	
	DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | 
	                      SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
	WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));

	WinHttpSetOption(hRequest, WINHTTP_OPTION_CLIENT_CERT_CONTEXT, WINHTTP_NO_CLIENT_CERT_CONTEXT, 0);
	
	if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
	{
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	if (!WinHttpReceiveResponse(hRequest, NULL))
	{
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	DWORD statusCode = 0;
	DWORD len = sizeof(statusCode);
	WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &len, WINHTTP_NO_HEADER_INDEX);
	
	responseBody.clear();
	for (;;)
	{
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0)
			break;
		std::string chunk;
		chunk.resize(avail);
		DWORD read = 0;
		if (!WinHttpReadData(hRequest, &chunk[0], avail, &read) || read == 0)
			break;
		responseBody.append(chunk.data(), chunk.data() + read);
	}
	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	
	return (statusCode == 200);
}

bool Http::postJson(const std::string &url, const std::string &jsonBody, std::string &responseBody)
{
	std::wstring host, path;
	INTERNET_PORT port;
	bool https;
	if (!parseUrl(url, host, port, path, https))
		return false;
	HINTERNET hSession = WinHttpOpen(L"OVson/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession)
		return false;

	// Force TLS 1.2/1.3
	DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | 0x00000800;
	WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

	HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
	if (!hConnect)
	{
		WinHttpCloseHandle(hSession);
		return false;
	}
	DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (!hRequest)
	{
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	std::wstring hdr = L"Content-Type: application/json\r\n";
	WinHttpAddRequestHeaders(hRequest, hdr.c_str(), (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
	
	DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | 
	                      SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
	WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));
	WinHttpSetOption(hRequest, WINHTTP_OPTION_CLIENT_CERT_CONTEXT, WINHTTP_NO_CLIENT_CERT_CONTEXT, 0);

	if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)jsonBody.data(), (DWORD)jsonBody.size(), (DWORD)jsonBody.size(), 0))
	{
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	if (!WinHttpReceiveResponse(hRequest, NULL))
	{
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	DWORD statusCode = 0;
	DWORD len = sizeof(statusCode);
	WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &len, WINHTTP_NO_HEADER_INDEX);
	
	responseBody.clear();
	for (;;)
	{
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0)
			break;
		std::string chunk;
		chunk.resize(avail);
		DWORD read = 0;
		if (!WinHttpReadData(hRequest, &chunk[0], avail, &read) || read == 0)
			break;
		responseBody.append(chunk.data(), chunk.data() + read);
	}
	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return (statusCode == 200);
}
