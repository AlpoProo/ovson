#pragma once
#include <string>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


namespace Config {
	bool initialize(HMODULE selfModule);
    HMODULE getModuleHandle();
	bool save();
	const std::string& getApiKey();
	void setApiKey(const std::string& key);
	
	// overlay mode: "gui", "chat", "invisible"
	const std::string& getOverlayMode();
	void setOverlayMode(const std::string& mode);

    // AutoGG settings
    bool isAutoGGEnabled();
    void setAutoGGEnabled(bool enabled);
    const std::string& getAutoGGMessage();
    void setAutoGGMessage(const std::string& msg);

	bool isTabEnabled();
	void setTabEnabled(bool enabled);
	const std::string& getSortMode(); // General sort metric (Stars, FKDR, etc.)
	void setSortMode(const std::string& mode);
	
    const std::string& getTabDisplayMode(); // What stat shows in Tab
    void setTabDisplayMode(const std::string& mode);
    
    bool isTabSortDescending();
    void setTabSortDescending(bool desc);

	bool isDebugging();
	void setDebugging(bool enabled);

	// bed defense settings
	bool isBedDefenseEnabled();
	void setBedDefenseEnabled(bool enabled);

    // click gui settings
    int getClickGuiKey();
    void setClickGuiKey(int key);

    bool isNotificationsEnabled();
    void setNotificationsEnabled(bool enabled);

    bool isClickGuiOn();
    void setClickGuiOn(bool on);

    // theme customization
    DWORD getThemeColor();
    void setThemeColor(DWORD color);

    // motion blur (gonna make this work one day)
    bool isMotionBlurEnabled();
    void setMotionBlurEnabled(bool enabled);
    float getMotionBlurAmount();
    void setMotionBlurAmount(float amount);

    // tags general
    bool isTagsEnabled();
    void setTagsEnabled(bool enabled);
    const std::string& getActiveTagService();
    void setActiveTagService(const std::string& service);

    bool isChatBypasserEnabled();
    void setChatBypasserEnabled(bool enabled);

    // overlay category visibility
    bool isShowStar();
    void setShowStar(bool show);
    bool isShowFk();
    void setShowFk(bool show);
    bool isShowFkdr();
    void setShowFkdr(bool show);
    bool isShowWins();
    void setShowWins(bool show);
    bool isShowWlr();
    void setShowWlr(bool show);
    bool isShowWs();
    void setShowWs(bool show);

    // urchin tags
    bool isUrchinEnabled();
    void setUrchinEnabled(bool enabled);
    const std::string& getUrchinApiKey();
    void setUrchinApiKey(const std::string& key);

    // seraph tags
    bool isSeraphEnabled();
    void setSeraphEnabled(bool enabled);
    const std::string& getSeraphApiKey();
    void setSeraphApiKey(const std::string& key);

    // granular debugging
    enum class DebugCategory {
        General,
        GameDetection,
        BedDetection,
        Urchin,
        Seraph,
        GUI,
        BedDefense
    };

    bool isDebugEnabled(DebugCategory cat);
    void setDebugEnabled(DebugCategory cat, bool enabled);
    bool isGlobalDebugEnabled();
    void setGlobalDebugEnabled(bool enabled);
    bool isDiscordRpcEnabled();
    void setDiscordRpcEnabled(bool enabled);
    const std::string& getDiscordAppId();
    void setDiscordAppId(const std::string& id);
}


