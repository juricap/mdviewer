# Development

Back to [[mdviewer]].

## Build

Requirements: Visual Studio 2022 (or 2019) with the C++ x64 toolchain. The script downloads
the WebView2 SDK (NuGet 1.0.2210.55) into `packages/` and maddy into `include/maddy/` if missing.

```bat
build_plugin_fixed.bat          :: release -> tc_markdown_lister.wlx (+ copy .wlx64)
build_plugin_fixed.bat debug    :: also tc_markdown_lister_debug.wlx (logs to %TEMP%\tc_markdown_lister.log)
create_release.bat              :: packages release\tc-markdown-viewer-*.zip
```

The debug build is `/Od` and roughly 3–5× slower in maddy than release. Don't use it for timing.

## Install in Total Commander

Copy the `.wlx64` into the TC plugins folder, then Configuration → Options → Plugins → Lister →
Add. The detect string uses `MULTIMEDIA & (EXT="MD" | ...)` so it overrides TC's built-in viewer.

## Architecture (tc_markdown_lister.cpp)

```
TC UI thread                         worker thread (RenderThread)
------------                         ----------------------------
ListLoadW
  create host window (TCMarkdownViewer class)
  StartRender ------------------->   ReadFileUtf8 (UTF-8 / BOM / ANSI, capped 64 MB)
  CreateCoreWebView2Environment      EstimateMaddyCost -> maddy  OR  escaped <pre> plain text
  return hwnd (~20 ms)               write %LOCALAPPDATA%\TCMarkdownViewer\render\view_<pid>_<n>.html
                                     PostMessage(WM_APP_RENDER_DONE)
controller ready -> "Loading..." page
WM_APP_RENDER_DONE / controller ready (whichever is last)
  -> webview->Navigate(file:///...render file)
ListCloseWindow -> cancel job, close controller, delete render file
```

Key points:
- **Never parse on the TC UI thread.** TC freezes completely if `ListLoad` blocks.
- **Don't use `NavigateToString` for documents.** It has a hard 2 MB limit. The page is written
  to a temp file and loaded with `Navigate(file://)`, and a `<base href>` pointing at the
  .md file's folder makes relative images work.
- The worker thread holds a reference on the DLL (`GetModuleHandleEx` + `FreeLibraryAndExitThread`),
  so TC unloading the plugin mid-render can't crash. It has a 64 MB stack reservation because
  MSVC `std::regex` recurses deeply.
- WebView2 user data: `%LOCALAPPDATA%\TCMarkdownViewer`. Stale render files older than a day are
  removed on first load.
- Lister search (`ListSearchTextW`) uses `window.find()`. Copy and select-all use
  `document.execCommand`.
- Dark mode: TC's `lcp_darkmode` flag adds `class="dark"` to `<body>`.

## Testing without Total Commander

`tests/harness/` contains:
- `lister_host.cpp`: a fake TC lister. It loads a `.wlx`, calls `ListLoadW`, pumps messages and
  reports how long `ListLoadW` blocked and the worst UI-thread stall, then calls `ListCloseWindow`.
- `bench_maddy*.cpp`: maddy throughput benchmarks (normal text and single long lines).
- `gen_samples.ps1`: generates `big.md` (20 MB), `longline.md` (1 MB, one line) and `small.md`.

```bat
tests\harness\build_harness.bat
tests\harness\out\lister_host.exe tc_markdown_lister_debug.wlx tests\harness\out\big.md 15
```

A healthy result: `ListLoadW` returns in under 50 ms and the max UI gap stays around 100 ms.

## Repo layout (selected)

| Path | What |
|---|---|
| `tc_markdown_lister.cpp/.def` | the plugin |
| `main.cpp` | standalone viewer prototype |
| `include/maddy`, `include/webview` | vendored header-only libs |
| `release/` | packaging files (`pluginst.inf`, `tc_markdown.ini`) |
| `docs/` | WLX API docs, integration notes |
| `example/`, `tc_plugin/` | reference plugins and WLX SDK (untracked) |
| `obsidian/` | this vault |
