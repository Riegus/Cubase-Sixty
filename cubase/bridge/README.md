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

## Later phases

7. **UC1 and plug-in focus.** Follow the focused insert or channel-strip plug-in in Cubase and map its parameters to the UC1. This depends on how much of the insert and strip-effect parameters the MIDI Remote API exposes, so prototype it early.
8. **SSL plug-in stream and meters.** Rea-Sixty receives the SSL plug-ins' meter stream by standing in for SSL 360° Core. The VST3s do not depend on the DAW, so this should carry over; the open question is tying each plug-in instance to a Cubase track. REAPER's "Rea-Sixty Input Level" JSFX probe has no Cubase equivalent, so a small VST3 that reports levels to the bridge is the likely replacement. Send meters over a separate channel (UDP or shared memory), not MIDI.
9. **Extras.** Layers, soft keys, a second UF8, the Stream Deck and Companion bridge, and the settings UI (ReaImGui is REAPER-only, so a local web UI served by the bridge is one option). Per-project state needs a sidecar file keyed by project, or a state-holder plug-in, since Cubase has no equivalent of REAPER's project ExtState.

## Risks

- **MIDI Remote API limits** (focused plug-in access, metering, project identity, value take-over) are the main unknown.
- **What this adds over SSL 360°**, which already supports Cubase through HUI/MCU and VST3 integration. Pin down which Rea-Sixty features you want in Cubase; that list is the scope.
- **Licences:** Rea-Sixty's licence, and the Steinberg VST3 SDK terms if you write the probe plug-in.

## Building

Not yet buildable. The build will use CMake, like Rea-Sixty's extension, and reuse its libusb and hidapi setup.
