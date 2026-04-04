#!/usr/bin/env python3
"""Analyze Panel.png to find precise widget positions for VCV Rack alignment."""
from PIL import Image
import numpy as np

img = Image.open("./Panel.png").convert("RGBA")
data = np.array(img)
h, w = data.shape[:2]

sx = 457.2 / w  # 0.20356
sy = 380.0 / h  # 0.20127

rgb = data[:,:,:3]
brightness = rgb.mean(axis=2)
alpha = data[:,:,3]

# Blue detection: R<100, G<100, B>140
blue_mask = (rgb[:,:,0] < 100) & (rgb[:,:,1] < 100) & (rgb[:,:,2] > 140)

# Dark detection: brightness < 25, opaque
dark_mask = (brightness < 25) & (alpha > 200)

# Grey button detection: brightness 60-130, opaque
grey_mask = (brightness > 55) & (brightness < 135) & (alpha > 200)

def find_runs_h(mask, y, min_w=40, max_w=300):
    """Find horizontal runs of True pixels at row y."""
    row = mask[y]
    runs = []
    in_run = False
    start = 0
    for x in range(len(row)):
        if row[x] and not in_run:
            in_run = True; start = x
        if (not row[x]) and in_run:
            in_run = False
            w = x - start
            if min_w <= w <= max_w:
                runs.append((start, x, (start+x)/2, w))
    return runs

def find_runs_v(mask, x, min_h=40, max_h=300):
    """Find vertical runs of True pixels at column x."""
    col = mask[:, x]
    runs = []
    in_run = False
    start = 0
    for y in range(len(col)):
        if col[y] and not in_run:
            in_run = True; start = y
        if (not col[y]) and in_run:
            in_run = False
            h = y - start
            if min_h <= h <= max_h:
                runs.append((start, y, (start+y)/2, h))
    return runs

def find_blob_center(mask, x_range, y_range, min_area=500):
    """Find center of mass of largest blob in region."""
    region = mask[y_range[0]:y_range[1], x_range[0]:x_range[1]]
    ys, xs = np.where(region)
    if len(xs) < min_area:
        return None
    cx = xs.mean() + x_range[0]
    cy = ys.mean() + y_range[0]
    return cx, cy

print("=" * 60)
print("Panel.png Analysis — positions in VCV coords (457.2 x 380)")
print("=" * 60)

# ── M BUTTONS (grey, ~y=720 in image) ──
# Scan multiple rows to find the button band
print("\n--- M BUTTON ROW (grey) ---")
for test_y in [700, 710, 720, 730, 740]:
    runs = find_runs_h(grey_mask, test_y, 80, 160)
    if len(runs) >= 5:
        print(f"  Found {len(runs)} buttons at img_y={test_y}")
        for s, e, cx, w in runs:
            print(f"    cx={cx*sx:.1f}  w={w*sx:.1f}")
        # Find vertical extent
        btn_cx = int(runs[0][2])
        vruns = find_runs_v(grey_mask, btn_cx, 80, 160)
        for s, e, cy, h in vruns:
            print(f"    Y center: {cy*sy:.1f}  h={h*sy:.1f}")
        break

# ── SUB BUTTON ROW (blue first 3, grey last 3) ──
print("\n--- SUB BUTTON ROW (blue) ---")
for test_y in [1390, 1400, 1410, 1420, 1430, 1440]:
    runs = find_runs_h(blue_mask, test_y, 80, 160)
    if len(runs) >= 3:
        print(f"  Found {len(runs)} blue buttons at img_y={test_y}")
        for s, e, cx, w in runs:
            print(f"    cx={cx*sx:.1f}  w={w*sx:.1f}")
        # Y center
        btn_cx = int(runs[0][2])
        vruns = find_runs_v(blue_mask, btn_cx, 80, 160)
        for s, e, cy, h in vruns:
            print(f"    Y center: {cy*sy:.1f}  h={h*sy:.1f}")
        break

print("\n--- SUB BUTTON ROW (grey S1-S3) ---")
for test_y in [1390, 1400, 1410, 1420, 1430, 1440]:
    runs = find_runs_h(grey_mask, test_y, 80, 160)
    if len(runs) >= 3:
        print(f"  Found {len(runs)} grey buttons at img_y={test_y}")
        for s, e, cx, w in runs:
            print(f"    cx={cx*sx:.1f}  w={w*sx:.1f}")
        break

# ── HARD BUTTON ROW (blue: ENTER, UP, SHIFT) ──
print("\n--- HARD BUTTON ROW (blue) ---")
for test_y in [1630, 1640, 1650, 1660, 1670, 1680]:
    runs = find_runs_h(blue_mask, test_y, 80, 160)
    if len(runs) >= 3:
        print(f"  Found {len(runs)} blue buttons at img_y={test_y}")
        for s, e, cx, w in runs:
            print(f"    cx={cx*sx:.1f}  w={w*sx:.1f}")
        btn_cx = int(runs[0][2])
        vruns = find_runs_v(blue_mask, btn_cx, 80, 160)
        for s, e, cy, h in vruns:
            print(f"    Y center: {cy*sy:.1f}  h={h*sy:.1f}")
        break

# ── KNOB (large dark circle) ──
print("\n--- KNOB ---")
# Find the knob by looking for a large dark blob in the left-center area
knob_center = find_blob_center(dark_mask, (50, 800), (600, 1200), min_area=20000)
if knob_center:
    kcx, kcy = knob_center
    print(f"  Center: ({kcx*sx:.1f}, {kcy*sy:.1f})")
    # Find radius by scanning outward
    for r in range(50, 400):
        if not dark_mask[int(kcy), int(kcx) + r]:
            print(f"  Radius: {r*sx:.1f}")
            break

# ── MAIN DISPLAY (large dark rectangle, upper area) ──
print("\n--- MAIN DISPLAY ---")
disp_center = find_blob_center(dark_mask, (50, 1300), (100, 600), min_area=50000)
if disp_center:
    dcx, dcy = disp_center
    # Find exact edges by scanning from center
    row = dark_mask[int(dcy)]
    left = right = int(dcx)
    while left > 0 and row[left]: left -= 1
    while right < w-1 and row[right]: right += 1
    col = dark_mask[:, int(dcx)]
    top = bot = int(dcy)
    while top > 0 and col[top]: top -= 1
    while bot < h-1 and col[bot]: bot += 1
    print(f"  Bounds: x={left*sx:.1f} y={top*sy:.1f} w={(right-left)*sx:.1f} h={(bot-top)*sy:.1f}")
    print(f"  Center: ({dcx*sx:.1f}, {dcy*sy:.1f})")

# ── SUB DISPLAY ──
print("\n--- SUB DISPLAY ---")
# Sub display is to the right of knob, below M buttons
sub_center = find_blob_center(dark_mask, (500, 1300), (800, 1200), min_area=5000)
if sub_center:
    scx, scy = sub_center
    row = dark_mask[int(scy)]
    left = right = int(scx)
    while left > 0 and row[left]: left -= 1
    while right < w-1 and row[right]: right += 1
    col = dark_mask[:, int(scx)]
    top = bot = int(scy)
    while top > 0 and col[top]: top -= 1
    while bot < h-1 and col[bot]: bot += 1
    print(f"  Bounds: x={left*sx:.1f} y={top*sy:.1f} w={(right-left)*sx:.1f} h={(bot-top)*sy:.1f}")
    print(f"  Center: ({scx*sx:.1f}, {scy*sy:.1f})")

# ── JACKS (dark circles in right section) ──
print("\n--- JACKS (upper section) ---")
# Find jack column X positions by scanning horizontally through rows
for row_idx, test_y_vcv in enumerate([50, 97, 145, 193]):
    test_y = int(test_y_vcv / sy)
    runs = find_runs_h(dark_mask, test_y, 50, 200)
    jack_runs = [r for r in runs if r[2] > 1200]  # right section only
    if jack_runs:
        print(f"  Row {row_idx+1} (vcv_y~{test_y_vcv}): " +
              "  ".join(f"cx={r[2]*sx:.1f}" for r in jack_runs))

print("\n--- JACKS (ABCD section) ---")
for row_idx, test_y_vcv in enumerate([241, 290, 338]):
    test_y = int(test_y_vcv / sy)
    runs = find_runs_h(dark_mask, test_y, 50, 200)
    jack_runs = [r for r in runs if r[2] > 1200]
    if jack_runs:
        print(f"  Row {row_idx+1} (vcv_y~{test_y_vcv}): " +
              "  ".join(f"cx={r[2]*sx:.1f}" for r in jack_runs))

# ── JACK Y POSITIONS (scan vertically through OUT column) ──
print("\n--- JACK Y POSITIONS (OUT column, x~2150) ---")
vruns = find_runs_v(dark_mask, 2150, 50, 200)
for s, e, cy, h in vruns:
    print(f"  cy={cy*sy:.1f}  h={h*sy:.1f}")

# ── SELECT BUTTONS (grey squares, left of jack section) ──
print("\n--- SELECT BUTTONS ---")
# Look for grey squares around x=1300 (VCV ~265)
for test_y_vcv in [50, 97, 145, 193]:
    test_y = int(test_y_vcv / sy)
    runs = find_runs_h(grey_mask, test_y, 50, 200)
    sel_runs = [r for r in runs if 1200 < r[2] < 1500]
    if sel_runs:
        print(f"  Row vcv_y~{test_y_vcv}: " +
              "  ".join(f"cx={r[2]*sx:.1f} w={r[3]*sx:.1f}" for r in sel_runs))

# ── OUTPUT LEDS (small bright dots between select and G column) ──
print("\n--- LED POSITIONS ---")
# LEDs are tiny bright spots; look for white/bright pixels
bright_mask = (brightness > 220) & (alpha > 200)
for test_y_vcv in [50, 74, 97, 121, 145, 169, 193]:
    test_y = int(test_y_vcv / sy)
    runs = find_runs_h(bright_mask, test_y, 3, 30)
    led_runs = [r for r in runs if 1300 < r[2] < 1700]
    if led_runs:
        print(f"  vcv_y~{test_y_vcv}: " +
              "  ".join(f"cx={r[2]*sx:.1f}" for r in led_runs))

print("\n" + "=" * 60)
print("Done.")
