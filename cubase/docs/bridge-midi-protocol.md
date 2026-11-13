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
| Automation Write  | Note 112 + strip−1, vel 127 / 0  | both | SEL in the AUTO selection mode: the bridge sends the press here instead of on SEL's note; the script toggles the track's automation Write and sends its state back on the same note (the bridge lights SEL red). In REC the bridge sends SEL presses on the top soft key's note (record-enable). |
| SSL soft key focus | `F0 7D 52 36 21 <page> <slot> F7` (`7F 7F` = none) | bridge → Cubase | A top soft key focused (or unfocused) a parameter of SSL's soft-key pages (page 0 = V-POT, 1–5; slot 0–7, Rea-Sixty's softkey::kCsBanks; page + 8 = the bus comp's pages, kBcBanks, picked with Q2, Q1 back). The bridge owns the domain (Q1 / Q2 LEDs), the page (bank keys, ids 0x68–0x6D), the labels (slot cell per strip) and the key LEDs. |
| Focused V-pot | `F0 7D 52 36 22 <strip> <delta + 64> F7`, `23 <strip>` | bridge → Cubase | While a parameter is focused the V-pots go here instead of CC 16 + strip (turn) and the push instead of note 32 + strip; the script works the parameter on that strip's SSL channel strip (turn 2 % per detent; a switch follows the direction; push toggles a switch or puts a gain / trim at 0 dB). |
| Focused value | `F0 7D 52 36 20 <strip> <has> <position 0–127> <ring> <text> F7` | Cubase → bridge | The focused parameter on one strip: has 0 = no SSL channel strip there (or not on that model); ring 1 unipolar, 2 bipolar (centre bar); the plug-in's value text. The bridge shows it in the value line and the ring instead of pan, and pan again when the focus ends. |
| CHANNEL encoder | Channel 2, notes 0–5 (127 then 0); notes 44 / 45 | bridge → Cubase | One detent of the UF8's CHANNEL encoder (FF 24 with pot 8), by mode: Channel Select = notes 0 / 1 (*Select Track: Prev / Next*), Nudge = 2 / 3 (*Nudge Cursor Left / Right*), Markers = 4 / 5 (*Locate Previous / Next Marker*), Bank by 1 = the Page notes 44 / 45. The mode is the bridge's: hold the encoder's push (id 0x76) and turn to pick it (the scribble strips list the modes, the current one on strip 4 with its top soft key lit); NAV (0x73) / NUDGE (0x74) switch Markers / Nudge on and off (their LEDs lit while on). |
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
| Faders / V-pots on | Channel 2, notes 80–92 | bridge → Cubase | What the faders and V-pots are on (two sub-page areas): faders 80 = track volume, 81 = track pan (FLIP), 82–89 = send 1–8 of each track (SEND / PLUGIN 1–8; CUT = the send on / off); V-pots 90 = track pan, 91 = track volume (FLIP), 92 = sends 1–8 of the selected track (FLIP held 0.4 s). CHANNEL (left panel) = home: volume and pan. The bridge lights SEND / PLUGIN N and FLIP. In a send view the strip name is the send's destination (the faders' view wins). |
| UC1 secondary encoder's job | `F0 7D 52 36 4C <0-3> F7` | bridge → Cubase | Sent with the favourites (after 42): 0 bus-comp, 1 strips, 2 cs-fav, 3 bc-fav, from `uc1-encoder2` in cubase\settings.txt. The script changes it when the push is held and the encoder turned; a short push opens / closes the window of the plug-in it is on. |
| UC1 EXT FUNCS | `4B <1>` Cubase → bridge (open); `4A <prev> 00 <current> 00 <next> 00 <long> 00 <value> 00 <pos> <active>` Cubase → bridge (draw); `4D <op> <64 + steps>` bridge → Cubase (1 turn, 2 push, 3 closed) | both | BACK on the main screen opens it (the script asks, the bridge shows CentralMode ExtFuncs); the secondary encoder scrolls / adjusts, its push switches, BACK / CONFIRM close. |
| Follow quiet | `F0 7D 52 36 5D <n> F7` | bridge → Cubase (UF8 port) | The UF1's BANK without a UF8 moves the UF8 script's strips and selects on them: the script's follow selection does nothing for n × 100 ms (the bridge sends 25 per press), as the selection reports then are late and on the old bank. |
| UC1 key jobs | `F0 7D 52 36 4F <key> <job> F7` | bridge → Cubase (UC1 port) | Settings: UC1 keys. key 0 magnifier, 1 360, 2 secondary encoder push; job 0 next strip, 1 previous strip, 2 the strip's window, 3 Channel Settings, 4 MixConsole, 5 bypass the strip's insert, 6 nothing. Sent with the favourites (on 42) and when a setting changes. |
| UC1 on Cubase's strip (Settings) | `F0 7D 52 36 5E <loading> <DYN IN's compressor> <Bus Comp start> F7` | bridge → Cubase (UC1 port) | Sent when the UC1 script starts and when one of the three settings changes. loading: 0 = knobs and keys load a missing gate / compressor, 1 = the keys only (DYN IN, EXP), 2 = never. DYN IN's compressor: 0 Standard, 1 Tube, 2 Vintage, 3 the last one picked (since the script started). Bus Comp start: 0 = the bus comp on an output channel (else the first), 1 = the selected track's when it has one. |
| Pulse a CC | `F0 7D 52 36 47 <cc> F7` | Cubase → bridge (UC1 port) | The bridge sends the CC 127, then 0 40 ms later: a command bound to that CC (the MixConsole key job, CC 99: Devices > Mixer). Insert bypass per slot: CC 80 + slot (through SysEx 3A, as the edit switches on CC 50 + slot). |
| UC1 knob step | `F0 7D 52 36 4E <0/1/2> F7` | bridge → Cubase (UC1 port) | Settings: UC1 knob steps. One knob click moves a value by 1/128 (0, fine), 1/64 (1, normal, the default) or 1/32 (2, coarse) of its range; FINE makes it a quarter of that. Sent when the script asks for the favourites (42) and when the setting changes. |
| No project | `F0 7D 52 36 7C <0/1> F7` | Cubase → bridge (each surface's port) | 0: the script's driver was deactivated (Cubase closed the project and shows the Hub): the bridge blanks that surface (UF8: every strip empty, faders down, value lines and V-pot bars off; UF1 / UC1: "No Project", no colour / level / model, meters and fader down, rings off), and the LEDs Cubase drives go off. 1: active again (a project opened); the scripts send everything again, and the UF8 script runs its start-up follow again. |
| UF8 Nav Mode | Channel 2, notes 10–25 | bridge → Cubase | NAV held 1 s turns Nav Mode on / off (a tap still puts the encoder on Markers). The top soft keys then send 10–17 = Transport / To Marker 1–8 (Q2, the default) or 18–25 = Recall Cycle Marker 1–8 (Q1), followed 40 ms later by 26 = To Left Locator (recalling only sets the locators). The keys go by marker ID: an ID the project does not have does nothing. Labels "Marker N" / "Cycle N"; the last one pressed lit. |
| UF8 SSL Strip Mode | `24 <0/1>`, `25 <strip> <msb> <lsb>` bridge → Cubase; `26 <strip> <fader 0/1> <pan 0/1>`, `27 <strip> <msb> <lsb>` Cubase → bridge, `28 <strip> <64 + delta>` (V-pot on the strip's pan), `29 <strip>` (its push: centre) bridge → Cubase (all `F0 7D 52 36 … F7`) | both | The PLUGIN key turns it on / off (24, PLUGIN LED). The script puts each strip whose track has an SSL channel strip with a fader / output gain parameter on it (26 = 1); the bridge then sends that strip's hand position as 25 (14 bit) instead of pitch bend, ignores Cubase's volume there, and drives the motor from 27; the dB field shows the strip fader's value (0C). With a pan parameter (Pan / Panning / Stereo Pan / Pan/Bal / Balance) the V-pot goes on it too (26 pan = 1), shown as the track's pan is (06 / 07). Strips without one, FLIP and the send views keep the fader / V-pot where it was. |
| Send level on the V-pot | `F0 7D 52 36 0D <strip> <text> F7`, `F0 7D 52 36 0E <strip> <0–127> F7` | Cubase → bridge | The V-pots on the selected track's sends: value line "Send" + the level, and a unipolar bar. |
| UF8 send views (10.11.2026) | the strip-mode messages: 25 / 27 (faders), 28 / 29 (V-pots), 26 per strip | both ways | In the fader send view (send N of every track) and the V-pot send view (the selected track's sends) the levels go through Direct Access, not MIDI Remote bindings (a binding on a send level made Cubase pause ~0.8 s at a time while it kept changing). The script says per strip that the fader / V-pot is its own (26), the bridge sends the hand as 25 and the V-pot as 28 (the push as 29 = the send on / off; in the V-pot send view the turn goes as `2A <strip> <msb> <lsb>`, the level itself, which the bridge keeps and draws at once), and always so in a send view; the script writes the send slot's level and reports it back: 27 for the motor, 0C for the dB field, 0D / 0E for the V-pot's value line and ring. In a fader send view the bridge ignores Cubase's pitch bend: the faders' bindings there are placeholders on the send's on switch (as are the V-pots' in the V-pot send view). |

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
| V-pots 1–4 | 14-bit CC, absolute. DAW / SENDS: 21–24 (high 7 bits) + 53–56 (low 7 bits); PLUGIN: 17–20 + 49–52 | bridge → Cubase | DAW / SENDS: sends 1–4 / 5–8 of the selected track. The script writes the send slot's level through Direct Access (setParameterProcessValue on the channel's Sends > slot > Level) and reads it back for the display; a MIDI Remote binding on a send level made Cubase pause ~0.8 s at a time while it kept changing (10.11.2026). The V-pot knobs (17–20) are bound to the send's on switch there as a placeholder (a knob needs a binding in every sub page). PLUGIN without an SSL plug-in: Quick Controls 1–4 / 5–8 on 17–20 (bindings). The bridge keeps each V-pot's position, adds the turns itself and draws them at once (SysEx 17 reports Cubase's position; ignored for 0.5 s after a turn); in DAW it also draws "S1 …" and the bar on the small display while turning. |
| V-pot pushes | Notes 33–36 | bridge → Cubase | Send on / off. Note 32 = push of the pan knob. The channel encoder's push (note 37 before) is handled in the bridge: PLUGIN event kind 5 (the script opens / closes the window), or the SEL note (24, toggles the channel editor) when the PLUGIN view is on no SSL plug-in. |
| 5–8 | Note 53 (sends 1–4) / 54 (sends 5–8); in the PLUGIN view 88 (Quick Controls 1–4) / 89 (5–8) | bridge → Cubase | The bridge toggles on each press and lights the 5–8 LED for 5–8. |
| MODE + display soft keys | Note 53 / 54 (DAW) or 88 / 89 (PLUGIN) | bridge → Cubase | Handled in the bridge: while MODE is held, the four display soft keys show the view picker (1 PLUGIN, 2 DAW, 3 METER, 4 SENDS; labels at cell 0x0104, LEDs 0x01–0x04, the active view lit yellow). Picking a view sends the V-pot page for that view and the current 5–8 state (SENDS uses the DAW notes). |
| MASTER / FLIP | Notes 44–47 | bridge → Cubase | Strip sub-page: 44 selected track, 45 main output, 46 selected track flipped, 47 main output flipped. The bridge toggles MASTER and FLIP and lights their LEDs. |
| SHIFT + keys | Notes 56–61 | bridge → Cubase | SHIFT held in the bridge. ◀ = 56 Read, ▶ = 57 Write (selected track), Cycle = 58 Cross-Over, Click = 59 Trim, 1 = 60 Latch, 2 = 61 Touch. |
| Meter | CC 80 (left), CC 81 (right), 0–127 | Cubase → bridge | From the script (already in dB, −55…0 dBFS); the level meter on the small display. |
| UF8 V-pot push: centre pan | `F0 7D 52 36 2B <strip> F7` | bridge → Cubase (UF8 port) | The setting `uf8-vpot-push` = centre (default) and the V-pot on the track's own pan: the script sets the pan to the centre (Direct Access on the strip's channel where it shows a pan parameter, else a value variable bound to the pan). With `editor`, and under FLIP, the push is the note that opens the channel editor. |
| BYPASS state | Note 62, velocity 0 / 64 / 127 | Cubase → bridge | From the script: no inserts / inserts active / all bypassed. LED id 0x00 off / orange 35 % / orange. |
| BYPASS press | `F0 7D 52 36 1F <0 remember / 1 all> F7` | bridge → Cubase (UF1 port) | Key id 0x18 pressed; the byte is the setting `uf1-bypass` (1: nothing is remembered, a second press switches every insert on). The UF1 script toggles: with any insert active it bypasses the active ones and remembers (per track object) the ones bypassed already; with all bypassed it switches on the ones it bypassed (nothing remembered: all). It answers with the slots to set (below). |
| BYPASS slots | `F0 7D 52 36 1E <1 bypass / 0 active> <slots: 7 + 7 + 2 bits> F7`, then channel 2, notes 0–15, velocity 127 / 0 | Cubase → bridge, then bridge → Cubase | The script's answer to a press; the bridge sends each named slot its state as a note to the key the script bound to that slot's bypass. (Set from the script through the value variables instead, the variables stopped following Cubase to the next track.) |
| 360° | Note 55 | bridge → Cubase | `Transport / Exchange Time Formats`. |
| Markers / bar steps | Notes 79 / 80, 81 / 82 | bridge → Cubase | SHIFT + jog = previous / next marker; SHIFT + ARROW = step back / forward one bar. |
| Shuttle speeds | Notes 65–78 | bridge → Cubase | SHUTTLE (SHIFT + SCRUB): reverse 8× … 1/8×, then 1/8× … 8×; zero = the Stop note. |
| Channel encoder | Note 40 (left) / 41 (right) | bridge → Cubase | One note pulse per 4 detents. Select previous / next track. |
| Jog wheel | Note 42 (left) / 43 (right) | bridge → Cubase | One note pulse per 4 detents. Nudge cursor left / right. In SCRUB the jog goes to the Mackie port instead (below). |
| Transport | Notes 91 / 92 / 93 / 94 / 95 / 86 / 87 | both | Rwd, ffw, stop, play, rec, cycle, click; Cubase → bridge sets the LED. |
| ARROW ◀ / ▶ | Note 40 / 41 | bridge → Cubase | One channel-encoder step: previous / next track. |
| BANK ◀ / ▶ | UF8 note 46 / 47 | bridge → Cubase (UF8 port) | Banks the UF8. Without a UF8: eight steps of note 40 / 41, 40 ms apart. |
| NAV cross | Notes 48–52 | bridge → Cubase | Up, left, centre, right, down: vertical zoom in, zoom out, open / close editor, zoom in, vertical zoom out. In the Playhead and Scrub jog modes only. |
| Jog modes | Channel 2, notes 30–55 | bridge → Cubase | The jog and nav cross in the Items / Envelope / Range / Fades jog modes (the mode, the aim and SHIFT are kept in the bridge). 30–33 Nudge Left / Right / Up / Down, 34–41 Nudge Start Left / Right, End Left / Right, Top Up / Down, Bottom Up / Down (38–41 bound but unused: they did nothing in the Project window), 42–45 Decrement / Increment Fade In Length, Decrement / Increment Fade Out Length, 46–49 Navigate Left / Right / Up / Down, 50–53 Navigate Add Left / Right / Up / Down, 54 Zoom to Selection, 55 Undo Zoom. Nav cross LEDs 0x10–0x14 (up, left, centre, right, down) show the aim. |

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
| UF1 PLUGIN: plug-in | `F0 7D 52 36 50 <present 0/1> <page> <pages> <eq in 0/1> <ordinal> <model…> 00 <name…> F7` | Cubase → bridge | The SSL plug-in the PLUGIN view shows (the selected track's channel strip, else its bus compressor): the page shown (0-based) of how many, whether its EQ is in, its position among the track's SSL channel strips, and the model (`CS 2`, `4K B`, `4K E`, `4K G`, `Link`, `BC 2`; empty = none), then the plug-in's name in Cubase (for its preset folder). |
| UF1 PLUGIN: cell | `F0 7D 52 36 51 <pot 0–3> <position 0–100> <flags> <label>|<value> F7` | Cubase → bridge | One V-pot cell. Flags: 1 = bipolar (centre bar), 2 = used. With an SSL plug-in its control on the current page; without one, the Quick Control's name and value. |
| UF1 PLUGIN: soft key | `F0 7D 52 36 52 <key 0–3> <state> <label…> F7` | Cubase → bridge | The plug-in's switch on this soft key: state 0 off, 1 on (LED lit), 2 no switch (label only). 0x7F in the label = SSL's Ø glyph. |
| UF1 PLUGIN: event | `F0 7D 52 36 53 <kind> <a> <b> <fine> F7` | bridge → Cubase | PLUGIN view with an SSL plug-in: kind 0 = V-pot a turned by b − 64 detents (fine = 1: a quarter of the step), 1 = V-pot a pushed, 2 = soft key a pressed, 3 = page back (a = 0) / forward (a = 1), 5 = the CHANNEL encoder's push: open / close the window of the plug-in shown (the script moves one insert viewer to its slot, checks it by name, and toggles its window; sixteen fixed viewers made track changes lag, and steps sent as MIDI notes landed on the wrong slot), 4 = SHIFT + CHANNEL encoder: the next (a = 1) / previous (a = 0) SSL plug-in on the track, then none (Quick Controls), wrapping; also without a plug-in shown. |
| Track step (MixConsole) | `F0 7D 52 36 5C <1 forward / 0 back> F7` | bridge → Cubase (Helper port; UF1 port if the helper's ports are missing) | With `track-stepping = mixconsole` (the default) the UF1 arrows and the UF1 / UF8 / UC1 CHANNEL encoders step through the helper script: it keeps which channel of its all-channels zone (the MixConsole's visible channels, in its order) is selected, and selects the next / previous one directly (`mValue.mSelected`). A press within 1.5 s of the last counts from the last target. From a track not in the list (a folder, the Stereo Out) it triggers Cubase's own next / previous track first. Cubase's next / previous track alone skips the tracks in collapsed folders, so counting them (the UF1 script's old way, kept as the fallback) does not work with collapsed folders. Logged per step in scripts.log. |
| UF1 strip mode | `F0 7D 52 36 5A <fader 0/1> <pan 0/1> F7`, `F0 7D 52 36 5B <msb> <lsb> F7` | Cubase → bridge | 5A: the fader is on the SSL channel strip's fader (1) or the track's volume (0); while 1 the bridge ignores Cubase's volume and sends the hand's position as PLUGIN event kind 6 (a = msb, b = lsb, 14 bit). 5B: the strip's fader value for the motor. Pan = 1: the pan knob goes to the script as PLUGIN event kind 7 (b = 64 + delta) and turns the strip's pan, shown on the value line. |
| UF1 strip pick | `F0 7D 52 36 59 <kind> <ordinal> F7` | Cubase → bridge | SHIFT + CHANNEL moved the UF1's PLUGIN view to a channel strip (kind 1 = SSL, ordinal among the track's SSL plug-ins; 2 = Cubase's own). The bridge passes it to the UC1 script as `45 <kind> <ordinal>`, which goes to the same strip. |
| UF1 PLUGIN: plug-in slot | `F0 7D 52 36 58 <slot> F7` | Cubase → bridge | The insert slot (0–15) of the plug-in the PLUGIN view is on, 7F = none. |
| UF1 script started | `F0 7D 52 36 54 F7` | Cubase → bridge | Sent when the script's page activates (Cubase started, scripts reloaded); the bridge answers with the V-pot page note of the view it shows (53 / 54 / 88 / 89). |
| UF1 Cubase EQ | `F0 7D 52 36 55 <text…> F7` | Cubase → bridge | For the EQ graph on a track without an SSL channel strip: Cubase's channel EQ bands and input-filter cuts that are on, as `<kind> <Hz> <dB> <Q> <order>` separated by `;` (kind B bell, L / H low / high shelf, P high pass / low cut, C low pass / high cut; an inverted band's gain already turned round). `-` = nothing on (flat line). Sent on every change. |
| UF1 Quick Controls source | `F0 7D 52 36 56 <text…> F7` | Cubase → bridge | What the Quick Controls on the V-pots belong to (Cubase's object title of the first one assigned, usually a plug-in), shown in the header in PLUGIN without an SSL plug-in. `-` = not known. |
| UF1 strip choice | `F0 7D 52 36 57 <kind> <ordinal> F7` | bridge → Cubase (UF1 script) | The channel strip the UC1 is on (its magnifier key), passed on from the UC1 script's `41 <kind> <ordinal>`: kind 0 none, 1 an SSL strip (ordinal = its position among the track's SSL plug-ins, bus comps included), 2 Cubase's own strip. The UF1's PLUGIN view then shows the same one. |
| UF1 Solo Clear | Channel 2, note 20 | bridge → Cubase | Quick key 1 (without SHIFT): the script runs Cubase's *Deactivate All Solo*. |
| Solo active | `F0 7D 52 36 1D <0 / 1> F7` (helper port) | Cubase → bridge | From the helper script, which watches the solo of every channel in its list (Cubase's MIDI Remote has no project-wide solo state): 1 = something is soloed. The UF1 then shows the firmware's SOLO ACTIVE (cell 0x0120 = FF, not in the meter / browser layouts) and lights quick key 1 (LED 0x1C, yellow). Sent on change and with the lists. |
| UF1 preset values | `F0 7D 52 36 3C 00 <id> 7F 7F 7F <value> F7`, then `3D 00 <name>` | bridge → Cubase | The UF1 preset browser's load (SHIFT + 5-8, V-pot 4 push): as the UC1's (3 × 7 bits per character); the script sets them in the plug-in the PLUGIN view shows. |
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
| Plug-ins for PRESETS | `F0 7D 52 36 3B <cs plug-in> 7F 7F 7F <bc plug-in> F7` | Cubase → bridge | The SSL plug-ins the two sections are on (empty = none); the bridge's PRESETS screen lists their library. |
| Preset value | `F0 7D 52 36 3C <section> <id> 7F 7F 7F <value> F7` | bridge → Cubase | One `<PARAM id value>` of the preset file being loaded; section 0 Channel Strip, 1 Bus Comp. |
| Preset end | `F0 7D 52 36 3D <section> <name> F7` | bridge → Cubase | All values sent; the script reports and shows "<name> LOADED". |
| Routing switches | `F0 7D 52 36 3F <f-in> <f-sc> <eq-sc> <dyn-pre-eq> <ext-sc> F7` | Cubase → bridge | The channel strip's routing switches, 0 / 1 (7F = no routing), for the ROUTING screen. |
| Set routing | `F0 7D 52 36 3E <f-in> <f-sc> <eq-sc> <dyn-pre-eq> <ext-sc> F7` | bridge → Cubase | The ROUTING screen's choice. |
| Gain reduction source | `F0 7D 52 36 40 <cs track hi> <lo> <cs ordinal> <bc track hi> <lo> <bc ordinal> <cs model> F7` | Cubase → bridge | Which SSL plug-ins the meters read: the channel strip's MixConsole number and its position among the SSL strips / bus comps there (insert order), the bus comp's MixConsole number and its position among the bus comps there; model = "CS 2", "4K E" … (0 = none). The SSL plug-ins announce the same MixConsole number to SSL 360 Core. |
| Send CC back | `F0 7D 52 36 3A <cc> <value> F7` | Cubase → bridge | The bridge sends CC `<cc>` `<value>` (channel 1) to Cubase on the UC1 port: lets the script set a host value it has bound to a CC (the strip Gate / Compressor on switches, CC 40 / 41). |
| Bus Comp header | `F0 7D 52 36 39 <text> F7` | Cubase → bridge | Shown for 1.5 s in place of the model field ("BUS COMP 2"), as SSL 360 does while the Bus Comp encoder turns. |
| Ask for favourites | `F0 7D 52 36 42 F7` | Cubase → bridge | The bridge reads `cubase\favourites.txt` (written with a default list when missing) and answers with two `43`. Sent when the page activates and when stepping after 5 s without. |
| Favourites | `F0 7D 52 36 43 <domain> <name> 7F 7F 7F <name> … F7` | bridge → Cubase | Domain 0 channel strips, 1 bus comps; up to eight names each, as Cubase lists the plug-ins. |
| Favourite step | `F0 7D 52 36 44 <domain> <dir> F7` | bridge → Cubase | One notch of the UF8's CHANNEL encoder in CS Fav / BC Fav mode; dir 1 forward, 0 back. The script shows the favourites on the name row and swaps the plug-in 0.8 s after the last step (counting from the last swap until Cubase shows it). |

## Script log

| Item | Message | Direction | Notes |
|------|---------|-----------|-------|
| Log line | `F0 7D 52 36 7E <source> <text> F7` | Cubase → bridge (any script's port) | Source 1 UF8, 2 UF1, 3 UC1, 4 Helper; text 3 × 7 bits per character. Written to `cubase\logs\scripts.log` (dropped with `--no-script-log`). At most 20 per idle tick. |

## Not yet covered

- The UF1 MODE view METER (soft key 3).
- A version or ping message. Add one (`F0 7D 52 36 7F …`) before the protocol grows.
