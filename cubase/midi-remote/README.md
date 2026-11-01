# MIDI Remote scripts

The Cubase side of Cubase-Sixty: one MIDI Remote script per surface. They speak the MIDI defined in [`../docs/bridge-midi-protocol.md`](../docs/bridge-midi-protocol.md) and map it to Cubase's mixer and transport.

| Folder | Script | Surface | What it does |
|--------|--------|---------|--------------|
| `UF8/` | `CubaseSixty_UF8.js` | SSL UF8 | Eight faders with touch (Touch automation works), pan on the V-pots (value and bar on each strip's display), SEL / SOLO / CUT with LEDs, top soft keys toggle record-enable (dark on strips that cannot record), V-pot push opens the channel editor, bank left / right, starts on the bank with the selected track, automation Read / Write on the selected track, foot switches (play / record), track names and colours on the scribble strips. |
| `UF1/` | `CubaseSixty_UF1.js` | SSL UF1 | One strip that controls the **selected track**: fader with touch, SOLO / CUT / SEL, pan on the knob above the fader, V-pots 1–4 = sends 1–4 (turn = level, push = on / off), channel encoder = previous / next track, jog wheel = nudge cursor, transport keys (rwd, ffw, stop, play, record, cycle, click) with LEDs. The display shows the track name, level, colour, and a value line with pan, or a send for 3 seconds after it is turned. |

Tested with Cubase 15 Pro on Windows. The folder names, the file names and the device names in the scripts must match (`CubaseSixty` / `UF8` / `UF1`), or Cubase will not list the script.

## Setup

1. **Install loopMIDI** (https://www.tobias-erichsen.de/software/loopmidi.html). The bridge uses its driver to create the MIDI ports itself, so you do not create any ports in loopMIDI. The ports are `Cubase-Sixty UF8 to Cubase`, `Cubase-Sixty UF8 from Cubase`, `Cubase-Sixty UF1 to Cubase` and `Cubase-Sixty UF1 from Cubase` ("to Cubase" is where the bridge writes and Cubase reads, "from Cubase" the other way round). They exist only while the bridge runs. If you made ports with these names in loopMIDI earlier, delete them. (Without the driver library, or with `--loopmidi`, the bridge uses ports you create in loopMIDI with exactly these names instead.)
2. **Keep the ports out of All MIDI Inputs.** Once the bridge is running, open *Studio > Studio Setup > MIDI > MIDI Port Setup* in Cubase and untick **In 'All MIDI Inputs'** for all the ports. Otherwise a selected instrument or MIDI track receives the controller messages as played notes. Cubase may tick it again for a port it sees as new, so check after the first start.
3. **Install the scripts.** Run
   ```
   powershell -ExecutionPolicy Bypass -File cubase\midi-remote\tools\install-scripts.ps1
   ```
   It copies each script to `<Documents>\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty\<UF8|UF1>\` (it finds your Documents folder even when it is redirected to OneDrive) and removes the old `SSL` folder from earlier versions. If your Cubase driver-script folder is elsewhere, pass it with `-Destination "<path to ...\Driver Scripts\Local>"`. Run it again after each edit.
4. **Activate.** Restart Cubase, or press **Reload Scripts** in the MIDI Remote Script Console (turn on *Scripting Tools* from the context menu of the MIDI Remote info line). The script console should show `[CubaseSixty] script loaded`. In the MIDI Remote Manager each surface should show as connected. A stale entry named "SSL" from an earlier version can be removed there.
5. **Start the bridge before Cubase** (see [`../bridge/README.md`](../bridge/README.md)). Cubase activates the scripts when it sees the ports. If you restart the bridge, restart Cubase too.

## Testing without the hardware

`tools/test_bridge.py` plays the UF8 bridge (it needs the UF8 ports, so create two ports in loopMIDI with the UF8 names while testing, and do not run the real bridge at the same time): it sends fader, mute, solo, bank and transport messages to `Cubase-Sixty UF8 to Cubase` and prints everything Cubase sends back on `Cubase-Sixty UF8 from Cubase`, including the name and colour SysEx. It needs Python with `mido` and `python-rtmidi` (`python -m pip install mido python-rtmidi`).

```
python cubase\midi-remote\tools\test_bridge.py
```

Open the MixConsole while it runs, ideally with more than 8 tracks so banking is visible.

## Debug logging

Set `DEBUG = true` near the top of the UF8 script to log every fader, solo, mute, name and colour change to the Script Console. It is off by default because it is very chatty while a fader moves.

## Behaviour to know about

- **Implicit mute.** Soloing a track makes Cubase report `MUTE 1` for the other tracks, so the surface cannot tell implicit mute from an explicit one.
- **Empty strips.** A strip with no track sends an empty name and a colour with alpha 0; the bridge shows it blank.
- **Colours.** Cubase gives red, green and blue as 0 to 1; the script scales them to 0 to 127. Cubase only reports a colour when a track is selected or banked in, not the moment the colour is changed (a known Cubase limit), so a changed colour appears on the scribble strip after you select another track.
- **LED feedback.** Cubase reports a button's state as note on / off, and "on" has also been seen as polyphonic aftertouch with value 127. The bridge accepts both.
- **Fader touch** uses the MIDI Remote API 1.1 touch state, guarded so the script still loads on older Cubase versions.
- **Automation Read / Write** act on the selected track, because the UF8 buttons are global.

- **Which UF8 strips can record.** The MIDI Remote API does not say what kind of channel a strip is, and Cubase even reports record-enable as "on" for a group after a press. So the UF8 script keeps a second, hidden bank with only the channel types that can record (groups, FX, VCA, inputs and outputs excluded, up to 128 channels) and collects their names. A strip whose name is not on that list gets a dark, inactive top soft key. Two tracks with the same name, or more than 128 recordable tracks, can confuse it.
- **Cubase re-sends values.** When one bound value changes, Cubase often reports the others again unchanged, and it re-sends all strip names after a bank key. The scripts only act on values that really changed.
- **Start-up bank (UF8).** The script asks the bridge to press bank-right until the selected track shows, and counts a bank as arrived only when every strip's name has changed. Pressing a bank key ends the search.
- **Command bindings (UF1).** The channel encoder and jog use `makeCommandBinding` with the internal category and name: `Project` / `Select Track: Prev` and `Select Track: Next`, `Transport` / `Nudge Cursor Left` and `Nudge Cursor Right`.

## Not yet done

Meters, the UC1, UF1 SEL LED and navigation keys, zoom keys, Page left / right, other automation modes, and "show only record-enabled tracks" on the UF8.
