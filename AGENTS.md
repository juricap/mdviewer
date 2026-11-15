# mdview development

- C++17, CMake, native platform APIs. `src/core` and `src/render` stay portable: no Win32,
  cairo, GTK or other GUI types there. Platform code lives in `src/platform/<backend>/`.
- Dependencies are vendored in `third_party/`, unmodified, with a `VERSION.txt`, and built
  from source. Don't add runtime dependencies (no WebView2, no browser engines). Shims go in
  our code (e.g. `src/platform/win32/gdiplus_minmax.h`), not in vendored files.
- Never do heavy work on the host's UI thread (Total Commander freezes completely). Loading runs
  on a worker thread. Layout is bounded by the chunk size (≤ 32 KB of Markdown per chunk,
  ~0.2 s worst case).
- Build and test on Windows with `build.bat`. **Linux checks run via `ssh ai`** in an isolated
  folder under `/data/devel/` (e.g. `mdview-check-<date>`). **Do not use WSL.** Don't disturb
  other projects there.
- Verify rendering visually (`mdview --png`) when touching CSS, the renderer or containers.
- Keep `obsidian/` current: record verified results separately from plans and untested behavior.
- Local Git on a feature branch. Commit after validation. Don't push unless asked.
