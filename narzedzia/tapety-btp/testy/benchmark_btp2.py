#!/usr/bin/env python3
"""Reproducible in-memory corpus benchmark; never writes wallpaper assets."""
import argparse
from io import BytesIO
from pathlib import Path
import random
import sys
import time

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import btpcodec
import konwerterbtp


def corpus():
    solid = Image.new('RGBA', (257, 255), (25, 50, 75, 255))
    yield 'synthetic-solid.png', solid
    gradient = Image.new('RGBA', (257, 255))
    gradient.putdata([(x & 255, y & 255, (x + y) & 255, 255)
                      for y in range(255) for x in range(257)])
    yield 'synthetic-gradient.png', gradient
    checker = Image.new('RGBA', (257, 255))
    checker.putdata([(255, 255, 255, 255) if (x // 8 + y // 8) & 1 else (0, 0, 0, 255)
                     for y in range(255) for x in range(257)])
    yield 'synthetic-checker.png', checker
    ui = Image.new('RGBA', (640, 360), (30, 40, 50, 255))
    draw = ImageDraw.Draw(ui)
    for i in range(20):
        draw.rectangle((20 + i * 28, 40 + i % 4 * 40, 200 + i * 20, 65 + i % 4 * 40),
                       fill=(i * 11 % 256, 180, 75, 255))
    yield 'synthetic-ui.png', ui
    rng = random.Random(1729)
    noise = Image.new('RGBA', (257, 255))
    noise.putdata([(rng.randrange(256), rng.randrange(256), rng.randrange(256), 255)
                   for _ in range(257 * 255)])
    yield 'synthetic-noise.png', noise


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--photo', type=Path, default=Path('narzedzia/tapety-btp/tapeta.jpg'))
    parser.add_argument('--chunk-size', type=int, default=65536)
    args = parser.parse_args()
    print('| Image | Dimensions | Source B | Raw B | BTP1 B | BTP2 B | v2/raw | v2/v1 | Encode s | Decode s | Filters | Chunks |')
    print('| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |')
    images = list(corpus())
    with Image.open(args.photo) as photo:
        images.append((args.photo.name, photo.convert('RGBA')))
    try:
        for name, image in images:
            w, h = image.size
            if name == args.photo.name:
                source_size = args.photo.stat().st_size
            else:
                buffer = BytesIO()
                image.save(buffer, format='PNG')
                source_size = buffer.tell()
            pixels = konwerterbtp.przygotujpiksele(image, zachowajalfa=True)
            v1 = konwerterbtp.kodujbtp(pixels, w, h)
            begin = time.perf_counter()
            encoded = btpcodec.encode(pixels, w, h, method='hybrid', chunk_size=args.chunk_size)
            encode_seconds = time.perf_counter() - begin
            begin = time.perf_counter()
            decoded = btpcodec.decode(encoded)
            decode_seconds = time.perf_counter() - begin
            if decoded != (w, h, pixels):
                raise RuntimeError(f'round-trip mismatch: {name}')
            count = btpcodec.HEADER.unpack_from(encoded)[10]
            filters = {}
            modes = {}
            for index in range(count):
                entry = btpcodec.CHUNK.unpack_from(encoded, btpcodec.HEADER.size + index * btpcodec.CHUNK.size)
                filters[entry[4]] = filters.get(entry[4], 0) + 1
                modes[entry[3]] = modes.get(entry[3], 0) + 1
            print(f'| {name} | {w}×{h} | {source_size} | {len(pixels)} | '
                  f'{v1.rozmiarpliku} | {len(encoded)} | {len(encoded)/len(pixels):.3f} | '
                  f'{len(encoded)/v1.rozmiarpliku:.3f} | {encode_seconds:.3f} | '
                  f'{decode_seconds:.3f} | {filters} | {modes} |', flush=True)
    finally:
        for _, image in images:
            image.close()


if __name__ == '__main__':
    main()
