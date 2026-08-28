# Raz documentation

This directory is the reference set for the Raz 1.0 language, compiler, standard library, package system, and toolchain.

**New to Raz?** Start with [Getting Started](GETTING-STARTED.md). Use the [Language specification](LANGUAGE-SPECIFICATION.md) for exact semantics and the [CLI reference](CLI.md) when you need command details.

## Learn the language

| Document | What it covers |
|---|---|
| [Getting Started](GETTING-STARTED.md) | A guided tour from the first project through ownership, generics, async, packages, and native interop |
| [Language specification](LANGUAGE-SPECIFICATION.md) | Normative reference: lexical structure, types, ownership, traits, async, compilation semantics |
| [Language stability](LANGUAGE-STABILITY.md) | What the Raz 1.x compatibility promise covers and what may still change |
| [Formatting](FORMATTING.md) | Canonical source layout and the formatter's contract |

The specification is the normative prose reference; the repository conformance suite is the executable one.

## Build applications

| Document | What it covers |
|---|---|
| [CLI reference](CLI.md) | Every `raz` and `razc` command, plus diagnostics, formats, and warning policy |
| [Package management](PACKAGE-MANAGEMENT.md) | Manifests, dependencies, lockfiles, registries, the shared store, vendoring, publishing |
| [Standard library](STANDARD-LIBRARY.md) | Module map of every layer, module, and public item |
| [Diagnostic index](DIAGNOSTIC-INDEX.md) | Every diagnostic code the compiler can emit, by category |
| [Common diagnostics](DIAGNOSTICS-EXPLAINED.md) | Extended explanations for the errors people hit most |
| [Standard-library performance](STANDARD-LIBRARY-PERFORMANCE.md) | How the library is designed for allocation-conscious, reusable, batched work |
| [Performance](PERFORMANCE.md) | The language and backend performance model, and what it does and does not promise |
| [Language server](LANGUAGE-SERVER.md) | Editor integration surface |
| [Semantic queries](SEMANTIC-QUERIES.md) | Programmatic access to compiler semantic information |

## Compiler internals

| Document | What it covers |
|---|---|
| [Toolchain specification](TOOLCHAIN-SPECIFICATION.md) | Driver contract, supported hosts, and qualification scope |
| [Architecture](ARCHITECTURE.md) | Frontend, HIR, MIR, and pipeline structure |
| [MIR](MIR.md) | The backend-neutral verified intermediate representation |
| [Backends](BACKENDS.md) | Forge, LLVM, WebAssembly, and RXE code generation |
| [Compiler bootstrap](COMPILER-BOOTSTRAP.md) | How the production compiler is constructed from source |
| [Compiler reproducibility](COMPILER-REPRODUCIBILITY.md) | Deterministic self-reproduction and the inputs it depends on |
| [Windows build](WINDOWS-BUILD.md) | Building the toolchain on Windows |

## Target formats

| Document | What it covers |
|---|---|
| [WebAssembly ABI v1](WASM-ABI-v1.md) | Calling convention and memory contract for the `.wasm` target |
| [RXE](RXE.md) | The RXE bytecode execution target |
| [RXE v1 format](RXE-v1-FORMAT.md) | Container layout and encoding |
| [RXE v1 ISA](RXE-ISA-v1.md) | Instruction set reference |

## Project

| Document | What it covers |
|---|---|
| [Licensing](LICENSING.md) | Apache-2.0 terms, third-party attribution, and redistribution |
| [Contributing](../CONTRIBUTING.md) | Design rules, required checks, and contribution licensing |
| [Security policy](../SECURITY.md) | Vulnerability reporting and security-relevant areas |
| [Changelog](../CHANGELOG.md) | User-visible changes by release |
