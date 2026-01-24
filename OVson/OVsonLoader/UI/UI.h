#pragma once
#include <Windows.h>
#include <string>

extern int g_targetProgress;
extern double g_animProgressSmoothed;
extern std::wstring g_statusText;
extern COLORREF g_statusColor;
extern HFONT g_titleFont;
extern HFONT g_smallFont;

void InitUI(HWND hWnd);
void CleanupUI();
void PaintUI(HWND hWnd, HDC hdc);
