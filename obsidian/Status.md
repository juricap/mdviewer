# Status

Back to [[mdviewer]] · open work in [[TODO]].

## Current state (2026-11-15), version 0.3.0

Verified:
- **Windows:** `mdview.exe` (1.9 MB) and `mdview.wlx64` (1.9 MB) build with static CRT and no
  warnings in our code. Core and render tests pass. Screenshots checked: light, dark, the 20 MB
  file in the fake lister, End/PgUp in the standalone window.
- **Linux (`ai`, Ubuntu 22.04):** the same core and renderer with the cairo/pango backend build
  with no warnings in our code. Core and render tests pass. The `--png` output matches Windows.
- Rendering: headings, emphasis, strikethrough, inline/block code (wrapped), tables with column
  alignment and striped rows, task lists, nested/ordered lists, blockquotes, rules, autolinks,
  relative images, Unicode including CJK and Greek, light/dark themes.

Measured (release builds):

| | Windows | Linux |
|---|---|---|
| 20 MB: decode + split | 0.05 s | 0.06 s |
| 20 MB: first paint | 0.08 s | 0.10 s |
| 20 MB: jump to middle / end | 0.08 s | 0.10 s |
| 20 MB: scroll step (120 px) | 10 ms | 3.8 ms |
| 20 MB: find text at the end | 3 ms | 2 ms |
| fake TC lister: `ListLoadW` | ~15 ms | — |
| worst UI stall (20 MB / 1 MB single line) | 104 ms / 134 ms | — |

**Not yet verified:** inside real Total Commander or Double Commander, and aicmd integration.

## Progress log

### 2026-11-15: self-contained component (litehtml)
- The user chose a built-in renderer (litehtml) over the OS web views, and asked for a reusable
  component: standalone, TC plugin reusable for Double Commander, and aicmd, on all platforms.
- Restructured into CMake: `src/core` (portable), `src/render` (portable litehtml virtualized
  view), `src/platform/win32`, `src/platform/cairo`, `apps/mdview`, `plugins/wlx`, `tests`,
  `third_party` (md4c 0.5.2, litehtml 0.10 vendored unmodified).
- Removed the WebView2 plugin, the maddy/webview standalone and the old batch build scripts
  (they remain in git history).
- Fixes found along the way: blank-line detection (chunks never split), the UTF-8 check accepting
  a truncated tail on untruncated files, and chunk sizes tuned to litehtml's measured layout
  speed (~180 KB/s).
- Added link-reference propagation across chunks, UTF-16 BOM support, `ListLoadNextW` window
  reuse, link following, find highlight, and packaging (`scripts/package.ps1`).
- Linux build and tests via `ssh ai` (`/data/devel/mdview-check-20261115`).

### 2026-11-14: freeze fix (WebView2 era, superseded)
- Worker-thread parsing, file navigation, and maddy replaced by md4c. See [[Large file freeze]].
- Obsidian vault created.

### 2025-08-03 (from git history)
- WebView2-based TC plugin.
