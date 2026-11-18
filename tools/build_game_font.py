from pathlib import Path
import numpy as np
from PIL import Image
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT.parent / "VOIMA_Data" / "StreamingAssets" / "Font"
OUT = ROOT / "assets" / "VOIMA_Game.ttf"
SCALE = 5.0
NAMED = {
    'space': ' ', 'apostrophe': "'", 'backslash': '\\', 'colon': ':', 'comma': ',',
    'dot': '.', 'double quote': '"', 'equals': '=', 'exclamation': '!', 'greater': '>',
    'hashtag': '#', 'left bracket': '(', 'left square bracket': '[', 'lesser': '<',
    'minus': '-', 'percent': '%', 'plus': '+', 'question': '?', 'right bracket': ')',
    'right square bracket': ']', 'semicolon': ';', 'slash': '/', 'underscore': '_',
    'degree': '°', 'infinity': '∞', '€': '€',
}
SKIP = {'left mouse button', 'right mouse button', 'middle mouse button', 'scroll'}

def glyph_name(ch):
    return 'uni%04X' % ord(ch)

def load_mask(path):
    im = Image.open(path).convert('RGBA')
    a = np.array(im)[:, :, 3]
    return (a > 80).astype(np.uint8)

BASE_DELTA = {
    'g': 47, 'p': 63, 'q': 62, 'y': 72, 'j': 70,
    ',': 17, ';': 17, '?': 70,
    "'": -105, '=': -45,
}

def make_glyph(path, ch):
    mask = load_mask(path)
    h, w = mask.shape
    ys = np.where(mask.any(axis=1))[0]
    base = (int(ys.max()) if len(ys) else h - 1) - BASE_DELTA.get(ch, 0)
    pen = TTGlyphPen(None)
    for y in range(h):
        row = mask[y]
        x = 0
        while x < w:
            if not row[x]:
                x += 1
                continue
            x0 = x
            while x < w and row[x]:
                x += 1
            x1 = x
            # One thin rectangle per opaque run keeps the exact bitmap shape.
            y0 = (base - y) * SCALE
            y1 = (base - y + 1) * SCALE
            pts = [
                (x0 * SCALE, y0), (x1 * SCALE, y0),
                (x1 * SCALE, y1), (x0 * SCALE, y1)
            ]
            pen.moveTo(pts[0])
            for p in pts[1:]:
                pen.lineTo(p)
            pen.closePath()
    return pen.glyph()

files = sorted(SRC.glob('*.png'))
items = []
for p in files:
    name = p.stem
    if name in SKIP:
        continue
    if name in NAMED:
        ch = NAMED[name]
    elif len(name) == 1:
        ch = name
    else:
        continue
    if ord(ch) < 32:
        continue
    items.append((ch, p))

glyphs = {'.notdef': TTGlyphPen(None).glyph()}
cmap = {}
metrics = {}
for ch, p in items:
    gname = glyph_name(ch)
    if gname in glyphs:
        cmap[ord(ch)] = gname
        continue
    glyphs[gname] = make_glyph(p, ch)
    cmap[ord(ch)] = gname
    mask = load_mask(p)
    ys, xs = np.where(mask > 0)
    adv = 200 if ch == ' ' or not len(xs) else max(180, round((xs.max() - xs.min() + 1) * SCALE + 60))
    metrics[gname] = (adv, 0)

glyph_order = ['.notdef'] + sorted([g for g in glyphs if g != '.notdef'])
metrics['.notdef'] = (500, 0)
fb = FontBuilder(1000, isTTF=True)
fb.setupGlyphOrder(glyph_order)
fb.setupCharacterMap(cmap)
fb.setupGlyf(glyphs)
fb.setupHorizontalMetrics({g: metrics.get(g, (500, 0)) for g in glyph_order})
fb.setupHorizontalHeader(ascent=900, descent=-200)
fb.setupNameTable({'familyName': 'VOIMA Game', 'styleName': 'Regular', 'uniqueFontIdentifier': 'VOIMA Game Regular', 'fullName': 'VOIMA Game', 'psName': 'VOIMAGame-Regular'})
fb.setupOS2(sTypoAscender=900, sTypoDescender=-200, usWinAscent=900, usWinDescent=200)
fb.setupPost()
fb.save(str(OUT))
print('wrote', OUT, 'glyphs', len(glyph_order), 'bytes', OUT.stat().st_size)
