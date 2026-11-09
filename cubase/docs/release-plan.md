# Release plan (first public beta)

What has to happen before Cubase-Sixty can go out as a beta, in this order. Features still missing are in
[Missing features](missing-features.md); they do not hold the beta back.

## 1. Closing and opening projects (done 8.11.2026)

- Find out what each surface shows when a project closes and another opens (a test on the hardware first).
- On close: every surface to a clean "no project" state (names, faders, meters, LEDs, the UF1 / UC1 screens).
- On open: everything picked up again (UF8 follow, UF1 PLUGIN, UC1 strip and bus comp, favourites, settings).
- Found (8.11.2026): on close each script's driver is deactivated (mOnDeactivate), on open activated again with the
  page; there is no project callback. After reopening, the UF8 did not bank to the selected track.
- Done: the scripts send SysEx 7C 00 / 01 on deactivate / activate. On close the bridge blanks each surface (UF8:
  strips empty, faders down, its own keys dark; UF1: only "No Proj.", the big display's fields blank; UC1: "No
  Project", bus comp row empty, rings and LEDs off). On open each surface puts back what Cubase does not send again
  (it sends only what changed), and the UF8's start-up follow runs again.

## 2. Different setups of the surfaces (done 9.11.2026)

- Always create every surface's MIDI ports, so all scripts load whatever is connected (Cubase only sees ports at start).
- Hot-plug: a surface connected or disconnected while Cubase runs comes and goes without a restart.
- Done (9.11.2026): the bridge makes all ports at start; a missing surface is looked for every 2 s, an unplugged
  one let go of and picked up again with the last state Cubase sent (the UF8's foot switches ignored for 4 s after
  each connect: an empty jack reports "pressed" and started the transport).
- ~~Move the jobs that serve every surface into the Helper script~~: not needed. With every surface's ports always
  made, every script is loaded in every setup, so the UF1 script's track stepping and the UC1 script's favourites
  serve the other surfaces whether or not the UF1 / UC1 is connected (the bridge no longer asks for the UF1 device
  before the UF8 / UC1 step tracks through it).
- Check each combination: UF8 alone, UF1 alone, UC1 alone, each pair, all three (fallbacks such as the UF1's BANK
  without a UF8). Done 9.11.2026: all work. Found on the way: single track steps stopped on folders (now stepped past
  and remembered); the UF1's BANK without a UF8 now banks the UF8 script's strips and selects on them (see
  [Banking and track stepping](banking.md)).
- Later: two UF8s (16 strips).

## 3. The bridge as a background app

- Starts by itself at login (before Cubase), no console window.
- Tray icon: status (which surfaces, Cubase connected), open the logs, restart, quit.
- Restarts itself if it crashes.
- Not a Windows service: services run in their own session, where the MIDI ports (WinMM), closing SSL 360 Core and the
  SSL plug-ins' connection would not work reliably.
- Built (wip/tray, to be tested): `CubaseSixty.exe` runs the bridge without a console, restarts it if it stops (five
  times a minute at most), tray icon green / yellow / red with each surface's and Cubase's state from bridge.log,
  Restart bridge, Open logs folder, Start with Windows, Quit. Status lines in bridge.log without --trace ("Cubase
  connected", "Cubase: project open / no project"). Open: a restarted bridge needs Cubase restarted (its ports are
  new), unless loopMIDI ports are used; the installer (step 5) sets Start with Windows.

## 4. Settings

- A Settings page opened from the tray icon (a local web page the bridge serves, shown in the browser), reading and
  writing `settings.txt` and `favourites.txt`; changes apply without a restart.
- Everything on the [settings wishlist](settings-wishlist.md): logging, the UC1 options, favourites (slots, sets, copy
  / own settings), track stepping, the secondary encoder's job, key assignments.

## 5. Installer and documentation

- An installer: the built bridge (with `libusb-1.0.dll`), the scripts into Cubase's MIDI Remote folder, the default
  settings, autostart, an uninstaller; a check for loopMIDI (its driver makes the ports) with a pointer to it.
- Release defaults: the script log off, a version number shown at start.
- A user guide: what each key does on each surface, the setup steps that stay manual (the Mackie Control device for
  scrubbing, SSL 360 not running), with links to the known issues and the Cubase limitations.
