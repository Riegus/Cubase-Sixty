# Cubase-Sixty Bridge

The bridge is a small background app that sits between the SSL controllers and Cubase:

- **USB side:** opens the UF8, UC1 and UF1 over their vendor-USB interfaces, using the protocol and device code from Rea-Sixty's `extension/src/` (`Protocol`, `UF8Device`, `UC1Device`, `HidDevice` and related).
- **MIDI side:** creates virtual MIDI ports and translates between SSL frames and the messages in [`../docs/bridge-midi-protocol.md`](../docs/bridge-midi-protocol.md).

SSL 360° must not be running, since it claims the controllers' USB interfaces exclusively.

## Plan

1. **Prove the USB layer runs without REAPER.** Build Rea-Sixty's standalone probes (for example `uf8_color_test`) on Windows and run them with Cubase open. If the UF8 responds, the approach works.
2. **Separate the reusable code.** Identify which files in `extension/src/` depend on the REAPER SDK and which are pure protocol and device code. Build the latter into a library the bridge links against, without copying files.
3. **Virtual MIDI ports.** Choose how to create ports on Windows: the Windows MIDI Services app SDK (Windows 11) or a virtual MIDI driver. macOS can create virtual ports through CoreMIDI directly.
4. **Faders first.** Move the motor faders from Cubase and read them back, with fader touch.
5. **Buttons, V-Pots, LEDs, banking.**
6. **Scribble strips and track colours**, once the script can send them as SysEx.

## Building

Not yet buildable. The build will use CMake, like Rea-Sixty's extension, and reuse its libusb and hidapi setup.
