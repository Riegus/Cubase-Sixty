# Bridge ↔ Cubase MIDI protocol (draft v0)

This is the contract between the Cubase-Sixty Bridge and the MIDI Remote script. It is a first draft covering the UF8 fader bank only, and will change as the bridge takes shape.

## Ports

The bridge creates two virtual MIDI ports per surface, one for each direction. Names are seen from Cubase's side:

| Surface | Cubase input (bridge → Cubase)   | Cubase output (Cubase → bridge)     |
|---------|----------------------------------|-------------------------------------|
| UF8     | `Cubase-Sixty UF8 to Cubase`     | `Cubase-Sixty UF8 from Cubase`      |
| UC1     | `Cubase-Sixty UC1 to Cubase` (later) | `Cubase-Sixty UC1 from Cubase` (later) |
| UF1     | `Cubase-Sixty UF1 to Cubase` (later) | `Cubase-Sixty UF1 from Cubase` (later) |

The MIDI Remote script detects the surface by these exact names.

Why two ports with different names: a virtual port made with a tool like loopMIDI is a loopback, so anything written to it is delivered straight back to whoever reads it. If Cubase used one port for both directions, it would hear its own output. In testing, a fader moved in Cubase snapped back to its old value for that reason. With one port per direction, the bridge writes to the first and reads the second, and nothing loops.

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

## Scribble strips and track colours (draft v0)

Cubase pushes a track's name and colour to the bridge as SysEx on the surface's output port. Both messages use the non-commercial manufacturer ID `7D`, followed by the ASCII tag `52 36` ("R6"). The strip byte is 0–7 (strip 1 = 0), the same index as the strip's MIDI channel.

| Item         | Message                                         | Direction       | Notes |
|--------------|-------------------------------------------------|-----------------|-------|
| Track name   | `F0 7D 52 36 01 <strip> <text…> F7`             | Cubase → bridge | ASCII, up to 32 bytes. Non-ASCII characters are sent as `3F` (`?`); the bridge maps the text to what the UF8 display can show. |
| Track colour | `F0 7D 52 36 02 <strip> <r> <g> <b> F7`         | Cubase → bridge | 0–127 per channel. The bridge quantises to the UF8 palette. |

Both are sent whenever the bank changes or the track's name or colour changes. An empty name clears the strip.

## Not yet covered

- Meters.
- UC1 and UF1 mappings.
- A version or ping message. Add one (`F0 7D 52 36 7F …`) before the protocol grows.
- Echo suppression. A fader moved in Cubase comes back as a motor move on the UF8, which must not be reported to Cubase as a touch or a new position.
