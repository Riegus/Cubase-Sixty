# Known issues

Bugs seen in testing and noted for later. Each entry: what happens, where, and what is known about the cause.

## Bridge

- **Cubase does not reconnect reliably after a bridge restart** (14.11.2026). Bridge down about 6 s with Cubase and
  a project open: Cubase sent nothing for over 2 minutes; Reload Scripts brought back only Cubase → surfaces (the
  displays updated, faders, keys and the UF1 reached nothing); a Cubase restart fixed it. Matters for the release:
  the tray app restarts a bridge that stops. To measure first: how long Cubase takes to see the new ports, in each
  direction, with and without SSL plug-ins in the project; then whether the bridge can make Cubase look again, or
  at least tell the user (the surfaces) what to do. See cubase-limitations.md (bridge outage).

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
