# WebView2 Integration in Plugin Host Applications

## Developer Notes for Embedding WebView2 in Child Windows/Plugins

Based on lessons learned from implementing a Total Commander lister plugin with WebView2, here are essential patterns and solutions for integrating WebView2 as a child control in plugin architectures.

## Core Problem: Message Loop Integration

**❌ NEVER use webview wrapper libraries that require `.run()`**

Most WebView2 wrapper libraries (like the simple `webview.h` wrapper) expect to control the main message loop by calling `.run()`. In plugin environments where the host application owns the message loop, this creates a deadlock - the wrapper waits for `.run()` to be called, but the plugin cannot block the host's thread.

**✅ Always use WebView2 directly via COM interfaces**

```cpp
#include <wrl.h>
#include <comdef.h>
#include "WebView2.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

struct WebView2Instance {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
};
```

## Essential Setup Steps

### 1. COM Initialization
```cpp
// Call this in your plugin load function, NOT in DllMain
CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
```

### 2. User Data Folder Setup
**Critical:** WebView2 requires a writable user data folder. Default locations often fail in plugin contexts.

```cpp
void PrepareWebView2UserData() {
    char appdata[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appdata))) {
        std::string ud = std::string(appdata) + "\\YourPluginName";
        CreateDirectoryA(ud.c_str(), NULL);
        SetEnvironmentVariableA("WEBVIEW2_USER_DATA_FOLDER", ud.c_str());
    }
}
```

### 3. Async Creation Pattern
WebView2 creation is asynchronous. Never assume synchronous completion.

```cpp
// Create environment first
CreateCoreWebView2EnvironmentWithOptions(
    nullptr, // browser exe path
    _wgetenv(L"WEBVIEW2_USER_DATA_FOLDER"),
    nullptr,
    Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [hwnd, htmlContent](HRESULT envHr, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(envHr) || !env) return S_OK;
            
            // Then create controller
            env->CreateCoreWebView2Controller(
                hwnd,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [hwnd, htmlContent](HRESULT ctlHr, ICoreWebView2Controller* ctl) -> HRESULT {
                        if (FAILED(ctlHr) || !ctl) return S_OK;
                        
                        // Store references and configure
                        g_instances[hwnd].controller = ctl;
                        ctl->get_CoreWebView2(&g_instances[hwnd].webview);
                        
                        // Size to parent
                        RECT rc{};
                        GetClientRect(hwnd, &rc);
                        ctl->put_Bounds(rc);
                        
                        // Load content
                        if (g_instances[hwnd].webview) {
                            std::wstring whtml(htmlContent.begin(), htmlContent.end());
                            g_instances[hwnd].webview->NavigateToString(whtml.c_str());
                        }
                        
                        return S_OK;
                    }
                ).Get()
            );
            return S_OK;
        }
    ).Get()
);
```

## Window Management

### Child Window Creation
```cpp
// Create a child window to host WebView2
HWND hwnd = CreateWindowEx(
    0,
    "YourWindowClass",
    "",
    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, // WS_CLIPCHILDREN is important
    0, 0, width, height,
    parentWindow,
    nullptr,
    hInstance,
    nullptr
);
```

### Window Procedure
```cpp
LRESULT CALLBACK WebViewWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto it = g_instances.find(hWnd);
    
    switch (message) {
        case WM_SIZE: {
            // Resize WebView2 to match window
            if (it != g_instances.end() && it->second.controller) {
                RECT rc{};
                GetClientRect(hWnd, &rc);
                it->second.controller->put_Bounds(rc);
            }
            break;
        }
        case WM_DESTROY: {
            // Cleanup handled elsewhere
            break;
        }
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}
```

## Focus and Keyboard Handling

### Initial Focus
WebView2 doesn't automatically receive keyboard focus. Set it explicitly:

```cpp
// After controller creation
controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
```

### Settings Configuration
Access settings through the webview, not the controller:

```cpp
if (webview) {
    ComPtr<ICoreWebView2Settings> settings;
    webview->get_Settings(&settings);
    if (settings) {
        settings->put_IsZoomControlEnabled(TRUE);
        settings->put_AreDefaultContextMenusEnabled(TRUE);
    }
}
```

## Key Event Handling

### Problem: Host Application Key Interception
Host applications often intercept special keys (like ESC) before they reach your plugin. Standard Windows message handling may not work.

### Solution: JavaScript-to-Native Communication
```cpp
// 1. Set up message handler
webview->add_WebMessageReceived(
    Callback<ICoreWebView2WebMessageReceivedEventHandler>(
        [hwnd](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
            LPWSTR message;
            args->TryGetWebMessageAsString(&message);
            
            if (wcscmp(message, L"ESC_PRESSED") == 0) {
                // Handle ESC key - close parent window, etc.
                HWND parent = GetParent(hwnd);
                if (parent) {
                    PostMessage(parent, WM_CLOSE, 0, 0);
                }
            }
            
            CoTaskMemFree(message);
            return S_OK;
        }
    ).Get(), nullptr);

// 2. Inject JavaScript key handler
std::wstring keyScript = LR"(
    document.addEventListener('keydown', function(event) {
        if (event.key === 'Escape') {
            window.chrome.webview.postMessage('ESC_PRESSED');
        }
    });
)";

webview->ExecuteScript(keyScript.c_str(), nullptr);
```

## Content Loading

### Prefer NavigateToString() over file:// URLs
```cpp
// ✅ Good - Direct HTML content
std::wstring whtml(htmlContent.begin(), htmlContent.end());
webview->NavigateToString(whtml.c_str());

// ❌ Avoid - File URLs can have encoding/permission issues
webview->Navigate(L"file:///path/to/file.html");
```

## Cleanup and Resource Management

### Proper Shutdown Sequence
```cpp
void CleanupWebView(HWND hwnd) {
    auto it = g_instances.find(hwnd);
    if (it != g_instances.end()) {
        if (it->second.controller) {
            it->second.controller->Close();
        }
        g_instances.erase(it);
    }
}
```

## Common Pitfalls and Solutions

### 1. **Blank/White Screen**
- **Cause:** WebView2 runtime not installed or user data folder not writable
- **Solution:** Set up proper user data folder, check WebView2 runtime installation

### 2. **Plugin Immediately Unloads**
- **Cause:** Missing dependencies, improper function exports, or crashes during initialization
- **Solution:** Use extensive debug logging, check .def file exports

### 3. **Keyboard Navigation Doesn't Work**
- **Cause:** WebView2 doesn't have focus
- **Solution:** Call `MoveFocus()` after controller creation

### 4. **Special Keys Not Working (ESC, F-keys)**
- **Cause:** Host application intercepts these keys
- **Solution:** Use JavaScript event handlers with `postMessage()` communication

### 5. **Resize Issues**
- **Cause:** Not handling `WM_SIZE` messages properly
- **Solution:** Always call `controller->put_Bounds(rect)` on resize

## Build Configuration

### Required Headers
```cpp
#include <wrl.h>
#include <comdef.h>
#include "WebView2.h"
```

### Required Libraries
```
user32.lib kernel32.lib ole32.lib oleaut32.lib 
shell32.lib shlwapi.lib version.lib windowsapp.lib
WebView2LoaderStatic.lib
```

### WebView2 SDK Integration
Download and include WebView2 SDK in your build process:
```batch
REM Download WebView2 SDK automatically
if not exist "packages\Microsoft.Web.WebView2" (
    powershell -Command "Invoke-WebRequest -Uri 'https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2210.55' -OutFile 'webview2.zip'"
    powershell -Command "Expand-Archive -Path 'webview2.zip' -DestinationPath 'packages\Microsoft.Web.WebView2' -Force"
    del webview2.zip
)
```

## Testing and Debugging

### Debug Logging Strategy
```cpp
void DebugLog(const std::string& message) {
    std::string logPath = "F:\\your\\project\\debug\\debug.log";
    std::ofstream logFile(logPath, std::ios::app);
    if (logFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        logFile << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " - " << message << std::endl;
    }
}
```

### Key Debug Points
- DLL load/unload events
- Function call entries (ListLoad, etc.)
- WebView2 creation stages
- Focus and resize events
- Key event handling

## Performance Considerations

1. **Single Environment:** Reuse the WebView2 environment across multiple instances
2. **Memory Management:** Properly release COM references
3. **Async Handling:** Don't block on WebView2 operations
4. **Resource Cleanup:** Always call `controller->Close()` before destruction

## Security Notes

- WebView2 runs with the same privileges as your plugin
- Be careful with `ExecuteScript()` - validate any dynamic JavaScript
- User data folder should be plugin-specific to avoid conflicts
- Consider content security policies for web content

## Conclusion

WebView2 integration in plugin environments requires careful attention to asynchronous initialization, proper message loop integration, and robust error handling. The key is to work **with** the host application's message loop rather than trying to control it, and to handle the asynchronous nature of WebView2 creation properly.