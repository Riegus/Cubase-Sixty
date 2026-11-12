# Removes the WinUSB driver binding for the SSL UF8, UC1 and UF1 (every copy of rea_sixty_winusb.inf in the driver
# store) and its signing certificate. Run elevated. After it: unplug and replug the controllers; they go back to
# SSL's driver (reinstall SSL 360 if they do not). Rea-Sixty's script, unchanged. Note that Rea-Sixty uses the same
# driver: removing it takes the controllers away from Rea-Sixty too.
# Log: %TEMP%\rea_sixty_winusb_uninstall.log. Exit code 0 = removed.

$ErrorActionPreference = 'Stop'

$log = Join-Path $env:TEMP 'rea_sixty_winusb_uninstall.log'
try { Start-Transcript -Path $log -Force | Out-Null } catch { }
try {

# Remove EVERY copy of our package, not just the first. The old regex matched a
# single oemNN, so a machine that had accumulated copies (one per press of the
# install button, before that was fixed) could never be cleaned from the UI and
# the user was left reaching for regedit, which Windows refuses.
$pub = $null
$ours = @()
foreach ($line in (pnputil /enum-drivers)) {
    if ($line -match 'Published Name:\s+(oem\d+\.inf)') { $pub = $matches[1] }
    elseif ($line -match 'Original Name:\s+rea_sixty_winusb\.inf') {
        if ($pub) { $ours += $pub }
    }
}
foreach ($o in $ours) { pnputil /delete-driver $o /uninstall 2>&1 | Out-Null }
Write-Output ("removed: " + ($ours -join ',') )

# Remove signing cert from all stores.
$subject = 'CN=Rea-Sixty Driver Signer'
foreach ($store in @('My','Root','TrustedPublisher')) {
    Get-ChildItem "Cert:\LocalMachine\$store" |
        Where-Object { $_.Subject -eq $subject } |
        Remove-Item -Force 2>$null
}

Write-Output 'REASIXTY_RESULT=OK'
}
catch {
    Write-Output ('REASIXTY_RESULT=FAIL ' + $_.Exception.Message)
    try { Stop-Transcript | Out-Null } catch { }
    exit 1
}
try { Stop-Transcript | Out-Null } catch { }
exit 0
