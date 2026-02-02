#pragma once
#include <windows.h>
#include <gl/GL.h>
#include <cmath>
#include <cstdint>

namespace RenderUtils {
    extern float g_animAlpha;

    void drawRect(float x, float y, float w, float h, DWORD color, float alphaOverride = -1.0f);
    void drawGradientRect(float x, float y, float w, float h, DWORD col1, DWORD col2, float alphaShift = 1.0f);
    void drawCircleSector(float x, float y, float radius, float startAngle, float endAngle, int segments);
    void drawCircle(float x, float y, float radius, DWORD color, float alphaShift = 1.0f);
    void drawRoundedRect(float x, float y, float w, float h, float radius, DWORD color, float alphaOverride = -1.0f);
    void drawOutline(float x, float y, float w, float h, float thickness, DWORD color, float alphaOverride = -1.0f);
    void drawRoundedOutline(float x, float y, float w, float h, float radius, float thickness, DWORD color, float alphaOverride = -1.0f);
    
    DWORD lerpColor(DWORD c1, DWORD c2, float t);
}
