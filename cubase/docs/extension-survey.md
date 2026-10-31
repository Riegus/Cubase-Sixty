# Survey of `extension/src/`: what can be reused for the Cubase bridge

Step 2 of `cubase/bridge/README.md`. Based on `#include` analysis, a search for REAPER API names, and which files the existing unit tests in `CMakeLists.txt` already build without REAPER. It is a static read, not a compile, so treat the "pure" list as strong candidates, not guarantees.

## Headline

- 165 files in `extension/src/`; only **24 include REAPER/WDL/ReaImGui headers directly**, and about 40 reference REAPER APIs or types in some way.
- The **USB and protocol layer is clean**: it has no REAPER dependency and several pieces already build standalone in the test targets.
- The **behaviour layer is not separated**. `main.cpp` is ~62,300 lines and holds the UF8 input handlers (`onUf8Input`, `onUf8Input2`, `onUf1Input`), layer/mode logic and the REAPER track glue together (~700 direct REAPER track/FX/CSurf calls). `UC1Surface.cpp` (6k lines) and `Bindings.cpp` (10k lines) are also REAPER-bound.
- So the practical reuse is: **take the device and protocol libraries as they are, and rewrite the glue** against the Cubase MIDI protocol instead of extracting it from `main.cpp`.

## A. Reusable as-is (no REAPER dependency)

USB drivers (libusb/hidapi):
- `UF8Device.*`, `UC1Device.*`, `UF1Device.*` (plus `init_sequence.inc`, `uc1_init_sequence.inc`, `uf1_init_sequence.inc`, `layer_plugin_mixer.inc`)
- `Uc1SendQueue.h`, `Uf1Pacer.*`, `LogPath.*`, `BuildId.*`

Wire protocol and rendering:
- `Protocol.*` (UF8 frames, faders, strips), `UC1Protocol.*`, `UF1Protocol.*`
- `Uf8Frames.h`, `Uf8Fader.h`, `Uf8Strips.h`, `Uf8Units.h`, `Uf8ModeViews.h`
- `Palette.*`, `TrackName.*`, `EncoderRing.h`, `JogGrid.h`, `Brightness.h`, `Uf1Text.*`, `Uf1EqCurve.*`, `Uf1SoftKeys.*`, `Uf1Spread.*`, `GrCalibration.h`

SSL plug-in stream and meters:
- `SslCoreImpersonator.*`, `SslMeterProtocol.*`, `SslTrackIdentity.h`

Pure logic headers (each has a standalone test): `LaneModel.h`, `MidiCcLayer.h`, `ModeSync.h`, `PushQuiet.h`, `SelsetBank.h`, `VcaSpill.h`, `McpFolderCollapse.h`, `ProjectScoped.h`, `SurfaceSleep.h`, `OrcHandover.h`, `BindingsPick.h`, `RoomCells.h`.

Evidence: `test_protocol`, `test_uf8_fader`, `test_uf8_strips`, `test_uc1_protocol`, `test_surface_sleep`, `test_uf1_eq` and others are built in CMake from just these sources.

## B. Independent services (no REAPER; optional extras)

`HttpClient`, `WsClient`, `Sha256`, `RmeOsc`, `Rme*`, `Uf8Rme`, `ObsProto`/`ObsManager`, `HueClient`/`HueColor`/`HueManager` (`HueClient.cpp` uses WDL's JSON parser, which is easy to swap), `DynaMountClient`, `KeyMacro`/`KeyCodes.h`, `StreamDeckBridge` (check; includes REAPER headers), `orc/` sidecar. Port later, in any order.

## C. REAPER-bound: needs a Cubase replacement

- `main.cpp`: central glue. It must be re-expressed as a bridge that reads/writes the MIDI protocol rather than REAPER's track API.
- `UC1Surface.*`, `FocusedParam.*`, `PluginMap.*`, `UC1PluginMap.*`, `ParameterGroups.*`, `PluginChunkPatch.*`, `SslPresetLibrary.*`, `UserPluginCatalog.*`: plug-in focus and parameter mapping use REAPER's `TrackFX_*` API. In Cubase this depends on what the MIDI Remote API exposes for inserts and strip effects.
- `Bindings.*`, `BindingsHost.h`, `NavDispatch.*`, `KeyMacroSend.cpp`, `KnobFeelPresets.cpp`, `ColorSync.*`: REAPER actions and track colours.
- `MidiBridge.*`: **useful reference.** It already turns MCU frames into UF8 USB commands and back (macOS Core MIDI only; Windows/Linux are noted as "to do"). Its translation logic is the closest existing match to our protocol, but our numbering differs, and on Windows we need loopMIDI or teVirtualMIDI.

## D. REAPER-only UI (drop or replace)

`SettingsScreen*`, `MixerWindow.*`, `ExchangeView.*`, `ManualView.*`, `MarkerOverlay.*`, `ThemeBridge.*`, `SetupBundle.*`, `macos_*.mm`. These use ReaImGui. Replace with a small standalone settings UI or a config file.

## Coupling points that decide the design

1. **`main.cpp` is the real boundary.** Splitting it is possible but costly; a new bridge `main` that uses the A libraries is cheaper for the first milestone (UF8 faders, buttons, scribble strips).
2. **Windows virtual MIDI.** `MidiBridge` is Core MIDI only. On Windows the bridge should open the two loopMIDI ports (`... to Cubase` / `... from Cubase`) with WinMM or RtMidi.
3. **SSL 360° must not run**, since both it and the bridge claim the USB device.
4. **Plug-in focus and meters (UC1)** are the biggest open question; they depend on the Cubase API.

## Suggested next step

Build a minimal `cubase/bridge/` C++ target with `UF8Device`, `Protocol` and `Palette` from the A list, plus RtMidi for the two ports. First goal: fader moves on the UF8 appear as pitch bend in Cubase and back. Part of this is verifiable without hardware via the existing test_uf8_* targets.
