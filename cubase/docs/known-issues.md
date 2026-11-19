# Known issues

Bugs seen in testing and noted for later. Each entry: what happens, where, and what is known about the cause.

## Bridge

- **Fixed (19.11.2026): installing with Cubase running left the surfaces dead.** The installer quits and restarts
  the tray app, which replaces the MIDI ports; Cubase half reconnected, and switched its scripts off 4 s after a
  project opened (the script console: "inactive (no project)" with the project open; nothing more reached the bridge).
  A Cubase restart brought them back. The installer and the uninstaller now ask for Cubase to be closed first.
- **To be confirmed (19.11.2026, wip-port-retry): "The bridge stopped and was started again" right after logging in.**
  The tray app makes the MIDI ports at login and starts the bridge, which found a port but could not open it yet
  ("MidiOutWinMM::openPort: error creating Windows MM MIDI output port") and ended; the tray app started it again
  5 s later and it ran. Now the bridge tries a failed open again for up to 10 s before giving up.
- **Fixed (14.11.2026, wip/bridge-reconnect): the tray app holds the ports; a new bridge says hello, the scripts
  send their state again and the tray app gives it Cubase's own values. Back within a second, tested.**
  **Cubase does not reconnect reliably after a bridge restart** (14.11.2026). Bridge down about 6 s with Cubase and
  a project open: Cubase sent nothing for over 2 minutes; Reload Scripts brought back only Cubase → surfaces (the
  displays updated, faders, keys and the UF1 reached nothing); a Cubase restart fixed it. Matters for the release:
  the tray app restarts a bridge that stops. To measure first: how long Cubase takes to see the new ports, in each
  direction, with and without SSL plug-ins in the project; then whether the bridge can make Cubase look again, or
  at least tell the user (the surfaces) what to do. See cubase-limitations.md (bridge outage).

## All surfaces

- **Fixed (16.11.2026): wrong track numbers in a project with disabled tracks.** A
  disabled track has no mixer channel but keeps its track index; the MixConsole numbers by that index and the SSL
  plug-ins announce it ("Vio S 2": 22). The UF1 and UF8 counted mixer channels (13), showed that, and asked SSL's
  connection for the plug-in on track 13: the UF1's EQ graph was another track's. Now they show and use the track index
  (the UF1 script sends it; see bridge-midi-protocol.md, Track numbers), as the UC1 always did.
  The script's channel tree (the VST Mixer's children) ended early (at index 46; the last group and the FX channels
  missing): the mixer reports as many children as there are channels but hands them out by track index, so with
  disabled tracks the holes repeat other channels and the last channels are past the count. The scripts now ask on
  until the list is empty (`mixerChildIds`): the track numbers, track stepping, the UF1's METER search and the UC1's
  bus comp search. (Should a list still end early, the bridge counts on from its end.)

## UF8

- **Fixed (16.11.2026): the track numbers and plug-in names were blank after the same project was closed and opened
  again.** The bridge forgets the numbers when a project closes; the Helper sent them again only when a channel's name
  changed, which none does when the same project comes back. It now sends them at every activation (and 3 s later).
- **Not ours (15.11.2026): another controller's MIDI Remote scripts moved the selection when a project opened.** The
  selected track jumped twice a few seconds after the project opened (Vocals Main → the track before it → Audio FX, the
  last audio track), with every surface following. Traced to the zone moves at a project's start, before Cubase has a
  selected mixer channel; with the four CubaseSixty scripts off it still happened, with Geir's Arturia keyboard scripts
  off it did not. Left to the user (the guide's "When something is wrong" says so); a put-back in the Helper worked but
  was dropped as undoing someone else's change.
- **Fixed (15.11.2026): the view at start ALL was not applied.** Cubase opens the UF8 on the page it was last on (INSTR),
  and the bridge sent the start view only when it was not ALL. It is now always sent.

- **Fixed (15.11.2026, on `cubase`): the faders are placed by dB on the printed scale (see control-feel.md, Faders).**
  **The faders did not match the UF8's printed dB scale** (seen 15.11.2026, in every view; there from the start). The
  bridge drives the motors with Cubase's own fader position (pitch bend), which follows Cubase's fader curve: -14 dB
  in Cubase sits near the UF8's "30" mark (Geir: "it looks like the values should be doubled"; -3.85 dB sat near "5",
  -27 dB near "45"). Rea-Sixty places them by dB (REAPER's slider law plus a calibration table
  measured against the UF8's scale, `pbToLinearVolume` / `linearVolumeToPb` in extension/src/main.cpp). To do: learn
  Cubase's curve (14-bit position and its dB text, as the send level curve), place the motor by dB with Rea-Sixty's
  mapping, and turn the hand's position back the same way.

## UF1

- **V-pots not smooth: fixed on wip/uf1-vpot-test (10.11.2026), to be merged after the test checklist.** Cause: a
  MIDI Remote value binding on a send level makes Cubase pause ~0.8 s at a time while the level keeps changing (it
  answered about 1 step in 20); the scripts' own work was not it. The UF1's send V-pots now write and read the send
  slot's level through Direct Access (see cubase-limitations.md). The UF8's send views (faders and V-pots) the same way
  (12.11.2026), the bridge drawing the V-pot's ring and text itself. Left, taken as Cubase limits: on fast turns of
  a send the MixConsole trails (Cubase stalls while a send level changes quickly; see control-feel.md), and the dB
  text can correct itself by a digit when a turn ends (the bridge's estimate; it gets closer with use,
  cubase\level-curve.txt).
- **V-pots and some encoders no longer feel smooth** (noticed 9.11.2026). They were smooth on 6.11.2026 (a03b82a,
  SENDS view); the send lag noted since as a Cubase issue is probably the same regression. To find by halving the
  commits since a03b82a. Suspect: the UF1 script reading Cubase's channel EQ (40 parameters) seven times a second on
  every track and view since cf8f7a2 (7.11.2026).
- **Send levels lag** (UF8 send views, UF1 send V-pots). Moving a send with a fader or V-pot follows a little behind;
  volume and pan do not. The same on the UF1, so it is how Cubase handles send levels from MIDI Remote scripts, not
  the views. To look into: whether Direct Access on the send parameters is quicker.
- **Track change may lag a little** (since the CHANNEL encoder push / SHIFT + turn, 7.11.2026). Stepping the track
  with the UF1's CHANNEL encoder felt slower; sixteen insert viewers were replaced by one, which helped, but it is not
  sure it is as quick as before. To check against `cubase` without the change; suspects: the PLUGIN scan's extra slot
  reads, the insert viewer. Still slow (9.11.2026). Measured since: Cubase reports a new selection 0.7-0.9 s after
  the step (bridge.log), so the UF1's name and values follow that late. May be the same regression as the V-pots
  (above): to look at together. See [Banking and track stepping](banking.md).
  10.11.2026: much better after unticking **In 'All MIDI Inputs'** for the Cubase-Sixty ports (it had been on for
  all of them): new track reported 20-300 ms after a step, mostly, instead of 600-800 ms. A Mackie Control test
  showed Cubase selects within 1-20 ms; what lags is Cubase being busy after each selection change (the scripts'
  per-track work among it). Watch whether it stays quick.
