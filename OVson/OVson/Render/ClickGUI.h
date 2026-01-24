#pragma once
#include <Windows.h>

namespace Render {
    class ClickGUI {
    public:
        static void init();
        static void render(HDC hdc);
        static void shutdown();
        static bool isOpen();
        static void toggle();
        static void updateInput(HWND hwnd);
        static void handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    };
}
