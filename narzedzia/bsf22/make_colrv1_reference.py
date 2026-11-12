#!/usr/bin/env python3
"""COLRv1 reference bitmaps (host only): blackrenderer 0.6.0 + skia-python.

Each base glyph of NotoCOLRv1-subset.ttf is rendered at PX pixels per em
with BSF's bitmap geometry: x0 = floor(xMin*s), y0 = floor(-yMax*s) from the
ClipList box, w/h likewise; pixel = (x*s - x0, -y*s - y0). Output:
testy/fixtures/bsf22/color/colrv1_ref.bin: per glyph
  u16 gid, i16 x0, i16 y0, u16 w, u16 h, then w*h straight-alpha RGBA.
Also writes adversarial COLRv1 fonts (cycle, deep transform chain).
"""
import math, pathlib, struct
import skia
from blackrenderer.font import BlackRendererFont
from blackrenderer.backends.skia import SkiaCanvas
from fontTools.ttLib import TTFont
from fontTools.colorLib.builder import buildCOLR, buildCPAL
from fontTools.ttLib.tables import otTables as ot

root = pathlib.Path(__file__).resolve().parents[2]
color = root / 'testy/fixtures/bsf22/color'
PX = 64

def references():
    path = color / 'NotoCOLRv1-subset.ttf'
    tt = TTFont(str(path))
    brf = BlackRendererFont(str(path))
    upem = tt['head'].unitsPerEm
    s = PX / upem
    colr = tt['COLR'].table
    clips = colr.ClipList.clips if colr.ClipList else {}
    out = bytearray()
    count = 0
    skipped = []
    for rec in colr.BaseGlyphList.BaseGlyphPaintRecord:
        name = rec.BaseGlyph
        gid = tt.getGlyphID(name)
        box = clips.get(name)
        if box is None:
            continue
        xmin, ymin, xmax, ymax = box.xMin, box.yMin, box.xMax, box.yMax
        x0, y0 = math.floor(xmin * s), math.floor(-ymax * s)
        w, h = math.floor(xmax * s) + 1 - x0, math.floor(-ymin * s) + 1 - y0
        w, h = min(w, 128), min(h, 128)
        surface = skia.Surface(w, h)
        try:
            with surface as c:
                c.clear(skia.Color4f(0, 0, 0, 0))
                c.translate(-x0, -y0)
                c.scale(s, -s)
                brf.drawGlyph(name, SkiaCanvas(c))
        except AttributeError:
            # blackrenderer 0.6.0 cannot draw PaintGlyph of an empty outline.
            skipped.append(name)
            continue
        img = surface.makeImageSnapshot()
        arr = img.toarray(colorType=skia.ColorType.kRGBA_8888_ColorType, alphaType=skia.AlphaType.kUnpremul_AlphaType)
        out += struct.pack('>HhhHH', gid, x0, y0, w, h) + arr.tobytes()
        count += 1
    (color / 'colrv1_ref.bin').write_bytes(bytes(out))
    print('COLRv1 references:', count, 'skipped (reference renderer limitation):', skipped)

def adversarial():
    base = TTFont(str(root / 'testy/fixtures/bsf22/web/NotoSans-Latin.ttf'))
    cmap = base.getBestCmap()
    A, G, L = cmap[ord('A')], cmap[ord('G')], cmap[ord('L')]
    solid = {'Format': ot.PaintFormat.PaintSolid, 'PaletteIndex': 0, 'Alpha': 1.0}
    # A -> ColrGlyph(G) -> ColrGlyph(A): cycle.
    glyphs = {
        A: {'Format': ot.PaintFormat.PaintColrGlyph, 'Glyph': G},
        G: {'Format': ot.PaintFormat.PaintColrGlyph, 'Glyph': A},
    }
    # L: 200 nested PaintTranslate around a PaintGlyph.
    deep = {'Format': ot.PaintFormat.PaintGlyph, 'Paint': solid, 'Glyph': L}
    for _ in range(200):
        deep = {'Format': ot.PaintFormat.PaintTranslate, 'Paint': deep, 'dx': 1, 'dy': 0}
    glyphs[L] = deep
    base['COLR'] = buildCOLR(glyphs, version=1)
    base['CPAL'] = buildCPAL([[(1, 0, 0, 1)]])
    base.save(str(color / 'BSF-COLRv1-adversarial.ttf'))

if __name__ == '__main__':
    references()
    adversarial()
