# Status

Back to [[mdviewer]] · open work in [[TODO]].

## Current state (2026-11-14)

- TC lister plugin: WebView2 rendering, **md4c parser (vendored, static)**, UTF-8/ANSI files,
  dark-mode flag, ESC to close, zoom, context menu, search, copy and select-all.
- **Large file freeze is fixed** and verified with the test harness and screenshots. A 20 MB file
  shows formatted in about 1.5 s, and TC stays responsive throughout. It still needs testing in
  real Total Commander. Details in [[Large file freeze]].
- Remaining system dependency: the **WebView2 runtime** (Edge). The loader is linked statically,
  but the runtime itself is a system component. Whether to replace it is an open question in [[TODO]].
- Standalone `md_viewer.exe` (`main.cpp`) is a dormant prototype and still uses maddy.

## Progress log

### 2026-11-14 (later): md4c
- Vendored md4c 0.5.2 in `include/md4c/` and replaced maddy in the plugin. The cost-estimate and
  plain-text fallback are removed.
- Chunked `content-visibility:auto` sections for documents of 512 KB or more: WebView2 load of
  20 MB went from 84 s to 6 s.
- GitHub-flavoured extras: tables with alignment, task lists, strikethrough, autolinks.
- `build_plugin_fixed.bat` compiles md4c in and no longer downloads maddy.
- Added `tests/harness/bench_md4c.cpp`.

### 2026-11-14
- Created this Obsidian vault (`obsidian/`) with notes on purpose, development and status.
- Investigated the TC freeze on 10–20 MB files. Benchmarked maddy: ~12 s/MB, and quadratic or
  worse on long lines.
- Rewrote `tc_markdown_lister.cpp`:
  - worker-thread parsing, loading page, file-based navigation (no 2 MB limit)
  - safe cancel on close, DLL refcount held by the worker, temp-file cleanup
  - `ListLoad` now converts the path from ANSI (`CP_ACP`); before, it wrongly used UTF-8
  - ANSI and wide entry points share one implementation
  - ESC handler via `AddScriptToExecuteOnDocumentCreated`
  - lister search via `window.find()`, Copy and Select All via `execCommand`
  - `lcp_darkmode` honoured
- Fixed `build_plugin_fixed.bat` reporting "Build failed!" after a successful build.
- Added `tests/harness/` (fake lister host and benchmarks).

### 2025-08-03 (from git history)
- WebView2-based plugin completed, UTF-8/ANSI encoding support, release packaging (64-bit only).
