# Davidian Duo '57

A circuit-modelled guitar amplifier plugin (LV2), solved stage by stage from the service diagram of a classic 1957 tweed 2x12 combo: four input jacks on Normal and Bright channels, a treble/bass tone stack, presence, a 6L6GC push-pull power amp with rectifier sag, and five speaker cabinet models.

**A close model, not an exact copy.** It is a simulation of the circuit from its component values, not a sampled or measured amp, and some details are reasoned estimates (listed under [Estimates](#estimates)). It has not been compared side by side with a built unit, so expect it to sound close to the hardware rather than identical.

![The GUI](docs/gui.png)

A mono amp with no latency (mono or stereo tracks work too), with the same GUI on every system. GPL-3.0-or-later.

| Format | Windows | macOS | Linux |
|---|---|---|---|
| LV2 | yes | yes | yes |

## Download

Ready-made builds are on the [Releases page](https://github.com/evadavevadave/davidian-duo-57/releases). Each zip holds one `duo57.lv2` folder with the plugin, its GUI, the two .ttl files and the licences:

| Download | Contains | For |
|---|---|---|
| `duo57-windows-x64.zip` | LV2 (Win32 GUI) | Windows 10/11, 64-bit hosts |
| `duo57-macos-universal.zip` | LV2 (Cocoa GUI) | macOS 10.13 or later, Apple silicon and Intel |
| `duo57-linux-x86_64.zip` | LV2 (X11 GUI) | 64-bit Intel/AMD Linux |

Unzip, copy the plugin folder into your LV2 folder, and restart your host (or rescan plugins):

| Format | Copy | Into |
|---|---|---|
| LV2 (Windows) | `duo57.lv2` | `%APPDATA%\LV2` (create it if it doesn't exist) |
| LV2 (macOS) | `duo57.lv2` | `~/Library/Audio/Plug-Ins/LV2` |
| LV2 (Linux) | `duo57.lv2` | `~/.lv2` |

In the host it is listed as **Davidian Duo '57**. Replace any older copy instead of keeping two: when two bundles have the same URI, the loader uses one and ignores the other. If your host doesn't scan that folder, add it to the host's LV2 search path or set the `LV2_PATH` environment variable. The LV2 URI is `urn:davidian:duo-57`.

The macOS build isn't notarized, so macOS may block it after download. Run this once in Terminal after copying:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/LV2/duo57.lv2
```

## Controls

| Panel | Control | Circuit part |
|---|---|---|
| Inputs | Bright 1, Bright 2, Normal 1, Normal 2, Y Cable | The four input jacks; click one to plug into it, and it lights amber. Input 1 is about 6 dB hotter than Input 2. Y Cable feeds Normal 1 and Bright 1 together. Default Bright 1 |
| Volume | Bright, Normal | R19 / R18 1M. Bright has C4 100 pF across the pot. The unused channel's volume still loads the mixer, as on the real amp. Default 6 |
| Tone | Treble | R26 1M "A", with C7 250p and C8 47p. Default 7 |
| Tone | Bass | R30 1M "A", with C9 .01, C10 .0047 and R27/R28/R29 220k. Default 5 |
| Tone | Presence | R31 5k + C11 .1µF on the V4-A cathode. Turning it up lifts the treble and reduces treble in the negative feedback. Default 6 |
| Studio | Cabinet | Not on the amp: Off, 2x12 Open, 1x12 Open, 4x10 Open, 2x12 Closed or 4x12 Closed (see [Cabinets](#cabinets)). Off leaves the speaker-terminal signal for your own impulse response. Default 2x12 Open |
| Studio | Input | Input trim, -24 to +24 dB: 0 dBFS at the input counts as 1 V peak at the guitar jack |
| Studio | Output | Not on the amp: output level, -60 to +12 dB. It comes after the circuit and doesn't change the tone |

Volume, Treble, Bass and Presence carry a 1–12 scale, as on a tweed amp. The red pilot jewel is decoration and always lit. The readout at the top right shows the control you last touched and its value.

Drag a knob up or down, hold Shift to fine-tune, use the mouse wheel, and double-click to reset. Click a cabinet row to choose it, or use the mouse wheel over the list.

### Cabinets

| Cabinet | Character |
|---|---|
| 2x12 Open | The amp's own speakers: open back, alnico magnets. Loose bass, a gentle mid scoop and a soft top. The default |
| 1x12 Open | A single 12" in an open-back cab. Less low end and more forward upper mids |
| 4x10 Open | Four 10" in an open-back cab. Deep but loose bass with a softer, earlier top roll-off |
| 2x12 Closed | The same two 12" in a sealed box. Tighter bass with a box thump around 100 Hz |
| 4x12 Closed | A sealed 4x12 with ceramic speakers. The biggest low end, a deeper mid scoop and a bright break-up peak |

Each cabinet is level-matched to the stock 2x12 over 100 Hz–5 kHz, so switching changes the tone rather than the volume; `render` checks they stay within 3 dB (they measure within 0.6 dB). Switching crossfades over 20 ms, so it doesn't click. The Cabinet port's values 0 and 1 mean the same as the old on/off switch, so sessions saved with it load unchanged.

## How it is modelled

All in `src/duo57.c`, with the service diagram's part values.

- **Input section:** four 12AX7 triodes (V1-A/B Normal, V2-A/B Bright). They share R9 470R ‖ C1 220µF, use 100k plate loads, and each pair is mixed through 100k resistors.
- **Volume and mixer:** the volume pots are modelled as Thevenin sources into the R20/R21 270k mixer, with the V3-A grid's input (Miller) capacitance.
- **V3 stages:** V3-A and the V3-B cathode follower are DC-coupled, so they are solved as one table. This includes grid conduction of the cathode follower through R23, and the R22 10M shunt-feedback loop, solved implicitly every sample.
- **Tone stack:** the full RC network, including the V4-A input capacitance, solved by nodal analysis with trapezoidal capacitor models. It is not a fitted curve.
- **V4-A and feedback:** V4-A has an unbypassed cathode. The R32 56k negative feedback from the speaker enters its cathode, and presence shapes both the stage gain and the feedback.
- **Phase inverter:** a cathodyne (V4-B), with swing limits.
- **Power amp:** 6L6GC push-pull, biased at −50.2 V for 33.3 mA per tube (note 7 on the schematic), with output-transformer bandwidth and plate saturation. The feedback loop is solved implicitly every sample.
- **Blocking:** power-tube grid current charges C13/C14, which shifts the bias the way a cranked tweed amp does.
- **Rectifier sag:** 5U4 rectifiers, choke and 22µF reservoir.
- **Triodes:** every 12AX7 stage uses the Koren equations, solved on that stage's AC load line into a lookup table when the plugin loads.
- **Oversampling:** everything runs at 4x with 12th-order anti-alias filters. The plugin adds no latency.

### Checked against the schematic

Note 6 of the schematic gives AC test-point voltages for 10 mV at 1 kHz into J3 (Bright 1), with all controls at 50%. The schematic allows ±20%. `render` prints the model's value at each point.

| Point | Model | Schematic | Difference |
|---|---|---|---|
| TP14 (C3 node) | 554 mV | 488 mV | +14% |
| TP15 (V3-A grid) | 60 mV | 50 mV | +20% |
| TP16 (cathode follower) | 3.09 V | 2.51 V | +23% |
| TP17 (V4-A grid) | 371 mV | 261 mV | +42% |
| TP23 (speaker, 4 Ω) | 3.76 V | 2.6 V | +45% |
| Power at full clipping | 36 W | 30 W RMS at 5% THD | — |

The input stages are within the schematic's tolerance. From TP16 on the model runs hot: TP17 and TP23 are 3 dB above the schematic. The power figures aren't like for like: the model's 36 W is at full clipping, not at 5% THD.

### Estimates

The schematic doesn't settle these, so they are reasoned estimates:

- **Jack switching:** the scan's jack drawing alone suggests that Input 1 drives only one triode. But a single 12AX7 cannot reach the TP14 gain of 48.8× with this load. With both triodes driven, the model gives about 55×. So Input 1 is modelled as driving both triodes, and Input 2 as driving one, with the other grid grounded.
- **Volume taper:** the volume pots are marked "SPL. D", which doesn't say what law they follow. A linear law reproduces the TP14 → TP15 ratio at 50% rotation, so that is what is used. Treble and bass use a standard audio taper.
- **Power-amp constants:** the 6L6GC curve, output-transformer saturation and sag depth.
- **Cabinet filters:** all five are shaped by ear from the usual character of each cabinet type, not from measured speakers or impulse responses. Their curves are in the `CABS` table in `src/duo57.c`.

### Simplifications

These parts are behavioural models, not a component-level circuit solution:

- the cathodyne's swing limits
- the 6L6GC, which uses a 3/2-power law
- output-transformer saturation
- rectifier sag
- the cabinet filters, which are six-band EQ curves rather than speaker models: they don't model cone break-up distortion, mic position or room

Power-supply ripple isn't modelled, and the preamp supply doesn't sag with the power amp.

## Build

The code is plain C (Objective-C for the macOS window layer) with no dependencies beyond the platform's own libraries: Win32 on Windows, Cocoa on macOS, Xlib on Linux. No LV2 SDK is needed; build with `-DHAVE_LV2` to use the system headers instead.

| Source | What it is |
|---|---|
| `src/duo57.c` | The amp model, cabinet models and LV2 entry points |
| `src/ports.h` | Port numbers, cabinet numbers and URIs shared by the plugin and the GUI |
| `src/ui/ui_lv2.c` | LV2 GUI entry points and the Win32 and X11 window layers |
| `src/ui/window_cocoa.h` | The macOS (Cocoa) window layer |
| `src/ui/panel.h` | The panel: layout, drawing and mouse handling, with no platform code |
| `src/ui/style.h`, `canvas.h`, `font.h` | Davidian GUI house style: palette, software renderer and stroke font, copied unchanged from the Bi-Lumen |
| `src/lv2_core.h` | The LV2 C interface, so no SDK is needed |
| `duo57.lv2/` | LV2 metadata. The build fills in the library extension and GUI type in `manifest.ttl.in` |

### Windows (Visual Studio)

Needs Visual Studio with the C++ tools. `build.bat` finds them with `vswhere`, and builds `build\duo57.lv2`.

```bash
build.bat
```

### macOS, Linux, MinGW

Needs a C compiler and `make`. On Linux, also the X11 headers (`libx11-dev` on Debian/Ubuntu). On macOS, the Xcode command-line tools; the build is universal (Apple silicon and Intel) by default, and `make ARCH=` builds for this Mac's architecture only. The libraries get the right name for each system (`.so` or `.dylib`).

```bash
make
```

```bash
make install
```

`make install` copies the bundle to `~/.lv2` (set `LV2DIR` for another folder; on macOS use `LV2DIR=~/Library/Audio/Plug-Ins/LV2`). `make dist` zips the bundle for release.

## Tests

Windows:

```bash
build.bat test
```

macOS, Linux:

```bash
make test
```

- `render` checks the plugin against the schematic's test-point voltages and its rated power. It renders a synthetic chord through five settings and through each cabinet to `build\test_*.wav`, checks that the cabinets are level-matched, and checks for NaN, glitches when switching input jacks or cabinets, and stability of the feedback loops. A single instance runs about 7–9 times faster than real time on the build machine.
- `ui_render` draws the panel to a BMP file: `build\ui_render.exe panel.bmp`.
- `ui_check` (Windows) loads the GUI the way a Windows host does and drives it with mouse messages: `build\ui_check.exe duo57.lv2 window.bmp`.

GitHub Actions builds and tests on Windows (MSVC), Ubuntu 22.04 and the current macOS for every push (see `.github/workflows/build.yml`). The macOS and Linux GUIs are checked by compiling them and rendering the shared panel code, not by opening a window. Pushing a version tag publishes a release with a zip for each system.

## Use of AI

Made with generative AI (Claude Code, Anthropic's Claude Opus 5.5), directed and reviewed by Davidian. The code, tests, LV2 metadata, build scripts and this README were written by the AI.

- **The author** supplied the schematic, the GUI house style, the names and the licensing choices. They also decided what to build and changed parts of the result themselves, including renaming the project to Duo '57.
- **The AI**:
  - read component values and topology from the scanned schematic and chose the estimates listed under [Estimates](#estimates)
  - wrote the DSP model and calibrated it against the schematic's test points
  - designed the five cabinet curves
  - adapted the house-style GUI to this panel and wrote the macOS window layer
  - wrote the test programs, the build scripts and CI workflow, and the documentation
- **Checking:** the results are checked by the automated tests described above:
  - test-point levels and power against the schematic
  - numerical stability, jack and cabinet switching, cabinet levels and speed
  - the GUI loaded and driven by simulated mouse input (Windows)
  - builds and DSP tests on Windows, macOS and Linux in CI
- **Not checked:** the sound hasn't been compared with a real amplifier, and the plugin hasn't been tried in a host yet. The macOS and Linux GUIs compile but haven't been opened in a host. The values under [Estimates](#estimates) should be tuned by ear.

## References

The model re-implements equations from these published works. No code was copied from them.

- N. Koren, "Improved vacuum tube models for SPICE simulations", *Glass Audio* 8(5), 1996. The 12AX7 triode equation and parameters.
- R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients" (the Audio EQ Cookbook). The low-pass, high-pass and peaking filters in the anti-alias and cabinet filters.
- V. Zavalishin, *The Art of VA Filter Design*, Native Instruments, 2012–2020. The topology-preserving one-pole filters.
- K. Karplus and A. Strong, "Digital synthesis of plucked-string and drum timbres", *Computer Music Journal* 7(2), 1983. Only used to make the test signal in `test/render.c`.

## Licence

Copyright (C) 2026 Davidian. GPL-3.0-or-later: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See [LICENSE](LICENSE) for the full text. Every source file starts with an SPDX licence line saying which licence applies to it.

### Third-party licences

| Component | Licence | How it's used |
|---|---|---|
| LV2 C interface (`lv2/core/lv2.h`, `lv2/ui/ui.h`) | ISC | Its declarations are re-stated in `src/lv2_core.h`, the UI ABI block of `src/ui/ui_lv2.c` and `test/ui_check.c`, so no LV2 SDK is needed. Copyright and permission notice in [LICENSE-LV2](LICENSE-LV2). |
| LV2 vocabularies (`lv2core`, `ui`, `units`) | ISC | The `.ttl` files refer to them by URI. Nothing is copied. |
| Microsoft C runtime | Visual Studio licence | `build.bat` links it statically (`/MT`) into the DLLs you build. It counts as a system library under the GPL, so this is allowed. No binaries are committed to this repository. |
| Apple Cocoa, Core Graphics; X11 (libX11) | System libraries | The macOS and Linux GUIs link them dynamically. Nothing is copied. |
| GitHub Actions (`actions/checkout`, `upload-artifact`, `download-artifact`, `softprops/action-gh-release`) | MIT | Used only by the CI workflow to build and publish releases. Not part of the plugin. |

This project is not affiliated with or endorsed by the maker of the original amplifier. Valve type numbers (12AX7, 6L6GC, 5U4) are generic industry designations.
