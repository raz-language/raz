# Release readiness

Forge 2.0.0, released July 29, 2026, is a stable release of the documented compiler core, frontend SDK, ABI-classification API, native library workflows, and x86-64 scalar/pointer backend.

## Required release gates

- Strict Release build with warnings treated as errors
- Complete CTest matrix
- ASan and UBSan matrix with leak detection
- Parser and binary fuzz-smoke targets
- Verification of every checked-in `.fir` example
- Deterministic canonical optimization output at every optimization level
- Deterministic ELF and COFF objects
- Native compile, link, and execute workflow
- Incremental object and final-binary cache tests
- Isolated installed C and C++ package consumers
- Exact backend code-quality baselines
- Repository-hygiene checks
- License-header coverage for maintained source and build files

## Supported production surface

- Textual and binary Forge IR
- IR construction, verification, and canonical printing
- Optimization levels `-O0`, `-O1`, `-O2`, `-O3`, `-Os`, and `-Oz`
- Reference interpreter
- x86-64 JIT on supported hosts
- System V AMD64 and Windows x64 scalar/pointer calls
- ELF64 and COFF AMD64 object generation
- C++ SDK and opaque C API v16
- Incremental fingerprints, dependency planning, native function artifacts, object assembly, and final-binary caching
- Installed CMake package through `Forge::forge`

## Explicitly unsupported or incomplete

- Native by-value aggregate ABI classification
- True variadic function definitions
- Unwind and debug metadata
- Segmented live-range register allocation
- Production-parity architectures other than x86-64; AArch64 ELF is currently an experimental cross/native object backend

A release must not claim these capabilities until implementation, semantic tests, platform tests, and release gates are complete.

## Forge 1.3 additions

- Alias-analysis and LICM regressions cover disjoint memory, overlapping ranges, natural loops, and interpreter-equivalent transformations.
- The `-O2` and `-O3` release gates exercise memory forwarding and loop-invariant code motion.


- System V AMD64 and Windows x64 aggregate ABI classification
- Calling-convention, variadic, linkage, and visibility IR metadata
- Deterministic static archives with native symbol indexes
- Host-toolchain shared-library linking
- Native static/shared library link-and-run release gates

ABI classification is production-supported as an analysis API. Full register-classified by-value aggregate machine lowering is not part of the 1.2 support contract.

### Sanitizer boundary

Forge 2.0.0 uses two explicit gates. The strict production matrix runs all 66 tests, including JIT execution and installed C/C++ consumers. The ASan/UBSan core matrix runs 56 sanitizer-safe tests and excludes only dynamic entry into uninstrumented generated code plus separately linked installed-consumer processes. Both matrices pass on the release source.

## Release artifacts

`scripts/release-gate.sh` and `scripts/release-gate.ps1` build the strict release matrix, verify the reported `2.0.0` version, generate binary and source packages with CPack, and write `_packages/SHA256SUMS`. A release is incomplete unless the strict tests, installed-consumer tests, package creation, and checksums all succeed.
