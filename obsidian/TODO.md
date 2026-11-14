# TODO

Back to [[mdviewer]] · history in [[Status]].

## Now
- [ ] **Test the freeze fix in real Total Commander** (F3 and Ctrl+Q quick view) with 10–20 MB
      files, rapid cursor movement in quick view, and closing while "Loading…" is shown.
- [ ] Decide on **md4c** to replace maddy (see [[Large file freeze]]). It would give full rendering of
      large files and remove the plain-text fallback.
- [ ] Rebuild the release zip (`create_release.bat`) once verified.

## Next
- [ ] Read settings from `tc_markdown.ini`. Today the ini is shipped but nothing reads it. Candidates:
      the rich-render budget (`kMaxMaddySeconds`), the truncation size, and dark mode.
- [ ] `ListLoadNext(W)`: reuse the WebView when the quick-view cursor moves, instead of creating a
      new environment and controller every time (slow, and spawns processes).
- [ ] Share one `ICoreWebView2Environment` across lister windows.
- [ ] Syntax highlighting for code blocks (e.g. a bundled highlight.js).
- [ ] Print (`ListPrintW` via `ICoreWebView2_16::Print`).
- [ ] Links: open external links in the default browser instead of inside the lister.

## Cleanup
- [ ] Remove the stray `nul` file in the repo root (an artifact of a `> nul` redirect from a POSIX shell).
- [ ] Decide what to do with untracked `example/`, `tc_plugin/`, `bak/`, `build_plugin_fixed.bat.bak`,
      `build_commands.txt`, `task.txt`, and `TC_PLUGIN_README.md` (outdated: still describes the
      Rich Edit version).
- [x] Add `*.wlx`, `*.wlx64` and `tests/harness/out/` to `.gitignore`.
- [ ] `README.md` only describes the standalone viewer. Document the TC plugin there.
- [ ] `CoInitializeEx` is called on every `ListLoad` without a matching `CoUninitialize`.
