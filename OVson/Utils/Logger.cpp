#include "Logger.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <cstdio>
#include <cstdarg>

static FILE* g_logFile = nullptr;

static void vwrite(const char* level, const char* fmt, va_list args) {
	char buffer[2048];
	vsnprintf(buffer, sizeof(buffer), fmt, args);
    if (g_logFile) {
        fprintf(g_logFile, "[%s] %s\n", level, buffer);
        fflush(g_logFile);
    }
}

bool Logger::initialize(const char* logFileName) {
	if (!g_logFile) {
		fopen_s(&g_logFile, logFileName, "w");
	}
	return g_logFile != nullptr;
}

void Logger::info(const char* fmt, ...) {
	va_list args; va_start(args, fmt);
	vwrite("INFO", fmt, args);
	va_end(args);
}

void Logger::error(const char* fmt, ...) {
	va_list args; va_start(args, fmt);
	vwrite("ERROR", fmt, args);
	va_end(args);
}

void Logger::shutdown() {
	if (g_logFile) {
		fclose(g_logFile);
		g_logFile = nullptr;
	}
}


