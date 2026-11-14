# Cubase-Sixty user guide

Cubase-Sixty makes the SSL UF8, UF1 and UC1 work in Cubase the way [Rea-Sixty](https://github.com/Riegus/Cubase-Sixty#readme)
makes them work in REAPER: the mixer on faders, V-pots and displays, the SSL plug-ins of your tracks on the UC1's
knobs, without SSL 360. Any one of the three surfaces is enough; they work alone and together.

Tested with Cubase 15 Pro on Windows 11.

**Getting started**

- [What you need](#what-you-need)
- [Installing](#installing)
- [Setting up Cubase (once)](#setting-up-cubase-once)
- [Every day](#every-day)
- [How the surfaces think](#how-the-surfaces-think)

**Using the surfaces**

- [UF8](#uf8): [the strips](#the-strips), [moving around](#moving-around), [what SEL does](#what-sel-does-norm-rec-auto),
  [the top soft keys](#the-top-soft-keys-your-ssl-channel-strips), [sends](#sends-on-the-uf8), [FLIP and SSL Strip Mode](#flip-and-ssl-strip-mode),
  [the CHANNEL encoder](#the-channel-encoder), [markers](#markers-and-nav-mode), [automation](#automation), [zoom](#zoom-and-the-editor)
- [UF1](#uf1): [the strip](#the-strip), [getting around](#getting-around), [the four views](#the-four-views),
  [transport and time](#transport-and-time), [jog wheel](#the-jog-wheel-and-the-nav-cross), [quick keys](#quick-keys-and-shift)
- [UC1](#uc1): [SSL channel strips](#on-an-ssl-channel-strip), [Cubase's own strip](#on-cubases-own-channel-strip),
  [the Bus Comp section](#the-bus-comp-section), [the middle](#the-middle-screen-encoders-and-keys),
  [presets, routing, EXT FUNCS](#presets-routing-and-ext-funcs)
- [Favourites](#favourites)
- [Using them together](#using-them-together)

**Reference**

- [Settings](#settings)
- [Going back to SSL 360](#going-back-to-ssl-360)
- [When something is wrong](#when-something-is-wrong)
- [What is different from Rea-Sixty and SSL 360](#what-is-different-from-rea-sixty-and-ssl-360)
- [Uninstalling](#uninstalling)

---

Cubase-Sixty is free, open-source software, provided "as is" without warranty (see the licence). It is not made,
endorsed or supported by Solid State Logic or Steinberg. SSL, UF8, UF1, UC1 and SSL 360 are trademarks of Solid
State Logic; Cubase is a trademark of Steinberg Media Technologies.

## What you need

- Windows 10 or 11 (64 bit) and Cubase with MIDI Remote and Direct Access (developed and tested on Cubase 15 Pro;
  older versions are untested).
- An SSL UF8, UF1 or UC1, connected by USB.
- **A virtual MIDI driver** (teVirtualMIDI). It comes with [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html)
  (free) and with SSL 360. Cubase-Sixty makes its own MIDI ports with it; you do not create any ports in loopMIDI.
- **SSL 360 not running.** SSL 360 and Cubase-Sixty cannot use the controllers at the same time (see
  [Going back to SSL 360](#going-back-to-ssl-360)). The SSL plug-ins themselves work as always, and you do not need
  SSL 360 for them.

## Installing

1. Plug the controllers in and switch them on.
2. Run `CubaseSixty-Setup-<version>.exe`. Windows may show "Windows protected your PC", because the installer is not
   signed: choose **More info**, then **Run anyway**. It installs for your user account only.
3. On the options page:
    - **Start Cubase-Sixty with Windows**: leave it ticked unless you want to start it by hand.
    - **Install the USB driver for the SSL controllers**: ticked by itself when it is needed. It asks for
      administrator permission once. This moves the controllers from SSL's own USB driver to Windows' WinUSB driver,
      which Cubase-Sixty needs; from then on SSL 360 does not see them.
4. If the driver was installed: **unplug and replug the controllers** when the installer has finished.
5. If the installer says the virtual MIDI driver is missing, install loopMIDI and start Cubase-Sixty again.

Cubase-Sixty now sits in the notification area (the tray) as a small mixing-surface icon, and the controllers show
"Waiting for Cubase".

### What the installer changes on your PC

For your user account only, without administrator rights:

- The program in `%LOCALAPPDATA%\Programs\Cubase-Sixty`, with your settings, favourites and logs beside it.
- Four MIDI Remote scripts in `Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty`.
- With *Start with Windows* ticked: an entry in your user's Run key (`HKCU\...\CurrentVersion\Run`).
- While it runs, Cubase-Sixty makes its own virtual MIDI ports (through loopMIDI's or SSL 360's teVirtualMIDI
  driver) and closes SSL 360's background program `SSL360Core.exe` whenever it starts.

The uninstaller removes all of this, and asks before deleting your settings and favourites.

Only with *Install the USB driver* ticked (administrator permission, once):

- The SSL controllers move from SSL's USB driver to Windows' WinUSB driver; SSL 360 no longer sees them. Rea-Sixty
  uses the same driver.
- The driver package is signed on your PC with a certificate made there ("Rea-Sixty Driver Signer"), added to the
  computer's trusted root and trusted publisher certificates. Its private key stays in the computer's certificate
  store.

The uninstaller leaves the driver in place (Rea-Sixty may use it). To undo it, use the tray menu **USB driver >
Remove it** before uninstalling: that removes the driver and the certificate (see
[Going back to SSL 360](#going-back-to-ssl-360)). A recent Windows restore point or backup before installing the
driver is a sensible precaution.

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

- **Click the icon** for the Settings page (in your browser); **right-click** for the menu: Settings, User guide,
  Restart bridge, Open logs folder, Start with Windows, USB driver, Quit.
- Start order does not matter, and controllers can be plugged in and unplugged at any time.
- What the surfaces say when no project is open: **"No Project"** while Cubase shows the Hub, **"Waiting for
  Cubase"** while Cubase is not running (or has not connected yet), **"Bridge off"** (UF1: "Off") when Cubase-Sixty
  quits. The faders go to 0 dB. (Cubase-Sixty learns where 0 dB is from a track at 0.00 dB; until it has seen one,
  the faders stay where they are.)
- After **Restart bridge**, Cubase finds it again by itself within some seconds. If the controllers stay on "Waiting
  for Cubase" for long, restart Cubase.
- After Cubase's own *Reload Scripts*, a surface can behave oddly: restart Cubase.
- With *Sleep when idle* set (Settings > Devices), the surfaces go dark after that time without a touch while the
  transport is stopped. The first touch wakes them and does nothing else.

## How the surfaces think

A few ideas that make the rest easier:

- **The selected track** is the centre. The UF1 and the UC1 always work on it, and the UF8 moves its eight strips
  so that it is among them. Select a track anywhere (Project window, MixConsole, a surface) and all three follow.
- **The MixConsole decides what a track list is.** The UF8's strips, the track numbers on the displays and the
  channel encoders go by the MixConsole's channels in its order. A channel hidden in the MixConsole is skipped, and
  folder tracks (which have no channel) are skipped too. (Stepping can be set to follow the Project window instead.)
- **An SSL plug-in on a track is found by itself.** If a track has an SSL Channel Strip 2, a 4K B / E / G or a 360
  Link strip in its inserts, the UC1's knobs, the UF8's soft keys and the UF1's PLUGIN view are on it as soon as the
  track is selected. No learning, no assigning. On a track without one, the UC1 works Cubase's own channel strip.
- **The controller shows your hand first.** When you turn a knob, the ring and the value follow your hand at once
  and Cubase catches up; when you stop, the value Cubase ended on is shown. On fast turns the MixConsole on screen
  can trail the controller for a moment.

---

## UF8

Eight strips on a bank of the MixConsole's channels.

### The strips

| Control | What it does |
|---------|--------------|
| Fader | The track's volume, with touch: Touch and Latch automation write while your finger is on it. |
| Display | Track name and colour, the level in dB, a stereo meter, and a value line (pan, or what the V-pot is on). |
| V-pot | Pan, with a position bar on the display. **Push: centres the pan** (Settings > Behaviour can make it open the track's channel editor instead). |
| SOLO, CUT | Solo and mute. |
| SEL | Selects the track. **On the track that is already selected (SEL lit): opens / closes its first SSL channel strip, or the channel editor on a track without one** (Settings > Behaviour can make it always the channel editor). (See [What SEL does](#what-sel-does-norm-rec-auto) for the other two modes.) |
| Top soft key | A control of the track's SSL channel strip (see [The top soft keys](#the-top-soft-keys-your-ssl-channel-strips)). |

Touching a fader can also select its track (Settings > Behaviour).

### Moving around

- **BANK ◂ ▸** moves the strips by eight tracks. Bank right stops when the last track is on strip 8, so you never
  see a half-empty bank at the end.
- **PAGE ◂ ▸** moves by one track.
- **The UF8 follows the selection.** Select a track that is not on the strips, with the mouse or from another
  surface, and the UF8 banks to it. A far jump takes a moment (Cubase can only bank step by step); the UF8 waits and
  shows the result in one go. At start-up it goes to the selected track too.
- **CHANNEL** is "home": faders on volume, V-pots on pan, whatever view you were in.
- **PAN** puts the V-pots back on pan (out of a soft-key parameter, the V-pot send view or FLIP). It is lit while
  they are on pan.
- **FINE**: while held, the V-pots move a quarter as far. Press it twice quickly to latch it (lit); a press
  unlatches.
- **360°** opens the Settings page in your browser.

### What SEL does: NORM, REC, AUTO

The NORM, REC and AUTO keys choose what the SEL keys do. The mode's key is lit.

| Mode | SEL does | SEL lights |
|------|----------|-----------|
| **NORM** | Selects the track; on the selected track, opens / closes its SSL channel strip or channel editor. | The selected track. |
| **REC** | Arms / disarms the track for recording. | Dim red: can be armed. Red: armed. Dark: this strip cannot record (group, FX, VCA, output). |
| **AUTO** | Switches the track's automation **Write** on / off. | Red while the track is writing. |

### The top soft keys: your SSL channel strips

The row of keys above the displays reaches the **SSL channel strip plug-in on each track** (Channel Strip 2, 4K B /
E / G, 360 Link). Each strip's display shows what its key is.

1. **Pick a page** with the **V-POT** and **1–5** keys. The page's key is lit.

   | Page | The eight keys |
   |------|----------------|
   | V-POT | BYPASS, IN TRIM, PHASE, PRE, MIC/DRV, –, IMP IN, IMP |
   | 1 | WIDTH, –, –, A/B, HPF, LPF, EQ, EQ TYPE |
   | 2 | LF FREQ, LF GAIN, LF TYPE, –, LMF FREQ, LMF GAIN, LMF Q, – |
   | 3 | HMF FREQ, HMF GAIN, HMF Q, –, –, HF FREQ, HF GAIN, HF TYPE |
   | 4 | DYNAMICS, COMP MIX, COMP RATIO, COMP THR, COMP REL, COMP F.ATK, PEAK/RMS, – |
   | 5 | GATE REL, GATE THR, GATE RANGE, GATE HOLD, GATE F.ATK, GATE/EXP, HQ MODE, OUT TRIM |

2. **Press a key** (say HMF GAIN). That control is now on **all eight V-pots**, one per track: each V-pot turns the
   HMF gain of *its own* track's SSL channel strip, and each display shows that track's value. The key is lit.
   A switch (EQ in / out, PHASE …) is toggled by turning or pushing; a push puts a gain back to the centre.
3. **Press the same key again** to give the V-pots back to pan.

This is how you set the same control across a group of tracks without opening a single plug-in window: bank to the
drums, press COMP THR, and you have eight compressor thresholds under your fingers.

A strip whose track has no SSL channel strip shows "-" and its V-pot does nothing while a key is active. A/B and
HQ MODE are shown but cannot be reached from Cubase.

**Q1 / Q2**: Q2 puts the **bus compressor's** controls on the soft keys instead (page V-POT: THR,
ATTACK, RELEASE, RATIO, S/C HPF, MIX, EXT S/C, BUS COMP; page 1: OUTGAIN), for tracks that have an SSL Bus Compressor 2.
Q1 goes back to the channel strip.

### Sends on the UF8

Two views, both left with the same key that entered them (or with CHANNEL):

- **One send across all tracks, on the faders.** Press one of the eight **SEND / PLUGIN** keys: the
  faders are now send N of each track. Use it to build a headphone mix or to ride a reverb send across a bank.
  The displays keep the track names and show the send level in dB; **CUT switches the send on / off**. A track that
  does not have that send has its fader at the bottom and no value. The V-pots stay on pan.
- **All sends of the selected track, on the V-pots.** **Hold FLIP** for about half a second (it lights): the eight
  V-pots are sends 1–8 of the selected track, the displays show where each goes and its level. **Push a V-pot** to
  switch that send on / off. The faders stay on volume. Select another track and the V-pots show its sends.

### FLIP and SSL Strip Mode

- **FLIP** (a tap) swaps faders and V-pots: pan on the faders, volume on the V-pots. Tap again to swap back.
- **PLUGIN** is **SSL Strip Mode**: each fader moves the *fader inside the track's SSL channel strip*
  (its output level) instead of the track's volume, and the V-pot its pan. Use it when you mix "inside" the SSL
  strips and want the track faders left alone. Tracks without an SSL strip keep volume and pan. Press PLUGIN again
  to leave.

### The CHANNEL encoder

The CHANNEL encoder has several jobs. **Hold its push and turn** to choose: the displays list the jobs, and
letting go keeps the one shown.

| Job | A turn does |
|-----|-------------|
| Channel Select (the start job) | Selects the previous / next track. |
| Nudge | Moves the project cursor by Cubase's nudge value. The **NUDGE** key switches to this and back. |
| Markers | Jumps to the previous / next marker. The **NAV** key (a tap) switches to this and back. |
| Bank by 1 | Moves the strips one track at a time. |
| CS Fav / BC Fav | Steps through your favourite channel strips / bus comps (see [Favourites](#favourites)). |
| Mouse | Changes **the value under Cubase's mouse pointer**: point at any knob or fader in Cubase (a plug-in, the MixConsole) and turn. The **FOCUS** key switches to this and back. With FINE it moves a quarter as far. |

### Markers and Nav Mode

- **Tap NAV**: the CHANNEL encoder steps through markers (NAV lit). Tap again for tracks.
- **Hold NAV for about 0.7 seconds**: **Nav Mode**. The eight top soft keys now jump straight to **markers 1–8**; the last
  one pressed stays lit. With **Q1** they recall **cycle markers 1–8** instead (the cursor goes to the start of the
  cycle); **Q2** is back to markers. Hold NAV again to leave. Cubase does not tell controllers the markers' names,
  so the keys go by number.

### Automation

- **READ** and **WRITE** switch Read / Write on the **selected track**.
- **OFF, TRIM, LATCH, TOUCH** choose Cubase's global automation mode (OFF is Cross-Over; TRIM is a switch that
  combines with the others). Cubase does not tell controllers which mode is active, so the lit key is the last one
  you pressed on a surface; a mode changed with the mouse is not shown.
- To write automation on several tracks: AUTO mode (above), press SEL on each track to arm Write, then ride the faders.

### Zoom and the editor

The cursor pad zooms: ◂ ▸ horizontally, ▴ ▾ vertically. The centre key opens / closes the editor of
the selected event, like a double-click.

**FOOT 1 / 2**: foot switches play and record.

---

## UF1

One strip for the **selected track**, a large display with four V-pots, transport and a jog wheel.

### The strip

| Control | What it does |
|---------|--------------|
| Fader | The selected track's volume, with touch. |
| SOLO, CUT | Solo and mute. SEL shows the track's colour; bright while its channel editor is open. |
| Knob above the fader | Pan. |
| Small display | Track name, number (as in the MixConsole) and colour; the level; a meter; and a **value line**: pan, or for two seconds what a V-pot is changing. |
| MASTER | Puts the strip on the main output instead of the selected track. Press again to return. |
| FLIP | Swaps the fader and the pan knob. |
| BYPASS | Bypasses all inserts of the track (dim: inserts active; bright: all bypassed; off: no inserts). Press again to switch them back on; inserts that were bypassed beforehand stay bypassed (Settings > Behaviour can make it switch all on). |

### Getting around

- **CHANNEL encoder** and **ARROW ◂ ▸**: previous / next track. Folder tracks and tracks hidden in the MixConsole
  are skipped (Settings > Behaviour changes this).
- **Encoder push**: opens / closes a window: in the PLUGIN view the plug-in's window, otherwise the channel editor.
- **SHIFT + turn**: the next / previous SSL plug-in on the track, for the PLUGIN view (after the last one: Quick
  Controls).
- **BANK ◂ ▸**: banks the UF8. Without a UF8 it moves the selection by eight tracks.
- **SHIFT + ARROW**: the cursor one bar back / forward.

### The four views

**Hold MODE**: the four keys under the big display show PLUGIN, DAW, METER and SENDS. Press one and let go. Which
view the UF1 starts in is a setting.

**DAW** — the everyday view. The V-pots are the selected track's **sends 1–4**: turn for the level, push to switch
the send on / off. The **5–8** key puts them on sends 5–8. The small display shows the send and its level while you
turn.

**SENDS** — the same four sends, but the big display shows all four at all times: where each goes, its level, and a
bar. They follow the mouse and automation too.

**PLUGIN** — the track's SSL plug-in, laid out as on SSL's own UF1 pages.

- The header shows the plug-in and the page ("2/8"). **◀ ▶** (the two outer keys under the display) turn the pages.
- Each page puts four of the plug-in's controls on the **V-pots** and four of its switches on the **display soft
  keys** (lit while on). A V-pot push puts a gain or trim back to the centre.
- The graph shows the **EQ curve** of the channel strip (flat while its EQ is out).
- On a track without an SSL channel strip but with a Bus Compressor 2, the pages are the compressor's.
- On a track **without any SSL plug-in**, the V-pots are Cubase's **Quick Controls 1–4** (5–8 with the 5–8 key): of
  the plug-in whose window has the focus, or of the track itself. Only Quick Controls that have a parameter assigned
  do anything (assign them in the plug-in window or the track's Inspector); an empty one shows `QC1 -`. The graph
  then shows Cubase's own channel EQ.
- **SHIFT + 5–8** opens the **preset browser** for the SSL plug-in: SSL's preset library. V-pot 4 scrolls, its push
  loads the preset; soft key 3 goes back.

**METER** — the big display becomes the SSL **Meter / Meter Pro** plug-in of the selected track. **Soft key 1**
steps through its screens; its label shows the one you are on:

- **OVERVIEW**: goniometer, peak and RMS bars with holds, and the numbers.
- **ANALOGUE**: two VU needles with their max needles, and the VU numbers.
- **RTA**: the 31-band spectrum with holds.
- **LOUDNESS** (Meter Pro only): the loudness history and the Integrated, Short-Term, True Peak and Short-Term Max
  readouts.

Soft key 2 (RESET) resets the holds (on LOUDNESS, the loudness measurement too). Put a Meter on the Stereo Out and
it shows whatever you select; "NO METER" means no Meter plug-in was found. The screens use the Meter's default
settings (VU, -18 dBFS reference, RTA scale 0 to -120 dB, loudness range -18 to +9 LU); changing them in the plug-in
does not change the UF1's drawing yet.

### Transport and time

- **Transport keys**: rewind, forward, stop, play, record, **cycle** and **click**, each with its LED.
- The **time display** shows the project position in Cubase's primary format. **360°** swaps it with the secondary
  format (bars and beats ↔ time, as set in Cubase's transport).
- **SCRUB** (a tap) switches scrubbing on; the jog then scrubs audio. This needs the Mackie Control device from the
  setup. **SHIFT + SCRUB** is **shuttle**: each jog step changes the play speed (1/8× … 8×, and backwards); leaving
  stops playback. The SCRUB LED is white for scrub, orange for shuttle.

### The jog wheel and the NAV cross

The jog wheel and the cross beside it work as a pair, and what they act on is the **jog mode**. **Hold SCRUB and turn
the jog** to pick it:

| Mode | The jog | The cross |
|------|---------|-----------|
| **Scrub** | Scrubs audio (the same as a tap on SCRUB). | Zoom. |
| **Playhead** (the start mode) | Moves the cursor by Cubase's nudge value (the Project window's nudge setting). | Zoom: ◂ ▸ horizontal, ▴ ▾ vertical. Centre: opens / closes the editor. |
| **Items** | Moves the selected events. | ◂ ▸ previous / next event, ▴ ▾ the track above / below (SHIFT adds to the selection). Centre: zoom to the selection and back. |
| **Envelope** | Moves the selected automation points in time. | ◂ ▸ previous / next point (SHIFT adds), ▴ ▾ another lane. Centre: the jog on the play cursor and back. |
| **Range** | Moves what is aimed at. | ◂ ▸ aim at the left / right edge of the range selection, centre at the whole range. |
| **Fades** | Makes the aimed fade longer / shorter. | ◂ ▸ aim at the fade-in / fade-out of the selected events. Centre: the cross walks the events instead, and back. |

**SHIFT + jog** steps through markers in any mode.

### Quick keys and SHIFT

- **Key 1: Solo Clear** — clears every solo in the project. It is lit, and the display says SOLO ACTIVE, while
  anything is soloed.
- **Key 2: FINE** — the V-pots move a quarter as far per click (sends, Quick Controls, plug-in controls).
- **SHIFT + a key** is automation: **◀ Read** and **▶ Write** for the selected track; **Cycle** Cross-Over,
  **Click** Trim, **key 1** Latch and **key 2** Touch as the global mode.
- **SSL Strip Mode**: on the PLUGIN view's first page, soft key 4 (PLUG-IN) puts the fader on the SSL channel
  strip's own fader and the pan knob on its pan, as PLUGIN does on the UF8.
- **FOOT 1 / 2**: foot switches.

---

## UC1

The UC1 is your channel strip and bus compressor in hardware. The left and right thirds work the **channel strip of
the selected track**, the middle top a **bus compressor**, and the middle bottom gets you around.

### On an SSL channel strip

Select a track with an SSL Channel Strip 2, 4K B, 4K E, 4K G or 360 Link strip, and every knob and key on the UC1 is
that plug-in's: filters, the four EQ bands with their bell / type / IN keys, the compressor and gate with F.ATK,
PEAK, DYN IN and EXP, S/C LISTEN, input and output trim, polarity.

- The **rings and LEDs** show the plug-in's settings, and follow when you use the mouse or automation plays.
- The **Dyn and Gate LED rows** show the strip's gain reduction as it happens.
- The **screen** shows the track's name and colour, the plug-in (the model field), and the name and value of what
  you are turning.
- **CHANNEL IN** is the plug-in's own bypass. **FINE** makes the knobs finer.
- **Several SSL strips on one track?** The **magnifier** key steps to the next one (in insert order, then Cubase's
  own strip); the name row shows previous / current / next for a moment. The choice is remembered per track.

### On Cubase's own channel strip

A track without an SSL strip is not a dead end: the same knobs work **Cubase's channel strip**, so every track in
the project has an EQ, a gate and a compressor under your hands.

| UC1 section | Works |
|-------------|-------|
| Input trim, HPF, LPF | Cubase's Pre section: gain, low cut, high cut. A cut filter switches on when you turn it and off at the end of its range. |
| The four EQ bands | Cubase's channel EQ, bands 1–4: gain, frequency, Q. Turning a band's knob switches the band on. The **bell keys** switch the outer bands between shelf and bell. **EQ IN** is the EQ's bypass. |
| Compressor | The strip's compressor module: threshold, ratio, release. **DYN IN** is its bypass. With the Tube or Vintage Compressor, the Ratio knob is Input Gain and the Threshold knob Output Gain (those have no threshold or ratio). |
| Gate | The strip's Noise Gate: threshold, range, release, hold. **EXP** is its bypass. |
| F.ATK (either one) | Switches that section's **Release knob to Attack** (lit) and back; the UC1 has no attack knobs. The choice stays as you change tracks. |

**Loading a module by reaching for it.** If the track's strip has no gate or compressor yet:

- **EXP**, or turning a Gate knob, loads the **Noise Gate**.
- A short press on **DYN IN** loads a compressor (the Standard Compressor, unless Settings says otherwise).
- Turning a Dyn knob, or **holding DYN IN**, opens the **compressor picker** on the screen: turn a Dyn knob to choose Standard,
  Tube or Vintage, press DYN IN (or CONFIRM) to load it, BACK to cancel. Holding DYN IN on a track that has a
  compressor swaps it for another type.
- A module that is loaded but switched off is switched on by its knobs.

Settings > *UC1 on Cubase's strip* decides whether knobs, only keys, or nothing loads modules, and which compressor
DYN IN loads.

### The Bus Comp section

The seven knobs and **BUS COMP IN** in the middle work an SSL **Bus Compressor 2** (or 360 Link Bus Compressor)
**anywhere in the project** — not necessarily on the selected track. Typically that is the one on your mix bus, and
it stays there while you select and work on other tracks. The needle meter shows its gain reduction.

- **Choosing which one**: turn the **secondary encoder** (right of the screen; this is its start job). It steps
  through every bus compressor in the project; the large name row shows the track it is on, with its neighbours.
- Until you pick one, the section is on the bus comp of an output channel if there is one, else the first in the
  project (Settings can make it the selected track's).

### The middle: screen, encoders and keys

| Control | What it does |
|---------|--------------|
| **CHANNEL encoder** (left of the screen) | Previous / next track. |
| **Secondary encoder** (right of the screen) | Its job is shown when you **hold its push and turn**: the Bus Comp section through the project's bus comps (start job), the track's channel strips (like the magnifier), CS favourites, BC favourites. A **short push** opens / closes the window of the plug-in it is on (a bus comp's only when it is on the selected track). |
| SOLO, CUT, Ø | The selected track's solo, mute and polarity. **Solo Clear** clears every solo. |
| Magnifier | The next channel strip on the track. |
| 360° | Opens / closes the channel strip's plug-in window. |
| 7-segment display | The track's number in the MixConsole. |

What the magnifier, 360° and the secondary encoder's push do can be changed (Settings > UC1 keys): next / previous
strip, the plug-in window, Channel Settings, the MixConsole, bypass, or nothing.

### Presets, routing and EXT FUNCS

- **PRESETS** opens SSL's preset library (the same presets SSL 360 uses). First choose CHANNEL STRIP or BUS COMP (the
  secondary encoder flips, CONFIRM enters), then folders and presets: the encoder moves, **CONFIRM** opens a folder or
  loads the preset, **BACK** goes up and out. The plug-in's controls take the preset's values; the preset *name* in
  the plug-in's own window does not change (Cubase lets a controller set controls, not the plug-in's saved state).
- **ROUTING** changes the order of the SSL strip's sections (filters, EQ, dynamics). The secondary encoder steps
  through the ten orders and the screen draws each; BACK, CONFIRM or ROUTING closes.
- **BACK on the main screen: EXT FUNCS** — the channel strip's controls that have no knob of their own: Mix, Pan,
  Width, Output Trim, Auto Makeup and others, by model. The encoder scrolls the list, its push switches to adjusting
  the one shown, BACK or CONFIRM closes.

---

## Favourites

Up to eight channel strips and eight bus compressors that you swap between by turning an encoder: compare a 4K E
against a 4K G on a vocal without opening a menu.

- Choose them in **Settings > Favourites**.
- **Step through them** with the UF8's CHANNEL encoder in *CS Fav* / *BC Fav* mode, or the UC1's secondary encoder
  in its favourites jobs. The UC1's name row shows previous / current / next.
- A moment after you stop, the plug-in on the track is **replaced** by the favourite shown, and the settings the two
  have in common are carried across by value (an HMF at 2.5 kHz stays at 2.5 kHz). On a track without one, the
  favourite goes into the first free insert slot.
- CS swaps the strip the UC1 is on; BC swaps the Bus Comp section's compressor. This needs a UC1.

## Using them together

- **Selection ties everything together.** SEL on the UF8, the channel encoder on any surface, or a mouse click: the
  UF1's strip, the UC1's channel strip and the UF8's bank all go to that track.
- **UF8 + UC1**: bank and balance on the UF8, press SEL, and the track's channel strip is on the UC1. The UF8's top
  soft keys spread one strip control across eight tracks; the UC1 gives you one track's whole strip.
- **UF1 + UF8**: the UF1's BANK keys bank the UF8, so the right hand never leaves the transport.
- **UF1 + UC1**: the UF1's SHIFT + encoder and the UC1's magnifier pick the same channel strip, so the PLUGIN view
  and the UC1 always show the same plug-in.
- **UF8 alone**: every send, the SSL strips through the soft keys, markers and automation are all reachable; that
  is what the send views, Nav Mode and the AUTO mode are for.

---

## Settings

Click the tray icon. The page opens in your browser; it is served by Cubase-Sixty on this computer only. A change
is saved and takes effect at once. The **search box** above the sections finds a setting in any section (press
**/** to jump to it, Escape to clear). This guide has one too, in the HTML version. On **Behaviour**, the
UF8 / UF1 / UC1 buttons pick which surfaces' settings show (one or more; remembered in your browser).

| Section | What is there |
|---------|---------------|
| Devices | Which surfaces are connected and whether Cubase is; key / LED and display brightness; sleep when idle. |
| Behaviour | How the channel encoders step through tracks (as the MixConsole shows them, as the Project window does, or every track); touching a UF8 fader selects its track; what a UF8 V-pot push and SEL on the selected track do; what the UF1's BYPASS key does with inserts that were bypassed already; the UF1's view at start; the UC1 secondary encoder's job at start; the UC1 knob step. |
| UC1 keys | What the magnifier, 360° and secondary encoder push do. |
| UC1 on Cubase's strip | Whether the knobs / keys load a missing gate or compressor; which compressor DYN IN loads; which bus comp the Bus Comp section starts on. |
| Display | How long track names are shortened; the UC1's 7-segment leading zeros. |
| Favourites | Eight channel strips and eight bus comps. |
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
compete for the SSL plug-ins' meters.

## When something is wrong

| What you see | What to do |
|--------------|-----------|
| The controllers stay dark, the icon is yellow or green | The USB driver: tray menu **USB driver > Install**, with the controllers plugged in, then replug them. Is SSL 360 running? Quit it. |
| "Waiting for Cubase" although a project is open | Restart Cubase with Cubase-Sixty running. In *Studio > MIDI Remote Manager* the four CubaseSixty devices should be listed and active. |
| Red icon | Right-click > **Start bridge**. If it will not stay up: the virtual MIDI driver may be missing (install loopMIDI), or ports with the names `Cubase-Sixty ...` exist in loopMIDI (delete them there). |
| An instrument plays when you touch a control | "In 'All MIDI Inputs'" is ticked for a Cubase-Sixty port (see setup). |
| SCRUB only nudges | The Mackie Control device is missing (see setup). |
| SCRUB moves the cursor but there is no sound | Cubase does not scrub audio played by an ARA extension such as SpectraLayers (Cubase's own Scrub tool is silent there too). Scrub over events without the extension, or render the extension into the event. |
| The UC1's knobs do nothing on a track | The track has neither an SSL strip nor Cubase's strip reachable (an instrument return, for example), or loading modules is set to "never". |
| A top soft key on the UF8 shows "-" | That track has no SSL channel strip. |
| A surface is odd after *Reload Scripts* | Restart Cubase; a reload does not always set the scripts up fully. |
| Stepping to a track lands somewhere else | "Project window" stepping needs the Project window focused; the default (MixConsole) does not. |

For a report: switch on the script log (Settings > Logging), reproduce the problem, and send the files from the logs
folder (tray menu **Open logs folder**) with what you did and what you expected.

## What is different from Rea-Sixty and SSL 360

Cubase gives controllers less than REAPER does, so some things are missing or different:

- A track's **colour** reaches the displays when the track is next selected or banked in, not the moment you change it.
- The **automation mode** keys light the last mode pressed on a surface; Cubase does not report the mode.
- **Markers** are reached by number; their names are not shown.
- On fast turns of a **send**, the MixConsole trails the controller for a moment and then lands on the same value.
- A **preset** loaded from a surface sets the plug-in's controls, not the preset name in its window.
- The soft keys' **A/B** and **HQ MODE**, layers, user soft-key banks and a second UF8 are not there. On the UF8,
  **Q3** and **LAYER 1–3** do nothing.
- In the METER view the V-pots, FINE, PRESETS and the Loudness PLAY key do nothing yet, and the screens are drawn
  with the Meter's default settings.

The full lists: [known issues](https://github.com/Riegus/Cubase-Sixty/blob/cubase/cubase/docs/known-issues.md),
[what Cubase does not allow](https://github.com/Riegus/Cubase-Sixty/blob/cubase/cubase/docs/cubase-limitations.md),
[what Rea-Sixty has that Cubase-Sixty does not](https://github.com/Riegus/Cubase-Sixty/blob/cubase/cubase/docs/missing-features.md).

## Uninstalling

Windows *Settings > Apps > Installed apps > Cubase-Sixty > Uninstall*. It stops Cubase-Sixty, removes the program
and the scripts, and asks whether to remove your settings and favourites too.

The USB driver and its certificate stay (Rea-Sixty may be using them). To give the controllers back to SSL 360 and
remove the certificate, use the tray menu **USB driver > Remove it** **before** uninstalling, or run SSL 360's
installer afterwards (that does not remove the certificate).
