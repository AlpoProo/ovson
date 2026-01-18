#pragma once
#include <Windows.h>
#include <gl/GL.h>
#include <string>
#include <vector>

class StatsOverlay {
public:
	static void init();
	static void render(HDC hdc);
	static void shutdown();

private:
	static bool s_initialized;
	static bool s_enabled;
	static bool s_lastInsertState;
	
	// cache optimization
	static GLuint s_displayList;
	static bool s_dirty;
	static int s_lastStatsCount;
};
