/*
 * hello.c — an ORIGINAL Win32 GUI application (Bible §11 compatibility proof).
 *
 * Compiled with MinGW to a genuine PE/Win32 .exe, this program uses only the
 * Windows API (user32/gdi32). It runs under Wine on Castalia OS to prove the
 * compatibility layer works end to end — with a program we wrote ourselves,
 * so it is entirely legally clean (no Microsoft code or assets).
 *
 * Build:  i686-w64-mingw32-gcc -O2 -mwindows -o hello.exe hello.c \
 *             -luser32 -lgdi32
 */
#include <windows.h>

/* Strings are Windows-1252 (single-byte), the ANSI codepage TextOutA uses. */
static const char *kTitle = "Aplicaci\xF3n Windows en Castalia OS";
static const char *kLine1 = "\xA1Hola desde una aplicaci\xF3n Win32!";
static const char *kLine2 =
    "Este es un .exe de Windows real, ejecut\xE1ndose bajo Wine";
static const char *kLine3 =
    "sobre Castalia OS \x97 Tombatossals Softworks.";
static const char *kLine4 =
    "Windows(R) es marca de Microsoft. Castalia no est\xE1 afiliado.";

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        /* sandstone-over-sea backdrop, echoing the Castalia palette */
        HBRUSH sky = CreateSolidBrush(RGB(0xEC, 0xE9, 0xE4));
        FillRect(hdc, &rc, sky);
        DeleteObject(sky);
        RECT band = rc;
        band.top = rc.bottom - 46;
        HBRUSH sea = CreateSolidBrush(RGB(0x3E, 0x82, 0xB6));
        FillRect(hdc, &band, sea);
        DeleteObject(sea);

        SetBkMode(hdc, TRANSPARENT);
        HFONT big = CreateFontA(28, 0, 0, 0, FW_BOLD, 0, 0, 0,
                                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                                0, "Arial");
        HFONT small = CreateFontA(16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                  DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                                  0, "Arial");
        HGDIOBJ old = SelectObject(hdc, big);
        SetTextColor(hdc, RGB(0x2C, 0x66, 0x99));
        TextOutA(hdc, 28, 34, kLine1, lstrlenA(kLine1));

        SelectObject(hdc, small);
        SetTextColor(hdc, RGB(0x1E, 0x1E, 0x1E));
        TextOutA(hdc, 28, 84, kLine2, lstrlenA(kLine2));
        TextOutA(hdc, 28, 108, kLine3, lstrlenA(kLine3));
        SetTextColor(hdc, RGB(0x5A, 0x56, 0x4E));
        TextOutA(hdc, 28, 150, kLine4, lstrlenA(kLine4));

        SelectObject(hdc, old);
        DeleteObject(big);
        DeleteObject(small);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    (void)hPrev;
    (void)cmd;
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "CastaliaHello";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA(
        wc.lpszClassName, kTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 260,
        NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
