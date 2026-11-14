# Large file freeze

Back to [[mdviewer]] · see [[Status]], [[TODO]].

**Reported 2026-11-14:** opening a 10–20 MB `.md` file freezes all of Total Commander. The
only way out is to kill and restart TC.

## Root causes (verified)

1. **All work ran on TC's UI thread.** `ListLoadW` read the file, ran the parser and converted to
   UTF-16 before returning, so TC's message loop was blocked for the whole parse.
2. **The maddy parser is slow.** Measured (x64 `/O2`, `tests/harness/bench_maddy*.cpp`):
   - normal Markdown: **~12 s per MB**, linear. A 20 MB file takes about 4 minutes.
   - **super-linear in line length**. A single line of 2 KB takes 2.4 s, 5 KB takes 13 s,
     20 KB takes 140 s, and 100 KB did not finish in 10+ minutes. Even a small file with one
     long line could freeze TC.
3. **`NavigateToString` is capped at 2 MB.** The 49 MB HTML could never have loaded.
4. (found while fixing) **WebView2 layout of huge pages.** Even when served properly, a 49 MB HTML
   page (about 1M elements) took **84 s** to finish loading.

## Fix

### Step 1 (2026-11-14, commit f0c7b5f)
Worker thread, "Loading…" page, temp file + `Navigate(file://)`, and a maddy cost estimate that
fell back to plain text. This stopped the freeze, but large files lost their formatting.

### Step 2 (2026-11-14): replace maddy with md4c
- **md4c 0.5.2** (github.com/mity/md4c, MIT) is **vendored in `include/md4c/`** and compiled
  statically into the plugin. It uses only the C standard library, with no download at build time.
  GitHub dialect: tables, strikethrough, task lists, autolinks. Raw HTML passes through.
- Benchmarks (`tests/harness/bench_md4c.cpp`, x64 /O2):

  | input | md4c | maddy |
  |---|---|---|
  | 20 MB normal doc | 0.44 s | ~4 min |
  | 20 MB one paragraph | 0.54 s | ~4 min |
  | 20 MB **single line** | 0.70 s | never finishes |
  | 200 MB doc | 4.3 s | — |

- The cost estimate and plain-text fallback were removed, so everything renders formatted.
- **Chunked output for documents of 512 KB or more:** the Markdown is split at blank lines outside
  code fences (about 64 KB chunks). Each chunk becomes `<section class="chunk">` with CSS
  `content-visibility: auto`, so Chromium skips layout of off-screen sections.
  Full load of the 20 MB file went from **84 s to 6 s**, and the **first screen is visible at
  about 1.5 s** (screenshot-verified).
  Trade-off: a construct that spans a split point (e.g. a reference-style link whose definition
  is in another chunk) may not resolve.
- Files over 64 MB: only the first 64 MB is rendered, cut at a line break, with a notice bar.
- Plugin DLL shrank from 434 KB to 350 KB (no `std::regex`).

### Verified with `tests/harness/lister_host.cpp` (optimized build with logging)

| File | ListLoadW | max UI stall | parse | page loaded |
|---|---|---|---|---|
| features.md | 21 ms | 119 ms | 1 ms | 0.6 s |
| medium.md (150 KB) | 15 ms | 78 ms | 6 ms | 0.65 s |
| longline.md (1 MB single line) | 16 ms | 88 ms | 42 ms | 1.6 s |
| big.md (20 MB) | 23 ms | 174 ms | 0.71 s | 6.0 s (first screen ~1.5 s) |

**Not yet verified inside real Total Commander.** See [[TODO]].
