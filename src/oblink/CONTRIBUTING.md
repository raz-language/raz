# Contributing to ObLink

ObLink turns Microsoft COFF objects and libraries into deterministic PE32+
executables. Contributions should preserve that determinism and keep every
input path defensive.

## Before opening a pull request

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

## Contribution requirements

- Build cleanly with warnings treated as errors.
- Every successful link must emit a structurally valid image. Tests should use
  `oblink::testing::validate_pe` rather than asserting on the `MZ` signature —
  an image whose section table is internally inconsistent is written happily and
  then rejected by the loader, which a signature check cannot distinguish from
  success.
- Add a focused regression for behavioral changes, and confirm it fails with the
  fix reverted. A test that passes either way documents nothing.
- Treat all input as untrusted. Bounds-check offsets read from headers; a
  truncated or malformed input is a diagnostic, never an out-of-bounds read.
- Keep output deterministic. Archive member selection, section ordering, and
  symbol resolution must not depend on hash iteration order or timestamps.
- Unresolved symbols stay hard errors.
- Update `docs/compatibility.md` when changing what ObLink accepts from a
  producer, and `docs/roadmap.md` when completing a roadmap item.

## Debugging a link

- `--map <path>` writes section and symbol addresses. Diagnosing a bad
  relocation without one means disassembling the output and guessing.
- `--verbose` reports which archive member satisfied which symbol, which is how
  you find a member pulled in for a symbol nobody expected to be live.

## Scope

ObLink owns symbol resolution, relocation, image construction, libraries, and
imports. Code generation and object emission belong to Forge; changes that
belong on the producer side should go there instead.
