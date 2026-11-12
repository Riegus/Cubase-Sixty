# The installer

`CubaseSixty-Setup-<version>.exe`, made with [Inno Setup](https://jrsoftware.org/isdl.php) (free; version 7 or 6).

```
powershell -ExecutionPolicy Bypass -File cubase\installer\build-installer.ps1
```

builds the bridge and the tray app (Release) and writes the installer to `cubase\dist\`. `-NoBuild` uses the bridge
as it is built. The version comes from `cubase\VERSION`.

What it does (`CubaseSixty.iss`):

- Per user, no administrator rights. The program goes to `%LOCALAPPDATA%\Programs\Cubase-Sixty`: `CubaseSixty.exe`
  (the tray app), `cubase_sixty_bridge.exe`, `libusb-1.0.dll`. Settings, favourites, the level curve and the logs are
  written beside them, as in the repo.
- The MIDI Remote scripts go to `<Documents>\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty`
  (Documents as Windows has it, so a OneDrive-redirected folder works).
- "Start Cubase-Sixty with Windows" (a task, on by default): the same Run entry the tray app's menu sets.
- A running copy is asked to quit first (`CubaseSixty.exe --quit`: the bridge stops and blanks the surfaces), on an
  update and on uninstall.
- After installing it says so if the virtual MIDI driver (teVirtualMIDI, which comes with loopMIDI and with SSL 360)
  or the Visual C++ runtime is missing, with where to get them.
- Uninstall removes the program, the scripts, the logs and the Run entry; it asks before removing `settings.txt` and
  `favourites.txt`.

## The USB driver

The bridge opens the controllers through libusb, which needs them on Windows' WinUSB driver instead of SSL's own
(SSLBUS). `driver\` holds Rea-Sixty's way of doing that, unchanged but for where the INF comes from:

- `rea_sixty_winusb.inf`: binds the UF8, UC1 and UF1 to WinUSB (a copy of `extension\resources\rea_sixty_winusb.inf`;
  keep the two the same).
- `install-driver.ps1`: makes (or reuses) a self-signed certificate "Rea-Sixty Driver Signer", trusts it, signs a
  catalog for the INF, adds the package to the driver store, force-binds the three device ids, and keeps exactly one
  copy in the store. The same package and certificate as Rea-Sixty's, so the two share one driver.
- `uninstall-driver.ps1`: removes every copy and the certificate; the controllers go back to SSL's driver on the
  next replug (reinstall SSL 360 if not). It takes the controllers away from Rea-Sixty too.

Both need administrator rights (one prompt) and write a log to `%TEMP%`. They are run by:

- the installer's task "Install the USB driver for the SSL controllers", ticked by itself when a controller Windows
  knows is not on WinUSB, or none is known (a first install);
- the tray app's **USB driver** sub menu (shown when `driver\` is beside the program): install, or remove. The bridge
  is stopped meanwhile and started again after; a message says how it went.

After either: unplug and replug the controllers. Only controllers that are plugged in when the install runs are
moved to the driver.

Not in the installer yet: the user guide.
