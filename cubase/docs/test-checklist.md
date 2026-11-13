# Test checklist

What to check before a branch is merged into `cubase`. Each branch says which sections it touches; those are checked
in full, the rest with the quick line at the top of each section. Started 10.11.2026, after a change to the UF1's send
V-pots broke the DAW view's left display line without anyone noticing until testing.

A branch that changes how a button, knob or fader behaves also updates [control-feel.md](control-feel.md) (and
[banking.md](banking.md) for banking / track stepping) before it is merged.

Setup: Breaking Free-02, bridge from the tray app, a track with several sends and an SSL plug-in, a track with only
Cubase's own strip. Bridge log and script log on.

## Start and connection

Quick: Cubase starts, all surfaces show the project.

- Script console: no errors after a Cubase start (Reload Scripts can show "DukValue is uninitialized" from Cubase).
- scripts.log: each script's "script loaded"; the UF1's "loaded to the end".
- Close the project (Hub): "No Project" on the surfaces, the UF8 and UF1 faders at 0 dB; reopen: everything back,
  faders on the tracks' levels. Quit from the tray: faders at 0 dB.
- Restart the bridge from the tray: surfaces "Waiting for Cubase", then back by themselves.

## UF8

Quick: banking, SEL, a fader, a V-pot.

- Bank left / right, Page left / right; banking ends at the first / last track.
- Follow selection: select a track in Cubase off the strips; the UF8 banks to it.
- Faders: volume, touch; FLIP; send views (the route keys); SSL Strip Mode (PLUGIN).
- V-pots: pan; push (centres the pan; Settings > Behaviour > UF8 V-pot push = editor: the channel editor); the top soft keys (SSL soft keys, Q1 / Q2, pages); Nav Mode (NAV held 0.7 s), NAV tap.
- SOLO / CUT / SEL with LEDs; REC / AUTO selection modes. SEL on the selected track (NORM): its first SSL channel
  strip's window opens / closes (no SSL strip: the channel editor; setting = editor: always the editor); the first press on another track only selects.
- PAN (V-pots back on pan from a soft-key parameter, the V-pot send view, FLIP; lit on pan); FINE (held and
  latched: pan, a focused parameter, a send level move a quarter as far); 360 opens the Settings page.
- CHANNEL encoder: each mode (hold its push and turn); zoom pad. FOCUS: the encoder on the value under the
  mouse pointer (a plug-in knob, a MixConsole fader); FOCUS lit; about as fast as a V-pot on pan; FINE a quarter.

## UF1

Quick: each view once, a V-pot in each, the CHANNEL encoder.

- Top: track name, number and colour; the small display's value line (pan at rest).
- Fader with touch; SOLO / CUT / SEL; pan knob (value line "Pan ...").
- **DAW view**: V-pots = sends 1-4 (5-8 key: 5-8). While turning, the left display shows "S1 -6.02dB" and the bar.
  Smooth on fast turns; after stopping, the value matches the MixConsole. Push = send on / off.
- **SENDS view**: the four cells show the send names and levels at all times; they follow a turn, a mouse change in
  the MixConsole, a track change, 5-8.
- **PLUGIN view, SSL plug-in**: its pages on the V-pots and display soft keys, ◀ ▶, the EQ curve.
- **PLUGIN view, no SSL plug-in**: Quick Controls on the V-pots; their names and values in the cells; "QC1 ..." on
  the left display while turning.
- **METER view**: the Overview screen (with an SSL Meter plug-in).
- CHANNEL encoder steps tracks (MixConsole mode skips folders and hidden tracks); push opens / closes the plug-in
  window; SHIFT + turn steps the plug-ins.
- BYPASS: bypasses all inserts and restores them; an insert bypassed beforehand stays bypassed (Settings >
  Behaviour > UF1 BYPASS key = all: it is switched on too); LED dim / bright
  / off (no inserts).
- ARROW, BANK (with and without a UF8), jog modes, SCRUB, SHUTTLE, 360°, NAV cross, transport keys with LEDs.

## UC1

Quick: select a track with an SSL strip, turn a knob; the bus comp.

- SSL channel strip: knobs and keys, rings and LEDs, gain reduction meters.
- Cubase's own strip: the knobs that map, module loading, DYN IN.
- Bus Comp section, its encoder; magnifier / 360 / secondary encoder push (as set on the Settings page).
- 7-segment track number; favourites (CS Fav / BC Fav).

## Settings page and tray

Quick: the page opens from the tray; one setting changed shows at once.

- Each section opens; a change is written to settings.txt and logged once. Search ("/"): matching settings from
  all sections, words marked; a choice changed from the results is saved; Escape clears.
  Behaviour: the UF8 / UF1 / UC1 buttons show those surfaces' settings (several at once, kept after a reload);
  the ones for all surfaces always.
- Tray icon colours; menu items; Start with Windows.

## Installer and user guide

Quick: the installer builds and installs over the previous version; the tray's User guide item opens the guide.

- `build-installer.ps1` builds; the licence page shows Cubase-Sixty's licence; publisher "Geir Sveen".
- Install with Cubase-Sixty running (it is stopped and started again); Settings > About shows the version.
- The USB driver task is ticked only when needed; tray menu USB driver > Install / Remove.
- Uninstall: the program and scripts are gone; it asks about settings and favourites.
- A change to a control: user-guide.md updated, `make-user-guide.py` run, both committed.
- The guide's search ("/"): sections listed, words marked, Enter / Shift+Enter step through, Escape clears.
