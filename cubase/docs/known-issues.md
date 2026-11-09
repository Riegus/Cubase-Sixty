# Known issues

Bugs seen in testing and noted for later. Each entry: what happens, where, and what is known about the cause.

## UF1

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
