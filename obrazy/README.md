# Ring 3 image decoding

`dekoder_obrazu.h` is the shared image surface API. It recognizes JPEG and
BTP1/BTP2 today, returns a validated packed BGRA8888 surface, and reserves
format identifiers for BMP and PNG adapters. Consumers own the output and
release it with the allocator passed to `dekoduj_obraz`.

JPEG entropy and DCT decoding uses the vendored `stb_image.h` v2.30 in
`obrazy/vendor/`, compiled with `STBI_ONLY_JPEG`, no filesystem I/O, no SIMD,
and no thread-local storage. It runs only in the Tapety Ring 3 binary. A
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
