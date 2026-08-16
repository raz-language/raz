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
- **Deterministic project model** — manifests, lockfiles, module fingerprints, incremental caches, content-addressed native linking, and reproducible recursive compiler reproducibility.
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

## Installation

Windows releases provide both an MSI installer and a portable ZIP. The MSI installs Raz under `C:\Program Files\Raz` and includes an **Add Raz to PATH** option that is enabled by default and can be deselected during setup. Both distributions include `razup` for versioned stable, nightly, and pinned toolchains.

After installation:

```text
raz --version
raz doctor
razup --version
```

The portable ZIP also includes `install.ps1`, with user, machine, or no-PATH modes:

```powershell
.\install.ps1 -PathScope User
.\install.ps1 -PathScope Machine
.\install.ps1 -PathScope None
```

See [Installing Raz](docs/INSTALLATION.md) for MSI, silent-install, portable-install, and release-layout details.

Toolchains can then be managed with:

```text
razup install stable
razup update
razup default nightly
razup toolchain list
```

Downloaded toolchains are SHA-256 verified before installation.

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
raz run

# Or initialize an existing directory without overwriting existing files:
raz init .
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

The production compiler is written in Raz and organized as independent Raz modules under `compiler/src/`.

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

Forge is Raz's default native backend. It provides typed SSA verification, optimization, machine lowering, ABI lowering, register allocation, instruction encoding, and deterministic ELF/COFF object emission. The production compiler talks to Forge in-process through the audited Forge bridge.

### LLVM

The LLVM backend is implemented in Raz and emits LLVM IR from the same verified MIR used by Forge. External LLVM/Clang tools can then produce objects and executables for supported targets.

### WebAssembly

The WASM backend emits WebAssembly from verified MIR, including deterministic runtime memory handling, WASI integration, async/future support, SIMD coverage, and ABI validation.

### RXE

RXE (Raz Executable) is Raz's deterministic bytecode format. It provides a compact verified executable image with canonical instructions, callable metadata, aggregate layouts, module fingerprints, exports, and a serialized control-flow directory.

See [Backends](docs/BACKENDS.md), [WASM ABI](docs/WASM-ABI-v1.md), and [RXE](docs/RXE.md).

## Package management

The official package registry is the public [`raz-language/packages`](https://github.com/raz-language/packages) repository. Normal package commands require no registry configuration.

```text
raz search algorithms
raz info semver
raz add algorithms
raz add backoff@^1.0.0
raz outdated
raz update
raz lock
raz metadata
raz graph
```

Local, Git, scoped, and feature-gated dependencies use the same package graph:

```text
raz add math ../math
raz add test-kit ../test-kit --dev
raz add tls registry:tls@^1.4.0 --optional
raz add linux-io ../linux-io --target=linux
raz add codec git:https://github.com/example/raz-codec#0123456789abcdef0123456789abcdef01234567
```

`raz.toml` supports `[dependencies]`, `[dev-dependencies]`, `[build-dependencies]`, `[optional-dependencies]`, OS-specific dependency sections, and `[features]`. Builds accept `--features=a,b`, `--all-features`, and `--no-default-features`. Git sources are pinned to full 40-character commit SHAs and are restored by `raz fetch` when their local cache is missing.

Multi-package repositories can declare a workspace at the repository root:

```toml
[workspace]
members = [
    "crates/core",
    "crates/cli",
]
```

Run `raz build --workspace`, `raz check --workspace`, `raz test --workspace`, `raz update --workspace`, `raz fetch --workspace`, `raz lock --workspace`, `raz metadata --workspace`, or `raz graph --workspace` from that root. Workspace locking is centralized in the root `raz.lock`; member-local lockfiles are not retained by workspace update/fetch operations.

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

Raz uses a compact native host compiler to construct the Raz-written production compiler and verify deterministic compiler reproducibility.

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

See [Compiler bootstrap](docs/COMPILER-BOOTSTRAP.md) and [Windows build](docs/WINDOWS-BUILD.md).

## Repository layout

```text
compiler/       production Raz compiler
library/        Raz standard library
src/            host compiler, runtime boundaries, and Forge
tests/          conformance and integration tests
  examples/     language and backend conformance programs
docs/           language, toolchain, backend, and architecture reference
tools/          build, formatting, verification, and repository utilities
```

## Documentation

Start with [docs/README.md](docs/README.md).

Core references:

- [Getting Started](docs/GETTING-STARTED.md)
- [Language specification](docs/LANGUAGE-SPECIFICATION.md)
- [Language stability](docs/LANGUAGE-STABILITY.md)
- [CLI reference](docs/CLI.md)
- [Package management](docs/PACKAGE-MANAGEMENT.md)
- [Toolchain specification](docs/TOOLCHAIN-SPECIFICATION.md)
- [Compiler architecture](docs/ARCHITECTURE.md)
- [MIR](docs/MIR.md)
- [Backends](docs/BACKENDS.md)
- [Language server](docs/LANGUAGE-SERVER.md)
- [Formatting](docs/FORMATTING.md)

## License

Raz is licensed under the [Apache License 2.0](LICENSE). Maintained source, build, and script files carry SPDX `Apache-2.0` headers. Forge retains its nested Apache-2.0 license for independent redistribution.

See [Licensing](docs/LICENSING.md) and [NOTICE](NOTICE).
