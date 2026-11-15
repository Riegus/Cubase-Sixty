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

**Done (15.11.2026, 0.17.0-dev).** `eachPage(function (page, bankZone, view) {...})` blocks, built by `buildViews()` at the end of the script, and `useView()` for the per-view Cubase objects. Checked offline with `cubase/midi-remote/tools/binding-snapshot.js`: the same 851 bindings and callbacks as before. Tested on the UF8. Found on the way (already on `cubase`, fixed): SSL Strip Mode didn't follow the plug-in's fader and pan, and its dB field didn't follow the fader. It is now polled like the send views. The send views' lag is unchanged (the known Cubase issue).

### 3. The type views

- Build the pages ALL, AUDIO, INSTR (instrument + sampler), MIDI, GROUPS, FX and VCA. Inputs and outputs stay out, as today.
- The bridge switches pages with notes. For now Q3 cycles through them, so they can be tested before the picker exists.
- On a switch the script tells the bridge the view (new SysEx) and resends the strips' state. The fader / V-pot mode carries over to the new view.

Test: each view shows only its track type. Banking, fader modes, sends, SOLO / CUT / SEL, meters and colours all work in every view.

**Done (15.11.2026, script 0.18.0-dev).** Tested on the UF8: every view, banking, the route carried over by the bridge's route notes. Found on the way: the fader send view's dB value lagged, and the fader and the value jumped when the hand stopped. The bridge wrote the hand to Cubase 50+ times a second and Cubase fell seconds behind. Fixed: at most one write every 60 ms, the dB value drawn from the send level curve, and late values and positions ignored for 3 s after the hand (see control-feel.md).

### 4. Picking a view on the UF8

- Q3 opens the picker. The top soft keys show the view names, and the current view is lit. Pressing one switches view and closes the picker; Q3 again closes it unchanged. It works the same way as Nav Mode on Q1 / Q2.
- Q3's LED is lit while a view other than ALL is on.
- The view's name shows briefly on the displays.
- Views with no tracks of that type are offered but dimmed. (Not done in step 4: the UF8 script hears only the active page, so it can't count the other views' tracks. Possible with the Helper's lists in step 5.)
- After a pick the chosen key stays lit for 0.6 s, then the keys go back. That is the feedback, instead of the view's name on the displays: the new view's names arrive in the same moment and would overwrite it.

Test: picking views from the surface, and that Q1 / Q2 / Nav Mode / soft keys are unchanged.

**Done (15.11.2026, bridge only).** Tested on the UF8.

### 5. Follow selection and track numbers in a filtered view

Both now find the strips by name in the Helper's list of all channels. In a filtered view the strips aren't next to each other in that list.

- Numbers: each strip shows its project track number, so AUDIO might show 1, 4, 7. They are found by name in the Helper's numbers list, not by bank offset.
- Follow selection: selecting a track of the view's type banks to it. Selecting a track of another type leaves the bank alone.
- The bank report (SysEx 12) per view.

How it was built: the Helper has one names-only zone per filtered view (the same filters, following visibility as the UF8's banks do) and sends each list (1E) to the UF8 script and its project numbers (1F, matched against the numbers zone like the full list) to the bridge. The UF8 script's follow block works on the active view's list; switching views looks the selection up in the new list and banks to it. The picker darkens views whose list is empty.

Test: numbers and follow selection in each view, including two tracks with the same name. Selecting a track of the view's type in the MixConsole or project window banks the UF8 to it (in the spike's AUDIO page it did not: the follow only ran on the Mixer page).

**Done (15.11.2026; UF8 script 0.19.0, Helper 0.4.0).** Tested on the UF8. Found on the way:
- The bridge dropped the views' numbers (it expected 1F one byte longer).
- After a view switch Cubase drops bank presses for a second or more. The UF8 is held frozen from the switch until it is on the selected track, so only the end shows.
- Near the end of a list, bank-right's single steps each moved the motors ("banking twice"), and at 60 ms apart one could be lost. The bridge now freezes the UF8, spaces the steps 100 ms apart, and shows the end as soon as the script reports it.
- The UF8 script logs when Cubase pauses (idle calls). Banking and selecting were found acceptable with the Helper's six extra lists.

**Then, the same day:**
- **Bank moves by the script itself** (`bankZone.mAction.….trigger(activeMapping)`), all at once. A follow jump landed in 40–120 ms, every time, also right after a view switch. Bank-right near the end landed in ~0.2 s. Now the normal path; the bridge's bank notes are the fallback.
- **The Helper's lists cost ~0.9 s of stall after every selection change** (A/B: the longest gap 2.0 s with them, 1.1 s without). They are now read through a 16-channel window per view that the Helper banks through itself: 96 channels instead of 1,280. The stall is back to ~1.0–1.1 s. Reading all six takes 6–8 s in the background, 1 s after the full list last changed (a track added, removed, renamed or hidden).

### 6. ARMED

Starts with its own spike. Two candidate routes:

- **a.** Have the Helper select the armed tracks, then run Cubase's "Show Only Selected Tracks" from the project window. This needs testing first: does it work without the MixConsole selected, and does the UF8 bank follow? Leaving the view must give back the user's own hidden tracks, so the selection and visibility must be restored. That is the risky part.
- **b.** If (a) fails: a RECORD view, showing the tracks that can record (audio, instrument, sampler and MIDI), with the armed ones marked. This is safe but not quite "armed only".

**Built instead (15.11.2026, branch wip-views-armed): a route c, made possible by the script's own bank moves.** The ARMED page gives each strip a bank zone of one channel (the recordable types), and the script moves each one to an armed track. The Helper reports every recordable channel's record-enable (value bindings on its recordable list, SysEx 2F). What was found on the way:
- **Zones with the same filters on one page move together**, whatever their names: Cubase keeps one bank position for them. Each strip's zone needs a filter set of its own that holds the same channels. Includes of audio, instrument, sampler and MIDI, plus each combination of "exclude groups / FX / VCA" (8 sets) work. "Exclude inputs / outputs" on top of includes counted for nothing.
- **Include filters and exclude filters don't hold the same channels.** "Exclude inputs, outputs, groups, FX and VCA" also held one channel that the includes leave out (not identified: "Northern Texture", suspected, is an instrument track and is in the includes list). The Helper's recordable list now uses the includes too, so the positions match.
- **Cubase puts a page's zones back to the start each time the page is activated.** ARMED lays its strips out afresh on every activation (~120 moves in ~50 ms, no stall).
- **A strip without an armed track still has a channel behind it.** The bridge marks it dead (SysEx 32 with 7F 7F) and blanks it: name, colour, dB, meter, LEDs and fader down. It drops what Cubase sends for that strip, and ignores the hand on it. The script tells the bridge each strip's track in the full list (SysEx 32), for the numbers and SSL plug-in names.
- REC mode's SEL colours had to become solid colours (as Rea-Sixty's) once the selected-strip mask was sent.
- **Cubase scrolls every zone of a page to a newly selected track**, so all eight one-channel zones jumped to it. After a real selection change (Cubase re-reports the selected track after every layout, so compare the name), the UF8 is held, laid out afresh 0.3 s later, and shown again.
- **The bank keys' own callback didn't reach the script in ARMED.** The bridge pages instead (SysEx 30, kinds 5–8).
- **The bridge's handling of empty strips** now keeps SOLO / CUT / SEL sent for an empty strip and shows them when a track's name arrives.

**Done (15.11.2026; UF8 0.22.0, Helper 0.6.0).** Tested on the UF8:
- ARMED shows the armed tracks, eight at a time. Bank / page keys page through them, and it follows the selection to its page.
- Arming and disarming rearranges it live. Blank strips are dark and ignore the hand.
- The picker darkens ARMED with nothing armed.
- VCAs can be armed (REC mode, top soft keys); their linked tracks then show in ARMED.
- The Helper's arm states cost nothing measurable: the longest stall after a selection change stayed at ~1 s.

### 7. Settings and docs

**Done (15.11.2026, branch wip-views-settings).** Settings > UF8 track views: the view at start (ALL by default, any view, or "the last view used", kept in uf8-1-last-view.txt) and the views the picker offers (one switch per view; one switched off is dark and does nothing). The keys have the unit's number (`uf8-1-...`), so a second UF8 gets its own. The start view is applied 3 s after a project opens, if the UF8 is on ALL then. The user guide has a section "Track views". The test checklist, the protocol and this plan are updated.


- Settings: which views the picker offers, and the view at start-up (ALL, or the last one).
- user-guide.md (regenerate the HTML), test-checklist.md, bridge-midi-protocol.md (the new notes and SysEx), banking.md, and known-issues / cubase-limitations for anything Cubase can't do.

## After the views

- ~~The UF8 faders follow Cubase's fader curve, not the UF8's printed dB scale~~: done 15.11.2026, on `cubase` (placed by dB, the marks measured on the hardware).

## Decided (2026-11-15)

- The release waits for this feature.
- The views are chosen on the UF8 itself, not through Cubase key commands.

## Open (defaults, change if wanted)

- INSTR includes sampler tracks.
- In a filtered view, selecting a track of another type leaves the bank alone. The alternative is to switch to ALL.
- The view is not remembered across Cubase restarts: it starts on ALL.
