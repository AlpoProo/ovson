#include "NotificationManager.h"
#include "FontRenderer.h"
#include "../Utils/Timer.h"
#include "../Config/Config.h"
#include <gl/GL.h>
#include <algorithm>
#include <cmath>
#include "RenderHook.h"

namespace Render {

    static FontRenderer g_notifyFont;

    static uint32_t applyAlpha(uint32_t color, float alpha) {
        uint8_t a = (uint8_t)(((color >> 24) & 0xFF) * alpha);
        return (uint32_t)((a << 24) | (color & 0x00FFFFFF));
    }

    NotificationManager* NotificationManager::getInstance() {
        static NotificationManager s_instance;
        return &s_instance;
    }

    DWORD Notification::getTitleColor() const {
        switch (type) {
        case NotificationType::Success: return 0xFF00FF55; // Vibrant Green
        case NotificationType::Error:   return 0xFFFF3333; // Vibrant Red
        case NotificationType::Warning: return 0xFFFFCC00; // Bright Yellow
        case NotificationType::Info:    return Config::getThemeColor();
        default: return 0xFFFFFFFF;
        }
    }

    DWORD Notification::getBodyColor() const {
        return 0xFFE0E0E0; // Light White
    }

    void NotificationManager::add(const std::string& title, const std::string& message, NotificationType type, float duration) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        const size_t MAX_NOTIFICATIONS = 20;
        while (m_notifications.size() >= MAX_NOTIFICATIONS) {
            m_notifications.erase(m_notifications.begin());
        }
        
        Notification n;
        n.title = title;
        n.message = message;
        n.type = type;
        n.duration = duration;
        n.timer = 0.0f;
        n.slideAnim = 0.0f;
        m_notifications.push_back(n);
    }

    static void drawRect(float x, float y, float w, float h, DWORD color, float alphaMult = 1.0f) {
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8) & 0xFF) / 255.0f;
        float b = (color & 0xFF) / 255.0f;
        float a = (((color >> 24) & 0xFF) / 255.0f) * alphaMult;
        glColor4f(r, g, b, a);
        glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glVertex2f(x + w, y + h);
        glVertex2f(x, y + h);
        glEnd();
    }

    static float easeOutElastic(float x) {
        const float c4 = (2.0f * 3.14159f) / 3.0f;
        return x == 0 ? 0 : x == 1 ? 1 : (float)(pow(2, -10 * x) * sin((x * 10 - 0.75f) * c4) + 1);
    }

    void NotificationManager::render(HDC hdc) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_notifications.empty()) return;

        if (!m_fontInit || !g_notifyFont.isInitialized()) {
            if (g_notifyFont.init(hdc)) {
                m_fontInit = true;
                OutputDebugStringA("[OVson] Notification Font Initialized\n");
            } else {
                return; 
            }
        }

        float dt = RenderHook::getDelta();

        // GL setup
        glPushMatrix();
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_ALPHA_TEST); 
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);  
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();

        HWND hwnd = WindowFromDC(hdc);
        if (!hwnd) hwnd = GetActiveWindow(); 
        
        RECT rect = {0};
        if (hwnd) GetClientRect(hwnd, &rect);
        float sw = (float)(rect.right - rect.left);
        float sh = (float)(rect.bottom - rect.top);

        if (sw <= 0 || sh <= 0) {
            GLint vp[4];
            glGetIntegerv(GL_VIEWPORT, vp);
            sw = (float)vp[2];
            sh = (float)vp[3];
        }

        if (sw <= 0 || sh <= 0) {
            glPopMatrix();
            glMatrixMode(GL_MODELVIEW);
            glPopAttrib();
            glPopMatrix();
            return;
        }

        glOrtho(0, sw, sh, 0, -1, 1);
        
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        float padding = 20.0f;
        float notifW = 240.0f;
        float notifH = 54.0f;
        float yPos = sh - notifH - padding;

        for (auto it = m_notifications.begin(); it != m_notifications.end(); ) {
            it->timer += dt;
            
            float life = it->timer / it->duration;
            float revealDuration = 0.5f;
            float hideDuration = 0.4f;
            
            float alpha = 1.0f;
            float slide = 1.0f;

            if (it->timer < revealDuration) {
                float t = it->timer / revealDuration;
                slide = easeOutElastic(t);
                alpha = t;
            } else if (it->timer > it->duration - hideDuration) {
                float t = (it->duration - it->timer) / hideDuration;
                slide = t * t * t;
                alpha = t;
            }

            if (it->timer >= it->duration) {
                it = m_notifications.erase(it);
                continue;
            }

            float x = sw - (notifW * slide) - padding;
            float y = yPos;

            drawRect(x, y, notifW, notifH, 0xD0101012, alpha); 
            drawRect(x, y, 2, notifH, it->getTitleColor(), alpha);
            float progress = 1.0f - life;
            drawRect(x, y + notifH - 1, notifW * progress, 1, it->getTitleColor(), alpha * 0.8f);

            glEnable(GL_TEXTURE_2D);
            g_notifyFont.drawString(x + 14.0f, y + 10.0f, it->title, applyAlpha(it->getTitleColor(), alpha));
            g_notifyFont.drawString(x + 14.0f, y + 30.0f, it->message, applyAlpha(it->getBodyColor(), alpha));
            glDisable(GL_TEXTURE_2D);

            yPos -= (notifH + 12.0f);
            ++it;
        }

        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        
        glDepthMask(GL_TRUE);
        glPopAttrib();
        glPopMatrix();
    }
}
