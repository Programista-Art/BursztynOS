# BTP wallpaper binary format

All multibyte integers are unsigned little endian. Bytes are indexed from zero. A file has no padding or trailing records. Pixels are ordered from the top left, row major, with no row padding. The stored pixel format is BGRA8888 (format ID 1): B, G, R, A bytes. BTP2 preserves source alpha; the current wallpaper compositor uses that high byte as a visibility marker, not for alpha blending. The codec preserves all four supplied bytes exactly.

## BTP1 (legacy)

The 14-byte header is `BTP1` at 0, width u32 at 4, height u32 at 8, bits per pixel u8 (=32) at 12 and compression u8 at 13. Mode 0 is exactly `width*height*4` pixel bytes. Mode 1 consists of five-byte records: run length u8 (1..255), followed by one BGRA pixel. Runs may cross scanlines. No CRC is defined. The decoder rejects missing or extra bytes, zero dimensions, dimensions above 8192 on either axis, invalid modes, and runs that underfill or overfill the image. BTP1 remains readable.

## BTP2 header (72 bytes)

| Offset | Width | Field | Required value or meaning |
| --- | ---: | --- | --- |
| 0 | 4 | magic | `BTP2` |
| 4 | 2 | version | 2 |
| 6 | 2 | header size | 72 |
| 8 | 4 | width | 1..8192 |
| 12 | 4 | height | 1..8192 |
| 16 | 1 | pixel format | 1 = BGRA8888 |
| 17 | 1 | method | 1 = independent chunk LZ/RLE |
| 18 | 2 | flags | 0 |
| 20 | 8 | decoded bytes | exactly width × height × 4 |
| 28 | 8 | compressed bytes | bytes after chunk table |
| 36 | 4 | chunk count | ceiling(decoded bytes / chunk size) |
| 40 | 4 | chunk size | multiple of four, 4..131072; default 65536 |
| 44 | 8 | chunk table offset | 72 |
| 52 | 8 | payload offset | 72 + chunk count × 20 |
| 60 | 4 | decoded CRC32 | IEEE CRC32 of the complete unfiltered pixel stream |
| 64 | 4 | payload CRC32 | IEEE CRC32 of all bytes from payload offset to EOF |
| 68 | 4 | reserved | 0 |

The payload must extend to EOF exactly. Each chunk table entry is 20 bytes:

| Relative offset | Width | Field |
| --- | ---: | --- |
| 0 | 8 | absolute payload offset |
| 8 | 4 | stored byte count |
| 12 | 4 | decoded byte count |
| 16 | 1 | storage mode: 0 RAW, 1 token stream |
| 17 | 1 | filter: 0 none, 1 horizontal delta, 2 vertical delta |
| 18 | 2 | reserved zero |

Chunks are contiguous in table order, beginning exactly at payload offset, without gaps or overlap. Every chunk except the last decodes to `chunk size` bytes; the last decodes to the remainder. RAW requires stored byte count = decoded byte count and filter 0. Encoder picks RAW whenever tokens do not save bytes. Each chunk starts with empty LZ history; no reference crosses a chunk boundary.

## Filters and token stream

Filtering is applied to a chunk before tokenization. Each byte operation is modulo 256. Horizontal delta at byte index `i>=4` subtracts the original byte `i-4`; vertical delta at `i>=width*4` subtracts the original byte one row earlier. Bytes before the respective distance pass through unchanged. The inverse adds the already reconstructed prior byte. Distances are local to the chunk and therefore independent of chunk alignment to rows. A RAW chunk always has filter 0.

Each token starts with one command byte. The top two bits select its type; the low six bits are `n`:

| Top bits | Type | Output length | Following bytes |
| --- | --- | --- | --- |
| `00` | LITERAL | `n+1` bytes | exactly those bytes |
| `01` | RLE BYTE | `n+3` bytes | one repeated byte |
| `10` | RLE PIXEL | `n+2` four-byte pixels | one four-byte pattern |
| `11` | LZ MATCH | `n+3` bytes | distance u16 LE |

For RLE BYTE, RLE PIXEL and LZ MATCH, a low-six-bit value of 63 is an
extension marker: read a u16 LE length before the following byte/pixel/distance.
The minimum extended lengths are 66 bytes, 65 pixels and 66 bytes,
respectively. Zero, shorter, and truncated extended lengths are invalid.
LITERAL `n=63` retains its ordinary 64-byte meaning. This gives flat-color
wallpapers long pixel runs without allocating additional decoder memory.

LZ distance must be in 1..65535 and no greater than bytes already emitted by this chunk. Copy one byte at a time from `output[current-distance]`, allowing overlap. The token stream must end exactly at the stored byte count and emit exactly the declared decoded byte count. A command that reads outside its chunk or emits too many bytes is invalid. There is no end marker or implicit padding.

The encoder uses deterministic bounded hash chains (at most 24 candidate positions per three-byte key), a configurable window up to 65535 bytes, and selects byte RLE, pixel RLE, or LZ by estimated bytes saved. `--filter auto` tests all reversible filters and selects the smallest representation per chunk. The default is `none`: on the supplied photograph filters saved only 3.5% while making encoding roughly 12 times slower. RAW fallback applies in either case. The decoder is integer only, iterative and requires no compression library.

## Validation and runtime policy

Readers reject unknown versions, format IDs, methods, flags, filters, nonzero reserved fields, zero dimensions, incorrect byte counts, offsets outside the file, overlapping or missing chunks, truncated tokens, zero or out-of-history LZ distances, output overflow, trailing data and CRC mismatches. Calculate dimensions and offsets in checked wide arithmetic before allocation. The kernel accepts axes up to 8192 but the current wallpaper scratch layout additionally limits loaded images to 4,194,304 pixels and BTP files to 16 MiB. The converter enforces the pixel limit with a diagnostic. The decoded CRC is checked after writing the candidate pixels, so callers must supply a private scratch target. The selected wallpaper decodes in scratch memory and is published only after full decode and CRC validation; on failure the current surface stays active. The source surface is reused for a display resolution rebuild. Only one selected wallpaper is decoded at once.

`tapety.pak` remains BTPK1: 32-byte header with u32 sizes and offsets, a variable name table with u32 entry sizes/offsets and per-entry CRC32, then 4-byte-aligned BTP payloads. BTP1 and BTP2 may coexist. The builder rejects any value outside u32 and verifies BTP2 decoded CRC before packaging. The current build/runtime policy allows at most 64 files and 256 MiB total. A package beyond u32 needs a future version rather than field reinterpretation.

## Examples

```sh
python3 narzedzia/tapety-btp/konwerterbtp.py input.png /tmp/input.btp --verify
python3 narzedzia/tapety-btp/konwerterbtp.py input.png /tmp/preview.btp --max-width 1920 --max-height 1080 --fit contain
python3 narzedzia/tapety-btp/konwerterbtp.py input.png /tmp/legacy.btp --btp-version 1
```

The CLI defaults to BTP2 and native dimensions. `--width` and `--height` specify an exact canvas; `--fit contain` preserves aspect and pads, `--fit cover` crops, and `--fit stretch` is the explicit distortion mode. A single specified dimension preserves aspect. `--compression raw|rle|lz|hybrid|auto`, `--chunk-size`, `--window` and `--filter none|horizontal|vertical|auto` tune v2. `--verify` checks exact decoded pixels; `--benchmark` prints size, timing and chunk choices. Output naming never overwrites an existing file. The supplied `tapeta.jpg` is read only and is not a production output.
