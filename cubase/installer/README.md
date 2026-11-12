# The installer

`CubaseSixty-Setup-<version>.exe`, made with [Inno Setup 6](https://jrsoftware.org/isdl.php) (free).

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

Not in the installer yet: the WinUSB driver for the controllers (planned as an optional step and a tray menu item,
ported from Rea-Sixty), and the user guide.
