# Ring 3 image decoding

`dekoder_obrazu.h` is the shared image surface API. It recognizes JPEG, PNG,
GIF and BTP1/BTP2, returns a validated packed BGRA8888 surface, and reserves
a format identifier for a future BMP adapter. Consumers own the output and
release it with the allocator passed to `dekoduj_obraz`.

JPEG entropy and DCT decoding uses the vendored `stb_image.h` v2.30 in
`obrazy/vendor/`, compiled with `STBI_ONLY_JPEG`, `STBI_ONLY_PNG` and `STBI_ONLY_GIF`, no filesystem
I/O, no SIMD and no thread-local storage. It runs in Ring 3 image consumers,
including Tapety and the Husarz resource worker. A
separate bounded marker/table/dimension pass rejects malformed input before
`stb_image` allocates decoded surfaces. The public API returns no surface on
failure. Baseline grayscale and YCbCr 4:4:4, 4:2:2, and 4:2:0 are supported;
the original progressive 1920×1080 JPEG is also supported. Twelve-bit,
arithmetic, CMYK, and other MCU layouts are outside the current contract.

Tapety reads JPEG and user BTP through BWS file wrappers, previews them in Ring 3, then
submits a BGRA8888 pointer with exact dimensions, stride, and byte count to
its dedicated IPC endpoint. The kernel validates the caller's PZB rights,
path, pixel format, size arithmetic and user pages, copies the surface into
private wallpaper storage, and uses the existing scaler. JPEG bytes never
enter a kernel JPEG parser. The selected path is stored in `pulpit.cfg`; a
small Ring 3 Tapety restore run decodes the original file at boot. There is
no BTP cache and no source-file modification.

Run `make test-image` for ASan/UBSan malformed-image and format tests. The
test checks the SHA-256 of the unchanged reference JPEG before and after.

GIF support added on 2026-10-23 uses the already vendored `stb_image.h` v2.30
(public domain or MIT, the dual license remains at the end of that file).
No new upstream files or library were incorporated. Local GIF changes replace
recursive LZW prefix traversal with a bounded 8,192-byte heap-owned stack,
validate prefix ordering, and allocate first-frame decoder state on the heap.
`STBI_NO_GIF_ANIMATION` excludes the eager all-frames API entirely.

The shared decoder recognizes GIF87a/GIF89a, including interlacing and transparent
pixels, and returns only the first frame. `sprawdz_gif` validates dimensions,
frame rectangles, color table availability, extension/sub-block bounds and a
maximum of 256 image descriptors before decoding. Generic image limits remain
16 MiB source, 8,192 pixels per side and 8 Mi pixels; Husarz tightens these to
240 KiB source, 2,048 per side, 384 Ki pixels and its existing decoder allocation
budget. GIF executes in the native resource worker, outside the GUI event path.
It does not animate, allocate subsequent decoded frames, follow references or
execute scripts. IMG and CSS background resources share decoded cache storage.

`make test-husarz-gif-host` exercises static/transparent/animated-first-frame
samples, every truncated prefix, invalid dimensions and deterministic mutations
under ASan/UBSan. Fixtures are generated locally with Pillow; no third-party
artwork was incorporated.
