# Development

Back to [[mdviewer]] · [[Architecture]].

## Windows

Requirements: Visual Studio 2022 with C++ tools (its bundled CMake and Ninja are used).

```bat
build.bat                        :: configure + build Release into build\ + ctest
build\mdview.exe file.md         :: standalone
build\mdview.exe --png out.png --width 1000 --height 1400 [--scroll Y] [--zoom Z] [--dark] file.md
build\mdview.wlx64               :: lister plugin
powershell scripts\package.ps1   :: artifacts\release\mdview-<ver>.zip with pluginst.inf
```

`mdview.exe` is a GUI-subsystem program. To capture `--png` output from PowerShell, use
`Start-Process -Wait -NoNewWindow -RedirectStandardOutput`.

Install in TC: open the zip in TC (it auto-installs from `pluginst.inf`), or Configuration →
Options → Plugins → Lister → Add `mdview.wlx64`.

## Linux (via `ssh ai`, never WSL)

Copy the sources (`CMakeLists.txt src apps tests third_party`) to `/data/devel/mdview-check-<date>/`,
then:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8 && (cd build && ctest)
build/mdview --png out.png tests/fixtures/features.md
```

Needs cairo, pangocairo and pkg-config (present on `ai`: Ubuntu 22.04, cairo 1.16, pango 1.50, g++ 11).

## Tests

- `mdview-core-tests` (portable): encodings, md4c output, chunking (fences, forced splits, UTF-8
  boundaries), reference links across chunks, search, 20 MB speed.
- `mdview-render-tests` (Windows GDI and Linux cairo): features fixture renders, find and
  highlight, dark mode, and a 20 MB document with timing assertions (first paint, jumps,
  scroll step, find, cache bound), plus PNG output.
- `mdview-lister-host` (Windows): a fake TC lister. It loads a `.wlx`, calls `ListLoadW`, pumps
  messages and reports the worst UI-thread stall.
  `build\mdview-lister-host.exe build\mdview.wlx64 file.md 10`
- `tests/harness/gen_samples.ps1 -Dir <dir>`: big.md (20 MB), longline.md (1 MB on one line), small.md.

## Conventions
See `AGENTS.md` in the repo root.
