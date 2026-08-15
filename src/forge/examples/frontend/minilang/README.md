# MiniLang: a complete Forge frontend example

MiniLang is a deliberately small language frontend that demonstrates the entire path from source code to executable machine code:

```text
MiniLang source
    ↓ lexer
Tokens
    ↓ parser
Typed-by-construction AST
    ↓ semantic checks + lowering
Forge IR
    ↓ verifier
Verified Forge IR
    ├─ interpreter
    └─ machine lowering → optimizer → x86-64 JIT
```

Unlike `tiny_frontend.cpp`, this example is organized like a real language implementation. Each frontend responsibility has its own file and API boundary.

## Language

All values are signed 64-bit integers. MiniLang supports:

- Functions and parameters
- Local immutable bindings with `let`
- Integer literals
- Function calls
- `+`, `-`, `*`, `/`
- `==`, `!=`, `<`, `<=`, `>`, `>=`
- `if` / `else`
- `return`
- `//` comments

Example:

```text
fn square(value) {
    return value * value;
}

fn main() {
    let input = 7;
    if input < 10 {
        return square(input) + 1;
    } else {
        return input * 2;
    }
}
```

## Project layout

```text
minilang/
├── CMakeLists.txt
├── README.md
├── example.mini
├── include/minilang/
│   ├── ast.hpp          AST nodes
│   ├── codegen.hpp      Forge lowering entry point
│   ├── lexer.hpp        Lexer API
│   ├── parser.hpp       Parser API
│   ├── source.hpp       Source spans
│   └── token.hpp        Token definitions
└── src/
    ├── codegen.cpp      Semantic checks + Forge IR lowering
    ├── lexer.cpp        Source text → tokens
    ├── main.cpp         Driver, verifier, interpreter, JIT
    └── parser.cpp       Tokens → AST
```

## Build against an installed Forge SDK

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/minilang example.mini main --source-map
```

On Windows:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\minilang.exe example.mini main --source-map
```

When Forge is installed in a custom prefix, add:

```text
-DCMAKE_PREFIX_PATH=/path/to/forge/install
```

## What to study

### 1. Lexer (`src/lexer.cpp`)

The lexer records complete source spans for every token. Those spans eventually become Forge operation locations and JSON source-map entries.

### 2. Parser (`src/parser.cpp`)

The recursive-descent parser demonstrates operator precedence, statements, blocks, function declarations, error recovery, and ownership-safe AST construction with `std::unique_ptr`.

### 3. AST (`include/minilang/ast.hpp`)

The AST is independent of Forge. This is important: language syntax and semantics should not be forced to mirror backend IR structure.

### 4. Semantic lowering (`src/codegen.cpp`)

The lowerer owns frontend semantics:

- Name lookup
- Duplicate declaration checks
- Function lookup
- Return-path checks
- Operator selection
- Source range propagation
- Frontend metadata and attributes

It then emits core Forge operations through stable `FunctionHandle` and `BlockHandle` values.

### 5. Driver (`src/main.cpp`)

The driver shows the complete embedding workflow:

1. Read source
2. Lex and parse
3. Lower to Forge IR
4. Verify
5. Print canonical Forge IR
6. Optionally print a JSON source map
7. Execute with the Forge interpreter
8. Lower and optimize machine IR
9. Execute with the x86-64 JIT
10. Compare interpreter and JIT results

## Deliberate simplifications

MiniLang is an educational frontend, not a production language. To keep the lowering easy to follow:

- Every value is `i64`
- Bindings are immutable
- Each `if` branch must terminate with `return`
- There are no loops, strings, structs, modules, or user-defined types
- Calls are only to functions in the same source file

The next natural improvements are lexical scopes, a type checker, block-parameter-based expression joins, loops, external declarations, native object emission, and incremental compilation.
