# Kick model icons (32x17, 1-bit), hand-traced from artifacts/kick_icons_sheet.png.
# Prints ASCII, writes build/kick_icons_preview.png and build/kick_icons.json (hex rows for
# KICK_ART in src/ui/canvas.js). Needs Pillow + numpy.
import math, json, numpy as np
from PIL import Image
W, H = 32, 17
def new(): return np.zeros((H, W), int)
def px(g, x, y):
    if 0 <= x < W and 0 <= y < H: g[y, x] = 1
def line(g, x0, y0, x1, y1):
    dx, dy = abs(x1 - x0), -abs(y1 - y0); sx = 1 if x0 < x1 else -1; sy = 1 if y0 < y1 else -1; e = dx + dy
    while True:
        px(g, x0, y0)
        if x0 == x1 and y0 == y1: break
        e2 = 2 * e
        if e2 >= dy: e += dy; x0 += sx
        if e2 <= dx: e += dx; y0 += sy
def poly(g, pts):
    for a, b in zip(pts, pts[1:]): line(g, *a, *b)
def rect(g, x0, y0, x1, y1): poly(g, [(x0, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0)])
def fill(g, x0, y0, x1, y1): g[y0:y1 + 1, x0:x1 + 1] = 1
def rrect(g, x0, y0, x1, y1):
    line(g, x0 + 1, y0, x1 - 1, y0); line(g, x0 + 1, y1, x1 - 1, y1)
    line(g, x0, y0 + 1, x0, y1 - 1); line(g, x1, y0 + 1, x1, y1 - 1)
def curve(g, f, xa, xb):
    pts = [(x, int(round(f(x)))) for x in range(xa, xb + 1)]; poly(g, pts)

icons = {}
g = new()                                            # FM2
rect(g, 3, 1, 14, 10)
poly(g, [(4, 8), (5, 8), (5, 4), (7, 4), (7, 8), (10, 8), (10, 4), (12, 4), (12, 8), (13, 8)])
poly(g, [(15, 5), (18, 5), (21, 8)])
rect(g, 17, 9, 29, 15)
poly(g, [(18, 14), (19, 14), (19, 11), (21, 11), (21, 14), (24, 14), (24, 11), (26, 11), (26, 14), (28, 14)])
icons["FM2"] = g
g = new()                                            # FM4
for (x, y) in [(3, 1), (13, 1), (3, 11), (13, 11)]: rrect(g, x, y, x + 6, y + 4)
line(g, 10, 3, 12, 3); line(g, 10, 13, 12, 13)
line(g, 6, 6, 6, 10); line(g, 16, 6, 16, 10); line(g, 6, 8, 16, 8)
line(g, 20, 3, 22, 3); line(g, 22, 3, 22, 13); line(g, 20, 13, 22, 13)
line(g, 23, 8, 28, 8); px(g, 27, 7); px(g, 27, 9); px(g, 26, 6); px(g, 26, 10)
icons["FM4"] = g
g = new()                                            # WTR
curve(g, lambda x: 8 - 6 * math.sin((x - 2) / 21 * 2 * math.pi + 0.35), 2, 23)
for y in (2, 5, 8, 11, 14): line(g, 25, y, 29, y)
icons["WTR"] = g
g = new()                                            # PHY
line(g, 8, 1, 14, 5); line(g, 8, 2, 14, 6); fill(g, 15, 5, 17, 7)
line(g, 20, 5, 21, 3); line(g, 21, 7, 23, 6)
line(g, 7, 9, 25, 9); line(g, 6, 10, 26, 10)
line(g, 6, 11, 6, 14); line(g, 26, 11, 26, 14); line(g, 7, 12, 25, 12)
line(g, 10, 13, 10, 15); line(g, 21, 13, 21, 15); line(g, 9, 15, 11, 15); line(g, 20, 15, 22, 15)
icons["PHY"] = g
g = new()                                            # HRD
poly(g, [(1, 9), (5, 9), (7, 1), (9, 14), (11, 4), (12, 12), (14, 6), (15, 11), (17, 7), (18, 10), (20, 8), (21, 10), (22, 8), (23, 9), (30, 9)])
icons["HRD"] = g
g = new()                                            # DIG
poly(g, [(1, 11), (3, 11), (3, 5), (7, 5), (7, 11), (9, 11), (9, 3), (14, 3), (14, 7), (16, 7), (16, 13), (22, 13), (22, 5), (25, 5), (25, 8), (27, 8), (27, 10), (30, 10)])
for p in [(1, 3), (8, 1), (20, 2), (27, 1), (30, 5), (5, 15), (11, 15), (25, 15), (30, 13)]: px(g, *p)
icons["DIG"] = g
g = new()                                            # TRS
poly(g, [(1, 11), (4, 11), (5, 6), (6, 2), (7, 6), (8, 4), (9, 8), (13, 8), (13, 9), (19, 9), (19, 10), (24, 10), (24, 11), (30, 11)])
for x in (3, 9, 15, 21, 27): line(g, x, 13, x, 15)
icons["TRS"] = g
g = new()                                            # ANA
curve(g, lambda x: 12 - 10 * math.exp(-((x - 15.5) / 7.5) ** 4), 1, 30)
line(g, 1, 14, 30, 14)
icons["ANA"] = g
g = new()                                            # USR
line(g, 3, 2, 8, 2); px(g, 9, 3); line(g, 10, 3, 17, 3)
line(g, 3, 2, 3, 15); line(g, 17, 3, 17, 15); line(g, 3, 15, 17, 15); line(g, 3, 5, 17, 5)
for x, h in zip((19, 21, 23, 25, 27, 29), (1, 3, 5, 6, 3, 1)): line(g, x, 9 - h, x, 9 + h)
icons["USR"] = g

names = list(icons)
for n in names:
    print(n); [print("".join("#" if v else "." for v in r)) for r in icons[n]]
# preview: 3x3, 8x scale, grey tile frames
S, G = 8, 16
im = np.zeros((3 * (H * S + G) + G, 3 * (W * S + G) + G), np.uint8)
for i, n in enumerate(names):
    r, c = divmod(i, 3); y0, x0 = G + r * (H * S + G), G + c * (W * S + G)
    im[y0 - 1:y0 + H * S + 1, x0 - 1:x0 + W * S + 1] = 70
    im[y0:y0 + H * S, x0:x0 + W * S] = np.kron(icons[n], np.ones((S, S), int)) * 255
Image.fromarray(im).save("build/kick_icons_preview.png")
json.dump({n: ["%08x" % int("".join(map(str, r)), 2) for r in icons[n]] for n in names}, open("build/kick_icons.json", "w"))
