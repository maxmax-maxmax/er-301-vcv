# ER-301 VCV Rack Plugin — Development Context

## Architecture

The plugin embeds the real ER-301 Sound Computer engine (Lua + DSP) inside VCV Rack as a 30HP module.

### Panel Rendering (current approach)

- **Panel.png** (2246×1888, 638KB) is the visual reference — a detailed render of the real ER-301 faceplate with all labels, buttons, jacks, knob, toggles, dividers, specs text, etc.
- The PNG is loaded as a **NanoVG texture** in `draw()` and rendered scaled to 457.2×380 (VCV's 30HP panel size).
- All panel chrome (labels, bezels, dividers, pill outlines, column headers, ABCD labels, linked text, etc.) comes from the PNG — **no NanoVG drawing code** for panel decoration.
- Only the **display pixel buffers** (main 256×64, sub 128×64) are drawn dynamically on top of the PNG.
- VCV widgets (buttons, knob, toggles) are **transparent overlays** — they handle click/drag interaction but draw nothing (or a subtle press highlight). The PNG provides their visual appearance.
- Standard **PJ301MPort** jacks render on top of the PNG jack artwork.
- Standard VCV **LEDs** (MediumLight) are placed at mapped positions.

### Position Mapping

All widget positions were derived by analyzing `Panel.png` pixel data and converting to VCV coordinates:
- Scale: `vcv_x = img_x × (457.2 / 2246)`, `vcv_y = img_y × (380 / 1888)`
- Positions were iteratively refined using a Python overlay renderer (`render_overlay.py`) that draws widget hit areas on top of the PNG for visual verification.

### Key Positions (VCV coords, 457.2×380)

| Element | Position |
|---------|----------|
| M button X centers | 26.8, 69.8, 113.3, 156.6, 200.1, 243.5 |
| M button row Y | 147.0 |
| Sub button row Y | 290.5 |
| Hard button row Y | 338.0 |
| Knob center | (84.0, 213.0), r=32 |
| Main display | x=20, y=50, 230×42 |
| Sub display | x=128, y=194, 120×48 |
| G/IN/OUT jack X | 350.5, 393.9, 436.8 |
| Upper jack rows Y | 50.0, 97.5, 145.4, 193.2 |
| ABCD jack X (A/B/C/D) | 307.0, 350.5, 393.5, 436.7 |
| ABCD jack rows Y | 241.5, 290.1, 337.8 |
| Select buttons X | 256.0 |
| Output LED X | 275.0 |

### Files

- **`src/ER301Module.cpp`** — Main source: module definition, HAL wiring, widget classes, panel layout, draw function
- **`res/Panel.png`** — Panel background image (copied from root `Panel.png`)
- **`Panel.png`** — Source panel image (2246×1888 RGBA)
- **`Panel.svg`** — SVG version (11MB, mostly embedded raster — not used by VCV since nanosvg doesn't support `<image>`)
- **`render_overlay.py`** — Python tool to render alignment overlay for position verification
- **`align.py` / `align2.py`** — Python scripts for automated position detection from PNG pixel analysis
- **`Makefile`** — Build config; `DISTRIBUTABLES += res` ensures Panel.png ships with the plugin

### Build

```bash
cd er-301-vcv
PATH="/opt/homebrew/bin:$PATH" make install
```

Requires: fftw (`brew install fftw`), VCV Rack SDK in `Rack-SDK/`, ER-301 source in `er-301/`

## Immediate Next Steps

1. **Fine-tune display positions** — The main and sub display render areas may need per-pixel adjustment once viewed in VCV Rack with actual ER-301 engine output. The current values are estimated from the PNG black regions.
2. **Test in VCV Rack** — Launch Rack, add the ER-301 module, verify:
   - PNG background renders correctly
   - Displays show ER-301 output in the right location
   - Buttons/knob/toggles are clickable at the right positions
   - Jacks align with the PNG artwork
3. **Iterate positions** — Adjust any misaligned widgets based on VCV Rack screenshots
4. **Commit and push** once alignment is confirmed
