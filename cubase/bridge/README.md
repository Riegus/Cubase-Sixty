# Cubase-Sixty Bridge

A small Windows program that sits between the SSL controllers and Cubase:

- **USB side:** opens the UF8 and UF1 over their vendor-USB interfaces, using the device and protocol code from Rea-Sixty's `extension/src/` (`UF8Device`, `UF1Device`, `Protocol`, `UF1Protocol`, `Palette`, ...), compiled in place rather than copied.
- **MIDI side:** talks to the loopMIDI ports (through RtMidi) and translates between SSL frames and the messages in [`../docs/bridge-midi-protocol.md`](../docs/bridge-midi-protocol.md). The Cubase end is the MIDI Remote scripts in [`../midi-remote/`](../midi-remote/).

SSL 360° must not be running: it claims the controllers' USB interfaces exclusively. Each surface is optional; the bridge skips a surface whose device or ports are missing and says so.

## Status

| Surface | Working |
|---------|---------|
| UF8 | Faders (motor, touch, echo suppression), SEL / SOLO / CUT with LEDs, top soft keys = record-enable (red; dark and ignored on strips that cannot record), V-pots (pan) with the pan value and bar on the display, V-pot push, banking, Page left / right (shift the bank by one channel), starts on the bank with the selected track, automation Read / Write with LEDs, automation mode keys (Off = Cross-Over, Trim, Latch, Touch), zoom pad (left / right = zoom, up / down = vertical zoom, centre = open / close editor), scribble-strip names and colours, fader level in dB on each strip, stereo meters. Foot switches are implemented but untested. |
| UF1 | Fader (motor, touch), SOLO / CUT / SEL (SEL LED in the track colour), transport keys with LEDs, ARROW = previous / next track, BANK = banks the UF8 (8 tracks without a UF8), NAV cross = zoom (centre = open / close editor), pan knob above the fader, V-pots 1–4 = sends 1–4, or 5–8 after the 5–8 key (turn = level, push = on / off), MASTER puts fader, pan, SOLO and CUT on the main output, SHIFT + the transport soft keys / Cycle / Click = automation (◀ Read, ▶ Write, Cycle Cross-Over, Click Trim, 1 Latch, 2 Touch), channel encoder selects the previous / next track, jog wheel nudges the cursor. Display: track name, level, colour, and a value line with bar (pan; a send for 3 seconds after it is turned), stereo meter, and the UF8 strip that holds the selected track in the channel-number box. The key above the display is labelled BYPASS but does nothing yet (see Later). |
| UC1 | Not started. |

## Building

Requires Visual Studio Build Tools (MSVC), CMake and [libusb](https://libusb.info) (the Windows binary release). RtMidi is fetched automatically.

```
cmake -S cubase\bridge -B cubase\bridge\build -DLIBUSB_ROOT=C:/path/to/libusb
cmake --build cubase\bridge\build --config Release
```

`LIBUSB_ROOT` is the folder that contains `libusb.h` somewhere below it and `VS2022\MS64\dll\libusb-1.0.lib`. Stop the running bridge before rebuilding, or the link step fails with LNK1104. The result is `cubase\bridge\build\Release\cubase_sixty_bridge.exe` (with `libusb-1.0.dll` next to it). `build\` is git-ignored.

A small test program checks the note, LED and colour mapping without hardware:

```
cubase\bridge\build\Release\test_mapping.exe
```

## Running

1. Install loopMIDI (its driver is what lets the bridge create MIDI ports; you do not create any ports in it yourself) and the Cubase scripts, as described in [`../midi-remote/README.md`](../midi-remote/README.md).
2. Start the bridge **before** Cubase:
   ```
   cubase\bridge\build\Release\cubase_sixty_bridge.exe
   ```
3. Start Cubase. It sees the ports, activates the scripts and sends the current state (names, colours, faders, LEDs, display) to the running bridge.
4. If you restart the bridge, restart Cubase too: it does not pick the ports up again when they disappear and come back (neither **Reload Scripts** nor the refresh button in the MIDI Remote Manager helps).
5. If a surface still does not respond, restart Cubase with the bridge running.
6. **Stuck MIDI ports.** After many bridge restarts in one Windows session, the MIDI layer can get stuck: the bridge sends fine, but nothing from Cubase arrives (with `--trace`, no `[virtual port in]` lines at all; the screens stay blank, the UF1 keeps showing READY). Sometimes ports get garbled names. Restarting the PC clears it.

Options:

| Option | What it does |
|--------|--------------|
| `--trace` | Prints UF1 events, the MIDI the UF8 and UF1 send to Cubase (not fader positions), every UF1 LED change, UF1 value-line updates, and the UF8 record-enable state per strip. |
| `--loopmidi` | Use ports made in loopMIDI (the old way) instead of creating them. The bridge does this by itself if loopMIDI's driver library (`teVirtualMIDI64.dll`) is missing or cannot create a port. |
| `--lamptest` | Lights each UF1 LED in turn about 6 seconds after start-up. |

## Behaviour to know about

- **Foot switches.** An unplugged foot-switch jack reports itself as "pressed" while the UF8 starts, which used to start the transport. The foot switches are ignored for the first 4 seconds.
- **MIDI ports.** The bridge creates the four ports (`Cubase-Sixty UF8|UF1 to|from Cubase`) with loopMIDI's driver library while it runs; they disappear when it exits. Delete any ports with these names that you made in loopMIDI earlier. Cubase may tick **In 'All MIDI Inputs'** for a port it sees as new; untick it (see the MIDI Remote README). In the `--loopmidi` fallback, Cubase sends state only when it changes, so a bridge that starts after Cubase sees nothing: if nothing has arrived 2.5 seconds after start-up, the bridge presses bank-next and bank-previous on the UF8 to make Cubase resend (if Cubase is on its last bank the UF8 ends up one bank back), and for the UF1 you press **Reload Scripts**.
- **Start-up bank.** When the UF8 script starts, it steps bank by bank (the bridge sends the bank-right presses) until the selected track is on one of the eight strips. Cubase ignores bank presses during its first seconds, so the bridge holds them until 3 seconds after Cubase first sends anything: expect a short pause before the UF8 moves. Pressing a bank key ends the search. With no track selected, nothing happens until you select one.
- **Record-enable keys.** Dim red = can arm, bright red = armed, dark = this strip cannot record (group, FX, VCA, bus, empty strip); a dark key ignores presses. The script decides which strips can record (see the MIDI Remote README). An audio track with no input bus still shows dim red but Cubase will not arm it.
- **Encoder speed (UF1).** The channel encoder and the jog wheel send one step per 4 detents (`kDetentsPerPulse` in `Uf1Bridge.cpp`).
- **UF1 SEL LED.** Shows the selected track's colour at 35 % (`kSelDimPct`; the UF1's own dim level did not dim visibly), full brightness while the channel editor is open.
- **UF1 BANK and ARROW.** BANK presses the UF8's bank key when a UF8 is running; without one it steps the selection 8 tracks (40 ms apart, back-to-back steps were merged by Cubase). ARROW is one channel-encoder step.
- **UF1 value line.** Pan is the line's normal content. Turning a send V-pot shows that send for 3 seconds after the last change (`kTemporaryMs`); selecting another track shows its pan straight away.
- **Automation mode LEDs (UF8).** Cubase does not tell scripts which global automation mode is active, so the bridge lights Off / Latch / Touch from the last one pressed (on the UF8, or SHIFT + key on the UF1) and Trim as a toggle. All off at start-up; a mode changed with the mouse is not seen. The Off key's LED stays dark (its LED address is not found yet).
- **Meters.** Cubase reports the meter as linear amplitude (0–1); the scripts convert it to dB and map −55…0 dBFS onto 0–127 (`METER_FLOOR_DB` near the end of each script), the same scale Rea-Sixty uses on the UF8. Left and right per channel; a mono channel (only a left value) is shown on both sides. The UF8 meter frames are throttled like Rea-Sixty's (a byte moved by 2, or 50 ms passed) so faders and LEDs do not lag during playback. Empty strips are cleared and ignore late values.
- **dB readout (UF8).** Each strip shows its fader level. Cubase does not resend a level that equals what the strip showed before it went empty, so the bridge keeps the last text per strip and shows it again when a track returns.
- **UF1 colour bar.** At start-up the bridge writes 12 spaces to the text cell inside the colour bar (Rea-Sixty's "nothing"; an empty text drops the layer), clears the channel number, and sets the peak indicator and the compressor / gate gain-reduction cells to rest. The dimmed block at the left of the colour bar is the channel-number box, which the firmware always draws; it shows the UF8 strip (1–8) of the selected track, blank when that track is not on the UF8's current bank or MASTER is on.
- **UF1 display cell 0x0000.** Never write it: Rea-Sixty found that it belongs to the display's plane setup and writing it tore the layout down. The key above the display has its own LED (id 0x00).
- **LEDs.** The UF1 buttons are lit by colour (an LED with colour black is off), as in Rea-Sixty. LED brightness is set to full when the UF1 opens.

## Development

`cubase\tools\dev-restart.ps1` restarts the whole chain in the right order: closes Cubase (resending Quit until it exits), ends Cubase processes that are left without a window after 15 seconds (an unfinished shutdown; they cannot be showing a dialog), stops the bridge and waits 3 seconds for Windows to release the devices and ports, optionally rebuilds it, installs the scripts, starts the bridge in a window titled *Cubase-Sixty Bridge* that stays open if it exits (so the reason is readable), then opens your test project. If the bridge is not running after 4 seconds, Cubase is not started.

```
powershell -ExecutionPolicy Bypass -File cubase\tools\dev-restart.ps1 -Build
```

Options: `-Build`, `-Trace` (bridge with `--trace`), `-Force` (kill Cubase after 60 seconds; unsaved changes are lost), `-NoStartCubase`, `-Project <file.cpr>`, `-CubaseExe <path>`. The project is, in this order, `-Project`, the environment variable `CUBASE_SIXTY_PROJECT`, or the first line of `cubase\tools\project.txt` (a local file; do not commit it). Without a project, Cubase opens on the Hub.

## Layout

```
src/main.cpp        UF8 handling, MIDI ports, main loop
src/Uf1Bridge.*     UF1 handling (own device and MIDI ports)
src/Mapping.h       Button / LED / colour mapping shared by both (namespace cs, cs::uf1m)
tests/              test_mapping
```

## Later

- **UF1:** MODE, 5–8, 360°, SCRUB, FLIP, MASTER and the transport soft keys; keep the display when the selection lands on something without a mixer channel.
- **UF8:** the Off key's LED.
- **UF1 BYPASS key:** should bypass the selected track's inserts, but the API has no insert-bypass value and Cubase refuses `Mixer` / `Bypass: Inserts` from a script (the bridge sends note 62, Cubase does nothing).
- **Considered later:** UF1 FLIP (fader on pan); 360°, SCRUB, MODE (need a time display, jog modes and more UF1 views); "show only record-enabled tracks" on the UF8.
- **UC1 and plug-in focus.** Depends on how much of the insert and strip-effect parameters the MIDI Remote API exposes; prototype early.
- **SSL plug-in stream and meters.** Rea-Sixty gets the SSL plug-ins' meters by standing in for SSL 360° Core. Tying each plug-in instance to a Cubase track is the open question.
- **Extras:** layers, a second UF8, settings UI, packaging.

## Risks

- **MIDI Remote API limits** (focused plug-in access, metering, live colour updates) are the main unknown.
- **Licences:** Rea-Sixty's licence, and the Steinberg VST3 SDK terms if a probe plug-in is ever written.

See also [`../docs/extension-survey.md`](../docs/extension-survey.md) for which parts of Rea-Sixty can be reused.
