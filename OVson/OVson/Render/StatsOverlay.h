#pragma once
#include <cstdint>
#include <string>
#include <vector>

// forward declaration of OpenGL type if needed, or just use unsigned int
typedef unsigned int GLuint;

class StatsOverlay {
public:
	static void init();
	static void render(void* hdc);
	static void shutdown();
    static void setEnabled(bool enabled) { s_enabled = enabled; }
    static bool isEnabled() { return s_enabled; }

private:
	static bool s_initialized;
	static bool s_enabled;
	static bool s_lastInsertState;
	
	// cache optimization
	static GLuint s_displayList;
	static bool s_dirty;
	static int s_lastStatsCount;
};
