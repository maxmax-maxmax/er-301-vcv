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

### Phase 4-7: Panel & Display
- SVG panel built in Inkscape with VCV-standard component layer workflow
- Custom SVG artwork: grey/blue buttons, Rogan2SGray encoder knob, NKK 3-position toggles
- Display rendering with 4× nearest-neighbor upscale for crisp pixels at any zoom level
- `sync_panel.sh` script for automated SVG-to-C++ position syncing

### Phase 8: Core DSP Packages
- Promoted plugin symbols to `RTLD_GLOBAL` so `libcore.so` can resolve ER-301 symbols
- All 80+ core units now load and work (oscillators, filters, delays, sample players, etc.)

## Roadmap

### Phase 1 — Stability & Safety
- [x] **Error handling** — Lua thread and HAL init wrapped in try/catch, error overlay on both displays if engine fails
- [x] **Single instance guard** — Detects and blocks second instance with error overlay
- [x] **Sample rate warning** — Shows amber warning on main display if VCV rate doesn't match ER-301's 48kHz at boot. Note: rate changes require a VCV restart to take effect (the ER-301 engine locks its sample rate at init)

### Phase 2 — Usability
- [ ] **Keyboard shortcuts** — Map keyboard keys to buttons (like the ER-301 emulator), enabling button combos (SHIFT+ENTER, SHIFT+SELECT for mute, SELECT+SELECT for channel linking)
- [ ] **Toggle click from center** — Allow clicking the center of the toggle to cycle state (in addition to current top/bottom click regions)
- [ ] **Button/knob tooltips** — Add hover tooltips to custom buttons (M1-M6, S1-S3, fine/coarse, cancel, zero, enter, up, shift), encoder knob, and toggles (storage, mode)
- [ ] **Right-click context menu** — Show log paths, xroot/rear/front paths, link to docs. Disable VCV's "Randomize" action (or override it to randomize ER-301 internal patch — add random units at different chain levels inside the ER-301 engine)

### Phase 3 — Persistence
- [ ] **Toggle persistence** — Save/restore toggle positions in VCV patch JSON (`dataToJson`/`dataFromJson`)
- [ ] **SD card / filesystem testing** — Validate quicksaves, preset save/load, WAV sample loading
- [ ] **Module state persistence** — Trigger quicksave on VCV patch save, restore on load after engine init

### Phase 4 — Integration
- [ ] **MIDI mapping** — Allow MIDI CC/note mapping to buttons, encoder, and toggles for hardware controller integration
- [ ] **Performance profiling** — Add timing around `Pump_callback()`, track frame processing time

### Phase 5 — Deep Work
- [ ] **Full engine state save/restore** — Serialize complete Lua + DSP state beyond quicksaves
- [ ] **Multi-instance support** — Would require refactoring all ER-301 global state (probably not worth it)

### Phase 6 — VCV Library Publication
- [ ] **Brand permission** — Get explicit approval from Orthogonal Devices (Brian Clarkson) to use ER-301 name and panel design, or rebrand
- [ ] **Static FFTW** — Replace Homebrew dynamic link with static build in `dep/` for cross-compilation
- [ ] **Pre-generate SWIG** — Commit `app_swig.cpp` to repo so SWIG isn't needed at build time
- [ ] **Cross-platform build** — Ensure Linux + Windows compilation via VCV rack-plugin-toolchain
- [ ] **Relocate data files** — Move `~/.od/` data into Rack's standard user/plugin folder
- [ ] **Bundle assets** — Include Lua scripts and xroot assets in `DISTRIBUTABLES`

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
