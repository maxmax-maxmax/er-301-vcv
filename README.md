# ER-301 Sound Computer — VCV Rack Module

A VCV Rack plugin that embeds the full [ER-301 Sound Computer](https://www.orthogonaldevices.com/er-301) engine, including its Lua-based UI, DSP graph, and display. This is not a simplified emulation — it runs the actual ER-301 firmware with a HAL (Hardware Abstraction Layer) adapted for VCV Rack.

## Status

Working prototype. The full ER-301 engine runs inside VCV Rack with all 80+ core DSP units (oscillators, filters, delays, sample players, etc.), interactive controls, and dual display rendering.

**What works:**
- Full engine with Lua UI booting on a dedicated thread
- 20 inputs (4 audio, 12 CV, 4 gate) and 4 outputs
- 19 buttons (grey tactile + blue function) with press feedback
- Rotary encoder with drag/scroll interaction
- 2 toggle switches (STORAGE, MODE) — 3-position NKK style
- Main display (256x64, 4-bit grayscale) and sub display (128x64, 1-bit mono), amber tint, 4x upscaled with nearest-neighbor for crisp pixels
- All LEDs: output levels, link indicators, fine/coarse, I/O, safe, bicolor green/red CV LEDs via PWM readback
- Error handling with overlay on both displays if engine fails
- Single instance guard (ER-301 uses global state)
- Sample rate mismatch warning at boot

**Current limitations:**
- macOS only (ARM64 and x86_64)
- Requires 48kHz sample rate in VCV Rack (ER-301 engine locks rate at init)
- Single instance only (global state in ER-301 firmware)
- ~2.67ms audio latency (128-sample frame buffering)

## Roadmap

### Phase 1 — Usability
- [ ] Keyboard shortcuts — Map keys to buttons (SHIFT+ENTER, SHIFT+SELECT for mute, SELECT+SELECT for channel linking)
- [ ] Toggle click from center — Cycle state by clicking the middle zone (in addition to top/bottom regions)
- [ ] Button/knob tooltips — Hover tooltips on all custom controls
- [ ] Right-click context menu — Show log/data paths, link to docs. Override VCV "Randomize" to insert random ER-301 units

### Phase 2 — Persistence
- [ ] Toggle persistence — Save/restore toggle positions via `dataToJson`/`dataFromJson`
- [ ] SD card / filesystem testing — Validate quicksaves, preset save/load, WAV sample loading
- [ ] Module state persistence — Trigger quicksave on VCV patch save, restore on load

### Phase 3 — Integration
- [ ] MIDI mapping — Map MIDI CC/notes to buttons, encoder, and toggles
- [ ] Performance profiling — Timing around `Pump_callback()`, frame processing stats

### Phase 4 — Deep Work
- [ ] Sample rate resampler — Decouple VCV and ER-301 sample rates by resampling at the audio bridge boundary, removing the 48kHz requirement
- [ ] Full engine state save/restore — Serialize complete Lua + DSP state beyond quicksaves
- [ ] Multi-instance support — Requires refactoring all ER-301 global state (probably not worth it)

### Phase 5 — VCV Library Publication
- [ ] Brand permission — Get approval from Orthogonal Devices (Brian Clarkson) to use ER-301 name/design, or rebrand
- [ ] Static FFTW — Replace Homebrew dynamic link with static build for cross-compilation
- [ ] Pre-generate SWIG — Commit `app_swig.cpp` so SWIG isn't needed at build time
- [ ] Cross-platform build — Linux + Windows via VCV rack-plugin-toolchain
- [ ] Relocate data files — Move `~/.od/` into Rack's standard plugin data folder
- [ ] Bundle assets — Include Lua scripts and xroot in `DISTRIBUTABLES`

## Architecture

The plugin replaces the ER-301's SDL-based emulator HAL with a VCV-native HAL layer (~18 files in `src/hal/`):

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

The audio bridge accumulates VCV's sample-by-sample calls into 128-sample frames, then calls `Pump_callback()` synchronously. Core DSP packages (like `libcore.so`) resolve ER-301 symbols via `RTLD_GLOBAL` promotion of the plugin dylib.

## Building

### Prerequisites

- [VCV Rack 2 SDK](https://vcvrack.com/manual/Building)
- [ER-301 source code](https://github.com/odevices/er-301)
- [FFTW3](http://www.fftw.org/) — `brew install fftw`
- [SWIG](http://www.swig.org/) — `brew install swig`

### Steps

```bash
git clone https://github.com/maxmax-maxmax/er-301-vcv.git
cd er-301-vcv

# Symlink ER-301 source and VCV Rack SDK
ln -s /path/to/er-301 er-301
ln -s /path/to/Rack-SDK Rack-SDK

# Copy Lua scripts to ER-301 data directory
mkdir -p ~/.od/rear
cp -r er-301/xroot/* ~/.od/rear/

# Build and install
make install
```

For development, `make direct-install` copies files directly without zstd packaging.

## File Structure

```
src/
  ER301Module.cpp      Main module: engine, widgets, panel layout
  plugin.cpp           VCV plugin registration
  hal/                 VCV HAL implementations (~18 files)
res/
  ER301.svg            SVG panel (Inkscape, 30HP)
  components/          SVG artwork for buttons, knob, toggles
sync_panel.sh          Automated SVG-to-C++ position syncing
```

## License

GPL-3.0-or-later

The ER-301 firmware is copyright [Orthogonal Devices](https://www.orthogonaldevices.com/) and licensed under GPL-3.0.
