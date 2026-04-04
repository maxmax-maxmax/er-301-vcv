#!/usr/bin/env python3
"""Render Panel.png with CORRECTED widget position overlay."""
from PIL import Image, ImageDraw

img = Image.open("./Panel.png").convert("RGBA")
iw, ih = img.size  # 2246 x 1888

S = 2
out = img.resize((int(457.2*S), int(380*S)), Image.LANCZOS)
draw = ImageDraw.Draw(out)

def vcv(x, y):
    return int(x*S), int(y*S)

def rect(x, y, w, h, color, width=1):
    draw.rectangle([vcv(x,y), vcv(x+w,y+h)], outline=color, width=width)

def circ(cx, cy, r, color, width=1):
    draw.ellipse([vcv(cx-r, cy-r), vcv(cx+r, cy+r)], outline=color, width=width)

def dot(cx, cy, r, color):
    draw.ellipse([vcv(cx-r, cy-r), vcv(cx+r, cy+r)], fill=color)

BTN_W, BTN_H = 22, 22
JACK_R = 12.1
CYAN = (0, 255, 255, 180)
GREEN = (0, 255, 0, 180)
RED = (255, 0, 0, 200)
YELLOW = (255, 255, 0, 200)
MAGENTA = (255, 0, 255, 180)

# ══════════════════════════════════════════════════════════
# CORRECTED positions — adjusted from overlay inspection
# ══════════════════════════════════════════════════════════

# M button X centers (confirmed from image scan — these are good)
mbX = [26.8, 69.8, 113.3, 156.6, 200.1, 243.5]

# Button row Y centers
mbY  = 147.0
sbY  = 290.5
hbY  = 338.0

# Knob
knobCX = 84.0
knobCY = 213.0
knobR  = 32.0

# Main display — inner screen area
mainDispX = 20.0
mainDispY = 50.0
mainDispW = 230.0
mainDispH = 42.0

# Sub display — match black rectangle
subDispX = 128.0
subDispY = 194.0
subDispW = 120.0
subDispH = 48.0

# Jack columns (confirmed good)
jGX, jINX, jOUTX = 350.5, 393.9, 436.8
jUpperY = [50.0, 97.5, 145.4, 193.2]
jAX, jBX, jCX, jDX = 307.0, 350.5, 393.5, 436.7
jABCDY = [241.5, 290.1, 337.8]

# Select buttons — align with actual grey squares
selBtnX = 256.0

# Output/link LEDs
outLedX = 275.0

# Toggle switches
tStorageX = 8.0
tStorageY = hbY - 16   # center vertically on hard button row
tModeX = 90.0
tModeY = tStorageY

# Fine/coarse LEDs
fineX = 30.0    # was 36
fineY = 253.0
coarseY = 265.0

# I/O, safe LEDs
ioLedX = 74.0   # was 78
ioLedY = tStorageY + 9
safeLedY = tStorageY + 24

# ── Draw everything ──

# Buttons
for x in mbX:
    rect(x-BTN_W/2, mbY-BTN_H/2, BTN_W, BTN_H, CYAN)
for x in mbX:
    rect(x-BTN_W/2, sbY-BTN_H/2, BTN_W, BTN_H, CYAN)
for x in mbX[3:]:
    rect(x-BTN_W/2, hbY-BTN_H/2, BTN_W, BTN_H, CYAN)

# Select buttons
for y in jUpperY:
    rect(selBtnX-BTN_W/2, y-BTN_H/2, BTN_W, BTN_H, MAGENTA)

# Knob
circ(knobCX, knobCY, knobR, CYAN, 2)
# crosshair
draw.line([vcv(knobCX-4, knobCY), vcv(knobCX+4, knobCY)], fill=CYAN, width=1)
draw.line([vcv(knobCX, knobCY-4), vcv(knobCX, knobCY+4)], fill=CYAN, width=1)

# Displays
rect(mainDispX, mainDispY, mainDispW, mainDispH, RED, 2)
rect(subDispX, subDispY, subDispW, subDispH, RED, 2)

# Jacks
for y in jUpperY:
    circ(jGX, y, JACK_R, GREEN, 2)
    circ(jINX, y, JACK_R, GREEN, 2)
    circ(jOUTX, y, JACK_R, GREEN, 2)
for x in [jAX, jBX, jCX, jDX]:
    for y in jABCDY:
        circ(x, y, JACK_R, GREEN, 2)

# Output LEDs
for y in jUpperY:
    dot(outLedX, y, 3, YELLOW)
for i in range(3):
    dot(outLedX, (jUpperY[i]+jUpperY[i+1])/2, 3, YELLOW)

# CV LEDs
for x in [jAX, jBX, jCX, jDX]:
    for y in jABCDY:
        dot(x-15, y-15, 2, YELLOW)

# Fine/coarse
dot(fineX, fineY, 3, YELLOW)
dot(fineX, coarseY, 3, YELLOW)

# I/O, safe
dot(ioLedX, ioLedY, 3, YELLOW)
dot(ioLedX, safeLedY, 3, YELLOW)

# Toggles
rect(tStorageX, tStorageY, 36, 32, MAGENTA)
rect(tModeX, tModeY, 36, 32, MAGENTA)

out.save("./overlay.png")
print("Saved overlay.png")
