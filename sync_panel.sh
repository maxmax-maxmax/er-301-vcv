#!/bin/bash
# sync_panel.sh — Sync ALL widget positions from SVG panel to C++ source
# Usage: ./sync_panel.sh [--build]
#
# Reads component positions directly from the SVG (with label mapping),
# then updates mm2px() coordinates in ER301Module.cpp.

set -e
cd "$(dirname "$0")"

SVG="res/ER301.svg"
CPP="src/ER301Module.cpp"

echo ">>> Syncing positions from $SVG to $CPP..."

python3 - "$SVG" "$CPP" << 'PYEOF'
import sys, re, xml.etree.ElementTree as ET

svg_file = sys.argv[1]
cpp_file = sys.argv[2]

tree = ET.parse(svg_file)
root = tree.getroot()
ns = {'svg': 'http://www.w3.org/2000/svg',
      'inkscape': 'http://www.inkscape.org/namespaces/inkscape'}

# Find components layer
comp_layer = None
for g in root.findall('.//svg:g', ns):
    if g.get('{http://www.inkscape.org/namespaces/inkscape}label') == 'components':
        comp_layer = g
        break

if not comp_layer:
    print("ERROR: No 'components' layer found!")
    sys.exit(1)

# SVG viewBox -> mm conversion
vb = root.get('viewBox').split()
vb_w, vb_h = float(vb[2]), float(vb[3])

# Document size in mm
w_str = root.get('width', '152.4mm')
h_str = root.get('height', '128.5mm')
doc_w = float(re.match(r'([\d.]+)', w_str).group(1))
doc_h = float(re.match(r'([\d.]+)', h_str).group(1))

sx = doc_w / vb_w  # viewBox units -> mm
sy = doc_h / vb_h

def get_transform(elem):
    """Parse a matrix transform from an element or its ancestors up to comp_layer."""
    t = elem.get('transform', '')
    m = re.match(r'matrix\(([\d.e+-]+),([\d.e+-]+),([\d.e+-]+),([\d.e+-]+),([\d.e+-]+),([\d.e+-]+)\)', t.replace(' ', ''))
    if m:
        return [float(x) for x in m.groups()]
    return None

def apply_transform(x, y, matrix):
    """Apply SVG matrix transform."""
    if matrix:
        a, b, c, d, e, f = matrix
        return a * x + c * y + e, b * x + d * y + f
    return x, y

# Collect all labeled elements
components = {}
for elem in comp_layer:
    label = elem.get('{http://www.inkscape.org/namespaces/inkscape}label', '')
    tag = elem.tag.split('}')[-1] if '}' in elem.tag else elem.tag

    # Check for group transform (some elements are in transformed groups)
    transform = get_transform(elem)

    if tag == 'circle' or tag == 'ellipse':
        cx = float(elem.get('cx', 0))
        cy = float(elem.get('cy', 0))
        cx, cy = apply_transform(cx, cy, transform)
        r = float(elem.get('r', 0))
        components[label] = {'type': 'circle', 'cx': cx * sx, 'cy': cy * sy, 'r': r}
    elif tag == 'rect':
        x = float(elem.get('x', 0))
        y = float(elem.get('y', 0))
        w = float(elem.get('width', 0))
        h = float(elem.get('height', 0))
        x, y = apply_transform(x, y, transform)
        # Also transform the size if there's a scale
        if transform:
            a, b, c, d, e, f = transform
            w *= a
            h *= d
        else:
            w *= sx
            h *= sy
            x *= sx
            y *= sy
        components[label] = {'type': 'rect', 'x': x if transform else x, 'y': y if transform else y, 'w': w, 'h': h}
        if transform:
            components[label]['x'] = x * sx / (transform[0] if transform else 1) * transform[0]
            # Simplified: just use the final transformed coords in mm
            components[label]['x'] = (transform[0] * float(elem.get('x', 0)) + transform[4]) * sx
            components[label]['y'] = (transform[3] * float(elem.get('y', 0)) + transform[5]) * sy

if not components:
    print("ERROR: No labeled components found!")
    sys.exit(1)

with open(cpp_file, 'r') as f:
    cpp = f.read()

changes = 0

# === Map SVG labels to C++ patterns ===

# Inputs/Outputs/Lights: matched by enum name (handled by mm2px(Vec(...)), module, ER301Module::NAME)
io_map = {
    'G1': 'G1_INPUT', 'G2': 'G2_INPUT', 'G3': 'G3_INPUT', 'G4': 'G4_INPUT',
    'In1': 'IN1_INPUT', 'In2': 'IN2_INPUT', 'In3': 'IN3_INPUT', 'In4': 'IN4_INPUT',
    'A1': 'A1_INPUT', 'A2': 'A2_INPUT', 'A3': 'A3_INPUT',
    'B1': 'B1_INPUT', 'B2': 'B2_INPUT', 'B3': 'B3_INPUT',
    'C1': 'C1_INPUT', 'C2': 'C2_INPUT', 'C3': 'C3_INPUT',
    'D1': 'D1_INPUT', 'D2': 'D2_INPUT', 'D3': 'D3_INPUT',
    'Out1': 'OUT1_OUTPUT', 'Out2': 'OUT2_OUTPUT', 'Out3': 'OUT3_OUTPUT', 'Out4': 'OUT4_OUTPUT',
    'led-1': 'LED_1_LIGHT', 'led-2': 'LED_2_LIGHT', 'led-3': 'LED_3_LIGHT', 'led-4': 'LED_4_LIGHT',
    'linked1-2': 'LINKED1_2_LIGHT', 'linked2-3': 'LINKED2_3_LIGHT', 'linked3-4': 'LINKED3_4_LIGHT',
    'led-fine': 'LED_FINE_LIGHT', 'led-coarse': 'LED_COARSE_LIGHT',
    'led-I/o': 'LED_I_O_LIGHT', 'led-safe': 'LED_SAFE_LIGHT',
    'led-A1': 'LED_A1_GREEN', 'led-A2': 'LED_A2_GREEN', 'led-A3': 'LED_A3_GREEN',
    'led-B1': 'LED_B1_GREEN', 'led-B2': 'LED_B2_GREEN', 'led-B3': 'LED_B3_GREEN',
    'led-C1': 'LED_C1_GREEN', 'led-C2': 'LED_C2_GREEN', 'led-C3': 'LED_C3_GREEN',
    'led-D1': 'LED_D1_GREEN', 'led-D2': 'LED_D2_GREEN', 'led-D3': 'LED_D3_GREEN',
}

# Buttons: matched by GPIO name in createER301Button(BUTTON_XXX, mm2px(Vec(...)), ...)
button_map = {
    'Button-1': 'BUTTON_SELECT1', 'Button-2': 'BUTTON_SELECT2',
    'Button-3': 'BUTTON_SELECT3', 'Button-4': 'BUTTON_SELECT4',
    'Button-M1': 'BUTTON_MAIN1', 'Button-M2': 'BUTTON_MAIN2', 'Button-M3': 'BUTTON_MAIN3',
    'Button-M4': 'BUTTON_MAIN4', 'Button-M5': 'BUTTON_MAIN5', 'Button-M6': 'BUTTON_MAIN6',
    'Button-fine/coarse': 'BUTTON_DIAL1', 'Button-cancel': 'BUTTON_DIAL2', 'Button-zero': 'BUTTON_DIAL3',
    'Button-S1': 'BUTTON_SUB1', 'Button-S2': 'BUTTON_SUB2', 'Button-S3': 'BUTTON_SUB3',
    'Button-enter': 'BUTTON_ENTER', 'Button-up': 'BUTTON_UP', 'Button-shift': 'BUTTON_SHIFT',
}

# Update inputs/outputs/lights (centered, use cx/cy)
for svg_label, cpp_name in io_map.items():
    if svg_label not in components:
        continue
    c = components[svg_label]
    x_mm = f"{c['cx']:.3f}"
    y_mm = f"{c['cy']:.3f}"
    pattern = r'(mm2px\(Vec\()[\d.]+f,\s*[\d.]+f(\)\),\s*module,\s*ER301Module::' + cpp_name + r')'
    repl = rf'\g<1>{x_mm}f, {y_mm}f\2'
    new_cpp, n = re.subn(pattern, repl, cpp)
    if n > 0:
        cpp = new_cpp
        changes += n

# Update buttons (top-left, use rect x/y)
for svg_label, gpio_name in button_map.items():
    if svg_label not in components:
        continue
    c = components[svg_label]
    x_mm = f"{c['x']:.3f}"
    y_mm = f"{c['y']:.3f}"
    # Pattern: createER301Button(BUTTON_XXX, mm2px(Vec(X, Y)),
    pattern = r'(createER301Button\(' + gpio_name + r',\s*mm2px\(Vec\()[\d.]+f,\s*[\d.]+f(\)\))'
    repl = rf'\g<1>{x_mm}f, {y_mm}f\2'
    new_cpp, n = re.subn(pattern, repl, cpp)
    if n > 0:
        cpp = new_cpp
        changes += n

# Update screws (centered circles)
screw_map = {
    'Screw-1': 0, 'Screw-2': 1, 'Screw-3': 2, 'Screw-4': 3,
}
screw_positions = []
for label in ['Screw-1', 'Screw-2', 'Screw-3', 'Screw-4']:
    if label in components:
        c = components[label]
        screw_positions.append((c['cx'], c['cy']))

if len(screw_positions) == 4:
    # Sort: top-left, top-right, bottom-left, bottom-right
    screws_sorted = sorted(screw_positions, key=lambda p: (p[1], p[0]))  # sort by y then x
    top = sorted(screws_sorted[:2], key=lambda p: p[0])
    bottom = sorted(screws_sorted[2:], key=lambda p: p[0])
    screw_order = [top[0], top[1], bottom[0], bottom[1]]  # TL, TR, BL, BR

    # Find all ScrewSilver lines and replace in order
    pattern = r'(createWidgetCentered<ScrewSilver>\(mm2px\(Vec\()[\d.]+f,\s*[\d.]+f(\)\)\))'
    matches = list(re.finditer(pattern, cpp))
    if len(matches) == 4:
        for i, m in enumerate(reversed(matches)):  # replace from end to preserve offsets
            idx = 3 - i
            x_mm = f"{screw_order[idx][0]:.3f}"
            y_mm = f"{screw_order[idx][1]:.3f}"
            old = m.group(0)
            new = re.sub(r'Vec\([\d.]+f,\s*[\d.]+f\)', f'Vec({x_mm}f, {y_mm}f)', old)
            cpp = cpp[:m.start()] + new + cpp[m.end():]
            changes += 1

# Update knob (centered circle)
if 'Knob-Main' in components:
    c = components['Knob-Main']
    x_mm = f"{c['cx']:.3f}"
    y_mm = f"{c['cy']:.3f}"
    pattern = r'(knobCenter\s*=\s*mm2px\(Vec\()[\d.]+f,\s*[\d.]+f(\)\))'
    repl = rf'\g<1>{x_mm}f, {y_mm}f\2'
    new_cpp, n = re.subn(pattern, repl, cpp)
    if n > 0:
        cpp = new_cpp
        changes += n

# Update toggles (rects, top-left)
toggle_map = {
    'switch-storage': 'TOGGLE_STORAGE_A',
    'switch-mode': 'TOGGLE_MODE_A',
}
for svg_label, toggle_id in toggle_map.items():
    if svg_label not in components:
        continue
    c = components[svg_label]
    x_mm = f"{c['x']:.3f}"
    y_mm = f"{c['y']:.3f}"
    # Pattern: ER301Toggle(TOGGLE_XXX_A, ... box.pos = mm2px(Vec(X, Y))
    # We search for the toggle ID then find the next mm2px after it
    toggle_pattern = toggle_id + r'[^;]*?box\.pos\s*=\s*mm2px\(Vec\([\d.]+f,\s*[\d.]+f\)\)'
    match = re.search(toggle_pattern, cpp, re.DOTALL)
    if match:
        old = match.group(0)
        new = re.sub(r'mm2px\(Vec\([\d.]+f,\s*[\d.]+f\)\)', f'mm2px(Vec({x_mm}f, {y_mm}f))', old)
        cpp = cpp.replace(old, new)
        changes += 1

# Update display positions (rects)
if 'Screen-Large' in components:
    c = components['Screen-Large']
    x_mm = f"{c['x']:.3f}"
    y_mm = f"{c['y']:.3f}"
    w_mm = f"{c['w']:.3f}"
    h_mm = f"{c['h']:.3f}"
    # mainPos
    cpp = re.sub(
        r'(Vec mainPos = mm2px\(Vec\()[\d.]+f,\s*[\d.]+f(\)\))',
        rf'\g<1>{x_mm}f, {y_mm}f\2', cpp)
    cpp = re.sub(
        r'(Vec mainSize = mm2px\(Vec\()[\d.]+f,\s*[\d.]+f(\)\))',
        rf'\g<1>{w_mm}f, {h_mm}f\2', cpp)
    changes += 2

if 'Screen-small' in components:
    c = components['Screen-small']
    x_mm = f"{c['x']:.3f}"
    y_mm = f"{c['y']:.3f}"
    w_mm = f"{c['w']:.3f}"
    h_mm = f"{c['h']:.3f}"
    cpp = re.sub(
        r'(Vec subPos = mm2px\(Vec\()[\d.]+f,\s*[\d.]+f(\)\))',
        rf'\g<1>{x_mm}f, {y_mm}f\2', cpp)
    cpp = re.sub(
        r'(Vec subSize = mm2px\(Vec\()[\d.]+f,\s*[\d.]+f(\)\))',
        rf'\g<1>{w_mm}f, {h_mm}f\2', cpp)
    changes += 2

with open(cpp_file, 'w') as f:
    f.write(cpp)

print(f"   Updated {changes} positions")
print(f"   Components found: {len(components)}")
found_labels = sorted(components.keys())
print(f"   Labels: {', '.join(found_labels)}")

PYEOF

# Optionally build
if [ "$1" = "--build" ]; then
    echo ">>> Building..."
    PATH="/opt/homebrew/bin:$PATH" make install
fi

echo ">>> Done!"
