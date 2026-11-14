# mdviewer

Home note for the **mdviewer** project. Open this `obsidian/` folder as a vault.

## Purpose

A lightweight native Markdown viewer for Windows, in two forms:

1. **Total Commander lister plugin** (`tc_markdown_lister.wlx` / `.wlx64`), the main product.
   Press F3 / Ctrl+Q on a `.md` file in Total Commander to see it rendered.
   Source: `tc_markdown_lister.cpp`.
2. **Standalone viewer** (`md_viewer.exe`, `main.cpp`): `md_viewer.exe file.md` opens a
   window with the rendered file. It was the original prototype and is not actively developed.

Both convert Markdown to HTML in C++ (currently the header-only **maddy** parser) and show it
in **Microsoft Edge WebView2**, so there is no Electron and no bundled browser.

- Supported extensions: `.md .markdown .mdown .mkd .mkdn`
- 64-bit only (TC 64-bit). Needs the WebView2 runtime (ships with Windows 10/11).
- Remote: `ghjuricap:juricap/mdviewer.git` (GitHub `juricap/mdviewer`), branch `main`.

## Notes

- [[Status]]: where things stand, progress log
- [[TODO]]: open work, prioritised
- [[Development]]: building, architecture, testing
- [[Large file freeze]]: investigation of the 10–20 MB file hang (Nov 2026)
