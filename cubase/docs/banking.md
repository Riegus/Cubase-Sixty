# Banking and track stepping: what we know

Everything found so far about moving the strips (banking) and the selection (track stepping) in Cubase, so it does
not have to be found again. **Add to this file whenever banking or stepping is worked on**: what was tried, what was
measured, what failed and why. Newest findings go in their section with the date.

## How Cubase behaves

### Timing (measured in bridge traces)

- **Strips move ~0.3 s after a bank note.** The new names arrive about 300 ms after the bridge sends the bank key
  (9.11.2026: bank note 25.752, new strip names 26.059).
- **The selection is reported 0.7–0.9 s late.** After a next / previous track action (or a SEL), the selected
  channel's new title arrives up to ~0.9 s later. Anything that "steps, then looks" is slow.
- **Cubase reports the tracks a step passes, too**, late, folders included. A burst of N next-track triggers can
  produce reports for tracks on the way before the last one. Do not react to each report: wait until they stop
  (0.45 s quiet worked, 9.11.2026), or do not react at all.
- **Cubase ignores bank keys for its first seconds** after loading a project (a burst 3 s after start still lost most
  steps). Script-requested bank steps wait until 3 s (single) / 5 s (jumps) after Cubase first spoke.

### MIDI to scripts

- **Notes sent back to back are merged or lost.** Bank steps 60–100 ms apart (at 60 ms Cubase still merged some in
  jumps: 100 ms there); other notes 40 ms apart.
- **Notes are taken in order, but the effect is not immediate.** A SEL sent 40 ms after a bank step reached Cubase
  before the strips had moved: it selected on the old bank (9.11.2026). Wait for the evidence (the script's bank
  report, SysEx 12) before acting on the new bank.

### What the scripts can see

- **The MixConsole's channel list** (Direct Access on `mMixConsole`: the VST Mixer's children except inputs / outputs,
  sorted by `getMixerChannelIndex`) **is in track order** (the track numbers), even when the MixConsole shows
  channels grouped differently (FX / group channels in another place). Confirmed by log, 9.11.2026.
- **Instrument tracks are type "Synth"**; MIDI sub-channels must not be walked into: take the VST Mixer's direct
  children only.
- **Folder tracks have no channel**, and neither do the ruler / marker / Sections / Comments tracks and the
  Input/Output folder. They are not in the channel list, but **the next / previous track action stops on them**.
  On one, `getBaseObjectID` of the selected channel gives **-1**. (A track with a channel that is not in the list,
  e.g. an FX channel in some states, gives an id >= 0: do not treat it as a folder; track 56 was skipped that way.)
- **Hidden tracks** (MixConsole visibility, `isMixerChannelVisible`) are included by next / previous track and by
  Select Track; skipping them needs the list.
- **No project tree**: scripts cannot see where folders are before stepping onto them.
- **Mixer bank zones** (`makeMixerBankZone`) put only channels on strips: folders never, and they move by whole banks
  natively. Selecting through a strip (`mValue.mSelected` bound to SEL) selects that channel directly.

### Cost inside a script

- **Large name lists in the UF8 script made each bank press ~0.3 s.** Cubase re-checks every channel of a script when
  its bank moves; with the all-channel / recordable lists moved to the Helper script a bank press takes ~0.07 s.
- **Per-channel value bindings in hidden banks** (256 + 256 + 128) made Cubase pause up to 0.7 s while a value kept
  changing: hidden banks collect names only.

## What works (keep)

- **UF8 banking**: the bank keys go straight to the UF8 script's bank zone. Near the end the bridge sends single-track
  shifts so the last track lands on strip 8 (`userBankRight`, from the script's SysEx 12 total / offset).
- **UF8 follow selection**: on the idle timer; if the selected track is not on a strip, a jump (reset + n bank-rights,
  100 ms apart), then one burst of the remaining steps. The bridge freezes the UF8 during multi-step bursts and shows
  only the end state (SysEx 13 holds the freeze at start-up; released after 30 s by itself).
- **UF1 BANK with a UF8**: presses the UF8's bank key (same path as above).
- **Single track steps past hidden tracks** (`track-stepping = mixconsole`, UF1 script, SysEx 5C): count from the
  channel list to the next visible track and trigger next / previous that many times at once. A press within 1.5 s
  of the last (or before the last has arrived) counts from the last target, not from the reported selection.
- **Folders on single steps**: a step that lands on -1 steps once more (back instead, if there is no visible track
  beyond the target), checked once the reports have settled; the extra step is remembered per (track, direction),
  so the next step there goes past at once. Forgotten when the channel list changes.

## UF1 BANK without a UF8 (9.11.2026)

The selection has to move by a bank. Tried, in order:

1. **Eight next-track notes, 40 ms apart** (the original fallback): visibly scrolls through each track, includes
   hidden tracks, and folders take a step each.
2. **The UF1 script counts eight visible tracks and triggers that many at once** (SysEx 5C with a count): fast, but
   folders are invisible to the count. Crossing one lands one short; ending on one leaves nothing selected.
3. **Corrections after landing** (make up missing steps, step back when past, step past folders): went wrong.
   Cubase reports tracks on the way, late; quick presses overlap; the corrections fed on stale reports and the
   selection ran off onto the Input/Output folder. **Do not correct from individual selection reports.**
4. **A press counting from the reported selection 0.6 s after the last**: the report had not arrived yet (0.7–0.9 s),
   so it counted from the old track and overshot. Count from the last target for 1.5 s / until arrival.
5. **Learning per (start track, direction, count)**: helped repeated presses from the same track only.
6. **Bank the UF8 script's strips (loaded even without a UF8), then SEL the same strip**: folders and hidden tracks
   are handled by Cubase's bank zone.
   - SEL 40 ms after the bank note: selected on the old bank (strips move ~0.3 s later); presses looked lost, and the
     next press took strip 1 because the old selection had left the strips.
   - Current: each press waits for the script's new bank position (SysEx 12, up to 0.6 s), then SEL; presses queue
     (one at a time); the strip of the last press is kept for 3 s (Cubase's SEL LEDs are in flux meanwhile). At the
     first / last bank nothing moves and nothing is selected. Still slow, missed presses, wrong tracks, lost
     selection. Found (reading the code): **the UF8 script's follow selection fought it.** Follow looks 0.3 s after
     each selection report; the report of the SEL comes 0.7-0.9 s late, by then for the previous bank, so follow saw
     the selected track off the strips and banked back.
   - Now: the bridge tells the UF8 script to keep follow quiet for 2.5 s per press (SysEx 5D), and takes the
     strips as moved when either the script's position (SysEx 12) or a new strip-1 name arrives. To be confirmed.

   - bridge.log (9.11.2026, ten presses, four did not bank): the "moved" signal was wrong. **While Cubase changes
     the strips, the UF8 script reports bank position -1** (the names match nowhere halfway), and **strip 1's name
     arrives first**: SEL on either (60-80 ms after the press) landed on the old bank. A real new position came
     after 200-530 ms. Now SEL waits for a known (>= 0), different position, up to ~1 s.
   - Confirmed (9.11.2026, 18 presses): every press landed on the next bank, strip 5 kept. The strips moved
     150-840 ms after the bank key; Cubase reported the new track ~0.7-1 s after that. A left press on the first
     bank does nothing (nothing to move).

**Lesson: wait for a known new position (>= 0), not for "something changed".**

**Lesson: anything that moves the UF8 script's strips or the selection must keep follow selection out of it, or
follow pulls the strips back to where the late selection report says.**

## Rules of thumb

- Prefer Cubase's own bank zones over counting tracks: they know about folders and visibility.
- Never act on a new bank before the script has reported it (SysEx 12 / new strip names).
- Never chain decisions on selection reports during a burst; they are late and include the tracks passed.
- Space notes (40 ms; bank steps 60–100 ms).
- Add a trace (`dev-restart.ps1 -Trace`, `cubase\logs\bridge.log`) before guessing: every finding above came from one.
