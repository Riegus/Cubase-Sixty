# Development helper: restart the whole Cubase-Sixty chain in the right order.
#
#   powershell -ExecutionPolicy Bypass -File cubase\tools\dev-restart.ps1 -Project "D:\Music\Test\Test.cpr"
#
# Steps: close Cubase -> stop the bridge -> (optionally) rebuild it -> install the MIDI Remote scripts
#        -> start the bridge -> start Cubase with the project.
#
# Options:
#   -Project <file.cpr>   The project to open. Default: the environment variable CUBASE_SIXTY_PROJECT.
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
    $quitSent = $false
    $started = Get-Date
    $deadline = $started.AddSeconds(60)
    while ((Get-Date) -lt $deadline -and (Get-CubaseProcesses)) {
        if (-not $quitSent -and ((Get-Date) - $started).TotalSeconds -gt 6) {
            $quitSent = $true
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
}
Start-Sleep -Seconds 1

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
if ($bridgeArgs.Count -gt 0) {
    Start-Process -FilePath $bridgeExe -ArgumentList $bridgeArgs -WorkingDirectory (Split-Path $bridgeExe)
} else {
    Start-Process -FilePath $bridgeExe -WorkingDirectory (Split-Path $bridgeExe)
}
Start-Sleep -Seconds 4     # let it open the surfaces and create the ports before Cubase looks for them

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
    Start-Process -FilePath $Project
} else {
    Write-Warning 'No -Project given (and CUBASE_SIXTY_PROJECT is not set): starting Cubase without a project.'
    Start-Process -FilePath $CubaseExe
}
Write-Host 'Done.'
