# Changelog

All notable user-visible changes to Raz are documented here.

## 1.0.0

### Language

- Finalized the Raz 1.0 syntax and semantic model, including ownership, borrowing, traits, generics, pattern matching, structured error handling, modules, visibility, and deterministic package interfaces.
- Added concrete generic aggregate layouts and complete cross-package generic specialization metadata.
- Added deterministic formatting and stable diagnostics for compiler and language-server workflows.

### Compiler

- Raz's production compiler is implemented in Raz and lowers source through HIR and MIR to native backends.
- Forge is the default native backend, with LLVM available as an alternate production backend.
- Added dependency-safe lowering, scalar promotion, SSA-aware scheduling, aggregate ABI classification, deterministic object generation, incremental compilation, and optimized release profiles through O3.
- Added reproducible compiler qualification and a compatibility-pinned native host compiler used only to construct the production compiler.

### CLI and tooling

- Added clean command diagnostics, typo suggestions, command-specific help, stable exit codes, quiet/verbose modes, and aligned build status output.
- Added `raz new`, `raz init`, `raz check`, `raz build`, `raz run`, `raz test`, `raz clean`, `raz fmt`, `raz doctor`, package commands, and backend selection.
- Added language-server support for diagnostics, completion, navigation, references, rename, symbols, formatting, and code actions.

### Packages

- Added GitHub-hosted package discovery, portable manifests, lockfiles, content-addressed storage, offline fetches, version resolution, transitive dependency hydration, checksums, signatures, workspaces, and conflict detection.
- Added `raz add`, `remove`, `update`, `fetch`, `search`, `tree`, `pack`, and `publish` workflows.

### Standard library

- Added production filesystem, process, environment, networking, HTTP, TLS, DNS, time, random, collections, serialization, logging, concurrency, compression, and testing facilities.
- Added allocation-conscious containers, bump arenas, fixed pools, reusable buffered I/O, vectored sockets, scalable polling, reactor-integrated monotonic timers and backpressure controls, bounded DNS/HTTP reuse, TLS session reuse, lock-free SPSC/MPMC queues, and zero-copy HTTP request views.
- Added a pure-Raz LZ4 block codec and optimized CRC-32 processing.

### Platforms and distribution

- Supports Windows and Linux native toolchains, with platform ABI/object qualification for x86-64 Windows and System V targets.
- Added MSI/portable installation layout, PATH integration, redistributable manifests, checksums, and release verification tooling.
