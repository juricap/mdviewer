@echo off
REM Setup and build script for Markdown Viewer with WebView2

echo Setting up WebView2 SDK...

REM Download WebView2 SDK using NuGet
if not exist "packages" mkdir packages
if not exist "packages\Microsoft.Web.WebView2" (
    echo Downloading WebView2 SDK...
    powershell -Command "Invoke-WebRequest -Uri 'https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2210.55' -OutFile 'webview2.zip'"
    powershell -Command "Expand-Archive -Path 'webview2.zip' -DestinationPath 'packages\Microsoft.Web.WebView2' -Force"
    del webview2.zip
)

echo.
echo Building Markdown Viewer with Visual Studio...

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

REM Compile with MSVC including WebView2 headers
cl /std:c++14 /EHsc /O2 /I"include" /I"packages\Microsoft.Web.WebView2\build\native\include" main.cpp /Fe:md_viewer.exe /link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup "packages\Microsoft.Web.WebView2\build\native\x64\WebView2LoaderStatic.lib" advapi32.lib ole32.lib shell32.lib shlwapi.lib user32.lib version.lib

if %ERRORLEVEL% == 0 (
    echo.
    echo Build successful!
    echo.
    echo Note: WebView2 Runtime is required to run the application.
    echo Most Windows 11 and updated Windows 10 systems have it pre-installed.
    echo If not, download from: https://go.microsoft.com/fwlink/p/?LinkId=2124703
    echo.
    echo Run: md_viewer.exe yourfile.md
) else (
    echo.
    echo Build failed!
    exit /b 1
)