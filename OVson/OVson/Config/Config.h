#pragma once
#include <string>
#include <Windows.h>


namespace Config {
	bool initialize(HMODULE selfModule);
	bool save();
	const std::string& getApiKey();
	void setApiKey(const std::string& key);
}


