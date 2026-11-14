// Total Commander lister plugin (WLX) that renders Markdown via WebView2.
//
// Threading model (important - see obsidian/Large file freeze.md):
//   * ListLoadW runs on Total Commander's UI thread and must return quickly.
//     It only creates the host window and kicks off WebView2 creation.
//   * Reading + parsing the file happens on a worker thread (RenderThread).
//     The result is written to a temp .html file and the host window is
//     notified with WM_APP_RENDER_DONE.
//   * The page is loaded with Navigate(file://...) rather than
//     NavigateToString, which is limited to 2 MB of content.
//   * Markdown is converted with md4c (vendored in include/md4c, compiled in
//     statically). It is linear time: 20 MB in ~0.5 s, even as a single line.
//     The previous parser, maddy, took ~12 s/MB and minutes on long lines.

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "WebView2.h"
extern "C" {
#include "include/md4c/md4c-html.h"
}

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------

// Files larger than this are truncated (the first part is still rendered) to
// bound memory use: the HTML is ~2.5x the Markdown size, and the browser DOM more.
static const size_t kMaxDisplayBytes = 64u * 1024u * 1024u;
// md4c parser flags: GitHub dialect = tables, strikethrough, task lists, autolinks.
static const unsigned kMd4cParserFlags = MD_DIALECT_GITHUB;
// Documents at least this big are rendered in independent chunks (see SplitMarkdown).
static const size_t kChunkThreshold = 512u * 1024u;
static const size_t kChunkBytes = 64u * 1024u;
// Worker stack reservation (generous; md4c itself does not recurse deeply).
static const SIZE_T kWorkerStackReserve = 8u * 1024u * 1024u;

#define WM_APP_RENDER_DONE (WM_APP + 1)

// ---------------------------------------------------------------------------
// Logging (only active when DEBUG_LOG is defined)
// ---------------------------------------------------------------------------

void DebugLog(const std::string& message) {
#ifdef DEBUG_LOG
    try {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string logPath = std::string(tempPath) + "tc_markdown_lister.log";
        std::ofstream logFile(logPath, std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto t = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
            localtime_s(&tm, &t);
            logFile << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " [" << GetCurrentThreadId()
                    << "] " << message << std::endl;
        }
    } catch (...) {
    }
#else
    (void)message;
#endif
}

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring CodepageToWide(const char* s, UINT cp) {
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(cp, 0, s, -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(n - 1, 0);
    MultiByteToWideChar(cp, 0, s, -1, &w[0], n);
    return w;
}

// Valid UTF-8 check. An incomplete sequence at the very end is accepted,
// because the input may have been truncated to kMaxDisplayBytes.
bool IsUtf8(const std::string& data) {
    const unsigned char* b = (const unsigned char*)data.data();
    size_t len = data.size();
    for (size_t i = 0; i < len;) {
        size_t extra;
        if (b[i] < 0x80) { i++; continue; }
        else if ((b[i] & 0xE0) == 0xC0) extra = 1;
        else if ((b[i] & 0xF0) == 0xE0) extra = 2;
        else if ((b[i] & 0xF8) == 0xF0) extra = 3;
        else return false;
        for (size_t k = 1; k <= extra; k++) {
            if (i + k >= len) return true;  // truncated tail
            if ((b[i + k] & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

std::string AnsiToUtf8(const std::string& ansi) {
    if (ansi.empty()) return {};
    int n = MultiByteToWideChar(CP_ACP, 0, ansi.data(), (int)ansi.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_ACP, 0, ansi.data(), (int)ansi.size(), &w[0], n);
    return WideToUtf8(w);
}

void AppendHtmlEscaped(std::string& out, const std::string& in) {
    out.reserve(out.size() + in.size() + in.size() / 16);
    for (char c : in) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
}

std::wstring PathToFileUrl(const std::wstring& path) {
    wchar_t url[4096];
    DWORD len = 4096;
    if (SUCCEEDED(UrlCreateFromPathW(path.c_str(), url, &len, 0))) return url;
    return L"";
}

// ---------------------------------------------------------------------------
// File reading / HTML generation (runs on the worker thread)
// ---------------------------------------------------------------------------

// Reads up to kMaxDisplayBytes and converts to UTF-8 (UTF-8 or ANSI input).
bool ReadFileUtf8(const std::wstring& path, std::string& out, bool& truncated, unsigned long long& fileSize) {
    truncated = false;
    fileSize = 0;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    fileSize = (unsigned long long)sz.QuadPart;
    size_t toRead = (size_t)min((unsigned long long)kMaxDisplayBytes, fileSize);
    truncated = fileSize > kMaxDisplayBytes;
    std::string data(toRead, 0);
    size_t got = 0;
    while (got < toRead) {
        DWORD chunk = (DWORD)min((size_t)(1u << 20), toRead - got);
        DWORD n = 0;
        if (!ReadFile(h, &data[got], chunk, &n, nullptr) || n == 0) break;
        got += n;
    }
    CloseHandle(h);
    data.resize(got);

    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF) {
        data.erase(0, 3);
        out.swap(data);
    } else if (IsUtf8(data)) {
        out.swap(data);
    } else {
        out = AnsiToUtf8(data);
    }
    return true;
}

static void Md4cAppend(const MD_CHAR* text, MD_SIZE size, void* userdata) {
    static_cast<std::string*>(userdata)->append(text, size);
}

static bool IsFenceLine(const char* p, const char* end) {
    while (p < end && *p == ' ') p++;
    return end - p >= 3 && ((p[0] == '`' && p[1] == '`' && p[2] == '`') || (p[0] == '~' && p[1] == '~' && p[2] == '~'));
}

// Splits Markdown into chunks of roughly kChunkBytes at blank lines that are
// outside code fences and not followed by indented (continuation) text.
// Chunks are parsed independently; the cost is that a construct spanning a
// split point (e.g. a reference-style link defined in another chunk) may not
// resolve. Only used for documents larger than kChunkThreshold.
std::vector<std::pair<size_t, size_t>> SplitMarkdown(const std::string& md) {
    std::vector<std::pair<size_t, size_t>> chunks;
    const char* base = md.data();
    size_t n = md.size(), start = 0, pos = 0;
    bool inFence = false, prevBlank = false;
    while (pos < n) {
        size_t eol = md.find('\n', pos);
        size_t next = (eol == std::string::npos) ? n : eol + 1;
        const char* ls = base + pos;
        const char* le = base + ((eol == std::string::npos) ? n : eol);
        if (le > ls && le[-1] == '\r') le--;
        bool blank = true;
        for (const char* p = ls; p < le; p++)
            if (*p != ' ' && *p != '\t') { blank = false; break; }
        if (!blank && IsFenceLine(ls, le)) inFence = !inFence;
        // Split before this line if: previous line blank, we're big enough, not in a
        // fence, and this line starts a new top-level block (not indented).
        if (prevBlank && !blank && !inFence && pos - start >= kChunkBytes && *ls != ' ' && *ls != '\t') {
            chunks.emplace_back(start, pos - start);
            start = pos;
        }
        prevBlank = blank;
        pos = next;
    }
    if (start < n) chunks.emplace_back(start, n - start);
    return chunks;
}

// Markdown (UTF-8) -> HTML fragment, appended to out. Returns false on parser error.
// Large documents are emitted as <section class="chunk"> blocks; with CSS
// content-visibility:auto the browser skips layout of off-screen sections.
bool MarkdownToHtml(const std::string& md, std::string& out) {
    out.reserve(out.size() + md.size() * 5 / 2);
    if (md.size() < kChunkThreshold)
        return md_html(md.data(), (MD_SIZE)md.size(), Md4cAppend, &out, kMd4cParserFlags, 0) == 0;
    auto chunks = SplitMarkdown(md);
    DebugLog("Split into " + std::to_string(chunks.size()) + " chunks");
    for (auto& c : chunks) {
        out += "<section class=\"chunk\">\n";
        if (md_html(md.data() + c.first, (MD_SIZE)c.second, Md4cAppend, &out, kMd4cParserFlags, 0) != 0)
            return false;
        out += "</section>\n";
    }
    return true;
}

static const char* kCss = R"CSS(
body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; padding: 20px;
       max-width: 900px; margin: 0 auto; line-height: 1.6; color: #333; background: white; }
h1, h2, h3, h4, h5, h6 { margin-top: 24px; margin-bottom: 16px; font-weight: 600; }
h1 { font-size: 2em; border-bottom: 1px solid #eaecef; padding-bottom: .3em; }
h2 { font-size: 1.5em; border-bottom: 1px solid #eaecef; padding-bottom: .3em; }
h3 { font-size: 1.25em; }
code { background-color: rgba(27,31,35,.05); padding: .2em .4em; margin: 0; font-size: 85%;
       border-radius: 3px; font-family: Consolas, 'Courier New', monospace; }
pre { background-color: #f6f8fa; padding: 16px; overflow: auto; font-size: 85%; line-height: 1.45; border-radius: 3px; }
pre code { background-color: transparent; padding: 0; }
pre.plain { white-space: pre-wrap; word-break: break-word; background: transparent; padding: 0;
            font-family: Consolas, 'Courier New', monospace; }
blockquote { margin: 0; padding: 0 1em; color: #6a737d; border-left: .25em solid #dfe2e5; }
table { border-spacing: 0; border-collapse: collapse; margin-top: 0; margin-bottom: 16px; }
table th, table td { padding: 6px 13px; border: 1px solid #dfe2e5; }
table tr:nth-child(2n) { background-color: #f6f8fa; }
a { color: #0366d6; text-decoration: none; }
a:hover { text-decoration: underline; }
img { max-width: 100%; height: auto; }
section.chunk { content-visibility: auto; contain-intrinsic-size: auto 4000px; }
li.task-list-item { list-style-type: none; }
li.task-list-item input { margin: 0 .3em 0 -1.3em; vertical-align: middle; }
del { opacity: .7; }
.notice { font-size: 90%; padding: 8px 12px; margin-bottom: 16px; border-radius: 4px;
          background: #fff8c5; border: 1px solid #d4a72c; color: #3b2e00; }
.loading { color: #888; font-style: italic; }
body.dark { background: #0d1117; color: #f0f6fc; }
.dark h1, .dark h2 { border-bottom-color: #30363d; }
.dark code { background-color: rgba(110,118,129,.4); }
.dark pre { background-color: #161b22; }
.dark pre.plain { background: transparent; }
.dark table th, .dark table td { border-color: #30363d; }
.dark table tr:nth-child(2n) { background-color: #161b22; }
.dark blockquote { color: #8b949e; border-left-color: #30363d; }
.dark a { color: #58a6ff; }
.dark .notice { background: #272115; border-color: #9e6a03; color: #e3b341; }
)CSS";

std::string HtmlHead(const std::wstring& baseDirUrl) {
    std::string h = "<!DOCTYPE html>\n<html><head><meta charset=\"UTF-8\">\n";
    if (!baseDirUrl.empty()) {
        h += "<base href=\"";
        AppendHtmlEscaped(h, WideToUtf8(baseDirUrl));
        h += "\">\n";
    }
    h += "<style>";
    h += kCss;
    h += "</style></head>\n";
    return h;
}

std::string BodyOpen(bool dark) { return dark ? "<body class=\"dark\">\n" : "<body>\n"; }

std::string FormatMB(unsigned long long bytes) {
    char buf[32];
    sprintf_s(buf, "%.1f MB", bytes / (1024.0 * 1024.0));
    return buf;
}

std::string BuildDocumentHtml(const std::wstring& mdPath, bool dark) {
    std::wstring dir = mdPath;
    size_t slash = dir.find_last_of(L"\\/");
    dir = (slash == std::wstring::npos) ? L"" : dir.substr(0, slash + 1);
    std::string html = HtmlHead(dir.empty() ? L"" : PathToFileUrl(dir)) + BodyOpen(dark);

    std::string md;
    bool truncated = false;
    unsigned long long fileSize = 0;
    if (!ReadFileUtf8(mdPath, md, truncated, fileSize)) {
        html += "<p>Error: could not open file.</p></body></html>";
        return html;
    }
    DebugLog("Read " + std::to_string(md.size()) + " bytes (file " + std::to_string(fileSize) + ")");

    if (truncated) {
        // Cut at the last line break so we don't end mid-line.
        size_t nl = md.find_last_of('\n');
        if (nl != std::string::npos) md.resize(nl + 1);
        html += "<div class=\"notice\">File is " + FormatMB(fileSize) + "; showing only the first " +
                FormatMB(kMaxDisplayBytes) + ".</div>\n";
    }

    size_t before = html.size();
    if (!MarkdownToHtml(md, html)) {
        // md4c only fails on out-of-memory; show escaped text rather than nothing.
        DebugLog("md4c failed, falling back to plain text");
        html.resize(before);
        html += "<pre class=\"plain\">";
        AppendHtmlEscaped(html, md);
        html += "</pre>";
    }
    html += "\n</body></html>";
    return html;
}

std::wstring LoadingHtml(bool dark) {
    std::string h = HtmlHead(L"") + BodyOpen(dark) + "<p class=\"loading\">Loading&hellip;</p></body></html>";
    return CodepageToWide(h.c_str(), CP_UTF8);
}

// ---------------------------------------------------------------------------
// Temp files
// ---------------------------------------------------------------------------

std::wstring GetDataDir() {
    wchar_t appdata[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appdata))) return L"";
    std::wstring d = std::wstring(appdata) + L"\\TCMarkdownViewer";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

std::wstring GetRenderDir() {
    std::wstring d = GetDataDir();
    if (d.empty()) return L"";
    d += L"\\render";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

// Remove render files left behind (e.g. by a crash) that are older than a day.
void CleanupStaleRenders(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.html").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    ULONGLONG now = ((ULONGLONG)nowFt.dwHighDateTime << 32) | nowFt.dwLowDateTime;
    const ULONGLONG day = 24ull * 3600ull * 10000000ull;
    do {
        ULONGLONG t = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
        if (now > t && now - t > day) DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

bool WriteAllBytes(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    size_t done = 0;
    bool ok = true;
    while (done < data.size()) {
        DWORD chunk = (DWORD)min((size_t)(1u << 20), data.size() - done);
        DWORD n = 0;
        if (!WriteFile(h, data.data() + done, chunk, &n, nullptr) || n == 0) { ok = false; break; }
        done += n;
    }
    CloseHandle(h);
    return ok;
}

// ---------------------------------------------------------------------------
// WLX constants (from listplug.h)
// ---------------------------------------------------------------------------

#define lc_copy 1
#define lc_newparams 2
#define lc_selectall 3
#define lc_setpercent 4

#define lcp_wraptext 1
#define lcp_fittowindow 2
#define lcp_ansi 4
#define lcp_ascii 8
#define lcp_variable 12
#define lcp_forceshow 16
#define lcp_fitlargeronly 32
#define lcp_center 64
#define lcp_darkmode 128
#define lcp_darkmodenative 256

#define lcs_findfirst 1
#define lcs_matchcase 2
#define lcs_wholewords 4
#define lcs_backwards 8

#define LISTPLUGIN_OK 0
#define LISTPLUGIN_ERROR 1

typedef struct {
    int size;
    DWORD PluginInterfaceVersionLow;
    DWORD PluginInterfaceVersionHi;
    char DefaultIniName[MAX_PATH];
} ListDefaultParamStruct;

// ---------------------------------------------------------------------------
// Per-window state
// ---------------------------------------------------------------------------

HINSTANCE g_hInstance = nullptr;

// Shared between UI thread and worker thread.
struct RenderJob {
    std::wstring mdPath;
    std::wstring htmlPath;
    bool dark = false;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::atomic<bool> cancelled{false};
    std::atomic<bool> done{false};
    std::atomic<bool> ok{false};
};

// UI-thread only.
struct View2 {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    std::shared_ptr<RenderJob> job;
    bool navigated = false;
};

static std::map<HWND, View2> g_views;

// Navigate to the rendered file once both the WebView and the render are ready.
void TryShowRendered(HWND hwnd) {
    auto it = g_views.find(hwnd);
    if (it == g_views.end()) return;
    View2& v = it->second;
    if (v.navigated || !v.webview || !v.job || !v.job->done) return;
    v.navigated = true;
    if (v.job->ok) {
        std::wstring url = PathToFileUrl(v.job->htmlPath);
        HRESULT hr = v.webview->Navigate(url.c_str());
        DebugLog("Navigate to rendered file, hr=" + std::to_string(hr));
    } else {
        v.webview->NavigateToString(L"<html><body><p>Error: could not render file.</p></body></html>");
    }
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

struct ThreadArg {
    std::shared_ptr<RenderJob> job;
    HWND hwnd;
    HMODULE module;  // extra reference so the DLL can't unload under us
};

DWORD WINAPI RenderThread(LPVOID param) {
    ThreadArg* arg = static_cast<ThreadArg*>(param);
    HMODULE module = arg->module;
    {
        std::shared_ptr<RenderJob> job = arg->job;
        HWND hwnd = arg->hwnd;
        delete arg;

        auto t0 = std::chrono::steady_clock::now();
        std::string html;
        try {
            html = BuildDocumentHtml(job->mdPath, job->dark);
        } catch (...) {
            html = "<html><body><p>Error: failed to render file.</p></body></html>";
        }
        DebugLog("Render took " +
                 std::to_string(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()) +
                 " s, html " + std::to_string(html.size()) + " bytes");

        if (!job->cancelled) {
            job->ok = WriteAllBytes(job->htmlPath, html);
            job->done = true;
            if (job->cancelled) {
                DeleteFileW(job->htmlPath.c_str());  // window closed while writing
            } else {
                PostMessage(hwnd, WM_APP_RENDER_DONE, 0, 0);
            }
        }
    }  // all destructors run before the module reference is dropped
    FreeLibraryAndExitThread(module, 0);
    return 0;
}

bool StartRender(HWND hwnd, const std::shared_ptr<RenderJob>& job) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&RenderThread, &module)) return false;
    ThreadArg* arg = new ThreadArg{job, hwnd, module};
    HANDLE th = CreateThread(nullptr, kWorkerStackReserve, RenderThread, arg, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!th) {
        delete arg;
        FreeLibrary(module);
        return false;
    }
    CloseHandle(th);
    return true;
}

// ---------------------------------------------------------------------------
// Host window
// ---------------------------------------------------------------------------

LRESULT CALLBACK HostWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE: {
            auto it = g_views.find(hWnd);
            if (it != g_views.end() && it->second.controller) {
                RECT rc{};
                GetClientRect(hWnd, &rc);
                it->second.controller->put_Bounds(rc);
            }
            break;
        }
        case WM_APP_RENDER_DONE:
            TryShowRendered(hWnd);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                HWND parent = GetParent(hWnd);
                if (parent) PostMessage(parent, WM_CLOSE, 0, 0);
                return 0;
            }
            break;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

static const wchar_t* kHostClass = L"TCMarkdownViewer";

bool EnsureHostClass() {
    static bool registered = false;
    if (registered) return true;
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HostWndProc;
    wc.hInstance = g_hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kHostClass;
    registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return registered;
}

bool IsMarkdownFile(const std::wstring& path) {
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    static const wchar_t* exts[] = {L".md", L".markdown", L".mdown", L".mkd", L".mkdn"};
    for (auto e : exts)
        if (_wcsicmp(ext, e) == 0) return true;
    return false;
}

static const wchar_t* kEscScript = LR"(
document.addEventListener('keydown', function(event) {
    if (event.key === 'Escape') window.chrome.webview.postMessage('ESC_PRESSED');
});
)";

void OnControllerCreated(HWND hwnd, ICoreWebView2Controller* ctl) {
    auto it = g_views.find(hwnd);
    if (it == g_views.end()) {
        // Lister was closed before WebView2 finished initialising.
        ctl->Close();
        return;
    }
    View2& v = it->second;
    v.controller = ctl;
    ctl->get_CoreWebView2(&v.webview);

    RECT rc{};
    GetClientRect(hwnd, &rc);
    ctl->put_Bounds(rc);

    if (v.webview) {
        ComPtr<ICoreWebView2Settings> settings;
        v.webview->get_Settings(&settings);
        if (settings) {
            settings->put_IsZoomControlEnabled(TRUE);
            settings->put_AreDefaultContextMenusEnabled(TRUE);
        }
#ifdef DEBUG_LOG
        v.webview->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [hwnd](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                    BOOL ok = FALSE;
                    args->get_IsSuccess(&ok);
                    auto it = g_views.find(hwnd);
                    double secs = 0;
                    if (it != g_views.end() && it->second.job)
                        secs = std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                             it->second.job->started).count();
                    DebugLog("NavigationCompleted success=" + std::to_string(ok) + " at " +
                             std::to_string(secs) + " s after ListLoad");
                    return S_OK;
                })
                .Get(),
            nullptr);
#endif
        // Runs on every document (unlike ExecuteScript, which only hit the first one).
        v.webview->AddScriptToExecuteOnDocumentCreated(kEscScript, nullptr);
        v.webview->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [hwnd](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    LPWSTR message = nullptr;
                    if (SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message) {
                        if (wcscmp(message, L"ESC_PRESSED") == 0) {
                            HWND parent = GetParent(hwnd);
                            if (parent) PostMessage(parent, WM_CLOSE, 0, 0);
                        }
                        CoTaskMemFree(message);
                    }
                    return S_OK;
                })
                .Get(),
            nullptr);

        if (v.job && v.job->done) {
            TryShowRendered(hwnd);
        } else {
            v.webview->NavigateToString(LoadingHtml(v.job && v.job->dark).c_str());
        }
    }
    ctl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

HWND CreateLister(HWND parentWin, const std::wstring& path, int showFlags) {
    DebugLog("ListLoad: " + WideToUtf8(path) + " flags=" + std::to_string(showFlags));
    if (!(showFlags & lcp_forceshow) && !IsMarkdownFile(path)) return nullptr;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (!EnsureHostClass()) return nullptr;

    std::wstring dataDir = GetDataDir();
    std::wstring renderDir = GetRenderDir();
    if (renderDir.empty()) return nullptr;
    static bool cleaned = false;
    if (!cleaned) {
        CleanupStaleRenders(renderDir);
        cleaned = true;
    }

    RECT pr{};
    GetClientRect(parentWin, &pr);
    HWND hwnd = CreateWindowExW(0, kHostClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0,
                                pr.right - pr.left, pr.bottom - pr.top, parentWin, nullptr, g_hInstance, nullptr);
    if (!hwnd) return nullptr;

    static std::atomic<unsigned> counter{0};
    auto job = std::make_shared<RenderJob>();
    job->mdPath = path;
    job->dark = (showFlags & lcp_darkmode) != 0;
    job->htmlPath = renderDir + L"\\view_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                    std::to_wstring(++counter) + L".html";
    g_views[hwnd].job = job;

    if (!StartRender(hwnd, job)) {
        DebugLog("Failed to start render thread");
        g_views.erase(hwnd);
        DestroyWindow(hwnd);
        return nullptr;
    }

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, dataDir.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT envHr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(envHr) || !env) {
                    DebugLog("WebView2 environment failed: " + std::to_string(envHr));
                    return S_OK;
                }
                if (g_views.find(hwnd) == g_views.end()) return S_OK;  // already closed
                env->CreateCoreWebView2Controller(
                    hwnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                              [hwnd](HRESULT ctlHr, ICoreWebView2Controller* ctl) -> HRESULT {
                                  if (FAILED(ctlHr) || !ctl) {
                                      DebugLog("WebView2 controller failed: " + std::to_string(ctlHr));
                                      return S_OK;
                                  }
                                  OnControllerCreated(hwnd, ctl);
                                  return S_OK;
                              })
                              .Get());
                return S_OK;
            })
            .Get());
    if (FAILED(hr)) DebugLog("CreateCoreWebView2EnvironmentWithOptions failed: " + std::to_string(hr));

    return hwnd;
}

// JSON string literal for embedding in scripts.
std::wstring JsString(const std::wstring& s) {
    std::wstring r = L"\"";
    for (wchar_t c : s) {
        switch (c) {
            case L'"': r += L"\\\""; break;
            case L'\\': r += L"\\\\"; break;
            case L'\n': r += L"\\n"; break;
            case L'\r': r += L"\\r"; break;
            default:
                if (c < 0x20) {
                    wchar_t buf[8];
                    swprintf_s(buf, L"\\u%04x", c);
                    r += buf;
                } else {
                    r += c;
                }
        }
    }
    return r + L"\"";
}

ICoreWebView2* WebViewFor(HWND w) {
    auto it = g_views.find(w);
    return it == g_views.end() ? nullptr : it->second.webview.Get();
}

// ---------------------------------------------------------------------------
// Exported WLX API
// ---------------------------------------------------------------------------

extern "C" {

__declspec(dllexport) HWND __stdcall ListLoadW(HWND ParentWin, WCHAR* FileToLoad, int ShowFlags) {
    if (!FileToLoad) return nullptr;
    try {
        return CreateLister(ParentWin, FileToLoad, ShowFlags);
    } catch (...) {
        DebugLog("Exception in ListLoadW");
        return nullptr;
    }
}

__declspec(dllexport) HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
    // TC passes the ANSI path in the system codepage.
    std::wstring w = CodepageToWide(FileToLoad, CP_ACP);
    if (w.empty()) return nullptr;
    return ListLoadW(ParentWin, &w[0], ShowFlags);
}

__declspec(dllexport) void __stdcall ListCloseWindow(HWND ListWin) {
    auto it = g_views.find(ListWin);
    if (it != g_views.end()) {
        if (it->second.job) {
            it->second.job->cancelled = true;
            if (it->second.job->done) DeleteFileW(it->second.job->htmlPath.c_str());
        }
        if (it->second.controller) it->second.controller->Close();
        g_views.erase(it);
    }
    if (ListWin && IsWindow(ListWin)) DestroyWindow(ListWin);
}

__declspec(dllexport) void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    // MULTIMEDIA overrides TC's internal viewer for these extensions.
    const char* detectStr = "MULTIMEDIA & (EXT=\"MD\" | EXT=\"MARKDOWN\" | EXT=\"MDOWN\" | EXT=\"MKD\" | EXT=\"MKDN\")";
    strncpy_s(DetectString, maxlen, detectStr, _TRUNCATE);
}

__declspec(dllexport) int __stdcall ListSearchTextW(HWND ListWin, WCHAR* SearchString, int SearchParameter) {
    ICoreWebView2* wv = WebViewFor(ListWin);
    if (!wv || !SearchString) return LISTPLUGIN_ERROR;
    // window.find(text, caseSensitive, backwards, wrapAround, wholeWord)
    std::wstring script = L"window.find(" + JsString(SearchString) + L"," +
                          ((SearchParameter & lcs_matchcase) ? L"true" : L"false") + L"," +
                          ((SearchParameter & lcs_backwards) ? L"true" : L"false") + L",true," +
                          ((SearchParameter & lcs_wholewords) ? L"true" : L"false") + L")";
    if (SearchParameter & lcs_findfirst) script = L"window.getSelection().removeAllRanges();" + script;
    wv->ExecuteScript(script.c_str(), nullptr);
    return LISTPLUGIN_OK;
}

__declspec(dllexport) int __stdcall ListSearchText(HWND ListWin, char* SearchString, int SearchParameter) {
    std::wstring w = CodepageToWide(SearchString, CP_ACP);
    if (w.empty()) return LISTPLUGIN_ERROR;
    return ListSearchTextW(ListWin, &w[0], SearchParameter);
}

__declspec(dllexport) int __stdcall ListSendCommand(HWND ListWin, int Command, int Parameter) {
    ICoreWebView2* wv = WebViewFor(ListWin);
    if (!wv) return LISTPLUGIN_ERROR;
    switch (Command) {
        case lc_copy:
            wv->ExecuteScript(L"document.execCommand('copy')", nullptr);
            return LISTPLUGIN_OK;
        case lc_selectall:
            wv->ExecuteScript(L"document.execCommand('selectAll')", nullptr);
            return LISTPLUGIN_OK;
        default:
            return LISTPLUGIN_ERROR;
    }
}

__declspec(dllexport) void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps) {
    if (dps) {
        dps->PluginInterfaceVersionLow = 11;
        dps->PluginInterfaceVersionHi = 2;
        strcpy_s(dps->DefaultIniName, "tc_markdown.ini");
    }
}

__declspec(dllexport) int __stdcall ListLoadNext(HWND, HWND, char*, int) { return LISTPLUGIN_ERROR; }
__declspec(dllexport) int __stdcall ListLoadNextW(HWND, HWND, WCHAR*, int) { return LISTPLUGIN_ERROR; }
__declspec(dllexport) int __stdcall ListSearchDialog(HWND, int) { return LISTPLUGIN_ERROR; }
__declspec(dllexport) int __stdcall ListPrint(HWND, char*, char*, int, RECT*) { return LISTPLUGIN_ERROR; }
__declspec(dllexport) int __stdcall ListPrintW(HWND, WCHAR*, WCHAR*, int, RECT*) { return LISTPLUGIN_ERROR; }

__declspec(dllexport) int __stdcall ListNotificationReceived(HWND ListWin, int Message, WPARAM wParam, LPARAM lParam) {
    if (Message == WM_SIZE) {
        RECT rect;
        GetClientRect(GetParent(ListWin), &rect);
        SetWindowPos(ListWin, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
        auto it = g_views.find(ListWin);
        if (it != g_views.end() && it->second.controller) it->second.controller->put_Bounds(rect);
    } else if (Message == WM_KEYDOWN && wParam == VK_ESCAPE) {
        PostMessage(GetParent(ListWin), WM_CLOSE, 0, 0);
    }
    (void)lParam;
    return 0;
}

__declspec(dllexport) HBITMAP __stdcall ListGetPreviewBitmap(char*, int, int, char*, int) { return nullptr; }
__declspec(dllexport) HBITMAP __stdcall ListGetPreviewBitmapW(WCHAR*, int, int, char*, int) { return nullptr; }

}  // extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInstance = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}
