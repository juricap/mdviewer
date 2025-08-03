# TC Markdown Viewer

A modern Markdown lister plugin for Total Commander using WebView2 for rendering.

## Features

- **Modern Rendering**: Uses Microsoft Edge WebView2 for fast, accurate HTML rendering
- **GitHub-Style Formatting**: Clean, professional appearance with GitHub-flavored CSS
- **Full Markdown Support**: Headers, lists, code blocks, tables, links, images, blockquotes
- **Native Performance**: Header-only C++ implementation with minimal footprint
- **Dark Mode Support**: Automatic or manual dark/light theme switching
- **Cross-Platform Ready**: Built with modern web standards

## Supported File Types

- `.md` - Markdown files
- `.markdown` - Extended markdown files  
- `.mdown` - Markdown down files
- `.mkd` - Markdown files (short extension)
- `.mkdn` - Markdown files (alternative)

## System Requirements

- **Windows 10/11** (WebView2 runtime - pre-installed on most systems)
- **Total Commander** 8.0 or newer
- **Visual C++ Redistributable** (usually already installed)

## Installation

### Automatic Installation (Recommended)

1. Download the plugin package (`tc-markdown-viewer-x.x.zip`)
2. Double-click the ZIP file in Total Commander
3. Confirm the installation when prompted
4. The plugin will be automatically configured

### Manual Installation

1. Extract the ZIP file to Total Commander's plugin directory:
   - Default: `C:\Program Files\Total Commander\plugins\wlx\MarkdownViewer\`
2. In Total Commander: **Configuration → Options → Plugins → Lister**
3. Click **Configure** and add `tc_markdown_lister.wlx`
4. The plugin should auto-detect markdown file extensions

## Usage

1. Navigate to a folder containing Markdown files in Total Commander
2. Press **F3** (View) or **Ctrl+Q** (Quick View) on any `.md` file
3. The markdown content will be displayed with GitHub-style formatting
4. Use **Ctrl+F** to search within the document

## Configuration

Edit `tc_markdown.ini` to customize:

- **Dark mode settings** - Automatic or manual theme switching
- **File extensions** - Add custom markdown file extensions
- **Rendering options** - Image loading, link handling policies
- **Hotkeys** - Customize keyboard shortcuts

## Technical Details

### Architecture
- **Parser**: maddy (header-only C++ markdown parser)
- **Renderer**: Microsoft Edge WebView2
- **Interface**: Total Commander WLX API
- **Styling**: GitHub-flavored CSS with dark mode support

### Performance
- **Binary size**: ~500KB
- **Memory usage**: Minimal (shared WebView2 runtime)
- **Startup time**: Near-instantaneous
- **Rendering speed**: Hardware-accelerated via WebView2

## Troubleshooting

**Plugin not loading:**
- Ensure WebView2 runtime is installed (download from Microsoft)
- Check Visual C++ Redistributable is installed
- Verify plugin is in correct directory

**Files not recognized:**
- Check file extension configuration in Total Commander
- Verify detection string includes your file extensions
- Try manual file association in plugin settings

**Rendering issues:**
- Update WebView2 runtime to latest version
- Check `tc_markdown.ini` rendering settings
- Disable other markdown plugins that might conflict

## Development

Built with:
- **Visual Studio 2022** (C++17)
- **maddy parser** - Fast, header-only markdown parsing
- **WebView2** - Modern web engine integration
- **WLX SDK** - Total Commander plugin interface

Source code and build instructions available at: https://github.com/juricap/mdviewer

## License

Open source software. Dependencies:
- maddy: MIT License
- WebView2: Microsoft Software License
- WLX SDK: Ghisler Software License

## Version History

### v1.0.0
- Initial release with WebView2 integration
- GitHub-style CSS rendering
- Full markdown feature support
- Dark mode compatibility
- Auto-installation support

## Support

For issues, feature requests, or contributions:
- GitHub Issues: https://github.com/juricap/mdviewer/issues
- Documentation: https://github.com/juricap/mdviewer

---

**Made with ❤️ for the Total Commander community**