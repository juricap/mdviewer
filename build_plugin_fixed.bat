@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem --- Store original environment ---
set "_ORIG_PATH=%PATH%"
set "_ORIG_INCLUDE=%INCLUDE%"
set "_ORIG_LIB=%LIB%"
set "_ORIG_LIBPATH=%LIBPATH%"
REM Build script for Total Commander Markdown Lister Plugin

echo Building Total Commander Markdown Lister Plugin...
echo Script version: 2.0 with automatic dependency download

REM Download WebView2 SDK if not exists (before setting up VS environment)
if not exist "packages\Microsoft.Web.WebView2" (
    echo Downloading WebView2 SDK...
    
    REM Try PowerShell first
    powershell -Command "Invoke-WebRequest -Uri 'https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2210.55' -OutFile 'webview2.zip'"
    
    REM If PowerShell failed, try alternative methods
    if not exist "webview2.zip" (
        echo PowerShell failed, trying alternative download method...
        
        REM Try curl (available in Windows 10+)
        curl -L -o webview2.zip "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2210.55"
        
        REM If curl also failed, try wget if available
        if not exist "webview2.zip" (
            wget -O webview2.zip "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2210.55"
        )
        
        REM If all failed, show error
        if not exist "webview2.zip" (
            echo Error: Could not download WebView2 SDK
            echo Please manually download from: https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2210.55
            echo Save as 'webview2.zip' in this directory and run the script again
            pause
            exit /b 1
        )
    )
    
    REM Extract the archive
    echo Extracting WebView2 SDK...
    mkdir packages 2>nul
    
    REM Try PowerShell extraction first
    powershell -Command "Expand-Archive -Path 'webview2.zip' -DestinationPath 'packages\Microsoft.Web.WebView2' -Force" 2>nul
    
    REM If PowerShell extraction failed, try tar (Windows 10+)
    if not exist "packages\Microsoft.Web.WebView2\build" (
        echo PowerShell extraction failed, trying tar...
        mkdir packages\Microsoft.Web.WebView2 2>nul
        tar -xf webview2.zip -C packages\Microsoft.Web.WebView2 2>nul
    )
    
    REM If tar also failed, try manual PowerShell with different approach
    if not exist "packages\Microsoft.Web.WebView2\build" (
        echo Tar extraction failed, trying alternative PowerShell method...
        powershell -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::ExtractToDirectory('webview2.zip', 'packages\Microsoft.Web.WebView2')" 2>nul
    )
    
    REM Verify extraction success
    if exist "packages\Microsoft.Web.WebView2\build\native\include\WebView2.h" (
        echo WebView2 SDK downloaded and extracted successfully
        REM Clean up only after successful extraction
        del webview2.zip 2>nul
    ) else (
        echo Error: WebView2 SDK extraction failed
        echo.
        echo The webview2.zip file has been preserved for manual extraction.
        echo Please:
        echo 1. Manually extract webview2.zip to packages\Microsoft.Web.WebView2\
        echo 2. Ensure the structure is: packages\Microsoft.Web.WebView2\build\native\include\WebView2.h
        echo 3. Run this script again
        echo.
        echo Alternative: Delete webview2.zip and rerun script to re-download
        pause
        exit /b 1
    )
)

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

REM Download Maddy markdown parser if not exists
if not exist "include\maddy\parser.h" (
    echo Downloading Maddy markdown parser...
    
    REM Try PowerShell first
    powershell -Command "Invoke-WebRequest -Uri 'https://github.com/progsource/maddy/archive/refs/heads/master.zip' -OutFile 'maddy.zip'"
    
    REM If PowerShell failed, try alternative methods
    if not exist "maddy.zip" (
        echo PowerShell failed, trying alternative download method...
        
        REM Try curl (available in Windows 10+)
        curl -L -o maddy.zip "https://github.com/progsource/maddy/archive/refs/heads/master.zip"
        
        REM If curl also failed, try wget if available
        if not exist "maddy.zip" (
            wget -O maddy.zip "https://github.com/progsource/maddy/archive/refs/heads/master.zip"
        )
        
        REM If all failed, show error
        if not exist "maddy.zip" (
            echo Error: Could not download Maddy markdown parser
            echo Please manually download from: https://github.com/progsource/maddy/archive/refs/heads/master.zip
            echo Save as 'maddy.zip' in this directory and run the script again
            pause
            exit /b 1
        )
    )
    
    REM Extract the archive
    echo Extracting Maddy headers...
    mkdir temp_maddy 2>nul
    
    REM Try PowerShell extraction first
    powershell -Command "Expand-Archive -Path 'maddy.zip' -DestinationPath 'temp_maddy' -Force" 2>nul
    
    REM If PowerShell extraction failed, try tar (Windows 10+)
    if not exist "temp_maddy\maddy-master" (
        echo PowerShell extraction failed, trying tar...
        tar -xf maddy.zip -C temp_maddy 2>nul
    )
    
    REM If tar also failed, try manual PowerShell with different approach
    if not exist "temp_maddy\maddy-master" (
        echo Tar extraction failed, trying alternative PowerShell method...
        powershell -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::ExtractToDirectory('maddy.zip', 'temp_maddy')" 2>nul
    )
    
    REM Copy headers to include directory
    if exist "temp_maddy\maddy-master\include\maddy" (
        echo Copying Maddy headers...
        mkdir include 2>nul
        xcopy temp_maddy\maddy-master\include\maddy include\maddy\ /E /I /Q
        
        REM Clean up
        rmdir /s /q temp_maddy 2>nul
        del maddy.zip 2>nul
        
        echo Maddy headers installed successfully
    ) else (
        echo Error: Maddy extraction failed
        echo.
        echo The maddy.zip file has been preserved for manual extraction.
        echo Please:
        echo 1. Manually extract maddy.zip
        echo 2. Copy the include/maddy/ folder to this project's include/ directory
        echo 3. Run this script again
        echo.
        pause
        exit /b 1
    )
)

echo Compiling DLL...

REM Compile the DLL (Release build - no debug logging)
echo Compiling Release version...
cl /std:c++17 /EHsc /O2 /I"include" /I"packages\Microsoft.Web.WebView2\build\native\include" ^
   tc_markdown_lister.cpp /LD /Fe:tc_markdown_lister.wlx ^
   /link user32.lib kernel32.lib ole32.lib oleaut32.lib shell32.lib shlwapi.lib version.lib windowsapp.lib ^
   "packages\Microsoft.Web.WebView2\build\native\x64\WebView2LoaderStatic.lib"

set "RELEASE_BUILD_RESULT=%ERRORLEVEL%"

REM Also compile debug version with logging (optional)
if "%1"=="debug" (
    echo.
    echo Compiling Debug version with logging...
    cl /std:c++17 /EHsc /Od /Zi /DDEBUG_LOG /I"include" /I"packages\Microsoft.Web.WebView2\build\native\include" ^
       tc_markdown_lister.cpp /LD /Fe:tc_markdown_lister_debug.wlx ^
       /link user32.lib kernel32.lib ole32.lib oleaut32.lib shell32.lib shlwapi.lib version.lib windowsapp.lib ^
       "packages\Microsoft.Web.WebView2\build\native\x64\WebView2LoaderStatic.lib"
)

REM Check if build succeeded (release build is the primary indicator)
if %RELEASE_BUILD_RESULT% == 0 (
    REM Copy 64-bit version (optional)
    if exist "tc_markdown_lister.wlx" (
        copy tc_markdown_lister.wlx tc_markdown_lister.wlx64 >nul 2>&1
    )
    
    echo.
    echo Build successful!
    echo Release plugin created: tc_markdown_lister.wlx
    if "%1"=="debug" (
        echo Debug plugin created: tc_markdown_lister_debug.wlx
        echo Debug version logs to: %%TEMP%%\tc_markdown_lister.log
    )
    echo.
    echo Installation:
    echo 1. Copy tc_markdown_lister.wlx to Total Commander's plugin directory
    echo 2. In Total Commander: Configuration ^> Options ^> Plugins ^> Lister ^> Configure
    echo 3. Add the plugin and configure file associations for .md files
    echo.
    echo Usage:
    echo   build_plugin_fixed.bat        - Build release version (no logging)
    echo   build_plugin_fixed.bat debug  - Build both release and debug versions
    echo.
    echo The plugin will preview markdown files in Total Commander's lister.
) else (
    echo.
    echo Build failed!
    exit /b 1
)

rem --- Restore original environment ---
endlocal & set "PATH=%_ORIG_PATH%" ^& set "INCLUDE=%_ORIG_INCLUDE%" ^& set "LIB=%_ORIG_LIB%" ^& set "LIBPATH=%_ORIG_LIBPATH%"