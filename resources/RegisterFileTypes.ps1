[CmdletBinding()]
param(
    [switch]$Associate,
    [switch]$Unregister,
    [switch]$Restore
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $here 'Prism.exe'
$iconsSrc = Join-Path $here 'icons'
$appData = Join-Path $env:LOCALAPPDATA 'Prism'
$iconsDst = Join-Path $appData 'icons'
$backupPath = Join-Path $appData 'assoc-backup.json'

if (-not (Test-Path $exe)) { throw "Prism.exe not found next to this script: $exe" }
New-Item -ItemType Directory -Path $appData -Force | Out-Null

function EnsureKey([string]$path) {
    if (-not (Test-Path $path)) { New-Item -Path $path -Force | Out-Null }
}
function SetValue([string]$path, [string]$name, [string]$value) {
    if ([string]::IsNullOrEmpty($name)) { $name = '(default)' }
    EnsureKey $path
    New-ItemProperty -Path $path -Name $name -Value $value -PropertyType String -Force | Out-Null
}
function RemoveTree([string]$path) {
    if (Test-Path $path) { Remove-Item -Path $path -Recurse -Force }
}
function NotifyShell() {
    Add-Type -Namespace Win32 -Name ShellNotify -MemberDefinition '[DllImport("shell32.dll")] public static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);' -ErrorAction SilentlyContinue
    [Win32.ShellNotify]::SHChangeNotify(0x08000000, 0x1000, [IntPtr]::Zero, [IntPtr]::Zero)
    $ie = Join-Path $env:SystemRoot 'System32\ie4uinit.exe'
    if (Test-Path $ie) { & $ie -show 2>$null | Out-Null }
}

if ($Restore -or $Unregister) {
    RemoveTree 'HKCU:\Software\Classes\Prism.Player'
    RemoveTree 'HKCU:\Software\Classes\Applications\Prism.exe'
    $ra = 'HKCU:\Software\RegisteredApplications'
    if (Test-Path $ra) { Remove-ItemProperty -Path $ra -Name 'Prism' -ErrorAction SilentlyContinue }
    if (Test-Path $backupPath) {
        $backup = Get-Content $backupPath -Raw -Encoding UTF8 | ConvertFrom-Json
        foreach ($p in $backup.PSObject.Properties) {
            $ext = $p.Name
            RemoveTree "HKCU:\Software\Classes\Prism.$ext"
            $k = "HKCU:\Software\Classes\.$ext"
            if ($p.Value.Default) { SetValue $k '' $p.Value.Default }
            if ($p.Value.Icon) { SetValue (Join-Path $k 'DefaultIcon') '' $p.Value.Icon }
        }
        Write-Host "Restored associations for $($backup.PSObject.Properties.Count) formats." -ForegroundColor Green
    } else {
        Write-Host "No assoc-backup.json found; only Prism ProgIDs were removed." -ForegroundColor Yellow
    }
    NotifyShell
    exit 0
}

if (-not (Test-Path $iconsSrc)) { throw "icons folder not found: $iconsSrc" }
$exts = @(Get-ChildItem $iconsSrc -Filter *.ico | ForEach-Object { $_.BaseName.ToLower() } | Sort-Object)
if ($exts.Count -eq 0) { throw "No .ico files in $iconsSrc" }

New-Item -ItemType Directory -Path $iconsDst -Force | Out-Null
Copy-Item (Join-Path $iconsSrc '*.ico') $iconsDst -Force

if ($Associate -and -not (Test-Path $backupPath)) {
    $backup = @{}
    foreach ($ext in $exts) {
        $k = "HKCU:\Software\Classes\.$ext"
        $def = $null; $ico = $null
        if (Test-Path $k) {
            $def = (Get-ItemProperty -Path $k -Name '(default)' -ErrorAction SilentlyContinue).'(default)'
            $ik = Join-Path $k 'DefaultIcon'
            if (Test-Path $ik) { $ico = (Get-ItemProperty -Path $ik -Name '(default)' -ErrorAction SilentlyContinue).'(default)' }
        }
        $backup[$ext] = @{ Default = $def; Icon = $ico }
    }
    $backup | ConvertTo-Json -Depth 4 | Set-Content -Path $backupPath -Encoding UTF8
    Write-Host "Backed up existing associations to $backupPath" -ForegroundColor DarkGray
}

$prog = 'HKCU:\Software\Classes\Prism.Player'
SetValue $prog '' 'Prism Player'
SetValue $prog 'FriendlyTypeName' 'Prism media file'
SetValue (Join-Path $prog 'DefaultIcon') '' "$exe,0"
$cmd = '"' + $exe + '" "%1"'
SetValue (Join-Path $prog 'shell\open\command') '' $cmd

$cap = Join-Path $prog 'Capabilities'
SetValue $cap 'ApplicationName' 'Prism'
SetValue $cap 'ApplicationDescription' 'Prism media player'
SetValue $cap 'ApplicationIcon' "$exe,0"
foreach ($ext in $exts) {
    SetValue (Join-Path $cap 'FileAssociations') ".$ext" "Prism.$ext"
}
SetValue 'HKCU:\Software\RegisteredApplications' 'Prism' 'Software\Classes\Prism.Player\Capabilities'

$app = 'HKCU:\Software\Classes\Applications\Prism.exe'
SetValue $app 'FriendlyAppName' 'Prism'
SetValue (Join-Path $app 'shell\open\command') '' $cmd
foreach ($ext in $exts) { SetValue (Join-Path $app 'SupportedTypes') ".$ext" '' }

foreach ($ext in $exts) {
    $p = "HKCU:\Software\Classes\Prism.$ext"
    SetValue $p '' "Prism $($ext.ToUpper()) media"
    $icon = (Join-Path $iconsDst "$ext.ico") + ',0'
    SetValue (Join-Path $p 'DefaultIcon') '' $icon
    SetValue (Join-Path $p 'shell\open\command') '' $cmd
    SetValue "HKCU:\Software\Classes\.$ext\OpenWithProgids" "Prism.$ext" ''
}

$mismatch = @()
if ($Associate) {
    foreach ($ext in $exts) {
        $k = "HKCU:\Software\Classes\.$ext"
        SetValue $k '' "Prism.$ext"
        $icon = (Join-Path $iconsDst "$ext.ico") + ',0'
        SetValue (Join-Path $k 'DefaultIcon') '' $icon
        $uc = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.$ext\UserChoice"
        if (Test-Path $uc) {
            $cur = (Get-ItemProperty -Path $uc -Name 'ProgId' -ErrorAction SilentlyContinue).ProgId
            if ($cur -and $cur -ne "Prism.$ext") { $mismatch += ".$ext -> $cur" }
        }
    }
}

NotifyShell
Write-Host "Registered Prism for $($exts.Count) media formats." -ForegroundColor Green
Write-Host "Icons: $iconsDst" -ForegroundColor DarkGray
if ($Associate) {
    Write-Host "Set HKCU defaults and per-format icons." -ForegroundColor Green
    if ($mismatch.Count -gt 0) {
        Write-Host ""
        Write-Host "Windows still has UserChoice for these extensions:" -ForegroundColor Yellow
        $mismatch | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
        Write-Host "Open Settings > Apps > Default apps > Prism and confirm." -ForegroundColor Yellow
        Start-Process 'ms-settings:defaultapps?registeredAppUser=Prism' -ErrorAction SilentlyContinue
    } else {
        Write-Host "No conflicting UserChoice entries found." -ForegroundColor Green
    }
} else {
    Write-Host "Use -Associate to set defaults/icons, or pick Prism under Settings > Default apps." -ForegroundColor Cyan
}
