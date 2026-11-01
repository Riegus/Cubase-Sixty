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
#   -Force                If Cubase does not close within 60 seconds, kill it. Unsaved changes are LOST.
#   -NoStartCubase        Do everything except start Cubase.

param(
    [string]$Project = $env:CUBASE_SIXTY_PROJECT,
    [string]$CubaseExe,
    [switch]$Build,
    [switch]$Trace,
    [switch]$Force,
    [switch]$NoStartCubase
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

# --- 1. Close Cubase (politely: Cubase asks about unsaved changes itself) ---------------------------------
function Get-CubaseProcesses {
    Get-Process | Where-Object { $_.ProcessName -like 'Cubase*' -and $_.ProcessName -notlike '*Sixty*' }
}
$cubase = Get-CubaseProcesses
if ($cubase) {
    Write-Host 'Closing Cubase... (answer any "save changes?" dialog in Cubase)'
    $cubase | ForEach-Object { [void]$_.CloseMainWindow() }
    # Closing the last project leaves Cubase on the Hub instead of quitting: give it a few seconds, then send Quit (Ctrl+Q).
    $lastQuit = $null
    $started = Get-Date
    $deadline = $started.AddSeconds(60)
    while ((Get-Date) -lt $deadline -and (Get-CubaseProcesses)) {
        # Resend Quit every 8 seconds from the 6th on (the first one can land while Cubase is busy or not focused).
        if (((Get-Date) - $started).TotalSeconds -gt 6 -and (-not $lastQuit -or ((Get-Date) - $lastQuit).TotalSeconds -gt 8)) {
            $lastQuit = Get-Date
            $p = Get-CubaseProcesses | Select-Object -First 1
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
    if ($left) {
        if ($Force) { Write-Warning 'Cubase did not close; killing it.'; $left | Stop-Process -Force; Start-Sleep -Seconds 2 }
        else { throw 'Cubase is still running (a dialog may be waiting). Close it, or run again with -Force.' }
    }
}

# --- 2. Stop the bridge --------------------------------------------------------------------------------
Get-Process -Name 'cubase_sixty_bridge' -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host 'Stopping the bridge...'
    $_ | Stop-Process -Force
    try { $_ | Wait-Process -Timeout 10 } catch { }
}
# Close the window left over from the previous run (it stays open after the bridge exits).
Get-Process -Name 'powershell' -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowTitle -eq 'Cubase-Sixty Bridge' } | Stop-Process -Force -ErrorAction SilentlyContinue
# Give Windows time to release the USB devices and remove the bridge's virtual MIDI ports; starting the
# next bridge too soon made it fail to open them (and its window closed before the error could be read).
Start-Sleep -Seconds 3

# --- 3. Rebuild (optional) -----------------------------------------------------------------------------
if ($Build) {
    Write-Host 'Building the bridge...'
    & cmake --build (Join-Path $bridgeDir 'build') --config Release
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
}
if (-not (Test-Path $bridgeExe)) { throw "Bridge not found: $bridgeExe (build it first, or use -Build)." }

# --- 4. Install the MIDI Remote scripts ----------------------------------------------------------------
& powershell -ExecutionPolicy Bypass -File (Join-Path $repo 'cubase\midi-remote\tools\install-scripts.ps1')

# --- 5. Start the bridge in its own window --------------------------------------------------------------
$bridgeArgs = @()
if ($Trace) { $bridgeArgs += '--trace' }
Write-Host 'Starting the bridge...'
# Run it inside a PowerShell window that stays open, so an error message is still readable if it exits.
$argText = ($bridgeArgs | ForEach-Object { " $_" }) -join ''
Start-Process -FilePath 'powershell' -WorkingDirectory (Split-Path $bridgeExe) `
    -ArgumentList @('-NoExit', '-NoProfile', '-Command', "`$Host.UI.RawUI.WindowTitle = 'Cubase-Sixty Bridge'; & '$bridgeExe'$argText; Write-Host ''; Write-Host ('Bridge exited (code ' + `$LASTEXITCODE + ').')")
Start-Sleep -Seconds 4     # let it open the surfaces and create the ports before Cubase looks for them
if (-not (Get-Process -Name 'cubase_sixty_bridge' -ErrorAction SilentlyContinue)) {
    throw 'The bridge is not running (see its window for the reason). Cubase not started.'
}

# --- 6. Start Cubase ------------------------------------------------------------------------------------
if ($NoStartCubase) { Write-Host 'Done (Cubase not started).'; return }

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
