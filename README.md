# Markdown Viewer

A lightweight, native Markdown viewer for Windows built with C++ using [maddy](https://github.com/progsource/maddy) for Markdown parsing and [webview](https://github.com/webview/webview) for rendering.

## Features

- Native Windows application (no Electron, minimal footprint)
- GitHub-flavored Markdown support
- Clean, professional styling
- Fast and responsive
- Small binary size (~500KB)

## Supported Markdown Features

- Headers (H1-H6)
- Bold, italic, and inline code
- Code blocks with syntax highlighting
- Tables
- Lists (ordered and unordered)
- Blockquotes
- Horizontal rules
- Links
- Images

## Prerequisites

- Windows 10/11 with WebView2 runtime (pre-installed on most systems)
- Visual Studio 2019 or 2022 for building

## Building from Source

1. Clone the repository:
```bash
git clone https://github.com/juricap/mdviewer.git
cd mdviewer
```

2. Run the build script:
```bash
setup_and_build.bat
```

This will:
- Download the WebView2 SDK
- Compile the application
- Create `md_viewer.exe`

## Usage

```bash
md_viewer.exe <markdown-file>
```

Example:
```bash
md_viewer.exe README.md
```

## Dependencies

- [maddy](https://github.com/progsource/maddy) - Header-only Markdown to HTML parser
- [webview](https://github.com/webview/webview) - Cross-platform webview library
- WebView2 - Microsoft Edge WebView2 runtime

## License

This project is open source. The dependencies have their own licenses:
- maddy: MIT License
- webview: MIT License

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.