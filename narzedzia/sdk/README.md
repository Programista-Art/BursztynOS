# BursztynOS SDK

The native POSIX SDK is the musl sysroot produced by:

```text
make test-musl-host
```

It is installed locally under `.musl-build/sysroot/` and contains standard C
headers, `crt1.o`, `libc.a`, and the self-contained
`<bursztyn/syscall.h>` ABI header. Programs using this SDK must not include
kernel or compositor headers.

The older `bursztyn_api.h` BWS API remains available for `.bur` applications.
It is intentionally separate from the POSIX ABI so existing applications keep
working while native user space is introduced.
