# ER-301 VCV Plugin — Development Context

This document captures the current state, key architectural decisions, and known constraints for developers continuing work on the plugin.

## Current State

The plugin is a working prototype. The full ER-301 engine (Lua UI, DSP graph, all 80+ core units) runs inside VCV Rack with interactive controls and dual display rendering. It replaces the ER-301's SDL-based emulator HAL with ~18 VCV-native HAL files in `src/hal/`.

Everything is in a single source file (`src/ER301Module.cpp`, ~968 lines) containing the module, four widget classes (button, toggle, knob, display/panel), and the engine initialization logic. This is intentional — the file is manageable at this size and avoids splitting tightly coupled VCV/HAL glue code across files.

### What's working
- Engine init with error handling, single instance guard, sample rate warning
- Audio bridge: 128-sample ring buffer bridging VCV's per-sample processing to ER-301's frame-based `Pump_callback()`
- Display: main (256x64, 4-bit grayscale) and sub (128x64, 1-bit mono), decoded and rendered via NanoVG with 4x nearest-neighbor upscale
- All controls: 19 GPIO-mapped buttons, encoder knob (drag + scroll), 2 three-position toggles
- All LEDs: GPIO-driven (output, link, fine/coarse, I/O, safe) and PWM-driven (12 bicolor CV LEDs)
- Core DSP packages: `libcore.so` and all 80+ units load via RTLD_GLOBAL symbol promotion

### What's not working yet
- No keyboard shortcuts (button combos like SHIFT+ENTER require this)
- No state persistence (toggles reset, no quicksave on patch save/load)
- No MIDI mapping
- macOS only, 48kHz only

## Architecture Decisions

### Audio bridge (128-sample frames)
The ER-301 engine processes audio in fixed-size frames (default 128 samples). VCV Rack processes sample-by-sample. The bridge accumulates samples in `inFrame[]`, calls `Pump_callback()` when a full frame is ready, then reads output from `outFrame[]` one sample at a time. This adds ~2.67ms latency at 48kHz. There is no way around this without rewriting the ER-301 engine — it's fundamental to how the DSP graph works.

### RTLD_GLOBAL for DSP packages
The ER-301's Lua layer loads DSP packages (like `libcore.so`) via `dlopen`. These .so files reference symbols from the main ER-301 engine (e.g. `od::ZeroOutput`). In VCV Rack, those symbols live in `plugin.dylib`, which is loaded with `RTLD_LOCAL` by default. The fix: at init time, we use `dladdr` to find our own dylib path, then re-open it with `RTLD_NOW | RTLD_GLOBAL` to make symbols visible to subsequently loaded .so files.

### Single instance only
The ER-301 engine uses extensive global state (`globalConfig`, HAL singletons, Lua interpreter). Supporting multiple instances would require refactoring the entire ER-301 codebase. Instead, we use an atomic `instanceCount` and block the second instance with an error overlay.

### Sample rate locked at init
`globalConfig` (sample rate, frame length, derived values) is set once by `Config_init()` and used everywhere in the engine. The ER-301 was designed for fixed 48kHz operation. VCV Rack destroys and recreates modules on sample rate changes, but the engine's global state makes runtime rate changes unreliable. Current approach: check at init, show a warning if mismatched. Future approach: add a resampler at the audio bridge boundary to decouple the two rates entirely.

### Display rendering
The ER-301 writes to packed pixel buffers (4-bit grayscale for main, 1-bit for sub). We decode these into RGBA, writing each source pixel as a 4x4 block (UPSCALE=4) into a larger buffer. NanoVG renders the texture with `NVG_IMAGE_NEAREST` for sharp pixels at any zoom. The amber tint comes from setting R to full intensity and G to 85% (SCREEN_TINT=0.85).

### Config paths
`Config_init()` stores raw `const char*` pointers to path strings. The module keeps `std::string` members (`xRootStr`, `rearRootStr`, `frontRootStr`, `firmwareCfgStr`) alive for the lifetime of the module to prevent dangling pointers.

## Key Files

| File | Purpose |
|---|---|
| `src/ER301Module.cpp` | Module, widgets, engine init, display rendering |
| `src/plugin.cpp` | VCV plugin registration |
| `src/hal/` | VCV HAL layer (~18 files replacing SDL HAL) |
| `src/hal/audio.c` | Audio bridge — exposes buffers to `Pump_callback()` |
| `src/hal/display.cpp` | Display buffer management |
| `src/hal/gpio.c` | Button/LED state via GPIO read/write |
| `src/hal/encoder.cpp` | Encoder delta accumulator |
| `src/hal/card.cpp` | Filesystem/SD card abstraction |
| `res/ER301.svg` | SVG panel (Inkscape, 30HP, component positions in hidden layer) |
| `res/components/` | SVG artwork: GreyButton, BlueButton, Rogan2SGray, NKK toggles |
| `sync_panel.sh` | Extracts component positions from SVG and prints C++ code |
| `Makefile` | Build config — ER-301 sources, FFTW, SWIG, direct-install target |

## Immediate Next Steps

These follow the roadmap in README.md, Phase 1 (Usability):

### 1. Keyboard shortcuts
Map keyboard keys to ER-301 buttons when the module widget has hover focus. Use VCV's `onHoverKey` event. Key bindings should mirror the ER-301 emulator where possible. Critical for button combos — SHIFT is a global modifier (held while pressing other buttons), and SELECT+SELECT toggles channel linking (polled via `Button_pressed`).

### 2. Toggle click from center
Currently toggles only respond to clicking the top or bottom third. Add center-third click to cycle through states (0→1→2→0). The click regions are in `ER301Toggle::onButton`.

### 3. Button/knob tooltips
Add `description` strings to custom widgets. VCV shows tooltips on hover for standard components, but our custom `SvgWidget`-based buttons and knob need manual tooltip support or wrapping in a `ParamWidget`.

### 4. Right-click context menu
Override `appendContextMenu` on `ER301Widget` to add: log file path, xroot/rear/front paths, link to docs. Override or disable VCV's built-in "Randomize" (or repurpose it to insert random ER-301 units at different chain levels).

### 5. Mouse & keyboard interaction improvements
The real ER-301 has a physical encoder and buttons — in VCV we can do better. Key ideas:
- **Keyboard text input** — When the ER-301 UI is in a search/rename context, intercept `onHoverKey` text events and feed characters into the engine instead of requiring encoder scrolling through an alphabet. This is exploratory — it depends on how the Lua UI handles text input internally (may need to inject keypress events or call Lua functions directly).
- **Click-drag scrolling on displays** — Detect drag gestures over the main/sub display areas and translate vertical movement into encoder deltas, giving a touch-screen feel for scrolling lists and menus.
- **Mouse wheel on displays** — Forward scroll wheel events over display areas to the encoder, so you don't have to hover over the knob to scroll.
- The feasibility of text input depends on whether the ER-301 Lua UI exposes a text entry API or if it's purely encoder-driven. Needs investigation.

After usability, the next priority is **persistence** (Phase 2) — saving toggle state and triggering quicksaves on VCV patch save/load.
