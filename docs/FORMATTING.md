# Source formatting

Raz keeps formatting deliberately boring. A contributor should be able to open a file and spend attention on the language or compiler logic rather than somebody else's whitespace preferences.

## Raz source

Raz-owned `.rz` files use four spaces, braces on the same line as declarations/control flow, one statement per line, and semicolon-terminated statements. The repository formatter is the source of truth:

```text
python tools/format-raz.py path/to/source.rz
python tools/format-raz.py path/to/source.rz --check
```

The formatter is deterministic, idempotent, and width-driven. It targets 110 columns for Raz source: short signatures, calls, and conditions stay on one line, while overflowing parameter/argument lists and boolean conditions are expanded structurally with four-space continuation indentation. Multiline parameter and argument lists use a trailing comma; compact single-line lists do not. Mutable references are always spelled `T&mut`.

Long boolean conditions break at top-level `&&` / `||` operators, and nested calls are only expanded when their own width requires it. Parentheses used purely for grouping are never treated as comma-list constructs, so formatting cannot turn `(expr)` into a tuple. Generic closing brackets remain parser-safe (`Outer<Inner<T> >`) until the lexer/parser can distinguish nested generic closes from the `>>` shift token without spacing help.

Comments should explain **why** something exists, the invariant a maintainer must preserve, or a non-obvious performance/correctness tradeoff. Avoid comments that simply translate the next line into English.

## Native C++

Raz-owned C++ follows the root `.clang-format` and `.editorconfig` files: two-space indentation, spaces instead of tabs, attached braces, and a 120-column target where practical. Mechanical ABI dispatch tables may remain wider when wrapping them would make the arity mapping harder to verify.

`src/forge/` is the bundled C++ Forge production backend and follows Forge's native formatting (`.clang-format`). It is a backend dependency, not Raz-language source. `src/bootstrap/` remains the native host compiler; Raz-owned compiler and library `.rz` code follows Raz formatting.

## Scripts and build files

Python, PowerShell, CMake, and batch files use spaces, LF line endings, final newlines, and no trailing whitespace. Keep command wrappers small; put reusable logic in the corresponding script/module instead of duplicating it across entry points.

## Before submitting

Run the formatter and repository hygiene checks before opening a change:

```text
python tests/python/check-source.py
python tests/python/check-raz-formatter-layout.py
python tools/format-cpp-spacing.py --check src tests
```

Format the Raz files you touched with `tools/format-raz.py`; the repository does
not rewrite the entire qualified compiler tree as part of an unrelated change.

Formatting changes should not change Raz semantics. Compiler-formatting changes are additionally covered by the recursive fixed-point and bootstrap-format gates.
