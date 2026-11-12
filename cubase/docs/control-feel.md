# Knobs, faders and buttons: what makes them feel right

What was found while making the controls smooth and dependable, so it does not have to be found again. **Add to this
file whenever a control's feel is worked on**: what was measured, what helped, what did not. Track stepping and
banking have their own file: [banking.md](banking.md). Cubase's limits are in
[cubase-limitations.md](cubase-limitations.md).

## The rules

1. **Draw a turn yourself, at once.** A step that goes bridge → Cubase → script → Cubase → bridge before it shows
   feels laggy, however fast each hop is. The bridge keeps the control's position, adds the turn, and draws the ring /
   bar / value text immediately.
2. **Ignore Cubase while the hand is on it.** For 0.5 s after a turn the bridge ignores what Cubase reports for that
   control (the reports are late and out of step, and make the display jump back and forth).
3. **Take Cubase's value once it has settled.** 0.6 s after the last step the script reads the real value and sends
   it once (after the bridge's 0.5 s, or it would be thrown away). Display and next turn then start from the truth.
4. **Count quick turns from what you last wrote, not from what Cubase says.** Read back at once, Cubase still gives
   the old value, and steps are lost. Keep the value last written for 1 s and add to that.
5. **One side counts.** If the bridge draws a turn itself, it sends the script the resulting value, not the step:
   with both sides adding steps the two drift apart, and the settled value arrives as a small correction (UF8 V-pot
   send view, 12.11.2026: SysEx 2A carries the level).
6. **Send levels never through a MIDI Remote value binding.** See below.
7. **Measure before changing.** Every fix here came from a log of what really happened per turn; the guesses made
   before measuring were wrong (see "What did not help").

## Send levels (10.–12.11.2026)

- **The cause of the lag.** With a MIDI Remote value binding on a send level, Cubase pauses ~0.8 s at a time while
  the level keeps changing, answers about 1 step in 20, and re-reports the selected track each time. Volume and pan
  bindings do not do this. A binding kept only for the display brings the pauses back.
- **The fix.** Write the level with Direct Access: `setParameterProcessValue` on the channel's `Sends` object → slot →
  the parameter whose title contains "Level". Read it back the same way.
- **Placeholders.** A surface element needs a binding in every sub page, or the whole script stops at load ("DukValue
  is uninitialized", with a line number that points nowhere). In the send views the knobs and faders are bound to
  the send's **on** switch, which does not change while a level turns, and their callbacks are ignored there.
- **Empty send slots.** Cubase keeps eight per track; an empty one has no destination (its title is empty or only
  its number) but still a 0 dB level. Treat it as no send: fader at the bottom, no value.
- **Following the mouse.** Cubase reports a Direct Access change only when an edit ends (a mouse drag showed only at
  the release), and the text read right after a write is not updated yet. In the send views the script reads the
  levels each idle tick (~9 a second) and sends what changed.
- **Where it is used.** UF1 V-pots in DAW / SENDS (turns on CC 21–24 to custom values); UF8 fader send view and
  V-pot send view (the strip-mode messages 25–29); the same path as SSL Strip Mode.

## The value text while turning

- The bridge shows the dB text for its own position from a **level curve** it learns from Cubase: pairs of (14-bit
  position, text), a straight line between neighbours up to a quarter of the range apart. Shared by the UF1 and the
  UF8 (`uf1LevelText`, `uf1LearnLevel`).
- It learns only from **settled** values: on the UF1 a position that follows its text within 60 ms (so send the text
  first, then the position); on the UF8 the script sends the exact position only with Cubase's own value (0E with
  msb / lsb), never with a turn in flight.
- Where the curve has a gap, Cubase's own text shows (never "...").
- The UF1's temporary value line goes back to pan 2 s after the last step (3 s felt too long), and a read-back must
  not restart that timer or move the bar.
- The UF1's value line is laid out as on the UF8: label left, value right-aligned (Rea-Sixty's `uf1ValueLine`).

## Cubase repeats itself

- While a value of the selected track changes, Cubase reports the track's title again about every 0.8 s. Taken as a
  new track, each report rescanned plug-ins and put "Pan C" back on the value line mid-turn. Reports of the same
  track (same object ID, same name) are skipped (UF1, UC1).
- A callback read back from a surface value is Cubase's wrapper, not your function: keep your own reference.
- Cubase delivers the first values while a script is still loading: define what the callbacks use before the
  bindings.

## Faders

- **The hand wins.** A touched fader is never driven; its position is echoed so the motor does not jump back on
  release. Position reports count only while touched.
- **Who drives the motor.** Normally Cubase's pitch bend. Where the script owns the fader (SSL Strip Mode, the fader
  send view) the script's value does (SysEx 27), the hand goes to the script (25), and in a fader send view Cubase's
  pitch bend is ignored altogether (it is the placeholder binding's).
- **Closing a project / stopping the bridge leaves the faders where they are.** All eight motors running to the
  bottom at once was loud; the dark displays show that nothing is connected.
- "Touch selects the track" is a setting.

## Buttons

- **Tap and hold on one key** (NAV: tap = encoder on markers, held 1 s = Nav Mode; FLIP: tap = flip, held 0.4 s =
  the V-pot send view): a counter per press cancels the hold timer, and a **separate flag** says whether this press
  was held. Using the counter for both made every tap after a hold act as a hold.
- **A release without its press is not a tap.** The UF8 sends a NAV release when it connects.
- **0.4 s is short for a hold** that changes a mode: NAV went to 1 s.
- **Foot switches are ignored for 4 s after each connect** (a reconnect started playback).
- **Requests are not replayed on reconnect** (54, 59, 7C, temporary lines): only state is.
- **Notes back to back are merged or lost by Cubase**: 40 ms apart (bank steps 60–100 ms; see banking.md).
- Messages shorter than seven bytes were dropped by the bridge's SysEx check: the UF1 script's six-byte 54 and 0A
  are let through explicitly.

## Encoders and knobs

- The UC1's knob step is a setting (SysEx 4E to the script).
- The CHANNEL encoders step tracks by selecting the next channel directly (the Helper script), not by counting
  Cubase's next / previous track: see banking.md.
- **"In 'All MIDI Inputs'" must be off for every Cubase-Sixty port.** Left on, track steps were reported 600–800 ms
  late instead of 20–300 ms (and everything the bridge sends reaches every MIDI track).

## How to measure

- `dev-restart.ps1 -Trace`: the bridge logs each turn once the control is still for 0.8 s ("Turn test: ..."):
  detents and events from the device, the longest pause, what was sent and by which path, what was dropped and why,
  and for the UF1 V-pots how many answers Cubase gave and how late.
- For "is it Cubase or us": count the script's idle calls per 10 s (normally ~92, never more than ~0.12 s apart;
  20–40 with 0.8 s gaps means Cubase is pausing) and time the script's own work.
- A/B by a temporary Settings switch (one build, flip it while testing) beats two builds.

## What did not help (10.11.2026)

All tried against the send-level lag, with no measurable change, before the binding itself was found:

- skipping the scripts' rescans on repeated track reports (kept anyway: it fixed "Pan C" mid-turn);
- removing the Helper script's 512 per-channel value bindings;
- switching off every Direct Access parameter / object listener;
- EQ graph polling off (it does nothing on a track with an SSL strip);
- disconnecting the Mackie Control device;
- halving old commits: a03b82a was smoother but not smooth, and the order of later commits explained nothing.
