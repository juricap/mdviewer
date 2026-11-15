// mdview: standalone Markdown viewer (Windows frontend).
//
//   mdview.exe [--dark] [file.md]
//   mdview.exe --png out.png [--width 1000] [--height 1400] [--scroll 0] [--zoom 1] [--dark] file.md
//
// Esc closes, Ctrl +/-/0 zoom, drop a file to open it.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>
#include <vector>

#include "platform/win32/view_window.h"

using namespace mdview::win32;

namespace {

HWND g_view = nullptr;

void attach_console() {
    // Keep stdout if it is already redirected (pipe/file); else use the parent console.
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN) return;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
}

void set_title(HWND hwnd, const std::wstring& path) {
    std::wstring name = path.empty() ? L"" : std::filesystem::path(path).filename().wstring();
    SetWindowTextW(hwnd, name.empty() ? L"mdview" : (name + L" - mdview").c_str());
}

LRESULT CALLBACK frame_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            if (g_view) MoveWindow(g_view, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
            return 0;
        case WM_SETFOCUS:
            if (g_view) SetFocus(g_view);
            return 0;
        case WM_DROPFILES: {
            wchar_t path[MAX_PATH * 4];
            if (DragQueryFileW(reinterpret_cast<HDROP>(wp), 0, path, MAX_PATH * 4)) {
                view_open(g_view, path);
                set_title(hwnd, path);
            }
            DragFinish(reinterpret_cast<HDROP>(wp));
            return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int run_png(const std::vector<std::wstring>& args) {
    attach_console();
    std::wstring out, file;
    int width = 1000, height = 1400;
    long long scroll = 0;
    float zoom = 1.0f;
    bool dark = false;
    for (size_t i = 1; i < args.size(); i++) {
        const auto& a = args[i];
        auto next = [&]() { return i + 1 < args.size() ? args[++i] : std::wstring(); };
        if (a == L"--png") out = next();
        else if (a == L"--width") width = _wtoi(next().c_str());
        else if (a == L"--height") height = _wtoi(next().c_str());
        else if (a == L"--scroll") scroll = _wtoi64(next().c_str());
        else if (a == L"--zoom") zoom = static_cast<float>(_wtof(next().c_str()));
        else if (a == L"--dark") dark = true;
        else file = a;
    }
    if (out.empty() || file.empty()) {
        fprintf(stderr, "usage: mdview --png out.png [--width W] [--height H] [--scroll Y] [--zoom Z] [--dark] file.md\n");
        return 2;
    }
    std::string stats;
    bool ok = render_file_to_png(file, out, width, height, scroll, zoom, dark, &stats);
    printf("%s %s\n", ok ? "ok" : "FAILED", stats.c_str());
    return ok ? 0 : 1;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args(argv, argv + argc);
    LocalFree(argv);

    for (const auto& a : args)
        if (a == L"--png") return run_png(args);

    bool dark = false;
    std::wstring file;
    for (size_t i = 1; i < args.size(); i++) {
        if (args[i] == L"--dark") dark = true;
        else file = args[i];
    }

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = frame_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"MdViewFrame";
    RegisterClassExW(&wc);
    HWND frame = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"mdview", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                 CW_USEDEFAULT, 1100, 900, nullptr, nullptr, inst, nullptr);

    ViewWindowOptions opts;
    opts.dark = dark;
    opts.unhandled_key = [frame](UINT msg, WPARAM wp, LPARAM) {
        if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
            PostMessageW(frame, WM_CLOSE, 0, 0);
            return true;
        }
        return false;
    };
    RECT rc;
    GetClientRect(frame, &rc);
    g_view = create_view_window(frame, rc, opts);
    if (!file.empty()) {
        view_open(g_view, file);
        set_title(frame, file);
    }
    ShowWindow(frame, show);
    SetFocus(g_view);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
