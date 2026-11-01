# Cubase-Sixty Bridge

A small Windows program that sits between the SSL controllers and Cubase:

- **USB side:** opens the UF8 and UF1 over their vendor-USB interfaces, using the device and protocol code from Rea-Sixty's `extension/src/` (`UF8Device`, `UF1Device`, `Protocol`, `UF1Protocol`, `Palette`, ...), compiled in place rather than copied.
- **MIDI side:** talks to the loopMIDI ports (through RtMidi) and translates between SSL frames and the messages in [`../docs/bridge-midi-protocol.md`](../docs/bridge-midi-protocol.md). The Cubase end is the MIDI Remote scripts in [`../midi-remote/`](../midi-remote/).

SSL 360° must not be running: it claims the controllers' USB interfaces exclusively. Each surface is optional; the bridge skips a surface whose device or ports are missing and says so.

## Status

| Surface | Working |
|---------|---------|
| UF8 | Faders (motor, touch, echo suppression), SEL / SOLO / CUT with LEDs, top soft keys (record-enable), V-pots and V-pot push, banking, transport, automation Read / Write with LEDs, scribble-strip names and colours. Foot switches are implemented but untested. |
| UF1 | Fader (motor, touch), SOLO / CUT / SEL, transport keys with LEDs, V-pot 1. The display shows a "Cubase-Sixty" banner only. |
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

1. Set up loopMIDI and install the Cubase scripts as described in [`../midi-remote/README.md`](../midi-remote/README.md).
2. Start Cubase first, then the bridge:
   ```
   cubase\bridge\build\Release\cubase_sixty_bridge.exe
   ```
3. If a surface does not respond, restart Cubase and then the bridge (Cubase sometimes only picks a script up after a restart).

Options:

| Option | What it does |
|--------|--------------|
| `--trace` | Prints UF1 events, the MIDI the UF8 and UF1 send to Cubase (not fader positions), and every UF1 LED change. |
| `--lamptest` | Lights each UF1 LED in turn about 6 seconds after start-up. |

## Behaviour to know about

- **Foot switches.** An unplugged foot-switch jack reports itself as "pressed" while the UF8 starts, which used to start the transport. The foot switches are ignored for the first 4 seconds.
- **Starting the bridge after Cubase.** Cubase sends track names, colours, faders and LED states only when they change, so a bridge that starts later would see nothing. If nothing has arrived 2.5 seconds after start-up, the bridge presses bank-next and then bank-previous, which makes Cubase send everything again. If Cubase is on its last bank, bank-next does nothing and the UF8 ends up one bank back; bank once more.
- **LEDs.** The UF1 buttons are lit by colour (an LED with colour black is off), as in Rea-Sixty. LED brightness is set to full when the UF1 opens.

## Layout

```
src/main.cpp        UF8 handling, MIDI ports, main loop
src/Uf1Bridge.*     UF1 handling (own device and MIDI ports)
src/Mapping.h       Button / LED / colour mapping shared by both (namespace cs, cs::uf1m)
tests/              test_mapping
```

## Later

- **UF1:** display (track name, values), V-pots 2 to 4, encoders and jog, SEL LED and track colour, navigation buttons.
- **UC1 and plug-in focus.** Depends on how much of the insert and strip-effect parameters the MIDI Remote API exposes; prototype early.
- **SSL plug-in stream and meters.** Rea-Sixty gets the SSL plug-ins' meters by standing in for SSL 360° Core. Tying each plug-in instance to a Cubase track is the open question.
- **Extras:** layers, a second UF8, settings UI, packaging.

## Risks

- **MIDI Remote API limits** (focused plug-in access, metering, live colour updates) are the main unknown.
- **Licences:** Rea-Sixty's licence, and the Steinberg VST3 SDK terms if a probe plug-in is ever written.

See also [`../docs/extension-survey.md`](../docs/extension-survey.md) for which parts of Rea-Sixty can be reused.
