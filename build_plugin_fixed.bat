@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem --- Minimize env size to avoid "The input line is too long" ---
set "_ORIG_PATH=%PATH%"
set "_ORIG_INCLUDE=%INCLUDE%"
set "_ORIG_LIB=%LIB%"
set "_ORIG_LIBPATH=%LIBPATH%"
set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem"
set "INCLUDE="
set "LIB="
set "LIBPATH="
REM Build script for Total Commander Markdown Lister Plugin

echo Building Total Commander Markdown Lister Plugin...

REM Try to find Visual Studio 2022 or 2019
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    call "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    call "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    call "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
) else (
    echo Error: Visual Studio not found!
    echo Please install Visual Studio 2019 or 2022
    exit /b 1
)

REM Load Rich Edit library for text display
echo Compiling DLL...

REM Download WebView2 SDK if not exists
if not exist "packages\Microsoft.Web.WebView2" (
    echo Downloading WebView2 SDK...
    powershell -Command "Invoke-WebRequest -Uri 'https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2210.55' -OutFile 'webview2.zip'"
    powershell -Command "Expand-Archive -Path 'webview2.zip' -DestinationPath 'packages\Microsoft.Web.WebView2' -Force"
    del webview2.zip
)

REM Compile the DLL
cl /std:c++17 /EHsc /O2 /I"include" /I"packages\Microsoft.Web.WebView2\build\native\include" ^
   tc_markdown_lister.cpp /LD /Fe:tc_markdown_lister.wlx ^
   /link user32.lib kernel32.lib ole32.lib oleaut32.lib shell32.lib shlwapi.lib version.lib windowsapp.lib ^
   "packages\Microsoft.Web.WebView2\build\native\x64\WebView2LoaderStatic.lib"

if %ERRORLEVEL% == 0 (
    REM Copy required files
    copy md_viewer.exe . >nul 2>&1
    copy tc_markdown_lister.wlx tc_markdown_lister.wlx64 >nul 2>&1
    
    echo.
    echo Build successful!
    echo Plugin created: tc_markdown_lister.wlx
    echo Required files: tc_markdown_lister.wlx, md_viewer.exe
    echo.
    echo Installation:
    echo 1. Copy tc_markdown_lister.wlx AND md_viewer.exe to Total Commander's plugin directory
    echo 2. In Total Commander: Configuration ^> Options ^> Plugins ^> Lister ^> Configure
    echo 3. Add the plugin and configure file associations for .md files
    echo.
    echo The plugin will preview markdown files in Total Commander's lister.
) else (
    echo.
    echo Build failed!
    exit /b 1
)

rem --- Restore original environment ---
endlocal & set "PATH=%_ORIG_PATH%" ^& set "INCLUDE=%_ORIG_INCLUDE%" ^& set "LIB=%_ORIG_LIB%" ^& set "LIBPATH=%_ORIG_LIBPATH%"