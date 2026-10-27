# Build Prism: MSVC (x64) + Ninja.  Usage:  .\build.ps1 [-Clean] [-Dist]
param([switch]$Clean, [switch]$Dist, [switch]$Fast)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$build = Join-Path $root 'build'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vsPath) { $vsPath = 'D:\DevTools\VS2022' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found under $vsPath" }

if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }

$cmd = '"' + $vcvars + '" >nul && set VSLANG=1033 && cmake -S "' + $root + '" -B "' + $build + '" -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build "' + $build + '"' + $(if ($Fast) { '' } else { ' --clean-first' })
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "build failed with exit code $LASTEXITCODE" }

$exe = Join-Path $build 'bin\Prism.exe'
Write-Host "`nBuilt: $exe" -ForegroundColor Green

if ($Dist) {
    $distDir = Join-Path $root 'dist\Prism'
    if (Test-Path $distDir) { Remove-Item $distDir -Recurse -Force }
    New-Item -ItemType Directory -Path $distDir -Force | Out-Null
    Copy-Item (Join-Path $build 'bin\Prism.exe') $distDir -Force
    Copy-Item (Join-Path $build 'bin\*.dll') $distDir -Force
    Copy-Item (Join-Path $root 'README.md') $distDir -Force
    Copy-Item (Join-Path $root 'THIRD_PARTY_NOTICES.md') $distDir -Force
    if (Test-Path (Join-Path $root 'resources\file-icons')) {
        $iconDist = Join-Path $distDir 'icons'
        New-Item -ItemType Directory -Path $iconDist -Force | Out-Null
        Copy-Item (Join-Path $root 'resources\file-icons\*.ico') $iconDist -Force
    }
    if (Test-Path (Join-Path $root 'resources\RegisterFileTypes.ps1')) {
        Copy-Item (Join-Path $root 'resources\RegisterFileTypes.ps1') $distDir -Force
    }
    if (Test-Path (Join-Path $root 'docs')) { Copy-Item (Join-Path $root 'docs') $distDir -Recurse -Force }
    if (Test-Path (Join-Path $root 'samples')) { Copy-Item (Join-Path $root 'samples') $distDir -Recurse -Force }
    $lic = Join-Path $root '_vendor\ffmpeg\ffmpeg-master-latest-win64-gpl-shared\LICENSE.txt'
    if (Test-Path $lic) { Copy-Item $lic (Join-Path $distDir 'LICENSE-FFMPEG.txt') -Force }
    $assLic = Join-Path $root '_vendor\libass\LICENSE-libass.txt'
    if (Test-Path $assLic) { Copy-Item $assLic (Join-Path $distDir 'LICENSE-LIBASS.txt') -Force }
    $assLicDir = Join-Path $root '_vendor\libass\licenses'
    if (Test-Path $assLicDir) {
        New-Item -ItemType Directory -Path (Join-Path $distDir 'licenses\libass') -Force | Out-Null
        Copy-Item (Join-Path $assLicDir '*') (Join-Path $distDir 'licenses\libass') -Force
    }

    $launcher = @"
@echo off
start "" "%~dp0Prism.exe"
"@
    Set-Content -Path (Join-Path $distDir 'start Prism.cmd') -Value $launcher -Encoding ASCII

    $shortcut = @"
@echo off
powershell -NoProfile -Command "`$ws=New-Object -ComObject WScript.Shell; `$lnk=`$ws.CreateShortcut([Environment]::GetFolderPath('Desktop')+'\Prism.lnk'); `$lnk.TargetPath='%~dp0Prism.exe'; `$lnk.WorkingDirectory='%~dp0'; `$lnk.IconLocation='%~dp0Prism.exe'; `$lnk.Save()"
echo Desktop shortcut created.
pause
"@
    Set-Content -Path (Join-Path $distDir 'create desktop shortcut.cmd') -Value $shortcut -Encoding ASCII

    $total = (Get-ChildItem $distDir -Recurse -File | Measure-Object Length -Sum).Sum
    Write-Host ("Dist:  {0}  ({1:N0} MB)" -f $distDir, ($total / 1MB)) -ForegroundColor Green
}
