# Cubase-Sixty user guide

Cubase-Sixty makes the SSL UF8, UF1 and UC1 work in Cubase the way [Rea-Sixty](../../README.md) makes them work in
REAPER: faders, V-pots and displays for the mixer, the UC1 on the SSL plug-ins of the selected track, without SSL 360.
Any one of the three surfaces is enough; they work alone and together.

Tested with Cubase 15 Pro on Windows 11.

- [What you need](#what-you-need)
- [Installing](#installing)
- [Setting up Cubase (once)](#setting-up-cubase-once)
- [Every day](#every-day)
- [UF8](#uf8)
- [UF1](#uf1)
- [UC1](#uc1)
- [Settings](#settings)
- [Going back to SSL 360](#going-back-to-ssl-360)
- [When something is wrong](#when-something-is-wrong)
- [Uninstalling](#uninstalling)

## What you need

- Windows 10 or 11 (64 bit) and Cubase 13 or later with MIDI Remote (developed on Cubase 15 Pro).
- An SSL UF8, UF1 or UC1, connected by USB.
- **A virtual MIDI driver** (teVirtualMIDI). It comes with [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html)
  (free) and with SSL 360. Cubase-Sixty makes its own MIDI ports with it; you do not create any ports in loopMIDI.
- **SSL 360 not running.** SSL 360 and Cubase-Sixty cannot use the controllers at the same time (see
  [Going back to SSL 360](#going-back-to-ssl-360)). The SSL plug-ins themselves work as always.

## Installing

1. Plug the controllers in and switch them on.
2. Run `CubaseSixty-Setup-<version>.exe`. Windows may show "Windows protected your PC", because the installer is not
   signed: choose **More info**, then **Run anyway**. It installs for your user account and needs no administrator
   rights by itself.
3. On the options page:
   - **Start Cubase-Sixty with Windows**: leave it ticked unless you want to start it by hand.
   - **Install the USB driver for the SSL controllers**: ticked by itself when it is needed. It asks for
     administrator permission once. This moves the controllers from SSL's own USB driver to Windows' WinUSB driver,
     which Cubase-Sixty needs; from then on SSL 360 does not see them.
4. When the installer has finished, **unplug and replug the controllers** if the driver was installed.
5. If the installer says the virtual MIDI driver is missing, install loopMIDI and start Cubase-Sixty again.

Cubase-Sixty now sits in the notification area (the tray) as a small mixing-surface icon, and the controllers show
"Waiting for Cubase".

What is installed where: the program in `%LOCALAPPDATA%\Programs\Cubase-Sixty` (with your settings, favourites and
logs beside it), four MIDI Remote scripts in `Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty`.

## Setting up Cubase (once)

Start Cubase with Cubase-Sixty running. Cubase finds the scripts and the ports by itself: *Studio > MIDI Remote
Manager* lists CubaseSixty - UF8, UF1, UC1 and Helper (the Helper has no hardware; it keeps lists for the others).
Open a project and the controllers come alive. Then two things in *Studio > Studio Setup*:

1. **MIDI Port Setup: untick "In 'All MIDI Inputs'" for every port whose name starts with `Cubase-Sixty`** (ten of
   them). Left ticked, every MIDI and instrument track receives the controllers' messages as played notes, and
   stepping through tracks is noticeably slower. Cubase may tick it again for a port it sees as new, so look again
   after the first start.
2. **For SCRUB on the UF1: add a Mackie Control device.** Click **+** (Add Device), choose **Mackie Control**, select
   it and set **MIDI Input** to `Cubase-Sixty MCU to Cubase` and **MIDI Output** to `Cubase-Sixty MCU from Cubase`,
   then **Apply**. Nothing else uses this device.

## Every day

Cubase-Sixty runs in the tray, whether Cubase is open or not. The icon's colour says how things are:

| Icon | Meaning |
|------|---------|
| Green | Cubase has a project open; the controllers are live. |
| Yellow | Running; Cubase is not open, has no project, or has not connected yet. |
| Red | The bridge (the part that talks to the controllers) is not running. |

- **Click the icon** for the Settings page (in your browser); **right-click** for the menu: Settings, Restart bridge,
  Open logs folder, Start with Windows, USB driver, Quit.
- Controllers can be plugged in and unplugged at any time.
- Closing a project blanks the displays; the faders stay where they are.
- After **Restart bridge**, Cubase finds it again by itself within some seconds. If the controllers stay on "Waiting
  for Cubase" for long, restart Cubase.
- After a script update, or if a surface behaves oddly after Cubase's own *Reload Scripts*, restart Cubase.

## UF8

Eight strips on a bank of the MixConsole's channels (in MixConsole order, hidden channels left out). The UF8 follows
the track selection: select a track anywhere in Cubase and the UF8 banks to it.

### Each strip

| Control | What it does |
|---------|--------------|
| Fader | Volume, with touch (Touch automation works). In dB on the display. |
| V-pot | Pan; value and bar on the strip's display. Push: opens / closes the channel editor. |
| SEL | Selects the track. In REC mode it arms the track, in AUTO mode it switches the track's automation Write (lit red when armed / writing). |
| SOLO, CUT | Solo and mute. |
| Top soft key | SSL's soft keys: a parameter of the track's SSL channel strip (see below). |
| Display | Track name and colour, level, stereo meter. |

### Left side

| Keys | What they do |
|------|--------------|
| V-POT, 1–5 | The six pages of the top soft keys (the SSL channel strip's parameters). |
| Q1 / Q2 | Q2 puts the bus compressor's parameters on the soft keys (two pages), Q1 goes back to the channel strip. |
| A top soft key | Puts its parameter on all eight V-pots, each for its own track's SSL channel strip, until pressed again. |
| PAGE ◂ ▸ | Moves the strips by one channel. |
| CHANNEL | Home: faders on volume, V-pots on pan. |
| PLUGIN | SSL Strip Mode: each fader on its track's SSL channel strip fader (tracks without one keep volume). |
| FLIP | Tap: swaps faders and V-pots. Hold (about half a second): the **selected track's sends 1–8 on the V-pots** (turn = level, push = on / off); hold again to leave. |
| SEND / PLUGIN 1–8 | **Send N of every track on the faders** (CUT switches the send on / off; a track without that send has its fader at the bottom). Press again to leave. |
| READ, WRITE | Automation Read / Write of the selected track. |
| OFF, TRIM, LATCH, TOUCH | Cubase's automation mode (OFF = Cross-Over). |
| FOOT 1 / 2 | Foot switches: play, record. |

### Right side

| Keys | What they do |
|------|--------------|
| NORM / REC / AUTO | What SEL does (select / arm / automation Write). |
| BANK ◂ ▸ | Moves the strips by eight. Bank right stops with the last track on strip 8. |
| CHANNEL encoder | Turn: previous / next track. **Hold its push and turn** to give it another job: Channel Select, Nudge, Markers, Bank by 1, CS Fav / BC Fav (steps through your favourite channel strips / bus comps, see Settings). |
| NAV | Tap: the encoder moves between markers (lit); tap again for tracks. **Hold 1 s: Nav Mode**: the top soft keys jump to markers 1–8 (in the Q1 view: cycle markers 1–8). |
| NUDGE | The encoder nudges the cursor. |
| Cursor pad | Zoom in / out, vertical zoom; centre opens / closes the editor of the selected event. |

## UF1

One strip on the **selected track**, a big display, and transport.

| Control | What it does |
|---------|--------------|
| Fader, SOLO, CUT, SEL | The selected track's volume (with touch), solo, mute. |
| Knob above the fader | Pan. |
| Small display | Track name, number and colour, level, meter, and a value line: pan, or the send / Quick Control being turned. |
| MODE (hold) + a display soft key | The view: DAW, SENDS, PLUGIN or METER (the start view is a setting). |
| CHANNEL encoder, ARROW | Previous / next track (folders and hidden tracks are skipped; a setting). Encoder push: opens / closes the plug-in window of the PLUGIN view, or the channel editor. SHIFT + turn: the track's next / previous SSL plug-in. |
| BANK | Banks the UF8; without a UF8, moves the selection by eight tracks. |
| 5–8 | V-pots on sends 5–8 (Quick Controls 5–8 in PLUGIN). |
| MASTER | The strip on the main output. |
| FLIP | Swaps the fader and the pan knob. |
| Transport keys | Rewind, forward, stop, play, record, cycle, click, with their LEDs. |
| SHIFT + transport soft keys / Cycle / Click | Automation Read, Write, Cross-Over, Trim, Latch, Touch. |
| Jog wheel | Moves the cursor; its modes (Playhead, Items, Envelope, Range, Fades) nudge, navigate and change fades. SHIFT: markers. SHIFT + ARROW: bar steps. |
| SCRUB | Scrubbing (needs the Mackie Control device, see setup). SHIFT + SCRUB: shuttle. |
| NAV cross | Zoom; centre opens / closes the editor. |
| 360° | Swaps the two time formats. |
| BYPASS | Bypasses all inserts of the track. |
| FOOT 1 / 2 | Foot switches. |

### The views

- **DAW** and **SENDS**: the four V-pots are the selected track's sends 1–4 (5–8 with the 5–8 key): turn = level,
  push = on / off. SENDS shows all four sends' names, levels and bars above the V-pots at all times; DAW shows the
  send on the small display while you turn it.
- **PLUGIN**: the track's SSL plug-in in pages, as in Rea-Sixty: V-pots, display soft keys and ◀ ▶ on its controls,
  the EQ curve in the graph. Without an SSL plug-in: Cubase's **Quick Controls** 1–4 (5–8) of the focused plug-in
  window, or of the track. Only assigned Quick Controls do anything; an empty one shows `QC1 -`.
- **METER**: the SSL Meter / Meter Pro plug-in's overview, when the track has one.

## UC1

The UC1 works the **selected track's channel strip** and one **bus compressor**.

- **With an SSL channel strip on the track** (Channel Strip 2, 4K B / E / G, 360 Link): the Channel Strip knobs and
  keys are the plug-in's, the rings and LEDs follow it (also when you use the mouse, or automation), and the Dyn / Gate
  LED rows show its gain reduction.
- **Without one**: the knobs work **Cubase's own channel strip**: input filters, the four EQ bands (the bell keys
  switch the outer bands between shelf and bell), gate and compressor. Reaching for a gate or compressor that is not
  loaded loads it: EXP or a Gate knob loads the Noise Gate; a short press on DYN IN loads a compressor; a Dyn knob,
  or DYN IN held, opens the compressor picker (turn to choose Standard / Tube / Vintage, DYN IN to load). F.ATT
  switches the section's Release knob to attack and back. All of this can be changed in Settings.
- **Bus Comp section**: a Bus Compressor 2 (or 360 Link Bus Compressor) anywhere in the project. The **Bus Comp
  encoder** steps through the project's bus comps; the large name row shows which one (and its neighbours). It stays
  on that one while you select other tracks. The needle shows its gain reduction.

| Control | What it does |
|---------|--------------|
| CHANNEL encoder | Previous / next track. |
| SOLO, CUT, Ø | The track's solo, mute and polarity. Solo Clear clears all solos. |
| Channel IN / Bus Comp IN | The strip's / the bus comp's bypass. |
| FINE | Finer knob steps. |
| Magnifier | The next channel strip on the track, when it has several (a setting). |
| 360° | Opens / closes the strip's plug-in window (a setting). |
| Secondary encoder | Hold its push and turn to choose its job; a short push opens the window of the plug-in it is on (a setting). |
| PRESETS | SSL's preset library for the channel strip or the bus comp: the Bus Comp encoder moves, CONFIRM opens / loads, BACK goes up. |
| ROUTING | The SSL strip's routing order; the Bus Comp encoder steps through the ten orders. |
| Display | Track name and colour, the plug-in, the value being turned; the 7-segment shows the track's MixConsole number. |

## Settings

Click the tray icon. The page opens in your browser; it is served by Cubase-Sixty on this computer only. A change
is saved and takes effect at once.

| Section | What is there |
|---------|---------------|
| Devices | Which surfaces and whether Cubase is connected; key / LED and display brightness; sleep when idle. |
| Behaviour | How the channel encoders step through tracks (as the MixConsole shows them, as the Project window does, or every track); touching a UF8 fader selects its track; the UF1's view at start; the UC1 secondary encoder's job at start; the UC1 knob step. |
| UC1 keys | What the magnifier, 360° and secondary encoder push do. |
| UC1 on Cubase's strip | Whether the knobs / keys load a missing gate or compressor; which compressor DYN IN loads; which bus comp the Bus Comp section starts on. |
| Display | How long track names are shortened; the UC1's 7-segment leading zeros. |
| Favourites | Eight channel strips and eight bus comps for the CS Fav / BC Fav encoder modes. |
| Logging | The script log (off by default; switch it on when reporting a problem). |
| About | The version, and where the logs and settings files are. |

"Project window" track stepping only works while Cubase's Project window has the focus: click in it first.

## Going back to SSL 360

SSL 360 uses SSL's own USB driver for the controllers; Cubase-Sixty (like Rea-Sixty) needs Windows' WinUSB driver.
A controller can only be on one of them, so the two programs cannot share the controllers, and quitting Cubase-Sixty
is not enough.

- To give the controllers back: tray menu **USB driver > Remove it (give the controllers back to SSL 360)**, then
  unplug and replug them. If SSL 360 still does not see them, run SSL 360's installer again.
- To take them again: tray menu **USB driver > Install the driver for the SSL controllers**, with the controllers
  plugged in; then unplug and replug them.

Each asks for administrator permission once. Rea-Sixty uses the same driver: with it installed, both Rea-Sixty and
Cubase-Sixty see the controllers (one at a time: quit the one before starting the other).

While Cubase-Sixty runs it closes SSL 360's background program (SSL360Core.exe) whenever it appears, because the two
compete for the SSL plug-ins' gain reduction meters.

## When something is wrong

| What you see | What to do |
|--------------|-----------|
| The controllers stay dark, the icon is yellow or green | The USB driver: tray menu **USB driver > Install**, with the controllers plugged in, then replug them. Is SSL 360 running? Quit it. |
| "Waiting for Cubase" although a project is open | Restart Cubase with Cubase-Sixty running. In *Studio > MIDI Remote Manager* the four CubaseSixty devices should be listed and active. |
| Red icon | Right-click > **Start bridge**. If it will not stay up: the virtual MIDI driver may be missing (install loopMIDI), or ports with the names `Cubase-Sixty ...` exist in loopMIDI (delete them there). |
| An instrument plays when you touch a control | "In 'All MIDI Inputs'" is ticked for a Cubase-Sixty port (see setup). |
| SCRUB does nothing | The Mackie Control device is missing (see setup). |
| A surface is odd after *Reload Scripts* | Restart Cubase; a reload does not always set the scripts up fully. |
| Stepping to a track lands somewhere else | "Project window" stepping needs the Project window focused; the default (MixConsole) does not. |

For a report: switch on the script log (Settings > Logging), reproduce the problem, and send the files from the logs
folder (tray menu **Open logs folder**) with what you did and what you expected.

Known issues and limits:

- [Known issues](known-issues.md)
- [What Cubase does not allow](cubase-limitations.md)
- [What Rea-Sixty has that Cubase-Sixty does not](missing-features.md)

Some of them in short: a track's colour reaches the displays when the track is next selected or banked in, not the
moment it is changed; on fast turns of a send the MixConsole trails the controller a little and then lands on the
same value; a preset loaded from the UC1 sets the plug-in's controls but not the preset name in its window.

## Uninstalling

Windows *Settings > Apps > Installed apps > Cubase-Sixty > Uninstall*. It stops Cubase-Sixty, removes the program
and the scripts, and asks whether to remove your settings and favourites too.

The USB driver stays (Rea-Sixty may be using it). To give the controllers back to SSL 360, remove the driver from
the tray menu **before** uninstalling, or run SSL 360's installer afterwards.
