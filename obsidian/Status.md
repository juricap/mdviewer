# Status

Back to [[mdviewer]] · open work in [[TODO]].

## Current state (2026-11-14)

- TC lister plugin works with WebView2 rendering, UTF-8/ANSI files, dark-mode flag, ESC to close,
  zoom and context menu.
- **Large file freeze is fixed** in the working tree and verified with the test harness. It still
  needs testing in real Total Commander. Details in [[Large file freeze]].
- Large or complex files (more than about 250 KB of Markdown, or any line longer than a few KB)
  currently show as plain text. The fix is md4c (pending approval).
- Standalone `md_viewer.exe` is a dormant prototype.

## Progress log

### 2026-11-14
- Created this Obsidian vault (`obsidian/`) with notes on purpose, development and status.
- Investigated the TC freeze on 10–20 MB files. Benchmarked maddy: ~12 s/MB, and quadratic
  or worse on long lines.
- Rewrote `tc_markdown_lister.cpp`:
  - worker-thread parsing, loading page, file-based navigation (no 2 MB limit)
  - maddy cost estimate with a plain-text fallback, and a 64 MB truncation cap
  - safe cancel on close, DLL refcount held by the worker, temp-file cleanup
  - `ListLoad` now converts the path from ANSI (`CP_ACP`); before, it wrongly used UTF-8
  - ANSI and wide entry points share one implementation (removed ~250 duplicated lines)
  - ESC handler injected with `AddScriptToExecuteOnDocumentCreated`; the old `ExecuteScript`
    ran before navigation, so the handler was lost
  - lister search (F7 / Shift+F3) via `window.find()`, Copy and Select All via `execCommand`.
    These used to call Edit-control APIs on a non-edit window and did nothing.
  - `lcp_darkmode` honoured
  - closing before WebView2 finishes initialising no longer leaks or touches freed state
- Fixed `build_plugin_fixed.bat` reporting "Build failed!" after a successful build (unescaped
  `)` inside an `if` block).
- Added `tests/harness/` (fake lister host + maddy benchmarks).

### 2025-08-03 (from git history)
- WebView2-based plugin completed, UTF-8/ANSI encoding support, release packaging (64-bit only).
