// Minimal TC-like lister host: loads the WLX, calls ListLoadW, pumps messages and
// reports how long the UI thread was blocked.
// usage: host.exe <plugin.wlx> <file.md> <seconds>
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <string>

typedef HWND(__stdcall* ListLoadW_t)(HWND, WCHAR*, int);
typedef void(__stdcall* ListCloseWindow_t)(HWND);

using clk = std::chrono::steady_clock;
static clk::time_point g_lastTick;
static double g_maxGapMs = 0;

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER) {
        auto now = clk::now();
        double gap = std::chrono::duration<double, std::milli>(now - g_lastTick).count();
        if (gap > g_maxGapMs) g_maxGapMs = gap;
        g_lastTick = now;
        return 0;
    }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) return 2;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    auto load = (ListLoadW_t)GetProcAddress(dll, "ListLoadW");
    auto close = (ListCloseWindow_t)GetProcAddress(dll, "ListCloseWindow");

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FakeLister";
    RegisterClassW(&wc);
    HWND parent = CreateWindowW(L"FakeLister", L"fake lister", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 900, 700,
                                nullptr, nullptr, wc.hInstance, nullptr);

    auto t0 = clk::now();
    HWND lw = load(parent, argv[2], 0);
    double loadMs = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
    printf("ListLoadW returned %p in %.1f ms\n", (void*)lw, loadMs);
    if (!lw) return 1;

    SetTimer(parent, 1, 50, nullptr);
    g_lastTick = clk::now();
    double secs = _wtof(argv[3]);
    auto end = clk::now() + std::chrono::milliseconds((int)(secs * 1000));
    MSG msg;
    while (clk::now() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
    printf("max UI-thread gap while pumping: %.1f ms\n", g_maxGapMs);
    t0 = clk::now();
    close(lw);
    printf("ListCloseWindow took %.1f ms\n", std::chrono::duration<double, std::milli>(clk::now() - t0).count());
    DestroyWindow(parent);
    return 0;
}
