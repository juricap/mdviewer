@echo off
setlocal EnableExtensions DisableDelayedExpansion

echo Creating Total Commander Markdown Viewer Release Package...

REM Clean and create release directory
if exist "release\tc-markdown-viewer" rmdir /s /q "release\tc-markdown-viewer"
mkdir "release\tc-markdown-viewer" 2>nul

echo Copying plugin files...

REM Copy the compiled plugin and rename to 64-bit extension
if exist "tc_markdown_lister.wlx" (
    copy "tc_markdown_lister.wlx" "release\tc-markdown-viewer\tc_markdown_lister.wlx64" >nul
    echo Plugin copied as tc_markdown_lister.wlx64 (64-bit only)
) else (
    echo Warning: tc_markdown_lister.wlx not found. Run build_plugin_fixed.bat first.
)

REM Copy configuration and documentation
copy "release\pluginst.inf" "release\tc-markdown-viewer\" >nul
copy "release\tc_markdown.ini" "release\tc-markdown-viewer\" >nul
copy "release\README.md" "release\tc-markdown-viewer\" >nul

REM Copy source files for reference (optional)
echo Copying source files...
mkdir "release\tc-markdown-viewer\src" 2>nul
copy "tc_markdown_lister.cpp" "release\tc-markdown-viewer\src\" >nul
copy "tc_markdown_lister.def" "release\tc-markdown-viewer\src\" >nul
copy "build_plugin_fixed.bat" "release\tc-markdown-viewer\src\" >nul

REM Copy test file
copy "test.md" "release\tc-markdown-viewer\" >nul

REM Create the release ZIP
echo Creating release ZIP file...
cd release
powershell -Command "Compress-Archive -Path 'tc-markdown-viewer\*' -DestinationPath 'tc-markdown-viewer-1.0.zip' -Force"
cd ..

if exist "release\tc-markdown-viewer-1.0.zip" (
    echo.
    echo ================================
    echo Release package created successfully!
    echo ================================
    echo.
    echo File: release\tc-markdown-viewer-1.0.zip
    echo.
    echo Installation:
    echo 1. Double-click the ZIP file in Total Commander
    echo 2. Confirm installation when prompted
    echo 3. The plugin will be automatically configured
    echo.
    echo Manual installation:
    echo 1. Extract to Total Commander plugin directory
    echo 2. Configure in TC: Configuration ^> Plugins ^> Lister
    echo.
) else (
    echo Failed to create release package!
    exit /b 1
)

echo Done!