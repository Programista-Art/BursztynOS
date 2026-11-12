#!/usr/bin/env python3
"""BSF 2.2 colour-font fixtures (development host only, fontTools 4.55.3).

- NotoColorEmoji-subset.ttf: CBDT/CBLC PNG strikes, subset of the host
  Noto Color Emoji (Debian fonts-noto-color-emoji, OFL).
- NotoCOLRv1-subset.ttf: COLRv1 paint graphs, subset of Noto-COLRv1.ttf
  (googlefonts/noto-emoji v2.047, OFL; path given on the command line).
- BSF-COLRv0-test.ttf: purpose-built COLRv0/CPAL font (layers from Noto Sans
  Latin outlines, two palettes, foreground layer).
- BSF-sbix-test.ttf: purpose-built sbix font (PNG strikes 32/64 ppem, a
  'dupe' glyph, an unsupported 'jpg ' glyph and a corrupt PNG).
"""
import pathlib, struct, sys, zlib
from fontTools import subset
from fontTools.ttLib import TTFont, newTable
from fontTools.colorLib.builder import buildCOLR, buildCPAL

root = pathlib.Path(__file__).resolve().parents[2]
out = root / 'testy/fixtures/bsf22/color'
EMOJI = '😀😂❤️👍👍🏽👨‍👩‍👧🇵🇱🏳️‍🌈✌️#️⃣1️⃣☺️☺🎉🔥🌍🚀✨'

def do_subset(src, dst, text):
    f = TTFont(str(src))
    o = subset.Options()
    o.layout_features = ['*']; o.name_IDs = ['*']; o.notdef_outline = True
    s = subset.Subsetter(o); s.populate(text=text); s.subset(f)
    f.save(str(dst))

def png(w, h, pixel):
    raw = b''.join(b'\0' + b''.join(bytes(pixel(x, y)) for x in range(w)) for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))

def colrv0():
    f = TTFont(str(root / 'testy/fixtures/bsf22/web/NotoSans-Latin.ttf'))
    cmap = f.getBestCmap()
    A, O, B = cmap[ord('A')], cmap[ord('G')], cmap[ord('L')]
    # A: red A + translucent blue G; L: foreground L + green G.
    f['COLR'] = buildCOLR({A: [(A, 0), (O, 1)], B: [(B, 0xFFFF), (O, 2)]}, version=0)
    f['CPAL'] = buildCPAL([[(1, 0, 0, 1), (0, 0, 1, .5), (0, .6, 0, 1)],
                           [(0, 0, 0, 1), (1, 1, 0, .25), (1, 0, 1, 1)]])
    f['name'].setName('BSF COLRv0 Test', 1, 3, 1, 0x409)
    f['name'].setName('BSF COLRv0 Test', 4, 3, 1, 0x409)
    f.save(str(out / 'BSF-COLRv0-test.ttf'))

def sbix():
    f = TTFont(str(root / 'testy/fixtures/bsf22/web/NotoSans-Latin.ttf'))
    from fontTools.ttLib.tables.sbixStrike import Strike
    from fontTools.ttLib.tables.sbixGlyph import Glyph
    order = f.getGlyphOrder(); cmap = f.getBestCmap()
    t = newTable('sbix'); t.version = 1; t.flags = 1; t.strikes = {}
    for ppem in (32, 64):
        st = Strike(ppem=ppem, resolution=72)
        def add(name, gtype, data, ox=0, oy=0):
            st.glyphs[name] = Glyph(glyphName=name, graphicType=gtype, imageData=data, originOffsetX=ox, originOffsetY=oy)
        a, b, c, d, e = (cmap[ord(k)] for k in 'AGLJT')
        add(a, 'png ', png(ppem, ppem, lambda x, y: (255 * x // ppem, 255 * y // ppem, 128, 255)), 0, -ppem // 8)
        add(b, 'png ', png(ppem // 2, ppem, lambda x, y: (0, 0, 255, 128 if (x + y) % 2 else 255)), 2, 0)
        st.glyphs[c] = Glyph(glyphName=c, graphicType='dupe', referenceGlyphName=a)
        add(d, 'jpg ', b'\xff\xd8\xff\xe0 not decoded')
        add(e, 'png ', b'\x89PNG\r\n\x1a\n' + b'\0' * 40)  # corrupt
        for name in order:
            if name not in st.glyphs:
                st.glyphs[name] = Glyph(glyphName=name)
        t.strikes[ppem] = st
    f['sbix'] = t
    f['name'].setName('BSF sbix Test', 1, 3, 1, 0x409)
    f['name'].setName('BSF sbix Test', 4, 3, 1, 0x409)
    f.save(str(out / 'BSF-sbix-test.ttf'))

if __name__ == '__main__':
    colrv1 = pathlib.Path(sys.argv[1])
    do_subset('/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf', out / 'NotoColorEmoji-subset.ttf', EMOJI)
    do_subset(colrv1, out / 'NotoCOLRv1-subset.ttf', EMOJI)
    colrv0(); sbix()
    import hashlib
    (out / 'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name + '\n'
                                            for p in sorted(out.iterdir()) if p.name != 'SHA256SUMS'))
