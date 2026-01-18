#pragma once
#include <string>

namespace Logger {
	bool initialize(const char* logFileName = "OVson_debug.log");
	void info(const char* fmt, ...);
	void error(const char* fmt, ...);
	void shutdown();
}


