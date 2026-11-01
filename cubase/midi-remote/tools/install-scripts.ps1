# Copies the Cubase-Sixty MIDI Remote scripts into Cubase's local driver-script folder.
#
#   powershell -ExecutionPolicy Bypass -File cubase\midi-remote\tools\install-scripts.ps1
#   powershell -ExecutionPolicy Bypass -File cubase\midi-remote\tools\install-scripts.ps1 -Destination "E:\Cubase\MIDI Remote\Driver Scripts\Local"
#
# -Destination is Cubase's "Local" driver-script folder. By default it is
#   <Documents>\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local
# (Documents is looked up, so a OneDrive-redirected Documents folder works). Pass -Destination if yours is elsewhere.
# Restart Cubase (or press Reload Scripts in the MIDI Remote Script Console) afterwards.

param([string]$Destination)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot                          # ...\cubase\midi-remote

if (-not $Destination) {
    $Destination = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Steinberg\Cubase\MIDI Remote\Driver Scripts\Local'
}
if (-not (Test-Path $Destination)) {
    Write-Error ("Cubase's local driver-script folder was not found:`n  $Destination`n" +
        "Start Cubase and open the MIDI Remote Manager once so it creates the folder, or pass -Destination with the right path.")
}
$dest = Join-Path $Destination 'CubaseSixty'

$installed = 0
foreach ($device in 'UF8', 'UF1') {
    $src = Join-Path $repo "$device\CubaseSixty_$device.js"
    if (-not (Test-Path $src)) { Write-Warning "Missing $src"; continue }
    $dst = Join-Path $dest $device
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    Copy-Item $src $dst -Force
    Write-Host "Installed $device -> $dst"
    $installed++
}

# Earlier versions put the UF8 script in a folder called SSL; remove it so Cubase does not list it twice.
$old = Join-Path $dest 'SSL'
if ($installed -gt 0 -and (Test-Path $old)) {
    Remove-Item $old -Recurse -Force
    Write-Host "Removed old folder $old"
}
