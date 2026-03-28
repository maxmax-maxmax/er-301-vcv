# ER-301 Sound Computer — VCV Rack Module

A VCV Rack plugin that embeds the full [ER-301 Sound Computer](https://www.orthogonaldevices.com/er-301) engine, including its Lua-based UI, DSP graph, and display.

This is not a simplified emulation — it runs the actual ER-301 firmware with a HAL (Hardware Abstraction Layer) adapted for VCV Rack.

## Features

- Full ER-301 engine running inside VCV Rack
- Main display (256x64, 4-bit grayscale) and sub display (128x64, 1-bit mono) rendered via NanoVG
- 20 inputs: 4 audio (IN1–IN4), 12 CV (A1–D3), 4 gate (G1–G4)
- 4 outputs (OUT1–OUT4)
- Interactive controls: 19 buttons, rotary encoder, 2 toggle switches, 11 LEDs
- Audio bridge with 128-sample frame buffering between VCV and the ER-301 engine

## Prerequisites

- [VCV Rack 2 SDK](https://vcvrack.com/manual/Building)
- [ER-301 source code](https://github.com/odevices/er-301)
- [FFTW3](http://www.fftw.org/) (`brew install fftw` on macOS)
- [SWIG](http://www.swig.org/) (`brew install swig` on macOS)

## Building

1. Clone this repo:
   ```
   git clone https://github.com/maxmax-maxmax/er-301-vcv.git
   cd er-301-vcv
   ```

2. Symlink the ER-301 source tree:
   ```
   ln -s /path/to/er-301 er-301
   ```

3. Place or symlink the [VCV Rack SDK](https://vcvrack.com/manual/Building) as `Rack-SDK/`.

4. Copy ER-301 Lua scripts to the ER-301 rear root location (typically `~/.od/rear`):
   ```
   mkdir -p ~/.od/rear
   cp -r er-301/xroot/* ~/.od/rear/
   ```

5. Build and install:
   ```
   make install
   ```

## Architecture

The plugin replaces the ER-301's SDL-based emulator HAL with a VCV-native HAL layer:

| Component | ER-301 Emulator | VCV Plugin |
|---|---|---|
| Audio I/O | SDL audio callback | `Module::process()` with ring buffer |
| Display | SDL textures | NanoVG `nvgCreateImageRGBA` |
| Buttons/GPIO | SDL keyboard/mouse | `OpaqueWidget` click handlers |
| Encoder | SDL mouse wheel | Draggable knob widget |
| Concurrency | SDL threads/mutexes | `std::thread` / `std::mutex` |
| Timing | SDL ticks | `std::chrono` |
| Logging | stdout | `~/.od/er301-vcv.log` |
| File I/O | Filesystem | Filesystem (same) |

The audio bridge accumulates VCV's sample-by-sample calls into 128-sample frames, then calls the ER-301 `Pump_callback()` synchronously. This introduces ~2.67ms latency at 48kHz.

## Current Status

This is a work in progress. The engine boots, displays render, and controls are interactive. Known limitations:

- Single instance only (ER-301 uses global state)
- macOS only (tested on Apple Silicon)
- Sample rate must match ER-301 expectations (48kHz / 96kHz)
- No state save/restore yet
- Core DSP packages (mods/) not yet bundled

## License

GPL-3.0-or-later

The ER-301 firmware is copyright [Orthogonal Devices](https://www.orthogonaldevices.com/) and licensed under GPL-3.0.
