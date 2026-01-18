#include "Config.h"
#include "../Utils/Logger.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <string>

static std::string g_configPath;
static std::string g_apiKey;
static std::string g_overlayMode = "chat"; // default to chat mode

static std::string getConfigDir(){
	char appdata[MAX_PATH]{};
	if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
	{
		std::string dir = std::string(appdata) + "\\OVson";
		CreateDirectoryA(dir.c_str(), nullptr);
		return dir;
	}
	return ".";
}

static std::string getConfigPath(){
	return getConfigDir() + "\\config.json";
}

static bool parseJsonLine(const std::string &line, const char *key, std::string &out){
	std::string pat = std::string("\"") + key + "\"";
	size_t k = line.find(pat);
	if (k == std::string::npos)
		return false;
	size_t q1 = line.find('"', k + pat.size());
	if (q1 == std::string::npos)
		return false;
	size_t q2 = line.find('"', q1 + 1);
	if (q2 == std::string::npos)
		return false;
	out = line.substr(q1 + 1, q2 - (q1 + 1));
	return true;
}

bool Config::initialize(HMODULE){
	g_configPath = getConfigPath();
	FILE *f = nullptr;
	fopen_s(&f, g_configPath.c_str(), "r");
	if (!f){
		g_apiKey.clear();
		g_overlayMode = "chat";
		return save();
	}
	char buf[2048];
	std::string all;
	while (fgets(buf, sizeof(buf), f))
		all += buf;
	fclose(f);
	std::string val;
	if (parseJsonLine(all, "apiKey", val))
		g_apiKey = val;
	else
		g_apiKey.clear();
	if (parseJsonLine(all, "overlayMode", val))
		g_overlayMode = val;
	else
		g_overlayMode = "chat";
	return true;
}

bool Config::save(){
	FILE *f = nullptr;
	fopen_s(&f, g_configPath.c_str(), "w");
	if (!f)
		return false;
	fprintf(f, "{\n  \"apiKey\": \"%s\",\n  \"overlayMode\": \"%s\"\n}\n", g_apiKey.c_str(), g_overlayMode.c_str());
	fclose(f);
	return true;
}

const std::string &Config::getApiKey() { return g_apiKey; }

void Config::setApiKey(const std::string &key){
	g_apiKey = key;
	save();
}

const std::string &Config::getOverlayMode() { return g_overlayMode; }

void Config::setOverlayMode(const std::string &mode){
	g_overlayMode = mode;
	save();
}
