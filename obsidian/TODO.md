# TODO

Back to [[mdviewer]] · history in [[Status]].

## Now
- [ ] **Test in real Total Commander** (F3, Ctrl+Q quick view with cursor movement, F7 search,
      Esc, dark mode, 20 MB file, links). Install from `artifacts\release\mdview-0.3.0.zip`.
- [ ] Test the same `mdview.wlx64` in **Double Commander** on Windows.
- [ ] Decide how **aicmd** hosts it: helper executable (aicmd's viewer convention, crash
      isolation) or linked library. Then add the protocol/CLI that aicmd needs.

## Viewer features
- [ ] **Text selection and copy.** Today Ctrl+A, Ctrl+C copies the whole Markdown source; there
      is no partial selection. litehtml has no selection support, so it has to be built on its
      text elements.
- [ ] In-page anchors (`#heading`): generate heading ids and scroll to them.
- [ ] Syntax highlighting in code blocks (a small built-in tokenizer → spans).
- [ ] Images: JPEG/GIF on Linux (cairo decodes only PNG); size limits for huge images.
- [ ] Settings: zoom, theme and font persisted (ini next to the plugin, or a host-provided setting).
- [ ] Print (`ListPrintW`) and TC's "percent" status line.
- [ ] Follow TC's lister options for dark mode automatically (`lcp_darkmode` is honoured; system
      theme for standalone).

## Platforms
- [ ] **Linux interactive frontend**: GTK3 window plus the **Double Commander Linux WLX** (DC
      passes a GTK/Qt parent widget). Same cairo container.
- [ ] macOS: a CoreGraphics/CoreText container, or cairo via Homebrew.
- [ ] 32-bit `mdview.wlx` for 32-bit TC (the CMake suffix logic exists; untested).

## Robustness
- [ ] Fuzz `split_markdown` and the find code with random input.
- [ ] Consider running litehtml out-of-process for hosts that need crash isolation (aicmd helper
      model).
- [ ] Chunk boundaries: constructs spanning a split point (rare) render as two blocks.
      Footnotes are not supported by md4c anyway.

## Cleanup
- [ ] Untracked leftovers in the repo root (`example/`, `tc_plugin/`, `bak/`, `docs/` WebView2
      notes, `webview-master/`, `packages/`, `*.obj`, `nul`, `TC_PLUGIN_README.md`): delete or
      archive (needs the owner's OK).
