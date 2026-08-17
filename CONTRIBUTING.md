# Contributing to Raz

Thank you for helping improve Raz. The production compiler is implemented in Raz, so changes must preserve compiler correctness, host-compiler compatibility, and reproducible compiler construction.

## Design rules

1. **Forge remains the sole production backend.** Do not add a C emitter, LLVM fallback, or alternate production code generator.
2. **Language behavior belongs in Raz.** Do not move compiler semantics or standard-library functionality into native shims simply because the Raz implementation is harder.
3. **Keep the host compiler compatibility-stable and native boundaries narrow.** C++ is appropriate for the host compiler and permanent runtime/OS/ABI boundaries that cannot reasonably live in Raz. New language semantics belong under `compiler/src/`.
4. **Preserve determinism.** Source discovery, dependency ordering, generated identifiers, and compiler output must remain reproducible.
5. **Keep one canonical compiler source set.** The production compiler lives under `compiler/src/` as semantic modules with explicit imports; `src/main.rz` is the entry module. `compiler/host-source-order.txt` is host-compiler ordering metadata only. Do not reintroduce `compiler/source-order.txt` or commit generated compiler mirrors.

## Before opening a change

For normal native/compiler work:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

For Windows release/compiler reproducibility changes:

```bat
bootstrap.bat -Clean -RunTests
```

The compiler repository does not build installers. Binary packaging and publishing
are maintained in the separate `raz-language/installer` repository.

Also run the focused checks that match your change. Useful repository scripts include:

```text
tests/python/check-layout.py
tests/python/check-native-boundary.py
tests/python/check-forge-package.py
tools/format-raz.py
```

## Style and comments

Run the Raz formatter on the files or directories you changed before committing:

```text
python tools/format-raz.py compiler/src/driver/my_change.rz
python tools/format-raz.py compiler/src/driver/my_change.rz --check
```

Raz source uses four-space indentation; Raz-owned C++ follows the repository `.clang-format`/`.editorconfig`. Vendored Forge source keeps its upstream style. Comments should explain intent, invariants, or a non-obvious tradeoff rather than narrating syntax a reader can already see. See [Source formatting](docs/FORMATTING.md) for the full repository convention.

## Tests

Add positive and negative fixtures where appropriate. A language feature is not complete when it only parses: qualification should cover the relevant semantic, HIR, MIR, Forge IR, runtime, and native-execution behavior when those layers apply.

Regression tests should describe general language/compiler behavior rather than one benchmark-specific or source-specific special case.

## Commit scope

Keep generated build output out of commits. The repository `.gitignore` excludes CMake/Ninja products, compiler artifacts, IDE state, caches, and project targets.


## Licensing of contributions

Raz is Apache-2.0 licensed. Unless explicitly stated otherwise, work intentionally submitted for inclusion in Raz is contributed under Apache-2.0. New maintained source, build, and script files must include the repository copyright/SPDX header. Run:

```text
python tests/python/check-license-headers.py
```

before submitting changes. Preserve third-party copyright, license, and NOTICE material when importing external code. See [docs/LICENSING.md](docs/LICENSING.md).
