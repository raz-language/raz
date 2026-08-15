# Raz

Raz is a statically typed native programming language for systems and application development. It combines explicit low-level control with ownership and borrowing, deterministic resource management, generics, traits, pattern matching, closures, compile-time evaluation, structured async support, and a modern project toolchain.

**Language design and development: Mario Vinciguerra.**

The production compiler is written in Raz. Source is lowered through typed HIR and backend-neutral MIR, then compiled by either **Forge**, the default native backend, or the Raz-written **LLVM backend**.

**MIR Phase 2 is complete:** executable MIR is structurally verified, optimized through a backend-neutral pass pipeline, and independently checked for initialization, moves, partial moves, projection-aware loans, non-lexical loan regions, and reborrow provenance before either backend may consume it.

## Highlights

- Native compilation with Forge or LLVM.
- Ownership, moves, shared references, mutable references, lifetime checks, partial moves, and deterministic destruction.
- Structs, payload enums, tuples, fixed arrays, slices, references, raw pointers, and function pointers.
- Generic functions and types, trait bounds, associated types/constants, supertraits, static dispatch, and object-safe dynamic dispatch.
- Exhaustive pattern matching.
- Closures with immutable, mutable, and once-only capture semantics.
- Const functions, const generics, compile-time assertions, layout queries, and selected reflection.
- `async fn`, `spawn`, and `await` with ownership checks across suspension points.
- Typed collections, iterators, filesystem/process APIs, sockets, buffering, codecs, JSON, HTTP, and TLS integration.
- Deterministic package graphs, lockfiles, semantic-version registry resolution, HTTP/HTTPS registry mirrors, deterministic `.dpk` archives, authenticated publishing, integrity verification, and a shared content-addressed package store.
- Built-in project commands for build, run, check, test, lint, formatting, documentation, and package management.

## A quick look

Raz uses type-first declarations and semicolon-terminated statements.

```raz
public fn add(i64 left, i64 right) -> i64 {
    return left + right;
}

fn main() -> i64 {
    i64 answer = add(20, 22);
    return answer;
}
```

### Ownership and references

```raz
struct Resource {
    i64 handle;
}

fn increment(i64& mut value) {
    *value += 1;
}

fn main() -> i64 {
    Resource first = Resource(7);
    Resource second = move first;

    i64 value = 41;
    increment(&mut value);
    return second.handle + value;
}
```

### Traits and generics

```raz
trait Measurable {
    fn measure(Self& self) -> i64;
}

struct Metric {
    i64 value;
}

impl Measurable for Metric {
    fn measure(Metric& self) -> i64 {
        return self.value;
    }
}

fn read<T: Measurable>(T& value) -> i64 {
    return value.measure();
}
```

### Arrays and iteration

```raz
fn sum() -> i64 {
    i64 values[4] = [1, 2, 3, 4];
    i64 total = 0;

    for value in values {
        total += value;
    }

    return total;
}
```

## Projects

A normal project contains a `raz.toml` manifest and source under `src/`.

```text
hello/
  raz.toml
  src/
    main.rz
```

Create one with:

```text
raz new hello
cd hello
raz run src/main.rz
```

Common commands:

```text
raz build
raz run
raz check
raz test
raz lint
raz fmt
raz doc
raz clean
```

Inspect the toolchain with:

```text
raz --help
raz --version
raz doctor
raz backends
raz targets
```

## Backends

Forge is the default backend:

```text
raz build --backend=forge
raz forge source.rz output.fir
raz forge --forge-native source.rz output.obj
```

LLVM is available explicitly:

```text
raz build --backend=llvm
raz llvm --emit=llvm source.rz output.ll
raz llvm --emit=obj --opt=3 source.rz output.obj
raz llvm --emit=exe source.rz app.exe
```

Both backends consume the same HIR/MIR language semantics.

## Packages

Path dependencies can be added directly:

```text
raz add math ../math
```

Registry dependencies use semantic-version constraints:

```text
raz add json registry:json@^1.2.0
raz update
raz lock
raz metadata
raz graph
```

Registry packages are integrity-checked and materialized into a shared content-addressed store. Raz uses `RAZ_PACKAGE_STORE` when set, otherwise `RAZ_HOME/store`, then `~/.raz/store` (or `%USERPROFILE%\.raz\store` on Windows). Set `RAZ_OFFLINE=1` to require already-cached, integrity-valid registry packages. `raz pack` creates deterministic `.dpk` archives and `raz publish` supports HTTP/HTTPS or filesystem registries, Bearer authentication, and an optional detached-signature header.

See [Package management](docs/PACKAGE-MANAGEMENT.md).

## Build the compiler

### Portable bootstrap

The all-stage bootstrap is driven by Python and supports Windows, Linux, and macOS. It configures the native host toolchain, builds the Raz-written compiler, recursively rebuilds Stages 2 through 4, smoke-tests each compiler, and verifies byte-identical fixed-point output.

Windows:

```powershell
./bootstrap.bat
```

Linux/macOS:

```sh
./bootstrap.sh
```

Use `--clean`, `--run-tests`, `--jobs N`, `--host-preset release`, and `--bootstrap-profile debug|release` as needed. The Windows launcher also accepts the previous PowerShell-style spellings such as `-Clean` and `-Jobs 16`.

### CMake

```text
cmake --preset release
cmake --build --preset release
```

See [Self-hosting](docs/SELF-HOSTING.md) and [Windows bootstrap notes](docs/WINDOWS-ALL-STAGES-BUILD.md).

## Repository layout

```text
compiler/   Raz-written compiler
library/    Raz standard library
src/        frozen native bootstrap/runtime boundaries and vendored Forge
examples/   language and backend examples
tests/      conformance and integration tests
docs/       language, toolchain, backend, and architecture documentation
scripts/    build, qualification, and packaging utilities
```

## Documentation

- [Language server and IDE integration](docs/LANGUAGE-SERVER.md)
- [CLI and diagnostics](docs/CLI.md)

Start with [docs/README.md](docs/README.md).

Key references:

- [Raz Made Easy](docs/Raz_Made_Easy_1.0.0.md)
- [Language specification](docs/LANGUAGE-SPECIFICATION.md)
- [CLI reference](docs/CLI.md)
- [Package management](docs/PACKAGE-MANAGEMENT.md)
- [Toolchain specification](docs/TOOLCHAIN-SPECIFICATION.md)
- [Compiler architecture](docs/ARCHITECTURE.md)
- [Backends](docs/backends.md)


## License

Raz is licensed under the [Apache License 2.0](LICENSE). Maintained source/build/script files carry SPDX `Apache-2.0` headers, and Forge retains its nested Apache-2.0 license for independent redistribution. See [Licensing](docs/LICENSING.md) and [NOTICE](NOTICE).
