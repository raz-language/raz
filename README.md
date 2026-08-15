# Raz

**Raz** is a statically typed systems and application programming language built around native performance, explicit control, memory safety, deterministic resource management, and a practical project toolchain.

Raz combines ownership and borrowing with generics, traits, pattern matching, closures, compile-time evaluation, structured async programming, deterministic builds, and multiple code-generation targets. The production compiler is written in Raz and uses a shared HIR/MIR pipeline so language semantics stay independent from backend implementation details.

**Language design: Mario Vinciguerra.**

## Language highlights

- **Native by default** — compile through Forge or LLVM to platform-native objects and executables.
- **Ownership and borrowing** — moves, shared references, mutable references, non-lexical loan analysis, partial moves, reborrows, and deterministic destruction.
- **Expressive type system** — structs, payload enums, tuples, arrays, slices, references, raw pointers, and function pointers.
- **Generics and traits** — bounds, associated types and constants, supertraits, static dispatch, and object-safe dynamic dispatch.
- **Pattern matching** — exhaustive matching over supported enum and value patterns.
- **Closures** — immutable, mutable, and once-only capture semantics.
- **Compile-time programming** — const functions, const generics, compile-time assertions, layout queries, and selected reflection.
- **Structured async** — `async fn`, `spawn`, `await`, futures, cancellation, and ownership checks across suspension points.
- **Systems standard library** — collections, iterators, filesystem and process APIs, synchronization, sockets, codecs, JSON, HTTP, and TLS integration.
- **Deterministic project model** — manifests, lockfiles, module fingerprints, incremental caches, content-addressed native linking, and reproducible recursive self-hosting.
- **Built-in package management** — semantic-version resolution, deterministic `.dpk` archives, integrity verification, offline caching, mirrors, signing, and the official GitHub-backed package registry.

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

A Raz project uses `raz.toml` for package metadata and keeps source under `src/`.

```text
hello/
  raz.toml
  src/
    main.rz
```

Create and run a project:

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

Inspect the installed toolchain:

```text
raz --help
raz --version
raz doctor
raz backends
raz targets
```

## Compiler architecture

The production compiler is self-hosted and organized as independent Raz modules under `compiler/src/`.

```text
Raz source
   │
   ▼
Parser + semantic analysis
   │
   ▼
Typed HIR
   │
   ▼
Verified MIR
   │
   ├──────────────┬──────────────┬──────────────┐
   ▼              ▼              ▼              ▼
 Forge           LLVM           WASM            RXE
 native          native         .wasm           .rxe
```

MIR is the backend-neutral semantic boundary. Ownership cleanup, call signatures, aggregate layout, async state, globals, and reference behavior are decided before backend emission.

### Forge

Forge is Raz's default native backend. It provides typed SSA verification, optimization, machine lowering, ABI lowering, register allocation, instruction encoding, and deterministic ELF/COFF object emission. The self-hosted compiler talks to Forge in-process through the audited Forge bridge.

### LLVM

The LLVM backend is implemented in Raz and emits LLVM IR from the same verified MIR used by Forge. External LLVM/Clang tools can then produce objects and executables for supported targets.

### WebAssembly

The WASM backend emits WebAssembly from verified MIR, including deterministic runtime memory handling, WASI integration, async/future support, SIMD coverage, and ABI validation.

### RXE

RXE (Raz Executable) is Raz's deterministic bytecode format. It provides a compact verified executable image with canonical instructions, callable metadata, aggregate layouts, module fingerprints, exports, and a serialized control-flow directory.

See [Backends](docs/backends.md), [WASM ABI](docs/WASM-ABI-v1.md), and [RXE](docs/RXE.md).

## Package management

The official package registry is the public [`raz-language/packages`](https://github.com/raz-language/packages) repository. Normal package commands require no registry configuration.

```text
raz add json
raz add http@^2.1.0
raz update
raz lock
raz metadata
raz graph
```

Path dependencies are also supported:

```text
raz add math ../math
```

Registry packages are integrity-checked and stored in a shared content-addressed cache. `RAZ_OFFLINE=1` requires already-cached, integrity-valid package content. Private registries and mirrors remain available through environment configuration.

Create a deterministic package archive with:

```text
raz pack
```

Prepare a package for the official GitHub registry with:

```text
raz publish
```

This creates a `.raz-publish/` submission containing the package archive and registry index record. Published package versions in the official registry are immutable.

See [Package management](docs/PACKAGE-MANAGEMENT.md).

## Standard library

The standard library is written primarily in Raz and lives under:

```text
library/core/     language foundations
library/alloc/    allocation-backed data structures
library/std/      operating-system and application APIs
library/platform/ target-specific interfaces
```

Native C++ code is restricted to permanent host and ABI boundaries such as allocation primitives, raw memory operations, filesystem/process access, sockets, TLS, platform queries, cryptographic engines, and backend bridges. Higher-level policy belongs in Raz.

## Building Raz

Raz uses a small native bootstrap seed to build the self-hosted compiler and verify deterministic recursive compiler generation.

### Windows

```powershell
./bootstrap.bat
```

### Linux and macOS

```sh
./bootstrap.sh
```

Useful bootstrap options include `--clean`, `--run-tests`, `--jobs N`, `--host-preset release`, and `--bootstrap-profile debug|release`.

The native host components can also be built directly with CMake:

```text
cmake --preset release
cmake --build --preset release
```

See [Self-hosting](docs/SELF-HOSTING.md) and [Windows bootstrap](docs/WINDOWS-ALL-STAGES-BUILD.md).

## Repository layout

```text
compiler/   self-hosted Raz compiler
library/    Raz standard library
src/        native bootstrap, runtime boundaries, and Forge
examples/   language and backend examples
tests/      conformance and integration tests
docs/       language, toolchain, backend, and architecture reference
scripts/    build, verification, qualification, and packaging utilities
```

## Documentation

Start with [docs/README.md](docs/README.md).

Core references:

- [Raz Made Easy](docs/Raz_Made_Easy_1.0.0.md)
- [Language specification](docs/LANGUAGE-SPECIFICATION.md)
- [Stable language scope](docs/STABLE-LANGUAGE-SCOPE.md)
- [CLI reference](docs/CLI.md)
- [Package management](docs/PACKAGE-MANAGEMENT.md)
- [Toolchain specification](docs/TOOLCHAIN-SPECIFICATION.md)
- [Compiler architecture](docs/ARCHITECTURE.md)
- [MIR](docs/MIR.md)
- [Backends](docs/backends.md)
- [Language server](docs/LANGUAGE-SERVER.md)
- [Formatting](docs/FORMATTING.md)

## License

Raz is licensed under the [Apache License 2.0](LICENSE). Maintained source, build, and script files carry SPDX `Apache-2.0` headers. Forge retains its nested Apache-2.0 license for independent redistribution.

See [Licensing](docs/LICENSING.md) and [NOTICE](NOTICE).
