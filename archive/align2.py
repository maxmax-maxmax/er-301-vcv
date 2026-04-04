#!/usr/bin/env python3
"""Precise position analysis with relaxed thresholds."""
from PIL import Image
import numpy as np

img = Image.open("./Panel.png").convert("RGBA")
data = np.array(img)
h, w = data.shape[:2]

sx = 457.2 / w
sy = 380.0 / h

rgb = data[:,:,:3].astype(float)
brightness = rgb.mean(axis=2)
alpha = data[:,:,3]

# Relaxed dark threshold for displays/knob
dark40 = (brightness < 40) & (alpha > 200)
dark60 = (brightness < 60) & (alpha > 200)

# Blue: wider range
blue = (data[:,:,2].astype(int) - data[:,:,0].astype(int) > 40) & (data[:,:,2] > 120) & (alpha > 200)

def scan_edges(mask, cx, cy, max_r=500):
    """From center, find extent in all 4 directions."""
    cx, cy = int(cx), int(cy)
    left = cx
    while left > 0 and mask[cy, left]: left -= 1
    right = cx
    while right < w-1 and mask[cy, right]: right += 1
    top = cy
    while top > 0 and mask[top, cx]: top -= 1
    bot = cy
    while bot < h-1 and mask[bot, cx]: bot += 1
    return left+1, top+1, right, bot

def blob_bounds(mask, x_range, y_range, threshold=500):
    """Find bounding box of largest connected dark region."""
    region = mask[y_range[0]:y_range[1], x_range[0]:x_range[1]]
    ys, xs = np.where(region)
    if len(xs) < threshold:
        return None
    # Get tight bounds
    return (xs.min() + x_range[0], ys.min() + y_range[0],
            xs.max() + x_range[0], ys.max() + y_range[0])

print("=" * 60)
print("PRECISE ALIGNMENT ANALYSIS")
print("=" * 60)

# ── MAIN DISPLAY ──
print("\n=== MAIN DISPLAY ===")
# The main display is the largest dark rectangle in the upper portion
# Use dark40 threshold, search in upper-left quadrant
bounds = blob_bounds(dark40, (50, int(w*0.6)), (50, int(h*0.4)))
if bounds:
    l, t, r, b = bounds
    print(f"  Image: left={l} top={t} right={r} bottom={b}")
    print(f"  VCV:   x={l*sx:.1f}  y={t*sy:.1f}  w={(r-l)*sx:.1f}  h={(b-t)*sy:.1f}")
    print(f"  VCV center: ({(l+r)/2*sx:.1f}, {(t+b)/2*sy:.1f})")

# ── SUB DISPLAY ──
print("\n=== SUB DISPLAY ===")
# Sub display: dark rectangle, right of knob, y roughly 800-1200 in image
bounds = blob_bounds(dark40, (int(w*0.25), int(w*0.55)), (int(h*0.45), int(h*0.7)))
if bounds:
    l, t, r, b = bounds
    print(f"  Image: left={l} top={t} right={r} bottom={b}")
    print(f"  VCV:   x={l*sx:.1f}  y={t*sy:.1f}  w={(r-l)*sx:.1f}  h={(b-t)*sy:.1f}")
    print(f"  VCV center: ({(l+r)/2*sx:.1f}, {(t+b)/2*sy:.1f})")

# ── KNOB ──
print("\n=== KNOB ===")
# The knob is a large dark circle. Use dark60 for wider capture.
# Search in x=(50,700), y=(600,1200)
bounds = blob_bounds(dark60, (50, 700), (600, 1200), threshold=10000)
if bounds:
    l, t, r, b = bounds
    cx_img = (l + r) / 2
    cy_img = (t + b) / 2
    rx = (r - l) / 2
    ry = (b - t) / 2
    print(f"  Image bounds: left={l} top={t} right={r} bottom={b}")
    print(f"  VCV center: ({cx_img*sx:.1f}, {cy_img*sy:.1f})")
    print(f"  VCV radius X: {rx*sx:.1f}  radius Y: {ry*sy:.1f}")

# ── BLUE BUTTONS ──
print("\n=== BLUE BUTTONS ===")
# Find all blue regions
blue_ys, blue_xs = np.where(blue)
if len(blue_xs) > 0:
    print(f"  Total blue pixels: {len(blue_xs)}")
    # Group by Y bands
    y_min, y_max = blue_ys.min(), blue_ys.max()
    print(f"  Y range: {y_min*sy:.1f} to {y_max*sy:.1f} (VCV)")

    # Find distinct Y bands (clusters)
    unique_ys = np.unique(blue_ys)
    bands = []
    band_start = unique_ys[0]
    prev_y = unique_ys[0]
    for y in unique_ys[1:]:
        if y - prev_y > 20:  # gap between bands
            bands.append((band_start, prev_y))
            band_start = y
        prev_y = y
    bands.append((band_start, prev_y))

    print(f"  Found {len(bands)} Y bands:")
    for bs, be in bands:
        cy = (bs + be) / 2
        # Find X positions within this band
        band_mask = blue & (np.arange(h)[:,None] >= bs) & (np.arange(h)[:,None] <= be)
        band_xs = np.where(band_mask.any(axis=0))[0]
        if len(band_xs) > 0:
            # Find clusters in X
            x_clusters = []
            cs = band_xs[0]
            prev_x = band_xs[0]
            for x in band_xs[1:]:
                if x - prev_x > 20:
                    x_clusters.append((cs, prev_x))
                    cs = x
                prev_x = x
            x_clusters.append((cs, prev_x))

            centers = [f"cx={(s+e)/2*sx:.1f}" for s, e in x_clusters]
            print(f"    Band vcv_y={cy*sy:.1f}: {' '.join(centers)}")

# ── SELECT BUTTONS ──
print("\n=== SELECT BUTTONS ===")
# Grey squares in the right section, x around 1250-1400
grey = (brightness > 55) & (brightness < 140) & (alpha > 200)
for row_y_vcv in [50, 97.5, 145.4, 193.2]:
    y_img = int(row_y_vcv / sy)
    # Scan horizontal at this y
    row = grey[y_img]
    runs = []
    in_run = False
    start = 0
    for x in range(1150, 1500):
        if x < w and row[x] and not in_run:
            in_run = True; start = x
        if (x >= w or not row[x]) and in_run:
            in_run = False
            if x - start > 40:
                runs.append(((start+x)/2, x-start))
    if runs:
        for cx, rw in runs:
            print(f"  Row y={row_y_vcv:.1f}: cx={cx*sx:.1f} w={rw*sx:.1f}")

# ── OUTPUT/LINK LEDS ──
print("\n=== LED AREA (between select and G column) ===")
# Look for small bright or medium-grey dots
# The LEDs in the PNG are probably drawn as small circles
# Check the area around x=1400-1600 (VCV ~285-325)
for y_vcv in [50, 73.8, 97.5, 121.5, 145.4, 169.3, 193.2]:
    y_img = int(y_vcv / sy)
    # Sample brightness across the LED area
    samples = []
    for x in range(1350, 1650, 10):
        if x < w:
            b = brightness[y_img, x]
            if b > 200:  # bright spot
                samples.append(x)
    if samples:
        # Cluster them
        if len(samples) > 0:
            cx = np.mean(samples)
            print(f"  y_vcv={y_vcv:.1f}: bright spot at x_vcv={cx*sx:.1f}")

# ── TOGGLE SWITCHES ──
print("\n=== TOGGLE AREA (bottom-left) ===")
# Toggles have bright chrome levers
bright = brightness > 180
# Scan at y around hbY (vcv ~336)
for test_y_vcv in [330, 335, 340]:
    y_img = int(test_y_vcv / sy)
    row = bright[y_img]
    runs = []
    in_run = False
    start = 0
    for x in range(0, 800):
        if row[x] and not in_run:
            in_run = True; start = x
        if not row[x] and in_run:
            in_run = False
            if 5 < x - start < 60:
                runs.append(((start+x)/2, x-start))
    if runs:
        print(f"  y_vcv={test_y_vcv}: " + "  ".join(f"cx={r[0]*sx:.1f}(w={r[1]*sx:.1f})" for r in runs))

# ── FINE/COARSE AREA ──
print("\n=== FINE/COARSE LED AREA ===")
# Small bright dots in the left panel, around y=250-270 VCV
for y_vcv in [250, 255, 260, 265, 270]:
    y_img = int(y_vcv / sy)
    row = bright[y_img]
    for x in range(50, 400):
        if row[x]:
            runs = []
            start = x
            while x < 400 and row[x]: x += 1
            if 3 < x - start < 30:
                runs.append(((start+x)/2, x-start))
                print(f"  y_vcv={y_vcv}: bright at x_vcv={runs[0][0]*sx:.1f} (w={runs[0][1]*sx:.1f})")
            break

print("\n" + "=" * 60)
