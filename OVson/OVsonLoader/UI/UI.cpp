#include "UI.h"
#include <math.h>

int g_targetProgress = 10;
double g_animProgressSmoothed = 10.0;
std::wstring g_statusText = L"Initializing...";
COLORREF g_statusColor = RGB(160, 160, 170);
HFONT g_titleFont = nullptr;
HFONT g_smallFont = nullptr;

void InitUI(HWND hWnd)
{
    g_titleFont = CreateFontW(32, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_smallFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

void CleanupUI()
{
    if (g_titleFont) DeleteObject(g_titleFont);
    if (g_smallFont) DeleteObject(g_smallFont);
}

void PaintUI(HWND hWnd, HDC hdc)
{
    RECT rc;
    GetClientRect(hWnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    HBRUSH bg = CreateSolidBrush(RGB(24, 25, 28));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HBRUSH border = CreateSolidBrush(RGB(44, 45, 49));
    FrameRect(hdc, &rc, border);
    DeleteObject(border);

    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, g_titleFont);
    
    SetTextColor(hdc, RGB(10, 10, 12)); 
    RECT rTextShadow = { 2, h / 2 - 44, w + 2, h / 2 };
    DrawTextW(hdc, L"OVson", -1, &rTextShadow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SetTextColor(hdc, RGB(255, 255, 255));
    RECT rText = { 0, h / 2 - 46, w, h / 2 };
    DrawTextW(hdc, L"OVson", -1, &rText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, g_smallFont);
    SetTextColor(hdc, g_statusColor);
    RECT rStatus = { 0, h / 2 + 2, w, h / 2 + 25 };
    DrawTextW(hdc, g_statusText.c_str(), -1, &rStatus, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    int barWidth = w - 80;
    int barHeight = 6;
    int barX = (w - barWidth) / 2;
    int barY = h - 30;

    RECT rTrack = { barX, barY, barX + barWidth, barY + barHeight };
    HBRUSH brTrack = CreateSolidBrush(RGB(18, 18, 20));
    FillRect(hdc, &rTrack, brTrack);
    DeleteObject(brTrack);

    if (g_animProgressSmoothed > 0.1)
    {
        int fillW = (int)((double)barWidth * (g_animProgressSmoothed / 100.0));
        if (fillW > barWidth) fillW = barWidth;
        if (fillW < 1) fillW = 0;

        if (fillW > 0)
        {
            RECT rFill = { barX, barY, barX + fillW, barY + barHeight };
            
            COLORREF cFill = RGB(88, 101, 242);
            
            if (g_targetProgress == 100 && g_statusText.find(L"Success") != std::wstring::npos)
                 cFill = RGB(87, 242, 135);
            else if (g_targetProgress == 100 && g_statusText.find(L"Fail") != std::wstring::npos)
                 cFill = RGB(237, 66, 69);
            else if (g_statusText.find(L"Search") != std::wstring::npos)
                 cFill = RGB(254, 231, 92);

            HBRUSH brFill = CreateSolidBrush(cFill);
            FillRect(hdc, &rFill, brFill);
            DeleteObject(brFill);

            if (fillW > 2)
            {
                RECT rHead = { barX + fillW - 2, barY, barX + fillW, barY + barHeight };
                HBRUSH brHead = CreateSolidBrush(RGB(255, 255, 255));
                FillRect(hdc, &rHead, brHead);
                DeleteObject(brHead);
            }
        }
    }
}
