# Missing features

What Rea-Sixty has that Cubase-Sixty does not (yet), from Rea-Sixty's user manual. Where Cubase-Sixty does the same
job differently, see [On second thought](on-second-thought.md); bugs are in [Known issues](known-issues.md). Some of
these may not be possible, or only partly, through Cubase's MIDI Remote API; that is checked when one is taken up.

## Suggested next (value for the effort)

1. UF1 CHANNEL encoder: push = open / close the plug-in window, SHIFT + turn = the track's plug-ins.
2. UF8 Send view: one send across the eight strips.
3. Fader on the channel strip's output gain (UF1 strip mode).

## UF8

- **Selection modes**: NORM / REC / AUTO are done; REC + MON and the V-pot cycle modes (FX Cycle, Instance Cycle)
  are not.
- **CHANNEL encoder modes** (16 in Rea-Sixty): Channel Select, Nudge, Markers, Bank by 1 and CS / BC favourite cycle are done (hold the push
  and turn to pick; NAV / NUDGE switch to Markers / Nudge); missing are Mousewheel, Last Touched Param, Instance /
  FX Cycle (also across tracks), FX Move, Selset Cycle, focused favourite cycle, Lanes.
- **Soft keys on the top row**: SSL's channel-strip pages are done (V-POT, 1-5); missing are the Q1-Q3 sets (SSL bus
  comp pages, user sets), user bindings and layers, dynamic soft-key banks.
- **SSL Strip Mode / UF8 Plug-in Mode**: a channel strip's or a plug-in's controls across the eight V-pots (and
  faders).
- **Send and Receive views** spread across the strips.
- **Nav Mode**: markers and regions on the strips (with auto-follow).
- **Two UF8s** (16 strips); **the UF1 as an extender** (a 9th fader) and Pin Set.
- **Solo / Mute modifier modes**, the **MIDI CC layer**, the **layer keys**.
- **Show only armed tracks** (AUTO: only tracks armed for writing; also "only record-enabled tracks", on the pending
  list).
- Foot switches beyond play / record (assignable).

## UC1

- **Favourites**: stepping with the CHANNEL encoder (CS Fav / BC Fav) and carrying the settings are done; missing are
  sets, the Settings page (now a text file), A/B (copy to favourite N), "own settings" (each favourite's last
  settings), the Focused Domain cycle, the favourite soft-key banks, several tracks at once, the bypass state kept.
- **FX Learn**: non-SSL plug-ins mapped onto the UC1 (and the UF1 layer), with knob travel / curves, stepped
  parameters, a multi-instance picker.
- **GR calibration** and GR meter override.
- **Sticky Pot**, **Parameter Groups**, **Selection Sets**.
- The secondary encoder's modes.

## UF1

- **Jog modes**: Playhead, Scrub, Items, Envelope, Range and Fades are done; missing are Lanes (Cubase has no lane
  commands), the Range's top / bottom edge (Nudge Top / Bottom did nothing in the Project window), the Envelope points' value (no command; Nudge Up / Down moves them to another lane;
  later: try two Project Logical Editor presets, Trim > Multiply by slightly above / below 1 on selected automation,
  run by the jog: steps would be percentages, and one forum report says such a preset changed every point), pulling a time selection with the jog, fine steps, dragging with the centre key held, the fade type, Items
  on the fader, the copy / cross-axis modifiers, the ring's order and visibility (a setting).
- **METER**: the Analogue, RTA and Loudness screens, the V-pot meter settings, FINE / PRESETS on the meter.
- **DAW view**: four track volumes on the V-pots and the user's soft-key banks (see On second thought).
- **Receives** in the SENDS view; send solo / cut.
- **CHANNEL encoder**: push = open / close the plug-in window, SHIFT + turn = step through the track's plug-in
  instances; the encoder modes shown in the header.
- **PLUGIN**: Cubase's own channel strip in pages (tried and rolled back), jump to the page of a control touched in
  the plug-in window, HQ MODE / A/B, the Harrison 32C.
- Fader on the channel strip's output gain (strip mode).

## General

- **A settings window** (see the [settings wishlist](settings-wishlist.md)).
- **Installer / packaging.**
- Brightness and sleep settings for the surfaces.

## REAPER only (probably not ported)

Fixed lanes, razor editing, the TotalReaper (RME) integration, REAPER's mixer overlays and on-screen display.
