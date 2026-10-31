# MIDI Remote script

`CubaseSixty_SSL.js` is the Cubase side of Cubase-Sixty. It speaks the MIDI defined in [`../docs/bridge-midi-protocol.md`](../docs/bridge-midi-protocol.md) and maps it to Cubase's mixer and transport.

**Draft v0** covers the UF8 only: eight faders with touch, pan on the V-Pots, SEL / SOLO / CUT, bank left and right, and play / stop / record.

## Installing for development

Cubase loads local scripts from a vendor folder and a device folder:

```
Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty\SSL\
```

Link that `SSL` folder to this one with a directory junction (see the repo README), then restart Cubase or use **Reload Scripts** in the MIDI Remote Manager.

## Testing before the bridge exists

The script detects ports named `Cubase-Sixty UF8`. Until the bridge creates them, you can make a pair by hand with a virtual MIDI tool such as loopMIDI, then send test notes and pitch bend from any MIDI utility. The script log in Cubase's MIDI Remote Manager shows errors.

## Notes

- The API calls follow Steinberg's MIDI Remote API v1. Check them against the API reference in your Cubase installation if Cubase reports an error loading the script.
- Scribble strips, track colours and meters are not handled yet; see the open items in the protocol doc.
