# mdview

Fast, self-contained Markdown viewer component in C++17: one portable core and renderer, with
thin frontends for a standalone app, the Total Commander / Double Commander lister plugin, and
[aicmd](https://github.com/juricap/aicmd).

- **Parser:** [md4c](https://github.com/mity/md4c) (MIT), GitHub dialect: tables, task lists,
  strikethrough, autolinks. Linear time: 20 MB in about 0.5 s.
- **Renderer:** [litehtml](https://github.com/litehtml/litehtml) (BSD), a built-in HTML/CSS layout
  engine drawing through native 2D APIs: GDI/GDI+ on Windows, cairo/pango on Linux. No WebView2,
  no browser, no runtime to install.
- **Large files:** the document is split into chunks and only what's on screen is laid out. A
  20 MB file shows its first screen in about 0.1 s, and jumping anywhere or scrolling takes
  milliseconds.
- Everything is vendored in `third_party/` and built from source. The Windows binaries use the
  static CRT.

Docs: [obsidian/mdviewer.md](obsidian/mdviewer.md) (architecture, choices, status, TODO).

## Build

Windows (Visual Studio 2022):

```bat
build.bat                      :: configure + build (Release, Ninja) + tests
build\mdview.exe file.md       :: standalone viewer
build\mdview.wlx64             :: lister plugin (Total Commander / Double Commander)
powershell scripts\package.ps1 :: artifacts\release\mdview-<ver>.zip (TC auto-install)
```

Linux (cairo, pango, pkg-config, CMake ≥ 3.16):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && (cd build && ctest)
build/mdview --png out.png file.md     # headless render (interactive frontend: planned)
```

## Layout

| Path | What |
|---|---|
| `src/core/` | portable document model: decode, chunk, md4c → HTML, CSS, search (no GUI types) |
| `src/render/` | portable virtualized litehtml view: scrolling, hit testing, find |
| `src/platform/win32/` | GDI/GDI+ container, Win32 view control, headless PNG render |
| `src/platform/cairo/` | cairo/pango container, headless PNG render |
| `apps/mdview/` | standalone app (Win32 window; CLI on other platforms) |
| `plugins/wlx/` | WLX lister plugin wrapper |
| `tests/` | core tests, render tests, fixtures, fake lister host |
| `third_party/` | md4c 0.5.2, litehtml 0.10 (with gumbo), unmodified |
