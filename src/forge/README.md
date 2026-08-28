<div align="center">

# Forge

**A verified compiler infrastructure toolkit for building native language frontends.**

[![Release](https://img.shields.io/badge/release-2.0.0-2563eb)](CHANGELOG.md)
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599c)](CMakeLists.txt)
[![License](https://img.shields.io/badge/license-Apache--2.0-16a34a)](LICENSE)
[![C API](https://img.shields.io/badge/C%20API-v16-7c3aed)](include/forge-c/forge.h)
[![Target](https://img.shields.io/badge/target-x86--64%20%7C%20AArch64-334155)](#platform-support)

Forge provides typed SSA IR, verification, optimization pipelines, an interpreter, an x86-64 JIT/native backend, an experimental AArch64 native object backend, deterministic ELF/COFF output, and frontend SDKs for C++ and C.

[Quick start](#quick-start) · [Frontend guide](docs/building-a-language.md) · [Architecture](docs/architecture.md) · [Contributing](CONTRIBUTING.md)

</div>

---

## Overview

Forge is a compact compiler core intended for language implementers who want a native backend without adopting a very large compiler framework. A frontend performs parsing, semantic analysis, and type checking, then lowers into verified Forge IR.

```text
Source language
      │
      ▼
Parser · resolver · type checker
      │
      ▼
Forge C++ SDK or C API
      │
      ▼
Verified SSA IR
      ├──────────────► Interpreter
      ▼
Optimization pipeline
      ▼
Machine IR · target lowering · native encoding
      ├──────────────► JIT
      └──────────────► ELF64 / COFF object files
```

### Highlights

| Area | Capabilities |
|---|---|
| **Frontend SDK** | Typed builder, stable handles, source ranges, diagnostics, metadata, source maps, opaque C API |
| **IR** | Typed SSA, block arguments, globals, calls, aggregates, canonical text, binary serialization |
| **Optimization** | `-O0` through `-O3`, `-Os`, `-Oz`, liveness, DCE, CFG cleanup, copy propagation, pass reports |
| **Execution** | Reference interpreter, x86-64 JIT, runtime bindings, interpreter/JIT differential tests |
| **Native output** | System V/Windows x64 plus experimental AAPCS64 lowering, deterministic ELF64/COFF/Mach-O arm64 objects, static archives, shared-library linking |
| **ABI tooling** | Aggregate classification, variadic/calling-convention metadata, register and stack usage summaries |
| **Incremental builds** | Fingerprints, dependency invalidation, parallel scheduling, cached functions, object and executable caching |

## Quick start

### Requirements

- CMake 3.21 or newer
- A C++20 compiler
- Ninja when using the checked-in presets
- An x86-64 host for JIT tests

### Build

```sh
cmake --preset release-strict
cmake --build --preset release-strict
ctest --preset release-strict
```

Windows PowerShell:

```powershell
.\scripts\release-gate.ps1
```

Unix release gate:

```sh
./scripts/release-gate.sh
```

Sanitizer gate on supported Unix toolchains:

```sh
./scripts/sanitizer-gate.sh
```

### Command-line examples

```sh
# Verify textual IR
forge verify examples/native-i64.fir

# Optimize and inspect pass statistics
forge-opt examples/optimization.fir -O3 --stats --pass-timing

# Emit an ELF object
forge compile examples/native-i64.fir -O2 --format=elf -o native.o

# Compare interpreter and JIT results
forge-run --engine=compare examples/interpreter.fir factorial 10

# Create a native static library
forge compile examples/library-answer.fir --format=elf -o answer.o
forge archive create -o libanswer.a answer.o

# Link a shared library with the host compiler driver
forge link-shared --linker=c++ -o libanswer.so answer.o
```

## Tools

| Tool | Purpose |
|---|---|
| `forge` | Verify, optimize, compile, archive, and link Forge IR artifacts |
| `forge-as` | Assemble textual IR into binary IR |
| `forge-dis` | Disassemble binary IR into canonical text |
| `forge-opt` | Run optimization pipelines and report pass statistics |
| `forge-codegen` | Inspect lowering, allocation, encoding, and backend metrics |
| `forge-run` | Execute through the interpreter, JIT, or differential engine |

Optimization levels:

| Level | Intent |
|---|---|
| `-O0` | Minimal transformation and maximum IR fidelity |
| `-O1` | Low-cost cleanup |
| `-O2` | Standard production optimization |
| `-O3` | Aggressive optimization |
| `-Os` | Optimize for size |
| `-Oz` | Minimize code size |

See [docs/cli.md](docs/cli.md) for command details.

## Build a frontend

### C++

```cpp
#include <forge/ir/builder.hpp>

forge::ir::Context context;
auto& module = context.create_module("example");
forge::ir::IRBuilder builder(context, module);

std::vector<forge::ir::ValueDecl> parameters{
    {"%left", forge::ir::i64_type()},
    {"%right", forge::ir::i64_type()},
};

auto function = builder.create_function_handle(
    "add", forge::ir::i64_type(), parameters);
auto entry = builder.create_block_handle(function, "entry");

builder.position_at_end(entry);
builder.set_source_range("main.lang", 1, 1, 1, 15);
auto result = builder.create_add(
    forge::ir::i64_type(), "%left", "%right");
builder.create_return(result);

const auto diagnostics = builder.verify();
```

### C and FFI

```c
#include <forge-c/forge.h>

#if FORGE_C_API_VERSION != 16
#error "Unsupported Forge C API version"
#endif
```

The opaque C API is suitable for Rust, Zig, Go, C#, Python extensions, Raz, and other languages with C FFI support.
C API v16 retains in-memory parsing, optimizer-pipeline selection, structured construction, and two-call COFF/ELF/Mach-O object emission, and adds native thread-local globals plus `tls.address` lowering and explicit Darwin arm64 object selection. Frontends can therefore migrate from textual IR transport to zero-serialization module construction without launching a Forge tool executable.

Start with:

- [Building a language with Forge](docs/building-a-language.md)
- [MiniLang complete frontend](examples/frontend/minilang/) — lexer, parser, AST, semantic lowering, interpreter, and JIT
- [Tiny IRBuilder example](examples/frontend/tiny_frontend.cpp)
- [Standalone frontend template](examples/frontend/template/)

## Native ABI and libraries

Forge exposes System V AMD64, Windows x64, and AAPCS64 aggregate ABI classification through `<forge/platform/abi.hpp>`. Frontends can inspect integer/SSE classes, indirect passing, register consumption, stack bytes, and variadic state before lowering language-level signatures.

Function declarations also carry calling-convention, linkage, visibility, and variadic metadata through textual and binary IR. See [Native ABI classification and libraries](docs/abi-and-libraries.md).

Native library workflows:

```sh
forge archive create -o libmath.a math.o helpers.o
forge link-shared --linker=c++ -o libmath.so math.o helpers.o
```

## Native backends

The production x86-64 backend includes:

- System V AMD64 and Windows x64 scalar/pointer calling conventions
- Integer and scalar floating-point lowering
- Call-aware register allocation and reusable spill slots
- Spill caching, dead-store elimination, and rematerialization
- Immediate, memory-source, and address-mode instruction selection
- Compare/branch fusion and fallthrough-aware block layout
- Short branch relaxation and frameless leaf functions
- Deterministic ELF64 and COFF AMD64 object emission

Forge also contains an experimental AArch64 backend with AAPCS64 scalar/aggregate classification, HFA support, an AArch64 linear-scan allocator using preserved `x19`-`x28`/`v8`-`v15`, copy coalescing/hole-aware reuse/spill-slot coloring, register-native scalar A64 instruction encoding, Linux initial-exec TLS, Darwin TLV TLS, target-safe immediate selection, 128-bit NEON integer maps/reductions, packed chain/DAG lowering, call-safe full-width vector spilling, and alias-safe automatic SLP/reduction formation, plus deterministic ELF64 AArch64 plus Mach-O arm64 objects. x86-only machine pseudos remain isolated from the AArch64 path. See [AArch64 backend](docs/aarch64-backend.md).

The interpreter is the semantic reference implementation. Native behavior is checked through differential tests.

## Incremental native builds

Forge can rebuild dependency-affected functions only, reuse encoded native artifacts, assemble deterministic objects, and restore final executables from cache.

```text
Previous snapshot ─┐
                   ├─► dependency-aware build plan
Current module ────┘              │
                                  ▼
                        parallel function builds
                                  │
                     ┌────────────┴────────────┐
                     ▼                         ▼
               artifact cache            cache hits
                     └────────────┬────────────┘
                                  ▼
                     deterministic ELF / COFF
                                  ▼
                       cache-aware native link
```

Public headers:

```cpp
#include <forge/ir/artifact_cache.hpp>
#include <forge/ir/dependency_build.hpp>
#include <forge/object/incremental.hpp>
#include <forge/object/native_link.hpp>
```

## Install and consume

```sh
cmake --preset release-strict
cmake --build --preset release-strict
cmake --install build/release-strict --prefix "$PWD/_install"
```

Consumer project:

```cmake
find_package(Forge 2.0 CONFIG REQUIRED)
target_link_libraries(my_compiler PRIVATE Forge::forge)
```

The release matrix installs Forge into an isolated prefix and builds independent C and C++ consumers against the installed package.

## Optimizer and analysis

Forge ships reusable function analyses and a deterministic scalar optimization pipeline. At `-O2` and `-O3`, late scalar cleanup now runs to a bounded fixpoint so opportunities exposed by memory, loop, and CFG transforms are consumed without unbounded compile-time growth. Forge includes conservative alias analysis, global memory dataflow, scalar stack promotion, and natural-loop discovery. Stack promotion handles same-block locals, acyclic joins, and loop-carried scalar state through liveness-pruned dominance-frontier block-parameter placement and dominator-tree SSA renaming. The optimizer still forwards values across CFG edges, eliminates overwritten stores, hoists safe loop-invariant expressions, and can hoist a canonical invariant loop-header guard to the preheader without duplicating the loop body.

```text
CFG + dominators + use/def
          |
          +-- pointer-origin alias analysis
          +-- natural-loop discovery
          |
          v
SCCP -> algebraic simplification -> CSE -> memory forwarding -> LICM -> DCE -> jump threading -> CFG cleanup
```

The alias analysis intentionally returns `may_alias` when provenance or offsets are uncertain. This keeps the transformations correct for arbitrary frontend-generated pointer code while still recognizing stack allocations and distinct globals as non-aliasing. See [Optimizer and analysis](docs/optimizer.md).

## Platform support

| Capability | Linux x86-64 | Windows x86-64 | Linux AArch64 | macOS arm64 |
|---|:---:|:---:|:---:|:---:|
| Compiler and SDK | ✅ | ✅ | Cross-build capable | Cross-build capable |
| Interpreter | ✅ | ✅ | Portable | Portable |
| Native JIT | ✅ | ✅ | — | — |
| System V / Windows ABI | ✅ | ✅ | — | — |
| AAPCS64 / Darwin arm64 ABI classification | Generated/validated | Generated/validated | Experimental | Experimental |
| ELF64 objects | ✅ | Generated and validated | Experimental native encoder | Cross-generated |
| Mach-O arm64 objects | Cross-generated | Cross-generated | Cross-generated | Experimental native encoder |
| COFF AMD64 objects | Generated and validated | ✅ | — | — |
| ASan + UBSan release gate | ✅ | Toolchain-dependent | Not yet qualified | Not yet qualified |

## Project status and boundaries

Forge 2.0.0 is the stabilized production release of the compiler core, frontend SDK, native library workflows, optimizer, allocator, and x86-64 backend. The unified driver adds `forge inspect`, `forge explain`, and `forge doctor`; explicit native calling conventions support by-value aggregate parameters and register-classified aggregate returns, while ABI-indirect aggregates continue through hidden result buffers.

The following are not currently part of the supported surface:

- True variadic function definitions (external/signature metadata is available)
- Per-function mixed calling conventions in one emitted object
- Unwind and debug metadata
- Full arbitrary live-range splitting beyond the current call-boundary and CFG-aware splitting infrastructure
- Production-parity native architectures other than x86-64 (AArch64 ELF is experimental)

See [docs/release-readiness.md](docs/release-readiness.md) for the release contract and [docs/roadmap.md](docs/roadmap.md) for planned work.

## Repository layout

```text
include/forge/       Public C++ SDK
include/forge-c/     Stable opaque C API
src/                 Compiler implementation
tools/               Command-line tools
tests/               Unit, differential, ABI, object, fuzz, and quality tests
examples/            IR examples and frontend samples
docs/                Architecture, API, backend, and release documentation
cmake/               Installed CMake package support
scripts/             Reproducible release and sanitizer gates
.github/workflows/   CI and release packaging
```

## Documentation

| Document | Purpose |
|---|---|
| [Architecture](docs/architecture.md) | Pipeline, subsystem responsibilities, and invariants |
| [IR reference](docs/ir-reference.md) | Core IR concepts and verification rules |
| [Backend](docs/backend.md) | Machine IR, allocation, ABI, encoding, and objects |
| [ABI and libraries](docs/abi-and-libraries.md) | Aggregate classification, static archives, and shared libraries |
| [CLI reference](docs/cli.md) | Command-line tools and optimization levels |
| [Building a language](docs/building-a-language.md) | C++ SDK and C API integration |
| [Release readiness](docs/release-readiness.md) | Supported production contract and release gates |
| [Roadmap](docs/roadmap.md) | Planned compiler work |
| [Contributing](CONTRIBUTING.md) | Contribution workflow and contribution standards |
| [Security](SECURITY.md) | Vulnerability reporting policy |
| [Code of Conduct](CODE_OF_CONDUCT.md) | Community participation expectations |

## Contributing

Contributions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request. Changes must preserve deterministic output, verification boundaries, and the interpreter/JIT differential contract.

## License

Forge is licensed under the [Apache License 2.0](LICENSE). Maintained source, header, test, example, and build-script files carry SPDX headers identifying Copyright 2026 Mario Vinciguerra.

## Frontend Integration API

Forge includes reusable source management, structured diagnostics, nested symbol scopes, semantic declarations, and safe control-flow builders under `<forge/frontend/frontend.hpp>`.

Create a standalone frontend project with:

```bash
forge new-language Aurora aurora
```

See [Frontend Integration API](docs/frontend-integration.md) and the complete [MiniLang example](examples/frontend/minilang/README.md).


## Production inspection

```bash
forge inspect examples/optimization.fir --stage=all
forge explain examples/optimization.fir -O3
forge doctor
```

See [`docs/stabilization.md`](docs/stabilization.md) for the strict and sanitizer validation model.
