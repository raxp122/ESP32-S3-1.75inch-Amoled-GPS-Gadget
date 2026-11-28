#!/usr/bin/env python3
"""The tarot pack: 78 round cards in the neon style, the back, small previews and the meanings
(Italian and English). Writes the files into a folder; tools/packs/build.py packs it.

    python3 tools/packs/tarot.py OUT_DIR

Every card is drawn here (Pillow), nothing is copied from existing decks. The names of the
cards with a church background are replaced by secular ones (II The Seer, V The Teacher,
XV The Chains, XX The Awakening).
"""
import math, os, random, sys
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
POP = os.path.join(ROOT, "tools", "fonts", "Poppins-Medium.ttf")
SIZE, THUMB, QUALITY = 466, 128, 88
K = 3                       # supersampling
S = SIZE * K
INNER = 206                 # the picture stays inside this radius
CY = (60, 230, 255); MG = (255, 70, 200); MGD = (120, 40, 110); YL = (255, 214, 80); VI = (150, 110, 255)
WH = (235, 235, 255)


def P(x, y): return (x * K, y * K)


class Pen:
    def __init__(self, d): self.d = d

    def line(self, pts, col, w=2):
        self.d.line([P(*p) for p in pts], fill=col, width=max(1, int(w * K)), joint="curve")

    def circle(self, x, y, r, col, w=2, fill=None):
        self.d.ellipse([(x - r) * K, (y - r) * K, (x + r) * K, (y + r) * K], outline=col if w else None,
                       width=max(1, int(w * K)) if w else 0, fill=fill)

    def arc(self, x, y, r, a0, a1, col, w=2, ry=None):
        ry = r if ry is None else ry
        self.d.arc([(x - r) * K, (y - ry) * K, (x + r) * K, (y + ry) * K], a0, a1, fill=col, width=max(1, int(w * K)))

    def poly(self, pts, col, w=2, fill=None):
        if fill: self.d.polygon([P(*p) for p in pts], fill=fill)
        self.line(list(pts) + [pts[0]], col, w)

    def star(self, x, y, r, col, n=5, inner=0.4, w=2, rot=-90):
        pts = []
        for i in range(n * 2):
            rr = r if i % 2 == 0 else r * inner
            a = math.radians(rot + 180 * i / n)
            pts.append((x + rr * math.cos(a), y + rr * math.sin(a)))
        self.poly(pts, col, w)

    def dot(self, x, y, r, col): self.circle(x, y, r, col, 0, fill=col)

    def figure(self, x, y, s, col, w=2, arms=0):
        """a stick figure, head at (x, y); arms: 0 down, 1 open, 2 up"""
        self.circle(x, y, 7 * s, col, w)
        self.line([(x, y + 7 * s), (x, y + 34 * s)], col, w)
        ay = {0: 22, 1: 16, 2: 2}[arms]
        self.line([(x - 13 * s, y + ay * s), (x, y + 14 * s), (x + 13 * s, y + ay * s)], col, w)
        self.line([(x - 10 * s, y + 52 * s), (x, y + 34 * s), (x + 10 * s, y + 52 * s)], col, w)

    def grid(self, hz, col, spread=40, rows=7):
        self.line([(20, hz), (446, hz)], MG, 2)
        for k in range(1, rows):
            y = hz + (k ** 1.7) * 4.2
            self.line([(0, y), (466, y)], col, 1.3)
        for k in range(-9, 10):
            self.line([(233 + k * 12, hz), (233 + k * spread, hz + 170)], col, 1.3)


def gradient_disc(cx, cy, r, top, bottom, stripes=0, cut=None):
    img = Image.new("RGB", (S, S)); d = ImageDraw.Draw(img)
    for y in range(int((cy - r) * K), int((cy + r) * K) + 1):
        t = (y / K - (cy - r)) / (2 * r)
        d.line([(0, y), (S, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    m = Image.new("L", (S, S)); md = ImageDraw.Draw(m)
    md.ellipse([(cx - r) * K, (cy - r) * K, (cx + r) * K, (cy + r) * K], fill=255)
    for k in range(stripes):
        y = cy + r * 0.1 + k * r * 0.15
        md.rectangle([0, y * K, S, (y + 2 + k * 1.6) * K], fill=0)
    if cut: md.rectangle([0, cut * K, S, S], fill=0)
    return img, m


def spaced(d, y, text, col, size=20, sp=7):
    f = ImageFont.truetype(POP, int(size * K))
    widths = [d.textlength(c, font=f) for c in text]
    total = sum(widths) + sp * K * (len(text) - 1)
    x = SIZE * K / 2 - total / 2
    for c, w in zip(text, widths):
        d.text((x + w / 2, y * K), c, font=f, fill=col, anchor="mm")
        x += w + sp * K


def render(draw, numeral=None, title=None, fills=()):
    pic = Image.new("RGB", (S, S))
    for img, m in fills: pic.paste(img, (0, 0), m)
    draw(Pen(ImageDraw.Draw(pic)))
    clip = Image.new("L", (S, S)); cd = ImageDraw.Draw(clip)
    cd.ellipse([(233 - INNER) * K, (233 - INNER) * K, (233 + INNER) * K, (233 + INNER) * K], fill=255)
    if title: cd.rectangle([0, 384 * K, S, S], fill=0)
    if numeral: cd.rectangle([0, 0, S, 76 * K], fill=0)
    layer = Image.new("RGB", (S, S)); layer.paste(pic, (0, 0), clip)
    d = ImageDraw.Draw(layer); pen = Pen(d)
    for i, col in enumerate((MG, MGD, MGD)): pen.circle(233, 233, 222 - i * 6, col, 1.3 if i == 0 else 1)
    if numeral: spaced(d, 52, numeral, CY, 20, 6)
    if title:
        size = 20 if len(title) <= 14 else 17 if len(title) <= 18 else 15
        spaced(d, 410, title, MG, size, 6 if len(title) <= 14 else 4)
    im = ImageChops.add(layer, layer.filter(ImageFilter.GaussianBlur(6 * K)))
    im = ImageChops.add(im, layer.filter(ImageFilter.GaussianBlur(2 * K)))
    im = im.resize((SIZE, SIZE), Image.LANCZOS)
    m = Image.new("L", (SIZE, SIZE)); ImageDraw.Draw(m).ellipse([0, 0, SIZE - 1, SIZE - 1], fill=255)
    out = Image.new("RGB", (SIZE, SIZE)); out.paste(im, (0, 0), m)
    return out


# ---------------------------------------------------------------- the major arcana

def m0(p):   # the Fool: at the edge of a cliff, a light sun, a small dog
    p.line([(40, 330), (150, 300), (250, 285), (300, 290), (300, 384)], CY, 2.2)
    p.figure(270, 205, 1.2, MG, 2.2, arms=1)
    p.line([(286, 225), (330, 190)], YL, 2); p.circle(334, 186, 8, YL, 2)
    p.circle(150, 150, 30, YL, 2.5)
    for i in range(12):
        a = math.radians(i * 30); p.line([(150 + 38 * math.cos(a), 150 + 38 * math.sin(a)), (150 + 50 * math.cos(a), 150 + 50 * math.sin(a))], YL, 1.5)
    p.poly([(225, 270), (245, 270), (250, 258), (240, 262), (232, 255), (228, 262)], VI, 1.8)


def m1(p):   # the Magician: the infinity sign, a table with the four suits
    p.line([(233 + 50 * math.cos(t / 30 * 2 * math.pi) / (1 + math.sin(t / 30 * 2 * math.pi) ** 2),
             115 + 50 * math.sin(t / 30 * 2 * math.pi) * math.cos(t / 30 * 2 * math.pi) / (1 + math.sin(t / 30 * 2 * math.pi) ** 2)) for t in range(31)], YL, 2.5)
    p.line([(233, 150), (233, 260)], WH, 2.5); p.dot(233, 150, 5, WH)
    p.poly([(120, 280), (346, 280), (346, 296), (120, 296)], CY, 2)
    p.line([(140, 296), (140, 360)], CY, 2); p.line([(326, 296), (326, 360)], CY, 2)
    cup(p, 150, 262, 0.5, CY); sword(p, 200, 262, 0.5, VI); wand(p, 266, 262, 0.5, YL); coin(p, 316, 262, 0.5, MG)


def m2(p):   # the Seer: two pillars, a veil, the crescent moon
    for x, col in ((130, CY), (336, MG)):
        p.poly([(x - 16, 120), (x + 16, 120), (x + 16, 360), (x - 16, 360)], col, 2)
        p.line([(x - 22, 120), (x + 22, 120)], col, 2); p.line([(x - 22, 360), (x + 22, 360)], col, 2)
    p.arc(233, 165, 40, 100, 260, VI, 3); p.arc(250, 160, 34, 118, 242, VI, 2)
    for k in range(4):
        p.line([(150 + t * 4, 250 + k * 22 + 6 * math.sin(t / 4 + k)) for t in range(42)], VI if k % 2 else CY, 1.4)


def m3(p):   # the Empress: a crown of twelve stars, the sign of Venus, wheat
    for i in range(12):
        a = math.radians(200 + i * 12.7); p.star(233 + 95 * math.cos(a), 205 + 95 * math.sin(a), 6, YL, 5, 0.45, 1.3)
    p.circle(233, 200, 45, MG, 3); p.line([(233, 245), (233, 320)], MG, 3); p.line([(203, 285), (263, 285)], MG, 3)
    for x in (120, 346):
        p.line([(x, 370), (x, 270)], YL, 1.8)
        for k in range(5):
            y = 280 + k * 16; p.line([(x, y + 8), (x - 9, y)], YL, 1.5); p.line([(x, y + 8), (x + 9, y)], YL, 1.5)


def m4(p):   # the Emperor: a throne with ram horns, mountains, the orb
    p.poly([(178, 170), (288, 170), (288, 330), (178, 330)], CY, 2.5)
    for x, s in ((178, -1), (288, 1)):
        p.line([(x + s * (10 + 14 * math.cos(t / 6)), 160 - 14 * math.sin(t / 6) + t * 0.8) for t in range(40)], CY, 2)
    p.line([(40, 360), (110, 250), (160, 330), (233, 230), (306, 330), (356, 250), (426, 360)], MG, 2)
    p.circle(233, 235, 22, YL, 2.5); p.line([(233, 213), (233, 198)], YL, 2); p.line([(225, 205), (241, 205)], YL, 2)


def m5(p):   # the Teacher: an open book, a lamp, rays of light
    p.line([(150, 300), (233, 320), (316, 300), (316, 230), (233, 250), (150, 230), (150, 300)], CY, 2.5)
    p.line([(233, 250), (233, 320)], CY, 2)
    for k in range(4):
        y = 262 + k * 12; p.line([(165, y - 4), (220, y + 8)], VI, 1.2); p.line([(246, y + 8), (301, y - 4)], VI, 1.2)
    p.poly([(213, 160), (253, 160), (243, 190), (223, 190)], YL, 2); p.dot(233, 145, 7, YL)
    for i in range(9):
        a = math.radians(200 + i * 17.5); p.line([(233 + 30 * math.cos(a), 150 + 30 * math.sin(a)), (233 + 70 * math.cos(a), 150 + 70 * math.sin(a))], YL, 1.3)


def m6(p):   # the Lovers: two figures, a heart, the sun above
    p.figure(170, 220, 1.6, CY, 2.4, arms=1); p.figure(296, 220, 1.6, MG, 2.4, arms=1)
    hx, hy = 233, 150
    p.line([(hx + 16 * math.sin(t) ** 3 * 2, hy - (13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)) * 2) for t in [i / 40 * 2 * math.pi for i in range(41)]], YL, 2.5)
    p.grid(340, VI, 38, 5)


def m7(p):   # the Chariot: a car under a canopy of stars, running forward
    p.poly([(160, 210), (306, 210), (306, 290), (160, 290)], CY, 2.5)
    p.line([(150, 170), (316, 170)], VI, 2)
    for x in range(170, 310, 28): p.star(x, 150, 7, YL, 5, 0.45, 1.3)
    p.line([(160, 170), (160, 210)], VI, 1.5); p.line([(306, 170), (306, 210)], VI, 1.5)
    for x in (185, 281):
        p.circle(x, 305, 26, MG, 2.5); p.dot(x, 305, 4, MG)
        for i in range(6):
            a = math.radians(i * 60); p.line([(x, 305), (x + 24 * math.cos(a), 305 + 24 * math.sin(a))], MG, 1.2)
    for k in range(4): p.line([(60, 230 + k * 18), (130, 230 + k * 18)], CY, 1.2)


def m8(p):   # Strength: the infinity sign over a lion's face
    p.line([(233 + 34 * math.cos(t / 30 * 2 * math.pi) / (1 + math.sin(t / 30 * 2 * math.pi) ** 2),
             110 + 34 * math.sin(t / 30 * 2 * math.pi) * math.cos(t / 30 * 2 * math.pi) / (1 + math.sin(t / 30 * 2 * math.pi) ** 2)) for t in range(31)], YL, 2.2)
    for i in range(24):
        a = math.radians(i * 15); r0, r1 = 62, 92 + (8 if i % 2 else 0)
        p.line([(233 + r0 * math.cos(a), 240 + r0 * math.sin(a)), (233 + r1 * math.cos(a), 240 + r1 * math.sin(a))], MG, 2)
    p.circle(233, 240, 58, MG, 2.5)
    p.poly([(208, 228), (220, 222), (224, 232)], YL, 2); p.poly([(258, 228), (246, 222), (242, 232)], YL, 2)
    p.poly([(225, 255), (241, 255), (233, 266)], CY, 2); p.arc(233, 268, 16, 20, 160, CY, 2)


def m9(p):   # the Hermit: a lantern with a star on a staff, mountains
    p.line([(270, 110), (270, 360)], CY, 2.5)
    p.line([(270, 140), (220, 160)], CY, 2)
    p.poly([(200, 165), (240, 165), (246, 215), (194, 215)], YL, 2.2); p.star(220, 192, 13, YL, 6, 0.4, 1.6)
    p.circle(220, 190, 60, YL, 1)
    p.line([(30, 360), (130, 280), (190, 330), (300, 250), (436, 360)], VI, 2)


def m10(p):   # the Wheel of Fortune
    p.circle(233, 225, 120, MG, 2.5); p.circle(233, 225, 92, VI, 1.5); p.circle(233, 225, 24, YL, 2.5)
    for i in range(8):
        a = math.radians(i * 45 + 22.5)
        p.line([(233 + 24 * math.cos(a), 225 + 24 * math.sin(a)), (233 + 120 * math.cos(a), 225 + 120 * math.sin(a))], CY, 1.8)
    for i in range(4):
        a = math.radians(i * 90 - 90); p.dot(233 + 106 * math.cos(a), 225 + 106 * math.sin(a), 6, YL)


def m11(p):   # Justice: the scales and the sword
    p.line([(233, 110), (233, 360)], CY, 3); p.line([(218, 135), (248, 135)], CY, 3)
    p.line([(130, 190), (336, 190)], YL, 2.5)
    for x in (140, 326):
        p.line([(x, 190), (x - 34, 260)], YL, 1.5); p.line([(x, 190), (x + 34, 260)], YL, 1.5)
        p.arc(x, 260, 38, 0, 180, YL, 2.5, ry=24); p.line([(x - 38, 260), (x + 38, 260)], YL, 2)
    p.line([(200, 360), (266, 360)], MG, 2.5)


def m12(p):   # the Hanged One: upside down from a living tree, light round the head
    p.line([(130, 100), (130, 140), (336, 140), (336, 100)], CY, 3)
    for x in (130, 336):
        for k in range(3): p.line([(x, 115 + k * 12), (x + (10 if x == 130 else -10), 108 + k * 12)], VI, 1.3)
    p.line([(233, 140), (233, 175)], CY, 2)
    x, y = 233, 330
    p.circle(x, y, 13, MG, 2.4); p.circle(x, y, 30, YL, 1.5)
    p.line([(x, y - 13), (x, y - 120)], MG, 2.4)
    p.line([(x - 26, y - 70), (x, y - 50), (x + 26, y - 70)], MG, 2.4)
    p.line([(x, y - 120), (x - 24, y - 150)], MG, 2.4); p.line([(x, y - 120), (x + 14, y - 150), (x - 4, y - 162)], MG, 2.4)


def m13(p):   # Death, as change: a five-petal rose and the sun setting between two towers
    for x in (120, 346): p.poly([(x - 16, 330), (x - 16, 250), (x + 16, 250), (x + 16, 330)], VI, 2)
    p.arc(233, 330, 60, 180, 360, YL, 2.5); p.line([(40, 330), (426, 330)], MG, 2)
    for i in range(5):
        a = math.radians(i * 72 - 90); p.circle(233 + 26 * math.cos(a), 175 + 26 * math.sin(a), 24, WH, 2)
    p.circle(233, 175, 12, YL, 2.5)
    p.line([(233, 230), (233, 280)], CY, 2)


def m14(p):   # Temperance: water flowing from one cup to the other
    cup(p, 160, 230, 1.3, CY); cup(p, 306, 280, 1.3, MG)
    p.line([(166 + t * 4, 205 - 50 * math.sin(t / 34 * math.pi) + t * 1.5) for t in range(35)], VI, 2.2)
    p.line([(170 + t * 4, 212 - 46 * math.sin(t / 34 * math.pi) + t * 1.5) for t in range(33)], CY, 1.2)
    p.poly([(233, 110), (263, 160), (203, 160)], YL, 2)


def m15(p):   # the Chains: two figures chained to a block, horns above
    p.poly([(190, 210), (276, 210), (276, 270), (190, 270)], MG, 2.5)
    p.arc(233, 170, 60, 200, 340, YL, 2.5); p.line([(178, 150), (160, 110)], YL, 2.5); p.line([(288, 150), (306, 110)], YL, 2.5)
    p.figure(145, 280, 1.2, CY, 2.2); p.figure(321, 280, 1.2, CY, 2.2)
    for sx, ex in ((190, 152), (276, 314)):
        for k in range(6):
            t = k / 5; x = sx + (ex - sx) * t; y = 255 + (300 - 255) * t
            p.d.ellipse([(x - 6) * K, (y - 4) * K, (x + 6) * K, (y + 4) * K], outline=VI, width=int(1.5 * K))


def m16(p):   # the Tower: struck by lightning, flames, falling sparks
    p.poly([(195, 360), (195, 160), (271, 160), (271, 360)], CY, 2.5)
    p.line([(185, 160), (195, 140), (215, 150), (233, 130), (251, 150), (271, 140), (281, 160)], YL, 2)
    p.line([(330, 70), (290, 125), (310, 125), (262, 175)], YL, 3)
    for x, y in ((215, 210), (250, 260), (220, 300)):
        p.line([(x - 10, y + 12), (x - 4, y), (x, y + 8), (x + 6, y - 6), (x + 10, y + 12)], MG, 2)
    random.seed(16)
    for i in range(10): p.dot(random.randint(130, 340), random.randint(180, 360), 2.5, MG if i % 2 else YL)


def m17(p):   # the Star
    p.star(233, 185, 72, YL, 8, 0.3, 2.5); p.star(233, 185, 46, MG, 8, 0.3, 1.5, rot=-67.5)
    for i in range(7):
        a = math.radians(180 + i * 30); p.star(233 + 125 * math.cos(a), 195 + 85 * math.sin(a), 10, CY, 8, 0.35, 1.5)
    for k in range(5):
        y = 300 + k * 16; p.line([(20 + t * 6, y + 4 * math.sin(t / 3 + k)) for t in range(72)], CY if k % 2 == 0 else VI, 2 if k == 0 else 1.3)


def m18(p):   # the Moon
    p.arc(233, 150, 60, 95, 265, VI, 3); p.arc(256, 140, 52, 112, 248, VI, 2)
    for x0 in (120, 346):
        p.line([(x0 - 20, 300), (x0 - 20, 225), (x0, 200), (x0 + 20, 225), (x0 + 20, 300)], CY, 2)
        p.d.rectangle([(x0 - 4) * K, 245 * K, (x0 + 4) * K, 258 * K], fill=YL)
    p.grid(300, MGD, 34)
    p.line([(233 + 22 * math.sin(t / 5) * (1 - t / 36), 400 - t * 2.8) for t in range(36)], YL, 2)
    random.seed(4)
    for _ in range(14):
        x, y = random.randint(110, 356), random.randint(80, 190)
        if (x - 233) ** 2 + (y - 150) ** 2 > 70 ** 2: p.dot(x, y, 1.5, CY)


def m19(p):   # the Sun
    p.grid(262, CY)


SUN_FILL = gradient_disc(233, 200, 92, (255, 220, 60), (255, 50, 200), stripes=6, cut=262)


def m20(p):   # the Awakening: three figures rising, waves of sound from the dawn
    p.arc(233, 300, 50, 180, 360, YL, 2.5)
    for k in range(5): p.arc(233, 300, 75 + k * 26, 200, 340, VI if k % 2 else CY, 1.5)
    p.line([(40, 300), (426, 300)], MG, 2)
    for x, col in ((160, CY), (233, MG), (306, CY)): p.figure(x, 310, 0.9, col, 2, arms=2)


def m21(p):   # the World: a dancer inside a wreath, four signs round it
    p.d.ellipse([(233 - 80) * K, (225 - 125) * K, (233 + 80) * K, (225 + 125) * K], outline=MG, width=int(2.5 * K))
    for i in range(24):
        a = math.radians(i * 15); x, y = 233 + 80 * math.cos(a), 225 + 125 * math.sin(a)
        p.line([(x, y), (x + 10 * math.cos(a + 0.8), y + 10 * math.sin(a + 0.8))], VI, 1.3)
    p.figure(233, 170, 1.4, YL, 2.4, arms=2)
    for x, y in ((110, 120), (356, 120), (110, 330), (356, 330)): p.star(x, y, 14, CY, 4, 0.35, 1.6)


MAJORS = [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19, m20, m21]
ROMAN = ["0", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII", "XIII", "XIV", "XV",
         "XVI", "XVII", "XVIII", "XIX", "XX", "XXI"]

# ---------------------------------------------------------------- the suits

def cup(p, x, y, s, col, w=2):
    p.line([(x - 22 * s, y - 20 * s), (x + 22 * s, y - 20 * s)], col, w)
    p.arc(x, y - 20 * s, 22 * s, 0, 180, col, w)
    p.line([(x, y + 2 * s), (x, y + 22 * s)], col, w)
    p.line([(x - 14 * s, y + 22 * s), (x + 14 * s, y + 22 * s)], col, w)


def sword(p, x, y, s, col, w=2):
    p.line([(x, y - 30 * s), (x, y + 18 * s)], col, w)
    p.line([(x - 4 * s, y - 24 * s), (x, y - 34 * s), (x + 4 * s, y - 24 * s)], col, w)
    p.line([(x - 14 * s, y + 18 * s), (x + 14 * s, y + 18 * s)], col, w)
    p.line([(x, y + 18 * s), (x, y + 30 * s)], col, w); p.circle(x, y + 33 * s, 3 * s, col, w)


def wand(p, x, y, s, col, w=2):
    p.line([(x - 14 * s, y + 32 * s), (x + 14 * s, y - 32 * s)], col, w)
    for k in (-1, 1):
        t = 0.3 if k < 0 else 0.65
        cx, cy = x - 14 * s + 28 * s * t, y + 32 * s - 64 * s * t
        p.line([(cx, cy), (cx + k * 9 * s, cy - 6 * s)], col, w * 0.8)


def coin(p, x, y, s, col, w=2):
    p.circle(x, y, 22 * s, col, w); p.circle(x, y, 15 * s, col, w * 0.7)
    p.poly([(x - 5 * s, y - 5 * s), (x + 5 * s, y - 5 * s), (x + 5 * s, y + 5 * s), (x - 5 * s, y + 5 * s)], col, w * 0.8)


SUITS = [("cups", cup, CY), ("swords", sword, VI), ("wands", wand, YL), ("coins", coin, MG)]


def layout(n):
    """where the n symbols of a number card go (centre 233, 225)"""
    if n == 1: return [(233, 225, 2.4)]
    pts = {2: [(0, -60), (0, 60)], 3: [(0, -70), (-60, 45), (60, 45)], 4: [(-55, -55), (55, -55), (-55, 55), (55, 55)],
           5: [(-60, -60), (60, -60), (0, 0), (-60, 60), (60, 60)], 6: [(-55, -80), (55, -80), (-55, 0), (55, 0), (-55, 80), (55, 80)]}
    if n in pts: return [(233 + dx, 225 + dy, 1.3) for dx, dy in pts[n]]
    ring = n - (1 if n % 2 else 0)
    out = [(233 + 100 * math.cos(math.radians(-90 + i * 360 / ring)), 225 + 100 * math.sin(math.radians(-90 + i * 360 / ring)), 0.95) for i in range(ring)]
    if n % 2: out.append((233, 225, 1.3))
    return out


def court_mark(p, rank, col):
    """over the big suit sign: page = a star, knight = chevrons, queen = a crescent crown, king = a crown"""
    y = 112
    if rank == 11: p.star(233, y, 16, col, 5, 0.42, 2)
    elif rank == 12:
        for k in range(3): p.line([(205 + k * 20, y - 14), (220 + k * 20, y), (205 + k * 20, y + 14)], col, 2)
    elif rank == 13:
        p.arc(233, y - 10, 26, 20, 160, col, 2.2)
        for dx in (-18, 0, 18): p.dot(233 + dx, y + 18 - (6 if dx == 0 else 0), 3.5, col)
    else:
        p.line([(203, y + 14), (203, y - 10), (218, y + 2), (233, y - 16), (248, y + 2), (263, y - 10), (263, y + 14), (203, y + 14)], col, 2.2)


def minor(suit, rank):
    name, sym, col = SUITS[suit]

    def draw(p):
        p.circle(233, 225, 150, MGD, 1)
        if rank <= 10:
            for x, y, s in layout(rank): sym(p, x, y, s, col, 2.2 if s > 1.5 else 2)
        else:
            court_mark(p, rank, YL)
            sym(p, 233, 222, 2.3, col, 2.6)
            p.grid(342, MGD, 36, 5)
    return draw


# ---------------------------------------------------------------- names and meanings
# per card: Italian name, English name, upright (it, en), reversed (it, en)

MAJOR_TEXT = [
    ("Il Matto", "The Fool", "inizio, libertà, salto nel vuoto", "beginnings, freedom, a leap of faith", "imprudenza, esitazione, distrazione", "recklessness, hesitation, carelessness"),
    ("Il Bagatto", "The Magician", "volontà, abilità, nuove possibilità", "will, skill, new possibilities", "inganno, talento sprecato, confusione", "trickery, wasted talent, confusion"),
    ("La Veggente", "The Seer", "intuizione, silenzio, conoscenza nascosta", "intuition, silence, hidden knowledge", "segreti, voce interiore ignorata", "secrets, an ignored inner voice"),
    ("L'Imperatrice", "The Empress", "abbondanza, creatività, cura", "abundance, creativity, care", "dipendenza, blocco creativo", "dependence, creative block"),
    ("L'Imperatore", "The Emperor", "struttura, autorità, stabilità", "structure, authority, stability", "rigidità, controllo, testardaggine", "rigidity, control, stubbornness"),
    ("Il Maestro", "The Teacher", "insegnamento, tradizione, consiglio", "teaching, tradition, advice", "dogma, ribellione, strade nuove", "dogma, rebellion, new paths"),
    ("Gli Amanti", "The Lovers", "amore, scelta, armonia", "love, choice, harmony", "disaccordo, scelta rimandata", "discord, a choice put off"),
    ("Il Carro", "The Chariot", "vittoria, determinazione, movimento", "victory, determination, movement", "direzione persa, ostacoli", "lost direction, obstacles"),
    ("La Forza", "Strength", "coraggio, pazienza, dolcezza", "courage, patience, gentleness", "insicurezza, impulsività", "self-doubt, impulsiveness"),
    ("L'Eremita", "The Hermit", "ricerca interiore, solitudine, guida", "inner search, solitude, guidance", "isolamento, chiusura", "isolation, withdrawal"),
    ("La Ruota", "The Wheel", "cambiamento, destino, cicli", "change, fate, cycles", "resistenza al cambiamento, sfortuna", "resisting change, bad luck"),
    ("La Giustizia", "Justice", "equilibrio, verità, responsabilità", "balance, truth, responsibility", "ingiustizia, giudizio parziale", "unfairness, a biased judgement"),
    ("L'Appeso", "The Hanged One", "pausa, nuovo punto di vista, attesa", "pause, a new point of view, waiting", "stallo, sacrificio inutile", "stalling, a needless sacrifice"),
    ("La Morte", "Death", "fine, trasformazione, rinascita", "ending, transformation, renewal", "paura del cambiamento, ristagno", "fear of change, stagnation"),
    ("La Temperanza", "Temperance", "misura, pazienza, armonia", "moderation, patience, harmony", "eccesso, squilibrio", "excess, imbalance"),
    ("Le Catene", "The Chains", "attaccamento, tentazione, abitudini", "attachment, temptation, habits", "liberazione, consapevolezza", "release, awareness"),
    ("La Torre", "The Tower", "crollo improvviso, rivelazione", "sudden upheaval, revelation", "crisi evitata, paura del cambiamento", "a crisis avoided, fear of change"),
    ("La Stella", "The Star", "speranza, ispirazione, serenità", "hope, inspiration, calm", "sfiducia, scoraggiamento", "distrust, discouragement"),
    ("La Luna", "The Moon", "illusione, sogni, inconscio", "illusion, dreams, the unconscious", "chiarezza che torna, paure svelate", "clarity returning, fears revealed"),
    ("Il Sole", "The Sun", "gioia, successo, vitalità", "joy, success, vitality", "entusiasmo appannato", "dimmed enthusiasm"),
    ("Il Risveglio", "The Awakening", "rinascita, chiamata, bilancio", "rebirth, a calling, reckoning", "dubbio, occasione ignorata", "self-doubt, an ignored call"),
    ("Il Mondo", "The World", "completamento, integrazione, viaggio", "completion, integration, travel", "chiusura mancata, scorciatoie", "lack of closure, shortcuts"),
]

SUIT_NAMES = [("Coppe", "Cups"), ("Spade", "Swords"), ("Bastoni", "Wands"), ("Denari", "Coins")]
RANK_IT = ["Asso", "Due", "Tre", "Quattro", "Cinque", "Sei", "Sette", "Otto", "Nove", "Dieci", "Fante", "Cavallo", "Regina", "Re"]
RANK_EN = ["Ace", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine", "Ten", "Page", "Knight", "Queen", "King"]

# suit themes: cups feelings, swords thoughts, wands action, coins work and things
MINOR_TEXT = {
    0: [("nuovo amore, emozioni che sbocciano", "new love, feelings opening", "emozioni trattenute", "held-back feelings"),
        ("unione, attrazione, intesa", "union, attraction, partnership", "squilibrio, rottura", "imbalance, a break"),
        ("amicizia, festa, comunità", "friendship, celebration, community", "eccessi, pettegolezzi", "excess, gossip"),
        ("apatia, riflessione, offerta ignorata", "apathy, reflection, an ignored offer", "nuovo interesse, risveglio", "new interest, waking up"),
        ("delusione, rimpianto", "disappointment, regret", "accettazione, andare avanti", "acceptance, moving on"),
        ("ricordi, nostalgia, innocenza", "memories, nostalgia, innocence", "vivere nel passato", "living in the past"),
        ("fantasie, molte scelte, illusioni", "daydreams, many choices, illusions", "chiarezza, scelta concreta", "clarity, a real choice"),
        ("lasciare andare, cercare altro", "letting go, seeking more", "paura di partire", "fear of leaving"),
        ("desiderio esaudito, soddisfazione", "a wish come true, contentment", "insoddisfazione, eccesso", "discontent, overindulgence"),
        ("felicità, famiglia, armonia", "happiness, family, harmony", "tensioni in casa", "tension at home"),
        ("messaggio affettuoso, sensibilità", "a tender message, sensitivity", "immaturità emotiva", "emotional immaturity"),
        ("romanticismo, proposta, fascino", "romance, an offer, charm", "gelosia, illusioni", "jealousy, illusions"),
        ("compassione, intuito, calore", "compassion, intuition, warmth", "dipendenza emotiva", "emotional dependence"),
        ("equilibrio emotivo, generosità", "emotional balance, generosity", "manipolazione, freddezza", "manipulation, coldness")],
    1: [("chiarezza, idea nuova, verità", "clarity, a new idea, truth", "confusione, parole dure", "confusion, harsh words"),
        ("scelta difficile, stallo", "a hard choice, stalemate", "decisione presa, verità a galla", "a decision made, truth out"),
        ("dolore, separazione, ferita", "heartache, separation, hurt", "guarigione, perdono", "healing, forgiveness"),
        ("riposo, recupero, tregua", "rest, recovery, truce", "irrequietezza, ripresa", "restlessness, getting back up"),
        ("conflitto, vittoria amara", "conflict, a bitter win", "riconciliazione, rimorso", "reconciliation, remorse"),
        ("transizione, viaggio, lasciarsi alle spalle", "transition, a journey, moving on", "resistenza, bagaglio emotivo", "resistance, emotional baggage"),
        ("strategia, astuzia, segreti", "strategy, cunning, secrets", "confessione, coscienza", "confession, conscience"),
        ("limiti, paura, sentirsi bloccati", "limits, fear, feeling stuck", "liberazione, nuova prospettiva", "release, a new outlook"),
        ("ansia, notti insonni", "anxiety, sleepless nights", "speranza, paure che calano", "hope, fears easing"),
        ("fine di un ciclo, fondo toccato", "the end of a cycle, rock bottom", "ripresa, il peggio è passato", "recovery, the worst is over"),
        ("curiosità, vigilanza, idee", "curiosity, vigilance, ideas", "pettegolezzo, fretta", "gossip, haste"),
        ("azione rapida, ambizione", "swift action, ambition", "impulsività, aggressività", "impulsiveness, aggression"),
        ("lucidità, indipendenza, sincerità", "clear mind, independence, honesty", "freddezza, amarezza", "coldness, bitterness"),
        ("autorità intellettuale, giudizio", "intellectual authority, judgement", "abuso di potere, cinismo", "abuse of power, cynicism")],
    2: [("ispirazione, energia, inizio", "inspiration, energy, a start", "ritardi, mancanza di slancio", "delays, lack of drive"),
        ("progetti, scelte, orizzonte", "plans, choices, the horizon", "paura dell'ignoto", "fear of the unknown"),
        ("espansione, prospettive, attesa", "expansion, foresight, waiting", "ostacoli, ritardi", "obstacles, delays"),
        ("festa, casa, traguardo", "celebration, home, a milestone", "transizione, instabilità", "transition, instability"),
        ("competizione, attriti", "competition, friction", "evitare il conflitto", "avoiding conflict"),
        ("successo, riconoscimento", "success, recognition", "orgoglio, caduta", "pride, a fall"),
        ("difendere le proprie idee", "standing your ground", "sopraffazione, resa", "being overwhelmed, giving up"),
        ("velocità, notizie, movimento", "speed, news, movement", "attese, frustrazione", "waiting, frustration"),
        ("resilienza, ultima prova", "resilience, a last test", "stanchezza, difensività", "fatigue, defensiveness"),
        ("carico, responsabilità", "burden, responsibility", "delegare, alleggerirsi", "delegating, lightening up"),
        ("entusiasmo, scoperta", "enthusiasm, discovery", "idee senza seguito", "ideas without follow-through"),
        ("avventura, passione, partenza", "adventure, passion, departure", "fretta, irrequietezza", "haste, restlessness"),
        ("sicurezza, calore, determinazione", "confidence, warmth, determination", "gelosia, insicurezza", "jealousy, insecurity"),
        ("visione, guida, carisma", "vision, leadership, charisma", "prepotenza, impazienza", "overbearing, impatience")],
    3: [("opportunità, prosperità, concretezza", "opportunity, prosperity, the practical", "occasione persa", "a missed chance"),
        ("equilibrio, adattarsi, priorità", "balance, adapting, priorities", "disordine, troppi impegni", "disorder, overcommitment"),
        ("collaborazione, mestiere", "teamwork, craft", "lavoro di squadra difficile", "poor teamwork"),
        ("risparmio, sicurezza, controllo", "saving, security, control", "avarizia, rigidità", "greed, rigidity"),
        ("difficoltà, esclusione", "hardship, feeling left out", "ripresa, aiuto ricevuto", "recovery, help received"),
        ("generosità, dare e ricevere", "generosity, giving and receiving", "debiti, squilibrio", "debts, imbalance"),
        ("pazienza, raccolto in arrivo", "patience, a harvest coming", "impazienza, sforzi dispersi", "impatience, scattered effort"),
        ("impegno, abilità, dedizione", "diligence, skill, dedication", "perfezionismo, noia", "perfectionism, boredom"),
        ("autonomia, agio, raffinatezza", "independence, comfort, refinement", "dipendenza economica", "financial dependence"),
        ("eredità, famiglia, stabilità", "legacy, family, stability", "conflitti di soldi", "money conflicts"),
        ("studio, ambizione pratica", "study, practical ambition", "pigrizia, scarsa concentrazione", "laziness, poor focus"),
        ("costanza, metodo, affidabilità", "steadiness, method, reliability", "stagnazione, noia", "stagnation, boredom"),
        ("praticità, accoglienza, cura", "practicality, nurture, care", "trascuratezza, ansia materiale", "neglect, money worries"),
        ("successo, abbondanza, solidità", "success, abundance, solidity", "materialismo, testardaggine", "materialism, stubbornness")],
}


def cards():
    """(file stem, numeral, title for the face, it name, en name, up it, up en, rev it, rev en, draw, fills)"""
    out = []
    for i, f in enumerate(MAJORS):
        it, en, ui, ue, ri, re_ = MAJOR_TEXT[i]
        out.append(("%02d" % i, ROMAN[i], it.upper(), it, en, ui, ue, ri, re_, f, [SUN_FILL] if i == 19 else []))
    for s in range(4):
        for r in range(14):
            it = "%s di %s" % (RANK_IT[r], SUIT_NAMES[s][0])
            en = "%s of %s" % (RANK_EN[r], SUIT_NAMES[s][1])
            ui, ue, ri, re_ = MINOR_TEXT[s][r]
            numeral = str(r + 1) if r < 10 else None
            title = it.upper()
            out.append(("%02d" % (22 + s * 14 + r), numeral, title, it, en, ui, ue, ri, re_, minor(s, r + 1), []))
    return out


def back(p):
    p.star(233, 233, 150, MG, 8, 0.28, 2.2)
    p.star(233, 233, 105, VI, 8, 0.36, 1.4, rot=-67.5)
    p.circle(233, 233, 50, CY, 2); p.circle(233, 233, 36, CY, 1.2); p.dot(233, 233, 10, YL)
    for i in range(8):
        a = math.radians(-90 + i * 45); x, y = 233 + 185 * math.cos(a), 233 + 185 * math.sin(a)
        p.circle(x, y, 10, CY, 1.5)
        if i == 4: p.dot(x, y, 10, YL)
        elif i: p.d.pieslice([(x - 10) * K, (y - 10) * K, (x + 10) * K, (y + 10) * K], -90 if i < 4 else 90, 90 if i < 4 else 270, fill=YL)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "packs", "tarot")
    os.makedirs(os.path.join(out, "cards"), exist_ok=True)
    os.makedirs(os.path.join(out, "thumbs"), exist_ok=True)
    only = set(sys.argv[2:])   # optional: just these stems (quicker while drawing)
    b = render(back)
    b.save(os.path.join(out, "back.jpg"), quality=QUALITY)
    b.resize((THUMB, THUMB), Image.LANCZOS).save(os.path.join(out, "thumbs", "back.jpg"), quality=QUALITY)
    lines = ["# tarot cards: file|name it|name en|upright it|upright en|reversed it|reversed en"]
    for stem, num, title, it, en, ui, ue, ri, re_, draw, fills in cards():
        lines.append("|".join((stem, it, en, ui, ue, ri, re_)))
        if only and stem not in only: continue
        im = render(draw, num, title, fills)
        im.save(os.path.join(out, "cards", stem + ".jpg"), quality=QUALITY)
        im.resize((THUMB, THUMB), Image.LANCZOS).save(os.path.join(out, "thumbs", stem + ".jpg"), quality=QUALITY)
    with open(os.path.join(out, "cards.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("tarot: %d cards in %s" % (len(lines) - 1, out))


if __name__ == "__main__":
    main()
