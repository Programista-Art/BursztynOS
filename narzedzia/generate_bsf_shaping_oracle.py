#!/usr/bin/env python3
"""Generate BSF 2.0 shaping expectations using the host HarfBuzz as an oracle.

HarfBuzz is used ONLY on the development host (through PyGObject) to
produce testy/fixtures/bsf_shaping_oracle.tsv. Bursztyn OS never links or
loads HarfBuzz. Each row: font, ISO-15924 script, direction, UTF-8 hex,
then glyph records "gid,cluster,x_advance,y_advance,x_offset,y_offset"
in visual order, in font units (font scale == upem, no hinting/ppem).
"""
import sys
from pathlib import Path
import gi
gi.require_version('HarfBuzz', '0.0')
from gi.repository import HarfBuzz as hb

root = Path(__file__).resolve().parents[1]
host = Path('/usr/share/fonts/truetype/noto')
FONTS = {
    'DejaVuSans': root / 'czcionki/ttf/DejaVuSans.ttf',
    'DejaVuSansBold': root / 'czcionki/ttf/DejaVuSans-Bold.ttf',
    'DejaVuSerifCondensedItalic': root / 'czcionki/ttf/DejaVuSerifCondensed-Italic.ttf',
    'Alice': root / 'czcionki/ttf/Alice-Regular.ttf',
    'NotoNaskhArabic': host / 'NotoNaskhArabic-Regular.ttf',
    'NotoSansArabic': host / 'NotoSansArabic-Regular.ttf',
    'NotoSansHebrew': host / 'NotoSansHebrew-Regular.ttf',
    'NotoSans': host / 'NotoSans-Regular.ttf',
    'NotoKufiArabic': host / 'NotoKufiArabic-Regular.ttf',
    'NotoSerifHebrew': host / 'NotoSerifHebrew-Regular.ttf',
    # Lookup-type coverage: Nastaliq has GSUB 5, GPOS 3 (cursive), GPOS 7;
    # Coptic has GSUB 8 (reverse chaining).
    'NotoNastaliqUrdu': host / 'NotoNastaliqUrdu-Regular.ttf',
    'NotoSansCoptic': host / 'NotoSansCoptic-Regular.ttf',
}
LATIN = ['Hello, world!', 'Zażółć gęślą jaźń', 'AVATAR WAVE Toffee office ﬁ fi ffi',
         'Tést ạ̈ ñǫ́', 'Yo! LT Ty Vo P. 1/2',
         'café café', 'x́̂̃̄']
CASES = [
    ('DejaVuSans', 'Latn', 'ltr', LATIN),
    ('DejaVuSansBold', 'Latn', 'ltr', LATIN[:3]),
    ('DejaVuSerifCondensedItalic', 'Latn', 'ltr', LATIN[:4]),
    ('Alice', 'Latn', 'ltr', LATIN),
    ('NotoSans', 'Latn', 'ltr', LATIN),
    ('NotoSans', 'Cyrl', 'ltr', ['Привет, мир!', 'Ёлка й̆']),
    ('NotoSans', 'Grek', 'ltr', ['Καλημέρα κόσμε', 'ά̓']),
    ('DejaVuSans', 'Cyrl', 'ltr', ['Привет, мир!']),
    ('DejaVuSans', 'Grek', 'ltr', ['Καλημέρα κόσμε']),
    ('DejaVuSans', 'Arab', 'rtl', ['مرحبا بالعالم', 'لا', 'لأ', 'بِسْمِ ٱللَّٰهِ',
                                   'ببب ب بب', 'عربي', 'سلام', 'ف\u200dع', 'ب\u200cب',
                                   '١٢٣ (نص)']),
    ('NotoNaskhArabic', 'Arab', 'rtl', ['مرحبا بالعالم', 'لا', 'بِسْمِ ٱللَّٰهِ ٱلرَّحْمَٰنِ',
                                        'ببببب', 'محمد', 'فِي', 'ـبـ', 'كتب', 'ﷲ ريال',
                                        'لِلَّهِ', 'تَّ', '١٢٣', '[قوس]', 'ب\u200cب']),
    ('NotoSansArabic', 'Arab', 'rtl', ['مرحبا بالعالم', 'سَلَامٌ', 'لا إله']),
    ('DejaVuSans', 'Hebr', 'rtl', ['שלום עולם', 'שָׁלוֹם', 'עִבְרִית']),
    ('NotoSansHebrew', 'Hebr', 'rtl', ['שלום עולם', 'שָׁלוֹם', 'בְּרֵאשִׁית', 'וּ', '(סוגריים)']),
    ('NotoSerifHebrew', 'Hebr', 'rtl', ['בְּרֵאשִׁית בָּרָא אֱלֹהִים', 'שׂ שׁ', 'יְרוּשָׁלַיִם']),
    ('NotoKufiArabic', 'Arab', 'rtl', ['مرحبا بالعالم', 'لا', 'بِسْمِ']),
    ('NotoNaskhArabic', 'Arab', 'rtl', [
        'اللغة العربية هي إحدى أكثر اللغات انتشارا في العالم',
        'پچژگ یک فارسی', 'اردو زبان', 'كـــتـــاب', 'مُحَمَّدٌ رَسُولُ', 'ئۇيغۇرچە',
        'لَا', 'ﻻ', 'ـ', 'ء آ أ ؤ إ ئ', 'ابجد٠١٢٣٤٥٦٧٨٩']),
    ('NotoSans', 'Latn', 'ltr', ['Tiếng Việt có dấu', 'ǅ ǈ ǋ', 'ﬀ ff fl ffl', 'a\u0300\u0301\u0302\u0303\u0304\u0305',
                                'Ɑ̃ ŋ̊ ɛ̃', 'office suffix', 'Å Å Å']),
    ('DejaVuSans', 'Latn', 'ltr', ['Tiếng Việt có dấu', 'a\u0300\u0301\u0302\u0303', 'Å Å Å', 'o\u0308\u0304']),
    ('NotoSans', 'Cyrl', 'ltr', ['Съешь же ещё этих мягких французских булок']),
    ('NotoNastaliqUrdu', 'Arab', 'rtl', ['اردو', 'زبان', 'پاکستان', 'نستعلیق',
                                         'میں نے کتاب پڑھی', 'بچہ', 'لیکن', 'سے']),
    ('NotoSansCoptic', 'Copt', 'ltr', ['ⲡⲓⲛⲟⲩⲧⲉ', 'ⲓ̅ⲥ̅ ⲡ̅ⲭ̅ⲥ̅',
                                      'ⲁ̅ⲛ̅ⲟ̅ⲕ̅', 'ϣⲏⲣⲓ']),
    # GSUB 8 (reverse chaining) only fires when a plain overline precedes a
    # connected one (lowercase then uppercase); verified by the per-type
    # application counters in test_bsf_shaping, not by font inspection.
    ('NotoSansCoptic', 'Copt', 'ltr', ['ⲁ̅Ⲛ̅', 'ⲁ̿Ⲛ̿', 'ⲡ̅ⲛ̅Ⲁ̅Ⲟ̅', 'ⲓ̅ⲥ̅ Ⲡ̅Ⲭ̅']),
    # GSUB 3 (alternate) is reachable only through the opt-in 'aalt'
    # feature (requested from HarfBuzz as aalt=1, i.e. the first alternate).
    ('DejaVuSans', 'Latn', 'ltr', ['agly IJ Ŋ', 'Jay'], 'aalt'),
    ('NotoSans', 'Latn', 'ltr', ['(0123456789)', 'a+b=c'], 'aalt'),
    ('NotoNaskhArabic', 'Arab', 'rtl', ['بتث', 'ٮ'], 'aalt'),
]


def shape(font_path, script, direction, text, features=''):
    blob = hb.blob_create_from_file(str(font_path))
    face = hb.face_create(blob, 0)
    font = hb.font_create(face)
    upem = hb.face_get_upem(face)
    hb.font_set_scale(font, upem, upem)
    buf = hb.buffer_create()
    data = text.encode('utf-8')
    hb.buffer_add_utf8(buf, data, 0, -1)
    hb.buffer_set_direction(buf, hb.direction_t.RTL if direction == 'rtl' else hb.direction_t.LTR)
    hb.buffer_set_script(buf, hb.script_from_string(script.encode()))
    hb.buffer_set_language(buf, hb.language_from_string(b'und'))
    feats = []
    for tag in filter(None, features.split(',')):
        ok, feat = hb.feature_from_string(tag.encode())
        if not ok:
            raise SystemExit('bad feature ' + tag)
        feats.append(feat)
    hb.shape(font, buf, feats)
    infos = hb.buffer_get_glyph_infos(buf)
    poses = hb.buffer_get_glyph_positions(buf)
    return data, [(i.codepoint, i.cluster, p.x_advance, p.y_advance, p.x_offset, p.y_offset)
                  for i, p in zip(infos, poses)]


def main():
    out = ['# Generated by narzedzia/generate_bsf_shaping_oracle.py (host HarfBuzz oracle)']
    for case in CASES:
        name, script, direction, texts = case[:4]
        features = case[4] if len(case) > 4 else ''
        path = FONTS[name]
        if not path.exists():
            print('missing font', path, file=sys.stderr)
            continue
        for text in texts:
            data, glyphs = shape(path, script, direction, text, features)
            rec = ';'.join(','.join(str(v) for v in g) for g in glyphs)
            shown = path.relative_to(root) if path.is_relative_to(root) else path
            row = [name, str(shown), script, direction, data.hex(), rec]
            if features:
                row.append(features)
            out.append('\t'.join(row))
    target = root / 'testy/fixtures/bsf_shaping_oracle.tsv'
    target.parent.mkdir(exist_ok=True)
    target.write_text('\n'.join(out) + '\n')
    print(f'{len(out) - 1} oracle rows -> {target}')


if __name__ == '__main__':
    main()
