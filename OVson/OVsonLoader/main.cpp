#include <Windows.h>
#include <CommCtrl.h>
#include <math.h> // for fabs
#include "Utils.h"
#include "Injection.h"
#include "UI.h"

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Ws2_32.lib")

HANDLE g_loaderMutex = nullptr;
HINSTANCE g_hInst = nullptr;

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static InjectContext* ctx = nullptr;

    switch (msg)
    {
    case WM_CREATE:
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        HRGN rgn = CreateRoundRectRgn(0, 0, rc.right, rc.bottom, 16, 16);
        SetWindowRgn(hWnd, rgn, TRUE);
        
        InitUI(hWnd);
        SetTimer(hWnd, ID_TIMER_PROGRESS, 6, nullptr); 

        ctx = new InjectContext{ hWnd, L"" };
        std::vector<uint8_t> bytes;
        if (!getEmbeddedDllBytes(bytes))
        {
            g_statusText = L"Error: Payload missing";
            g_statusColor = RGB(220, 60, 60);
            return 0;
        }
        
        bool decrypted = decryptEmbeddedIfEncrypted(bytes);        
        ctx->dllBytes.swap(bytes);

        IpcNames names{};
        deriveIpcNames(names);
        for(int i=0; i<6; ++i) ctx->cpNames[i] = names.cp[i];

        DWORD pid = findProcessId(L"javaw.exe");
        if (pid && isAlreadyInjected(pid)) {
             g_statusText = L"Already injected";
             g_statusColor = RGB(220, 60, 60);
             g_targetProgress = 100;
             SetTimer(hWnd, ID_TIMER_CLOSE, 2000, nullptr);
             return 0;
        }

        g_loaderMutex = CreateMutexW(nullptr, FALSE, L"Global\\OVsonLoaderMutex");
        if (g_loaderMutex && GetLastError() != ERROR_ALREADY_EXISTS)
        {
             CreateThread(nullptr, 0, InjectThread, ctx, 0, nullptr);
        }
        else
        {
             g_statusText = L"Loader already open";
             g_statusColor = RGB(220, 150, 60);
             SetTimer(hWnd, ID_TIMER_CLOSE, 1500, nullptr);
        }
        break;
    }
    
    case WM_TIMER:
    {
        if (wParam == ID_TIMER_PROGRESS)
        {
            double diff = (double)g_targetProgress - g_animProgressSmoothed;
            if (fabs(diff) > 0.01)
            {
                g_animProgressSmoothed += diff * 0.05; 
            }
            else
            {
                g_animProgressSmoothed = (double)g_targetProgress;
            }
            
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (wParam == ID_TIMER_CLOSE)
        {
            KillTimer(hWnd, ID_TIMER_CLOSE);
            AnimateWindow(hWnd, 400, AW_BLEND | AW_HIDE);
            DestroyWindow(hWnd);
        }
        break;
    }

    case WM_APP_PROGRESS:
        g_targetProgress = (int)wParam;
        if (g_targetProgress > 100) g_targetProgress = 100;
        break;

    case (WM_APP + 1):
    {
        bool success = (wParam == 1);
        g_targetProgress = 100;
        if (success) {
            g_statusText = L"Injected Successfully";
            g_statusColor = RGB(80, 220, 100);
            SetTimer(hWnd, ID_TIMER_CLOSE, 1200, nullptr);
        } else {
            g_statusText = L"Injection Failed";
            g_statusColor = RGB(220, 60, 60);
            SetTimer(hWnd, ID_TIMER_CLOSE, 2500, nullptr);
        }
        break;
    }
    
    case (WM_APP + 3):
        g_statusText = L"Already Injected";
        g_statusColor = RGB(220, 60, 60);
        g_targetProgress = 100;
        SetTimer(hWnd, ID_TIMER_CLOSE, 1500, nullptr);
        break;

    case (WM_APP + 4):
        g_statusText = L"Process Not Found";
        g_statusColor = RGB(220, 60, 60);
        g_targetProgress = 100;
        SetTimer(hWnd, ID_TIMER_CLOSE, 2000, nullptr);
        break;

    case (WM_APP + 5):
    {
        wchar_t* msg = (wchar_t*)lParam;
        if (msg) g_statusText = msg;
        break;
    }

    case WM_NCHITTEST:
    {
        LRESULT hit = DefWindowProcW(hWnd, msg, wParam, lParam);
        if (hit == HTCLIENT) return HTCAPTION;
        return hit;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        
        PaintUI(hWnd, mem);
        
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        CleanupUI();
        if (g_loaderMutex) { ReleaseMutex(g_loaderMutex); CloseHandle(g_loaderMutex); }
        if (ctx) delete ctx;
        PostQuitMessage(0);
        break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    g_hInst = hInst;
    const wchar_t* cls = L"OVSON_LOADER_MAIN";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    
    RegisterClassW(&wc);
    
    int w = 400; 
    int h = 140;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    HWND mainWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW, cls, L"OVson", WS_POPUP | WS_VISIBLE, 
        x, y, w, h, nullptr, nullptr, hInst, nullptr);

    if (mainWnd)
    {
        SetLayeredWindowAttributes(mainWnd, 0, 255, LWA_ALPHA);
        AnimateWindow(mainWnd, 250, AW_BLEND);
        
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
