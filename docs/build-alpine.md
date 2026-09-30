# Alpine Linux builds

`build-alpine.sh` builds Lapis Obsidian using Alpine's default `/bin/sh`, a C
compiler and binutils. It supports 64-bit RISC-V (`riscv64`), ARM64 (`aarch64`)
and x86-64 (`x86_64`). No Bash, Java, vanilla server JAR, Node, Docker, registry
generation or game assets are needed for this build. The script downloads
nothing and does not install packages or start the server.

## Native build

Run the package installation as root on the target Alpine machine:

```sh
apk add --no-cache build-base
./build-alpine.sh
./lapis-obsidian
```

The script detects the compiler's target, so the same commands work on each
supported architecture. Optionally require a particular target:

| Machine | Command |
| --- | --- |
| RISC-V 64-bit | `./build-alpine.sh --arch riscv64` |
| ARM64 | `./build-alpine.sh --arch arm64` |
| x86-64 | `./build-alpine.sh --arch x64` |

`aarch64`, `x86_64` and `amd64` are also accepted. RISC-V 32-bit and ARM32 are
not targets of this script. It uses the toolchain's default ISA/ABI, without
`-march=native` or CPU-specific tuning, and retains `-ffp-contract=off` for the
generator's floating-point behavior. Use a toolchain whose baseline matches
your hardware.

## Options

```sh
./build-alpine.sh --static
./build-alpine.sh --debug
./build-alpine.sh --arch riscv64 --output lapis-obsidian-alpine-riscv64
./build-alpine.sh --help
```

The default is a dynamically linked release build at `<repository>/lapis-obsidian`.
`--static` links musl statically and needs the toolchain's static libraries.
`--debug` adds debug symbols, conversion/shadow warnings, ASan and UBSan; it needs
those sanitizer runtimes for the target and cannot be combined with `--static`.
Inherited core conversion warnings may still appear in debug builds.

Relative `--output` paths are relative to the directory from which you invoke
the script. Output directories are created after compilation. A failed compiler
or libc check leaves the previous output intact. `CC` is one compiler executable
or wrapper path, not a string of compiler options; `READELF` may select an
alternative ELF inspection executable.

## Cross compilation

`--arch` verifies the chosen compiler; it does not install a toolchain or turn
native GCC into a cross compiler. On another host, provide an already configured
Linux/musl compiler, its target headers/libraries and sysroot. For example, if
you have that toolchain installed:

```sh
CC=riscv64-linux-musl-gcc ./build-alpine.sh --arch riscv64 --static \
  --output lapis-obsidian-alpine-riscv64
CC=aarch64-linux-musl-gcc ./build-alpine.sh --arch arm64 --static \
  --output lapis-obsidian-alpine-aarch64
```

Executable names vary by toolchain. Native Alpine GCC needs no `CC` override.
A glibc toolchain such as `aarch64-linux-gnu-gcc` is not sufficient. The script
compiles a tiny 64-bit probe, checks its ELF interpreter is exactly Alpine's
`/lib/ld-musl-<arch>.so.1`, and rejects a mismatched target or libc. This also
supports a musl-gcc wrapper whose `-dumpmachine` still reports a GNU triple.
The probe is dynamically linked even with `--static`; the toolchain must support
both its normal musl linking mode and the requested final link mode. Neither
probe nor server executable is run, so cross builds need no emulator.

## Validation and current limits

Validated under POSIX `sh` with a real x86-64 musl toolchain: dynamic and static
server compilation, ELF interpreter/static-link checks, and live TCP startup,
configuration, chunk transmission, login and command smoke tests for both
binaries. Also checked unsupported targets, target mismatches, missing option
values, unknown options, incompatible debug/static options, glibc rejection and
preservation of an existing output after failure.

These checks ran on an Ubuntu host using musl built locally for verification;
they are not a native Alpine OS test. No compiler or musl sources are included
in Lapis Obsidian. ARM64 and RISC-V builds/runtime have not been exercised in
this environment; native validation on those machines remains required. Existing
world/player files use the inherited raw layouts; cross-architecture save-file
portability is not established by providing a build script.

The full existing C regression suite uses Bash. To run it natively on Alpine,
install Bash as well (`apk add --no-cache bash`) and run `./tests/run.sh`.
Sanitizer tests additionally require target sanitizer support. The portable
`./build.sh` remains available for the other existing build environments.

Reference: Alpine's [GCC guide](https://wiki.alpinelinux.org/wiki/GCC) documents
`build-base`; its [package index](https://pkgs.alpinelinux.org/packages?name=build-base)
provides packages by release and architecture.
