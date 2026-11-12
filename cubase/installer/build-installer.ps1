# Builds the Cubase-Sixty installer: cubase\dist\CubaseSixty-Setup-<version>.exe
#
#   powershell -ExecutionPolicy Bypass -File cubase\installer\build-installer.ps1
#   powershell -ExecutionPolicy Bypass -File cubase\installer\build-installer.ps1 -NoBuild     (use the bridge as built)
#
# Needs Inno Setup 6 (https://jrsoftware.org/isdl.php; free) and a configured bridge build (cubase\bridge\build, see
# cubase\bridge\README.md). The version comes from cubase\VERSION.

param([switch]$NoBuild)

$ErrorActionPreference = 'Stop'
$cubase = Split-Path -Parent $PSScriptRoot                     # ...\cubase
$bridge = Join-Path $cubase 'bridge'
$release = Join-Path $bridge 'build\Release'
$version = (Get-Content (Join-Path $cubase 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "cubase\VERSION should be like 0.1.0, found: '$version'" }

$iscc = @(
    (Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source),
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 was not found. Install it from https://jrsoftware.org/isdl.php and run this again.' }

if (-not $NoBuild) {
    Write-Host "Building the bridge and the tray app (Release)..."
    & cmake --build (Join-Path $bridge 'build') --config Release
    if ($LASTEXITCODE -ne 0) { throw 'Build failed. (Is the bridge or the tray app still running? Quit Cubase-Sixty from its tray icon first.)' }
}
foreach ($f in 'CubaseSixty.exe', 'cubase_sixty_bridge.exe', 'libusb-1.0.dll') {
    if (-not (Test-Path (Join-Path $release $f))) { throw "Missing $release\$f" }
}

$out = Join-Path $cubase 'dist'
New-Item -ItemType Directory -Force -Path $out | Out-Null
Write-Host "Making the installer, version $version..."
& $iscc "/DAppVersion=$version" "/DBuildDir=$release" "/DOutDir=$out" (Join-Path $PSScriptRoot 'CubaseSixty.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup failed.' }
Write-Host ""
Write-Host "Done: $out\CubaseSixty-Setup-$version.exe"
