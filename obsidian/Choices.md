# Choices

Decisions that should not silently change. Back to [[mdviewer]].

- **Self-contained, built-in renderer: litehtml** (user decision 2026-11-15). Not WebView2,
  WebKitGTK or CEF. The user wants every platform and no reliance on system components.
  Drawing still uses each OS's native 2D API (GDI/GDI+, cairo/pango).
- **Parser: md4c**, GitHub dialect (2026-11-14). It replaced maddy, which was regex based,
  ~12 s/MB and super-linear on long lines (see [[Large file freeze]]).
- **Vendored and built from source**, unmodified, in `third_party/` with `VERSION.txt`.
  Windows uses the **static CRT**, so the plugin needs no redistributable.
- **Never block the host's UI thread.** Loading runs on a worker thread; layout is bounded by
  chunk size (≤ 32 KB per chunk, ~0.2 s worst case on the UI thread).
- **Portable core/render, thin platform backends.** No GUI types in `src/core` or `src/render`.
- **One component, many hosts**: standalone, TC/DC lister plugin, aicmd. A host-specific
  feature goes in the host wrapper, not the core.
- **Linux checks via `ssh ai`** under `/data/devel/mdview-check-<date>`. No WSL.
- **No network**: remote images and external CSS are never fetched.
- Chunked rendering trades some Markdown fidelity at chunk boundaries (rare constructs that
  span a split point) for speed. Reference links are handled explicitly.
