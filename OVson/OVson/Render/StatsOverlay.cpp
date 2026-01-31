#include "StatsOverlay.h"
#include "FontRenderer.h"
#include "../Chat/ChatInterceptor.h"
#include "../Config/Config.h"
#include "../Services/Hypixel.h"
#include "../Services/UrchinService.h"
#include "../Services/SeraphService.h"
#include "../Utils/Logger.h"
#include "../Java.h"
#include <Windows.h>
#include <gl/GL.h>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cctype>

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

// will change these colors too when i got time

static uint32_t colorForFKDR(double fkdr)
{
	if (fkdr < 1.0) return colorFromRGB(170, 170, 170); // Gray
	if (fkdr < 2.0) return colorFromRGB(255, 255, 255); // White
	if (fkdr < 3.0) return colorFromRGB(255, 170, 0);   // Gold
	if (fkdr < 4.0) return colorFromRGB(85, 255, 255);  // Aqua
	if (fkdr < 5.0) return colorFromRGB(85, 255, 85);   // Light Green
	if (fkdr < 6.0) return colorFromRGB(170, 0, 170);   // Purple
	return colorFromRGB(255, 85, 85);                   // Light Red
}

static uint32_t colorForWLR(double wlr)
{
	if (wlr < 1.0) return colorFromRGB(255, 255, 255);
	if (wlr < 3.0) return colorFromRGB(255, 170, 0);
	if (wlr < 5.0) return colorFromRGB(255, 85, 85);
	return colorFromRGB(170, 0, 170);
}

static uint32_t colorForWins(int wins)
{
    if (wins < 500) return colorFromRGB(170, 170, 170);
    if (wins < 1000) return colorFromRGB(255, 255, 255);
    if (wins < 2000) return colorFromRGB(255, 255, 85);
    if (wins < 4000) return colorFromRGB(255, 85, 85);
    return colorFromRGB(170, 0, 170);
}

static uint32_t colorForWinstreak(int ws)
{
    if (ws < 3) return colorFromRGB(170, 170, 170);
    if (ws < 5) return colorFromRGB(255, 255, 255);
    if (ws < 10) return colorFromRGB(255, 170, 0);
    if (ws < 20) return colorFromRGB(85, 255, 255);
    if (ws < 50) return colorFromRGB(85, 255, 85);
    if (ws < 100) return colorFromRGB(170, 0, 170);
    return colorFromRGB(255, 85, 85); 
}

static uint32_t colorForFinalKills(int fk)
{
    if (fk < 1000) return colorFromRGB(170, 170, 170);
    if (fk < 2000) return colorFromRGB(255, 255, 255);
    if (fk < 4000) return colorFromRGB(255, 170, 0);
    if (fk < 5000) return colorFromRGB(85, 255, 255);
    if (fk < 10000) return colorFromRGB(255, 85, 85);
    return colorFromRGB(170, 0, 170);
}

static uint32_t colorForStar(int star)
{
	if (star < 100) return colorFromRGB(170, 170, 170);
	if (star < 200) return colorFromRGB(255, 255, 255);
	if (star < 300) return colorFromRGB(255, 170, 0);
	if (star < 400) return colorFromRGB(85, 255, 255);
	if (star < 500) return colorFromRGB(85, 255, 85);
	if (star < 600) return colorFromRGB(85, 255, 255);
	return colorFromRGB(255, 85, 85);
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
	if (team == "Gray" || team == "Grey") return colorFromRGB(85, 85, 85);
	return colorFromRGB(85, 85, 85);
}

static float s_panelX = -1.0f;
static float s_panelY = 50.0f;
static bool s_isDragging = false;
static float s_dragOffsetX = 0.0f;
static float s_dragOffsetY = 0.0f;

static uint32_t hsbToRgb(float h, float s, float b) {
	float r = 0, g = 0, bl = 0;
	if (s == 0) {
		r = g = bl = b;
	}
	else {
		float h_val = (h - floor(h)) * 6.0f;
		float f = h_val - floor(h_val);
		float p = b * (1.0f - s);
		float q = b * (1.0f - s * f);
		float t = b * (1.0f - s * (1.0f - f));
		switch ((int)h_val) {
		case 0: r = b; g = t; bl = p; break;
		case 1: r = q; g = b; bl = p; break;
		case 2: r = p; g = b; bl = t; break;
		case 3: r = p; g = q; bl = b; break;
		case 4: r = t; g = p; bl = b; break;
		case 5: r = b; g = p; bl = q; break;
		}
	}
	return colorFromRGB((int)(r * 255), (int)(g * 255), (int)(bl * 255));
}

void StatsOverlay::render(void* hdcPtr)
{
	HDC hdc = (HDC)hdcPtr;
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

	jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
	JNIEnv* env = lc->getEnv();
	if (!env || !mcCls) return;

	jfieldID f_mc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
	if (!f_mc) f_mc = env->GetStaticFieldID(mcCls, "field_71412_C", "Lnet/minecraft/client/Minecraft;");
	if (!f_mc) return;

	jobject mc = env->GetStaticObjectField(mcCls, f_mc);
	if (!mc) return;

	jfieldID f_screen = env->GetFieldID(mcCls, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;");
	if (!f_screen) f_screen = env->GetFieldID(mcCls, "field_71462_r", "Lnet/minecraft/client/gui/GuiScreen;");
	
	jobject screen = f_screen ? env->GetObjectField(mc, f_screen) : nullptr;

	if (screen) {
		jclass chatCls = lc->GetClass("net.minecraft.client.gui.GuiChat");
		if (chatCls && env->IsInstanceOf(screen, chatCls)) {
			jfieldID f_inputField = env->GetFieldID(chatCls, "inputField", "Lnet/minecraft/client/gui/GuiTextField;");
			if (!f_inputField) f_inputField = env->GetFieldID(chatCls, "field_146415_a", "Lnet/minecraft/client/gui/GuiTextField;");
			jobject inputField = f_inputField ? env->GetObjectField(screen, f_inputField) : nullptr;

			if (inputField) {
				jclass tfCls = lc->GetClass("net.minecraft.client.gui.GuiTextField");
				jmethodID m_getText = tfCls ? env->GetMethodID(tfCls, "getText", "()Ljava/lang/String;") : nullptr;
				if (tfCls && !m_getText) m_getText = env->GetMethodID(tfCls, "func_146179_b", "()Ljava/lang/String;");
				jstring textJ = m_getText ? (jstring)env->CallObjectMethod(inputField, m_getText) : nullptr;

				if (textJ) {
					const char* utf = env->GetStringUTFChars(textJ, 0);
					if (utf && utf[0] == '.') {
						jfieldID f_x = env->GetFieldID(tfCls, "xPosition", "I");
						if (!f_x) f_x = env->GetFieldID(tfCls, "field_146209_f", "I");
						jfieldID f_y = env->GetFieldID(tfCls, "yPosition", "I");
						if (!f_y) f_y = env->GetFieldID(tfCls, "field_146210_g", "I");
						jfieldID f_w = env->GetFieldID(tfCls, "width", "I");
						if (!f_w) f_w = env->GetFieldID(tfCls, "field_146218_h", "I");
						jfieldID f_h = env->GetFieldID(tfCls, "height", "I");
						if (!f_h) f_h = env->GetFieldID(tfCls, "field_146219_i", "I");

						if (f_x && f_y && f_w && f_h) {
							float x = (float)env->GetIntField(inputField, f_x);
							float y = (float)env->GetIntField(inputField, f_y);
							float w = (float)env->GetIntField(inputField, f_w);
							float h = (float)env->GetIntField(inputField, f_h);

							jclass screenCls = lc->GetClass("net.minecraft.client.gui.GuiScreen");
							jfieldID f_sw = screenCls ? env->GetFieldID(screenCls, "width", "I") : nullptr;
							if (screenCls && !f_sw) f_sw = env->GetFieldID(screenCls, "field_146294_l", "I");
							jfieldID f_sh = screenCls ? env->GetFieldID(screenCls, "height", "I") : nullptr;
							if (screenCls && !f_sh) f_sh = env->GetFieldID(screenCls, "field_146295_m", "I");

							float sw = (f_sw) ? (float)env->GetIntField(screen, f_sw) : 427.0f;
							float sh = (f_sh) ? (float)env->GetIntField(screen, f_sh) : 240.0f;

							glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
							glOrtho(0, sw, sh, 0, -1, 1);
							glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
							glPushAttrib(GL_ALL_ATTRIB_BITS);
							glDisable(GL_TEXTURE_2D); glDisable(GL_DEPTH_TEST);
							glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
							glEnable(GL_LINE_SMOOTH);

							float time = (float)(GetTickCount64() % 4000) / 4000.0f;
							
							for (int i = 0; i < 2; i++) {
								glLineWidth(1.0f + (i * 1.0f));
								glBegin(GL_LINE_LOOP);
								for (int j = 0; j < 4; j++) {
									float shift = (float)j / 4.0f + time;
									uint32_t color = hsbToRgb(shift, 0.8f, 1.0f);
									float r = ((color >> 16) & 0xFF) / 255.0f;
									float g = ((color >> 8) & 0xFF) / 255.0f;
									float b = (color & 0xFF) / 255.0f;
									glColor4f(r, g, b, i == 0 ? 0.9f : 0.25f);

									if (j == 0) glVertex2f(x - 2.0f, y - 1.0f);
									if (j == 1) glVertex2f(x + w - 2.0f, y - 1.0f);
									if (j == 2) glVertex2f(x + w - 2.0f, y + h - 1.0f);
									if (j == 3) glVertex2f(x - 2.0f, y + h - 1.0f);
								}
								glEnd();
							}

							glPopAttrib();
							glMatrixMode(GL_MODELVIEW); glPopMatrix();
							glMatrixMode(GL_PROJECTION); glPopMatrix();
						}
					}
					env->ReleaseStringUTFChars(textJ, utf);
					env->DeleteLocalRef(textJ);
				}
				env->DeleteLocalRef(inputField);
			}
		}
		env->DeleteLocalRef(screen);
	}
	env->DeleteLocalRef(mc);

	std::string mode = Config::getOverlayMode();
	if (mode != "gui") return;

    // MOVED TO CLICKGUI
	/*SHORT insertState = GetAsyncKeyState(VK_INSERT);
	bool isInsertDown = (insertState & 0x8000) != 0;
	if (!s_lastInsertState && isInsertDown) {
		s_enabled = !s_enabled;
	}
	s_lastInsertState = isInsertDown;*/

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

	bool showTags = Config::isTagsEnabled();
	float panelWidth = showTags ? 800.0f : 640.0f;
	float rowHeight = 28.0f;
	float headerHeight = 32.0f;
	float panelHeight = headerHeight + (statsData.size() * rowHeight) + 10.0f; 
	
	float colPlayer = 12.0f;
	float colTags   = showTags ? 150.0f : -1000.0f;
	float colStar   = showTags ? 320.0f : 165.0f; 
	float colFK     = showTags ? 390.0f : 235.0f;
	float colFKDR   = showTags ? 480.0f : 325.0f;
	float colWins   = showTags ? 570.0f : 415.0f;
	float colWLR    = showTags ? 650.0f : 495.0f;
	float colWS     = showTags ? 730.0f : 575.0f;

	if (!s_isDragging && s_panelX < 0) { 
		s_panelX = (screenWidth - panelWidth) / 2.0f;
		s_panelY = screenHeight / 5.0f; 
	}
	
	float panelX = s_panelX;
	float panelY = s_panelY;
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
		glColor4f(0.0f, 0.0f, 0.0f, 0.6f);
		glBegin(GL_QUADS);
		glVertex2f(localX, localY);
		glVertex2f(localX, localY + panelHeight);
		glVertex2f(localX + panelWidth, localY + panelHeight);
		glVertex2f(localX + panelWidth, localY);
		glEnd();

		glLineWidth(1.0f);
		glColor4f(0.5f, 0.5f, 0.5f, 0.4f);
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

		float headerTextY = localY + 9.0f; 
		g_font.drawString(localX + colPlayer, headerTextY, "PLAYER", colorFromRGB(255, 255, 255));
		if (showTags) g_font.drawString(localX + colTags,   headerTextY, "TAGS", colorFromRGB(255, 255, 255));
		g_font.drawString(localX + colStar,   headerTextY, "STAR", colorFromRGB(255, 255, 255));
		g_font.drawString(localX + colFK,     headerTextY, "F. KILLS", colorFromRGB(255, 255, 255));
		g_font.drawString(localX + colFKDR,   headerTextY, "FKDR", colorFromRGB(255, 255, 255));
		g_font.drawString(localX + colWins,   headerTextY, "WINS", colorFromRGB(255, 255, 255));
		g_font.drawString(localX + colWLR,    headerTextY, "WLR", colorFromRGB(255, 255, 255));
		g_font.drawString(localX + colWS,     headerTextY, "WS", colorFromRGB(255, 255, 255));

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
			
			if (showTags) {
				float currentTagX = localX + colTags;
                auto drawTag = [&](const std::string& rawType, uint32_t baseColor) {
                    std::string type = rawType;
                    std::replace(type.begin(), type.end(), '_', ' ');
                    for (auto & c: type) c = toupper(c);
                    g_font.drawString(currentTagX + 1.0f, currentY, type, baseColor & 0x40FFFFFF); 
                    g_font.drawString(currentTagX, currentY, type, baseColor);
                    currentTagX += g_font.getStringWidth(type) + 12.0f;
                };

				auto urchinTags = Urchin::getPlayerTags(name);
				if (urchinTags && !urchinTags->tags.empty()) {
					for (const auto& tag : urchinTags->tags) {
						uint32_t tagColor = colorFromRGB(255, 255, 255);
						if (tag.type == "legit_sniper") tagColor = colorFromRGB(255, 165, 0);
						else if (tag.type == "blatant_cheater") tagColor = colorFromRGB(220, 20, 60);
						else if (tag.type == "confirmed_cheater") tagColor = colorFromRGB(148, 0, 211);
                        drawTag(tag.type, tagColor);
					}
				}
                
                auto seraphTags = Seraph::getPlayerTags(name, stats.uuid);
                if (seraphTags && !seraphTags->tags.empty()) {
                    for(const auto& tag : seraphTags->tags) {
                        drawTag(tag.type, colorFromRGB(255, 85, 85));
                    }
                }

				if ((!urchinTags || urchinTags->tags.empty()) && (!seraphTags || seraphTags->tags.empty())) {
					g_font.drawString(currentTagX, currentY, "-", colorFromRGB(100, 100, 105));
				}
			}

			if (stats.isNicked) {
				uint32_t nickedColor = colorFromRGB(170, 0, 0);
				g_font.drawString(localX + colStar, currentY, "[NICKED]", nickedColor);
			} else {
				g_font.drawString(localX + colStar,   currentY, std::to_string(stats.bedwarsStar), colorForStar(stats.bedwarsStar));
				g_font.drawString(localX + colFK,     currentY, std::to_string(stats.bedwarsFinalKills), colorForFinalKills(stats.bedwarsFinalKills));
				g_font.drawString(localX + colFKDR,   currentY, fkdrSs.str(), colorForFKDR(fkdr));
				g_font.drawString(localX + colWins,   currentY, std::to_string(stats.bedwarsWins), colorForWins(stats.bedwarsWins));
				g_font.drawString(localX + colWLR,    currentY, wlrSs.str(), colorForWLR(wlr));
				g_font.drawString(localX + colWS,     currentY, std::to_string(stats.winstreak), colorForWinstreak(stats.winstreak));
			}

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
