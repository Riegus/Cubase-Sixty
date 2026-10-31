# Cubase-Sixty

*Cubase × SSL UF8 / UC1 / UF1, without SSL 360°.*

Cubase-Sixty is an open-source port of [Rea-Sixty](https://github.com/acklin83/Rea-Sixty) to Steinberg Cubase. Rea-Sixty drives SSL's UF8, UC1 and UF1 controllers directly from REAPER over their vendor-USB protocols. Cubase-Sixty brings the same direct connection to Cubase.

> **Status: pre-alpha.** Nothing here controls hardware yet. The current work is proving the architecture: driving the SSL devices from a standalone helper while Cubase runs. Watch the repo, or open an issue if you own the hardware and want to help.

## How it works (planned)

Cubase has no public C++ control-surface SDK like REAPER's. Its scripting layer, the MIDI Remote API, is JavaScript and only talks MIDI. Cubase-Sixty therefore has two parts:

```
 SSL UF8 / UC1 / UF1
        │  vendor USB (protocol code from Rea-Sixty)
        ▼
 ┌──────────────────────┐
 │  Cubase-Sixty Bridge │  background app (C++)
 └──────────────────────┘
        │  virtual MIDI ports
        ▼
 ┌──────────────────────┐
 │  MIDI Remote script  │  runs inside Cubase (JavaScript)
 └──────────────────────┘
        │
        ▼
      Cubase
```

1. **Bridge** ([`cubase/bridge/`](../cubase/bridge/)) owns the USB connection to the controllers. It reuses Rea-Sixty's protocol and device code and translates between the SSL wire format and MIDI on virtual ports.
2. **MIDI Remote script** ([`cubase/midi-remote/`](../cubase/midi-remote/)) maps that MIDI to Cubase's mixer, transport and plug-in parameters.

The MIDI between the two is defined in [`cubase/docs/bridge-midi-protocol.md`](../cubase/docs/bridge-midi-protocol.md).

What the MIDI Remote API can reach in Cubase limits what the surfaces can do, so expect a smaller feature set than Rea-Sixty at first.

## Relationship to Rea-Sixty

This repository is a fork of Rea-Sixty.

- `main` tracks upstream unchanged.
- `cubase` (the default branch) holds all Cubase work, under `cubase/` and `.github/README.md`, so upstream fixes merge without conflicts.

The protocol decoding, captures and REAPER extension are Rea-Sixty's work. Its own README is still at [`/README.md`](../README.md), and its protocol notes are in [`/docs`](../docs/).

## Repo layout

```
extension/            Rea-Sixty's REAPER extension (upstream, untouched)
docs/                 Rea-Sixty's protocol notes (upstream)
cubase/
  bridge/             C++ bridge: SSL vendor USB ↔ virtual MIDI
  midi-remote/        Cubase MIDI Remote script
  docs/               Cubase-Sixty architecture and protocol notes
```

## Development setup (Windows)

```
git clone https://github.com/Riegus/Cubase-Sixty.git
cd Cubase-Sixty
git remote add upstream https://github.com/acklin83/Rea-Sixty.git
```

To have Cubase load the script straight from the repo, create the vendor folder and then a directory junction (admin Command Prompt):

```
mkdir "%USERPROFILE%\Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty"
mklink /J "%USERPROFILE%\Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty\SSL" "<path-to-repo>\cubase\midi-remote"
```

Build instructions for the bridge will follow once it does something.

## Contributing

UF8, UC1 and UF1 owners running Cubase are very welcome. Open an issue describing your setup. Protocol-level findings usually belong upstream in Rea-Sixty, so both projects benefit.

## Legal

### Trademarks

Not affiliated with, endorsed by, or sponsored by Solid State Logic Ltd or Steinberg Media Technologies GmbH. "SSL", "Solid State Logic", "SSL 360°", "UF8", "UC1" and "UF1" are trademarks of Solid State Logic. "Cubase" is a trademark of Steinberg Media Technologies GmbH. These names are used only to identify the hardware and software this project interoperates with.

### Interoperability and warranty

Cubase-Sixty relies on Rea-Sixty's protocol work, developed through passive observation of USB traffic between legally purchased SSL hardware and SSL 360° for interoperability. See Rea-Sixty's [`docs/interop-rationale.md`](../docs/interop-rationale.md).

This software sends vendor-USB frames that are not part of SSL's documented API. It is provided "as is", without warranty of any kind. **Third-party communication with SSL hardware may void your hardware warranty.**

### License

MIT. Rea-Sixty's code is covered by its [`LICENSE`](../LICENSE); the Cubase-Sixty additions under `cubase/` by [`cubase/LICENSE`](../cubase/LICENSE).
