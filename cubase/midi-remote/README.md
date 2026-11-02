# MIDI Remote scripts

The Cubase side of Cubase-Sixty: one MIDI Remote script per surface. They speak the MIDI defined in [`../docs/bridge-midi-protocol.md`](../docs/bridge-midi-protocol.md) and map it to Cubase's mixer and transport.

| Folder | Script | Surface | What it does |
|--------|--------|---------|--------------|
| `UF8/` | `CubaseSixty_UF8.js` | SSL UF8 | Eight faders with touch (Touch automation works), pan on the V-pots (value and bar on each strip's display), SEL / SOLO / CUT with LEDs, top soft keys toggle record-enable (dark on strips that cannot record), V-pot push opens the channel editor, bank left / right (bank-right stops at the bank where the last track is on strip 8), Page left / right (one channel), follows the track selection (not yet at start-up), automation Read / Write on the selected track, automation mode keys (Off = Cross-Over, Trim, Latch, Touch), zoom pad (zoom, vertical zoom, centre = open / close editor), FLIP, foot switches (play / record), track names and colours on the scribble strips, fader level in dB and stereo meters per strip. |
| `UF1/` | `CubaseSixty_UF1.js` | SSL UF1 | One strip that controls the **selected track**: fader with touch, SOLO / CUT / SEL, pan on the knob above the fader, V-pots 1–4 = sends 1–4, or 5–8 after the 5–8 key (turn = level, push = on / off), MASTER = the strip on the main output, SHIFT + transport soft keys / Cycle / Click = automation Read, Write, Cross-Over, Trim, Latch, Touch, channel encoder and ARROW = previous / next track, BANK = banks the UF8, jog wheel = nudge cursor (SHIFT: markers), SHIFT + ARROW = bar steps, shuttle (SHIFT + SCRUB), BYPASS = all inserts of the track, FLIP, 360° = swap time formats, NAV cross = zoom (centre = open / close editor), transport keys (rwd, ffw, stop, play, record, cycle, click) with LEDs. The display shows the track name, level, colour, and a value line with pan, or a send for 3 seconds after it is turned, a stereo meter, the transport position and the selected track's number (as in the MixConsole). Scrubbing goes through a Mackie Control device (setup step 6). |

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
6. **Add the Mackie Control device (UF1 scrubbing).** With the bridge running: *Studio > Studio Setup*, click **+** (top left, "Add Device"), choose **Mackie Control**. Select it under *Remote Devices* and set **MIDI Input** to `Cubase-Sixty MCU to Cubase` and **MIDI Output** to `Cubase-Sixty MCU from Cubase`, then **Apply**. Under *MIDI Port Setup*, untick **In 'All MIDI Inputs'** for the two MCU ports too. The bridge only uses this device for SCRUB and the jog while scrubbing.

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
- **Following the selection (UF8).** Selecting a track that is not on the eight strips (for example in the MixConsole) banks the UF8 to it. This is mostly Cubase's own behaviour: it moves its MIDI Remote banks to the selected channel, in steps of eight from wherever the bank is. Two cases it does not handle: at start-up the bank does not move, and when the bank is off the eight-grid (after banking near the end of the list, or Page left / right) the first tracks cannot be reached. With several tracks selected, the script follows Cubase's current track (the one the UF1 shows). The script also has a correction for the off-grid case (hidden bank of all channels for positions, then reset + jump through the bridge, SysEx 10 / 11), but it waits for note 80 from the bridge, which never reaches its code (below), so in practice it does not run.
- **Notes from the bridge cannot run script code.** A note bound to an action triggers that action in Cubase, but in testing the button's `mOnProcessValueChange` never ran for notes bound to a dummy bank's reset (notes 80 / 81), nor for one bound to the hidden bank's reset. So the bridge cannot "call" the script. Messages the other way (script → bridge) work, except while a project loads: Cubase then drops some of what the scripts send and receive. The API has `deviceDriver.mOnIdle`, a regular callback that could give the scripts a timer of their own; not tried yet.
- **Bank-right at the end (UF8).** The script reports the bank's size and position to the bridge (SysEx 12). Near the end, the bridge turns a bank-right press into single-channel shifts so the last track lands on strip 8; on the last full bank it does nothing. Until the size is known, bank-right is a normal step. (Untested.)
- **Track number (UF1).** Cubase does not tell scripts a track's number, so the UF1 script keeps a hidden bank of all mixer channels (same types as the UF8 bank), sees which one is selected and sends its position (SysEx 0E). The bank ignores visibility (`setFollowVisibility(false)`), so hidden tracks are counted and the number matches the MixConsole. (The visibility setting is untested.)
- **Sub-pages (UF1).** The UF1 page has two sub-page areas: *Strip* (selected track / main output, switched by MASTER) and *Sends* (sends 1–4 / 5–8, switched by 5–8). The bridge activates them with notes 44 / 45 and 53 / 54 and keeps the key state and LEDs itself. The main output is the first channel of a one-channel bank that excludes every other channel type.
- **Meters.** Cubase gives the meter as linear amplitude, so the scripts convert it to dB (`meterToMidi`, floor `METER_FLOOR_DB = -55`) before sending. Each channel's left and right meter is bound with `mValue.getVUChannelMeterByChannelIndex(0 / 1)` to a custom value variable and sent as CC (UF8: 80 + strip left, 88 + strip right; UF1: 80 / 81). If that call is missing, the scripts fall back to `mVUMeter` (one value) and log it.
- **API reference.** Cubase installs the exact API it offers as `midiremote_api_v1.d.ts` under `<Documents>\Steinberg\Cubase\MIDI Remote\Driver Scripts\.api\v1\`. Check names there before guessing (for example `mTransportLocator.mOnChange(activeDevice, activeMapping, time, format)`, `makeInsertEffectViewer(...).excludeEmptySlots().accessSlotAtIndex(i)`).
- **Command bindings.** `makeCommandBinding` needs the internal category and command name. They are in the `.xml` files under `Cubase 15\Presets\KeyCommands`; this prints the category of a command:
  ```powershell
  Get-ChildItem "C:\Program Files\Steinberg\Cubase 15\Presets\KeyCommands\*.xml" | ForEach-Object {
    [xml]$x = Get-Content $_.FullName
    $x.SelectNodes("//item[list[@name='Commands']]") | ForEach-Object {
      $cat = $_.SelectSingleNode("string[@name='Name']").value
      $_.SelectNodes("list[@name='Commands']/item/string[@name='Name']") | Where-Object { $_.value -like '*Bypass: Inserts*' } |
        ForEach-Object { "Category: $cat  ->  $($_.value)" }
    }
  } | Sort-Object -Unique
  ``` In use:
  - `Project` / `Select Track: Prev`, `Select Track: Next` (UF1 channel encoder, ARROW, BANK without a UF8)
  - `Transport` / `Nudge Cursor Left`, `Nudge Cursor Right` (UF1 jog)
  - `Zoom` / `Zoom In`, `Zoom Out`, `Zoom In Vertically`, `Zoom Out Vertically` (UF1 NAV cross, UF8 zoom pad)
  - `Editors` / `Open/Close Editor` (NAV / zoom pad centre; acts on the selected part or event, like a double-click)
  - `Mixer` / `Bypass: Inserts` — the name is right, but Cubase does not run it from a script; not every command can be bound (BYPASS uses insert viewers instead)
  - `Transport` / `Exchange Time Formats` (UF1 360°), `Locate Previous Marker`, `Locate Next Marker` (SHIFT + jog), `Step Back Bar`, `Step Bar` (SHIFT + ARROW), `Shuttle Play 1/8x` … `8x` and `Shuttle Play Reverse 1/8x` … `8x` (shuttle)
  - `Transport` / `Jog Left`, `Jog Right` — bound but no longer used (choppy audio; scrubbing goes through Mackie Control)
  - `Automation` / `Automation Mode - Cross-Over`, `Automation Mode - Trim` (a toggle), `Automation Mode - Auto-Latch`, `Automation Mode - Touch` (UF8 automation keys)

## Not yet done

The UC1, the UF1 MODE key, the UF8's jump to the selected track at start-up, and "show only record-enabled tracks" on the UF8.
