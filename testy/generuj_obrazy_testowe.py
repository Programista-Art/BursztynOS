#!/usr/bin/env python3
"""Generuje pliki testowe dla Przegladarki Obrazow (PNG/JPEG/BMP/BTP1/BTP2 + uszkodzone warianty)."""
import struct, sys, zlib
from pathlib import Path
from PIL import Image
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'narzedzia/tapety-btp'))
import btpcodec

out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)

def cwiartki(w, h, alfa=False):
    im = Image.new('RGBA', (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            c = ((255, 0, 0) if y < h // 2 else (0, 255, 0)) if x < w // 2 else ((0, 0, 255) if y < h // 2 else (255, 255, 0))
            px[x, y] = c + ((0 if (alfa and x < 4 and y < 4) else 255),)
    return im

def bgra(im):
    return b''.join(bytes((b, g, r, a)) for (r, g, b, a) in im.getdata())

im = cwiartki(64, 48)
im.convert('RGB').save(out / 'cwiartki.png')
cwiartki(64, 48, True).save(out / 'alfa.png')
im.convert('RGB').save(out / 'cwiartki.jpg', quality=95, subsampling=0)
im.convert('RGB').save(out / 'cwiartki.bmp')
data = bgra(im)
(out / 'btp1_raw.btp').write_bytes(b'BTP1' + struct.pack('<IIBB', 64, 48, 32, 0) + data)
rle = bytearray(b'BTP1' + struct.pack('<IIBB', 64, 48, 32, 1))
i = 0
while i < len(data):
    px = data[i:i + 4]; run = 1
    while run < 255 and i + 4 * run < len(data) and data[i + 4 * run:i + 4 * run + 4] == px: run += 1
    rle += bytes((run,)) + px; i += 4 * run
(out / 'btp1_rle.btp').write_bytes(bytes(rle))
(out / 'btp2.btp').write_bytes(btpcodec.encode(data, 64, 48))
# uszkodzone / zlosliwe
png = (out / 'cwiartki.png').read_bytes()
(out / 'ucieta.png').write_bytes(png[:len(png) // 2])
(out / 'zly_naglowek.png').write_bytes(b'\x89PNG\r\n\x1a\n' + b'\x00' * 20)
big = bytearray(png); big[16:20] = struct.pack('>I', 100000)          # IHDR szerokosc ponad limit
(out / 'ogromny.png').write_bytes(bytes(big))
jpg = (out / 'cwiartki.jpg').read_bytes()
(out / 'ucieta.jpg').write_bytes(jpg[:len(jpg) // 3])
(out / 'zly.jpg').write_bytes(b'\xff\xd8' + bytes(range(200)))
b2 = bytearray((out / 'btp2.btp').read_bytes()); b2[len(b2) // 2] ^= 0xFF
(out / 'uszkodzony2.btp').write_bytes(bytes(b2))
(out / 'ucieta1.btp').write_bytes((out / 'btp1_raw.btp').read_bytes()[:500])
(out / 'btp_wymiary.btp').write_bytes(b'BTP1' + struct.pack('<IIBB', 100000, 100000, 32, 0) + b'\0' * 16)
(out / 'gif.gif').write_bytes(b'GIF89a' + b'\0' * 30)
(out / 'tekst.png').write_bytes(b'to nie jest obraz')
# rozmiary: Poziom 1 (pelna rozdzielczosc, do 17 Mpx) i Poziom 2 (podglad JPEG)
Image.new('RGB', (2040, 2040), (10, 120, 200)).save(out / 'graniczny_ok.png')           # 4,16 Mpx
Image.new('RGB', (2300, 2000), (200, 40, 40)).save(out / 'za_duzy_4_6mpx.png')           # 4,6 Mpx: pelna rozdzielczosc
Image.new('RGB', (2500, 2000), (40, 200, 40)).save(out / 'za_duzy.jpg', quality=50)      # 5 Mpx JPEG: pelna rozdzielczosc
KOLORY = [(220, 30, 30), (30, 200, 40), (30, 60, 220), (230, 220, 40)]
def cwiartki(w, h, tryb='RGB'):
    im = Image.new('RGB', (w, h))
    for i, c in enumerate(KOLORY):
        im.paste(c, ((i % 2) * (w // 2), (i // 2) * (h // 2), (i % 2 + 1) * (w // 2), (i // 2 + 1) * (h // 2)))
    return im.convert(tryb) if tryb != 'RGB' else im
cwiartki(5000, 4000).save(out / 'jpeg_20mpx_420.jpg', quality=85, subsampling=2)               # baseline 4:2:0
cwiartki(5000, 4000).save(out / 'jpeg_20mpx_444_rst.jpg', quality=85, subsampling=0, restart_marker_blocks=64)
cwiartki(5000, 4000).save(out / 'jpeg_20mpx_prog.jpg', quality=85, progressive=True, subsampling=2)
cwiartki(5000, 4000, 'L').save(out / 'jpeg_20mpx_gray.jpg', quality=85)
cwiartki(8000, 6000).save(out / 'jpeg_48mpx_420.jpg', quality=70, subsampling=2)                # powyzej limitu zrodla: nadal podglad 1/8? (patrz test)
Image.new('RGB', (9000, 16), (90, 90, 90)).save(out / 'jpeg_9000x16.jpg')                       # bok > 8192: odrzucony
cwiartki(5000, 4000).save(out / 'png_20mpx.png', compress_level=9)                              # PNG ponad 17 Mpx
# JPEG progresywny z redefinicja tablic Huffmana miedzy skanami (jak libjpeg): musi sie otwierac w pelnej rozdzielczosci
import random
rnd = random.Random(12345)
szum = Image.new('RGB', (1200, 900))
szum.putdata([(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)) for _ in range(1200 * 900)])
szum.save(out / 'jpeg_prog_dht.jpg', quality=90, progressive=True, optimize=True, subsampling=2)
_d = (out / 'jpeg_prog_dht.jpg').read_bytes()
_ile = {}; _i = 2
while _i < len(_d) - 4:
    if _d[_i] != 0xFF or _d[_i + 1] in (0, 0xFF) or 0xD0 <= _d[_i + 1] <= 0xD7: _i += 1; continue
    _m = _d[_i + 1]; _l = struct.unpack('>H', _d[_i + 2:_i + 4])[0]
    if _m == 0xC4: _ile[_d[_i + 4]] = _ile.get(_d[_i + 4], 0) + 1
    if _m == 0xDA:
        _i += 2 + _l
        while _i < len(_d) - 1 and not (_d[_i] == 0xFF and _d[_i + 1] not in (0, 0xFF) and not 0xD0 <= _d[_i + 1] <= 0xD7): _i += 1
        continue
    _i += 2 + _l
assert max(_ile.values()) > 1, 'fixture nie zawiera redefinicji DHT'
print('OK', len(list(out.iterdir())), 'plikow')
