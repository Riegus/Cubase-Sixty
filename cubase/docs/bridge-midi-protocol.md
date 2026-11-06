# Bridge ↔ Cubase MIDI protocol (draft)

This is the contract between the Cubase-Sixty Bridge and the MIDI Remote scripts. It covers the UF8 and the UF1 and will change as the bridge takes shape.

## Ports

The bridge creates two virtual MIDI ports per surface, one for each direction. Names are seen from Cubase's side:

| Surface | Cubase input (bridge → Cubase)   | Cubase output (Cubase → bridge)     |
|---------|----------------------------------|-------------------------------------|
| UF8     | `Cubase-Sixty UF8 to Cubase`     | `Cubase-Sixty UF8 from Cubase`      |
| UF1     | `Cubase-Sixty UF1 to Cubase`     | `Cubase-Sixty UF1 from Cubase`      |
| UC1     | `Cubase-Sixty UC1 to Cubase`     | `Cubase-Sixty UC1 from Cubase`      |
| Mackie Control | `Cubase-Sixty MCU to Cubase` | `Cubase-Sixty MCU from Cubase` |

Each MIDI Remote script detects its surface by these exact names. Keep the ports out of Cubase's *All MIDI Inputs*. The Mackie Control pair is for a Mackie Control remote device in Cubase (see "Mackie Control" below), not a script.

Why two ports with different names: a virtual port made with a tool like loopMIDI is a loopback, so anything written to it is delivered straight back to whoever reads it. If Cubase used one port for both directions, it would hear its own output. In testing, a fader moved in Cubase snapped back to its old value for that reason. With one port per direction, the bridge writes to the first and reads the second, and nothing loops.

## UF8 messages

Strips are numbered 1–8 and map to MIDI channels 1–8.

| Control           | Message                          | Direction    | Notes |
|-------------------|----------------------------------|--------------|-------|
| Fader position    | Pitch bend, channel = strip      | both         | 14-bit. Cubase → bridge moves the motor fader. |
| Fader touch       | Note 104 + strip−1, vel 127 / 0  | bridge → Cubase | 127 = touched, 0 = released. |
| V-Pot turn        | CC 16 + strip−1, channel 1       | bridge → Cubase | Relative: 1–63 = right, 65–127 = left (signed bit). |
| V-Pot press       | Note 32 + strip−1                | bridge → Cubase | Opens the channel editor. |
| SEL               | Note 24 + strip−1                | both         | Cubase → bridge sets the LED. |
| SOLO              | Note 8 + strip−1                 | both         | Cubase → bridge sets the LED. |
| CUT (mute)        | Note 16 + strip−1                | both         | Cubase → bridge sets the LED. |
| Top soft key      | Note 48 + strip−1                | both         | Toggles record-enable; Cubase → bridge sets the LED (red). |
| Cannot record     | Note 64 + strip−1, vel 127 / 0   | Cubase → bridge | From the script: 127 = this strip cannot be armed (key dark, presses ignored), 0 = it can. |
| Bank left / right | Note 46 / 47                     | bridge → Cubase | A bank-right from the UF8 key or the UF1's BANK stops at the bank where the last track is on strip 8: near the end the bridge sends Page-right notes instead (needs SysEx 12). |
| Bank reset        | Note 79                          | bridge → Cubase | Bank back to the first channel. Used by the follow-selection jump. |
| Tick / jump done  | Note 80                          | bridge → Cubase | Sent after SysEx 10 and 11. Not used by the scripts: notes from the bridge cannot run script code (see the MIDI Remote README); the script times its checks with Cubase's idle callback instead. |
| Page left / right | Note 44 / 45                     | bridge → Cubase | Shift the bank by one channel. |
| Zoom pad          | Notes 72–76                      | bridge → Cubase | Up, left, centre, right, down: vertical zoom in, zoom out, open / close editor, zoom in, vertical zoom out. |
| Play / Stop / Rec | Note 94 / 93 / 95                | both         | Cubase → bridge sets the LED. |
| Cycle / Click     | Note 86 / 87                     | both         | UF1 only so far. |
| Rewind / Forward  | Note 91 / 92                     | both         | UF1 only so far. |
| Automation Read / Write | Note 56 / 57               | both         | Act on the selected track; Cubase → bridge sets the LED. |
| Automation modes  | Notes 58 / 59 / 60 / 61          | bridge → Cubase | Off (= Cross-Over), Trim (toggle), Latch, Touch: Cubase's global automation mode. Cubase gives no feedback; the bridge lights the LEDs from the last key pressed. |
| Meters            | CC 80 + strip−1 (left), CC 88 + strip−1 (right), 0–127 | Cubase → bridge | From the script, already in dB (−55…0 dBFS onto 0–127). A strip with no right value is shown mono. |
| Foot switch 1 / 2 | Note 62 / 63                     | bridge → Cubase | Play / record. Ignored for 4 s after start-up (phantom press). |
| FLIP              | Note 77 (normal) / 78 (flipped)  | bridge → Cubase | The bridge toggles on each press and lights the FLIP LED; the script switches sub-pages. |

LED state arrives as note on / off, or as polyphonic aftertouch with value 127 for "on"; the bridge accepts both.

Button notes and fader-touch notes follow Mackie Control numbering. That keeps the draft familiar and makes it easy to test the script with a generic MCU device before the bridge exists. It is not MCU emulation: the bridge talks to the SSL hardware directly and only uses these numbers between itself and Cubase.

## UF1 messages

The UF1 is a single strip that controls the selected track, and uses the UF8's strip-1 numbering on channel 1.

| Control | Message | Direction | Notes |
|---------|---------|-----------|-------|
| Fader position | Pitch bend, channel 1 | both | 14-bit. |
| Fader touch | Note 104 | bridge → Cubase | |
| SOLO / CUT / SEL | Notes 8 / 16 / 24 | both | Cubase → bridge sets the LED. The SEL LED shows the track colour (from SysEx 02), dim, and bright while the editor is open. |
| Pan knob | CC 16, relative signed-bit | bridge → Cubase | The knob above the fader (encoder id 0). Pan of the selected track. |
| V-pots 1–4 | 14-bit CC: 17–20 (high 7 bits) + 49–52 (low 7 bits), absolute | bridge → Cubase | Sends 1–4 / 5–8 of the selected track (level), or Quick Controls in PLUGIN. The bridge keeps each V-pot's position (from SysEx 17), adds the turns itself (82 of 16383 per detent) and sends the new absolute position, so it can draw it at once. |
| V-pot pushes | Notes 33–36 | bridge → Cubase | Send on / off. Note 32 = push of the pan knob, 37 = channel encoder push (unused). |
| 5–8 | Note 53 (sends 1–4) / 54 (sends 5–8); in the PLUGIN view 88 (Quick Controls 1–4) / 89 (5–8) | bridge → Cubase | The bridge toggles on each press and lights the 5–8 LED for 5–8. |
| MODE + display soft keys | Note 53 / 54 (DAW) or 88 / 89 (PLUGIN) | bridge → Cubase | Handled in the bridge: while MODE is held, the four display soft keys show the view picker (1 PLUGIN, 2 DAW, 4 SENDS, 3 not used yet; labels at cell 0x0104, LEDs 0x01–0x04, the active view lit yellow). Picking a view sends the V-pot page for that view and the current 5–8 state (SENDS uses the DAW notes). |
| MASTER / FLIP | Notes 44–47 | bridge → Cubase | Strip sub-page: 44 selected track, 45 main output, 46 selected track flipped, 47 main output flipped. The bridge toggles MASTER and FLIP and lights their LEDs. |
| SHIFT + keys | Notes 56–61 | bridge → Cubase | SHIFT held in the bridge. ◀ = 56 Read, ▶ = 57 Write (selected track), Cycle = 58 Cross-Over, Click = 59 Trim, 1 = 60 Latch, 2 = 61 Touch. |
| Meter | CC 80 (left), CC 81 (right), 0–127 | Cubase → bridge | From the script (already in dB, −55…0 dBFS); the level meter on the small display. |
| BYPASS state | Note 62, velocity 0 / 64 / 127 | Cubase → bridge | From the script: no inserts / inserts active / all bypassed. LED id 0x00 off / orange 35 % / orange. |
| BYPASS slots | Channel 2, notes 0–15, velocity 127 / 0 | bridge → Cubase | On a press (key id 0x18, handled in the bridge): each insert slot's new state, 127 = bypass, 0 = active. |
| 360° | Note 55 | bridge → Cubase | `Transport / Exchange Time Formats`. |
| Markers / bar steps | Notes 79 / 80, 81 / 82 | bridge → Cubase | SHIFT + jog = previous / next marker; SHIFT + ARROW = step back / forward one bar. |
| Shuttle speeds | Notes 65–78 | bridge → Cubase | SHUTTLE (SHIFT + SCRUB): reverse 8× … 1/8×, then 1/8× … 8×; zero = the Stop note. |
| Channel encoder | Note 40 (left) / 41 (right) | bridge → Cubase | One note pulse per 4 detents. Select previous / next track. |
| Jog wheel | Note 42 (left) / 43 (right) | bridge → Cubase | One note pulse per 4 detents. Nudge cursor left / right. In SCRUB the jog goes to the Mackie port instead (below). |
| Transport | Notes 91 / 92 / 93 / 94 / 95 / 86 / 87 | both | Rwd, ffw, stop, play, rec, cycle, click; Cubase → bridge sets the LED. |
| ARROW ◀ / ▶ | Note 40 / 41 | bridge → Cubase | One channel-encoder step: previous / next track. |
| BANK ◀ / ▶ | UF8 note 46 / 47 | bridge → Cubase (UF8 port) | Banks the UF8. Without a UF8: eight steps of note 40 / 41, 40 ms apart. |
| NAV cross | Notes 48–52 | bridge → Cubase | Up, left, centre, right, down: vertical zoom in, zoom out, open / close editor, zoom in, vertical zoom out. |

## SysEx: names, colours and display

The scripts send display data to the bridge as SysEx on the surface's output port. All messages use the non-commercial manufacturer ID `7D`, followed by the ASCII tag `52 36` ("R6"). The strip byte is 0–7 (strip 1 = 0), the same index as the strip's MIDI channel.

| Item         | Message                                         | Direction       | Notes |
|--------------|-------------------------------------------------|-----------------|-------|
| Track name   | `F0 7D 52 36 01 <strip> <text…> F7`             | Cubase → bridge | ASCII, up to 32 bytes. Non-ASCII characters are sent as `3F` (`?`); the bridge maps the text to what the UF8 display can show. |
| Track colour | `F0 7D 52 36 02 <strip> <r> <g> <b> F7`         | Cubase → bridge | 0–127 per channel. The bridge quantises to the UF8 palette. |
| UF1 level text | `F0 7D 52 36 03 <text…> F7` | Cubase → bridge | The selected track's fader level, e.g. `-4.94`. |
| UF1 value line, temporary | `F0 7D 52 36 04 <text…> F7` | Cubase → bridge | A send, e.g. `S1 -6.0dB`. Shown for 3 s after the last change, then the home text returns. Max 12 characters. |
| UF1 bar, temporary | `F0 7D 52 36 05 <pos> [<style>] F7` | Cubase → bridge | Position 0–100. Style 0x01 pointer, 0x02 fill (default). |
| UF8 pan position | `F0 7D 52 36 06 <strip> <pos> F7` | Cubase → bridge | 0–127 (64 = centre). The readout bar under the strip, drawn as a line. |
| UF8 pan text | `F0 7D 52 36 07 <strip> <text…> F7` | Cubase → bridge | Cubase's pan text (e.g. `L23`, `C`). The value line shows `Pan` left and the text right. |
| UF1 value line, home | `F0 7D 52 36 08 <text…> F7` | Cubase → bridge | Pan, e.g. `Pan L23`. The line's normal content. |
| UF1 bar, home | `F0 7D 52 36 09 <pos> [<style>] F7` | Cubase → bridge | As 05, for the home view. |
| UF1 track selected | `F0 7D 52 36 0A F7` | Cubase → bridge | Show the home view now and ignore temporary values for 0.5 s (the new track's sends arrive in a burst). |
| UF8 bank-right request | `F0 7D 52 36 0B [<count>] F7` | Cubase → bridge | The bridge sends `count` bank-right presses (note 47, default 1, at most 64), 100 ms apart, held until 3 s after Cubase first spoke. With more than one press the UF8 is frozen as for a jump (SysEx 10). |
| UF8 fader level text | `F0 7D 52 36 0C <strip> <text…> F7` | Cubase → bridge | The strip's dB readout (6 characters + `dB`). |
| UF1 transport position | `F0 7D 52 36 0D <text…> F7` | Cubase → bridge | Cubase's time text, e.g. `  1. 1. 1.   0` or `0:00:47.691`, for the 7-segment time field. The seconds format is shown as blank-separated groups, hours and minutes only when not zero: `47 691`, `3 47 691`, `1 03 47 69`. |
| UF1 track number | `F0 7D 52 36 0E <high 7 bits> <low 7 bits> F7` | Cubase → bridge | The selected track's number, counted like the MixConsole (hidden tracks included), for the channel-number box. 0 = not known: the box shows the UF8 strip instead. |
| UF8 bank-left request | `F0 7D 52 36 0F [<count>] F7` | Cubase → bridge | As 0B, with bank-left presses (note 46). |
| UF8 jump | `F0 7D 52 36 10 <count> F7` | Cubase → bridge | Bank reset (note 79), then `count` bank-rights 100 ms apart, then note 80 250 ms later. Held until 5 s after Cubase first spoke. The UF8 is frozen meanwhile: what Cubase sends for the banks passed is only cached, and 400 ms after the last press the end state is shown once. Used to follow the selection. |
| UF8 tick | `F0 7D 52 36 11 F7` | Cubase → bridge | The bridge sends note 80 after 300 ms. Not used any more (see note 80). |
| UF1 send cell | `F0 7D 52 36 15 <pot 0–3> <position 0–100> <name>|<level> F7` | Cubase → bridge | A send's name (the destination, without Cubase's "1- " prefix, shortened to 9 characters) and level text, for the SENDS view's V-pot cells. Sent in DAW and SENDS (not PLUGIN); empty text = no send. |
| Helper: channel name | `F0 7D 52 36 18 <index hi> <index lo> <name> F7` | helper → bridge → UF8 script | A channel's name in the list of all channels (inputs / outputs excluded, up to 256), 3 × 7 bits per character (UTF-16). Sent on change and on request. |
| Helper: recordable name | `F0 7D 52 36 19 <slot> <name> F7` | helper → bridge → UF8 script | A recordable channel's name (groups, FX, VCA, inputs / outputs excluded, up to 128), encoded as above. |
| Lists request | `F0 7D 52 36 1B F7` | UF8 script → bridge → helper | Send both lists again (every 2 s until names arrive). |
| Same-name rank | `F0 7D 52 36 1C <rank> <name> F7` | UF8 script → bridge → UF1 script | Several tracks share the selected track's name: which one is selected (0 = topmost), with the name (encoded as above). For the UF1's track number. |
| UF1 V-pot position | `F0 7D 52 36 17 <pot 0–3> <high 7 bits> <low 7 bits> F7` | Cubase → bridge | The V-pot's value in Cubase (0..16383), on every change and once a second. The bridge takes it unless the V-pot was turned in the last 0.5 s. |
| UF8 freeze | `F0 7D 52 36 13 <01 / 00> F7` | Cubase → bridge | 01: hold the UF8 frozen (display, faders, LEDs, meters: cached, not shown) across several jumps; 00: release and show the end state. Used while the script looks for the selected track at start-up. Released by itself after 30 s. |
| UF8 bank size | `F0 7D 52 36 12 <total hi> <total lo> <offset hi> <offset lo> F7` | Cubase → bridge | The number of channels in the UF8 bank and where the bank starts (0 = first); `7F 7F` = not known. Sent when either changes; the bridge uses it to stop bank-right at the last full bank. |

Names and colours are sent whenever the bank changes or the track's name or colour changes. An empty name clears the strip. For the UF1, strip is always 0.

## Mackie Control (scrubbing)

The bridge talks to a Mackie Control device in Cubase only for what the MIDI Remote API cannot do well:

| Item | Message (MCU port) | Direction | Notes |
|------|--------------------|-----------|-------|
| Scrub mode | Note 101 (0x65), 127 then 0 | bridge → Cubase | Toggles Cubase's scrub mode (UF1 SCRUB). |
| Scrub LED | Note 101, velocity > 0 = on | Cubase → bridge | Keeps the UF1 SCRUB state and LED in step. |
| Jog | CC 60 (0x3C), 0x01 = clockwise, 0x41 = counter-clockwise | bridge → Cubase | One tick per detent while scrubbing. |

Everything else Cubase sends on that port (display, LEDs, faders) is ignored.

## UC1 messages

The UC1 script finds the SSL plug-ins on the selected track and drives their parameters itself (Direct Access); the bridge only passes events on and paints. Knob and button ids are Rea-Sixty's (`extension/src/UC1Protocol.h`). Names and texts are encoded 3 × 7 bits per character (UTF-16), as for the helper; the bridge shows Latin-1.

| Item | Message | Direction | Notes |
|------|---------|-----------|-------|
| Knob | `F0 7D 52 36 20 <knob id> <64 + clicks> F7` | bridge → Cubase | Clicks signed (usually ±1..3). |
| Button | `F0 7D 52 36 21 <button id> <1 pressed / 0 released> F7` | bridge → Cubase | All buttons except the note-bound ones below. |
| CHANNEL encoder | Note 100 (next) / 101 (previous), press + release | bridge → Cubase | `Project / Select Track: Next / Prev`. One step per detent: the bridge counts 3 ticks per step (Rea-Sixty's accumulator, reset after 100 ms or a change of direction). |
| Bus Comp encoder | `F0 7D 52 36 20 15 <64 + steps> F7` | bridge → Cubase | Steps counted like the CHANNEL encoder. |
| Polarity / Solo Clear / Solo / Cut | Notes 0x19 / 0x1B / 0x1C / 0x1D | both | Bound in the script (phase switch, `Edit / Deactivate All Solo`, solo, mute of the selected track); Cubase → bridge lights the LED. |
| Track name | `F0 7D 52 36 30 <name> F7` | Cubase → bridge | The small (Channel Strip) name row, 12 characters. |
| Track colour | `F0 7D 52 36 31 <r> <g> <b> F7` | Cubase → bridge | 0–127 each; the bridge quantises to the UC1 palette. |
| Model field | `F0 7D 52 36 32 <label> F7` | Cubase → bridge | "CS 2", "4K E", "BC 2" …; empty = no plug-in (the layout mode follows). |
| Value readout | `F0 7D 52 36 33 <zone> <text> F7` | Cubase → bridge | Zone 03 Channel Strip, 05 Bus Comp; text = name padded to 16 + value right-aligned in 7. |
| Button LED | `F0 7D 52 36 34 <button id> <0 off / 1 on / 2 dim> F7` | Cubase → bridge | |
| Clear readout | `F0 7D 52 36 35 <zone> F7` | Cubase → bridge | |
| Knob ring | `F0 7D 52 36 36 <knob id> <hi> <lo> <dim> F7` | Cubase → bridge | Position 0–16382 (already flipped for knobs whose parameter runs the other way), 7F 7F = ring off. The bridge draws it with Rea-Sixty's cell maps (dot, or a dot fading into the next cell on the bipolar knobs). |
| 7-segment | `F0 7D 52 36 37 <hi> <lo> F7` | Cubase → bridge | 0–999, leading zeros blank; 7F 7F = blank. Written again after the rings that share its cells (Bus Comp Release / Mix, Fader Level). |
| Bus Comp name row | `F0 7D 52 36 38 <prev> 7F 7F 7F <curr> 7F 7F 7F <next> F7` | Cubase → bridge | The tracks of the previous, current and next bus comp (the large name row, 12 characters each). |
| Bus Comp header | `F0 7D 52 36 39 <text> F7` | Cubase → bridge | Shown for 1.5 s in place of the model field ("BUS COMP 2"), as SSL 360 does while the Bus Comp encoder turns. |

## Not yet covered

- The UF1 MODE view METER (soft key 3).
- A version or ping message. Add one (`F0 7D 52 36 7F …`) before the protocol grows.
