# Settings wishlist

Behaviour that is fixed in code today but should become a setting once Cubase-Sixty has a place for settings
(a settings file read by the bridge, or a settings page). Each entry: what it is now, and what the choice would be.

## Logging

- **Script log** (`cubase\logs\scripts.log`). Now: on, `--no-script-log` turns it off. For releases: off by default.
- **Bridge log** (`cubase\logs\bridge.log`). Now: always on. Could get the same switch.

## UC1

- **Loading Cubase's strip modules.** Now: on a track without an SSL channel strip, turning a Gate or Dyn knob loads
  Cubase's Gate / Standard Compressor into a free strip slot, also when the track has a Bus Compressor 2 or another
  maker's gate / compressor in its inserts. Choices: as now; not when the track already has a gate / compressor
  plug-in (by name); only on a deliberate action (e.g. DYN IN on an empty strip); never.
- **Which compressor is loaded.** Now: Standard Compressor if the slot takes it, else the first compressor.
- **Bus Comp section default.** Now: until one is picked with the Bus Comp encoder, the bus comp on an output channel,
  else the first in the project. Could be: always the selected track's; remembered per project.
- **F.ATT on Cubase's strip.** Now: switches the section's Release knob between release and attack, the choice kept
  across tracks. Could be: fastest attack / back (like an SSL fast-attack switch).
- **Knob step size.** Now: 1/64 of the range per click, Fine = a quarter of that.
- **7-segment.** Now: the MixConsole number, leading zeros blank. Rea-Sixty shows leading zeros ("007").
