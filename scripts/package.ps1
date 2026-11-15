# Builds, tests and writes artifacts\release\mdview-<version>.zip
# (mdview.exe, mdview.wlx64, pluginst.inf for Total Commander auto-install, README, licenses).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
& cmd.exe /c "`"$root\build.bat`""
if ($LASTEXITCODE -ne 0) { throw "build/test failed" }

$version = (Select-String -Path "$root\CMakeLists.txt" -Pattern 'project\(mdview VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$stage = Join-Path $root "artifacts\release\mdview-$version"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force "$stage\licenses" | Out-Null

Copy-Item "$root\build\mdview.exe", "$root\build\mdview.wlx64" $stage
Copy-Item "$root\release\pluginst.inf", "$root\release\README.md" $stage
Copy-Item "$root\third_party\md4c\LICENSE.md" "$stage\licenses\md4c-LICENSE.md"
Copy-Item "$root\third_party\litehtml\LICENSE" "$stage\licenses\litehtml-LICENSE.txt"
$gumboLicense = Get-ChildItem "$root\third_party\litehtml\src\gumbo" -Filter 'LICENSE*' | Select-Object -First 1
if ($gumboLicense) { Copy-Item $gumboLicense.FullName "$stage\licenses\gumbo-LICENSE.txt" }

$zip = "$stage.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Get-Item $zip | Select-Object FullName, Length
