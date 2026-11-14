# TODO

Back to [[mdviewer]] · history in [[Status]].

## Now
- [ ] **Test in real Total Commander** (F3 and Ctrl+Q quick view) with 10–20 MB files, rapid
      cursor movement in quick view, and closing while "Loading…" is shown.
- [x] Replace maddy with md4c (vendored, static). See [[Large file freeze]].
- [ ] Decide whether to keep the **WebView2** runtime (a system component) or move to a fully
      self-contained renderer. See "Renderer options" below.
- [ ] Rebuild the release zip (`create_release.bat`) once verified.

## Renderer options (open question)
- **Keep WebView2** (current): full HTML/CSS, runs in a separate process (a renderer crash can't
  take down TC), but needs the Edge WebView2 runtime installed.
- **Native rendering** (e.g. md4c → RichEdit/RTF, or a custom GDI/DirectWrite renderer driven by
  md4c's parser callbacks): no runtime dependency and very small, but limited styling and a lot
  more code for tables and images. It also runs inside TC's process.

## Next
- [ ] Check `window.find()` search works across `content-visibility:auto` chunks in big files.
- [ ] Read settings from `tc_markdown.ini` (truncation size, chunk threshold, dark mode). Today
      the ini is shipped but nothing reads it.
- [ ] `ListLoadNext(W)`: reuse the WebView when the quick-view cursor moves, instead of creating a
      new environment and controller every time.
- [ ] Share one `ICoreWebView2Environment` across lister windows.
- [ ] Raw HTML in Markdown passes through and scripts can run. Consider a CSP or
      `MD_FLAG_NOHTML` option.
- [ ] Syntax highlighting for code blocks (vendored highlight.js).
- [ ] Print (`ListPrintW` via `ICoreWebView2_16::Print`).
- [ ] Links: open external links in the default browser instead of inside the lister.
- [ ] Standalone `main.cpp`: switch to md4c too, or drop it.

## Cleanup
- [ ] Remove the stray `nul` file in the repo root.
- [ ] Decide what to do with untracked `example/`, `tc_plugin/`, `bak/`, `build_plugin_fixed.bat.bak`,
      `build_commands.txt`, `task.txt`, and `TC_PLUGIN_README.md` (outdated).
- [x] Add `*.wlx`, `*.wlx64` and `tests/harness/out/` to `.gitignore`.
- [ ] `README.md` only describes the standalone viewer. Document the TC plugin there.
- [ ] `CoInitializeEx` is called on every `ListLoad` without a matching `CoUninitialize`.
