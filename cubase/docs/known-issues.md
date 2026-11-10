# Known issues

Bugs seen in testing and noted for later. Each entry: what happens, where, and what is known about the cause.

## UF1

- **V-pots not smooth: found and being fixed (10.11.2026, branch wip/uf1-vpot-test).** A MIDI Remote value binding
  on a send level makes Cubase pause ~0.8 s at a time while the level keeps changing (it answered about 1 step in
  20), whatever the scripts do: skipping their rescans, removing the Helper's 512 per-channel bindings and switching
  off every Direct Access listener changed nothing. Writing the level with Direct Access (setParameterProcessValue on
  the send slot) was smooth; even a binding kept only for the display brought the pauses back. A knob left without
  any binding in a page stopped the whole UF1 script at load ("DukValue is uninitialized"). Now: turns on CC 21-24 to
  custom values that write the slot, the level read back through Direct Access, the knobs bound to the send's on
  switch as a placeholder in DAW / SENDS. Checked (10.11.2026, evening): fast turns end on Cubase's value (a jump now and then
  while turning fast). Left: the DAW view's left-display line ("S1 ...") is not drawn while turning (it came from the
  level binding; the bridge should draw it from its own position, as it does the SENDS cells); PLUGIN without an SSL
  plug-in shows no Quick Control names / text (only the bar moves; that code was not changed, so check on cubase
  whether it is older). Then the test switches out and the same for the UF8's send views.
  (Bisect note: a03b82a was smoother than today but already lagged a little; the order of later commits did not
  explain it.)
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
