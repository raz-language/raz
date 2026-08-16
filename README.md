# Raz

**Raz is a statically typed systems programming language for building high-performance native software with explicit control, memory safety, and a complete project toolchain.**

Raz is designed for software where predictable performance matters: command-line tools, network services, runtimes, databases, compilers, infrastructure, and other native applications. The language combines ownership and borrowing with low-level memory access, generics, traits, pattern matching, deterministic destruction, structured concurrency, and multiple production code-generation targets.

The production compiler is written in Raz. Forge is the default native backend, with LLVM, WebAssembly, and RXE available from the same compiler pipeline.

## Highlights

- **Native performance** — direct compilation to platform objects and executables through Forge or LLVM.
- **Memory safety with explicit control** — ownership, moves, borrowing, mutable references, non-lexical loan analysis, deterministic destruction, and `unsafe` for deliberate low-level operations.
- **Systems-oriented type system** — structs, payload enums, tuples, arrays, slices, references, raw pointers, function pointers, generics, traits, associated items, and const generics.
- **Predictable resource management** — no tracing garbage collector and no hidden runtime ownership model.
- **High-performance standard library** — allocation-conscious collections, arenas and pools, buffered and vectored I/O, filesystem and process APIs, sockets, DNS, HTTP, TLS, compression, serialization, atomics, lock-free queues, and readiness-driven networking.
- **First-class project tooling** — builds, tests, formatting, documentation, diagnostics, package management, workspaces, lockfiles, incremental compilation, and language-server support.
- **Multiple backends** — Forge native code, LLVM IR/native code, WebAssembly, and RXE bytecode.
- **Reproducible packages** — semantic-version resolution, content-addressed storage, integrity verification, offline builds, Git dependencies, workspaces, deterministic package archives, and registry publishing.

## Quick start

Create and run a project:

```text
raz new hello
cd hello
raz run
```

A minimal Raz program:

```raz
fn main() -> i64 {
    i64 answer = 40 + 2;
    return answer;
}
```

Common project commands:

```text
raz check
raz build
raz run
raz test
raz fmt --check
raz doc src/main.rz
raz doctor
```

Build an optimized native binary:

```text
raz build --profile release --opt=3
```

## Language overview

Raz uses type-first declarations and semicolon-terminated statements.

### Functions and values

```raz
public fn add(i64 left, i64 right) -> i64 {
    return left + right;
}

fn main() -> i64 {
    i64 result = add(20, 22);
    return result;
}
```

### Ownership and borrowing

```raz
struct Resource {
    i64 handle;
}

fn increment(i64&mut value) {
    *value += 1;
}

fn main() -> i64 {
    Resource first = Resource { handle: 7 };
    Resource second = move first;

    i64 value = 41;
    increment(&mut value);
    return second.handle + value;
}
```

Aggregate values have value semantics: assigning a struct value to another local creates independent aggregate storage unless ownership is explicitly moved.

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

## Toolchain

`raz` is the project driver and primary command-line interface. `razc` is the direct compiler interface.

```text
raz --help
raz --version
raz doctor
raz backends
raz targets
```

Normal build output is concise and package-oriented:

```text
   Compiling core v0.4.2
   Compiling app v1.0.0
    Finished app [release, host] (3 compiled, 8 fresh) in 412 ms
```

Command parsing fails before build work begins, and common mistakes receive actionable diagnostics:

```text
error: no such command: 'biuld'
  help: a similar command exists: 'build'
  help: view all commands with 'raz --help'
```

See [CLI reference](docs/CLI.md) for the complete command and diagnostic interface.

## Standard library

The standard library is implemented primarily in Raz and is organized into three layers:

```text
library/core/    language and runtime-independent foundations
library/alloc/   allocation-backed collections and memory utilities
library/std/     operating-system, networking, concurrency, and application APIs
```

The library includes:

- vectors, deques, hash maps, hash sets, strings, slices, arenas, and fixed object pools;
- files, directories, paths, environment variables, processes, clocks, timers, and random generation;
- buffered, vectored, and byte-oriented I/O;
- TCP, UDP, address parsing, DNS, TLS, HTTP, URL handling, framed transports, and readiness polling;
- JSON, Base64, hexadecimal, binary encoding, CRC-32, and LZ4 block compression;
- threads, atomics, channels, worker executors, and lock-free SPSC/MPMC queues;
- structured logging and allocation-conscious CLI parsing.

Performance-sensitive APIs are designed around retained buffers, caller-owned storage, batching, bounded caches, and reuse. Higher-level policy stays in Raz; native code is restricted to permanent operating-system, ABI, cryptographic-engine, and backend boundaries.

### Readiness-driven networking

For high-connection-count services, `std::net::reactor::BatchReactor` keeps socket watches and monotonic timers in reusable storage. One wait handles the complete socket set and automatically clamps the OS timeout to the nearest scheduled timer.

```raz
BatchReactor reactor = std::net::reactor::batch_create(1024);
std::net::reactor::batch_watch_readable(&mut reactor, listener);
std::net::reactor::batch_schedule_after(&mut reactor, 1, 30000);

i64 ready = std::net::reactor::batch_wait(&mut reactor, -1);
```

Connection interests can be changed in place for backpressure, and watches can be removed in O(1) without rebuilding the poll set.

## Packages and workspaces

The official package registry is hosted at [`raz-language/packages`](https://github.com/raz-language/packages).

```text
raz search json
raz add json
raz add codec@^1.2.0
raz tree
raz update
raz fetch
raz pack
raz publish
```

Raz records exact dependency state in `raz.lock`. Registry packages are integrity-checked and stored in a shared content-addressed cache. `raz fetch` reproduces the lockfile exactly; `raz update` is the operation that re-resolves compatible versions.

A multi-package repository can use a workspace manifest:

```toml
[workspace]
members = [
    "crates/core",
    "crates/cli",
]
```

Workspace builds, checks, tests, updates, fetches, metadata, and dependency graphs operate from the root manifest and share one root lockfile.

See [Package management](docs/PACKAGE-MANAGEMENT.md).

## Compiler and backends

Raz source is analyzed once and lowered through a backend-neutral compiler pipeline:

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
    ├──────── Forge ──────── native object / executable
    ├──────── LLVM ───────── LLVM IR / native object
    ├──────── WebAssembly ── .wasm
    └──────── RXE ────────── .rxe bytecode
```

**Forge** is the default native backend. It provides SSA-based optimization, ABI lowering, machine lowering, register allocation, instruction encoding, and deterministic ELF/COFF object emission.

**LLVM** emits LLVM IR from the same verified MIR and can use an external LLVM/Clang toolchain for native code generation.

**WebAssembly** and **RXE** provide portable execution targets while preserving the same language semantics established before backend emission.

Detailed architecture documentation is available in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/BACKENDS.md](docs/BACKENDS.md).

## Installation

### Windows

Raz releases provide an MSI installer and a portable archive. The MSI can add Raz to `PATH` during installation.

After installation:

```text
raz --version
raz doctor
```

Toolchains can also be managed with `razup`:

```text
razup install stable
razup update
razup default stable
razup toolchain list
```

See [Installation](docs/INSTALLATION.md) for installer, portable, silent-install, and toolchain-management details.

## Building from source

Raz supports native source builds on Windows and Linux.

Windows:

```powershell
./bootstrap.bat
```

Linux:

```sh
./bootstrap.sh
```

The bootstrap command configures the native host components, constructs the Raz compiler, and verifies compiler reproducibility before producing the toolchain.

For native components only:

```text
cmake --preset release
cmake --build --preset release
```

See [Compiler bootstrap](docs/COMPILER-BOOTSTRAP.md) and [Windows build](docs/WINDOWS-BUILD.md).

## Repository layout

```text
compiler/    Raz compiler implementation
library/     core, allocation, and standard libraries
src/         native runtime boundaries, host compiler, and Forge
benchmarks/  maintained performance reference workloads
tests/       language, compiler, runtime, and integration coverage
docs/        language and toolchain reference
tools/       formatting, verification, and repository utilities
```

## Documentation

Start with [docs/README.md](docs/README.md).

Key references:

- [Getting Started](docs/GETTING-STARTED.md)
- [Language specification](docs/LANGUAGE-SPECIFICATION.md)
- [Language stability](docs/LANGUAGE-STABILITY.md)
- [CLI reference](docs/CLI.md)
- [Package management](docs/PACKAGE-MANAGEMENT.md)
- [Standard-library performance](docs/STANDARD-LIBRARY-PERFORMANCE.md)
- [Compiler architecture](docs/ARCHITECTURE.md)
- [Backends](docs/BACKENDS.md)
- [Language server](docs/LANGUAGE-SERVER.md)
- [Formatting](docs/FORMATTING.md)

## License

Raz is licensed under the [Apache License 2.0](LICENSE). Forge retains its nested Apache-2.0 license for independent redistribution. See [NOTICE](NOTICE) and [Licensing](docs/LICENSING.md) for attribution and redistribution details.
