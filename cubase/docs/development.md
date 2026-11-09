# Development: quicker test rounds

A full `dev-restart.ps1 -Build` closes Cubase, rebuilds, reinstalls the scripts and starts everything again. Most
changes need much less.

## What to run for what changed

| Changed | Run | Then |
|---|---|---|
| Only a MIDI Remote script | `.\dev-restart.ps1 -ScriptsOnly` | In Cubase: MIDI Remote (lower zone) > Reload Scripts. Cubase and the bridge keep running. |
| Only the bridge | `.\dev-restart.ps1 -BridgeOnly -Build` | Nothing: Cubase keeps running (needs the loopMIDI setup below). |
| Both | both of the above | |
| First time, or the ports changed | `.\dev-restart.ps1 -Build` | The full round. |

`-Trace` works with all of them (`cubase\logs\bridge.log`); the scripts' lines are in `cubase\logs\scripts.log`.

## The loopMIDI setup (once)

Cubase sees MIDI ports only when it starts. The bridge normally creates its ports itself, so they go away when the
bridge stops and Cubase has to be restarted with it. Ports made in the loopMIDI app stay while the bridge restarts:

1. In loopMIDI, add these ten ports (exact names):
   `Cubase-Sixty UF8 to Cubase`, `Cubase-Sixty UF8 from Cubase`, `Cubase-Sixty Helper to Cubase`,
   `Cubase-Sixty Helper from Cubase`, `Cubase-Sixty UF1 to Cubase`, `Cubase-Sixty UF1 from Cubase`,
   `Cubase-Sixty MCU to Cubase`, `Cubase-Sixty MCU from Cubase`, `Cubase-Sixty UC1 to Cubase`,
   `Cubase-Sixty UC1 from Cubase`. Let loopMIDI start with Windows.
2. Create an empty file `cubase\tools\use-loopmidi.txt` (not committed). From then on `dev-restart.ps1` always starts
   the bridge with `--loopmidi`, so Cubase sees the same ports either way.
3. Run the full `.\dev-restart.ps1 -Build` once, so Cubase starts with the loopMIDI ports.

After a bridge-only restart the surfaces show what Cubase sends next; the UF8 asks Cubase for everything again by a
bank step right and left (as with loopMIDI ports at any start).

To go back: delete `use-loopmidi.txt` and the ports in loopMIDI, then a full restart.

## Logs without tracing

Some things log one line per action in `bridge.log` even without `--trace`, so a test round needs no restart to
see them: the UF1's BANK without a UF8 (what was sent, how long until the strips moved, which strip was selected).
