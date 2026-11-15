# mdview (mdviewer)

Home note. Open the `obsidian/` folder as a vault.

## Purpose

A fast, **self-contained Markdown viewer component**, reusable in several hosts:

1. **Standalone viewer**: `mdview.exe file.md` (Windows window; Linux headless CLI for now).
2. **Lister plugin** `mdview.wlx64` for **Total Commander**, and the same plugin for **Double
   Commander** on Windows (DC on Linux/macOS: planned on the cairo backend).
3. **aicmd** (`F:\devel\aicmd`, the user's own cross-platform file manager): the viewer for
   `.md` files, either as a helper executable (aicmd's viewer convention) or linked in.

Markdown → HTML with **md4c**, laid out and drawn by the built-in **litehtml** engine through
native 2D APIs (GDI/GDI+ on Windows, cairo/pango on Linux). There is no WebView2 or browser.
Everything is vendored and statically linked.

Repo: `F:\devel\jpcservices\mdviewer`, remote `juricap/mdviewer` (not pushed; work is on the
branch `fix/large-file-freeze`).

## Notes

- [[Architecture]]: layers, chunked/virtualized rendering, platform backends, hosts
- [[Choices]]: decisions that should not silently change
- [[Development]]: build, test, Linux checks, packaging
- [[Status]]: verified state and progress log
- [[TODO]]: open work
- [[Large file freeze]]: the original TC freeze investigation (Nov 2026)
