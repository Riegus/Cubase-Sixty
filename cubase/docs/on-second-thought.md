# On second thought

Places where Cubase-Sixty does something differently from Rea-Sixty, kept so each can be looked at again later.
Each entry says what Rea-Sixty does, what Cubase-Sixty does, and why. **Forced** means Cubase (or the MIDI Remote API)
left no real choice; **chosen** means it was a decision and could go the other way. A choice that should become a
user setting also goes on the [settings wishlist](settings-wishlist.md).

## UC1

- **7-segment display** (chosen). Rea-Sixty shows leading zeros ("007"); Cubase-Sixty blanks them and shows the
  MixConsole number.
- **Bus Comp section before one is picked** (chosen). Rea-Sixty takes the master track's bus comp, else the first
  track's; Cubase-Sixty takes the one on an output channel, else the first in the MixConsole.
- **Magnifier and 360° keys** (chosen). Rea-Sixty lets the user bind them (and the Bus Comp encoder push) to actions;
  Cubase-Sixty fixes them: magnifier = next channel strip on the track, 360° = open / close the strip's window.
- **ROUTING: External S/C** (chosen). Rea-Sixty steps External S/C off / on between the ten orders (20 positions);
  Cubase-Sixty steps only the ten orders and leaves External S/C alone, because the UC1 showed every number twice.
- **ROUTING order 10** (chosen). Shows picture 10 with the numbers running 1–10, although the picture is slightly
  off; picture 8 (renumbered) was the alternative.
- **Gate Threshold direction** (forced). Rea-Sixty turns Channel Strip 2's Gate Threshold round; in Cubase it
  already runs the right way, so Cubase-Sixty does not.
- **Preset name in the plug-in** (forced). Loading an SSL preset sets the values one by one; the plug-in's own preset
  name does not change, because Cubase gives no way to load the preset file into the plug-in.
- **Cubase's own channel strip** (chosen, an addition). Rea-Sixty has nothing like it (REAPER has no built-in
  strip). On a track without an SSL channel strip, the UC1 drives Cubase's input filter, EQ, gate and compressor,
  and loads the strip modules when needed (gate / compressor knobs, DYN IN, EXP, the compressor picker). Within
  that: F.ATT flips the Release knob between release and attack; the bell keys switch to Parametric II; on the Tube
  and Vintage compressors the Ratio knob is the input and the Threshold knob the output; DYN IN works the compressor
  only and EXP the gate.

## UF1

- **DAW view** (chosen). Rea-Sixty: V-pots = the volumes of four tracks (the selected one and the next three; 5-8
  moves to the next four), display soft keys = the user's bank of assignable keys. Cubase-Sixty: V-pots = sends
  1–4 / 5–8 of the selected track, display soft keys unused.
- **SENDS view** (chosen). Rea-Sixty pages the sends in groups of four in the mixer's send order (hardware outputs
  included) and shows "N/M"; Cubase-Sixty shows sends 1–4 or 5–8 (the 5-8 key) and "1/2" / "2/2".
- **◀ ▶ arrows** (chosen). Rea-Sixty: they page (soft-key pages, channel-strip pages, send groups) in every view.
  Cubase-Sixty: previous / next track, except in PLUGIN with an SSL plug-in, where they page; SHIFT + arrow steps a
  bar.
- **BANK** (chosen). Rea-Sixty: eight tracks back / forward. Cubase-Sixty: banks the UF8 (eight tracks only when
  there is no UF8).
- **Channel encoder** (not yet). Rea-Sixty: push opens / closes the plug-in window, SHIFT + turn steps through the
  plug-in instances, and other encoder modes can be picked (shown in the header). Cubase-Sixty: turn steps the
  track; nothing else yet.
- **Quick keys 1 and 2** (not yet). Rea-Sixty: quick keys, key 2 = FINE (shown in the header's FINE CTRL field).
  Cubase-Sixty: only with SHIFT (Latch / Touch); the header always shows FINE OFF.
- **SCRUB** (chosen, an addition). Rea-Sixty leaves it free; Cubase-Sixty scrubs through a Mackie Control device,
  and SHIFT + SCRUB is a shuttle.
- **Key above the display** (chosen). Rea-Sixty: assignable. Cubase-Sixty: bypass / restore all inserts of the
  track, with the LED showing their state.
- **PLUGIN view without an SSL plug-in** (chosen). Rea-Sixty shows nothing there (a control that shows a value it
  cannot move was judged worse than none). Cubase-Sixty puts the V-pots on Cubase's Quick Controls and shows their
  names and values.
- **PLUGIN V-pot push** (chosen). Rea-Sixty puts any control back to its default. Cubase-Sixty only puts the
  bipolar ones (EQ gains, trims) back to the centre: Direct Access gives no default value.
- **PLUGIN keys without a parameter** (forced). HQ MODE and A/B: Rea-Sixty switches them in the plug-in's saved
  state; Cubase gives no way to do that, so they only show their label. (SOLO SAFE, PLUG-IN and S/C MODE are label
  only in both.)
- **PLUGIN page jump** (not yet). Rea-Sixty jumps to the page of a control touched in the plug-in window.
- **PLUGIN strips** (not yet). Rea-Sixty also has the Harrison 32C and FX-Learned plug-ins, and Cubase-Sixty does not
  show Cubase's own channel strip here yet.
- **Fader in strip mode** (not yet). Rea-Sixty can put the fader on the channel strip's output gain.
- **METER view V-pots** (not yet). Rea-Sixty: the meter's settings. Cubase-Sixty: they do nothing in METER (so they
  cannot change sends unseen). FINE, PRESETS and the Analogue / RTA / Loudness screens are not there yet.
- **METER bar scale** (forced, for now). Rea-Sixty reads the plug-in's Digital Type; Cubase-Sixty always uses the
  Non-Linear scale, because the bridge cannot read that parameter.
- **Header field under CHANNEL** (chosen). Rea-Sixty writes "REAPER", Cubase-Sixty "Cubase-Sixty".

## Whole system

- **A bridge program plus MIDI Remote scripts** (forced). Rea-Sixty runs inside REAPER and talks to the hardware and
  to REAPER directly; Cubase-Sixty needs a separate bridge for the USB side and talks to Cubase over virtual MIDI
  ports.
- **Logs** (chosen). The scripts' log lines go through the bridge into `cubase\logs\scripts.log` (switch:
  `--no-script-log`), next to `bridge.log`, instead of only Cubase's script console.
