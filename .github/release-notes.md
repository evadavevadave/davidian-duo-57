Davidian Duo '57 1.2.0: a tweed 2x12 combo amp model as LV2, VST3 and Audio Unit plugins, with the panel GUI in every format.

## What's new

- **VST3 for Windows, macOS and Linux, and an Audio Unit for macOS**, alongside LV2. All formats run the same amp code and sound the same; the VST3 is checked sample for sample against the LV2 build.
- **Estimates checked against the schematic.** The tone and volume pot curves are now fitted to the schematic's test-point levels, and the power stage to its output test (30 W at 5% THD). The DC operating points match the schematic's test points within 3–7%.

## Downloads

Each zip holds every format for one system, side by side. Copy the ones your host uses:

| System | File | Formats |
|---|---|---|
| Windows 10/11, 64-bit | `...-windows-x64.zip` | `duo57.lv2`, `Davidian Duo 57.vst3` |
| macOS 10.13+, Intel and Apple silicon | `...-macos-universal.zip` | `duo57.lv2`, `Davidian Duo 57.vst3`, `Davidian Duo 57.component` |
| Linux, x86_64 | `...-linux-x64.zip` | `duo57.lv2`, `Davidian Duo 57.vst3` |
| Linux, 64-bit ARM | `...-linux-arm64.zip` | `duo57.lv2`, `Davidian Duo 57.vst3` |

Where they go:

- **LV2:** `%APPDATA%\LV2` (Windows), `~/Library/Audio/Plug-Ins/LV2` (macOS), `~/.lv2` (Linux)
- **VST3:** `C:\Program Files\Common Files\VST3` (Windows), `~/Library/Audio/Plug-Ins/VST3` (macOS), `~/.vst3` (Linux)
- **Audio Unit:** `~/Library/Audio/Plug-Ins/Components` (macOS)

The macOS plugins are ad hoc signed, not notarized. After copying, clear the download quarantine: `xattr -dr com.apple.quarantine <plugin>`.

GPL-3.0-or-later. Made with generative AI (Claude Code); see the README.
