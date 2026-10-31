# Bridge ↔ Cubase MIDI protocol (draft v0)

This is the contract between the Cubase-Sixty Bridge and the MIDI Remote script. It is a first draft covering the UF8 fader bank only, and will change as the bridge takes shape.

## Ports

The bridge creates one virtual port pair per surface. Names are seen from Cubase's side:

| Surface | Cubase input (bridge → Cubase) | Cubase output (Cubase → bridge) |
|---------|--------------------------------|---------------------------------|
| UF8     | `Cubase-Sixty UF8`             | `Cubase-Sixty UF8`              |
| UC1     | `Cubase-Sixty UC1` (later)     | `Cubase-Sixty UC1` (later)      |
| UF1     | `Cubase-Sixty UF1` (later)     | `Cubase-Sixty UF1` (later)      |

The MIDI Remote script detects the surface by these exact names.

## UF8 messages (v0)

Strips are numbered 1–8 and map to MIDI channels 1–8.

| Control           | Message                          | Direction    | Notes |
|-------------------|----------------------------------|--------------|-------|
| Fader position    | Pitch bend, channel = strip      | both         | 14-bit. Cubase → bridge moves the motor fader. |
| Fader touch       | Note 104 + strip−1, vel 127 / 0  | bridge → Cubase | 127 = touched, 0 = released. |
| V-Pot turn        | CC 16 + strip−1, channel 1       | bridge → Cubase | Relative: 1–63 = right, 65–127 = left (signed bit). |
| V-Pot press       | Note 32 + strip−1                | bridge → Cubase | |
| SEL               | Note 24 + strip−1                | both         | Cubase → bridge sets the LED. |
| SOLO              | Note 8 + strip−1                 | both         | Cubase → bridge sets the LED. |
| CUT (mute)        | Note 16 + strip−1                | both         | Cubase → bridge sets the LED. |
| Bank left / right | Note 46 / 47                     | bridge → Cubase | |
| Play / Stop / Rec | Note 94 / 93 / 95                | both         | Cubase → bridge sets the LED. |

Button notes and fader-touch notes follow Mackie Control numbering. That keeps the draft familiar and makes it easy to test the script with a generic MCU device before the bridge exists. It is not MCU emulation: the bridge talks to the SSL hardware directly and only uses these numbers between itself and Cubase.

## Not yet covered

- Scribble-strip text and track colours. Cubase sends text and colours through the MIDI Remote API's display and colour callbacks; the script will forward them as SysEx to the bridge. Format to be defined.
- Meters.
- UC1 and UF1 mappings.
