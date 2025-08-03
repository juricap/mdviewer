#include <windows.h>
#include <string>
#include <fstream>
#include <sstream>
#include <memory>
#include <algorithm>
#include <map>
#include <chrono>
#include <iomanip>
#include <shlobj.h>
#include <wrl.h>
#include <comdef.h>
#include "WebView2.h"
#include "include/maddy/parser.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

// Debug logging function (only active when DEBUG_LOG is defined)
void DebugLog(const std::string& message) {
#ifdef DEBUG_LOG
    try {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string logPath = std::string(tempPath) + "tc_markdown_lister.log";
        
        std::ofstream logFile(logPath, std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            auto tm = *std::localtime(&time_t);
            
            logFile << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " - " << message << std::endl;
            logFile.close();
        }
    } catch (...) {
        // Ignore logging errors
    }
#endif
}

// Prepare WebView2 user data folder
void PrepareWebView2UserData() {
    char appdata[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appdata))) {
        std::string ud = std::string(appdata) + "\\TCMarkdownViewer";
        CreateDirectoryA(ud.c_str(), NULL);
        SetEnvironmentVariableA("WEBVIEW2_USER_DATA_FOLDER", ud.c_str());
        DebugLog("WebView2 user data folder set to: " + ud);
    } else {
        DebugLog("Failed to get LOCALAPPDATA path for WebView2");
    }
}

// Detect if data contains UTF-8 BOM or is valid UTF-8
bool IsUtf8(const std::string& data) {
    // Check for UTF-8 BOM
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && 
        (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
        return true;
    }
    
    // Simple UTF-8 validation heuristic
    const unsigned char* bytes = (const unsigned char*)data.c_str();
    size_t len = data.length();
    
    for (size_t i = 0; i < len; ) {
        if (bytes[i] < 0x80) {
            i++; // ASCII
        } else if ((bytes[i] & 0xE0) == 0xC0) {
            if (i + 1 >= len || (bytes[i + 1] & 0xC0) != 0x80) return false;
            i += 2; // 2-byte UTF-8
        } else if ((bytes[i] & 0xF0) == 0xE0) {
            if (i + 2 >= len || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80) return false;
            i += 3; // 3-byte UTF-8
        } else if ((bytes[i] & 0xF8) == 0xF0) {
            if (i + 3 >= len || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80 || (bytes[i + 3] & 0xC0) != 0x80) return false;
            i += 4; // 4-byte UTF-8
        } else {
            return false; // Invalid UTF-8
        }
    }
    return true;
}

// Convert ANSI (system codepage) to UTF-8
std::string AnsiToUtf8(const std::string& ansi) {
    if (ansi.empty()) return std::string();
    
    // Convert ANSI to wide string using system codepage
    int size_needed = MultiByteToWideChar(CP_ACP, 0, &ansi[0], (int)ansi.size(), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_ACP, 0, &ansi[0], (int)ansi.size(), &wstr[0], size_needed);
    
    // Convert wide string to UTF-8
    size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string utf8(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &utf8[0], size_needed, NULL, NULL);
    
    return utf8;
}

// Read file and convert to UTF-8, supporting both UTF-8 and ANSI encodings
std::string ReadAllUtf8(const std::wstring& wpath) {
    FILE* f = _wfopen(wpath.c_str(), L"rb");
    if (!f) {
        DebugLog("ReadAllUtf8: Failed to open file");
        return {};
    }
    
    std::string data;
    char buf[1 << 15];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        data.append(buf, n);
    }
    fclose(f);
    
    DebugLog("ReadAllUtf8: Read " + std::to_string(data.length()) + " bytes");
    
    // Check if the data is already UTF-8
    if (IsUtf8(data)) {
        // Remove UTF-8 BOM if present
        if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && 
            (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
            data = data.substr(3);
            DebugLog("ReadAllUtf8: Removed UTF-8 BOM");
        }
        DebugLog("ReadAllUtf8: File detected as UTF-8");
        return data;
    } else {
        // Assume ANSI and convert to UTF-8
        DebugLog("ReadAllUtf8: File detected as ANSI, converting to UTF-8");
        return AnsiToUtf8(data);
    }
}

// Convert UTF-8 string to UTF-16 wide string
std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &utf8[0], (int)utf8.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &utf8[0], (int)utf8.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// WLX Plugin constants (from listplug.h)
#define lc_copy   1
#define lc_newparams 2
#define lc_selectall 3
#define lc_setpercent 4

#define lcp_wraptext 1
#define lcp_fittowindow 2
#define lcp_ansi   4
#define lcp_ascii   8
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

// Global variables
HINSTANCE g_hInstance = nullptr;

// WebView2 structure
struct View2 {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
};

// Store WebView2 instances for cleanup
static std::map<HWND, View2> g_views;

// HTML template for the markdown preview
const char* HTML_TEMPLATE = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <style>
        body { 
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            padding: 20px;
            max-width: 900px;
            margin: 0 auto;
            line-height: 1.6;
            color: #333;
            background: white;
        }
        h1, h2, h3, h4, h5, h6 { 
            margin-top: 24px;
            margin-bottom: 16px;
            font-weight: 600;
        }
        h1 { font-size: 2em; border-bottom: 1px solid #eaecef; padding-bottom: .3em; }
        h2 { font-size: 1.5em; border-bottom: 1px solid #eaecef; padding-bottom: .3em; }
        h3 { font-size: 1.25em; }
        code {
            background-color: rgba(27,31,35,.05);
            padding: .2em .4em;
            margin: 0;
            font-size: 85%;
            border-radius: 3px;
            font-family: Consolas, 'Courier New', monospace;
        }
        pre {
            background-color: #f6f8fa;
            padding: 16px;
            overflow: auto;
            font-size: 85%;
            line-height: 1.45;
            border-radius: 3px;
        }
        pre code {
            background-color: transparent;
            padding: 0;
        }
        blockquote {
            margin: 0;
            padding: 0 1em;
            color: #6a737d;
            border-left: .25em solid #dfe2e5;
        }
        table {
            border-spacing: 0;
            border-collapse: collapse;
            margin-top: 0;
            margin-bottom: 16px;
        }
        table th, table td {
            padding: 6px 13px;
            border: 1px solid #dfe2e5;
        }
        table tr:nth-child(2n) {
            background-color: #f6f8fa;
        }
        a {
            color: #0366d6;
            text-decoration: none;
        }
        a:hover {
            text-decoration: underline;
        }
        img {
            max-width: 100%;
            height: auto;
        }
        body.dark {
            background: #0d1117;
            color: #f0f6fc;
        }
        .dark h1, .dark h2 {
            border-bottom-color: #30363d;
        }
        .dark code {
            background-color: rgba(110,118,129,.4);
        }
        .dark pre {
            background-color: #161b22;
        }
        .dark table th, .dark table td {
            border-color: #30363d;
        }
        .dark table tr:nth-child(2n) {
            background-color: #161b22;
        }
        .dark blockquote {
            color: #8b949e;
            border-left-color: #30363d;
        }
        .dark a {
            color: #58a6ff;
        }
    </style>
</head>
<body>
%CONTENT%
</body>
</html>
)";

// Convert markdown file to HTML (wide string version)
std::string ConvertMarkdownToHtml(const std::wstring& wfilePath) {
    try {
        // Convert wide path to UTF-8 for logging
        int len = WideCharToMultiByte(CP_UTF8, 0, wfilePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string logPath(len - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wfilePath.c_str(), -1, &logPath[0], len, nullptr, nullptr);
        
        DebugLog("ConvertMarkdownToHtml: Opening file: " + logPath);
        
        std::string mdContent = ReadAllUtf8(wfilePath);
        if (mdContent.empty()) {
            DebugLog("ConvertMarkdownToHtml: Failed to read file or file is empty");
            return "<html><body><p>Error: Could not open file or file is empty</p></body></html>";
        }
        
        DebugLog("ConvertMarkdownToHtml: Read " + std::to_string(mdContent.length()) + " bytes");
        
        // Parse Markdown to HTML
        std::shared_ptr<maddy::ParserConfig> config = std::make_shared<maddy::ParserConfig>();
        std::stringstream mdStream(mdContent);
        maddy::Parser parser(config);
        std::string html = parser.Parse(mdStream);
        
        DebugLog("ConvertMarkdownToHtml: Parsed HTML length: " + std::to_string(html.length()));
        
        // Replace placeholder in template
        std::string result = HTML_TEMPLATE;
        size_t pos = result.find("%CONTENT%");
        if (pos != std::string::npos) {
            result.replace(pos, 9, html);
        }
        
        DebugLog("ConvertMarkdownToHtml: Final HTML length: " + std::to_string(result.length()));
        
        return result;
    } catch (const std::exception& e) {
        DebugLog("ConvertMarkdownToHtml: Exception: " + std::string(e.what()));
        return "<html><body><p>Error: Failed to parse markdown</p></body></html>";
    } catch (...) {
        DebugLog("ConvertMarkdownToHtml: Unknown exception");
        return "<html><body><p>Error: Failed to parse markdown</p></body></html>";
    }
}

// Convert markdown file to HTML (narrow string version - for backward compatibility)
std::string ConvertMarkdownToHtml(const char* filePath) {
    // Convert narrow path to wide string and call the wide version
    int len = MultiByteToWideChar(CP_UTF8, 0, filePath, -1, nullptr, 0);
    if (len <= 0) {
        DebugLog("ConvertMarkdownToHtml: Failed to convert narrow path to wide");
        return "<html><body><p>Error: Invalid file path</p></body></html>";
    }
    
    std::wstring wfilePath(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, filePath, -1, &wfilePath[0], len);
    
    return ConvertMarkdownToHtml(wfilePath);
}

// Check if file is a markdown file
bool IsMarkdownFile(const char* filename) {
    if (!filename) return false;
    
    std::string file(filename);
    std::transform(file.begin(), file.end(), file.begin(), ::tolower);
    
    // Manual string ending check for C++17 compatibility
    return (file.length() >= 3 && file.substr(file.length() - 3) == ".md") ||
           (file.length() >= 9 && file.substr(file.length() - 9) == ".markdown") ||
           (file.length() >= 6 && file.substr(file.length() - 6) == ".mdown") ||
           (file.length() >= 4 && file.substr(file.length() - 4) == ".mkd") ||
           (file.length() >= 5 && file.substr(file.length() - 5) == ".mkdn");
}

// Required WLX API functions - remove declarations since they're in the header
extern "C" {

__declspec(dllexport) HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
    DebugLog("ListLoad called with file: " + std::string(FileToLoad ? FileToLoad : "NULL"));
    DebugLog("ListLoad ShowFlags: " + std::to_string(ShowFlags) + " (lcp_forceshow=" + std::to_string(lcp_forceshow) + ")");
    
    // Check if force show is enabled (user explicitly chose plugin)
    if (!(ShowFlags & lcp_forceshow)) {
        // Normal mode - check if this is a markdown file
        bool isMarkdown = IsMarkdownFile(FileToLoad);
        DebugLog("IsMarkdownFile result: " + std::string(isMarkdown ? "true" : "false"));
        if (!isMarkdown) {
            DebugLog("ListLoad returning NULL - not a markdown file");
            return nullptr;
        }
    } else {
        DebugLog("Force show enabled - attempting to load file regardless");
    }
    
    // Initialize COM
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    
    // Convert markdown to HTML
    std::string html = ConvertMarkdownToHtml(FileToLoad);
    
    try {
        // Get parent window dimensions
        RECT parentRect;
        GetClientRect(ParentWin, &parentRect);
        int width = parentRect.right - parentRect.left;
        int height = parentRect.bottom - parentRect.top;
        
        DebugLog("Parent window dimensions: " + std::to_string(width) + "x" + std::to_string(height));
        
        // Window procedure for webview host
        static auto WebViewWndProc = [](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
            auto it = g_views.find(hWnd);
            
            switch (message) {
                case WM_SIZE: {
                    // Handle resize for WebView2
                    if (it != g_views.end() && it->second.controller) {
                        RECT rc{};
                        GetClientRect(hWnd, &rc);
                        it->second.controller->put_Bounds(rc);
                        DebugLog("Resized WebView2 to: " + std::to_string(rc.right - rc.left) + "x" + std::to_string(rc.bottom - rc.top));
                    }
                    break;
                }
                case WM_KEYDOWN: {
                    // Handle ESC key to close the lister window
                    if (wParam == VK_ESCAPE) {
                        DebugLog("ESC key pressed - closing lister window");
                        // Send close message to parent lister window
                        HWND parent = GetParent(hWnd);
                        if (parent) {
                            PostMessage(parent, WM_COMMAND, MAKEWPARAM(0, 0), 0);
                        }
                        return 0;
                    }
                    break;
                }
                case WM_DESTROY: {
                    // Cleanup is handled in ListCloseWindow
                    break;
                }
            }
            return DefWindowProc(hWnd, message, wParam, lParam);
        };
        
        // Register window class for our webview host
        static bool classRegistered = false;
        if (!classRegistered) {
            WNDCLASSEX wc = {0};
            wc.cbSize = sizeof(WNDCLASSEX);
            wc.lpfnWndProc = WebViewWndProc;
            wc.hInstance = g_hInstance;
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wc.lpszClassName = "TCMarkdownViewer";
            
            if (RegisterClassEx(&wc)) {
                classRegistered = true;
            }
        }
        
        // Create a child window to host the webview
        HWND hwnd = CreateWindowEx(
            0,
            "TCMarkdownViewer",
            "",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            0, 0, width, height,
            ParentWin,
            nullptr,
            g_hInstance,
            nullptr
        );
        
        if (!hwnd) {
            DebugLog("Failed to create child window: " + std::to_string(GetLastError()));
            return nullptr;
        }
        
        DebugLog("Child window created successfully");
        
        // Prepare WebView2 user data folder before creating webview
        PrepareWebView2UserData();
        
        DebugLog("Creating WebView2 directly");
        
        // Capture HTML content for the lambda
        std::string htmlCopy = html;
        
        // Create WebView2 environment and controller asynchronously
        CreateCoreWebView2EnvironmentWithOptions(
            nullptr, // browser exe
            _wgetenv(L"WEBVIEW2_USER_DATA_FOLDER"), // user data folder set by PrepareWebView2UserData
            nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [hwnd, htmlCopy](HRESULT envHr, ICoreWebView2Environment* env) -> HRESULT {
                    if (FAILED(envHr) || !env) {
                        DebugLog("Failed to create WebView2 environment: " + std::to_string(envHr));
                        return S_OK;
                    }
                    
                    DebugLog("WebView2 environment created successfully");
                    
                    // Create controller bound to our child window
                    env->CreateCoreWebView2Controller(
                        hwnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [hwnd, htmlCopy](HRESULT ctlHr, ICoreWebView2Controller* ctl) -> HRESULT {
                                if (FAILED(ctlHr) || !ctl) {
                                    DebugLog("Failed to create WebView2 controller: " + std::to_string(ctlHr));
                                    return S_OK;
                                }
                                
                                DebugLog("WebView2 controller created successfully");
                                
                                g_views[hwnd].controller = ctl;
                                ctl->get_CoreWebView2(&g_views[hwnd].webview);
                                
                                // Size it to our client rect
                                RECT rc{};
                                GetClientRect(hwnd, &rc);
                                ctl->put_Bounds(rc);
                                
                                // Enable default context menus/zoom on the webview
                                if (g_views[hwnd].webview) {
                                    ComPtr<ICoreWebView2Settings> settings;
                                    g_views[hwnd].webview->get_Settings(&settings);
                                    if (settings) {
                                        settings->put_IsZoomControlEnabled(TRUE);
                                        settings->put_AreDefaultContextMenusEnabled(TRUE);
                                    }
                                }
                                
                                // Feed HTML directly
                                if (g_views[hwnd].webview) {
                                    std::wstring whtml = Utf8ToWide(htmlCopy);
                                    g_views[hwnd].webview->NavigateToString(whtml.c_str());
                                    DebugLog("WebView2 HTML content set, length: " + std::to_string(htmlCopy.length()));
                                    
                                    // Add JavaScript to handle ESC key
                                    std::wstring escScript = LR"(
                                        document.addEventListener('keydown', function(event) {
                                            if (event.key === 'Escape') {
                                                console.log('ESC key pressed in WebView2');
                                                window.chrome.webview.postMessage('ESC_PRESSED');
                                            }
                                        });
                                    )";
                                    
                                    // Add message handler for ESC key
                                    g_views[hwnd].webview->add_WebMessageReceived(
                                        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                            [hwnd](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                                LPWSTR message;
                                                args->TryGetWebMessageAsString(&message);
                                                if (wcscmp(message, L"ESC_PRESSED") == 0) {
                                                    DebugLog("ESC key received from WebView2 - closing lister");
                                                    // Send close message to Total Commander
                                                    HWND parent = GetParent(hwnd);
                                                    if (parent) {
                                                        PostMessage(parent, WM_CLOSE, 0, 0);
                                                    }
                                                }
                                                CoTaskMemFree(message);
                                                return S_OK;
                                            }
                                        ).Get(), nullptr);
                                    
                                    // Execute the ESC handler script
                                    g_views[hwnd].webview->ExecuteScript(escScript.c_str(), nullptr);
                                }
                                
                                // Set focus to the WebView2 control for immediate keyboard navigation
                                ctl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                                DebugLog("WebView2 focus set for keyboard navigation");
                                
                                return S_OK;
                            }
                        ).Get()
                    );
                    return S_OK;
                }
            ).Get()
        );
        
        DebugLog("ListLoad completed successfully, returning window handle");
        return hwnd;
    } catch (const std::exception& e) {
        DebugLog("Exception in ListLoad: " + std::string(e.what()));
        return nullptr;
    } catch (...) {
        DebugLog("Unknown exception in ListLoad");
        return nullptr;
    }
}

__declspec(dllexport) HWND __stdcall ListLoadW(HWND ParentWin, WCHAR* FileToLoad, int ShowFlags) {
    // Convert wide path to UTF-8 for logging
    int len = WideCharToMultiByte(CP_UTF8, 0, FileToLoad, -1, nullptr, 0, nullptr, nullptr);
    std::string logPath;
    if (len > 0) {
        logPath.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, FileToLoad, -1, &logPath[0], len, nullptr, nullptr);
    }
    
    DebugLog("ListLoadW called with file: " + logPath);
    DebugLog("ListLoadW ShowFlags: " + std::to_string(ShowFlags) + " (lcp_forceshow=" + std::to_string(lcp_forceshow) + ")");
    
    // Check if force show is enabled (user explicitly chose plugin)
    if (!(ShowFlags & lcp_forceshow)) {
        // Normal mode - check if this is a markdown file
        bool isMarkdown = IsMarkdownFile(logPath.c_str());
        DebugLog("IsMarkdownFile result: " + std::string(isMarkdown ? "true" : "false"));
        if (!isMarkdown) {
            DebugLog("ListLoadW returning NULL - not a markdown file");
            return nullptr;
        }
    } else {
        DebugLog("Force show enabled - attempting to load file regardless");
    }
    
    // Initialize COM
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    
    // Convert markdown to HTML using wide string version
    std::wstring wfilePath(FileToLoad);
    std::string html = ConvertMarkdownToHtml(wfilePath);
    
    try {
        // Get parent window dimensions
        RECT parentRect;
        GetClientRect(ParentWin, &parentRect);
        int width = parentRect.right - parentRect.left;
        int height = parentRect.bottom - parentRect.top;
        
        DebugLog("Parent window dimensions: " + std::to_string(width) + "x" + std::to_string(height));
        
        // Window procedure for webview host
        static auto WebViewWndProc = [](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
            auto it = g_views.find(hWnd);
            
            switch (message) {
                case WM_SIZE: {
                    // Handle resize for WebView2
                    if (it != g_views.end() && it->second.controller) {
                        RECT rc{};
                        GetClientRect(hWnd, &rc);
                        it->second.controller->put_Bounds(rc);
                        DebugLog("Resized WebView2 to: " + std::to_string(rc.right - rc.left) + "x" + std::to_string(rc.bottom - rc.top));
                    }
                    break;
                }
                case WM_KEYDOWN: {
                    // Handle ESC key to close the lister window
                    if (wParam == VK_ESCAPE) {
                        DebugLog("ESC key pressed - closing lister window");
                        // Send close message to parent lister window
                        HWND parent = GetParent(hWnd);
                        if (parent) {
                            PostMessage(parent, WM_COMMAND, MAKEWPARAM(0, 0), 0);
                        }
                        return 0;
                    }
                    break;
                }
                case WM_DESTROY: {
                    // Cleanup is handled in ListCloseWindow
                    break;
                }
            }
            return DefWindowProc(hWnd, message, wParam, lParam);
        };
        
        // Register window class for our webview host
        static bool classRegistered = false;
        if (!classRegistered) {
            WNDCLASSEX wc = {0};
            wc.cbSize = sizeof(WNDCLASSEX);
            wc.lpfnWndProc = WebViewWndProc;
            wc.hInstance = g_hInstance;
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wc.lpszClassName = "TCMarkdownViewer";
            
            if (RegisterClassEx(&wc)) {
                classRegistered = true;
            }
        }
        
        // Create a child window to host the webview
        HWND hwnd = CreateWindowEx(
            0,
            "TCMarkdownViewer",
            "",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            0, 0, width, height,
            ParentWin,
            nullptr,
            g_hInstance,
            nullptr
        );
        
        if (!hwnd) {
            DebugLog("Failed to create child window: " + std::to_string(GetLastError()));
            return nullptr;
        }
        
        DebugLog("Child window created successfully");
        
        // Prepare WebView2 user data folder before creating webview
        PrepareWebView2UserData();
        
        DebugLog("Creating WebView2 directly");
        
        // Capture HTML content for the lambda
        std::string htmlCopy = html;
        
        // Create WebView2 environment and controller asynchronously
        CreateCoreWebView2EnvironmentWithOptions(
            nullptr, // browser exe
            _wgetenv(L"WEBVIEW2_USER_DATA_FOLDER"), // user data folder set by PrepareWebView2UserData
            nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [hwnd, htmlCopy](HRESULT envHr, ICoreWebView2Environment* env) -> HRESULT {
                    if (FAILED(envHr) || !env) {
                        DebugLog("Failed to create WebView2 environment: " + std::to_string(envHr));
                        return S_OK;
                    }
                    
                    DebugLog("WebView2 environment created successfully");
                    
                    // Create controller bound to our child window
                    env->CreateCoreWebView2Controller(
                        hwnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [hwnd, htmlCopy](HRESULT ctlHr, ICoreWebView2Controller* ctl) -> HRESULT {
                                if (FAILED(ctlHr) || !ctl) {
                                    DebugLog("Failed to create WebView2 controller: " + std::to_string(ctlHr));
                                    return S_OK;
                                }
                                
                                DebugLog("WebView2 controller created successfully");
                                
                                g_views[hwnd].controller = ctl;
                                ctl->get_CoreWebView2(&g_views[hwnd].webview);
                                
                                // Size it to our client rect
                                RECT rc{};
                                GetClientRect(hwnd, &rc);
                                ctl->put_Bounds(rc);
                                
                                // Enable default context menus/zoom on the webview
                                if (g_views[hwnd].webview) {
                                    ComPtr<ICoreWebView2Settings> settings;
                                    g_views[hwnd].webview->get_Settings(&settings);
                                    if (settings) {
                                        settings->put_IsZoomControlEnabled(TRUE);
                                        settings->put_AreDefaultContextMenusEnabled(TRUE);
                                    }
                                }
                                
                                // Feed HTML directly
                                if (g_views[hwnd].webview) {
                                    std::wstring whtml = Utf8ToWide(htmlCopy);
                                    g_views[hwnd].webview->NavigateToString(whtml.c_str());
                                    DebugLog("WebView2 HTML content set, length: " + std::to_string(htmlCopy.length()));
                                    
                                    // Add JavaScript to handle ESC key
                                    std::wstring escScript = LR"(
                                        document.addEventListener('keydown', function(event) {
                                            if (event.key === 'Escape') {
                                                console.log('ESC key pressed in WebView2');
                                                window.chrome.webview.postMessage('ESC_PRESSED');
                                            }
                                        });
                                    )";
                                    
                                    // Add message handler for ESC key
                                    g_views[hwnd].webview->add_WebMessageReceived(
                                        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                            [hwnd](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                                LPWSTR message;
                                                args->TryGetWebMessageAsString(&message);
                                                if (wcscmp(message, L"ESC_PRESSED") == 0) {
                                                    DebugLog("ESC key received from WebView2 - closing lister");
                                                    // Send close message to Total Commander
                                                    HWND parent = GetParent(hwnd);
                                                    if (parent) {
                                                        PostMessage(parent, WM_CLOSE, 0, 0);
                                                    }
                                                }
                                                CoTaskMemFree(message);
                                                return S_OK;
                                            }
                                        ).Get(), nullptr);
                                    
                                    // Execute the ESC handler script
                                    g_views[hwnd].webview->ExecuteScript(escScript.c_str(), nullptr);
                                }
                                
                                // Set focus to the WebView2 control for immediate keyboard navigation
                                ctl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                                DebugLog("WebView2 focus set for keyboard navigation");
                                
                                return S_OK;
                            }
                        ).Get()
                    );
                    return S_OK;
                }
            ).Get()
        );
        
        DebugLog("ListLoadW completed successfully, returning window handle");
        return hwnd;
    } catch (const std::exception& e) {
        DebugLog("Exception in ListLoadW: " + std::string(e.what()));
        return nullptr;
    } catch (...) {
        DebugLog("Unknown exception in ListLoadW");
        return nullptr;
    }
}

__declspec(dllexport) void __stdcall ListCloseWindow(HWND ListWin) {
    if (ListWin && IsWindow(ListWin)) {
        // Clean up WebView2 instance
        auto it = g_views.find(ListWin);
        if (it != g_views.end()) {
            if (it->second.controller) {
                it->second.controller->Close();
            }
            g_views.erase(it);
        }
        DestroyWindow(ListWin);
    }
}

// Simple string copy function like the official example
char* strlcpy(char* p, const char* p2, int maxlen) {
    if ((int)strlen(p2) >= maxlen) {
        strncpy(p, p2, maxlen - 1);
        p[maxlen - 1] = 0;
    } else {
        strcpy(p, p2);
    }
    return p;
}

__declspec(dllexport) void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    DebugLog("ListGetDetectString called with maxlen=" + std::to_string(maxlen));
    
    // Use MULTIMEDIA to override internal viewers and support multiple markdown extensions
    const char* detectStr = "MULTIMEDIA & (EXT=\"MD\" | EXT=\"MARKDOWN\" | EXT=\"MDOWN\" | EXT=\"MKD\" | EXT=\"MKDN\")";
    strlcpy(DetectString, detectStr, maxlen);
    
    DebugLog("ListGetDetectString returning: " + std::string(DetectString));
}

__declspec(dllexport) int __stdcall ListSearchText(HWND ListWin, char* SearchString, int SearchParameter) {
    // Basic search implementation
    if (!ListWin || !SearchString) return LISTPLUGIN_ERROR;
    
    // Get text from the control
    int textLen = GetWindowTextLengthA(ListWin);
    if (textLen <= 0) return LISTPLUGIN_ERROR;
    
    std::string text(textLen + 1, 0);
    GetWindowTextA(ListWin, &text[0], textLen + 1);
    
    // Simple case-insensitive search
    std::string searchStr(SearchString);
    if (!(SearchParameter & lcs_matchcase)) {
        std::transform(text.begin(), text.end(), text.begin(), ::tolower);
        std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
    }
    
    size_t pos = text.find(searchStr);
    if (pos != std::string::npos) {
        // Select found text
        SendMessage(ListWin, EM_SETSEL, pos, pos + searchStr.length());
        return LISTPLUGIN_OK;
    }
    
    return LISTPLUGIN_ERROR;
}

__declspec(dllexport) int __stdcall ListSearchTextW(HWND ListWin, WCHAR* SearchString, int SearchParameter) {
    // Convert wide string to narrow string
    int len = WideCharToMultiByte(CP_UTF8, 0, SearchString, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return LISTPLUGIN_ERROR;
    
    std::string narrowSearch(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, SearchString, -1, &narrowSearch[0], len, nullptr, nullptr);
    
    return ListSearchText(ListWin, &narrowSearch[0], SearchParameter);
}

__declspec(dllexport) int __stdcall ListSendCommand(HWND ListWin, int Command, int Parameter) {
    switch (Command) {
        case lc_copy:
            SendMessage(ListWin, WM_COPY, 0, 0);
            return LISTPLUGIN_OK;
        case lc_selectall:
            SendMessage(ListWin, EM_SETSEL, 0, -1);
            return LISTPLUGIN_OK;
        default:
            return LISTPLUGIN_ERROR;
    }
}

__declspec(dllexport) void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps) {
    DebugLog("ListSetDefaultParams called");
    
    // Initialize default parameters
    if (dps) {
        DebugLog("ListSetDefaultParams: size=" + std::to_string(dps->size));
        DebugLog("ListSetDefaultParams: VersionHi=" + std::to_string(dps->PluginInterfaceVersionHi));
        DebugLog("ListSetDefaultParams: VersionLow=" + std::to_string(dps->PluginInterfaceVersionLow));
        DebugLog("ListSetDefaultParams: DefaultIniName=" + std::string(dps->DefaultIniName));
        
        dps->PluginInterfaceVersionLow = 11;
        dps->PluginInterfaceVersionHi = 2;
        strcpy_s(dps->DefaultIniName, "tc_markdown.ini");
    } else {
        DebugLog("ListSetDefaultParams: dps is NULL");
    }
}

// Optional functions - return error for now
__declspec(dllexport) int __stdcall ListLoadNext(HWND ParentWin, HWND PluginWin, char* FileToLoad, int ShowFlags) {
    return LISTPLUGIN_ERROR;
}

__declspec(dllexport) int __stdcall ListLoadNextW(HWND ParentWin, HWND PluginWin, WCHAR* FileToLoad, int ShowFlags) {
    return LISTPLUGIN_ERROR;
}

__declspec(dllexport) int __stdcall ListSearchDialog(HWND ListWin, int FindNext) {
    return LISTPLUGIN_ERROR;
}

__declspec(dllexport) int __stdcall ListPrint(HWND ListWin, char* FileToPrint, char* DefPrinter, int PrintFlags, RECT* Margins) {
    return LISTPLUGIN_ERROR;
}

__declspec(dllexport) int __stdcall ListPrintW(HWND ListWin, WCHAR* FileToPrint, WCHAR* DefPrinter, int PrintFlags, RECT* Margins) {
    return LISTPLUGIN_ERROR;
}

__declspec(dllexport) int __stdcall ListNotificationReceived(HWND ListWin, int Message, WPARAM wParam, LPARAM lParam) {
    switch (Message) {
        case WM_SIZE: {
            // Resize the window to fit the parent
            RECT rect;
            GetClientRect(GetParent(ListWin), &rect);
            SetWindowPos(ListWin, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
            DebugLog("ListNotificationReceived: Window resized to: " + std::to_string(rect.right - rect.left) + "x" + std::to_string(rect.bottom - rect.top));
            
            // Also update the WebView2 size
            auto it = g_views.find(ListWin);
            if (it != g_views.end() && it->second.controller) {
                it->second.controller->put_Bounds(rect);
            }
            
            return 0;
        }
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_KEYDOWN:
        case WM_KEYUP: {
            // Handle ESC key to close the lister window
            if (Message == WM_KEYDOWN && wParam == VK_ESCAPE) {
                DebugLog("ESC key pressed in ListNotificationReceived - closing lister");
                // Post WM_CLOSE to the lister window to close it
                PostMessage(GetParent(ListWin), WM_CLOSE, 0, 0);
                return 0;
            }
            
            // Forward other input messages to the webview window
            SendMessage(ListWin, Message, wParam, lParam);
            
            // WebView2 handles input automatically - no manual dispatch needed
            
            return 0;
        }
        default:
            break;
    }
    return 0;
}

__declspec(dllexport) HBITMAP __stdcall ListGetPreviewBitmap(char* FileToLoad, int width, int height, char* contentbuf, int contentbuflen) {
    return nullptr;
}

__declspec(dllexport) HBITMAP __stdcall ListGetPreviewBitmapW(WCHAR* FileToLoad, int width, int height, char* contentbuf, int contentbuflen) {
    return nullptr;
}

} // extern "C"

// DLL Entry Point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            g_hInstance = hModule;
            DisableThreadLibraryCalls(hModule);
            DebugLog("DLL_PROCESS_ATTACH - Plugin loaded");
            break;
        case DLL_PROCESS_DETACH:
            DebugLog("DLL_PROCESS_DETACH - Plugin unloaded");
            break;
    }
    return TRUE;
}