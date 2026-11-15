@echo off
rem Configure, build (Release) and test with Visual Studio 2022's bundled CMake + Ninja.
rem Outputs: build\mdview.exe, build\mdview.wlx64 (TC / Double Commander lister plugin)
setlocal
set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
if not exist "%VSROOT%\Common7\Tools\VsDevCmd.bat" (
  echo Visual Studio 2022 C++ tools not found. Use CMake from a developer prompt.
  exit /b 1
)
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo >nul
set "CMAKEBIN=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%CMAKEBIN%\CMake\bin;%CMAKEBIN%\Ninja;%PATH%"
pushd "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :fail
cmake --build build
if errorlevel 1 goto :fail
ctest --test-dir build --output-on-failure
if errorlevel 1 goto :fail
popd
exit /b 0
:fail
popd
exit /b 1
