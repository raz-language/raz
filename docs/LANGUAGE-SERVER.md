# Raz Language Server

Raz includes an LSP server in the project driver:

```text
raz lsp
```

The server communicates over standard input/output using the Language Server Protocol `Content-Length` framing used by editors such as VS Code and compatible clients.

## Compiler-backed diagnostics

LSP diagnostics come from the same lexer, parser, namespace lowering, semantic analyzer, ownership analysis, and MIR lowering used by the compiler. The language server does **not** maintain a separate heuristic parser for errors.

Unsaved editor buffers are analyzed directly in memory. No temporary file is required.

Published diagnostics include:

- stable Raz diagnostic codes;
- LSP severity;
- zero-based UTF-16 ranges;
- contextual help/notes in diagnostic data;
- related source labels when available;
- diagnostic category metadata;
- structured fix data for compiler-generated fixes.

The source-position conversion correctly accounts for non-BMP Unicode characters, which occupy two UTF-16 code units even though Raz source is stored as UTF-8.

## Quick fixes

Parser diagnostics can attach concrete replacement edits. For example, a missing semicolon produces an insertion fix. Those fixes are exposed through `textDocument/codeAction` as preferred `quickfix` actions and use the exact compiler diagnostic/range that produced the error.

The formatter remains available as `source.fixAll.raz` when formatting would change the document.

## Synchronization

The server advertises full-document synchronization with `openClose` support. Diagnostics are published on `didOpen` and `didChange`, and are explicitly cleared on `didClose`.

## Other capabilities

The current server also exposes:

- formatting;
- hover;
- completion;
- definition;
- references;
- rename;
- workspace/document symbols;
- signature help;
- semantic tokens;
- inlay hints;
- folding ranges;
- document highlights;
- selection ranges.

Hover, completion, document/workspace symbols, definition, references, rename, signature help, inlay hints, document highlights, and semantic tokens consume the compiler-owned semantic index built from the parsed syntax tree and enriched HIR types. Local symbol identity is scope-aware, so shadowed variables are not conflated during references or rename. Open-buffer global symbols can also participate in cross-document navigation and rename.

The semantic-token legend distinguishes keywords, functions, types, variables, parameters, properties, enum members, and namespaces. `auto` local inlay hints use the type inferred by semantic analysis rather than text heuristics.
