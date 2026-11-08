# Known issues

Bugs seen in testing and noted for later. Each entry: what happens, where, and what is known about the cause.

## Projects

- **After a project is closed and another opened, the UF8 does not bank to the selected track** (8.11.2026). At
  Cubase's start it does. Part of step 1 of the [release plan](release-plan.md) (closing and opening projects): the
  scripts' drivers are deactivated on close and activated again on open, but the UF8's start-up follow does not run
  again.
- **Closing a project leaves the surfaces showing the last state** (names, faders, meters, LEDs) while Cubase shows
  the Hub. Being fixed (release plan step 1).

## UF1

- **Send levels lag** (UF8 send views, UF1 send V-pots). Moving a send with a fader or V-pot follows a little behind;
  volume and pan do not. The same on the UF1, so it is how Cubase handles send levels from MIDI Remote scripts, not
  the views. To look into: whether Direct Access on the send parameters is quicker.
- **Track change may lag a little** (since the CHANNEL encoder push / SHIFT + turn, 7.11.2026). Stepping the track
  with the UF1's CHANNEL encoder felt slower; sixteen insert viewers were replaced by one, which helped, but it is not
  sure it is as quick as before. To check against `cubase` without the change; suspects: the PLUGIN scan's extra slot
  reads, the insert viewer.
