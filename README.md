<div align="center">

# Raz

### A systems programming language for native software that has to be fast, safe, and predictable.

[![Version](https://img.shields.io/badge/language-1.0-111827?style=flat-square)](docs/LANGUAGE-STABILITY.md)
[![License](https://img.shields.io/badge/license-Apache--2.0-111827?style=flat-square)](LICENSE)
[![Packages](https://img.shields.io/badge/packages-registry-111827?style=flat-square)](https://github.com/raz-language/packages)

[Getting started](docs/GETTING-STARTED.md) ·
[Language specification](docs/LANGUAGE-SPECIFICATION.md) ·
[CLI reference](docs/CLI.md) ·
[Documentation](docs/README.md)

</div>

---

Raz is a statically typed systems language for software where runtime cost and memory behavior matter: command-line tools, network services, runtimes, databases, compilers, storage engines, and infrastructure. It combines ownership and borrowing with low-level memory access, generics, traits, pattern matching, deterministic destruction, and structured concurrency.

**Raz 1.0 is stable.** Programs that stay within the documented 1.x language contract should not need source changes for later 1.x releases.

## Why Raz

**A cost model you can see.** No tracing garbage collector, no hidden runtime ownership model. Ownership, moves, borrows, and deterministic drop decide lifetime at compile time; generics monomorphize, dynamic dispatch happens where it is asked for, and `unsafe` marks the boundary where low-level work becomes deliberate.

**Safety without surrendering control.** Non-lexical loan analysis, bounds- and lifetime-aware slices, and explicit `extern` and raw-pointer boundaries let systems code stay systems code while the compiler proves the parts that should be provable.

**One frontend, four targets.** Every backend consumes the same verified MIR, so semantics are settled before code generation begins. Native output through Forge or LLVM, plus WebAssembly and RXE bytecode.

**A toolchain designed with the language.** Builds, tests, formatting, documentation, linting, package management, workspaces, lockfiles, incremental compilation, and a language server — all from one command, all sharing the compiler's semantic engine rather than reimplementing it.

**Engineering you can verify.** The production compiler is written in Raz and reproduces itself deterministically from its own source tree. The language specification is the normative reference; the conformance suite is the executable one. Package archives are deterministic and content-addressed.

## The language

| Area | Raz 1.0 |
|---|---|
| Execution | Native machine code through Forge or LLVM/Clang |
| Memory model | Ownership, moves, borrows, non-lexical loan analysis, deterministic drop |
| Managed runtime | No tracing garbage collector, no hidden runtime ownership model |
| Type system | Structs, payload enums, tuples, arrays, slices, references, raw pointers, function pointers |
| Abstraction | Generics, traits, associated items, const generics, closures, iterators |
| Control flow | Pattern matching, structured error handling, `defer`, deterministic destruction |
| Compile time | Constants, const functions and generics, `comptime`, reflection facilities |
| Concurrency | Threads, atomics, synchronization primitives, channels, tasks, futures |
| Async | `async fn`, `spawn`, `await`, readiness reactor, async I/O foundations |
| Native boundary | Explicit `extern`, raw pointers, and `unsafe` for deliberate low-level work |
| Project model | Manifests, path and registry dependencies, lockfiles, workspaces, tests, docs |

Raz uses type-first declarations and semicolon-terminated statements. The [language specification](docs/LANGUAGE-SPECIFICATION.md) is the normative reference; [Getting Started](docs/GETTING-STARTED.md) is the practical introduction.

## Install and start

Raz 1.0.0 ships as an MSI and portable ZIP for Windows x86-64, and as a portable `tar.gz` for Linux x86-64. `razup` can install and switch between published toolchains. See [Installation](docs/INSTALLATION.md) for platform-specific instructions.

Once `raz` is on `PATH`:

```text
raz --version
raz doctor
raz new hello
cd hello
raz run
```

[Getting Started](docs/GETTING-STARTED.md) continues from there with a guided tour of the language and toolchain.

## Toolchain

`raz` is the project driver and primary command-line interface. `razc` is the direct compiler interface.

| Command | Purpose |
|---|---|
| `raz new` · `raz init` | Create or initialize a package |
| `raz check` | Parse, resolve, type-check, and validate |
| `raz build` · `raz run` | Build a native artifact, or build and execute |
| `raz test` · `raz lint` | Run/filter `test_` functions with per-test results; run semantic validation |
| `raz fmt` · `raz doc` | Canonical formatting; API documentation |
| `raz add` · `raz remove` · `raz update` | Manage dependencies and re-resolve constraints |
| `raz search` · `raz info` · `raz outdated` | Inspect the official registry and tracked versions |
| `raz pack` · `raz publish` | Produce and submit deterministic package archives |
| `raz doctor` · `raz backends` · `raz targets` | Inspect the toolchain, backends, and targets |
| `raz bindgen` | Generate Raz C-ABI declarations from supported C headers |
| `raz c-header` | Generate C declarations for explicit Raz C-ABI exports |

Normal executable package builds are native by default: `raz build` writes `target/debug/bin/<package>` (`.exe` on Windows), and `raz build --release` writes `target/release/bin/<package>`. Forge `.fir` output is reserved for explicit backend/emission workflows.

Each profile has the same predictable artifact layout; normal profile builds create the category directories together:

```text
target/
├── debug/
│   ├── bin/       executables
│   ├── lib/       static/shared libraries
│   ├── obj/       native object files
│   ├── ir/        backend/intermediate IR
│   ├── modules/   semantic module metadata
│   └── packages/  package/distribution artifacts
├── release/
│   └── ... same profile layout ...
├── cache/          incremental and link state
├── git/            materialized git dependencies
├── store/          package store
└── profile/        explicit compiler profiling output
```

Build output is concise and package-oriented:

```text
   Compiling core v0.4.2
   Compiling app v1.0.0
    Finished app [release, host] (3 compiled, 8 fresh) in 412 ms
```

Command parsing fails before any build work begins, and common mistakes get actionable diagnostics:

```text
error: no such command: 'biuld'
  help: a similar command exists: 'build'
  help: view all commands with 'raz --help'
```

Diagnostics are stable and machine-readable, and the production compiler also owns `raz lsp`. The server provides compiler-backed diagnostics, project/dependency indexing, semantic navigation and rename, hover/signatures, semantic tokens, inlay hints, completion, formatting, code actions, folding, and selection ranges. `raz bindgen` generates supported C ABI declarations without a libclang runtime dependency, while `raz c-header` exports explicit `@abi(C)`/`@repr(C)` Raz APIs as C headers. See the [CLI reference](docs/CLI.md), [Language server](docs/LANGUAGE-SERVER.md), and [C interoperability](docs/C-INTEROPERABILITY.md).

## Standard library

Implemented primarily in Raz, in three layers:

| Layer | Scope |
|---|---|
| `library/core/` | Language and runtime-independent foundations |
| `library/alloc/` | Allocation-backed collections and memory utilities |
| `library/std/` | Operating-system, networking, concurrency, and application APIs |

| Domain | Coverage |
|---|---|
| Collections | Vectors, deques, hash maps and sets, strings, slices, arenas, fixed object pools |
| System | Files, directories, paths, environment, processes, clocks, timers, random generation |
| I/O | Buffered, vectored, and byte-oriented I/O |
| Networking | TCP, UDP, DNS, TLS, HTTP, URL handling, framed transports, readiness polling |
| Encoding | JSON, Base64, hexadecimal, binary encoding, CRC-32, LZ4 block compression |
| Concurrency | Threads, atomics, channels, worker executors, lock-free SPSC/MPMC queues |
| Application | Structured logging, allocation-conscious CLI parsing |

Performance-sensitive APIs are designed around retained buffers, caller-owned storage, batching, bounded caches, and reuse. Higher-level policy stays in Raz; native code is restricted to permanent operating-system, ABI, cryptographic-engine, and backend boundaries. See [Standard-library performance](docs/STANDARD-LIBRARY-PERFORMANCE.md) and the [performance model](docs/PERFORMANCE.md).

## Compiler and backends

Source is analyzed once and lowered through a backend-neutral pipeline. Every target consumes the same verified MIR, so language semantics are established before any backend runs.

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
Verified MIR ──────────► diagnostics · language server · tooling
    │
    ├──────── Forge ──────── native object / executable
    ├──────── LLVM ───────── LLVM IR / native object
    ├──────── WebAssembly ── .wasm
    └──────── RXE ────────── .rxe bytecode
```

| Backend | Role |
|---|---|
| **Forge** | Default native backend on x86-64 Windows/Linux, linked in-process. Production x86-64 allocation/encoding plus experimental AArch64 ELF64 and Darwin Mach-O arm64 object paths. |
| **LLVM** | Production option. Emits LLVM IR from the same verified MIR and uses an external LLVM/Clang toolchain for native code generation. |
| **WebAssembly** | Portable `.wasm` output under a documented ABI. |
| **RXE** | Portable bytecode target with a specified format and instruction set. |

Backend selection never changes language semantics. Compiler construction is reproducible: a compatibility-pinned host compiler builds the production compiler, and the result is verified before the toolchain is produced.

See [Architecture](docs/ARCHITECTURE.md), [Backends](docs/BACKENDS.md), [Platform support](docs/PLATFORM-SUPPORT.md), [MIR](docs/MIR.md), and [Compiler reproducibility](docs/COMPILER-REPRODUCIBILITY.md). Release qualification and backend differential requirements are defined in [Release qualification](docs/RELEASE-QUALIFICATION.md).

## Platform support

| | Native | ABI · object format |
|---|---|---|
| **Windows x64** | Forge, LLVM/Clang | Windows x64 · COFF |
| **Linux x86-64** | Forge, LLVM/Clang | System V AMD64 · ELF |
| **Linux AArch64** (`aarch64-unknown-linux-gnu`) | LLVM/Clang (default), Forge experimental | AAPCS64 · ELF |
| **macOS arm64** (`arm64-apple-macos`) | LLVM/Clang (default), Forge experimental | Darwin AArch64 · Mach-O |
| **WebAssembly** | — | Documented [WASM ABI v1](docs/WASM-ABI-v1.md) |
| **RXE** | — | Portable [RXE v1](docs/RXE-v1-FORMAT.md) bytecode |

Forge now includes experimental AArch64 ELF64 and Darwin Mach-O arm64 machine/object backends. The AArch64 path has native physical register allocation, copy coalescing/CFG-hole reuse/spill-slot coloring, target-safe immediate selection, register-native scalar encoding, 128-bit NEON integer maps and add reductions, packed chain/postfix-DAG evaluation, call-safe full-width Q spills, Linux TLS IE, and Darwin TLV TLS. Raz still automatically selects LLVM for ordinary AArch64 builds until the remaining reusable-DAG/masked/wider-vector forms, uncommon ABI, JIT, and recursive-bootstrap qualification reach parity. Explicit Forge native emission can exercise Linux AArch64 and macOS arm64 objects today.

Platform-specific standard-library facilities are documented per module and do not imply portability to unsupported targets.

## Packages and workspaces

The official registry lives at [`raz-language/packages`](https://github.com/raz-language/packages). It is a GitHub-backed static registry of immutable, deterministic archives covering data formats, security, networking, databases, systems utilities, observability, testing, and exact numeric types. See the [official package catalog](docs/OFFICIAL-PACKAGES.md) for the maintained list instead of duplicating it here.

Dependencies are declared as semantic-version constraints and pinned exactly in `raz.lock`. Archives are integrity-checked and stored in a shared content-addressed cache, so `build`, `check`, `run`, and `test` hydrate missing locked packages automatically — a clean checkout needs no separate install step. Offline builds, vendoring, Git dependencies, private registries, and mirrors are supported, and a root manifest can coordinate many member packages against one lockfile.

See [Package management](docs/PACKAGE-MANAGEMENT.md).

## Building from source

Raz builds from source on Windows, Linux, and macOS. On x86-64 Windows/Linux, the compatibility host compiler constructs the first production compiler directly. AArch64 hosts and macOS hosts use a compatible prebuilt Raz stage-0 compiler (`--stage0` or `RAZ_STAGE0_COMPILER`) to produce the first LLVM-native object, then continue with normal self-hosted reproducibility builds. This stage-0 requirement can disappear once Forge AArch64 reaches recursive-bootstrap parity, not merely when object encoding exists. `bootstrap.bat` and `bootstrap.sh` configure native host components, construct the production compiler, and verify compiler reproducibility before producing the toolchain. Repository-native CMake artifacts live under `build/<profile>/`, while compiler-produced qualification and project artifacts live under `target/`. Native components alone can be built with the CMake `release` preset.

See [Compiler bootstrap](docs/COMPILER-BOOTSTRAP.md) and [Windows build](docs/WINDOWS-BUILD.md).

## Repository layout

```text
compiler/    Raz compiler implementation
library/     core, allocation, and standard libraries
src/         native runtime boundaries, host compiler, and Forge
tests/       language, compiler, runtime, and integration coverage
docs/        language and toolchain reference
tools/       formatting, verification, and repository utilities
```

## Documentation

| Reference | Description |
|---|---|
| [Getting Started](docs/GETTING-STARTED.md) | Practical introduction to the language |
| [Language specification](docs/LANGUAGE-SPECIFICATION.md) | Normative syntax and semantic rules |
| [Language stability](docs/LANGUAGE-STABILITY.md) | Raz 1.x compatibility guarantees |
| [CLI reference](docs/CLI.md) | Complete command and diagnostic interface |
| [Package management](docs/PACKAGE-MANAGEMENT.md) | Dependencies, lockfiles, registries, publishing |
| [Standard library](docs/STANDARD-LIBRARY.md) | Module map of every layer, module, and public item |
| [Diagnostic index](docs/DIAGNOSTIC-INDEX.md) · [Common diagnostics](docs/DIAGNOSTICS-EXPLAINED.md) | Every error code, and extended explanations |
| [Architecture](docs/ARCHITECTURE.md) · [Backends](docs/BACKENDS.md) · [MIR](docs/MIR.md) | Compiler internals |
| [Performance](docs/PERFORMANCE.md) · [Standard library](docs/STANDARD-LIBRARY-PERFORMANCE.md) | Performance model and library design |
| [Language server](docs/LANGUAGE-SERVER.md) · [Formatting](docs/FORMATTING.md) | Editor integration and source conventions |
| [Documentation index](docs/README.md) | Everything else |

## Contributing

Contributions are welcome. [CONTRIBUTING.md](CONTRIBUTING.md) covers the design rules changes must preserve, required checks, formatting, test expectations, and contribution licensing.

For normal compiler and native work, build and test before opening a change:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Format the Raz source you touched, and confirm license headers:

```bash
python tools/format-raz.py <changed-file>.rz --check
python tests/python/check-license-headers.py
```

User-visible changes are recorded in [CHANGELOG.md](CHANGELOG.md). Contributors are credited in [AUTHORS.md](AUTHORS.md); to cite Raz in academic work, see [CITATION.cff](CITATION.cff).

## Security

Please do not report security vulnerabilities through public issues. See [SECURITY.md](SECURITY.md) for the reporting process and the areas considered security-relevant.

## License

Raz is licensed under the [Apache License 2.0](LICENSE). Forge retains its nested Apache-2.0 license for independent redistribution. See [NOTICE](NOTICE) and [Licensing](docs/LICENSING.md) for attribution and redistribution details.
