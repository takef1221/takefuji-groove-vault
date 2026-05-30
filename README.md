# Takefuji Groove Vault

A Windows standalone application for browsing, previewing, and dragging MIDI drum patterns into your DAW.

## Overview

Takefuji Groove Vault is a MIDI pattern browser built with [JUCE 8](https://juce.com/).  
It reads drum patterns from a local `Resources/` folder, lets you filter by meter, category, vibe, density, BPM, and source track, and drag the selected `.mid` file directly into Studio One (or any DAW that accepts external drag-and-drop).

Key features:
- Filter panel: Track Name / Meter / Category / Vibe / Density / BPM Range
- Drag-and-drop `.mid` files from the pattern list into any DAW
- In-app audio preview (WAV files)
- KEYMAP converter: convert BFD3 MIDI note assignments to GM, SD3, AD2, etc.
- Edit Map dialog with per-instrument target note editing
- Custom keymap save/load (stored in `%APPDATA%\TakefujiGrooveVault\keymaps\`)

## Requirements

| Tool | Version |
|------|---------|
| Windows | 10 or later (64-bit) |
| Visual Studio | 2022 (MSVC v143) or VS2026 (v145) |
| JUCE | 8.x (path: `C:\JUCE\`) |

## Building from Source

1. Open `Builds/VisualStudio2026/TakefujiGrooveVault.sln` in Visual Studio.
2. Select **Release | x64** and build the solution.  
   (Run Visual Studio as Administrator to auto-install to `C:\Program Files\TakefujiGrooveVault\`.)
3. The post-build event copies the exe and `Resources/` to the install folder automatically.

> **Note:** `JuceLibraryCode/` is excluded from this repo.  
> Re-generate it by opening `TakefujiGrooveVault.jucer` in Projucer and clicking *Save and Open in IDE*.

## Resource Folder Layout

```
Resources/
├── metadata.json       # Pattern metadata (generated from GSS — not in repo)
├── midi/               # MIDI pattern files (not in repo)
│   └── TDG_001.mid
├── preview/            # Preview WAV files (not in repo)
│   └── TDG_001.wav
└── keymaps/            # Drum instrument key maps
    ├── bfd3.json
    ├── gm.json
    ├── sd3.json
    └── ad2.json
```

`metadata.json` and media files are managed separately and must be placed in `Resources/` before building or running in development.

## Keymap Format

```json
{
  "name": "BFD3",
  "note_to_part": {
    "24": { "part": "kick", "artic": "hit" }
  },
  "part_to_note": {
    "kick_hit": 24
  }
}
```

## License

Private project — all rights reserved.
