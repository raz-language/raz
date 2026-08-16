# Frontend integration

Forge 1.2 provides an optional `forge::frontend` support layer for language authors. It does not prescribe a grammar or AST. It supplies the infrastructure most frontends otherwise rebuild themselves.

## Components

- `SourceManager` owns source files and maps byte offsets to line/column positions.
- `DiagnosticEngine` emits coded diagnostics with notes, source excerpts, and fix-it replacements.
- `SymbolTable` provides nested lexical scopes with safe shadowing.
- `SemanticContext` tracks functions and shared semantic state.
- `ControlFlowBuilder` creates valid `if` and `while` block structures using stable Forge handles.

```cpp
#include <forge/frontend/frontend.hpp>

forge::frontend::SourceManager sources;
auto file = sources.add("main.my", source_text);
forge::frontend::DiagnosticEngine diagnostics(sources);
forge::frontend::SemanticContext semantics(sources, diagnostics);
```

## Project generator

After installing Forge:

```bash
forge new-language Aurora aurora
cmake -S aurora -B aurora/build -G Ninja -DCMAKE_PREFIX_PATH=/path/to/forge/install
cmake --build aurora/build
```

The generated project is intentionally small and is designed to be replaced incrementally with a lexer, parser, semantic analyzer, and lowering layer.

## Relationship to MiniLang

Use `examples/frontend/minilang` for a complete source-to-JIT walkthrough. Use `forge new-language` when beginning a new standalone project.
