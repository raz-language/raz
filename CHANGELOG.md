# Changelog

All notable user-visible changes to Raz are documented here.

## Unreleased

### Language server

- Fixed Windows LSP framing by switching the protocol stdin/stdout streams to CRT binary mode before reading or writing frames; explicit CRLF headers are now emitted byte-for-byte instead of being translated to invalid CRCRLF sequences.
- Project indexing now includes resolved registry dependencies already present in the local content-addressed package store, using portable `raz.lock` checksums without editor-triggered network access.
- Indexed documents keep compact HIR-derived declaration summaries that are invalidated per document on disk/editor changes and reused by global completion and workspace-symbol queries.
- Bootstrap qualification now includes a resolved-registry LSP protocol gate in addition to baseline, semantic, and project-index coverage.
- The production LSP now builds a disk-backed project index from `rootUri`, including unopened `.rz` modules and direct path dependencies. Editor buffers overlay that index and closing a buffer restores the saved module instead of dropping it from workspace navigation.
- Workspace symbols now emit structurally valid LSP `SymbolInformation` locations, and project qualification covers unopened definitions/references/rename, generated-tree exclusion, dependency navigation, and overlay invalidation.

### Native linking

- Standalone Forge and ObLink now have an explicit synchronization contract with the copies embedded in Raz. CI checks maintained component files byte-for-byte, and `tools/sync-embedded-components.py` replaces the embedded trees from sibling standalone checkouts.
- ObLink is now the linker for every Raz link, including the bootstrap's
  reproducibility generations. `tools/bootstrap.py` previously invoked the host
  C++ compiler directly for those, and the forge bridge's ObLink branch never
  forwarded the OpenSSL archives the runtime is built against, so `raz build`
  failed on `EVP_*` even when the bootstrap succeeded.
- Fixed the CMake guard that silently disabled OpenSSL forwarding in the bridge.
  `OpenSSL::SSL` is an imported target scoped to the directory that found it, so
  testing for it from another directory is always false and the definition was
  never set.

### C interoperability

- Expanded `raz c-header` with package/directory generation and native C callback declarators; expanded `raz bindgen` with inline function-pointer parameters, common integral bitfield storage carriers, and normalization of C integer macro suffixes.
- Extended `raz bindgen` with bounded header-local conditional preprocessing and ABI-preserving inline anonymous struct/union storage fields; unsupported conditional expressions continue to fail rather than guess a platform branch.
- Added `raz c-header`, the reverse C-interop generator for explicit `@repr(C) public` aggregates and `@abi(C) public fn` exports. C ABI definitions now use their source spelling in the platform symbol namespace while ordinary Raz functions retain internal native mangling. Generated headers are qualified with a C compiler when available and round-trip through `raz bindgen` + `raz check`.
- Added `raz bindgen`, a dependency-free C-header translator implemented in the production Raz compiler. It emits C ABI externs, C-layout structs, explicit enums, ABI-preserving union storage, integer constants, pointer/array types, typedef substitution, and callback types.
- Bindgen has explicit `windows` (LLP64) and `unix` (x86-64 LP64) ABI modes and qualifies generated output by feeding it back through `raz check`.

### CLI

- `raz run` builds and launches the native executable and exits with the
  program's status, instead of interpreting the program. A direct-source run
  with no project manifest still uses the MIR interpreter, since there is no
  artifact to launch.
- `raz run -- <args>` now forwards program arguments through the runtime's
  shell-free argv API. Raz stops parsing at the separator, preserves option-like
  program arguments verbatim, and no longer mistakes a program's `--help` for
  compiler help.
- Build status output follows the established convention: the completion line
  names the profile and elapsed time rather than repeating the package name,
  which already appears on the line above. The linker no longer prints a success
  banner to stdout.

### Correctness

- Restored Stage-0 reproducibility compatibility without expanding the frozen
  C++ compiler: production RXE internals no longer use the legacy reserved
  identifier `function`, mutable-reference forwarding no longer forms accidental
  `&mut&mut` values, and WebAssembly sign-bit constants are expressed from
  representable i64 literals. The pinned host can once again check and build all
  production compiler modules.
- Native process-argument qualification now tests the permanent `raz_rt_*`
  runtime ABI directly, including shell-free argv forwarding and malformed-blob
  rejection. Compiler-owned `raz_compiler_rt_*` adapters remain implemented in
  Raz instead of being duplicated in C++ solely for a unit test.
- Bootstrap parser/semantic unit fixtures now use the current parenthesized
  condition and named-field aggregate syntax. The native semantic suite is kept
  to the frozen construction subset; evolving ownership, trait, effect, async,
  and closure semantics remain qualified by the Raz-written compiler.
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
