# Installs the WinUSB driver binding for the SSL UF8, UC1 and UF1, so that Cubase-Sixty's bridge (libusb) can open them.
# Run elevated (the installer and the tray app do: one administrator prompt). Rea-Sixty's install script, unchanged
# but for where the INF comes from: the same package (rea_sixty_winusb.inf) and the same signing certificate
# ("Rea-Sixty Driver Signer"), so a PC that has Rea-Sixty's driver already ends up with exactly one copy.
# After it: unplug and replug the controllers. SSL 360 no longer sees them; uninstall-driver.ps1 (or reinstalling
# SSL 360) gives them back. Log: %TEMP%\rea_sixty_winusb_install.log. Exit code 0 = installed.

$ErrorActionPreference = 'Stop'

# Everything this script does is transcribed. Before this, a throw closed the
# hidden PowerShell window with no trace, so neither the user nor we could tell
# whether the driver had installed — the whole reason creal's catalog failure
# went unseen for as long as it did. The transcript is the artifact he can paste.
$log = Join-Path $env:TEMP 'rea_sixty_winusb_install.log'
try { Start-Transcript -Path $log -Force | Out-Null } catch { }
try {

# Cubase-Sixty: the INF ships beside this script; it is copied into a working folder of its own (only the INF may be
# in there, see the catalog note below).
$wd = Join-Path $env:TEMP 'rea_sixty_winusb'
New-Item -ItemType Directory -Force -Path $wd | Out-Null
$inf = Join-Path $wd 'rea_sixty_winusb.inf'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'rea_sixty_winusb.inf') -Destination $inf -Force
if (-not (Test-Path -LiteralPath $inf)) { throw "INF missing at $inf" }

# Step 1+2: reuse or mint cert, then trust it.
$subject = 'CN=Rea-Sixty Driver Signer'
$cert = Get-ChildItem Cert:\LocalMachine\My |
        Where-Object { $_.Subject -eq $subject } |
        Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert -Subject $subject `
        -KeyUsage DigitalSignature `
        -CertStoreLocation 'Cert:\LocalMachine\My' `
        -KeyExportPolicy Exportable -NotAfter ((Get-Date).AddYears(5))
    $certPath = Join-Path $env:TEMP 'rea_sixty_signer.cer'
    Export-Certificate -Cert $cert -FilePath $certPath -Force | Out-Null
    Import-Certificate -FilePath $certPath -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Import-Certificate -FilePath $certPath -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
}

# Step 3: catalog + signature.
# ⛔ The catalog MUST NOT live inside $wd. New-FileCatalog hashes every file
# under -Path, so a .cat written in there is hashed by the NEXT run and the
# cmdlet throws "Unable to create the hash for file ...rea_sixty_winusb.cat".
# With $ErrorActionPreference = 'Stop' the script dies right there, before
# pnputil ever runs, and the window closes before anyone can read it. First run
# looked fine because the folder held only the INF — so every SECOND and later
# attempt on a user's machine was a silent no-op. Forum user creal lost his UF1
# to this and nearly sold it (2026-08-13).
# The signed CAT is referenced by CatalogFile= in the INF, so pnputil needs it
# NEXT TO the INF at /add-driver time: build it outside, then copy it in, and
# remove it again so the folder is clean for the next run.
$cat = Join-Path $env:TEMP 'rea_sixty_winusb.cat'
Remove-Item -LiteralPath $cat -Force -ErrorAction SilentlyContinue
# Self-healing, and NOT a duplicate of the C++ staging cleanup: a machine
# poisoned by an older build has a .cat sitting in $wd already, and if the
# staging delete failed for any reason the very first hash here would throw
# again. Clearing it here means the script repairs itself no matter how it
# was launched.
Get-ChildItem -LiteralPath $wd -Filter *.cat -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue
New-FileCatalog -Path $wd -CatalogFilePath $cat -CatalogVersion 2 | Out-Null
$sig = Set-AuthenticodeSignature -FilePath $cat -Certificate $cert -HashAlgorithm SHA256
if ($sig.Status -ne 'Valid') { throw "CAT signature failed: $($sig.Status)" }

# Step 4: add our package to the store. Do NOT delete SSL's sslbus.inf:
# it is the shared bus driver for the whole SSL family (UF8/UC1 AND UF1 +
# O-Series), and removing it breaks the UF1 in SSL 360 with Code 28.
# We coexist with it and override only our two device interfaces below.
# The INF's CatalogFile= names rea_sixty_winusb.cat as a sibling, so put the
# signed catalog beside it for the duration of the call and take it out again.
$catInWd = Join-Path $wd 'rea_sixty_winusb.cat'
Copy-Item -LiteralPath $cat -Destination $catInWd -Force
try   { pnputil /add-driver $inf | Out-Null }
finally {
    # ALWAYS, including on a throw: a catalog left behind here is exactly what
    # made the next run fail.
    Remove-Item -LiteralPath $catInWd -Force -ErrorAction SilentlyContinue
}

# Resolve the staged published name (oemNN.inf) of our just-added package.
# Walk enum-drivers line by line: "Published Name" precedes "Original Name"
# inside each block, so track the last Published Name and grab it when we
# reach our Original Name. (A lazy cross-entry regex mis-captures a foreign
# oem, e.g. the first Published Name in the listing.)
$ourInf = $inf
$pub = $null
foreach ($line in (pnputil /enum-drivers)) {
    if ($line -match 'Published Name:\s+(oem\d+\.inf)') { $pub = $matches[1] }
    elseif ($line -match 'Original Name:\s+rea_sixty_winusb\.inf') {
        if ($pub) { $ourInf = Join-Path $env:WINDIR ('INF\' + $pub) }
        break
    }
}

# Step 5: force-bind ONLY UF8 (PID_0021) + UC1 (PID_0023) to our WinUSB
# INF. pnputil /install uses rank-based selection and will not swap a
# device already working on sslbus, so go through newdev's
# UpdateDriverForPlugAndPlayDevices with INSTALLFLAG_FORCE (the mechanism
# Zadig uses) which overrides the current binding for a hardware id.
# UF8/UC1/UF1 are force-bound; O-Series ids are never named -> they stay on
# sslbus. sslbus is left installed, so O-Series keeps working; only the UF1
# interface moves to WinUSB (SSL 360 then stops seeing the UF1, as intended).
$nd = @'
using System;
using System.Runtime.InteropServices;
public static class RsNewDev {
  [DllImport("newdev.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern bool UpdateDriverForPlugAndPlayDevices(
    IntPtr hwndParent, string HardwareId, string FullInfPath,
    uint InstallFlags, out bool bRebootRequired);
}
'@
Add-Type -TypeDefinition $nd
$INSTALLFLAG_FORCE = 0x1
# NOTE: only devices PRESENT right now get force-bound. A UF8/UC1 that is
# first plugged in AFTER this runs goes through normal PnP ranking; with
# sslbus in the store it may land on sslbus, so the user re-runs this with
# the device attached. (The old delete-sslbus path auto-bound late hotplugs
# but at the cost of breaking UF1 — this trade is deliberate.)
foreach ($hwid in @('USB\VID_31E9&PID_0021','USB\VID_31E9&PID_0023','USB\VID_31E9&PID_0025')) {
    $reboot = $false
    try {
        [void][RsNewDev]::UpdateDriverForPlugAndPlayDevices(
            [IntPtr]::Zero, $hwid, $ourInf, $INSTALLFLAG_FORCE, [ref]$reboot)
    } catch { }
    # An absent device is a benign no-op (returns FALSE, no swap); only a
    # currently-attached UF8/UC1/UF1 is rebound to WinUSB.
}

# Step 6: keep exactly ONE copy of our package in the driver store.
# `pnputil /add-driver` stages a NEW oemNN on every call, so each press of the
# install button left another copy behind — measured on the StoerPC 2026-08-14,
# four runs turned oem2 into oem2 + oem13. That is how users end up with the
# duplicates they cannot remove: the uninstall path resolves ONE oemNN, so the
# extras are unreachable from the UI and regedit is not a supported way out
# (forum user creal had exactly this, 2026-08-13).
#
# Delete only packages NO DEVICE IS USING: read each device's actual
# DriverInfPath and keep those. Plain /delete-driver, never /uninstall /force —
# an unused package deletes cleanly, and refusing to touch a bound one is the
# whole safety property here.
try {
    $inUse = @{}
    foreach ($devpid in @('PID_0021','PID_0023','PID_0025')) {
        Get-PnpDevice -EA SilentlyContinue |
            Where-Object { $_.InstanceId -like "USB\VID_31E9&$devpid*" } |
            ForEach-Object {
                $p = (Get-PnpDeviceProperty -InstanceId $_.InstanceId `
                        -KeyName DEVPKEY_Device_DriverInfPath -EA SilentlyContinue).Data
                if ($p) { $inUse[$p.ToLower()] = $true }
            }
    }
    $ours = @()
    $pub2 = $null
    foreach ($line in (pnputil /enum-drivers)) {
        if ($line -match 'Published Name:\s+(oem\d+\.inf)') { $pub2 = $matches[1] }
        elseif ($line -match 'Original Name:\s+rea_sixty_winusb\.inf') {
            if ($pub2) { $ours += $pub2 }
        }
    }
    # Never leave zero: if nothing is bound yet (no device attached), keep the
    # newest and drop the rest, so a device plugged in later still finds one.
    if ($ours.Count -gt 1 -and $inUse.Count -eq 0) {
        $keep = ($ours | Sort-Object { [int]($_ -replace '\D','') })[-1]
        $inUse[$keep.ToLower()] = $true
    }
    foreach ($o in $ours) {
        if (-not $inUse.ContainsKey($o.ToLower())) {
            pnputil /delete-driver $o 2>&1 | Out-Null
        }
    }
} catch { }

Write-Output 'REASIXTY_RESULT=OK'
}
catch {
    # The exit code is what the caller reports as installed / failed, so a throw
    # must not leave it at 0. The message goes into the transcript too.
    Write-Output ('REASIXTY_RESULT=FAIL ' + $_.Exception.Message)
    try { Stop-Transcript | Out-Null } catch { }
    exit 1
}
try { Stop-Transcript | Out-Null } catch { }
exit 0
