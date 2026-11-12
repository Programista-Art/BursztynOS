#!/usr/bin/env python3
"""BSF 2.2 test-fixture generator (development host only).

Uses fontTools (pinned 4.55.3) to derive small, redistributable fixtures from
OFL fonts. Noto fonts carry no Reserved Font Name, so subsets may keep their
names. Run: PYTHONPATH=<fonttools> python3 narzedzia/bsf22/make_fixtures.py
Outputs go to testy/fixtures/bsf22/fonts/ (hashes in SHA256SUMS).
"""
import hashlib, pathlib, sys
from fontTools import subset
from fontTools.ttLib import TTCollection, TTFont

root = pathlib.Path(__file__).resolve().parents[2]
out = root / 'testy/fixtures/bsf22/fonts'
CJK = pathlib.Path('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc')
TEXT = ('Zażółć gęślą jaźń ZAŻÓŁĆ GĘŚLĄ JAŹŃ The quick brown fox jumps over the '
        'lazy dog 0123456789 .,:;!?()[]{}-–—\'"„”«»@#%&*/'
        'こんにちは世界 カタカナ ひらがな 日本語 你好世界 中文 汉字 漢字 안녕하세요 한국어 '
        '東京大阪京都北海道春夏秋冬山川田人口目耳手足上下左右中大小月火水木金土日年時分')

def subset_font(font, text, keep_hint=True):
    opts = subset.Options()
    opts.layout_features = ['*']
    opts.name_IDs = ['*']
    opts.name_languages = ['*']
    opts.hinting = keep_hint
    opts.notdef_outline = True
    opts.glyph_names = False
    opts.drop_tables += ['DSIG']
    s = subset.Subsetter(opts)
    s.populate(text=text)
    s.subset(font)
    return font

def cjk_subset():
    coll = TTCollection(str(CJK))
    f = coll.fonts[0]  # Noto Sans CJK JP Regular (CID-keyed CFF)
    subset_font(f, TEXT)
    f.save(str(out / 'NotoSansCJK-subset.otf'))
    # Collection of four faces sharing one CFF/cmap/hmtx subset (JP, KR, SC, TC
    # differ only in locl/GSUB/name), with shared tables.
    faces = []
    for i in range(4):
        g = TTCollection(str(CJK)).fonts[i]
        subset_font(g, TEXT)
        faces.append(g)
    c = TTCollection()
    c.fonts = faces
    c.save(str(out / 'NotoSansCJK-subset.ttc'), shareTables=True)

HOST_TTF = pathlib.Path('/usr/share/fonts/truetype/noto')
LATIN = 'Zażółć gęślą jaźń ZAŻÓŁĆ GĘŚLĄ JAŹŃ The quick brown fox jumps over the lazy dog 0123456789 .,;:!?-–()'

def ttc_fixtures():
    # Three TrueType faces (Noto Sans Regular/Bold/Italic, host Noto 2.x),
    # subset to Latin+Polish, sharing identical tables.
    faces = []
    for style in ('Regular', 'Bold', 'Italic'):
        f = TTFont(str(HOST_TTF / f'NotoSans-{style}.ttf'))
        subset_font(f, LATIN, keep_hint=False)
        faces.append(f)
    c = TTCollection()
    c.fonts = faces
    c.save(str(out / 'NotoSans-subset.ttc'), shareTables=True)
    # Mixed collection: one TrueType face and one CFF face.
    tt = TTFont(str(HOST_TTF / 'NotoSans-Regular.ttf'))
    subset_font(tt, LATIN, keep_hint=False)
    cff = TTFont(str(out / 'NotoSans-Bold.otf'))
    subset_font(cff, LATIN)
    m = TTCollection()
    m.fonts = [tt, cff]
    m.save(str(out / 'NotoSans-mixed.otc'), shareTables=True)

def woff_fixtures():
    from fontTools.ttLib import woff2
    web = root / 'testy/fixtures/bsf22/web'
    web.mkdir(exist_ok=True)
    # TrueType source: host Noto Sans 2.004 subset (Latin + Polish).
    tt = TTFont(str(HOST_TTF / 'NotoSans-Regular.ttf'))
    subset_font(tt, LATIN, keep_hint=False)
    tt.save(str(web / 'NotoSans-Latin.ttf'))
    bold = TTFont(str(HOST_TTF / 'NotoSans-Bold.ttf'))
    subset_font(bold, LATIN, keep_hint=False)
    bold.flavor = 'woff2'
    bold.save(str(web / 'NotoSans-Latin-Bold.woff2'))
    naskh = TTFont(str(HOST_TTF / 'NotoNaskhArabic-Regular.ttf'))
    naskh.save(str(web / 'NotoNaskhArabic.ttf'))
    sources = {
        'NotoSans-Latin': web / 'NotoSans-Latin.ttf',
        'NotoSans-CFF': out / 'NotoSans-Regular.otf',
        'NotoSansHebrew-VF': out / 'NotoSansHebrew-VF.ttf',
        'NotoNaskhArabic': web / 'NotoNaskhArabic.ttf',
    }
    for name, src in sources.items():
        f = TTFont(str(src))
        f.flavor = 'woff'
        f.save(str(web / (name + '.woff')))
        # glyf/loca (+ hmtx where the font allows it) transformed.
        woff2.compress(str(src), str(web / (name + '.woff2')),
                       transform_tables={'glyf', 'loca', 'hmtx'})
        woff2.compress(str(src), str(web / (name + '-notransform.woff2')),
                       transform_tables=set())
    # Adversarial: a Brotli stream that expands to 32 MiB of zeros inside a
    # WOFF2 whose directory claims one small table (decoder must stop at the
    # declared size), and a WOFF 1.0 zlib table larger than declared.
    import brotli, struct, zlib
    bomb = brotli.compress(b'\0' * (32 * 1024 * 1024), quality=5)
    def b128(v):
        out = [v & 0x7f]
        v >>= 7
        while v:
            out.append(0x80 | (v & 0x7f))
            v >>= 7
        return bytes(reversed(out))
    directory = bytes([5]) + b128(1000)  # 'name', origLength 1000
    length = 48 + len(directory) + len(bomb)
    header = struct.pack('>4sIIHHIIHHIIIII', b'wOF2', 0x10000, length, 1, 0, 2048,
                         len(bomb), 1, 0, 0, 0, 0, 0, 0)
    (web / 'bomb-brotli.woff2').write_bytes(header + directory + bomb)
    payload = zlib.compress(b'\0' * (1 << 20))
    length = 44 + 20 + ((len(payload) + 3) & ~3)
    header = struct.pack('>4sIIHHIHHIIIII', b'wOFF', 0x10000, length, 1, 0, 1024, 1, 0, 0, 0, 0, 0, 0)
    entry = struct.pack('>4sIIII', b'name', 64, len(payload), 1000, 0)
    (web / 'bomb-zlib.woff').write_bytes(header + entry + payload + b'\0' * (-len(payload) % 4))

def sums():
    for d in (out, root / 'testy/fixtures/bsf22/web'):
        if not d.exists():
            continue
        lines = []
        for p in sorted(d.iterdir()):
            if p.name == 'SHA256SUMS' or not p.is_file():
                continue
            lines.append(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name)
        (d / 'SHA256SUMS').write_text('\n'.join(lines) + '\n')

if __name__ == '__main__':
    what = sys.argv[1:] or ['cjk']
    if 'cjk' in what:
        cjk_subset()
    if 'ttc' in what:
        ttc_fixtures()
    if 'woff' in what:
        woff_fixtures()
    sums()
