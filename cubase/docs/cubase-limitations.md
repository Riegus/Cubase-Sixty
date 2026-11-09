# Cubase limitations

What Cubase (15 Pro, MIDI Remote API 1.3) does not let Cubase-Sixty do, or only in a roundabout way, found while
porting Rea-Sixty. Each entry says what is missing and what Cubase-Sixty does instead. What this costs in features is
in [Missing features](missing-features.md); where it made Cubase-Sixty behave differently from Rea-Sixty, in
[On second thought](on-second-thought.md). Everything about banking and track stepping (timings, folders, what was tried)
is collected in [Banking and track stepping](banking.md).

## Not available to scripts at all

- **Markers.** No list of markers or cycle markers: no names, positions, colours, or which IDs exist. Only Cubase's
  numbered commands: To Marker 1–9, Recall Cycle Marker 1–9, Locate Previous / Next Marker. So Nav Mode works by
  marker ID (an ID the project does not use does nothing), and "Recall Cycle Marker" only sets the locators (the
  cursor needs To Left Locator after it).
- **Receives.** No access to a channel's receives (no Receive views).
- **Send pan and send solo.** A send slot has level, on / off and pre / post only.
- **Project-wide solo state.** No "anything soloed" value: the Helper script counts the soloed channels itself.
- **Automation mode per track.** Only Read / Write switches; no Touch / Latch / Trim mode per track (AUTO toggles W).
- **Lanes.** No commands to step through or comp lanes (no Lanes jog mode).
- **Automation point values.** No command changes the value of selected automation points; Nudge Up / Down moves
  them to another lane. (Possible later: Project Logical Editor presets, see Missing features.)
- **Range selection's top / bottom edge.** Nudge Top / Bottom Up / Down do nothing in the Project window.
- **A parameter's default value.** Direct Access gives none, so a V-pot push can only centre bipolar controls.
- **A plug-in's own state.** No way to load a preset file into a plug-in (SSL presets are applied value by value,
  and the plug-in's preset name does not change) or to switch state-only options (HQ MODE, A/B).
- **Selecting a track through Direct Access.** It can read the mixer (and whether a channel is visible) but not
  select; selection goes through the track selection's next / previous actions or commands.
- **Files and USB.** Scripts cannot read files or reach USB devices: a separate bridge program does the hardware, the
  favourites and the settings, talking to the scripts over virtual MIDI ports.
- **The plug-in list for strip slots.** The plug-in manager lists nothing for the channel strip's module slots
  (Cubase-Sixty loads Cubase's strip modules by their class ids instead).

## Works, but with a catch

- **Back-to-back MIDI notes can be lost or merged.** Two or more notes sent to a script in a row sometimes arrive as
  one. The bridge spaces them 40 ms apart (FLIP's two route notes, bank steps, Nav Mode's cycle key).
- **New selection is reported late.** After a next / previous track action, the new track's name comes up to a second
  later, so "step, then look" is slow: hidden tracks are skipped by counting first (the MixConsole's channel list)
  and stepping once.
- **Sub-page changes.** The new bindings' values arrive before the sub-page's activation is announced, so displays
  are drawn again after it (FLIP showed values in each other's places).
- **Next / previous track and Select Track include hidden tracks.** Skipping them needs the MixConsole's visibility
  (Direct Access: isMixerChannelVisible) or the Project window's Navigate Up / Down.
- **The MixConsole's object tree.** Instrument tracks are type "Synth", not "...Channel"; the VST Mixer's children are
  the channels.
- **Insert effect viewers.** Moving one (next / previous slot) takes effect a little later, and sixteen fixed-slot
  viewers made every track change lag; the plug-in window is opened through one viewer, moved by the script and
  checked by the plug-in's name. Increment / decrement on its edit switch do nothing (a small step on a switch):
  it is set through a bound value variable.
- **A swapped plug-in shows up late.** After loading a plug-in into a slot (favourites), Direct Access shows the new
  one only seconds later; the script counts from what it loaded meanwhile.
- **Send levels lag** when moved from a script (fader or V-pot), on every surface.
- **Callbacks cannot be read back.** A function assigned to a surface value's callback is not returned when read; the
  scripts keep their own reference.
- **Commands only through MIDI.** A script cannot run a command binding itself: the bridge sends the note it is bound
  to. Action bindings (track selection, insert viewer, mixer bank) can be triggered from the script.
- **Loading a plug-in into a slot** needs API 1.3 (Cubase 15.0.20 or later): trySetSlotPlugin, with the class id
  found by name in the plug-in manager's collections.
- **No project callback.** Closing a project (Cubase goes to the Hub) and opening one are seen only as the script's
  driver being deactivated and activated. On activation Cubase sends only what changed, so the bridge keeps what
  each surface showed and puts back the rest.
- **Key command names.** The Key Commands file lists only part of Cubase's commands, so a command's exact name
  ("To Marker 1", "To Left Locator") can only be confirmed by trying it.
