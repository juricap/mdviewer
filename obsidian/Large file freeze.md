# Large file freeze

Back to [[mdviewer]] · see [[Status]], [[TODO]].

**Reported 2026-11-14:** opening a 10–20 MB `.md` file freezes all of Total Commander. The
only way out is to kill and restart TC.

## Root causes (verified)

1. **All work ran on TC's UI thread.** `ListLoadW` read the file, ran maddy and converted to
   UTF-16 before returning, so TC's message loop was blocked for the whole parse.
2. **maddy is slow.** Measured (x64 `/O2`, `tests/harness/bench_maddy*.cpp`):
   - normal short-line Markdown: **~12 s per MB**, linear. A 20 MB file takes about 4 minutes.
   - **super-linear in line length**. A single line of 2 KB takes 2.4 s, 5 KB takes 13 s,
     10 KB takes 31 s, 20 KB takes 140 s, and 100 KB did not finish in 10+ minutes.
     So even a *small* file with one long line (minified HTML, huge table row, base64 image)
     could freeze TC.
3. **`NavigateToString` is capped at 2 MB.** Even after the parse finished, the 27 MB HTML
   would have failed to load.

## Fix (2026-11-14)

See the architecture in [[Development]]. Summary:
- Parse on a worker thread, and show a "Loading…" page in the meantime. `ListLoadW` now returns
  in about 20 ms.
- Load the result from a temp file with `Navigate(file://)`, which has no size limit.
- `EstimateMaddyCost()` predicts maddy time from the line lengths (12 µs/byte + 0.73 µs·(L−200)²
  per line). If the estimate is over 3 s, the file is shown as **escaped plain text** with a notice
  bar instead. In practice, rich rendering applies up to about 250 KB of normal Markdown.
- Files over 64 MB are truncated (plain text) to bound memory.
- Closing the lister mid-render is safe. The job is cancelled, the DLL stays referenced until the
  thread exits, and the render file is deleted.

Verified with `tests/harness/lister_host.cpp`:

| File | ListLoadW | max UI stall | render |
|---|---|---|---|
| small.md (96 B) | 17 ms | 78 ms | 8 ms, rich |
| longline.md (1 MB single line) | 17 ms | 78 ms | 20 ms, plain text |
| big.md (20 MB) | 25 ms | 117 ms | 0.38 s, plain text |
| medium.md (150 KB) | 17 ms | 110 ms | 2.4 s (release), rich |
| medium.md closed after 0.5 s | 19 ms | 66 ms | cancelled cleanly |

**Not yet verified inside real Total Commander.** See [[TODO]].

## Proper long-term fix: replace maddy with md4c

The plain-text fallback keeps TC alive, but large files lose formatting. **md4c**
(github.com/mity/md4c, MIT, C, CommonMark + GFM tables, strikethrough and task lists) has a
linear-time parser that handles hundreds of MB/s, so 20 MB would render fully in well under a
second. It's 3 `.c` files plus headers, with no other dependencies.

Status: the sources were downloaded on 2026-11-14, but compiling them was blocked by the
session's sandbox (freshly downloaded third-party code). They were removed pending the owner's
OK. Swapping parsers only touches `BuildDocumentHtml()`, and the cost-model guard can then go.
