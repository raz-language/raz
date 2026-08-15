# Self-hosting

The production Raz compiler is written in Raz. A small native bootstrap compiler builds the first Raz-written compiler executable; that compiler then rebuilds itself recursively.

## Generations

- **Stage 0** — frozen native bootstrap seed. It only needs to build Stage 1; it is not feature-parity production Raz.
- **Stage 1** — Raz-written compiler produced by Stage 0.
- **Stage 2** — compiler produced by Stage 1.
- **Stage 3** — compiler produced by Stage 2.
- **Stage 4** — compiler produced by Stage 3.

Release qualification compares the native compiler objects from the recursive generations. Stage 2, Stage 3, and Stage 4 must be byte-identical for the fixed-point check.

## Source ordering

The compiler source is split into semantic modules under `compiler/src/`. Production Stage 1+ development uses explicit module namespaces/imports and the normal package graph. `compiler/bootstrap-source-order.txt` defines only the deterministic seed order used to create the disposable Stage 0 → Stage 1 legacy view; it does not govern normal compiler builds. `src/main.rz` remains the final entrypoint.

## Native boundary

The self-hosted compiler owns parsing, semantic analysis, HIR/MIR, backend policy, project loading, package resolution, formatting, tests, docs, and CLI behavior. Native runtime functions are limited to permanent host/ABI operations such as memory, filesystem/process access, environment queries, networking, cryptographic engines, and backend bridges.

## Frozen Stage 0 boundary

Language evolution happens in `compiler/src/*.rz`. Stage 0 is not updated merely because the production compiler gains a trait rule, borrow rule, diagnostic, optimizer, or language feature. `scripts/check-stage0-semantic-freeze.py` validates the frozen lexer/parser/semantic/HIR/MIR/lowering seed against `scripts/stage0-semantic-freeze.sha256`. The manifest may change only for an intentional bootstrap-compatibility repair required to keep Stage 0 capable of building Stage 1.

The repository audits this boundary so language/toolchain policy does not migrate into native shims merely for convenience.

## Windows

Windows compiler executables reserve an explicit stack suitable for compiler-sized recursive workloads. The bootstrap propagates the selected MSVC/Clang toolchain to native linking and quotes linker paths safely when they contain spaces.

See [Windows all-stage bootstrap](WINDOWS-ALL-STAGES-BUILD.md).
