# ER-301 Sound Computer — VCV Rack Module

A VCV Rack plugin that embeds the full [ER-301 Sound Computer](https://www.orthogonaldevices.com/er-301) engine, including its Lua-based UI, DSP graph, and display.

This is not a simplified emulation — it runs the actual ER-301 firmware with a HAL (Hardware Abstraction Layer) adapted for VCV Rack.

## Features

- Full ER-301 engine running inside VCV Rack
- 30HP SVG panel matching the physical module layout
- Main display (256x64, 4-bit grayscale) and sub display (128x64, 1-bit mono) with amber tint, rendered via NanoVG with rounded corners
- 20 inputs: 4 audio (IN1-IN4), 12 CV (A1-D3), 4 gate (G1-G4)
- 4 outputs (OUT1-OUT4)
- 19 buttons (grey tactile + blue function) with SVG artwork and press-darken feedback
- Rotary encoder (Rogan2SGray knob) with visual rotation on drag/scroll
- 2 toggle switches (STORAGE, MODE) using NKK 3-position SVGs
- LEDs: output levels (yellow), link indicators (red), fine/coarse, I/O, safe, and bicolor green/red CV input LEDs driven by PWM readback
- Audio bridge with 128-sample frame buffering (~2.67ms latency at 48kHz)

## What's Been Done

### Phase 1-3: Core Engine Integration
- **Audio bridge**: 128-sample ring buffer bridging VCV's sample-by-sample processing to ER-301's frame-based DSP via `Pump_callback()`
- **VCV HAL layer**: ~18 files in `src/hal/` replacing the ER-301's SDL-based emulator HAL (display, GPIO, encoder, timing, audio, card/filesystem, logging, threading, etc.)
- **Display decoding**: Main display (256x64, 4-bit grayscale packed) and sub display (128x64, 1-bit packed) decoded and rendered via NanoVG
- **Interactive controls**: 19 GPIO-mapped buttons, encoder knob with drag/scroll, 2 toggle switches
- **Lua interpreter**: ER-301's full Lua UI boots on a separate thread, running `start.lua`

### Phase 4-6: Panel Visual Matching
- Iterated through HTML Canvas preview and NanoVG-drawn panel approaches
- Arrived at Panel.png background with transparent widget overlays

### Phase 7: SVG Panel Transition (current)
- Replaced PNG panel with standard VCV SVG panel workflow (`setPanel(createPanel(...))`)
- Panel built in Inkscape with proper VCV component layer (color-coded placeholders for inputs/outputs/lights/custom widgets)
- All widget positions generated via VCV `helper.py` using `mm2px()` coordinates
- Custom SVG component artwork: grey and blue button SVGs, Rogan2SGray encoder knob, NKK 3-position toggle switch SVGs
- Encoder knob visually rotates when dragged/scrolled
- Buttons darken on press
- Display overlays render with rounded corners on top of the SVG panel

## What's Left

### Verification
- Fine-tune display positions to sit perfectly within the SVG panel bezels
- Verify all 19 button-to-GPIO mappings are correct
- Test toggle switch state persistence

### Functional Gaps
- **Encoder button press** — the physical ER-301 encoder has a push-button (used for selection); not yet wired
- **SD card / file system** — `card.cpp` has a basic implementation but saving/loading presets needs testing
- **USB emulation** — `usb.cpp` exists but likely stubbed
- **Audio sample rate matching** — ER-301 expects specific rates (48kHz/96kHz); VCV Rack runs at variable rates
- **Module state persistence** — saving/restoring module state when VCV patches are saved/loaded
- **Performance** — Lua interpreter + DSP engine running alongside VCV Rack may need profiling
- **Single instance only** — ER-301 uses global state; only one module instance is supported

### Polish
- Right-click context menu for module settings
- Error handling if ER-301 engine fails to initialize
- Core DSP packages (mods/) not yet bundled

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
| Display | SDL textures | NanoVG `nvgCreateImageRGBA` on SVG panel |
| Buttons/GPIO | SDL keyboard/mouse | `SvgWidget` with click handlers |
| Encoder | SDL mouse wheel | Draggable knob with visual rotation |
| Toggles | SDL keyboard | NKK 3-position `SvgWidget` |
| Concurrency | SDL threads/mutexes | `std::thread` / `std::mutex` |
| Timing | SDL ticks | `std::chrono` |
| Logging | stdout | `~/.od/er301-vcv.log` |
| File I/O | Filesystem | Filesystem (same) |

The audio bridge accumulates VCV's sample-by-sample calls into 128-sample frames, then calls the ER-301 `Pump_callback()` synchronously. This introduces ~2.67ms latency at 48kHz.

## File Structure

```
src/
  ER301Module.cpp    — Main module: engine integration, widget classes, panel layout
  hal/               — VCV HAL implementations (~18 files)
res/
  ER301.svg          — SVG panel (built in Inkscape, components layer hidden)
  components/        — SVG artwork for buttons, knob, toggles
er-301/              — Symlink to ER-301 source tree
Rack-SDK/            — VCV Rack SDK
```

## License

GPL-3.0-or-later

The ER-301 firmware is copyright [Orthogonal Devices](https://www.orthogonaldevices.com/) and licensed under GPL-3.0.
