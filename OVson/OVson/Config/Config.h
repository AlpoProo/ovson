#pragma once
#include <string>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


namespace Config {
	bool initialize(HMODULE selfModule);
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

	// tab list settings
	bool isTabEnabled();
	void setTabEnabled(bool enabled);
	const std::string& getTabMode(); // "fk", "fkdr", "wins", "wlr"
	void setTabMode(const std::string& mode);

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
}


