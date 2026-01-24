#pragma once
#include <string>

#include "../Config/Config.h"

namespace Logger {
	bool initialize(const char* logFileName = "OVson_debug.log");
	void info(const char* fmt, ...);
	void error(const char* fmt, ...);
    
    void log(Config::DebugCategory cat, const char* fmt, ...);

	void shutdown();
}


