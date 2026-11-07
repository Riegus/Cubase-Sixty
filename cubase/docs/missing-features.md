# Missing features

What Rea-Sixty has that Cubase-Sixty does not (yet), from Rea-Sixty's user manual. Where Cubase-Sixty does the same
job differently, see [On second thought](on-second-thought.md); bugs are in [Known issues](known-issues.md). Some of
these may not be possible, or only partly, through Cubase's MIDI Remote API; that is checked when one is taken up.

## Suggested next (value for the effort)

1. More CHANNEL encoder modes: Nudge, Markers, Bank by 1.
2. UC1 favourites.
3. UF1 jog modes.

## UF8

- **Selection modes**: NORM / REC / AUTO are done; REC + MON and the V-pot cycle modes (FX Cycle, Instance Cycle)
  are not.
- **CHANNEL encoder modes** (16 in Rea-Sixty): Nudge, Mousewheel, Markers, Bank by 1, Last Touched Param, Instance /
  FX Cycle (also across tracks), FX Move, Selset Cycle, CS / BC / focused favourite cycle, Lanes; hold the push and
  turn to pick a mode. Cubase-Sixty: channel select only.
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

- **Favourites** for the channel strip and the bus comp: slots and sets, A/B (copy to favourite N), copying values or
  restoring each favourite's own settings, several tracks at once.
- **FX Learn**: non-SSL plug-ins mapped onto the UC1 (and the UF1 layer), with knob travel / curves, stepped
  parameters, a multi-instance picker.
- **GR calibration** and GR meter override.
- **Sticky Pot**, **Parameter Groups**, **Selection Sets**.
- The secondary encoder's modes.

## UF1

- **Jog modes**: the jog wheel and nav cross for nudge, time selections, fades and more. Cubase-Sixty: scrub only.
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
