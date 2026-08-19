# Changelog

All notable user-visible changes to Raz are documented here.

## Unreleased

### Native linking

- ObLink is now the linker for every Raz link, including the bootstrap's
  reproducibility generations. `tools/bootstrap.py` previously invoked the host
  C++ compiler directly for those, and the forge bridge's ObLink branch never
  forwarded the OpenSSL archives the runtime is built against, so `raz build`
  failed on `EVP_*` even when the bootstrap succeeded.
- Fixed the CMake guard that silently disabled OpenSSL forwarding in the bridge.
  `OpenSSL::SSL` is an imported target scoped to the directory that found it, so
  testing for it from another directory is always false and the definition was
  never set.

### CLI

- `raz run` builds and launches the native executable and exits with the
  program's status, instead of interpreting the program. A direct-source run
  with no project manifest still uses the MIR interpreter, since there is no
  artifact to launch.
- Build status output follows the established convention: the completion line
  names the profile and elapsed time rather than repeating the package name,
  which already appears on the line above. The linker no longer prints a success
  banner to stdout.

### Correctness

- Fixed the incremental artifact cache ignoring the profile. `raz build`
  followed by `raz build --release` restored the cached debug artifact and
  shipped a byte-identical unoptimized binary as the release build.

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
