# Changelog

### CLI/package diagnostics and build progress

- Fixed production LSP completion JSON after the compiler text-literal migration: built-in completion items once again emit `{"label":...,"kind":14}` objects instead of malformed `{"data":[...,"kind":14}` payloads. Added a source qualification contract for the completion object shape; the end-to-end production LSP/bootstrap qualification now passes.
- `raz update` now reports dependency-resolution progress and each resolved package/version instead of succeeding silently.
- Project builds now print individual assembled dependency packages with their resolved versions before the root package compile status, preserving manifest package spelling (for example `Compiling http-router v0.2.0`).
- Dynamic-trait calls distinguish an actually unknown member from a declared trait method that has no concrete dispatch implementation in the assembled package graph, with package/update guidance. Project diagnostics now carry source provenance through assembled package sources so errors render against the originating `.rz` path and local line instead of a giant `<input>` line number.
- The official serde package is republished as immutable `serde 0.2.1`; the demo uses `^0.2.0` so `raz update` can select the fixed compatible release.

All notable user-visible changes to Raz are documented here.

## Unreleased

- Fixed the C bindgen carrier type for inline anonymous struct/union fields.
  Alignment 4 and 2 emitted `u64` while still dividing the aggregate size by 4
  and 2, so `struct { int x; int y; }` became `u64[2]` instead of `u32[2]`.
- Anchored assembled-source marker scanning to line starts. `__raz_package ` and
  `// __raz_package_version ` are written as whole lines, but the scan matched
  them anywhere, so once the compiler's own source contained those markers as
  string literals it announced a package named `"` while building itself.
- Moved the bootstrap self-qualification sequence out of `main` into
  `run_bootstrap_qualifications`, keeping the entrypoint a dispatcher.
- Added a per-command `raz help` qualification. Command help was reachable only
  by typing it, so a command whose help text faulted went unnoticed.

- Added match guards (`pattern if condition`) with binding-aware guard expressions, correct guarded-arm fallthrough, and exhaustiveness/subsumption rules that never treat runtime predicates as structural coverage. Added explicit `move name` payload bindings as ownership-intent syntax while retaining established by-value semantics.
- Added named struct-field destructuring inside enum payload patterns, including recursive aggregate-field paths, shorthand field bindings, terminal `..` rest fields, duplicate/missing-field validation, and exhaustiveness across enum-valued fields nested inside structs.
- Added recursive positional rest patterns (`..`) to enum payload matching. A terminal rest ignores every remaining payload in the current variant at any nesting depth, while `_` continues to ignore exactly one payload; non-terminal rest usage is rejected. Production qualification now exercises nested rest-pattern lowering through native Forge execution.
- Replaced compiler source decimal-ASCII arrays with real `string` literals wherever the data is textual, leaving only five explicitly annotated raw filename buffers required by pointer-oriented runtime ABIs. Added a qualification gate that rejects new unannotated printable numeric text arrays, and hardened bootstrap source scanning so braces/comment markers inside quoted literals cannot corrupt package-surface discovery.
- Generalized enum payload patterns from one nested level to arbitrary recursive payload paths, including deep binding/type recovery, recursive exhaustiveness, Cartesian product coverage for multiple enum-valued payloads, and dedicated non-exhaustive/unreachable diagnostics.
- Forge now preserves internal-data linkage through machine/codegen/object lowering. Internal globals emit as local `STB_LOCAL` symbols in x86-64/AArch64 ELF, `IMAGE_SYM_CLASS_STATIC` in COFF AMD64, and local non-`N_EXT` symbols in Mach-O arm64, preventing cross-module string-literal symbol collisions.
- Fixed production enum-match parsing so `Enum::Variant` is not consumed as a qualified type path, restoring ordinary payload/fieldless enum matches and enabling disjoint nested payload arms such as `Some(Ok(...))` and `Some(Err(...))`. Nested enum patterns now perform real exhaustiveness analysis across enum-valued payload combinations, accept complete coverage without a wildcard, and reject unreachable/subsumed arms with a dedicated `UnreachablePattern` diagnostic.
- Forge structured native lowering now infers types for MIR-only synthetic locals (including auto-borrowed literal receivers), keeping those paths on native stack storage instead of leaking compiler-private arena/reference helpers into user objects.
- Bundled Forge AArch64 now has call-safe 128-bit vector allocation/spilling, native integer add reductions, packed chain/postfix-DAG evaluation, and automatic contiguous reduction formation.
- Fixed AArch64 optimizer alias-resolution ordering before virtual-register compaction so vectorized/reduced consumers retain the correct rewritten value identity.
- Added a permanent Raz frontend -> Forge IR -> embedded Forge AArch64 cross-object qualification covering deterministic ELF64 `EM_AARCH64` and Mach-O `CPU_TYPE_ARM64` emission on non-ARM CI hosts.

### Platform support

- Added first-class LLVM AArch64 native targeting for Linux (`aarch64-unknown-linux-gnu`) and macOS arm64 (`arm64-apple-macos`). Forge remains the native default only on x86-64 Windows/Linux; AArch64 and macOS hosts default to LLVM until Forge gains the corresponding machine/object backends.
- Cross-target native objects no longer share incremental artifacts across LLVM triples or other codegen options. Cross-target executable links require an explicit target runtime so a host runtime archive cannot be linked into a foreign target by mistake.
- Source bootstrap now accepts a compatible prebuilt Raz stage-0 compiler for AArch64/macOS LLVM bootstrap, while x86-64 Windows/Linux retains the compatibility host + Forge construction path.
- Bundled Forge AArch64 now includes physical linear-scan allocation in AAPCS64 callee-saved integer/floating registers, deterministic spills and frame preservation, plus a target-safe canonical machine-combine pass. LLVM remains the default AArch64 backend until the remaining vector/Darwin/bootstrap parity gates close.

### Language server

- Fixed Windows LSP framing by switching the protocol stdin/stdout streams to CRT binary mode before reading or writing frames; explicit CRLF headers are now emitted byte-for-byte instead of being translated to invalid CRCRLF sequences.
- Project indexing now includes resolved registry dependencies already present in the local content-addressed package store, using portable `raz.lock` checksums without editor-triggered network access.
- Indexed documents keep compact HIR-derived declaration summaries that are invalidated per document on disk/editor changes and reused by global completion and workspace-symbol queries.
- Bootstrap qualification now includes a resolved-registry LSP protocol gate in addition to baseline, semantic, and project-index coverage.
- The production LSP now builds a disk-backed project index from `rootUri`, including unopened `.rz` modules and direct path dependencies. Editor buffers overlay that index and closing a buffer restores the saved module instead of dropping it from workspace navigation.
- Workspace symbols now emit structurally valid LSP `SymbolInformation` locations, and project qualification covers unopened definitions/references/rename, generated-tree exclusion, dependency navigation, and overlay invalidation.

### Native linking

- Fixed Windows bootstrap OpenSSL detection so a cached header-only
  `OPENSSL_INCLUDE_DIR` can no longer masquerade as an enabled runtime TLS
  dependency. CMake now exports the runtime feature state explicitly and
  forwards OpenSSL linker/import-library files for recursive compiler links.
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

- Fixed deferred early-exit cleanup for owned locals: `return` and `?` now drop only locals declared before the exit point, preventing generated MIR from loading/dropping later uninitialized `String` values while retaining normal end-of-scope RAII cleanup.
- Legacy textual Forge fallback now targets the ordinary `raz_rt_stage1_*` runtime ABI instead of compiler-private arena/reference symbols. Stage-1 references are direct arena-cell addresses with allocation-free integer/f64 load-store helpers, and f64 dereferences preserve their scalar type through Forge emission.
- Fixed the self-host Forge ABI for built-in `string` values: by-value strings now carry immutable C-string address bits in the ordinary `i64` scalar ABI while `string&`/`string&mut` remain native pointers. String-literal `global.address` operations now request an `i64` result in both textual and structured Forge lowering, preventing ptr/i64 verifier failures when literals flow through locals and runtime calls.
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

### Pattern borrow bindings
- Added sound `ref` and `ref mut` bindings for enum/struct match patterns.
- Reused MIR indexed-place references so nested pattern borrows alias original aggregate storage across Forge, LLVM, WASM, RXE, and the MIR interpreter.
- Added path-sensitive pattern-loan tracking, allowing disjoint mutable payload/field borrows while rejecting overlapping parent mutation.
- Added shorthand struct forms such as `Header { ref kind, ref mut length, .. }`.
