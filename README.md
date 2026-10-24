# asv8 (as v8)

Run arm64 binaries on armv8l systems: 64-bit ARM kernel but 32-bit userspace
The kernel can execute aarch64 binaries but the userland ships no aarch64 loader or libraries

asv8 is a single static armv7 binary with everything inside: the aarch64 musl loader, the Alpine Linux libraries, and a preload library (dns64 + shield)

The best part: it runs **natively**

## Usage

```sh
./asv8 /path/to/binary [args...]
./asv8 binary-in-PATH [args...]
```

## Build

```sh
apt install clang cmake binutils
cmake -B build
cmake --build build
```

The first configure downloads the Alpine aarch64 minirootfs (only `lib/` and `usr/lib/` are used), cross-builds `preload64.so` from `preload/` with `-target aarch64-linux-musl`, and links `./asv8` in the project root

## How it works

1. On first run, extracts the embedded bundle into `~/.cache/asv8/<id>/` later runs skip extraction
2. Resolves the target: direct path or `$PATH` search
3. Executes `ld-musl-aarch64.so.1 --preload lib/preload64.so --library-path lib:usr/lib <binary>`. `<id>` is a hash of the bundle, so rebuilding with different libs extracts into a new directory (Clean up with `rm -rf ~/.cache/asv8`)

## Adding libs

Drop any file into `payload/` and rebuild. It gets embedded into the runtime's `lib/` and put on the loader's library path:

```sh
cp libfoo.so.1 payload/
cmake --build build
```

## ...some Alpine info

Defaults to 3.24.2 aarch64 with a pinned sha256:

```sh
cmake -B build -DALPINE_VERSION=3.24.3 -DALPINE_SHA256=<sha256>
```

The sha is published next to the tarball as `alpine-minirootfs-<version>-aarch64.tar.gz.sha256`
