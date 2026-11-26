# Cubase-Sixty 0.1.0 (beta)

The first public release of Cubase-Sixty: the SSL **UF8**, **UF1** and **UC1** in Cubase, without SSL 360. The mixer on
the faders, V-pots and displays, and the SSL plug-ins on your tracks under the knobs. It is a port of Rea-Sixty (the
same controllers in REAPER) to Cubase's MIDI Remote API. Any one of the three surfaces is enough; they also work
together.

This is a **beta**: it has been used daily on one studio's setup, but not yet on many others. Please report what
does not work (see "Reporting a problem" below).

## What you need

- Windows 10 or 11 (64 bit) and Cubase with MIDI Remote (developed and tested on Cubase 15 Pro; older versions are
  untested). Nuendo is not supported yet.
- Windows only for now; a Mac version is planned.
- An SSL UF8, UF1 or UC1, connected by USB.
- The teVirtualMIDI driver, which comes with [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html) (free)
  and with SSL 360. You do not create any ports yourself. If it is missing, the installer offers to open loopMIDI's
  download page. Nothing else needs installing (the C++ runtime comes with Cubase-Sixty).
- SSL 360 not running. The SSL plug-ins themselves work as always.

## Installing

1. **Close Cubase** (the installer asks you to if it is running).
2. Run `CubaseSixty-Setup-0.1.0.exe`. It is not signed, so Windows may say "Windows protected your PC": choose
   **More info**, then **Run anyway**. It installs for your user account only.
3. Leave **Install the USB driver for the SSL controllers** ticked if the installer ticks it (if the controllers do
   not come alive afterwards, unplug and replug them). This moves them from SSL's USB driver to Windows' WinUSB driver; SSL 360 no
   longer sees them until you give them back (tray menu, **SSL controllers > Give them back to SSL 360**, which also
   points Cubase's Mackie Control device back at SSL 360's port, after asking).
4. Leave **Set up Cubase for Cubase-Sixty** ticked to have Cubase's two settings made for you: it shows exactly
   what it would change in Cubase's preferences and asks first (a copy of the file is kept). Or make them later from
   the Settings page (**Setup**), or by hand as the user guide describes.
5. Start Cubase and open a project. The Settings page's **Setup** section shows whether anything is missing.

The user guide is installed with it (tray menu, **User guide**), and it lists everything the installer changes on
your PC.

## What it does

- **UF8:** eight strips that follow the MixConsole and the selected track, with track numbers, colours and the SSL
  plug-in on each track. **Track views** (Q3): ALL, AUDIO, INSTR, MIDI, GROUPS, FX, VCA or ARMED, with the start view
  as a setting. The faders sit on the UF8's printed dB scale. The top soft keys reach the SSL channel strip of all
  eight tracks at once, and two send views cover every send. FLIP, SSL Strip Mode, the CHANNEL encoder's modes
  (including the value under the mouse), Nav Mode (markers 1–15, cycle markers 1–9), automation and zoom.
- **UF1:** the selected track's strip, transport, the jog wheel's six modes, and four views: DAW (sends), SENDS,
  PLUGIN (SSL's own plug-in pages and EQ curve, or Quick Controls) and METER (the SSL Meter / Meter Pro, also in the
  Control Room).
- **UC1:** the selected track's SSL channel strip, or Cubase's own channel strip when it has none, with a bus
  compressor anywhere in the project in the middle. SSL's preset library, routing, favourites to swap strips by
  turning an encoder (the UC1's, or the UF8's CHANNEL encoder, with the list on the UF8's strips).
- **Settings page** in your browser (click the tray icon), and a tray app that starts with Windows and keeps the
  surfaces connected while Cubase comes and goes.

## Known limits

Cubase gives controllers less than REAPER does. Among other things, marker names, the global automation mode and
some plug-in states (A/B, HQ MODE) are out of reach, and a track's colour reaches the displays a little late. A
second UF8 is not supported yet.

On Windows 11 with the new Windows MIDI Services, quitting Cubase-Sixty from the tray leaves a copy that only a PC
restart ends. It holds nothing you need (a new copy starts and works), and updating and uninstalling still work: the
held files are put aside, and they are removed after the next restart. Moving to Windows' own loopback ports, which ends
this, is planned once Microsoft's API for them is part of Windows. The full lists:
[known issues](https://github.com/Riegus/Cubase-Sixty/blob/cubase/cubase/docs/known-issues.md),
[what Cubase does not allow](https://github.com/Riegus/Cubase-Sixty/blob/cubase/cubase/docs/cubase-limitations.md),
[what Rea-Sixty has that Cubase-Sixty does not](https://github.com/Riegus/Cubase-Sixty/blob/cubase/cubase/docs/missing-features.md).

## Reporting a problem

Open an issue on GitHub. Switch on the script log first (Settings > Logging), reproduce the problem, and attach the
files from the logs folder (tray menu, **Open logs folder**), with what you did and what you expected.

---

Cubase-Sixty is free, open-source software under the MIT licence, provided "as is" without warranty. It is not made,
endorsed or supported by Solid State Logic or Steinberg. SSL, UF8, UF1, UC1 and SSL 360 are trademarks of Solid State
Logic; Cubase is a trademark of Steinberg Media Technologies.
