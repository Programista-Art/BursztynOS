# BTP2 benchmark

Measured with `python3 -B narzedzia/tapety-btp/testy/benchmark_btp2.py` on
2026-09-25. The script creates synthetic images in memory, reads the supplied
`tapeta.jpg`, checks every BTP2 round trip byte for byte, and writes no image
files. Times are wall clock values from one run and vary with host load.

| Image | Dimensions | Source bytes | Raw pixels | BTP1 | BTP2 hybrid | BTP2/raw | BTP2/BTP1 | Encode s | Decode s |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Solid | 257×255 | 856 | 262,140 | 1,299 | 180 | 0.001 | 0.139 | 0.271 | 0.002 |
| Gradient | 257×255 | 852 | 262,140 | 262,154 | 262,292 | 1.001 | 1.001 | 1.125 | <0.001 |
| Checkerboard | 257×255 | 985 | 262,140 | 40,974 | 20,128 | 0.077 | 0.491 | 0.406 | 0.045 |
| UI image | 640×360 | 1,973 | 921,600 | 7,254 | 5,633 | 0.006 | 0.777 | 0.774 | 0.001 |
| Noise | 257×255 | 231,017 | 262,140 | 262,154 | 262,292 | 1.001 | 1.001 | 1.360 | <0.001 |
| Supplied `tapeta.jpg` | 1920×1080 | 2,448,766 | 8,294,400 | 8,294,414 | 7,132,038 | 0.860 | 0.860 | 38.338 | 1.219 |

The photo has 127 independent 64 KiB chunks, all stored as hybrid token
streams with filter 0. Its BTP2 payload is 7,129,426 bytes; the remaining
2,612 bytes are the header and chunk table. BTP2 saves 1,162,362 bytes
(14.0%) against raw pixels and 1,162,376 bytes against BTP1. It is larger
than the 2,448,766-byte JPEG source because JPEG is lossy, while BTP2
preserves the exact decoded BGRA pixels. Its decoded CRC32 is `30b4a3ef`.

Separate method measurements on those same decoded pixels at 64 KiB chunks:

| BTP2 method | File bytes | Encode s |
| --- | ---: | ---: |
| RAW | 8,297,012 | 0.022 |
| RLE only | 8,207,760 | 30.087 |
| LZ only | 7,136,651 | 33.660 |
| Hybrid LZ/RLE | 7,132,038 | 53.909 |

The method timings were collected while QEMU was also running. The
independent converter invocation of the hybrid mode took 44.618 seconds and
Python decoding plus verification took 2.433 seconds. The kernel decoder uses
the same chunk and token format without a compression library. RAW fallback
keeps incompressible data close to raw size, with 2,612 bytes of metadata for
this image.

The 64 KiB default was selected as a balance between photo size and decode
time. Final-format chunk comparisons on this photo yielded 7,356,305 bytes
at 32 KiB, 7,132,038 bytes at 64 KiB, and 6,973,074 bytes at 128 KiB.
The 128 KiB choice saves 2.2% against 64 KiB, while an earlier trial of
the same photo took 1.665 seconds to decode in Python, versus 1.219 seconds
at 64 KiB in the corpus run. The format permits chunk sizes up to 128 KiB.
A final-format 32,768-byte LZ window produced 7,187,046 bytes,
versus 7,132,038 bytes with the default 65,535-byte window. The LZ distance
is u16, so the maximum window is 65,535 bytes;
a 128 KiB LZ window is outside this version's wire format.

## Runtime memory check

A temporary QEMU ISO contained ten wallpapers, including four copies of the
7,132,038-byte photo in BTP2 format. Its `tapety.pak` was 28,667,356 bytes.
The serial log confirmed that the Multiboot2 package memory was returned to
PMM after import. The 1280×720 prepared wallpaper buffer was 3,686,400
bytes. The 100-switch wallpaper stress check completed 100/100 changes and
reported kernel heap usage of 1,053,776 bytes both before and after.
All eight wallpaper test groups passed. The selected photo needs 8,294,400
decoded bytes plus its 7,132,038-byte source file in the 32 MiB wallpaper
scratch area; the ten images are not decoded together.
The final codec also booted with a 3840×2160 Multiboot2 framebuffer request;
the serial log reported `boot=3840x2160x32` and the desktop screenshot was
3840×2160 pixels.
