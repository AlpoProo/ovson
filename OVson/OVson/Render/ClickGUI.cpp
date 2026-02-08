#include "ClickGUI.h"
#include "../Config/Config.h"
#include "FontRenderer.h"
#include "NotificationManager.h"
#include "StatsOverlay.h"
#include "../Java.h"
#include "../Utils/Timer.h"
#include "../Utils/SensitivityFix.h"
#include "../Logic/BedDefense/BedDefenseManager.h"
#include <gl/GL.h>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <thread>
#include <atomic>
#include <sstream>
#include <functional>
#include "../Services/Hypixel.h"
#include "../Services/UrchinService.h"
#include "../Services/SeraphService.h"
#include "../Chat/ChatInterceptor.h"
#include "RenderUtils.h"

namespace Render {

    static bool s_open = false;
    static bool s_init = false;
    static FontRenderer g_guiFont;
    static float s_animAlpha = 0.0f; 
    static float s_targetAlpha = 0.0f;
    static float s_openingScale = 0.95f; 
    static int s_activeTab = 0; 
    static int s_targetTab = 0;
    static float s_tabIndicatorY = 80.0f;
    static float s_contentSlide = 0.0f; 
    static float s_contentAlpha = 1.0f;
    static bool s_lastLButton = false;
    static bool s_lastInsert = false;
    static bool s_lastBackspace = false;
    static std::string s_playerSearch = "";
    static std::string s_apiKeyInput = "";
    static std::string s_autoGGInput = "";
    static std::string s_urchinKeyInput = "";
    static std::string s_seraphKeyInput = "";
    static bool s_typingSearch = false;
    static bool s_typingApiKey = false;
    static bool s_typingAutoGG = false;
    static bool s_typingUrchinKey = false;
    static bool s_typingSeraphKey = false;
    static float s_scrollOffset = 0.0f;
    static float s_targetScroll = 0.0f;
    static bool s_isDropdownOpen = false;
    static float s_dropdownAnim = 0.0f;
    static bool s_isTagsDropdownOpen = false;
    static float s_tagsDropdownAnim = 0.0f;
    static bool s_isSortOrderDropdownOpen = false;
    static float s_sortOrderDropdownAnim = 0.0f;
    static Hypixel::PlayerStats s_lookupResult;
    static bool s_hasLookup = false;
    static bool s_searching = false;
    static std::string s_lookupName = "";
    static float g_x = 100.0f;
    static float g_y = 100.0f;
    static float g_w = 700.0f;
    static float g_h = 420.0f;
    static bool s_dragging = false;
    static float s_dragOffsetX = 0.0f;
    static float s_dragOffsetY = 0.0f;
    static bool s_waitingForKey = false;

    std::string ClickGUI::getKeyName(int vk) {
        if (vk == VK_INSERT) return "INSERT";
        if (vk == VK_DELETE) return "DELETE";
        if (vk == VK_HOME) return "HOME";
        if (vk == VK_END) return "END";
        if (vk == VK_PRIOR) return "PAGE UP";
        if (vk == VK_NEXT) return "PAGE DOWN";
        if (vk == VK_RSHIFT) return "RSHIFT";
        if (vk == VK_LSHIFT) return "LSHIFT";
        
        UINT scanCode = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
        char buf[32] = {0};
        if (GetKeyNameTextA(scanCode << 16, buf, 32)) return std::string(buf);
        return "Key " + std::to_string(vk);
    }

    #define THEME_NAVY Config::getThemeColor()
    const DWORD THEME_BG = 0xFF0D0D0F;
    const DWORD THEME_SIDEBAR = 0xFF121214;
    const DWORD THEME_CARD = 0xFF18181B;
    const DWORD THEME_ACCENT = THEME_NAVY;
    const DWORD THEME_BORDER = 0xFF252528;

    static float lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    static uint32_t applyAlpha(uint32_t color, float alpha) {
        uint8_t a = (uint8_t)(((color >> 24) & 0xFF) * alpha);
        return (uint32_t)((a << 24) | (color & 0x00FFFFFF));
    }

    // moved to RenderUtils.h

    static void setMouseGrabbed(bool grabbed) {
        JNIEnv* env = lc->getEnv();
        if (!env) return;
        jclass mouseCls = lc->GetClass("org.lwjgl.input.Mouse");
        if (!mouseCls) return;
        jmethodID m_setGrabbed = env->GetStaticMethodID(mouseCls, "setGrabbed", "(Z)V");
        if (m_setGrabbed) {
            env->CallStaticVoidMethod(mouseCls, m_setGrabbed, grabbed);
        }
    }

    static bool isHovered(float mx, float my, float x, float y, float w, float h) {
        return mx >= x && mx <= x + w && my >= y && my <= y + h;
    }

    static bool isIngame() {
        JNIEnv* env = lc->getEnv();
        if (!env) return false;
        jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
        if (!mcCls) return false;
        jmethodID m_getMc = env->GetStaticMethodID(mcCls, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
        if (!m_getMc) return false;
        jobject mcObj = env->CallStaticObjectMethod(mcCls, m_getMc);
        if (!mcObj) return false;
        jfieldID f_screen = env->GetFieldID(mcCls, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;");
        if (!f_screen) return false;
        jobject screen = env->GetObjectField(mcObj, f_screen);
        bool ingame = (screen == nullptr);
        if (screen) env->DeleteLocalRef(screen);
        env->DeleteLocalRef(mcObj);
        return ingame;
    }

    void ClickGUI::init() {
        s_init = true;
        TimeUtil::init();
    }

    void ClickGUI::shutdown() {
    }

    bool ClickGUI::isOpen() {
        return s_open;
    }

    void ClickGUI::toggle() {
        s_open = !s_open;
        s_targetAlpha = s_open ? 1.0f : 0.0f;
        
        if (s_open) {
             s_openingScale = 0.95f;
             s_apiKeyInput = Config::getApiKey();
             s_autoGGInput = Config::getAutoGGMessage();
             s_urchinKeyInput = Config::getUrchinApiKey();
             s_seraphKeyInput = Config::getSeraphApiKey();
             ShowCursor(TRUE);
             setMouseGrabbed(false);
             FocusFix::setIngameFocus(false); 
        } else {
             if (isIngame()) {
                 ShowCursor(FALSE);
                 setMouseGrabbed(true);
                 FocusFix::setIngameFocus(true); 
             }
             s_dragging = false;
        }
    }

    void ClickGUI::updateInput(HWND hwnd) {
        int key = Config::getClickGuiKey();
        bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        if (down && !s_lastInsert) {
             if (Config::isClickGuiOn()) {
                  toggle();
             } else {
                  if (Config::getOverlayMode() == "gui") {
                      bool current = StatsOverlay::isEnabled();
                      StatsOverlay::setEnabled(!current);
                      NotificationManager::getInstance()->add("Overlay", !current ? "Stats Overlay Enabled" : "Stats Overlay Disabled", NotificationType::Info);
                  } else {
                      NotificationManager::getInstance()->add("Overlay", "Unlock 'GUI' mode to toggle overlay", NotificationType::Warning);
                  }
             }
        }
        s_lastInsert = down;
    }

    struct SwitchAnim {
         float currX = 0.0f;
         float targetX = 0.0f;
    };
    static SwitchAnim s_switches[50]; 

    static void drawSwitch(int id, float x, float y, bool enabled, bool hovered, float alpha) {
        float w = 34.0f;
        float h = 18.0f;
        
        static std::unordered_map<int, float> anims;
        if (anims.find(id) == anims.end()) anims[id] = enabled ? 1.0f : 0.0f;
        anims[id] += ((enabled ? 1.0f : 0.0f) - anims[id]) * 0.2f;
        float t = anims[id];

        // background pill (bp)
        DWORD bgBase = RenderUtils::lerpColor(0xFF2D2D31, THEME_NAVY, t);
        if (hovered) bgBase = RenderUtils::lerpColor(bgBase, 0xFFFFFFFF, 0.15f);
        
        RenderUtils::drawRoundedRect(x, y, w, h, h / 2.0f, bgBase, alpha);
        
        float knobX = x + 2.0f + t * (w - h);
        RenderUtils::drawCircle(knobX + (h-4.0f)/2.0f + 1.0f, y + h/2.0f, (h-4.0f)/2.0f, 0xFFFFFFFF, alpha);
    }

    void ClickGUI::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
        if (!s_open) return;
        
        if (msg == WM_MOUSEWHEEL) {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            s_targetScroll -= (float)delta * 0.5f;
            return;
        }

        if (msg == WM_KEYDOWN) {
             if (wParam == VK_ESCAPE) {
                 toggle();
                 return;
             }
             if ((wParam == 'V') && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                  if (s_typingSearch || s_typingApiKey || s_typingAutoGG || s_typingUrchinKey || s_typingSeraphKey) {
                      if (OpenClipboard(NULL)) {
                          HANDLE hData = GetClipboardData(CF_TEXT);
                          if (hData) {
                              char* pszText = static_cast<char*>(GlobalLock(hData));
                              if (pszText) {
                                  std::string text(pszText);
                                  std::string filtered;
                                  for(char c : text) if(c >= 32 && c <= 126) filtered += c;
                                  
                                  std::string* target = s_typingSearch ? &s_playerSearch : (s_typingApiKey ? &s_apiKeyInput : (s_typingAutoGG ? &s_autoGGInput : (s_typingUrchinKey ? &s_urchinKeyInput : &s_seraphKeyInput)));
                                  int cap = (s_typingAutoGG || s_typingUrchinKey || s_typingSeraphKey) ? 100 : 48;
                                  if (target->length() + filtered.length() < cap) {
                                      *target += filtered;
                                      NotificationManager::getInstance()->add("Input", "Pasted from clipboard", NotificationType::Info);
                                  } else {
                                      NotificationManager::getInstance()->add("Input", "Text too long!", NotificationType::Warning);
                                  }
                                  GlobalUnlock(hData);
                              }
                          }
                          CloseClipboard();
                      }
                  }
                  return; 
             }
        }

        if (msg == WM_CHAR) {
            char c = (char)wParam;
            if (s_typingSearch || s_typingApiKey || s_typingAutoGG || s_typingUrchinKey || s_typingSeraphKey) {
                std::string* target = s_typingSearch ? &s_playerSearch : (s_typingApiKey ? &s_apiKeyInput : (s_typingAutoGG ? &s_autoGGInput : (s_typingUrchinKey ? &s_urchinKeyInput : &s_seraphKeyInput)));
                int cap = (s_typingAutoGG || s_typingUrchinKey || s_typingSeraphKey) ? 100 : 48;
                if (c == 8) {
                    if (!target->empty()) target->pop_back();
                } else if (c == 13) {
                    if (s_typingSearch && !s_playerSearch.empty()) {
                        std::string key = s_apiKeyInput;
                        if (key.empty() || key == "None") key = Config::getApiKey();

                        if (key.empty() || key == "None") {
                            NotificationManager::getInstance()->add("Hypixel", "Please set an API Key first!", NotificationType::Error);
                        } else {
                            s_searching = true;
                            s_hasLookup = false;
                            std::string searchName = s_playerSearch;
                            NotificationManager::getInstance()->add("Hypixel", "Fetching player ID...", NotificationType::Info);
                            
                            std::thread([searchName, key]() {
                                auto uuidOpt = Hypixel::getUuidByName(searchName);
                                if (uuidOpt) {
                                    NotificationManager::getInstance()->add("Hypixel", "ID found, fetching stats...", NotificationType::Info);
                                    auto statsOpt = Hypixel::getPlayerStats(key, *uuidOpt);
                                    if (statsOpt) {
                                        s_lookupResult = *statsOpt;
                                        s_lookupName = searchName;
                                        s_hasLookup = true;
                                    } else {
                                        NotificationManager::getInstance()->add("Hypixel", "Check API-Key or Connectivity", NotificationType::Error);
                                    }
                                } else {
                                    NotificationManager::getInstance()->add("Hypixel", "Player not found", NotificationType::Warning);
                                }
                                s_searching = false;
                            }).detach();
                        }
                        s_typingSearch = false;
                    }
                    if (s_typingApiKey) {
                        Config::setApiKey(s_apiKeyInput);
                        NotificationManager::getInstance()->add("Settings", "API Key Saved", NotificationType::Success);
                        s_typingApiKey = false;
                    }
                    if (s_typingAutoGG) {
                        Config::setAutoGGMessage(s_autoGGInput);
                        NotificationManager::getInstance()->add("AutoGG", "Custom message saved", NotificationType::Success);
                        s_typingAutoGG = false;
                    }
                    if (s_typingUrchinKey) {
                        Config::setUrchinApiKey(s_urchinKeyInput);
                        NotificationManager::getInstance()->add("Urchin", "API Key Saved", NotificationType::Success);
                        s_typingUrchinKey = false;
                    }
                    if (s_typingSeraphKey) {
                        Config::setSeraphApiKey(s_seraphKeyInput);
                        NotificationManager::getInstance()->add("Seraph", "API Key Saved", NotificationType::Success);
                        s_typingSeraphKey = false;
                    }
                } else if (c >= 32 && c <= 126) {
                    if (target->length() < cap) target->push_back(c);
                    else {
                        static ULONGLONG lastWarn = 0;
                        if (GetTickCount64() - lastWarn > 2000) {
                            NotificationManager::getInstance()->add("Input", "Text too long!", NotificationType::Warning);
                            lastWarn = GetTickCount64();
                        }
                    }
                }
            }
        }
    }

    void ClickGUI::render(HDC hdc) {
        HWND hwnd = WindowFromDC(hdc);
        updateInput(hwnd);

        if (s_open) {
            static int focusTick = 0;
            if (focusTick++ % 20 == 0) {
                FocusFix::setIngameFocus(false);
                setMouseGrabbed(false);
            }
        }

        float dt = TimeUtil::getDelta(); 
        
        float alphaDiff = s_targetAlpha - s_animAlpha;
        s_animAlpha += alphaDiff * 0.15f;
        s_openingScale = lerp(0.95f, 1.0f, s_animAlpha);

        if (s_animAlpha <= 0.001f && !s_open) return; 
        
        if (!g_guiFont.isInitialized()) {
            g_guiFont.init(hdc);
        }

        glPushMatrix();
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_ALPHA_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        RECT cr; GetClientRect(hwnd, &cr);
        float sw = (float)cr.right;
        float sh = (float)cr.bottom;

        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glOrtho(0, sw, sh, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();

        POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
        float mx = (float)pt.x; float my = (float)pt.y;
        bool lClick = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        bool clickEvent = lClick && !s_lastLButton;
        s_lastLButton = lClick;

        RenderUtils::drawRect(0, 0, sw, sh, 0xA0000000); 

        glPushMatrix();
        float centerX = g_x + g_w/2;
        float centerY = g_y + g_h/2;
        glTranslatef(centerX, centerY, 0);
        glScalef(s_openingScale, s_openingScale, 1.0f);
        glTranslatef(-centerX, -centerY, 0);

        if (s_open && s_animAlpha >= 0.95f) {
             if (lClick) {
                 if (!s_dragging) {
                     if (isHovered(mx, my, g_x, g_y, g_w, 52)) {
                         s_dragging = true;
                         s_dragOffsetX = mx - g_x;
                         s_dragOffsetY = my - g_y;
                     }
                 } else {
                     g_x = mx - s_dragOffsetX;
                     g_y = my - s_dragOffsetY;
                 }
             } else {
                 s_dragging = false;
             }
        }
        
        if (s_open && s_waitingForKey) {
             for (int k = 1; k < 255; ++k) {
                 if (k == VK_LBUTTON || k == VK_RBUTTON || k == VK_MBUTTON) continue; 
                 if ((GetAsyncKeyState(k) & 0x8000) != 0) {
                     if (k == VK_ESCAPE) {
                         s_waitingForKey = false;
                     } else {
                         Config::setClickGuiKey(k);
                         Config::save();
                         NotificationManager::getInstance()->add("Settings", "Bind set to " + getKeyName(k), NotificationType::Success);
                         s_waitingForKey = false;
                     }
                     break; 
                 }
             }
        }
        
         float mainX = g_x;
         float mainY = g_y;
         
         RenderUtils::drawRoundedRect(mainX - 1, mainY - 1, g_w + 2, g_h + 2, 10.0f, THEME_BORDER, s_animAlpha);
         RenderUtils::drawRoundedRect(mainX, mainY, g_w, g_h, 10.0f, THEME_BG, s_animAlpha);
         
         RenderUtils::drawRoundedRect(mainX, mainY, 170, g_h, 10.0f, THEME_SIDEBAR, s_animAlpha);
         RenderUtils::drawRect(mainX + 160, mainY, 10, g_h, THEME_SIDEBAR, s_animAlpha);
         
         RenderUtils::drawGradientRect(mainX + 170, mainY, g_w - 170, 52, 0xFF121214, 0x00121214);
         RenderUtils::drawRect(mainX + 170, mainY + 52, g_w - 170, 1, 0xFF202022); 
 
         glEnable(GL_TEXTURE_2D);
         g_guiFont.drawString(mainX + 25, mainY + 24.0f, "OVSON", applyAlpha(THEME_NAVY, s_animAlpha)); 
         g_guiFont.drawString(mainX + 75, mainY + 38.0f, "CLIENT", applyAlpha(0xFFA0A0A5, s_animAlpha)); 
 
          float targetY = 85.0f + (s_targetTab * 45.0f);
          s_tabIndicatorY += (targetY - s_tabIndicatorY) * 0.2f;
          
          glDisable(GL_TEXTURE_2D);
          RenderUtils::drawRoundedRect(mainX + 15, mainY + s_tabIndicatorY - 12.0f, 140, 48, 8.0f, THEME_NAVY & 0x40FFFFFF);
          RenderUtils::drawRoundedRect(mainX + 4, mainY + s_tabIndicatorY - 4.0f, 3, 32, 1.5f, THEME_NAVY);
          glEnable(GL_TEXTURE_2D);
 
          if (s_activeTab != s_targetTab) {
              s_contentAlpha -= 0.15f;
              if (s_contentAlpha <= 0.0f) {
                  s_activeTab = s_targetTab;
                  s_contentSlide = 15.0f;
                  s_targetScroll = 0.0f;
                  s_scrollOffset = 0.0f;
              }
          } else {
              s_contentAlpha += 0.15f;
              if (s_contentAlpha > 1.0f) s_contentAlpha = 1.0f;
              s_contentSlide += (0.0f - s_contentSlide) * 0.15f;
          }
 
          static float s_maxScroll = 0.0f;
          if (s_targetScroll < 0) s_targetScroll = 0;
          if (s_targetScroll > s_maxScroll) s_targetScroll = s_maxScroll;
 
          s_scrollOffset += (s_targetScroll - s_scrollOffset) * 0.15f;
 
          const char* tabs[] = { "Visuals", "Players", "Tags", "Settings", "Debug", "Utils", nullptr };
          float ty = mainY + 85;
          for (int i = 0; tabs[i]; ++i) {
               bool hover = isHovered(mx, my, mainX + 15, ty - 12, 140, 40);
               DWORD col = (s_targetTab == i) ? 0xFFFFFFFF : (hover ? 0xFFCCCCCC : 0xFF808085);
               g_guiFont.drawString(mainX + 45, ty, tabs[i], applyAlpha(col, s_animAlpha));
               if (clickEvent && hover) {
                   s_targetTab = i;
                    s_isDropdownOpen = false;
               }
               ty += 45;
          }
        
         
         float cx = mainX + 200 + s_contentSlide;
         float startCy = mainY + 85; 
         float cy = startCy - s_scrollOffset;
         float alpha = s_animAlpha * s_contentAlpha;

         glEnable(GL_SCISSOR_TEST);
         glScissor((int)(mainX + 170), (int)(sh - (mainY + g_h - 10)), (int)(g_w - 170), (int)(g_h - 60));

         if (s_activeTab == 5) {
              g_guiFont.drawString(cx, cy, "Utilities", applyAlpha(0xFFFFFFFF, alpha));
              cy += 40;
               g_guiFont.drawString(cx, cy, "Bed Defense", applyAlpha(0xFFFFFFFF, alpha));
                bool hCard = isHovered(mx, my, mainX + 190, cy - 10, g_w - 210, 95);
                glDisable(GL_TEXTURE_2D);
                DWORD bedCol = hCard ? 0xFF222226 : THEME_CARD;
                RenderUtils::drawRoundedRect(mainX + 190, cy - 10, g_w - 210, 95, 8.0f, bedCol, 0.6f * alpha);
                if (hCard) RenderUtils::drawRect(mainX + 190, cy - 10, 3, 95, THEME_NAVY, alpha);
                glEnable(GL_TEXTURE_2D);
                
                g_guiFont.drawString(cx, cy + 18, "X-Ray style outlines for bed defense blocks", applyAlpha(0xFFA0A0A5, alpha));
                g_guiFont.drawString(cx, cy + 42, "WARNING: THIS PROVIDES AN UNFAIR ADVANTAGE.", applyAlpha(0xFFFF5555, alpha), 0.4f);
                g_guiFont.drawString(cx, cy + 54, "YOU WILL BE BLACKLISTED IF CAUGHT. USE AT OWN RISK.", applyAlpha(0xFFFF5555, alpha), 0.4f);

                bool enabled = Config::isBedDefenseEnabled();
                glDisable(GL_TEXTURE_2D);
                float swX = mainX + g_w - 65;
                drawSwitch(0, swX, cy + 15, enabled, hCard, alpha); 
                glEnable(GL_TEXTURE_2D);
               if (clickEvent && hCard) {
                   bool newState = !enabled;
                   Config::setBedDefenseEnabled(newState);
                   if (newState) BedDefense::BedDefenseManager::getInstance()->enable();
                   else BedDefense::BedDefenseManager::getInstance()->disable();
                   
                   NotificationManager::getInstance()->add("Module", newState ? "Bed Defense Activated" : "Bed Defense Disabled", newState ? NotificationType::Success : NotificationType::Warning);
               }
                cy += 115;

                g_guiFont.drawString(cx, cy, "Chat Bypasser", applyAlpha(0xFFFFFFFF, alpha));
                
                 bool hBypass = isHovered(mx, my, mainX + 190, cy + 30, g_w - 210, 60);
                 glDisable(GL_TEXTURE_2D);
                 DWORD bypCol = hBypass ? 0xFF222226 : THEME_CARD;
                 RenderUtils::drawRoundedRect(mainX + 190, cy + 30, g_w - 210, 60, 6.0f, bypCol, 0.6f * alpha);
                 if (hBypass) RenderUtils::drawRect(mainX + 190, cy + 30, 3, 60, THEME_NAVY, alpha);
                 
                 glEnable(GL_TEXTURE_2D);
                 g_guiFont.drawString(cx, cy + 40, "Bypass Chat Filter", applyAlpha(0xFFFFFFFF, alpha));
                 g_guiFont.drawString(cx, cy + 58, "Allows sending messages that would normally be blocked", applyAlpha(0xFFA0A0A5, alpha));
                 
                 bool bypassEnabled = Config::isChatBypasserEnabled();
                 glDisable(GL_TEXTURE_2D);
                 float bypassSwX = mainX + g_w - 65;
                 drawSwitch(14, bypassSwX, cy + 40, bypassEnabled, hBypass, alpha);
                 glEnable(GL_TEXTURE_2D);
                if (clickEvent && hBypass) {
                    Config::setChatBypasserEnabled(!bypassEnabled);
                    NotificationManager::getInstance()->add("Utils", !bypassEnabled ? "Bypasser Enabled" : "Bypasser Disabled", !bypassEnabled ? NotificationType::Success : NotificationType::Warning);
                }
                cy += 110;

                g_guiFont.drawString(cx, cy, "Faster Stats", applyAlpha(0xFFFFFFFF, alpha));
                bool hNicked = isHovered(mx, my, mainX + 190, cy + 30, g_w - 210, 60);
                glDisable(GL_TEXTURE_2D);
                DWORD nickCol = hNicked ? 0xFF222226 : THEME_CARD;
                RenderUtils::drawRoundedRect(mainX + 190, cy + 30, g_w - 210, 60, 6.0f, nickCol, 0.6f * alpha);
                if (hNicked) RenderUtils::drawRect(mainX + 190, cy + 30, 3, 60, THEME_NAVY, alpha);

                glEnable(GL_TEXTURE_2D);
                g_guiFont.drawString(cx, cy + 40, "Direct UUID Fetching", applyAlpha(0xFFFFFFFF, alpha));
                g_guiFont.drawString(cx, cy + 58, "Use direct game UUIDs for instant stats", applyAlpha(0xFFA0A0A5, alpha));

                bool nickedBypass = Config::isNickedBypass();
                glDisable(GL_TEXTURE_2D);
                float nickSwX = mainX + g_w - 65;
                drawSwitch(20, nickSwX, cy + 40, nickedBypass, hNicked, alpha);
                glEnable(GL_TEXTURE_2D);
                if (clickEvent && hNicked) {
                    Config::setNickedBypass(!nickedBypass);
                    NotificationManager::getInstance()->add("Utils", !nickedBypass ? "Nicked Bypass Enabled" : "Nicked Bypass Disabled", !nickedBypass ? NotificationType::Success : NotificationType::Warning);
                }
                cy += 110;
          }
         else if (s_activeTab == 0) {
              g_guiFont.drawString(cx, cy, "Overlays", applyAlpha(0xFFFFFFFF, alpha));
              cy += 40;
               auto drawSettingsCard = [&](const char* title, const char* desc, bool& val, int id, float& cy_ref) {
                  bool hover = isHovered(mx, my, mainX + 190, cy_ref - 10, g_w - 210, 62);
                  RenderUtils::drawRoundedRect(mainX + 190, cy_ref - 10, g_w - 210, 62, 8.0f, THEME_CARD, 0.5f * alpha * s_contentAlpha);
                  if (hover) {
                      RenderUtils::drawRoundedRect(mainX + 190, cy_ref - 10, g_w - 210, 62, 8.0f, THEME_NAVY & 0x15FFFFFF, alpha * s_contentAlpha);
                      RenderUtils::drawRoundedRect(mainX + 190, cy_ref - 10, 2, 62, 2.0f, THEME_NAVY, alpha * s_contentAlpha);
                  }

                  g_guiFont.drawString(cx, cy_ref, title, applyAlpha(0xFFFFFFFF, alpha * s_contentAlpha));
                  g_guiFont.drawString(cx, cy_ref + 18, desc, applyAlpha(0xFFA0A0A5, alpha * s_contentAlpha), 0.45f);
                  
                  float swX = mainX + g_w - 65;
                  drawSwitch(id, swX, cy_ref + 5, val, hover, alpha);
                  
                  if (clickEvent && hover) {
                      val = !val;
                  }
                  cy_ref += 72;
              };

              bool ovEnabled = StatsOverlay::isEnabled();
              bool oldOv = ovEnabled;
              drawSettingsCard("Stats Overlay", "Display player skill metrics in a clean table", ovEnabled, 1, cy);
              if (ovEnabled != oldOv) StatsOverlay::setEnabled(ovEnabled);

              bool notifEnabled = Config::isNotificationsEnabled();
              drawSettingsCard("Refined Notifications", "Enable silky smooth toast alerts", notifEnabled, 2, cy);
              Config::setNotificationsEnabled(notifEnabled);

              bool blurEnabled = Config::isMotionBlurEnabled();
              drawSettingsCard("Motion Blur", "Adds a cinematic trail to camera movement", blurEnabled, 4, cy);
              Config::setMotionBlurEnabled(blurEnabled);
              cy += 10;

              if (blurEnabled) {
                  g_guiFont.drawString(cx, cy, "Blur Intensity", applyAlpha(0xFFA0A0A5, alpha));
                  cy += 20;
                  float sliderW = 200.0f;
                  float sliderH = 10.0f;
                  float sliderVal = Config::getMotionBlurAmount();
                  
                  glDisable(GL_TEXTURE_2D);
                  RenderUtils::drawRect(cx, cy, sliderW, sliderH, 0xFF2A2A2E, alpha);
                  RenderUtils::drawRect(cx, cy, sliderW * sliderVal, sliderH, Config::getThemeColor(), alpha);
                  glEnable(GL_TEXTURE_2D);
                  
                  bool hSlider = isHovered(mx, my, cx, cy - 5, sliderW, sliderH + 10);
                  if (hSlider && lClick) {
                      float newVal = (mx - cx) / sliderW;
                      if (newVal < 0) newVal = 0;
                      if (newVal > 1) newVal = 1;
                      Config::setMotionBlurAmount(newVal);
               }
               cy += 30;
           }
           
           cy += 20;
           g_guiFont.drawString(cx, cy, "Table Customization", applyAlpha(0xFFFFFFFF, alpha));
           cy += 35;

           g_guiFont.drawString(cx, cy, "Sort By:", applyAlpha(0xFFA0A0A5, alpha));
           const char* sModes[] = { "Team", "Star", "FK", "FKDR", "Wins", "WLR", "WS" };
           std::string curSort = Config::getSortMode();
           
            float bx = cx + 80;
            for(int i=0; i<7; ++i) {
                bool hov = isHovered(mx, my, bx, cy - 5, 50, 25);
                bool sel = (curSort == sModes[i]);
                glDisable(GL_TEXTURE_2D);
                RenderUtils::drawRoundedRect(bx, cy - 5, 50, 25, 4.0f, sel ? THEME_NAVY : (hov ? 0xFF35353A : THEME_CARD), alpha);
                glEnable(GL_TEXTURE_2D);
                g_guiFont.drawString(bx + 5, cy + 2, sModes[i], applyAlpha(sel ? 0xFFFFFFFF : 0xFF808085, alpha), 0.45f);
                if (clickEvent && hov) {
                    Config::setSortMode(sModes[i]);
                    NotificationManager::getInstance()->add("Sort", "Sorting by " + std::string(sModes[i]), NotificationType::Info);
                }
                bx += 58;
            }

            cy += 40;
            {
                bool isDesc = Config::isTabSortDescending();
                std::string currentOrder = isDesc ? "Descending" : "Ascending";
                const char* orders[] = { "Ascending", "Descending" };

                g_guiFont.drawString(cx, cy + 8, "Sort Order:", applyAlpha(0xFFA0A0A5, alpha));
                
                float dropX = cx + 100;
                float dropW = 160.0f;
                float dropH = 32.0f;
                bool hovDrop = isHovered(mx, my, dropX, cy, dropW, dropH);
                
                s_sortOrderDropdownAnim += (s_isSortOrderDropdownOpen ? 1.0f - s_sortOrderDropdownAnim : 0.0f - s_sortOrderDropdownAnim) * 0.15f;

                glDisable(GL_TEXTURE_2D);
                RenderUtils::drawRoundedRect(dropX, cy, dropW, dropH, 6.0f, THEME_CARD, 0.8f * alpha);
                if (hovDrop) RenderUtils::drawRoundedRect(dropX, cy + dropH - 3, dropW, 3.0f, 1.5f, THEME_NAVY, alpha);
                glEnable(GL_TEXTURE_2D);
                
                g_guiFont.drawString(dropX + 10, cy + 10, currentOrder, applyAlpha(0xFFFFFFFF, alpha), 0.45f);
                g_guiFont.drawString(dropX + dropW - 18, cy + 10, s_isSortOrderDropdownOpen ? "-" : "+", applyAlpha(0xFFA0A0A5, alpha), 0.45f);

                if (clickEvent && hovDrop) {
                    s_isSortOrderDropdownOpen = !s_isSortOrderDropdownOpen;
                }

                if (s_sortOrderDropdownAnim > 0.01f) {
                    float listY = cy + dropH + 2;
                    for (int i = 0; i < 2; ++i) {
                        float itemY = listY + (i * dropH);
                        bool hItem = isHovered(mx, my, dropX, itemY, dropW, dropH);
                        
                        glDisable(GL_TEXTURE_2D);
                        RenderUtils::drawRoundedRect(dropX, itemY, dropW, dropH, 4.0f, THEME_CARD, 0.95f * alpha * s_sortOrderDropdownAnim);
                        if (hItem) RenderUtils::drawRect(dropX, itemY, 3, dropH, THEME_NAVY, alpha * s_sortOrderDropdownAnim);
                        glEnable(GL_TEXTURE_2D);
                        
                        g_guiFont.drawString(dropX + 15, itemY + 10, orders[i], applyAlpha(currentOrder == orders[i] ? 0xFFFFFFFF : 0xFFA0A0A5, alpha * s_sortOrderDropdownAnim), 0.45f);
                        
                        if (clickEvent && hItem && (s_sortOrderDropdownAnim > 0.8f)) {
                            Config::setTabSortDescending(i == 1);
                            s_isSortOrderDropdownOpen = false;
                            NotificationManager::getInstance()->add("Sort", std::string("Order set to: ") + orders[i], NotificationType::Info);
                        }
                    }
                    cy += (2 * dropH) * s_sortOrderDropdownAnim;
                }
            }
           cy += 50;
           g_guiFont.drawString(cx, cy, "Visible Columns:", applyAlpha(0xFFA0A0A5, alpha));
           cy += 30;

           struct ColToggle { std::string name; bool enabled; std::function<void(bool)> setter; };
           ColToggle toggles[] = {
               { "Star", Config::isShowStar(), [](bool b){ Config::setShowStar(b); } },
               { "FK",   Config::isShowFk(),   [](bool b){ Config::setShowFk(b); } },
               { "FKDR", Config::isShowFkdr(), [](bool b){ Config::setShowFkdr(b); } },
               { "Wins", Config::isShowWins(), [](bool b){ Config::setShowWins(b); } },
               { "WLR",  Config::isShowWlr(),  [](bool b){ Config::setShowWlr(b); } },
               { "WS",   Config::isShowWs(),   [](bool b){ Config::setShowWs(b); } }
           };

            float tx = cx;
            for(int i=0; i<6; ++i) {
                float cardW = 125.0f;
                float cardH = 36.0f;
                bool hov = isHovered(mx, my, tx, cy, cardW, cardH);
                
                glDisable(GL_TEXTURE_2D);
                DWORD baseCol = hov ? 0xFF222226 : THEME_CARD;
                RenderUtils::drawRoundedRect(tx, cy, cardW, cardH, 5.0f, baseCol, 0.7f * alpha);
                if (hov) RenderUtils::drawRect(tx, cy, 3, cardH, THEME_NAVY, alpha);
                glEnable(GL_TEXTURE_2D);
                
                g_guiFont.drawString(tx + 12, cy + cardH/2.0f - 6.0f, toggles[i].name, applyAlpha(toggles[i].enabled ? 0xFFFFFFFF : 0xFF808085, alpha), 0.45f);
                
                glDisable(GL_TEXTURE_2D);
                drawSwitch(100 + i, tx + cardW - 45, cy + (cardH - 18.0f)/2.0f, toggles[i].enabled, hov, alpha);
                glEnable(GL_TEXTURE_2D);

                if (clickEvent && hov) {
                    toggles[i].setter(!toggles[i].enabled);
                }
                
                if (i % 3 == 2) { 
                    tx = cx; 
                    cy += cardH + 10; 
                } else {
                    tx += cardW + 12;
                }
            }
           cy += 60;
      }
         else if (s_activeTab == 1) {
              g_guiFont.drawString(cx, cy, "Player Search", applyAlpha(0xFFFFFFFF, alpha));
              cy += 40;
              
               RenderUtils::drawRoundedRect(cx, cy, 360, 40, 6.0f, THEME_CARD, 0.6f * alpha);
               if (s_typingSearch) RenderUtils::drawRect(cx, cy, 2, 40, THEME_NAVY, alpha);
              
              bool hSearch = isHovered(mx, my, cx, cy, 360, 40);
              
              std::string dispSearch = s_playerSearch;
              if (s_typingSearch && (GetTickCount64() / 500) % 2 == 0) dispSearch += "|";
              if (dispSearch.empty() && !s_typingSearch) dispSearch = "Enter player name...";
              
              g_guiFont.drawString(cx + 10, cy + 12, dispSearch, applyAlpha(s_typingSearch ? 0xFFFFFFFF : 0xFF808085, alpha));
              
              if (clickEvent && isHovered(mx, my, cx, cy, 360, 40)) {
                  s_typingSearch = true;
                  s_typingApiKey = s_typingAutoGG = s_typingUrchinKey = false;
                  NotificationManager::getInstance()->add("Input", "Search focused", NotificationType::Info);
              } else if (clickEvent && !hSearch) s_typingSearch = false;

              if (s_searching) {
                  cy += 60;
                  g_guiFont.drawString(cx, cy, "Fetching data from Hypixel API...", applyAlpha(0xFFA0A0A5, alpha));
              }
              else if (s_hasLookup) {
                  cy += 60;
                  g_guiFont.drawString(cx, cy, "Result for: " + s_lookupName, applyAlpha(THEME_NAVY, alpha));
                  cy += 25;
                  g_guiFont.drawString(cx, cy, "Star: " + std::to_string(s_lookupResult.bedwarsStar), applyAlpha(0xFFFFFFFF, alpha));
                  g_guiFont.drawString(cx + 100, cy, "Wins: " + std::to_string(s_lookupResult.bedwarsWins), applyAlpha(0xFFFFFFFF, alpha));
                  g_guiFont.drawString(cx, cy + 20, "FKDR: " + std::to_string((s_lookupResult.bedwarsFinalDeaths == 0) ? s_lookupResult.bedwarsFinalKills : (double)s_lookupResult.bedwarsFinalKills / s_lookupResult.bedwarsFinalDeaths), applyAlpha(0xFFFFFFFF, alpha));
                  cy += 40;
                  
                  auto drawWrapped = [&](const std::string& text, uint32_t color, float& currY) {
                      std::string line; std::string word; std::stringstream ss(text);
                      while (ss >> word) {
                          if (g_guiFont.getStringWidth(line + word) > 350) {
                              g_guiFont.drawString(cx + 10, currY, line, applyAlpha(color, alpha));
                              currY += 18.0f; line = "";
                          }
                          line += (line.empty() ? "" : " ") + word;
                      }
                      if (!line.empty()) { g_guiFont.drawString(cx + 10, currY, line, applyAlpha(color, alpha)); currY += 18.0f; }
                  };

                  if (Config::isTagsEnabled()) {
                      std::string activeS = Config::getActiveTagService();
                      auto utags = Urchin::getPlayerTags(s_lookupName);
                      auto stags = Seraph::getPlayerTags(s_lookupName, s_lookupResult.uuid);

                      bool hasUrchin = utags && !utags->tags.empty() && (activeS == "Urchin" || activeS == "Both");
                      bool hasSeraph = stags && !stags->tags.empty() && (activeS == "Seraph" || activeS == "Both");

                      if (hasUrchin) {
                          cy += 40;
                          g_guiFont.drawString(cx, cy, "Urchin Tags:", applyAlpha(0xFFA0A0A5, alpha));
                          cy += 20;
                          for (const auto& tag : utags->tags) {
                              std::string tagText = "[" + tag.type + "]";
                              if (!tag.reason.empty()) tagText += " - " + tag.reason;
                              drawWrapped(tagText, 0xFFCCCCCC, cy);
                          }
                      }
                      if (hasSeraph) {
                          cy += (hasUrchin ? 40 : 40);
                          g_guiFont.drawString(cx, cy, "Seraph Blacklist:", applyAlpha(0xFFFF5555, alpha));
                          cy += 20;
                          for (const auto& tag : stags->tags) {
                              std::string tagText = "[" + tag.type + "]";
                              if (!tag.reason.empty()) tagText += " - " + tag.reason;
                              drawWrapped(tagText, 0xFFCCCCCC, cy);
                           }
                      }
                  }
              }
              cy += 100;
         }
         else if (s_activeTab == 2) {
              g_guiFont.drawString(cx, cy, "Tagging Services", applyAlpha(0xFFFFFFFF, alpha));
              cy += 40;
              
              bool tagsEnabled = Config::isTagsEnabled();
               bool hMasterCard = isHovered(mx, my, mainX + 190, cy - 10, g_w - 210, 60);
               glDisable(GL_TEXTURE_2D);
               DWORD mastCol = hMasterCard ? 0xFF222226 : THEME_CARD;
               RenderUtils::drawRoundedRect(mainX + 190, cy - 10, g_w - 210, 60, 6.0f, mastCol, 0.6f * alpha);
               if (hMasterCard) RenderUtils::drawRect(mainX + 190, cy - 10, 3, 60, THEME_NAVY, alpha);
               glEnable(GL_TEXTURE_2D);
               g_guiFont.drawString(cx, cy, "Enable Tags", applyAlpha(0xFFFFFFFF, alpha));
               g_guiFont.drawString(cx, cy + 18, "Master switch for all tagging services", applyAlpha(0xFFA0A0A5, alpha));
               glDisable(GL_TEXTURE_2D);
               drawSwitch(10, mainX + g_w - 65, cy, tagsEnabled, hMasterCard, alpha);
               glEnable(GL_TEXTURE_2D);
              
              if (clickEvent && hMasterCard) {
                  Config::setTagsEnabled(!tagsEnabled);
                  NotificationManager::getInstance()->add("Tags", tagsEnabled ? "Tags Disabled" : "Tags Enabled", !tagsEnabled ? NotificationType::Success : NotificationType::Warning);
              }
              cy += 80;

              if (tagsEnabled) {
                  g_guiFont.drawString(cx, cy, "Active Service", applyAlpha(0xFFFFFFFF, alpha));
                  cy += 30;

                  std::string currentService = Config::getActiveTagService();
                  const char* services[] = { "Urchin", "Seraph", "Both" };

                  float dropW = 220.0f;
                  float dropH = 35.0f;
                  bool hovDrop = isHovered(mx, my, cx, cy, dropW, dropH);
                  
                  s_tagsDropdownAnim += (s_isTagsDropdownOpen ? 1.0f - s_tagsDropdownAnim : 0.0f - s_tagsDropdownAnim) * 0.15f;

                  glDisable(GL_TEXTURE_2D);
                  RenderUtils::drawRoundedRect(cx, cy, dropW, dropH, 6.0f, THEME_CARD, 0.8f * alpha);
                  if (hovDrop) RenderUtils::drawRoundedRect(cx, cy + dropH - 3, dropW, 3.0f, 1.5f, THEME_NAVY, alpha);
                  glEnable(GL_TEXTURE_2D);
                  
                  g_guiFont.drawString(cx + 10, cy + 10, currentService, applyAlpha(0xFFFFFFFF, alpha));
                  g_guiFont.drawString(cx + dropW - 20, cy + 10, s_isTagsDropdownOpen ? "-" : "+", applyAlpha(0xFFA0A0A5, alpha));

                  if (clickEvent && hovDrop) {
                      s_isTagsDropdownOpen = !s_isTagsDropdownOpen;
                  }

                  if (s_tagsDropdownAnim > 0.01f) {
                      float listY = cy + dropH + 2;
                      for (int i = 0; i < 3; ++i) {
                          float itemY = listY + (i * dropH);
                          bool hItem = isHovered(mx, my, cx, itemY, dropW, dropH);
                          
                          glDisable(GL_TEXTURE_2D);
                           RenderUtils::drawRoundedRect(cx, itemY, dropW, dropH, 4.0f, THEME_CARD, 0.95f * alpha * s_tagsDropdownAnim);
                           if (hItem) RenderUtils::drawRect(cx, itemY, 3, dropH, THEME_NAVY, alpha * s_tagsDropdownAnim);
                          glEnable(GL_TEXTURE_2D);
                          
                          g_guiFont.drawString(cx + 15, itemY + 10, services[i], applyAlpha(currentService == services[i] ? 0xFFFFFFFF : 0xFFA0A0A5, alpha * s_tagsDropdownAnim));
                          
                          if (clickEvent && hItem && (s_tagsDropdownAnim > 0.8f)) {
                              Config::setActiveTagService(services[i]);
                              s_isTagsDropdownOpen = false;
                              NotificationManager::getInstance()->add("Tags", "Active service set to: " + std::string(services[i]), NotificationType::Info);
                          }
                      }
                      cy += (3 * dropH) * s_tagsDropdownAnim;
                  }
                  cy += 50;

                  g_guiFont.drawString(cx, cy, "Urchin API Key", applyAlpha(0xFFA0A0A5, alpha));
                  cy += 20;
                  RenderUtils::drawRect(cx, cy, 350, 35, THEME_CARD, 0.6f * alpha);
                  if (s_typingUrchinKey) RenderUtils::drawRect(cx, cy, 2, 35, THEME_NAVY, alpha);
                  std::string dispUrchinKey = s_typingUrchinKey ? s_urchinKeyInput : (Config::getUrchinApiKey().empty() ? "None (Rate-limited)" : "********************");
                  if (s_typingUrchinKey && (GetTickCount64() / 500) % 2 == 0) dispUrchinKey += "|";
                  g_guiFont.drawString(cx + 10, cy + 10, dispUrchinKey, applyAlpha(0xFFFFFFFF, alpha));
                  if (clickEvent && isHovered(mx, my, cx, cy, 350, 35)) {
                      s_typingUrchinKey = true; s_typingSeraphKey = s_typingSearch = s_typingApiKey = s_typingAutoGG = false;
                      s_urchinKeyInput = Config::getUrchinApiKey();
                  } else if (clickEvent && s_typingUrchinKey) {
                      Config::setUrchinApiKey(s_urchinKeyInput);
                      s_typingUrchinKey = false;
                  }
                  cy += 55;

                  g_guiFont.drawString(cx, cy, "Seraph API Key", applyAlpha(0xFFA0A0A5, alpha));
                  cy += 20;
                  RenderUtils::drawRect(cx, cy, 350, 35, THEME_CARD, 0.6f * alpha);
                  if (s_typingSeraphKey) RenderUtils::drawRect(cx, cy, 2, 35, THEME_NAVY, alpha);
                  std::string dispSeraphKey = s_typingSeraphKey ? s_seraphKeyInput : (Config::getSeraphApiKey().empty() ? "None" : "********************");
                  if (s_typingSeraphKey && (GetTickCount64() / 500) % 2 == 0) dispSeraphKey += "|";
                  g_guiFont.drawString(cx + 10, cy + 10, dispSeraphKey, applyAlpha(0xFFFFFFFF, alpha));
                  if (clickEvent && isHovered(mx, my, cx, cy, 350, 35)) {
                      s_typingSeraphKey = true; s_typingUrchinKey = s_typingSearch = s_typingApiKey = s_typingAutoGG = false;
                      NotificationManager::getInstance()->add("Input", "Seraph Key focused", NotificationType::Info);
                  } else if (clickEvent) {
                      if (s_typingSeraphKey) {
                          Config::setSeraphApiKey(s_seraphKeyInput);
                          NotificationManager::getInstance()->add("Seraph", "API Key Saved", NotificationType::Success);
                      }
                      s_typingSeraphKey = false;
                  }
                  cy += 70;
              }

              g_guiFont.drawString(cx, cy, "Players in Current Game", applyAlpha(0xFFFFFFFF, alpha));
              cy += 35;

              std::lock_guard<std::mutex> stLock(ChatInterceptor::g_statsMutex);
              if (ChatInterceptor::g_playerStatsMap.empty()) {
                  g_guiFont.drawString(cx, cy, "No players detected in this session.", applyAlpha(0xFF808085, alpha));
                  cy += 30;
              } else {
                  g_guiFont.drawString(cx, cy, "Player", applyAlpha(0xFFA0A0A5, alpha));
                  g_guiFont.drawString(cx + 140, cy, "Star", applyAlpha(0xFFA0A0A5, alpha));
                  g_guiFont.drawString(cx + 200, cy, "FKDR", applyAlpha(0xFFA0A0A5, alpha));
                  g_guiFont.drawString(cx + 280, cy, "Urchin", applyAlpha(0xFFA0A0A5, alpha));
                  g_guiFont.drawString(cx + 420, cy, "Seraph", applyAlpha(0xFFA0A0A5, alpha));
                  cy += 25;

                  for (const auto& pair : ChatInterceptor::g_playerStatsMap) {
                      const std::string& name = pair.first;
                      const auto& stats = pair.second;
                      
                      bool rowHov = isHovered(mx, my, cx - 10, cy - 5, g_w - 220, 35);
                      uint32_t nameCol = 0xFFFFFFFF;
                      auto itT = ChatInterceptor::g_playerTeamColor.find(name);
                      if (itT != ChatInterceptor::g_playerTeamColor.end()) {
                           if (itT->second == "Red") nameCol = 0xFFFF5555;
                           else if (itT->second == "Blue") nameCol = 0xFF5555FF;
                           else if (itT->second == "Green") nameCol = 0xFF55FF55;
                           else if (itT->second == "Yellow") nameCol = 0xFFFFFF55;
                           else if (itT->second == "Pink") nameCol = 0xFFFF55FF;
                           else if (itT->second == "Aqua") nameCol = 0xFF55FFFF;
                      }
                      g_guiFont.drawString(cx, cy, name, applyAlpha(nameCol, alpha));
                      
                      g_guiFont.drawString(cx + 140, cy, std::to_string(stats.bedwarsStar), applyAlpha(0xFFCCCCCC, alpha));
                      double fkdr = (stats.bedwarsFinalDeaths == 0) ? stats.bedwarsFinalKills : (double)stats.bedwarsFinalKills / stats.bedwarsFinalDeaths;
                      char fBuf[16]; sprintf_s(fBuf, "%.2f", fkdr);
                      g_guiFont.drawString(cx + 200, cy, fBuf, applyAlpha(0xFFCCCCCC, alpha));

                      if (Config::isTagsEnabled()) {
                          std::string activeS = Config::getActiveTagService();
                          
                          if (activeS == "Urchin" || activeS == "Both") {
                              auto uTagsRes = Urchin::getPlayerTags(name);
                              if (uTagsRes && !uTagsRes->tags.empty()) {
                                  std::string tS;
                                  for(auto& t : uTagsRes->tags) { if(!tS.empty()) tS += ", "; tS += t.type; }
                                  if (tS.length() > 25) tS = tS.substr(0, 22) + "...";
                                  g_guiFont.drawString(cx + 280, cy, tS, applyAlpha(0xFFE0E0E0, alpha));
                              } else g_guiFont.drawString(cx + 280, cy, "-", applyAlpha(0xFF505055, alpha));
                          } else g_guiFont.drawString(cx + 280, cy, "Disabled", applyAlpha(0xFF505055, alpha));

                          if (activeS == "Seraph" || activeS == "Both") {
                              auto sTagsRes = Seraph::getPlayerTags(name, stats.uuid);
                              if (sTagsRes && !sTagsRes->tags.empty()) {
                                  std::string tS;
                                  for(auto& t : sTagsRes->tags) { if(!tS.empty()) tS += ", "; tS += t.type; }
                                  if (tS.length() > 25) tS = tS.substr(0, 22) + "...";
                                  uint32_t sCol = 0xFFFF5555;
                                  if (tS.find("Confirmed") != std::string::npos) sCol = 0xFFFF55FF;
                                  g_guiFont.drawString(cx + 420, cy, tS, applyAlpha(sCol, alpha));
                              } else g_guiFont.drawString(cx + 420, cy, "-", applyAlpha(0xFF505055, alpha));
                          } else g_guiFont.drawString(cx + 420, cy, "Disabled", applyAlpha(0xFF505055, alpha));
                      } else {
                          g_guiFont.drawString(cx + 280, cy, "Disabled", applyAlpha(0xFF505055, alpha));
                      }
                      cy += 35;
                  }
              }
         }
         else if (s_activeTab == 3) {
              g_guiFont.drawString(cx, cy, "Configuration", applyAlpha(0xFFFFFFFF, alpha));
              cy += 40;

              g_guiFont.drawString(cx, cy, "Hypixel API Key", applyAlpha(0xFFA0A0A5, alpha));
              cy += 20;
              RenderUtils::drawRect(cx, cy, 350, 35, THEME_CARD, 0.6f * alpha);
              if (s_typingApiKey) RenderUtils::drawRect(cx, cy, 2, 35, THEME_NAVY, alpha);
              
              if (!s_typingApiKey) s_apiKeyInput = Config::getApiKey();
              
              std::string dispKey = s_typingApiKey ? s_apiKeyInput : (s_apiKeyInput.empty() ? "None" : "********************");
              if (s_typingApiKey && (GetTickCount64() / 500) % 2 == 0) dispKey += "|";
              
              g_guiFont.drawString(cx + 10, cy + 10, dispKey, applyAlpha(0xFFFFFFFF, alpha));
              
               if (clickEvent && isHovered(mx, my, cx, cy, 350, 35)) {
                   s_typingApiKey = true;
                   s_typingSearch = s_typingAutoGG = s_typingUrchinKey = false;
                   NotificationManager::getInstance()->add("Input", "API Key focused", NotificationType::Info);
               } else if (clickEvent) {
                   if (s_typingApiKey) {
                       Config::setApiKey(s_apiKeyInput);
                       NotificationManager::getInstance()->add("Settings", "API Key Saved", NotificationType::Success);
                   }
                   s_typingApiKey = false;
               }

              cy += 60;
              g_guiFont.drawString(cx, cy, "AutoGG Settings", applyAlpha(0xFFFFFFFF, alpha));
              cy += 30;
              bool aggEnabled = Config::isAutoGGEnabled();
               bool hAggCard = isHovered(mx, my, mainX + 190, cy - 10, g_w - 210, 60);
               glDisable(GL_TEXTURE_2D);
               DWORD aggCol = hAggCard ? 0xFF222226 : THEME_CARD;
               RenderUtils::drawRoundedRect(mainX + 190, cy - 10, g_w - 210, 60, 6.0f, aggCol, 0.6f * alpha);
               if (hAggCard) RenderUtils::drawRect(mainX + 190, cy - 10, 3, 60, THEME_NAVY, alpha);
               glEnable(GL_TEXTURE_2D);
               g_guiFont.drawString(cx, cy, "AutoGG Module", applyAlpha(0xFFFFFFFF, alpha));
               g_guiFont.drawString(cx, cy + 18, "Automatically send a message when game ends", applyAlpha(0xFFA0A0A5, alpha));
               glDisable(GL_TEXTURE_2D);
               float aggSwX = mainX + g_w - 65;
               drawSwitch(3, aggSwX, cy, aggEnabled, hAggCard, alpha);
               glEnable(GL_TEXTURE_2D);
              if (clickEvent && hAggCard) Config::setAutoGGEnabled(!aggEnabled);
              
              cy += 75;
              g_guiFont.drawString(cx, cy, "Custom GG Message", applyAlpha(0xFFA0A0A5, alpha));
              cy += 20;
              RenderUtils::drawRect(cx, cy, 350, 35, THEME_CARD, 0.6f * alpha);
              if (s_typingAutoGG) RenderUtils::drawRect(cx, cy, 2, 35, THEME_NAVY, alpha);
              
              if (s_autoGGInput.empty() && !s_typingAutoGG) s_autoGGInput = Config::getAutoGGMessage();
              std::string dispGG = s_autoGGInput;
              if (s_typingAutoGG && (GetTickCount64() / 500) % 2 == 0) dispGG += "|";
              if (dispGG.empty() && !s_typingAutoGG) dispGG = "Enter GG message...";
              
              g_guiFont.drawString(cx + 10, cy + 10, dispGG, applyAlpha(0xFFFFFFFF, alpha));
               if (clickEvent && isHovered(mx, my, cx, cy, 350, 35)) {
                   s_typingAutoGG = true;
                   s_typingApiKey = s_typingSearch = s_typingUrchinKey = false;
                   NotificationManager::getInstance()->add("Input", "AutoGG message focused", NotificationType::Info);
               } else if (clickEvent) {
                  if (s_typingAutoGG) {
                       Config::setAutoGGMessage(s_autoGGInput);
                       NotificationManager::getInstance()->add("AutoGG", "Custom message saved", NotificationType::Success);
                   }
                   s_typingAutoGG = false;
               }
          
          cy += 65;
          g_guiFont.drawString(cx, cy, "Discord Rich Presence", applyAlpha(0xFFFFFFFF, alpha));
          cy += 30;
          bool discordEnabled = Config::isDiscordRpcEnabled();
           bool hDiscordCard = isHovered(mx, my, mainX + 190, cy - 10, g_w - 210, 60);
           glDisable(GL_TEXTURE_2D);
           DWORD discCol = hDiscordCard ? 0xFF222226 : THEME_CARD;
           RenderUtils::drawRoundedRect(mainX + 190, cy - 10, g_w - 210, 60, 6.0f, discCol, 0.6f * alpha);
           if (hDiscordCard) RenderUtils::drawRect(mainX + 190, cy - 10, 3, 60, THEME_NAVY, alpha);
           glEnable(GL_TEXTURE_2D);
           g_guiFont.drawString(cx, cy, "Broadcasting Status", applyAlpha(0xFFFFFFFF, alpha));
           g_guiFont.drawString(cx, cy + 18, "Show your activity on Discord", applyAlpha(0xFFA0A0A5, alpha));
           glDisable(GL_TEXTURE_2D);
           float discordSwX = mainX + g_w - 65;
           drawSwitch(15, discordSwX, cy, discordEnabled, hDiscordCard, alpha);
           glEnable(GL_TEXTURE_2D);
          if (clickEvent && hDiscordCard) {
              Config::setDiscordRpcEnabled(!discordEnabled);
              NotificationManager::getInstance()->add("Discord", discordEnabled ? "Rich Presence Disabled" : "Rich Presence Enabled", !discordEnabled ? NotificationType::Success : NotificationType::Warning);
          }

          cy += 70;
              bool hover = isHovered(mx, my, cx, cy, 140, 35);
              glDisable(GL_TEXTURE_2D);
              RenderUtils::drawRoundedRect(cx, cy, 140, 35, 6.0f, hover ? THEME_NAVY : 0xFF2A2A2E, alpha); 
              glEnable(GL_TEXTURE_2D);
              g_guiFont.drawString(cx + 20, cy + 10, "SAVE CONFIG", applyAlpha(0xFFFFFFFF, alpha));
              if (clickEvent && hover) {
                  Config::save();
                  NotificationManager::getInstance()->add("Cloud", "Settings synchronized successfully!", NotificationType::Success);
              }
              cy += 50;

              g_guiFont.drawString(cx, cy, "Menu Toggle Key", applyAlpha(0xFFFFFFFF, alpha));
              cy += 25;
              
              std::string keyText = s_waitingForKey ? "Press any key... (ESC to cancel)" : ("Current: " + getKeyName(Config::getClickGuiKey()));
              if (s_waitingForKey && (GetTickCount64() / 300) % 2 == 0) keyText = "> " + keyText + " <";

              bool hBind = isHovered(mx, my, cx, cy, 250, 35);
              glDisable(GL_TEXTURE_2D);
              RenderUtils::drawRoundedRect(cx, cy, 250, 35, 6.0f, s_waitingForKey ? THEME_NAVY : (hBind ? 0xFF35353A : THEME_CARD), alpha);
              glEnable(GL_TEXTURE_2D);
              g_guiFont.drawString(cx + 20, cy + 10, keyText, applyAlpha(s_waitingForKey ? 0xFFFFFFFF : 0xFFA0A0A5, alpha));
              
              if (clickEvent && hBind && !s_waitingForKey) {
                  s_waitingForKey = true;
                  s_typingApiKey = s_typingSearch = false;
              }
              cy += 70;

              g_guiFont.drawString(cx, cy, "Accent Color", applyAlpha(0xFFFFFFFF, alpha));
              cy += 25;
              const DWORD presets[] = { 0xFF0055A4, 0xFFD32F2F, 0xFF388E3C, 0xFFFFC107, 0xFF8E24AA, 0xFF00ACC1, 0xFFFF5722 };
              const char* presetNames[] = { "Navy", "Ruby", "Emerald", "Gold", "Iris", "Cyan", "Flame" };
              int presetCount = sizeof(presets) / sizeof(presets[0]);
              DWORD currentTheme = Config::getThemeColor();
              
              float presetBoxSize = 35.0f;
              float presetGap = 10.0f;
              for (int i = 0; i < presetCount; ++i) {
                  float px = cx + i * (presetBoxSize + presetGap);
                  bool selected = (currentTheme == presets[i]);
                  bool hPre = isHovered(mx, my, px, cy, presetBoxSize, presetBoxSize);
                  
                  glDisable(GL_TEXTURE_2D);
                  RenderUtils::drawRoundedRect(px, cy, presetBoxSize, presetBoxSize, 4.0f, presets[i], alpha);
                  if (selected) RenderUtils::drawRoundedRect(px, cy + presetBoxSize - 3, presetBoxSize, 4.0f, 2.0f, 0xFFFFFFFF, alpha);
                  if (hPre && !selected) RenderUtils::drawRoundedRect(px, cy, presetBoxSize, 2, 2.0f, 0xFFFFFFFF, alpha * 0.6f);
                  glEnable(GL_TEXTURE_2D);
                  
                  if (clickEvent && hPre) {
                      Config::setThemeColor(presets[i]);
                      NotificationManager::getInstance()->add("Theme", "Accent set to " + std::string(presetNames[i]), NotificationType::Info);
                  }
              }
              cy += 60;

              
              cy += 50;
               cy += 50;
         }
         else if (s_activeTab == 4) {
              g_guiFont.drawString(cx, cy, "Debug Console Settings", applyAlpha(0xFFFFFFFF, alpha));
              cy += 40;

              bool dbgGlobal = Config::isGlobalDebugEnabled();
               bool hDbgCard = isHovered(mx, my, mainX + 190, cy - 10, g_w - 210, 60); 
               
               glDisable(GL_TEXTURE_2D);
               DWORD dbgCol = hDbgCard ? 0xFF222226 : THEME_CARD;
               RenderUtils::drawRoundedRect(mainX + 190, cy - 10, g_w - 210, 60, 6.0f, dbgCol, 0.6f * alpha);
               if (hDbgCard) RenderUtils::drawRect(mainX + 190, cy - 10, 3, 60, THEME_NAVY, alpha);
               glEnable(GL_TEXTURE_2D);

               g_guiFont.drawString(cx, cy, "Master Debug Switch", applyAlpha(0xFFFFFFFF, alpha));
               g_guiFont.drawString(cx, cy + 18, "Master toggle for all client debug logs", applyAlpha(0xFFA0A0A5, alpha));
               
               float dbgSwX = mainX + g_w - 65;
               glDisable(GL_TEXTURE_2D);
               drawSwitch(5, dbgSwX, cy, dbgGlobal, hDbgCard, alpha);
               glEnable(GL_TEXTURE_2D);

              if (clickEvent && hDbgCard) Config::setGlobalDebugEnabled(!dbgGlobal);
              
              cy += 75;

              if (Config::isGlobalDebugEnabled()) {
                  auto renderDebugToggle = [&](const char* title, int id, Config::DebugCategory cat) {
                      bool enabled = Config::isDebugEnabled(cat);
                      bool hov = isHovered(mx, my, cx, cy - 5, 240, 30); 
                      
                      g_guiFont.drawString(cx, cy, title, applyAlpha(hov ? 0xFFFFFFFF : 0xFF808085, alpha));
                      float toggleX = cx + 180;
                      
                       glDisable(GL_TEXTURE_2D);
                       drawSwitch(id, toggleX, cy - 5, enabled, hov, alpha);
                       glEnable(GL_TEXTURE_2D);
                      
                      if (clickEvent && hov) Config::setDebugEnabled(cat, !enabled);
                      cy += 35;
                  };
                  
                  renderDebugToggle("Game Detection", 7, Config::DebugCategory::GameDetection);
                  renderDebugToggle("Bed Detection", 8, Config::DebugCategory::BedDetection);
                  renderDebugToggle("Urchin Service", 9, Config::DebugCategory::Urchin);
                  renderDebugToggle("Bed Defense Sys", 10, Config::DebugCategory::BedDefense);
                  renderDebugToggle("GUI Internals", 11, Config::DebugCategory::GUI);
                  renderDebugToggle("General / Other", 12, Config::DebugCategory::General);
                  cy += 15;
              }

              g_guiFont.drawString(cx, cy, "Logs are sent to OutputDebugString", applyAlpha(0xFF808085, alpha));
              cy += 20;
              g_guiFont.drawString(cx, cy, "Use DbgView to see live output.", applyAlpha(0xFF808085, alpha));
               cy += 40;

               glDisable(GL_TEXTURE_2D);
               bool hTest = isHovered(mx, my, cx, cy, 200, 35);
               RenderUtils::drawRoundedRect(cx, cy, 200, 35, 6.0f, hTest ? THEME_NAVY : THEME_CARD, alpha);
               glEnable(GL_TEXTURE_2D);
               g_guiFont.drawString(cx + 30, cy + 10, "SEND TEST TOAST", applyAlpha(0xFFFFFFFF, alpha));
               if (clickEvent && hTest) {
                   NotificationManager::getInstance()->add("System", "Toast notifications are working!", NotificationType::Success);
               }
               cy += 50;

               bool hClear = isHovered(mx, my, cx, cy, 200, 35);
               glDisable(GL_TEXTURE_2D);
               RenderUtils::drawRoundedRect(cx, cy, 200, 35, 6.0f, hClear ? 0xFF991111 : THEME_CARD, alpha);
               glEnable(GL_TEXTURE_2D);
               g_guiFont.drawString(cx + 35, cy + 10, "CLEAR CACHE", applyAlpha(0xFFFFFFFF, alpha));
               if (clickEvent && hClear) {
                   ChatInterceptor::clearAllCaches();
                   NotificationManager::getInstance()->add("System", "All caches cleared and reset!", NotificationType::Success);
               }
               cy += 50;
         }

         float contentHeight = (cy + s_scrollOffset) - startCy;
         float visibleHeight = g_h - 100.0f; 
         s_maxScroll = (contentHeight > visibleHeight) ? (contentHeight - visibleHeight + 40.0f) : 0.0f;

         glDisable(GL_SCISSOR_TEST);

        glPopMatrix();

        glDisable(GL_TEXTURE_2D);
        {
            DWORD c = THEME_NAVY;
            float r = ((c >> 16) & 0xFF) / 255.0f;
            float g = ((c >> 8) & 0xFF) / 255.0f;
            float b = (c & 0xFF) / 255.0f;
            glColor4f(r, g, b, s_animAlpha); 
        }
        glBegin(GL_TRIANGLES);
        glVertex2f(mx, my);
        glVertex2f(mx, my + 15);
        glVertex2f(mx + 10, my + 10);
        glEnd();
        
        glPopAttrib();
        glPopMatrix();
    }
}