# Settings wishlist

Behaviour that is fixed in code today but should become a setting once Cubase-Sixty has a place for settings
(a settings file read by the bridge, or a settings page). Each entry: what it is now, and what the choice would be.
The Settings page that will hold them is step 4 of the [release plan](release-plan.md).

Some settings are in `cubase\settings.txt` now (written with the defaults the first time the bridge starts; edit it,
no restart needed): `track-stepping` = mixconsole (default) / project / all; `uc1-encoder2` = bus-comp (default) / strips / cs-fav / bc-fav.

## Logging

- **Script log** (`cubase\logs\scripts.log`). Now: on, `--no-script-log` turns it off. For releases: off by default.
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
- **Key assignments.** Now: magnifier = next channel strip on the track, 360° = open / close the strip's window.
  Rea-Sixty lets the user bind these keys (and the Bus Comp encoder push) to other actions.
- **ROUTING order 10 on the UC1.** Now: picture 10 (numbers run 1-10, the picture is slightly off). Could be: picture 8
  (right path, renumbered to 8); order 10 left out.
- **Knob step size.** Now: 1/64 of the range per click, Fine = a quarter of that.
- **7-segment.** Now: the MixConsole number, leading zeros blank. Rea-Sixty shows leading zeros ("007").
- **Favourites.** Now: listed by hand in `cubase\favourites.txt` (eight channel strips, eight bus comps). Rea-Sixty
  picks them in its Settings window (eight slots per domain, a dropdown of mapped plug-ins), with named sets, a
  per-project base bank, copy / own settings per domain and which channel strip sections a swap carries.
