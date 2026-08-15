# Contributing to Raz

Thank you for helping improve Raz. The compiler is self-hosting, so changes must preserve both ordinary compiler correctness and recursive bootstrap correctness.

## Design rules

1. **Forge remains the sole production backend.** Do not add a C emitter, LLVM fallback, or alternate production code generator.
2. **Language behavior belongs in Raz.** Do not move compiler semantics or standard-library functionality into native shims simply because the Raz implementation is harder.
3. **Keep Stage 0 frozen and native boundaries narrow.** C++ is appropriate for the frozen bootstrap seed, runtime, OS, filesystem, process, networking, and ABI boundaries that cannot reasonably live in Raz. Do not implement new language semantics in Stage 0 for feature parity; production compiler work belongs under `compiler/src/`.
4. **Preserve determinism.** Source discovery, dependency ordering, generated identifiers, and compiler output must remain reproducible.
5. **Keep one canonical compiler source set.** The self-hosted compiler lives under `compiler/src/` as semantic modules with explicit imports; `src/main.rz` is the entry module. `compiler/bootstrap-source-order.txt` is seed/recovery metadata only. Do not reintroduce `compiler/source-order.txt` or commit generated stage mirrors.

## Before opening a change

For normal native/compiler work:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

For Windows release/self-hosting changes:

```bat
bootstrap.bat -Clean -RunTests
```

For full release qualification:

```bat
release.bat -Clean
```

Also run the focused checks that match your change. Useful repository scripts include:

```text
scripts/check-layout.py
scripts/check-native-boundary.py
scripts/check-forge-package.py
scripts/format-raz.py
```

## Style and comments

Run the Raz formatter before committing:

```text
python scripts/format-raz.py .
python scripts/format-raz.py . --check
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
python scripts/check-license-headers.py
```

before submitting changes. Preserve third-party copyright, license, and NOTICE material when importing external code. See [docs/LICENSING.md](docs/LICENSING.md).
