@echo off
rem Builds the test harness (fake TC lister host + maddy benchmarks) into tests\harness\out
rem Usage: tests\harness\build_harness.bat
rem Then:  out\lister_host.exe ..\..\tc_markdown_lister_debug.wlx out\big.md 15
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
pushd "%~dp0"
if not exist out mkdir out
cl /nologo /EHsc /O2 /Foout\ lister_host.cpp /Feout\lister_host.exe user32.lib ole32.lib || goto :fail
cl /nologo /std:c++17 /EHsc /O2 /I..\..\include /Foout\ bench_maddy.cpp /Feout\bench_maddy.exe || goto :fail
cl /nologo /std:c++17 /EHsc /O2 /I..\..\include /Foout\ bench_maddy_longline.cpp /Feout\bench_maddy_longline.exe || goto :fail
powershell -NoProfile -File gen_samples.ps1 -Dir out
popd
echo Harness built in tests\harness\out
exit /b 0
:fail
popd
echo Harness build failed
exit /b 1
