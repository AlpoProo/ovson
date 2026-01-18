#include "StatsOverlay.h"
#include "FontRenderer.h"
#include "../Chat/ChatInterceptor.h"
#include "../Config/Config.h"
#include "../Services/Hypixel.h"
#include "../Utils/Logger.h"
#include <Windows.h>
#include <gl/GL.h>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>

static std::ofstream g_overlayDebugLog;

static void writeOverlayLog(const char* msg) {
	if (!g_overlayDebugLog.is_open()) {
		char path[MAX_PATH];
		GetModuleFileNameA(NULL, path, MAX_PATH);
		std::string exePath(path);
		size_t lastSlash = exePath.find_last_of("\\/");
		std::string dir = exePath.substr(0, lastSlash);
		std::string logPath = dir + "\\ovson_overlay_debug.txt";
		g_overlayDebugLog.open(logPath, std::ios::app);
	}
	if (g_overlayDebugLog.is_open()) {
		SYSTEMTIME st;
		GetLocalTime(&st);
		char timeStr[64];
		sprintf_s(timeStr, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
		g_overlayDebugLog << timeStr << msg << std::endl;
		g_overlayDebugLog.flush();
	}
}

bool StatsOverlay::s_initialized = false;
bool StatsOverlay::s_enabled = false;
bool StatsOverlay::s_lastInsertState = false;

GLuint StatsOverlay::s_displayList = 0;
bool StatsOverlay::s_dirty = false;
int StatsOverlay::s_lastStatsCount = 0;

static FontRenderer g_font;

void StatsOverlay::init()
{
	if (s_initialized) return;
	
	
	s_initialized = true;
	s_enabled = false;
	writeOverlayLog("StatsOverlay initialized");
	Logger::info("StatsOverlay initialized");
}

void StatsOverlay::shutdown()
{
	s_initialized = false;
}

static uint32_t colorFromRGB(int r, int g, int b, int a = 255)
{
	return ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

static uint32_t colorForFKDR(double fkdr)
{
	if (fkdr < 1.0) return colorFromRGB(170, 170, 170); // Gray
	if (fkdr < 2.0) return colorFromRGB(255, 255, 255); // White
	if (fkdr < 3.0) return colorFromRGB(255, 170, 0);   // Gold
	if (fkdr < 4.0) return colorFromRGB(85, 255, 255);  // Aqua
	if (fkdr < 5.0) return colorFromRGB(0, 170, 0);     // Dark Green
	if (fkdr < 6.0) return colorFromRGB(170, 0, 170);   // Purple
	return colorFromRGB(170, 0, 0);                     // Dark Red
}

static uint32_t colorForWLR(double wlr)
{
	if (wlr < 1.0) return colorFromRGB(255, 255, 255);
	if (wlr < 3.0) return colorFromRGB(255, 170, 0);
	if (wlr < 5.0) return colorFromRGB(170, 0, 0);
	return colorFromRGB(170, 0, 170);
}

static uint32_t colorForWins(int wins)
{
    if (wins < 500) return colorFromRGB(170, 170, 170);
    if (wins < 1000) return colorFromRGB(255, 255, 255);
    if (wins < 2000) return colorFromRGB(255, 255, 85);
    if (wins < 4000) return colorFromRGB(170, 0, 0);
    return colorFromRGB(170, 0, 170);
}

static uint32_t colorForFinalKills(int fk)
{
    if (fk < 1000) return colorFromRGB(170, 170, 170);
    if (fk < 2000) return colorFromRGB(255, 255, 255);
    if (fk < 4000) return colorFromRGB(255, 170, 0);
    if (fk < 5000) return colorFromRGB(85, 255, 255);
    if (fk < 10000) return colorFromRGB(170, 0, 0);
    return colorFromRGB(170, 0, 170);
}

static uint32_t colorForStar(int star)
{
	if (star < 100) return colorFromRGB(170, 170, 170);
	if (star < 200) return colorFromRGB(255, 255, 255);
	if (star < 300) return colorFromRGB(255, 170, 0);
	if (star < 400) return colorFromRGB(85, 255, 255);
	if (star < 500) return colorFromRGB(0, 170, 0);
	if (star < 600) return colorFromRGB(85, 255, 255);
	return colorFromRGB(170, 0, 0);
}

static uint32_t colorForTeam(const std::string& team) 
{
	if (team == "Red") return colorFromRGB(255, 85, 85);
	if (team == "Blue") return colorFromRGB(85, 85, 255);
	if (team == "Green") return colorFromRGB(85, 255, 85);
	if (team == "Yellow") return colorFromRGB(255, 255, 85);
	if (team == "Aqua") return colorFromRGB(85, 255, 255);
	if (team == "White") return colorFromRGB(255, 255, 255);
	if (team == "Pink") return colorFromRGB(255, 85, 255);
	if (team == "Gray" || team == "Grey") return colorFromRGB(170, 170, 170);
	return colorFromRGB(170, 170, 170);
}

static float s_panelX = -1.0f;
static float s_panelY = 50.0f;
static bool s_isDragging = false;
static float s_dragOffsetX = 0.0f;
static float s_dragOffsetY = 0.0f;

void StatsOverlay::render(HDC hdc)
{
	static bool s_loggedOnce = false;
	if (!s_loggedOnce) {
		writeOverlayLog("StatsOverlay::render() called for first time");
		s_loggedOnce = true;
	}
	
	if (!g_font.isInitialized()) {
		if (hdc) {
			g_font.init(hdc);
		}
	}

	std::string mode = Config::getOverlayMode();
	if (mode != "gui") return;

	SHORT insertState = GetAsyncKeyState(VK_INSERT);
	bool isInsertDown = (insertState & 0x8000) != 0;
	if (!s_lastInsertState && isInsertDown) {
		s_enabled = !s_enabled;
	}
	s_lastInsertState = isInsertDown;

	if (!s_enabled) return;

	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	float screenWidth = (float)viewport[2];
	float screenHeight = (float)viewport[3];
	
	if (screenWidth <= 0) screenWidth = 1920.0f;
	if (screenHeight <= 0) screenHeight = 1080.0f;

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, screenWidth, screenHeight, 0, -1, 1);
	
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);

	std::vector<std::pair<std::string, Hypixel::PlayerStats>> statsData;
	{
		std::lock_guard<std::mutex> lock(ChatInterceptor::g_statsMutex);
		for (const auto& pair : ChatInterceptor::g_playerStatsMap) {
			statsData.push_back(pair);
		}
	}

	float panelWidth = 780.0f;
	float rowHeight = 30.0f;
	float headerHeight = 35.0f;
	float panelHeight = headerHeight + (statsData.size() * rowHeight) + 15.0f; 
	
	if (panelHeight < 50.0f) panelHeight = 50.0f;

	if (!s_isDragging && s_panelX < 0) { 
		s_panelX = (screenWidth - panelWidth) / 2.0f;
		s_panelY = screenHeight / 5.0f; 
	}
	
	float panelX = s_panelX;
	float panelY = s_panelY;
	
	float colPlayer = 20.0f;
	float colStar   = 220.0f;
	float colFK     = 340.0f;
	float colFKDR   = 470.0f;
	float colWins   = 590.0f;
	float colWLR    = 700.0f;
	if (hdc) {
		HWND hwnd = WindowFromDC(hdc);
		POINT pt;
		if (hwnd && GetCursorPos(&pt)) {
			ScreenToClient(hwnd, &pt);
			float mouseX = (float)pt.x;
			float mouseY = (float)pt.y;
			
			bool isLButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
			
			if (isLButtonDown) {
				if (!s_isDragging) {
					if (mouseX >= s_panelX && mouseX <= s_panelX + panelWidth &&
						mouseY >= s_panelY && mouseY <= s_panelY + headerHeight) {
						s_isDragging = true;
						s_dragOffsetX = mouseX - s_panelX;
						s_dragOffsetY = mouseY - s_panelY;
					}
				} else {
					s_panelX = mouseX - s_dragOffsetX;
					s_panelY = mouseY - s_dragOffsetY;
					
					if (s_panelX < 0) s_panelX = 0;
					if (s_panelY < 0) s_panelY = 0;
					if (s_panelX + panelWidth > screenWidth) s_panelX = screenWidth - panelWidth;
					if (s_panelY + panelHeight > screenHeight) s_panelY = screenHeight - panelHeight;
				}
			} else {
				s_isDragging = false;
			}
		}
	}

	std::sort(statsData.begin(), statsData.end(), [](const auto& a, const auto& b) {
		const std::string& teamA = a.second.teamColor;
		const std::string& teamB = b.second.teamColor;
		
		if (teamA.empty() && !teamB.empty()) return false;
		if (!teamA.empty() && teamB.empty()) return true;
		
		if (teamA != teamB) {
			return teamA < teamB;
		}
		
		return a.first < b.first;
	});

	panelX = s_panelX;
	panelY = s_panelY;
	
	
	int currentStatsSize = (int)statsData.size();
	if (s_displayList == 0) {
		s_displayList = glGenLists(1);
		s_dirty = true;
	}

	if (currentStatsSize != s_lastStatsCount) {
		s_dirty = true;
		s_lastStatsCount = currentStatsSize;
	}

	static DWORD s_lastRebuildTime = 0;
	DWORD now = GetTickCount();
	if ((now - s_lastRebuildTime) > 500) {
		s_dirty = true;
	}

	if (s_dirty) {
		s_lastRebuildTime = now;
		glNewList(s_displayList, GL_COMPILE);
		
		float localX = 0.0f;
		float localY = 0.0f;
		
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_CULL_FACE);
		glDisable(GL_ALPHA_TEST);
		glColor4f(0.0f, 0.0f, 0.0f, 0.45f);
		glBegin(GL_QUADS);
		glVertex2f(localX, localY);
		glVertex2f(localX, localY + panelHeight);
		glVertex2f(localX + panelWidth, localY + panelHeight);
		glVertex2f(localX + panelWidth, localY);
		glEnd();

		glLineWidth(1.0f);
		glColor4f(0.5f, 0.5f, 0.5f, 0.5f);
		glBegin(GL_LINE_LOOP);
		glVertex2f(localX, localY);
		glVertex2f(localX, localY + panelHeight);
		glVertex2f(localX + panelWidth, localY + panelHeight);
		glVertex2f(localX + panelWidth, localY);
		glEnd();

		float sepY = localY + headerHeight;
		glBegin(GL_LINES);
		glVertex2f(localX, sepY);
		glVertex2f(localX + panelWidth, sepY);
		glEnd();

		float headerTextY = localY + 8.0f;
		g_font.drawString(localX + colPlayer, headerTextY, "PLAYER", colorFromRGB(255, 255, 255));
		g_font.drawString(localX + colStar,   headerTextY, "STAR", colorFromRGB(255, 255, 255));
		g_font.drawString(localX + colFK,     headerTextY, "F. KILLS", colorFromRGB(255, 255, 255));
		g_font.drawString(localX + colFKDR,   headerTextY, "FKDR", colorFromRGB(255, 255, 255));
		g_font.drawString(localX + colWins,   headerTextY, "WINS", colorFromRGB(255, 255, 255));
		g_font.drawString(localX + colWLR,    headerTextY, "WLR", colorFromRGB(255, 255, 255));

		float currentY = localY + headerHeight + 5.0f;
		for (const auto& pair : statsData) {
			std::string name = pair.first;
			Hypixel::PlayerStats stats = pair.second;
			
			double fkdr = (stats.bedwarsFinalDeaths == 0) ? stats.bedwarsFinalKills : (double)stats.bedwarsFinalKills / stats.bedwarsFinalDeaths;
			double wlr = (stats.bedwarsLosses == 0) ? stats.bedwarsWins : (double)stats.bedwarsWins / stats.bedwarsLosses;
			
			std::stringstream fkdrSs;
			std::stringstream wlrSs;
			fkdrSs << std::fixed << std::setprecision(2) << fkdr;
			wlrSs << std::fixed << std::setprecision(2) << wlr;

			uint32_t nameColor = stats.teamColor.empty() ? colorFromRGB(170, 170, 170) : colorForTeam(stats.teamColor);
			g_font.drawString(localX + colPlayer, currentY, name, nameColor);
			g_font.drawString(localX + colStar,   currentY, std::to_string(stats.bedwarsStar), colorForStar(stats.bedwarsStar));
			g_font.drawString(localX + colFK,     currentY, std::to_string(stats.bedwarsFinalKills), colorForFinalKills(stats.bedwarsFinalKills));
			g_font.drawString(localX + colFKDR,   currentY, fkdrSs.str(), colorForFKDR(fkdr));
			g_font.drawString(localX + colWins,   currentY, std::to_string(stats.bedwarsWins), colorForWins(stats.bedwarsWins));
			g_font.drawString(localX + colWLR,    currentY, wlrSs.str(), colorForWLR(wlr));

			currentY += rowHeight;
		}
		
		glEndList();
		s_dirty = false;
	}

	glPushMatrix();
	glTranslatef(panelX, panelY, 0.0f);
	glCallList(s_displayList);
	glPopMatrix();

	glPopAttrib();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}
