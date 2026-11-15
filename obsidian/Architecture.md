# Architecture

Back to [[mdviewer]] · decisions in [[Choices]].

```
 hosts       apps/mdview (Win32 window | Linux CLI)   plugins/wlx (TC / DC)   aicmd (planned)
               \__________________ | __________________/
 platform    src/platform/win32: Container (GDI text, GDI+ shapes/images), ViewWindow (HWND control,
             worker-thread loading, scrollbar, keys, mouse, links, find), render_file_to_png
             src/platform/cairo: Container (cairo + pango), render_file_to_png
 render      src/render/view: View - virtualized multi-chunk litehtml view (portable)
 core        src/core/document: Document - decode, chunk, md4c -> HTML, CSS, search (portable)
 vendored    md4c 0.5.2 | litehtml 0.10 (+ gumbo HTML parser) | litehtml's win32/gdiplus/cairo containers
```

## Core (`src/core/document.*`), no GUI types
- `Document::load_file` reads up to 64 MB (with a notice if truncated). It detects UTF-8 BOM,
  UTF-16 LE/BE BOM, valid UTF-8, or else the legacy code page (Windows: ANSI/ACP; elsewhere Latin-1).
- `split_markdown`: documents of 32 KB or more are split into ~16 KB **chunks** at blank lines
  that are outside code fences and followed by a non-indented line. The hard maximum is 32 KB;
  one huge line is cut every 16 KB on a space or UTF-8 boundary.
- Each chunk is converted with md4c independently. **Link reference definitions** are collected
  once and appended to every chunk, so `[text][ref]` resolves across chunks.
- `default_css(dark)`: GitHub-like light/dark styling, using only CSS that litehtml supports.
- `find_chunk`: fast pre-filter on the source text, used by search.

## Render (`src/render/view.*`), portable
- One litehtml document per chunk, **created and laid out only near the viewport**. At most 24
  are cached; the least recently used are released, and their measured heights are kept.
- Heights of chunks not yet laid out are **estimated** from the measured px/byte ratio.
- The scroll position is an **anchor (chunk, offset)**, so refining estimates never moves what's
  on screen. Absolute scrolling (scrollbar) maps through estimated heights. The bottom is clamped
  after layout.
- Zoom changes the base font size (re-creates the chunk documents). Width changes re-render the
  cached documents.
- Mouse events are forwarded to the chunk under the cursor (hover cursor, link clicks).
- Find: pre-filter chunks by source text, then search the chunk's **rendered** text (litehtml
  word/space elements), scroll the match a third of the way down, and highlight the matching
  elements.

## Platform
- **Win32 container** = litehtml's `gdiplus_container` plus our host callbacks. Local images load
  through GDI+; remote URLs are never fetched; external CSS is never imported.
- **ViewWindow** (class `MdViewWindow`): loads on a worker thread that holds a DLL reference
  (`FreeLibraryAndExitThread`), double-buffered painting, a 64-bit-safe scrollbar, wheel, keys,
  Ctrl+zoom, DPI scale, links (http/https/mailto → browser, local `.md` → open in place, other
  files → shell). Exceptions never escape into the host.
- **cairo container** = litehtml's `container_cairo_pango` plus our callbacks. PNG images only
  (cairo's own decoder).

## Hosts
- **WLX plugin**: `ListLoadW` creates the view (~15 ms), and `ListLoadNextW` reuses it. Search,
  copy, select-all, dark mode and percent go to the view. Unhandled keys are posted to the lister
  window (Esc, n/p, 1–7…).
- **Standalone**: frame window plus the view, drag & drop, Esc closes. `--png` renders headless
  (Windows and Linux).
- **aicmd**: not wired yet. Its viewer convention is helper executables (e.g. aiview) with crash
  isolation, so `mdview` as a helper is the natural first step.

## Measured performance
See [[Status]]. Short version: a 20 MB document gets its first paint in ~0.1 s, jumps anywhere in
~0.1 s, scrolls in 4–10 ms per step, and finds text at the end in ~3 ms, on both Windows and Linux.
