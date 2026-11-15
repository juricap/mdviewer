// Lister plugin (WLX) for Total Commander and Double Commander on Windows.
// A thin wrapper: all work happens in the mdview view window.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlwapi.h>

#include <string>

#include "platform/win32/view_window.h"

using namespace mdview::win32;

namespace {

// From the WLX SDK (listplug.h).
constexpr int lc_copy = 1, lc_newparams = 2, lc_selectall = 3, lc_setpercent = 4;
constexpr int lcp_forceshow = 16, lcp_darkmode = 128;
constexpr int lcs_findfirst = 1, lcs_matchcase = 2, lcs_backwards = 8;
constexpr int LISTPLUGIN_OK = 0, LISTPLUGIN_ERROR = 1;

struct ListDefaultParamStruct {
    int size;
    DWORD PluginInterfaceVersionLow;
    DWORD PluginInterfaceVersionHi;
    char DefaultIniName[MAX_PATH];
};

bool is_markdown(const wchar_t* path) {
    const wchar_t* ext = PathFindExtensionW(path);
    for (const wchar_t* e : {L".md", L".markdown", L".mdown", L".mkd", L".mkdn", L".mdwn", L".mdtxt"})
        if (_wcsicmp(ext, e) == 0) return true;
    return false;
}

std::wstring from_ansi(const char* s) {
    if (!s) return {};
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
    return w;
}

HWND create_lister(HWND parent, const wchar_t* file, int flags) {
    if (!file || (!(flags & lcp_forceshow) && !is_markdown(file))) return nullptr;
    RECT rc;
    GetClientRect(parent, &rc);
    ViewWindowOptions opts;
    opts.dark = (flags & lcp_darkmode) != 0;
    // Keys the view doesn't use go to the lister window (Esc, F-keys, N/P, 1-7...).
    opts.unhandled_key = [parent](UINT msg, WPARAM wp, LPARAM lp) {
        PostMessageW(parent, msg, wp, lp);
        return true;
    };
    HWND hwnd = create_view_window(parent, rc, opts);
    if (!hwnd) return nullptr;
    view_open(hwnd, file);
    SetFocus(hwnd);
    return hwnd;
}

}  // namespace

extern "C" {

HWND __stdcall ListLoadW(HWND ParentWin, WCHAR* FileToLoad, int ShowFlags) {
    try {
        return create_lister(ParentWin, FileToLoad, ShowFlags);
    } catch (...) {
        return nullptr;
    }
}

HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
    std::wstring w = from_ansi(FileToLoad);
    return ListLoadW(ParentWin, w.data(), ShowFlags);
}

int __stdcall ListLoadNextW(HWND, HWND PluginWin, WCHAR* FileToLoad, int ShowFlags) {
    if (!FileToLoad || (!(ShowFlags & lcp_forceshow) && !is_markdown(FileToLoad))) return LISTPLUGIN_ERROR;
    view_open(PluginWin, FileToLoad);  // reuse the window (fast quick-view browsing)
    view_set_dark(PluginWin, (ShowFlags & lcp_darkmode) != 0);
    return LISTPLUGIN_OK;
}

int __stdcall ListLoadNext(HWND ParentWin, HWND PluginWin, char* FileToLoad, int ShowFlags) {
    std::wstring w = from_ansi(FileToLoad);
    return ListLoadNextW(ParentWin, PluginWin, w.data(), ShowFlags);
}

void __stdcall ListCloseWindow(HWND ListWin) {
    if (ListWin && IsWindow(ListWin)) DestroyWindow(ListWin);
}

void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    strncpy_s(DetectString, maxlen,
              "MULTIMEDIA & (EXT=\"MD\" | EXT=\"MARKDOWN\" | EXT=\"MDOWN\" | EXT=\"MKD\" | EXT=\"MKDN\" | "
              "EXT=\"MDWN\" | EXT=\"MDTXT\")",
              _TRUNCATE);
}

int __stdcall ListSearchTextW(HWND ListWin, WCHAR* SearchString, int SearchParameter) {
    if (!SearchString) return LISTPLUGIN_ERROR;
    bool ok = view_find(ListWin, SearchString, (SearchParameter & lcs_matchcase) != 0,
                        (SearchParameter & lcs_backwards) != 0, (SearchParameter & lcs_findfirst) != 0);
    return ok ? LISTPLUGIN_OK : LISTPLUGIN_ERROR;
}

int __stdcall ListSearchText(HWND ListWin, char* SearchString, int SearchParameter) {
    std::wstring w = from_ansi(SearchString);
    return ListSearchTextW(ListWin, w.data(), SearchParameter);
}

int __stdcall ListSendCommand(HWND ListWin, int Command, int Parameter) {
    switch (Command) {
        case lc_copy: view_copy(ListWin); return LISTPLUGIN_OK;
        case lc_selectall: view_select_all(ListWin); return LISTPLUGIN_OK;
        case lc_newparams: view_set_dark(ListWin, (Parameter & lcp_darkmode) != 0); return LISTPLUGIN_OK;
        case lc_setpercent: view_scroll_to_percent(ListWin, Parameter); return LISTPLUGIN_OK;
    }
    return LISTPLUGIN_ERROR;
}

void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps) {
    if (dps) strcpy_s(dps->DefaultIniName, "mdview.ini");
}

int __stdcall ListNotificationReceived(HWND, int, WPARAM, LPARAM) { return 0; }

}  // extern "C"
