# Platform support

Raz keeps source-language semantics independent of the native backend. Platform support is therefore defined by a host/toolchain contract rather than by separate language implementations.

## Qualified native targets

| Target triple | Host use | Native backend | ABI | Object |
|---|---|---|---|---|
| `x86_64-pc-windows-msvc` | native / cross object | Forge or LLVM | Windows x64 | COFF |
| `x86_64-unknown-linux-gnu` | native / cross object | Forge or LLVM | System V AMD64 | ELF64 |
| `aarch64-unknown-linux-gnu` | native / cross object | LLVM | AAPCS64 | ELF64 |
| `arm64-apple-macos` | native / cross object | LLVM | Darwin AArch64 | Mach-O 64 |

AArch64 hosts select LLVM for ordinary native builds. Forge has native AArch64 machine lowering, AAPCS64 ABI classification, physical register allocation, register-native scalar encoding, ELF64 and Mach-O arm64 object emission, Linux TLS IE and Darwin TLV TLS, and target-safe immediate selection. LLVM remains the release default until Forge closes the remaining reusable-DAG/masked/wider-vector, uncommon ABI, JIT, and recursive-bootstrap qualification gaps.

## Cross-target rules

`--emit=llvm` and `--emit=obj` can target a foreign architecture directly through Clang. Executable links are stricter because the runtime and system libraries are architecture/OS specific. A foreign-target executable therefore requires an explicit target runtime archive with `--runtime=<path>`. Sysroot, SDK, and additional linker search options are passed with `--link-arg`, `--library-path`, and `--lib`.

Raz never silently combines a foreign object with the host runtime. This is important for x86-64-to-AArch64 cross compilation: an object can be produced without a target sysroot, while a runnable executable must be linked against target-native libraries.

## Source bootstrap

x86-64 Windows/Linux source builds use the repository's compatibility-pinned C++ host compiler to create the first production compiler. AArch64 and macOS source builds use a compatible prebuilt Raz stage-0 compiler for that first object because the C++ host's Forge native/object path currently covers only x86-64 Windows/COFF and Linux/ELF. The rest of bootstrap is self-hosted through LLVM.

This is a bootstrap limitation, not a language or runtime limitation. Forge has the corresponding AArch64 and Mach-O machine/object paths; the remaining prebuilt stage-0 requirement is a recursive bootstrap/release-qualification constraint rather than an object-format limitation.

## Experimental Forge AArch64 path

The bundled Forge source has AArch64 ELF and Darwin Mach-O object backends with scalar integer/floating code, calls, aggregate ABI pieces, global/function relocations, Linux initial-exec TLS, Darwin TLV TLS, an AArch64 linear-scan allocator with scalar callee-saved allocation, call-local Q allocation in `v16`-`v23`, 16-byte stack homes for Q values live across calls, copy coalescing/CFG-hole recovery/size-aware spill-slot coloring, direct allocated-register scalar codegen, target-safe immediate/canonical combines, 128-bit NEON integer maps/add reductions, and packed chain/postfix-DAG evaluation. LLVM remains Raz's default and release-qualified backend on AArch64 while uncommon ABI cases, reusable/masked/wider vector forms, JIT, and recursive Forge bootstrap qualification remain open.
