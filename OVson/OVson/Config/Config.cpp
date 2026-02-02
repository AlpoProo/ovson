#include "Config.h"
#include "../Utils/Logger.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <string>

static std::string g_configPath;
static std::string g_apiKey;
static std::string g_overlayMode = "chat"; // default to chat mode
static bool g_tabEnabled = false;
static std::string g_tabDisplayMode = "fkdr";
static bool g_tabSortDescending = true;
static std::string g_sortMode = "Team";
static bool g_showStar = true;
static bool g_showFk = true;
static bool g_showFkdr = true;
static bool g_showWins = true;
static bool g_showWlr = true;
static bool g_showWs = true;
static bool g_debugging = false;
static bool g_bedDefenseEnabled = false;
static int g_clickGuiKey = 45; // INSERT
static bool g_clickGuiOn = true; 
static bool g_notificationsEnabled = true;
static bool g_autoGGEnabled = true;
static std::string g_autoGGMessage = "gg";
static DWORD g_themeColor = 0xFF0055A4;
static bool g_motionBlurEnabled = false;
static float g_motionBlurAmount = 0.5f;
static bool g_urchinEnabled = false;
static std::string g_urchinApiKey = "";
static bool g_seraphEnabled = false;
static std::string g_seraphApiKey = "";
static bool g_tagsEnabled = false;
static std::string g_activeTagService = "Urchin";
static bool g_chatBypasserEnabled = false;
static bool g_discordRpcEnabled = true;
static std::string g_discordAppId = "";
static HMODULE g_hModule = nullptr;

static bool g_debugGlobal = true;
static bool g_debugGameDetection = true;
static bool g_debugBedDetection = true;
static bool g_debugUrchin = true;
static bool g_debugSeraph = true;
static bool g_debugGUI = false;
static bool g_debugBedDefense = false;
static bool g_debugGeneral = true;

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

static bool parseJsonBool(const std::string &all, const char *key, bool &out){
	std::string pat = std::string("\"") + key + "\"";
	size_t k = all.find(pat);
	if (k == std::string::npos) return false;
	size_t colon = all.find(':', k);
	if (colon == std::string::npos) return false;
	size_t t = all.find("true", colon);
	size_t f = all.find("false", colon);
	if (t != std::string::npos && (f == std::string::npos || t < f)) {
		out = true;
		return true;
	}
	if (f != std::string::npos) {
		out = false;
		return true;
	}
	return false;
}

static bool parseJsonInt(const std::string &all, const char *key, int &out){
	std::string pat = std::string("\"") + key + "\"";
	size_t k = all.find(pat);
	if (k == std::string::npos) return false;
	size_t colon = all.find(':', k);
	if (colon == std::string::npos) return false;
	size_t start = all.find_first_of("-0123456789", colon);
	if (start == std::string::npos) return false;
	out = std::atoi(all.c_str() + start);
	return true;
}

static bool parseJsonFloat(const std::string &all, const char *key, float &out){
	std::string pat = std::string("\"") + key + "\"";
	size_t k = all.find(pat);
	if (k == std::string::npos) return false;
	size_t colon = all.find(':', k);
	if (colon == std::string::npos) return false;
	size_t start = all.find_first_of("-0123456789.", colon);
	if (start == std::string::npos) return false;
	out = (float)std::atof(all.c_str() + start);
	return true;
}

static bool parseJsonUInt(const std::string &all, const char *key, DWORD &out){
	std::string pat = std::string("\"") + key + "\"";
	size_t k = all.find(pat);
	if (k == std::string::npos) return false;
	size_t colon = all.find(':', k);
	if (colon == std::string::npos) return false;
	size_t start = all.find_first_of("0123456789", colon);
	if (start == std::string::npos) return false;
	out = (DWORD)std::strtoul(all.c_str() + start, nullptr, 10);
	return true;
}

bool Config::initialize(HMODULE self){
    g_hModule = self;
	g_configPath = getConfigPath();
	FILE *f = nullptr;
	fopen_s(&f, g_configPath.c_str(), "r");
	if (!f){
		g_apiKey.clear();
		g_overlayMode = "chat";
		g_tabEnabled = false;
		g_tabDisplayMode = "fkdr";
		g_tabSortDescending = true;
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

	if (!parseJsonBool(all, "tabEnabled", g_tabEnabled))
		g_tabEnabled = false;
	
	if (parseJsonLine(all, "tabDisplayMode", val))
		g_tabDisplayMode = val;
	else
		g_tabDisplayMode = "fkdr";

	if (!parseJsonBool(all, "tabSortDescending", g_tabSortDescending))
		g_tabSortDescending = true;

	if (!parseJsonBool(all, "debugging", g_debugging))
		g_debugging = false;

	if (!parseJsonBool(all, "bedDefenseEnabled", g_bedDefenseEnabled))
		g_bedDefenseEnabled = false;
        
    if (!parseJsonInt(all, "clickGuiKey", g_clickGuiKey))
        g_clickGuiKey = 45;

    if (!parseJsonBool(all, "clickGuiOn", g_clickGuiOn))
        g_clickGuiOn = true;

    if (!parseJsonBool(all, "notificationsEnabled", g_notificationsEnabled))
        g_notificationsEnabled = true; 

    if (!parseJsonBool(all, "autoGGEnabled", g_autoGGEnabled))
        g_autoGGEnabled = true;

    if (parseJsonLine(all, "autoGGMessage", val))
        g_autoGGMessage = val;
    else
        g_autoGGMessage = "gg";

    if (!parseJsonUInt(all, "themeColor", g_themeColor))
        g_themeColor = 0xFF0055A4;

    if (!parseJsonBool(all, "motionBlurEnabled", g_motionBlurEnabled))
        g_motionBlurEnabled = false;

    if (!parseJsonFloat(all, "motionBlurAmount", g_motionBlurAmount))
        g_motionBlurAmount = 0.5f;

    if (!parseJsonBool(all, "urchinEnabled", g_urchinEnabled))
        g_urchinEnabled = false;

    if (parseJsonLine(all, "urchinApiKey", val))
        g_urchinApiKey = val;
    else
        g_urchinApiKey = "";

    if (!parseJsonBool(all, "seraphEnabled", g_seraphEnabled))
        g_seraphEnabled = false;

    if (parseJsonLine(all, "seraphApiKey", val))
        g_seraphApiKey = val;
    else
        g_seraphApiKey = "";

    if (!parseJsonBool(all, "tagsEnabled", g_tagsEnabled))
        g_tagsEnabled = false;
    
    if (parseJsonLine(all, "activeTagService", val))
        g_activeTagService = val;
    else
        g_activeTagService = "Urchin";

    if (!parseJsonBool(all, "chatBypasserEnabled", g_chatBypasserEnabled))
        g_chatBypasserEnabled = false;

    if (!parseJsonBool(all, "discordRpcEnabled", g_discordRpcEnabled))
        g_discordRpcEnabled = true;

    if (parseJsonLine(all, "discordAppId", val))
        g_discordAppId = val;
    else
        g_discordAppId = "1467865675262329019";

    // Migration: If the loaded ID is the old broken one, force it to the new one
    if (g_discordAppId == "1335272304856010773") {
        g_discordAppId = "1467865675262329019";
        save(); // Save the new ID to config.json immediately
    }

    if (parseJsonLine(all, "sortMode", val))
        g_sortMode = val;
    else
        g_sortMode = "Team";

    if (!parseJsonBool(all, "showStar", g_showStar)) g_showStar = true;
    if (!parseJsonBool(all, "showFk", g_showFk)) g_showFk = true;
    if (!parseJsonBool(all, "showFkdr", g_showFkdr)) g_showFkdr = true;
    if (!parseJsonBool(all, "showWins", g_showWins)) g_showWins = true;
    if (!parseJsonBool(all, "showWlr", g_showWlr)) g_showWlr = true;
    if (!parseJsonBool(all, "showWs", g_showWs)) g_showWs = true;

    parseJsonBool(all, "debugGlobal", g_debugGlobal);
    parseJsonBool(all, "debugGameDetection", g_debugGameDetection);
    parseJsonBool(all, "debugBedDetection", g_debugBedDetection);
    parseJsonBool(all, "debugUrchin", g_debugUrchin);
    parseJsonBool(all, "debugSeraph", g_debugSeraph);
    parseJsonBool(all, "debugGUI", g_debugGUI);
    parseJsonBool(all, "debugBedDefense", g_debugBedDefense);
    parseJsonBool(all, "debugGeneral", g_debugGeneral);

	return true;
}

HMODULE Config::getModuleHandle() { return g_hModule; }

bool Config::save(){
	FILE *f = nullptr;
	fopen_s(&f, g_configPath.c_str(), "w");
	if (!f)
		return false;
	fprintf(f, "{\n"
		"  \"apiKey\": \"%s\",\n"
		"  \"overlayMode\": \"%s\",\n"
		"  \"tabEnabled\": %s,\n"
		"  \"debugging\": %s,\n"
		"  \"bedDefenseEnabled\": %s,\n"
		"  \"clickGuiKey\": %d,\n"
		"  \"clickGuiOn\": %s,\n"
		"  \"notificationsEnabled\": %s,\n"
		"  \"autoGGEnabled\": %s,\n"
		"  \"autoGGMessage\": \"%s\",\n"
		"  \"themeColor\": %u,\n"
		"  \"motionBlurEnabled\": %s,\n"
		"  \"motionBlurAmount\": %.2f,\n"
		"  \"urchinEnabled\": %s,\n"
		"  \"urchinApiKey\": \"%s\",\n"
		"  \"seraphEnabled\": %s,\n"
		"  \"seraphApiKey\": \"%s\",\n"
        "  \"tagsEnabled\": %s,\n"
        "  \"activeTagService\": \"%s\",\n"
        "  \"chatBypasserEnabled\": %s,\n"
        "  \"debugGlobal\": %s,\n"
        "  \"debugGameDetection\": %s,\n"
        "  \"debugBedDetection\": %s,\n"
        "  \"debugUrchin\": %s,\n"
        "  \"debugSeraph\": %s,\n"
        "  \"debugGUI\": %s,\n"
        "  \"debugBedDefense\": %s,\n"
        "  \"debugGeneral\": %s,\n"
        "  \"discordRpcEnabled\": %s,\n"
        "  \"discordAppId\": \"%s\",\n"
        "  \"tabDisplayMode\": \"%s\",\n"
        "  \"tabSortDescending\": %s,\n"
        "  \"sortMode\": \"%s\",\n"
        "  \"showStar\": %s,\n"
        "  \"showFk\": %s,\n"
        "  \"showFkdr\": %s,\n"
        "  \"showWins\": %s,\n"
        "  \"showWlr\": %s,\n"
        "  \"showWs\": %s\n"
		"}\n", 
 		g_apiKey.c_str(), 
        g_overlayMode.c_str(), 
        g_tabEnabled ? "true" : "false", 
        g_debugging ? "true" : "false", 
        g_bedDefenseEnabled ? "true" : "false",
        g_clickGuiKey, 
        g_clickGuiOn ? "true" : "false", 
        g_notificationsEnabled ? "true" : "false", 
        g_autoGGEnabled ? "true" : "false", 
        g_autoGGMessage.c_str(),
        g_themeColor, 
        g_motionBlurEnabled ? "true" : "false", 
        g_motionBlurAmount,
        g_urchinEnabled ? "true" : "false", 
        g_urchinApiKey.c_str(),
        g_seraphEnabled ? "true" : "false", 
        g_seraphApiKey.c_str(),
        g_tagsEnabled ? "true" : "false", 
        g_activeTagService.c_str(),
        g_chatBypasserEnabled ? "true" : "false",
        g_debugGlobal ? "true" : "false", 
        g_debugGameDetection ? "true" : "false", 
        g_debugBedDetection ? "true" : "false", 
        g_debugUrchin ? "true" : "false", 
        g_debugSeraph ? "true" : "false", 
        g_debugGUI ? "true" : "false", 
        g_debugBedDefense ? "true" : "false", 
        g_debugGeneral ? "true" : "false",
        g_discordRpcEnabled ? "true" : "false", 
        g_discordAppId.c_str(),
        g_tabDisplayMode.c_str(), 
        g_tabSortDescending ? "true" : "false",
        g_sortMode.c_str(),
        g_showStar ? "true" : "false",
        g_showFk ? "true" : "false",
        g_showFkdr ? "true" : "false",
        g_showWins ? "true" : "false",
        g_showWlr ? "true" : "false",
        g_showWs ? "true" : "false");
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

bool Config::isTabEnabled() { return g_tabEnabled; }
void Config::setTabEnabled(bool enabled) { g_tabEnabled = enabled; save(); }

const std::string& Config::getSortMode() { return g_sortMode; }
void Config::setSortMode(const std::string& mode) { g_sortMode = mode; save(); }

const std::string& Config::getTabDisplayMode() { return g_tabDisplayMode; }
void Config::setTabDisplayMode(const std::string& mode) { g_tabDisplayMode = mode; save(); }

bool Config::isTabSortDescending() { return g_tabSortDescending; }
void Config::setTabSortDescending(bool desc) { g_tabSortDescending = desc; save(); }

bool Config::isDebugging() { return g_debugging; }
void Config::setDebugging(bool enabled) { g_debugging = enabled; save(); }

bool Config::isBedDefenseEnabled() { return g_bedDefenseEnabled; }
void Config::setBedDefenseEnabled(bool enabled) { g_bedDefenseEnabled = enabled; save(); }

int Config::getClickGuiKey() { return g_clickGuiKey; }
void Config::setClickGuiKey(int key) { g_clickGuiKey = key; save(); }

bool Config::isNotificationsEnabled() { return g_notificationsEnabled; }
void Config::setNotificationsEnabled(bool enabled) { g_notificationsEnabled = enabled; save(); }

bool Config::isClickGuiOn() { return g_clickGuiOn; }
void Config::setClickGuiOn(bool on) { g_clickGuiOn = on; save(); }

bool Config::isAutoGGEnabled() { return g_autoGGEnabled; }
void Config::setAutoGGEnabled(bool enabled) { g_autoGGEnabled = enabled; save(); }
const std::string& Config::getAutoGGMessage() { return g_autoGGMessage; }
void Config::setAutoGGMessage(const std::string& msg) { g_autoGGMessage = msg; save(); }

DWORD Config::getThemeColor() { return g_themeColor; }
void Config::setThemeColor(DWORD color) { g_themeColor = color; save(); }

bool Config::isMotionBlurEnabled() { return g_motionBlurEnabled; }
void Config::setMotionBlurEnabled(bool enabled) { g_motionBlurEnabled = enabled; save(); }
float Config::getMotionBlurAmount() { return g_motionBlurAmount; }
void Config::setMotionBlurAmount(float amount) { g_motionBlurAmount = amount; save(); }

bool Config::isUrchinEnabled() { return g_urchinEnabled; }
void Config::setUrchinEnabled(bool enabled) { g_urchinEnabled = enabled; save(); }
const std::string& Config::getUrchinApiKey() { return g_urchinApiKey; }
void Config::setUrchinApiKey(const std::string& key) { g_urchinApiKey = key; save(); }

bool Config::isSeraphEnabled() { return g_seraphEnabled; }
void Config::setSeraphEnabled(bool enabled) { g_seraphEnabled = enabled; save(); }
const std::string& Config::getSeraphApiKey() { return g_seraphApiKey; }
void Config::setSeraphApiKey(const std::string& key) { g_seraphApiKey = key; save(); }

bool Config::isTagsEnabled() { return g_tagsEnabled; }
void Config::setTagsEnabled(bool enabled) { g_tagsEnabled = enabled; save(); }
const std::string& Config::getActiveTagService() { return g_activeTagService; }
void Config::setActiveTagService(const std::string& service) { g_activeTagService = service; save(); }

bool Config::isChatBypasserEnabled() { return g_chatBypasserEnabled; }
void Config::setChatBypasserEnabled(bool enabled) { g_chatBypasserEnabled = enabled; save(); }

bool Config::isDebugEnabled(DebugCategory cat) {
    if (!g_debugGlobal) return false;
    switch (cat) {
        case DebugCategory::General: return g_debugGeneral;
        case DebugCategory::GameDetection: return g_debugGameDetection;
        case DebugCategory::BedDetection: return g_debugBedDetection;
        case DebugCategory::Urchin: return g_debugUrchin;
        case DebugCategory::Seraph: return g_debugSeraph;
        case DebugCategory::GUI: return g_debugGUI;
        case DebugCategory::BedDefense: return g_debugBedDefense;
        default: return false;
    }
}

void Config::setDebugEnabled(DebugCategory cat, bool enabled) {
    switch (cat) {
        case DebugCategory::General: g_debugGeneral = enabled; break;
        case DebugCategory::GameDetection: g_debugGameDetection = enabled; break;
        case DebugCategory::BedDetection: g_debugBedDetection = enabled; break;
        case DebugCategory::Urchin: g_debugUrchin = enabled; break;
        case DebugCategory::Seraph: g_debugSeraph = enabled; break;
        case DebugCategory::GUI: g_debugGUI = enabled; break;
        case DebugCategory::BedDefense: g_debugBedDefense = enabled; break;
    }
    save();
}

bool Config::isGlobalDebugEnabled() { return g_debugGlobal; }
void Config::setGlobalDebugEnabled(bool enabled) { g_debugGlobal = enabled; save(); }

bool Config::isDiscordRpcEnabled() { return g_discordRpcEnabled; }
void Config::setDiscordRpcEnabled(bool enabled) { g_discordRpcEnabled = enabled; save(); }
const std::string& Config::getDiscordAppId() { return g_discordAppId; }
void Config::setDiscordAppId(const std::string& id) { g_discordAppId = id; save(); }


bool Config::isShowStar() { return g_showStar; }
void Config::setShowStar(bool show) { g_showStar = show; save(); }
bool Config::isShowFk() { return g_showFk; }
void Config::setShowFk(bool show) { g_showFk = show; save(); }
bool Config::isShowFkdr() { return g_showFkdr; }
void Config::setShowFkdr(bool show) { g_showFkdr = show; save(); }
bool Config::isShowWins() { return g_showWins; }
void Config::setShowWins(bool show) { g_showWins = show; save(); }
bool Config::isShowWlr() { return g_showWlr; }
void Config::setShowWlr(bool show) { g_showWlr = show; save(); }
bool Config::isShowWs() { return g_showWs; }
void Config::setShowWs(bool show) { g_showWs = show; save(); }

