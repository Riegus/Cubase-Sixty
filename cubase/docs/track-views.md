# UF8 track views: plan

Goal: pick on the UF8 which tracks the eight strips bank over: ALL, AUDIO, INSTR, MIDI, GROUPS, FX, VCA and ARMED.

## Why it is built this way

- **Key commands don't work.** Cubase's Mixer > Hide commands need the MixConsole open and selected. The UF8 bank also does not follow the MixConsole's channel-type filter (tested 2026-11-15).
- **The script can filter instead.** A MIDI Remote bank zone can be built with channel-type filters (`includeAudioChannels()`, `includeVCAChannels()` …), with no MixConsole involved. A zone's filter is fixed once the script has loaded, so every view needs its own bank zone. Its bindings must live on a mapping page of its own, because one binding can't belong to sub-pages from two areas: the fader modes already use one area, the V-pot modes another.
- **So: one mapping page per view, built by one function.** The bridge switches views by sending a note that the script binds to that page's activate action.
- **ARMED is different.** Bank zones can't filter on record-arm, so ARMED gets its own investigation (step 6).

## Steps

Each step is its own branch and test round. A step is merged only when its test passes, so a later step never builds on something untested.

### 1. Spike: answer the unknowns (small test build, thrown away)

Add a second page to the UF8 script: an AUDIO bank zone with only faders, names and SEL bound. Q3 toggles between it and the normal page. Logs answer these questions:

1. Does switching pages from a bridge note work, and how fast?
2. Do the new page's names, colours, fader positions and meters arrive at the bridge on switching? Or must the script resend them?
3. Do the inactive page's callbacks (names, meters, Direct Access) keep firing? If so, every callback needs an "is this view active" check.
4. Do the fader / V-pot sub-pages stay put per page, or reset on switching?
5. Does adding several include filters give a union (e.g. INSTR = instrument + sampler tracks)?
6. Load time and CPU with more pages. The full build has 8 pages × 8 strips, each with Direct Access.

Done when: the answers are logged and written into this file. If a "no" makes pages unworkable, the plan changes here, before any real code is written.

**Results (2026-11-15, Cubase 15, branch wip-track-views):** pages work for the views.

1. **Switching:** a page switch from a bridge note takes effect in the same tick.
2. **On a switch, Cubase sends everything by itself.** Within a second it delivers the new page's names (8), colours (8), meters and fader positions. Nothing needs resending.
3. **Inactive pages are silent.** Their titles, meters, Direct Access and idle callbacks don't fire at all (counted over 3 s windows). Callbacks therefore need no "is this view active" check. CPU doesn't grow with the number of pages.
4. **Sub-pages reset on activation.** Coming back to the Mixer page activated its first sub-pages (faders Volume, V-pots Pan), so FLIP was lost. Every sub-page action has `trigger(activeMapping)`, so the page's `mOnActivate` must restore the current route's sub-pages.
5. **Include filters add up.** `includeAudioChannels()` + `includeInstrumentChannels()` gives both types. Use them without chaining their return value.
6. **Cost:** the script with two extra pages built in 8 ms.

Also learnt: reading a callback back (`var f = page.mOnActivate`) stops the whole script with "DukValue is uninitialized", and no catchable exception is raised. A Cubase error that stops a script never reaches scripts.log. Log a line before each risky step to find it.

### 2. Refactor, no change in behaviour

Move everything that is bound per page into `buildView(view)`: the bank zone, strip bindings, sub-pages, Direct Access, follow-selection and bank reports. The script then calls it once, for ALL. State that belongs to a view goes into a view object.

Test: the whole UF8 part of test-checklist.md, unchanged. This is the step where regressions would come from, so it ships alone.

### 3. The type views

- Build the pages ALL, AUDIO, INSTR (instrument + sampler), MIDI, GROUPS, FX and VCA. Inputs and outputs stay out, as today.
- The bridge switches pages with notes. For now Q3 cycles through them, so they can be tested before the picker exists.
- On a switch the script tells the bridge the view (new SysEx) and resends the strips' state. The fader / V-pot mode carries over to the new view.

Test: each view shows only its track type. Banking, fader modes, sends, SOLO / CUT / SEL, meters and colours all work in every view.

### 4. Picking a view on the UF8

- Q3 opens the picker. The top soft keys show the view names, and the current view is lit. Pressing one switches view and closes the picker; Q3 again closes it unchanged. It works the same way as Nav Mode on Q1 / Q2.
- Q3's LED is lit while a view other than ALL is on.
- The view's name shows briefly on the displays.
- Views with no tracks of that type are offered but dimmed.

Test: picking views from the surface, and that Q1 / Q2 / Nav Mode / soft keys are unchanged.

### 5. Follow selection and track numbers in a filtered view

Both now find the strips by name in the Helper's list of all channels. In a filtered view the strips aren't next to each other in that list.

- Numbers: each strip shows its project track number, so AUDIO might show 1, 4, 7. They are found by name in the Helper's numbers list, not by bank offset.
- Follow selection: selecting a track of the view's type banks to it. Selecting a track of another type leaves the bank alone.
- The bank report (SysEx 12) per view.

Test: numbers and follow selection in each view, including two tracks with the same name. Selecting a track of the view's type in the MixConsole or project window banks the UF8 to it (in the spike's AUDIO page it did not: the follow only ran on the Mixer page).

### 6. ARMED

Starts with its own spike. Two candidate routes:

- **a.** Have the Helper select the armed tracks, then run Cubase's "Show Only Selected Tracks" from the project window. This needs testing first: does it work without the MixConsole selected, and does the UF8 bank follow? Leaving the view must give back the user's own hidden tracks, so the selection and visibility must be restored. That is the risky part.
- **b.** If (a) fails: a RECORD view, showing the tracks that can record (audio, instrument, sampler and MIDI), with the armed ones marked. This is safe but not quite "armed only".

### 7. Settings and docs

- Settings: which views the picker offers, and the view at start-up (ALL, or the last one).
- user-guide.md (regenerate the HTML), test-checklist.md, bridge-midi-protocol.md (the new notes and SysEx), banking.md, and known-issues / cubase-limitations for anything Cubase can't do.

## Decided (2026-11-15)

- The release waits for this feature.
- The views are chosen on the UF8 itself, not through Cubase key commands.

## Open (defaults, change if wanted)

- INSTR includes sampler tracks.
- In a filtered view, selecting a track of another type leaves the bank alone. The alternative is to switch to ALL.
- The view is not remembered across Cubase restarts: it starts on ALL.
