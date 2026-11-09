# Development helper: restart the whole Cubase-Sixty chain in the right order.
#
#   powershell -ExecutionPolicy Bypass -File cubase\tools\dev-restart.ps1 -Project "D:\Music\Test\Test.cpr"
#
# Steps: close Cubase -> stop the bridge -> (optionally) rebuild it -> install the MIDI Remote scripts
#        -> start the bridge -> start Cubase with the project.
#
# Options:
#   -Project <file.cpr>   The project to open. Default: the environment variable CUBASE_SIXTY_PROJECT,
#                         else the first line of cubase\tools\project.txt.
#   -CubaseExe <path>     Cubase's executable. Default: the newest Cubase*.exe under Program Files\Steinberg.
#   -Build                Rebuild the bridge first (cmake --build, Release).
#   -Trace                Start the bridge with --trace.
#   -NoScriptLog          Start the bridge with --no-script-log (the scripts' lines are not written to cubase\logs\scripts.log).
#   -Force                If Cubase does not close within 60 seconds, kill it. Unsaved changes are LOST.
#   -NoStartCubase        Do everything except start Cubase.
#   -BridgeOnly           Leave Cubase running: stop the bridge, (with -Build) rebuild it, start it again on ports
#                         made in loopMIDI (--loopmidi). Needs the ten Cubase-Sixty ports created once in loopMIDI
#                         and Cubase started while they existed (see cubase\docs\development.md).
#   -Tray                 Start the bridge through the tray app (CubaseSixty.exe: no console, restarts the bridge if
#                         it stops) instead of in a console window.
#   -ScriptsOnly          Only install the MIDI Remote scripts; then reload them in Cubase (MIDI Remote: Reload
#                         Scripts). Cubase and the bridge keep running.

param(
    [string]$Project = $env:CUBASE_SIXTY_PROJECT,
    [string]$CubaseExe,
    [switch]$Build,
    [switch]$Trace,
    [switch]$NoScriptLog,
    [switch]$Force,
    [switch]$NoStartCubase,
    [switch]$BridgeOnly,
    [switch]$Tray,
    [switch]$ScriptsOnly
)

$ErrorActionPreference = 'Stop'

# Project fallback: a one-line text file next to this script (cubase\tools\project.txt, not committed) holding the .cpr path.
if (-not $Project) {
    $projectFile = Join-Path $PSScriptRoot 'project.txt'
    if (Test-Path $projectFile) { $Project = (Get-Content $projectFile -TotalCount 1).Trim().Trim('"') }
}
$repo   = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)          # ...\Cubase-Sixty
$bridgeDir = Join-Path $repo 'cubase\bridge'
$bridgeExe = Join-Path $bridgeDir 'build\Release\cubase_sixty_bridge.exe'

if ($ScriptsOnly) {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $repo 'cubase\midi-remote\tools\install-scripts.ps1')
    Write-Host 'Scripts installed. In Cubase: MIDI Remote (lower zone) > Reload Scripts.'
    return
}

# --- 1. Close Cubase (politely: Cubase asks about unsaved changes itself) ---------------------------------
function Get-CubaseProcesses {
    Get-Process | Where-Object { $_.ProcessName -like 'Cubase*' -and $_.ProcessName -notlike '*Sixty*' }
}
$cubase = Get-CubaseProcesses
if ($cubase -and -not $BridgeOnly) {
    Write-Host 'Closing Cubase... (answer any "save changes?" dialog in Cubase)'
    # Quit with Ctrl+Q straight away. (Closing the main window first closed the project and left Cubase
    # showing the Hub before it quit.)
    $lastQuit = $null
    $started = Get-Date
    $deadline = $started.AddSeconds(60)
    while ((Get-Date) -lt $deadline -and (Get-CubaseProcesses)) {
        if (((Get-Date) - $started).TotalSeconds -gt 15 -and -not (Get-CubaseProcesses | Where-Object { $_.MainWindowHandle -ne 0 })) { break }
        # Send Quit now, and again every 8 seconds (one can land while Cubase is busy or not focused).
        if (-not $lastQuit -or ((Get-Date) - $lastQuit).TotalSeconds -gt 8) {
            $lastQuit = Get-Date
            # Only a process with a window can be activated (Cubase can leave a window-less process behind).
            $p = Get-CubaseProcesses | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
            if ($p) {
                Add-Type -AssemblyName Microsoft.VisualBasic
                Add-Type -AssemblyName System.Windows.Forms
                try {
                    [Microsoft.VisualBasic.Interaction]::AppActivate($p.Id)
                    Start-Sleep -Milliseconds 400
                    [System.Windows.Forms.SendKeys]::SendWait('^q')
                } catch { Write-Warning "Could not send Quit to Cubase: $_" }
            }
        }
        Start-Sleep -Milliseconds 500
    }
    $left = Get-CubaseProcesses
    # A Cubase process without any window cannot be showing a "save changes?" dialog: it is left over from
    # a shutdown that did not finish, so it is safe to end it.
    $windowless = @($left | Where-Object { $_.MainWindowHandle -eq 0 })
    if ($left -and $windowless.Count -eq @($left).Count) {
        Write-Warning ('Cubase is still running without a window (' + (($left | ForEach-Object { $_.ProcessName + ' ' + $_.Id }) -join ', ') + '); ending it.')
        $left | Stop-Process -Force
        Start-Sleep -Seconds 2
        $left = Get-CubaseProcesses
    }
    if ($left) {
        if ($Force) { Write-Warning 'Cubase did not close; killing it.'; $left | Stop-Process -Force; Start-Sleep -Seconds 2 }
        else { throw 'Cubase is still running (a dialog may be waiting). Close it, or run again with -Force.' }
    }
}

# --- 2. Stop the bridge --------------------------------------------------------------------------------
# The tray app first: it would start the bridge again.
Get-Process -Name 'CubaseSixty' -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host 'Stopping the tray app...'
    $_ | Stop-Process -Force
}
Get-Process -Name 'cubase_sixty_bridge' -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host 'Stopping the bridge...'
    $_ | Stop-Process -Force
    try { $_ | Wait-Process -Timeout 10 } catch { }
}
# Give Windows time to release the USB devices and remove the bridge's virtual MIDI ports; starting the
# next bridge too soon made it fail to open them.
Start-Sleep -Seconds 3

# --- 3. Rebuild (optional) -----------------------------------------------------------------------------
if ($Build) {
    Write-Host 'Building the bridge...'
    & cmake --build (Join-Path $bridgeDir 'build') --config Release
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
}
if (-not (Test-Path $bridgeExe)) { throw "Bridge not found: $bridgeExe (build it first, or use -Build)." }

# --- 4. Install the MIDI Remote scripts ----------------------------------------------------------------
if (-not $BridgeOnly) { & powershell -ExecutionPolicy Bypass -File (Join-Path $repo 'cubase\midi-remote\tools\install-scripts.ps1') }

# --- 5. Start the bridge in its own window --------------------------------------------------------------
$bridgeArgs = @()
if ($Trace) { $bridgeArgs += '--trace' }
if ($NoScriptLog) { $bridgeArgs += '--no-script-log' }
# With the ten Cubase-Sixty ports made in loopMIDI (for -BridgeOnly) the bridge always uses those ports, so Cubase
# sees the same ports whichever way the bridge was started.
# (cubase\tools\use-loopmidi.txt, not committed, says the ports have been made there.)
$useLoopMidi = Test-Path (Join-Path $PSScriptRoot 'use-loopmidi.txt')
if ($BridgeOnly -and -not $useLoopMidi) { throw '-BridgeOnly needs the ports made in loopMIDI and cubase\tools\use-loopmidi.txt (see cubase\docs\development.md).' }
if ($useLoopMidi -and -not (Get-Process -Name 'loopMIDI' -ErrorAction SilentlyContinue)) { throw 'cubase\tools\use-loopmidi.txt is there but loopMIDI is not running: start loopMIDI first.' }
if ($useLoopMidi) { $bridgeArgs += '--loopmidi' }
# Start it in its own window. Now and then it exits straight away when started right after the previous one
# (a surface or a port not released yet), so give it up to three starts before giving up.
$bridgeUp = $false
for ($try = 1; $try -le 3 -and -not $bridgeUp; $try++) {
    if ($try -gt 1) { Write-Warning "The bridge exited at start-up; starting it again ($try of 3)..."; Start-Sleep -Seconds 3 }
    else { Write-Host 'Starting the bridge...' }
    $exe = if ($Tray) { Join-Path (Split-Path $bridgeExe) 'CubaseSixty.exe' } else { $bridgeExe }
    if ($bridgeArgs.Count -gt 0) {
        Start-Process -FilePath $exe -ArgumentList $bridgeArgs -WorkingDirectory (Split-Path $bridgeExe)
    } else {
        Start-Process -FilePath $exe -WorkingDirectory (Split-Path $bridgeExe)
    }
    Start-Sleep -Seconds 4     # let it open the surfaces and create the ports before Cubase looks for them
    $bridgeUp = [bool](Get-Process -Name 'cubase_sixty_bridge' -ErrorAction SilentlyContinue)
}
if (-not $bridgeUp) {
    throw 'The bridge did not stay up after three starts (start cubase\bridge\build\Release\cubase_sixty_bridge.exe by hand to see why). Cubase not started.'
}

# --- 6. Start Cubase ------------------------------------------------------------------------------------
if ($NoStartCubase) { Write-Host 'Done (Cubase not started).'; return }
if ($BridgeOnly) { Write-Host 'Done (bridge restarted on the loopMIDI ports; Cubase left running).'; return }

if (-not $CubaseExe) {
    $CubaseExe = Get-ChildItem -Path "$env:ProgramFiles\Steinberg" -Recurse -Filter 'Cubase*.exe' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^Cubase\d*\.exe$' } |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $CubaseExe -or -not (Test-Path $CubaseExe)) { throw 'Cubase executable not found. Pass -CubaseExe "<path>\Cubase15.exe".' }

Write-Host "Starting Cubase: $CubaseExe"
if ($Project) {
    if (-not (Test-Path $Project)) { throw "Project not found: $Project" }
    # Open the file like a double-click does; that loads the project directly. (Passing the path to Cubase's exe showed the Hub.)
    Write-Host "Opening project: $Project"
    Start-Process -FilePath $Project
} else {
    Write-Warning 'No project found (no -Project, no CUBASE_SIXTY_PROJECT, no cubase\tools\project.txt): starting Cubase without a project, so it shows the Hub.'
    Start-Process -FilePath $CubaseExe
}
Write-Host 'Done.'
