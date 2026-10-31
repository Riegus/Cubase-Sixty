# MIDI Remote script

`CubaseSixty_SSL.js` is the Cubase side of Cubase-Sixty. It speaks the MIDI defined in [`../docs/bridge-midi-protocol.md`](../docs/bridge-midi-protocol.md) and maps it to Cubase's mixer and transport.

**Draft v0** covers the UF8 only: eight faders, pan on the V-Pots, SEL / SOLO / CUT, bank left and right, play / stop / record, and track names and colours sent to the bridge as SysEx for the scribble strips. It has been tested in Cubase 15 Pro on Windows against hand-sent MIDI (see below). The bridge that talks to the real UF8 does not exist yet.

## Setup

1. **Create two virtual MIDI ports** with a tool such as loopMIDI, with exactly these names, before you start Cubase (Cubase reads its port list at start-up):
   - `Cubase-Sixty UF8 to Cubase` (the bridge writes, Cubase reads)
   - `Cubase-Sixty UF8 from Cubase` (Cubase writes, the bridge reads)

   Use two ports. A single loopMIDI port returns everything written to it, so Cubase would hear its own output and the faders would fight themselves.
2. **Keep the ports out of All MIDI Inputs.** In *Studio > Studio Setup > MIDI > MIDI Port Setup*, untick **In 'All MIDI Inputs'** for both ports. Otherwise a selected instrument or MIDI track receives the controller messages as played notes, and can end up with stuck notes.
3. **Install the script.** Cubase loads local scripts from:
   ```
   <Documents>\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty\SSL\CubaseSixty_SSL.js
   ```
   Your Documents folder may be redirected (for example to `OneDrive\Dokumenter`); open it in Explorer and look for the `Steinberg\Cubase` folder. Copy the `.js` file there, and copy it again after each edit. A directory junction to this folder is more convenient, but it has not been tried with a OneDrive-redirected Documents folder.
4. **Activate it.** Restart Cubase, or press **Reload Scripts** in the MIDI Remote Script Console. After you create the ports, the script only becomes active once scripts have been reloaded. To open the console, turn on *Scripting Tools* from the context menu of the MIDI Remote info line.
5. **Check.** The console should show `[CubaseSixty] script loaded`, and the SSL row in the controller table should list the two port names. Errors show under *Error Messages*.

## Testing before the bridge exists

`tools/test_bridge.py` plays the bridge: it sends fader, mute, solo, bank and transport messages to `Cubase-Sixty UF8 to Cubase` and prints everything Cubase sends back on `Cubase-Sixty UF8 from Cubase`, including the name and colour SysEx. It needs Python with `mido` and `python-rtmidi` (`python -m pip install mido python-rtmidi`).

```
python tools/test_bridge.py
```

Open the MixConsole while it runs, ideally with more than 8 tracks so banking is visible.

## Debug logging

Set `DEBUG = true` near the top of the script to log every fader, solo, mute, name and colour change to the Script Console. It is off by default because it is very chatty while a fader moves.

## Behaviour to know about

- **Implicit mute.** Soloing a track makes Cubase report `MUTE 1` for the other tracks, so the surface sees them as muted. It cannot tell this apart from an explicit mute.
- **Empty strips.** A strip with no track sends an empty name and a colour with alpha 0. The bridge should show it blank.
- **Colours.** Cubase gives red, green and blue as fractions from 0 to 1. The script scales them to 0 to 127 for the SysEx.
- **LED feedback.** Cubase reports a button's state as note on or off, but "on" has been seen as polyphonic aftertouch (value 127) as well. A bridge should accept both.
- **Names and colours** come from the mixer bank channel (`mOnTitleChange`, `mOnColorChange`) and are sent when the script activates, when a track's name or colour changes, and when the bank changes. The same callbacks on the fader's surface value only fired on a rename and never for colours.
- **Fader touch** uses the MIDI Remote API 1.1 touch state (`mTouchState`), guarded so the script still loads on older Cubase versions.

## Not yet handled

Meters, UC1 and UF1 mappings, V-Pot and select behaviour beyond the basic bindings, echo suppression in the bridge, and a version or ping message. See the open items in the protocol doc.
