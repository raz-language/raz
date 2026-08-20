# Raz Language Server

Raz ships its language server as a command of the production project driver:

```text
raz lsp
```

The server uses Language Server Protocol `Content-Length` framing over standard input and output. Editors should launch it as a child process and reserve stdout exclusively for protocol traffic.

## Compiler ownership

The language server is implemented in Raz and is part of the production compiler source graph. It does not require the native bootstrap frontend. Unsaved editor buffers stay in memory and are analyzed with the same Raz frontend used by command-line compilation.

Diagnostics are produced by the compiler's HIR construction path. Formatting delegates to the canonical Raz formatter, so editor formatting and `raz fmt` follow the same token-preserving formatting policy.

The permanent native boundary used by the server is byte-oriented stdio. JSON-RPC framing, JSON handling, document state, source positions, diagnostics, formatting, and editor policy remain in Raz.

## Synchronization

The server advertises full-document synchronization (`TextDocumentSyncKind.Full`) with open/close notifications.

- `textDocument/didOpen` stores the unsaved document and publishes diagnostics.
- `textDocument/didChange` replaces the in-memory document with the supplied full text and republishes diagnostics.
- `textDocument/didClose` clears editor diagnostics and restores the saved on-disk module in the project index. If the URI no longer resolves to a file, the document is removed.

Source positions are converted from UTF-8 byte offsets to zero-based UTF-16 line/character positions as required by LSP clients. Non-BMP code points count as two UTF-16 code units.

## Current capabilities

The production server advertises only capabilities it implements:

| LSP method | Behavior |
|---|---|
| `initialize` / `shutdown` / `exit` | Normal server lifecycle |
| `textDocument/publishDiagnostics` | Compiler-backed diagnostics for open buffers |
| `textDocument/completion` | Keywords plus HIR-backed functions, parameters, and locals |
| `textDocument/hover` | Compiler-owned declaration/signature information |
| `textDocument/definition` | Scope-aware local definitions plus project/dependency global definitions, including unopened modules |
| `textDocument/references` | HIR-identity references with local/function scope preserved |
| `textDocument/rename` | Scope-safe workspace edits for resolved symbols |
| `textDocument/signatureHelp` | Function signatures at call sites |
| `textDocument/semanticTokens/full` | Keyword, function, type, variable, parameter, property, enum-member, and namespace token classes |
| `textDocument/inlayHint` | Inferred type hints where the HIR exposes the type |
| `textDocument/documentHighlight` | Semantic occurrences of the selected symbol |
| `textDocument/documentSymbol` | Top-level namespace, function, type, constant, and static symbols |
| `workspace/symbol` | Symbols from indexed project modules, direct path dependencies, and active editor overlays |
| `textDocument/formatting` | Whole-document canonical Raz formatting |
| `textDocument/codeAction` | Missing-semicolon quick fixes and `source.fixAll.raz` formatting actions |
| `textDocument/foldingRange` | Brace-delimited folding ranges |
| `textDocument/selectionRange` | Valid document selection ranges |

At `initialize`, a `file://` `rootUri` seeds a disk-backed workspace index. Raz recursively discovers `.rz` modules while excluding generated `build/`, `target/`, and `.git/` trees. Direct path dependencies declared under `[dependencies]` are indexed from their canonical native paths even when they live outside the editor workspace root. Resolved registry dependencies are read from the portable `raz.lock` and mapped through the same content-addressed package-store policy used by the package manager (`RAZ_PACKAGE_STORE`, `RAZ_HOME`, or the normal user store). Only packages already present in the verified local store are indexed; the language server never performs network fetches as an editor side effect.

Editor buffers overlay that disk index. `didOpen` and `didChange` replace one indexed module with unsaved bytes, while `didClose` reloads the saved file. Each indexed document also owns a compact HIR-derived declaration summary that is rebuilt only when that document's text changes. Global completion and workspace-symbol queries reuse these summaries instead of rebuilding HIR for every module on every request; scope-sensitive local queries still construct the active document's full HIR when required. Definitions, references, rename, completion, signature help, and workspace symbols therefore continue to work for unopened project and dependency files without repeatedly reanalyzing unchanged modules.

Semantic identity comes from the production HIR whenever a document is semantically valid. Locals and parameters carry their owning function identity, so shadowed names are not merged across scopes. For a temporarily invalid document that refers to a workspace-level global declared in another indexed module, navigation and refactoring may use exact lexer spelling as a provisional fallback until the document becomes HIR-valid. That fallback is never used for locals.

Requests for methods that are not advertised receive JSON-RPC `Method not found` rather than heuristic editor behavior.

## Diagnostics

Published diagnostics include:

- LSP error severity;
- a stable compiler diagnostic code;
- UTF-16 source range;
- the compiler's diagnostic message.

The server analyzes the current unsaved buffer rather than writing temporary files. A syntactically or semantically invalid edit can therefore be diagnosed before it is saved.

## Formatting and quick fixes

`textDocument/formatting` returns one whole-document edit. The same edit is exposed through `textDocument/codeAction` as `source.fixAll.raz` when canonical formatting would change the current document.

Formatting remains token-preserving. Syntax repair is represented separately as compiler-owned quick fixes. When the HIR reports a missing terminator at the beginning of the following source line, the server can offer an `Insert missing semicolon` edit at the end of the preceding statement without changing unrelated source.

## Release qualification

Compiler bootstrap qualification runs four end-to-end protocol suites against the newly built production compiler. The baseline suite exercises lifecycle, synchronization, diagnostics, completion, document symbols, formatting, folding, selection ranges, JSON parsing, and framing. The semantic suite qualifies hover, definition, scope-safe references and rename, cross-file navigation, signature help, semantic tokens, inferred-type hints, document highlights, and syntax quick fixes. The project-index suite starts from a real `rootUri` and verifies unopened-file definition/references/rename, generated-tree exclusion, direct path-dependency indexing, editor overlay replacement, and saved-state restoration on `didClose`. The registry-index suite supplies a portable lockfile plus an isolated content-addressed package store and verifies definition/workspace-symbol navigation into an unopened resolved registry package without network access. This prevents a release from advertising project-aware semantic LSP behavior that the shipped compiler does not actually provide.
