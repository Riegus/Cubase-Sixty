# Settings wishlist

Behaviour that is fixed in code today but should become a setting once Cubase-Sixty has a place for settings
(a settings file read by the bridge, or a settings page). Each entry: what it is now, and what the choice would be.
The Settings page that will hold them is step 4 of the [release plan](release-plan.md).

The Settings page (tray icon: click, or Settings in its menu) shows what is settable now; it writes
`cubase\settings.txt` and `cubase\favourites.txt`, which can still be edited by hand. On it so far: track stepping,
the UC1 secondary encoder's job at start, key / LED and display brightness (five steps, all surfaces), sleep when idle, the UC1's
7-segment leading zeros, the UC1 knob step, touching a UF8 fader selects its track, the UF1's view at start, the UC1's magnifier / 360 / secondary encoder push
keys (next / previous strip, the strip's window, Channel Settings, the MixConsole, bypass, nothing), long
track names (abbreviated, the default, or cut; Rea-Sixty's "Smart
abbreviate"), the script log, and the favourites (eight per domain). A new setting is one
entry in `defs()` in `bridge/src/Settings.cpp` (the page and the file follow).

## Logging

- **Script log** (`cubase\logs\scripts.log`). On the Settings page now (on / off). For releases: off by default.
- **Bridge log** (`cubase\logs\bridge.log`). Now: always on. Could get the same switch.

## UC1

- **Loading Cubase's strip modules.** Now: on a track without an SSL channel strip, a Gate knob or EXP loads the Noise
  Gate, DYN IN loads the Standard Compressor, a Dyn knob (or DYN IN held) opens the compressor picker; also when the
  track has a Bus Compressor 2 or another maker's gate / compressor in its inserts. Choices: as now; not when the track
  already has a gate / compressor plug-in (by name); only on the keys, not the knobs; never.
- **Which compressor DYN IN loads.** Now: Standard. Could be: the last one picked; a fixed choice.
- **Tube / Vintage Compressor knobs.** Now: Ratio knob = Input Gain, Threshold knob = Output Gain.
- **Bell keys on Cubase's EQ.** Now: Parametric II as the bell; back to the shelf type the band had.
- **Bus Comp section default.** Now: until one is picked with the Bus Comp encoder, the bus comp on an output channel,
  else the first in the project. Could be: always the selected track's; remembered per project.
- **F.ATT on Cubase's strip.** Now: switches the section's Release knob between release and attack, the choice kept
  across tracks. Could be: fastest attack / back (like an SSL fast-attack switch).
- **ROUTING order 10 on the UC1.** Now: picture 10 (numbers run 1-10, the picture is slightly off). Could be: picture 8
  (right path, renumbered to 8); order 10 left out.
- **Favourites.** Now: listed by hand in `cubase\favourites.txt` (eight channel strips, eight bus comps). Rea-Sixty
  picks them in its Settings window (eight slots per domain, a dropdown of mapped plug-ins), with named sets, a
  per-project base bank, copy / own settings per domain and which channel strip sections a swap carries.

## Candidates from Rea-Sixty's Settings window

Rea-Sixty's Settings (Devices, Appearance, Behaviour, Bindings, Modes, FX Learn, Favourites, Selection Sets, Sticky
Pot, Parameter Groups, About) has these that would make sense in Cubase-Sixty (looked at 9.11.2026):

- **Devices**: Identify (flash a surface); a second UF8; "Sleep now" (a key or the tray menu).
- **Behaviour**: the UC1 shows the main output as track 0; plug-in cycling wraps around; the UF8 / UF1 start on a
  fixed soft-key bank (the UF8's selection mode is always NORM at start already); announce the soft-key bank on the time display; the UF1 follows the
  UF8's SSL Strip Mode; the UF1's PLUG-IN key opens the plug-in window.
- **Metering**: where the gain reduction comes from; the UF1 soft-key LEDs as bus comp gain reduction.
- **Favourites**: named sets, a base bank per project, copy / own settings per domain, which strip sections a swap
  carries.
- Not for Cubase (REAPER-only): TCP / MCP mirroring, pinned tracks, VCA spill, FX-chain window pinning, ReaScript
  bindings.
