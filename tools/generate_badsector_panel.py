#!/usr/bin/env python3
"""Generates the Bad Sector faceplate (res/BadSector.svg) and all custom
component SVGs (res/components/*.svg): black anodised plate, off-white
ShareTechMono industrial typography, orange hazard markings, cyan diagnostics.
Text is baked to paths (nanosvg drops <text>). Keep the layout constants in
sync with src/BadSector.cpp (they are mirrored there for widget positions)."""
import os, math
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

BASE = os.path.dirname(os.path.abspath(__file__))
FONT_PATH = r"C:\Program Files\VCV\Rack2Pro\res\fonts\ShareTechMono-Regular.ttf"
font = TTFont(FONT_PATH)
upem = font["head"].unitsPerEm
cmap = font.getBestCmap()
gset = font.getGlyphSet()
hmtx = font["hmtx"]

# ------------------------------------------------------------ palette ----
BG = "#0e0f12"
EDGE = "#1c1e23"
INK = "#ece8dd"      # off-white
DIM = "#8f8c83"
ORANGE = "#e8641e"
CYAN = "#35d3e0"

W, H = 81.28, 128.5  # 16HP

# ------------------------------------------------------------- layout ----
KNOBS = {  # centre positions - keep in sync with src/BadSector.cpp
    "TIME": (15.0, 25.0), "REPEAT": (66.28, 25.0),
    "MIX": (15.0, 46.5), "MICRO": (66.28, 46.5),
    "DAMAGE": (15.0, 68.0), "CV AMT": (66.28, 68.0),
}
SEL1, SEL2 = (33.6, 68.0), (47.7, 68.0)          # square selector buttons
MODES = [(34.0, 54.4), (40.64, 54.4), (47.3, 54.4)]  # MODE / CLK / FRZ
JX = [10.2, 22.86, 35.52, 48.18, 60.84, 73.5]
CVY, GATEY, AUY = 89.0, 101.0, 116.5
AUX = [14.0, 31.7, 49.5, 67.2]

def text_path(x, y, s, h, color, anchor="middle", spacing=0.0, weight=0.0):
    scale = h / upem
    advs = [hmtx[cmap.get(ord(ch), ".notdef")][0] if ord(ch) in cmap else upem // 2 for ch in s]
    total = sum(advs) * scale + spacing * (len(s) - 1)
    x0 = x - total / 2.0 if anchor == "middle" else (x - total if anchor == "end" else x)
    pen = SVGPathPen(gset)
    penx = x0
    for ch, adv in zip(s, advs):
        gn = cmap.get(ord(ch))
        if gn:
            tpen = TransformPen(pen, (scale, 0, 0, -scale, penx, y))
            gset[gn].draw(tpen)
        penx += adv * scale + spacing
    d = pen.getCommands()
    if not d:
        return ""
    stroke = f' stroke="{color}" stroke-width="{weight}"' if weight > 0 else ""
    return f'<path d="{d}" fill="{color}"{stroke}/>'

svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">']
svg.append(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
# glow geometry + colours sampled from the ComfyUI render (badsector_plate.png)
svg.append('<defs><radialGradient id="anod" gradientUnits="userSpaceOnUse" cx="36.6" cy="10.0" r="112">'
           '<stop  offset="0" style="stop-color:#404046"/>'
           '<stop  offset="0.38" style="stop-color:#232327"/>'
           '<stop  offset="1" style="stop-color:#131218"/></radialGradient></defs>')
svg.append(f'<rect width="{W}" height="{H}" fill="url(#anod)"/>')
svg.append(f'<rect x="0.3" y="0.3" width="{W-0.6}" height="{H-0.6}" fill="none" stroke="{EDGE}" stroke-width="0.6"/>')

# ---- anodised aluminium grain: seeded speckle + faint brushed streaks ----
import random as _r
_r.seed(1618)
for i in range(650):
    gx, gy = _r.uniform(0.8, W - 1.1), _r.uniform(0.8, H - 1.1)
    gs = _r.uniform(0.12, 0.32)
    col = _r.choice(["#121317", "#0b0c0f", "#15161b", "#0a0b0d", "#111215"])
    svg.append(f'<rect x="{gx:.2f}" y="{gy:.2f}" width="{gs:.2f}" height="{gs:.2f}" fill="{col}"/>')
for i in range(44):
    gx, gy0 = _r.uniform(0.8, W - 1.0), _r.uniform(0.0, H - 16.0)
    gl = _r.uniform(4.0, 12.0)
    col = _r.choice(["#1a1b20", "#26272c"])
    svg.append(f'<rect x="{gx:.2f}" y="{gy0:.2f}" width="0.10" height="{gl:.2f}" fill="{col}"/>')

# ---- header ----
svg.append(text_path(W / 2, 6.4, "halfagiraf", 3.0, DIM, spacing=0.55))
svg.append(text_path(W / 2, 13.6, "BAD SECTOR", 6.2, INK, spacing=0.35, weight=0.22))
# CHK diagnostic cluster
svg.append(f'<circle cx="9.0" cy="9.6" r="0.8" fill="{CYAN}"/>')
svg.append(f'<path d="M 7.2 7.4 L 7.2 6.2 L 10.6 6.2" fill="none" stroke="{DIM}" stroke-width="0.25"/>')
svg.append(text_path(11.4, 10.4, "CHK", 1.9, DIM, anchor="start"))
svg.append(text_path(11.4, 12.7, "0xE7", 1.9, DIM, anchor="start"))
svg.append(text_path(11.4, 15.0, "A3", 1.9, DIM, anchor="start"))
# halfagiraf logo (both colours), large, flush to the top-right edge
LOGO_MAIN = "M 144.456 7.255 C 144.184 7.964, 144.082 18.884, 144.231 31.522 L 144.500 54.500 151.250 54.796 L 158 55.091 158 84.046 L 158 113 170.500 113 L 183 113 183 84.042 L 183 55.084 190.750 54.792 L 198.500 54.500 198.500 30.500 L 198.500 6.500 171.725 6.234 C 150.612 6.024, 144.845 6.240, 144.456 7.255 M 232 30.500 L 232 55 239 55 L 246 55 246 84 L 246 113 258.864 113 L 271.727 113 272.265 102.750 C 272.561 97.112, 272.669 86.425, 272.506 79 C 272.342 71.575, 272.438 63.138, 272.719 60.250 L 273.230 55 280.115 55 L 287 55 287 30.500 L 287 6 259.500 6 L 232 6 232 30.500 M 6.477 117.678 C 9.269 143.937, 26.269 168.765, 49.385 180.345 C 63.322 187.326, 68.877 188.197, 100.500 188.358 L 128.500 188.500 128.766 162.430 L 129.033 136.360 118.106 130.106 C 88.827 113.347, 82.068 111.722, 39.626 111.241 L 5.752 110.856 6.477 117.678 M 148.300 148 C 148.300 151.025, 148.487 152.262, 148.716 150.750 C 148.945 149.238, 148.945 146.762, 148.716 145.250 C 148.487 143.738, 148.300 144.975, 148.300 148 M 272.402 188 C 272.402 196.525, 272.556 200.012, 272.743 195.750 C 272.931 191.488, 272.931 184.512, 272.743 180.250 C 272.556 175.988, 272.402 179.475, 272.402 188 M 148.425 195 C 148.425 206.825, 148.569 211.662, 148.746 205.750 C 148.923 199.838, 148.923 190.162, 148.746 184.250 C 148.569 178.338, 148.425 183.175, 148.425 195 M 272.434 240.500 C 272.433 254.250, 272.574 260.014, 272.747 253.308 C 272.919 246.603, 272.920 235.353, 272.748 228.308 C 272.576 221.264, 272.434 226.750, 272.434 240.500 M 148.409 256.500 C 148.408 265.850, 148.558 269.810, 148.743 265.299 C 148.928 260.789, 148.929 253.139, 148.745 248.299 C 148.562 243.460, 148.410 247.150, 148.409 256.500 M 192.089 252.089 C 186.168 258.963, 182.104 264.490, 182.611 264.979 C 184.178 266.487, 232.948 285.893, 233.388 285.182 C 233.762 284.577, 205.861 243.461, 203.419 241.019 C 202.800 240.400, 198.744 244.363, 192.089 252.089 M 272.468 334 C 272.468 365.075, 272.594 377.788, 272.749 362.250 C 272.904 346.713, 272.904 321.288, 272.749 305.750 C 272.594 290.213, 272.468 302.925, 272.468 334 M 144.242 367.750 L 144.500 426.500 171.609 426.766 L 198.718 427.032 199.350 421.766 C 199.698 418.870, 199.987 408.593, 199.991 398.930 L 200 381.360 172.901 345.180 C 157.996 325.281, 145.392 309, 144.893 309 C 144.355 309, 144.089 332.998, 144.242 367.750 M 217 493.500 L 217 578 244.500 578 L 272 578 272 528.422 L 272 478.845 264.750 469.547 C 260.762 464.434, 248.769 448.719, 238.099 434.625 C 227.428 420.531, 218.316 409, 217.849 409 C 217.382 409, 217 447.025, 217 493.500 M 272.320 438 C 272.320 441.575, 272.502 443.038, 272.723 441.250 C 272.945 439.462, 272.945 436.538, 272.723 434.750 C 272.502 432.962, 272.320 434.425, 272.320 438 M 145 596.500 L 145 639.032 172.250 638.766 L 199.500 638.500 199.500 596.500 L 199.500 554.500 172.250 554.234 L 145 553.968 145 596.500 M 71 670 L 71 703 100 703 L 129 703 129 670 L 129 637 100 637 L 71 637 71 670 M 246.486 693.981 C 198.343 704.867, 159.739 751.267, 158.695 799.500 L 158.500 808.500 215.500 808.500 L 272.500 808.500 272.813 750.250 L 273.125 692 263.813 692.084 C 258.691 692.131, 250.894 692.984, 246.486 693.981 M 71 764.500 L 71 809.031 99.750 808.765 L 128.500 808.500 128.500 764.500 L 128.500 720.500 99.750 720.235 L 71 719.969 71 764.500"
LOGO_GOLD = "M 149 208.710 L 149 285.134 167.805 310.317 C 178.148 324.168, 191.063 341.575, 196.505 349 C 210.688 368.351, 226.481 389.720, 250.500 422.060 L 271.500 450.336 271.753 292.005 C 271.892 204.922, 271.742 133.409, 271.420 133.086 C 271.097 132.764, 243.421 132.452, 209.917 132.393 L 149 132.286 149 208.710 M 192.089 252.089 C 186.168 258.963, 182.104 264.490, 182.611 264.979 C 184.178 266.487, 232.948 285.893, 233.388 285.182 C 233.762 284.577, 205.861 243.461, 203.419 241.019 C 202.800 240.400, 198.744 244.363, 192.089 252.089 M 145 490.525 L 145 536.049 172 535.967 L 199 535.885 199 490.443 L 199 445 172 445 L 145 445 145 490.525 M 217 635 L 217 674 244.500 674 L 272 674 272 635 L 272 596 244.500 596 L 217 596 217 635 M 145 682.500 L 145 709 165.757 709 L 186.514 709 192 704.500 C 195.017 702.025, 197.827 700, 198.243 700 C 198.659 700, 199 690.100, 199 678 L 199 656 172 656 L 145 656 145 682.500"
_ls = 14.5 / 815.0
_tx = W - 0.6 - 293.0 * _ls
svg.append(f'<g transform="translate({_tx:.3f}, 8.6) scale({_ls:.6f})">'
           f'<path d="{LOGO_MAIN}" fill="{INK}"/>'
           f'<path d="{LOGO_GOLD}" fill="#df9a31"/></g>')

# ---- knob markers + labels ----
for name, (x, y) in KNOBS.items():
    svg.append(f'<circle cx="{x}" cy="{y - 7.3}" r="0.6" fill="{INK}"/>')
    svg.append(text_path(x, y + 10.4, name, 2.7, INK, spacing=0.5))

# ---- hazard bar beside DAMAGE (reference style) ----
def _clip_band(rect, c0, c1):
    def clip(poly, fn):
        out = []
        for i in range(len(poly)):
            a, b = poly[i], poly[(i + 1) % len(poly)]
            fa, fb = fn(a), fn(b)
            if fa >= 0: out.append(a)
            if (fa >= 0) != (fb >= 0):
                t = fa / (fa - fb)
                out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
        return out
    poly = clip(rect, lambda pt: (pt[0] + pt[1]) - c0)
    return clip(poly, lambda pt: c1 - (pt[0] + pt[1]))

BX0, BX1, BY0, BY1 = 3.6, 7.7, 59.0, 77.8   # the bar
STRIPE_Y1 = 71.8                              # stripes stop; triangle zone below
svg.append(f'<rect x="{BX0}" y="{BY0}" width="{BX1-BX0}" height="{BY1-BY0}" fill="#0a0b0e"/>')
srect = [(BX0, BY0), (BX1, BY0), (BX1, STRIPE_Y1), (BX0, STRIPE_Y1)]
c = BX0 + BY0 - 3.4
while c < BX1 + STRIPE_Y1:
    poly = _clip_band(srect, c, c + 1.7)
    if len(poly) >= 3:
        pts = " ".join(f"{'M' if i == 0 else 'L'} {x:.2f} {y:.2f}" for i, (x, y) in enumerate(poly))
        svg.append(f'<path d="{pts} Z" fill="{ORANGE}"/>')
    c += 3.4
svg.append(f'<rect x="{BX0}" y="{BY0}" width="{BX1-BX0}" height="{BY1-BY0}" fill="none" stroke="{ORANGE}" stroke-width="0.4"/>')
svg.append(f'<path d="M {BX0} {STRIPE_Y1} L {BX1} {STRIPE_Y1}" stroke="{ORANGE}" stroke-width="0.3"/>')
# warning triangle in the solid zone at the foot of the bar
svg.append(f'<path d="M 5.65 72.9 L 7.2 76.1 L 4.1 76.1 Z" fill="none" stroke="{ORANGE}" stroke-width="0.45" stroke-linejoin="round"/>')
svg.append(f'<rect x="5.43" y="73.8" width="0.44" height="1.1" fill="{ORANGE}"/>')
svg.append(f'<rect x="5.43" y="75.2" width="0.44" height="0.4" fill="{ORANGE}"/>')
# dashes + block, right of the DAMAGE label
for i in range(2):
    svg.append(f'<rect x="{22.6 + i * 3.4}" y="76.6" width="2.2" height="0.7" fill="{ORANGE}"/>')
svg.append(f'<rect x="26.4" y="76.0" width="1.7" height="1.7" fill="{ORANGE}"/>')

# ---- selector buttons: dots + labels ----
for (x, y), lab in [(SEL1, "DMG"), (SEL2, "CV")]:
    svg.append(text_path(x, y + 7.9, lab, 1.6, DIM, spacing=0.4))
for (x, y), lab in zip(MODES, ["MODE", "CLK", "FRZ"]):
    svg.append(text_path(x, y + 4.6, lab, 1.4, DIM, spacing=0.3))

# ---- jack labels ----
for x, lab in zip(JX, ["TIME", "REPEAT", "MIX", "BEND", "BREAK", "CRPT"]):
    svg.append(text_path(x, CVY - 4.9, lab, 2.2, DIM, spacing=0.15))
svg.append(text_path(1.6, CVY + 0.7, "CV", 1.8, DIM, anchor="start"))
svg.append(f'<path d="M 4.6 {CVY} L 73.5 {CVY}" stroke="#34363c" stroke-width="0.35" fill="none"/>')
for x, lab in zip(JX[2:], ["FRZ", "BEND", "BREAK", "CRPT"]):
    svg.append(text_path(x, GATEY - 4.9, lab, 2.2, DIM, spacing=0.15))
svg.append(text_path(1.6, GATEY + 0.7, "GATE", 1.8, DIM, anchor="start"))
svg.append(f'<path d="M 5.6 {GATEY} L 47.7 {GATEY}" stroke="#34363c" stroke-width="0.35" fill="none"/>')
for x, lab in zip(JX, ["IN L", "IN R", "CLOCK", "RESET", "OUT L", "OUT R"]):
    svg.append(text_path(x, AUY - 5.3, lab, 1.9, INK, spacing=0.25))

# ---- footers ----
svg.append(f'<rect x="12.4" y="123.2" width="1.9" height="1.9" fill="none" stroke="{DIM}" stroke-width="0.3"/>')
svg.append(text_path(15.4, 124.9, "16HP v2.0", 1.7, DIM, anchor="start"))
svg.append(f'<rect x="50.2" y="123.2" width="1.9" height="1.9" fill="none" stroke="{DIM}" stroke-width="0.3"/>')
svg.append(text_path(53.2, 124.9, "BS-16 25/07", 1.7, DIM, anchor="start"))

svg.append("</svg>")
os.makedirs(os.path.join(BASE, "res", "components"), exist_ok=True)
open(os.path.join(BASE, "res", "BadSector.svg"), "w").write("\n".join(svg))

# ================================================== component SVGs =====
def write(name, size, body):
    s = f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}mm" height="{size}mm" viewBox="0 0 {size} {size}">\n'
    s += "\n".join(body) + "\n</svg>\n"
    open(os.path.join(BASE, "res", "components", name), "w").write(s)

# knob: rounded dome cap like the concept art — ribbed skirt, high light,
# specular crown, exclamation pointer (12.5 mm)
k = []
c = 6.25
k.append('<defs>')
k.append('<radialGradient id="dome" gradientUnits="userSpaceOnUse" cx="6.25" cy="4.4" r="6.6">'
         '<stop  offset="0" style="stop-color:#3b3e46"/>'
         '<stop  offset="0.45" style="stop-color:#1c1e24"/>'
         '<stop  offset="0.8" style="stop-color:#0e0f13"/>'
         '<stop  offset="1" style="stop-color:#07080b"/></radialGradient>')
k.append('<radialGradient id="gloss" gradientUnits="userSpaceOnUse" cx="6.25" cy="3.6" r="3.1">'
         '<stop  offset="0" style="stop-color:#4d505a"/>'
         '<stop  offset="1" style="stop-color:#23252b"/></radialGradient>')
k.append('<radialGradient id="skirt" gradientUnits="userSpaceOnUse" cx="6.25" cy="6.25" r="6.2">'
         '<stop  offset="0.75" style="stop-color:#101116"/>'
         '<stop  offset="1" style="stop-color:#040506"/></radialGradient>')
k.append('</defs>')
# ribbed skirt
k.append(f'<circle cx="{c}" cy="{c}" r="6.1" fill="url(#skirt)"/>')
for i in range(32):
    a = (i + 0.5) * math.pi * 2 / 32
    x0, y0 = c + math.cos(a) * 4.95, c + math.sin(a) * 4.95
    x1, y1 = c + math.cos(a) * 6.02, c + math.sin(a) * 6.02
    col = "#181a20" if i % 2 == 0 else "#07080a"
    k.append(f'<path d="M {x0:.3f} {y0:.3f} L {x1:.3f} {y1:.3f}" stroke="{col}" stroke-width="0.62"/>')
# groove, then the dome
k.append(f'<circle cx="{c}" cy="{c}" r="4.85" fill="#050608"/>')
k.append(f'<circle cx="{c}" cy="{c}" r="4.55" fill="url(#dome)"/>')
# specular crown high on the dome
k.append(f'<circle cx="{c}" cy="3.95" r="2.35" fill="url(#gloss)"/>')
# rim light on the top edge
k.append('<path d="M 2.6 4.5 A 4.55 4.55 0 0 1 9.9 4.5" stroke="#565a64" stroke-width="0.28" fill="none"/>')
# pointer: line to the rim with a dot at its centre end
k.append(f'<rect x="{c - 0.3}" y="1.55" width="0.6" height="3.15" rx="0.3" fill="#f2efe6"/>')
k.append(f'<circle cx="{c}" cy="5.5" r="0.68" fill="#f2efe6"/>')
write("knob.svg", 12.5, k)

# screw: black torx (5 mm)
s5 = []
s5.append('<circle cx="2.5" cy="2.5" r="2.4" fill="#101114"/>')
s5.append('<circle cx="2.5" cy="2.5" r="2.4" fill="none" stroke="#2a2d33" stroke-width="0.25"/>')
pts = []
for i in range(12):
    a = i * math.pi / 6
    r = 1.35 if i % 2 == 0 else 0.62
    pts.append(f'{2.5 + math.cos(a) * r:.3f} {2.5 + math.sin(a) * r:.3f}')
s5.append(f'<path d="M {" L ".join(pts)} Z" fill="none" stroke="#3a3d45" stroke-width="0.3" stroke-linejoin="round"/>')
write("screw.svg", 5, s5)

# port.svg is the ComfyUI-rendered hex jack (D:/comfy/panels/jack.svg,
# seed 21), traced by vtracer and chroma-keyed — not generated here

# square LED button, unpressed / pressed (7.5 mm)
for frame, inset in (("sqbtn_0.svg", 0.0), ("sqbtn_1.svg", 0.28)):
    b = []
    b.append('<rect x="0.15" y="0.15" width="7.2" height="7.2" rx="0.9" fill="#0a0b0e"/>')
    b.append('<rect x="0.15" y="0.15" width="7.2" height="7.2" rx="0.9" fill="none" stroke="#26282f" stroke-width="0.3"/>')
    i0 = 1.25 + inset
    sz = 5.0 - inset * 2
    b.append(f'<rect x="{i0}" y="{i0}" width="{sz}" height="{sz}" rx="0.5" fill="#191b20"/>')
    write(frame, 7.5, b)

# small square LED button, unpressed / pressed (5.8 mm)
for frame, inset in (("sqbtn_s0.svg", 0.0), ("sqbtn_s1.svg", 0.22)):
    b = []
    b.append('<rect x="0.12" y="0.12" width="5.56" height="5.56" rx="0.7" fill="#0a0b0e"/>')
    b.append('<rect x="0.12" y="0.12" width="5.56" height="5.56" rx="0.7" fill="none" stroke="#26282f" stroke-width="0.28"/>')
    i0 = 1.0 + inset
    sz = 3.8 - inset * 2
    b.append(f'<rect x="{i0}" y="{i0}" width="{sz}" height="{sz}" rx="0.4" fill="#191b20"/>')
    write(frame, 5.8, b)

print("wrote faceplate + components")
