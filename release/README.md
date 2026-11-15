# mdview

Fast, self-contained Markdown viewer: a Total Commander / Double Commander lister plugin
(`mdview.wlx64`) and a standalone viewer (`mdview.exe`).

- Renders GitHub-flavoured Markdown: tables, task lists, strikethrough, autolinks, code
  blocks, images (local files), light and dark themes.
- Self-contained: no WebView2, no browser, no Visual C++ redistributable. The Markdown parser
  (md4c) and HTML/CSS layout engine (litehtml) are compiled in.
- Large files stay responsive: a 20 MB file shows its first screen in about 0.1 s, and scrolling
  or jumping anywhere takes about 10–80 ms. Files over 64 MB show their first 64 MB.

## Install in Total Commander

Open the zip in Total Commander and confirm the plugin installation. Or copy `mdview.wlx64`
anywhere, then Configuration → Options → Plugins → Lister plugins → Add.
Press F3 or Ctrl+Q on a `.md` file.

Keys: arrows / PgUp / PgDn / Space / Home / End scroll; Ctrl + mouse wheel or Ctrl +/-/0 zoom;
F7 finds (Total Commander's search dialog); Esc closes. Clicking a link opens web links in the
browser and `.md` links inside the viewer.

## Standalone

```
mdview.exe file.md
mdview.exe --dark file.md
mdview.exe --png out.png --width 1000 --height 1400 file.md     (headless render)
```

## Licenses

mdview: see the project. Bundled: md4c (MIT), litehtml (BSD-3-Clause), gumbo (Apache-2.0).
License texts are in `licenses/`.
