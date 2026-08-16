# Raz Language Specification 1.0

Status: normative for the Raz 1.0 stable language surface.

Raz is a statically typed native systems programming language. The frontend owns language semantics through typed HIR and MIR; the production compiler then lowers to Forge IR for verification, optimization, machine lowering, and native object emission.

## 1. Source files and declarations

Raz source files use the `.rz` extension. Statements are terminated with semicolons. Block-form declarations and control flow use braces.

Public declarations use `public`. Functions use `fn` and type-first parameters:

```raz
public fn add(i64 left, i64 right) -> i64 {
    return left + right;
}
```

The return type may be omitted for functions that return no value. Local bindings are type-first:

```raz
i64 count = 42;
bool ready = true;
string name = "Raz";
```

`mut` appears on references and receivers where mutation permission is part of the type/borrow contract. Assignment controls whether a local value changes; Raz does not require a separate `let`/`var` binding grammar for ordinary locals.

## 2. Namespaces, modules, imports, and visibility

Packages are described by `raz.toml`. A source may establish a file-scoped namespace or a braced namespace:

```raz
namespace util::math;
```

```raz
namespace util::math {
    fn identity(i64 value) -> i64 { return value; }
}
```

Namespaces form the source-level declaration identity boundary. The same source spelling may be declared in different namespaces. Duplicate declarations in one namespace are rejected. Project composition additionally preserves package and physical-module ownership, so unrelated libraries never have to coordinate private implementation names.

Declarations have three visibility levels. `public` declarations may cross package boundaries, unmodified declarations are package-internal, and `private` declarations are restricted to their defining module. Local declarations still take precedence over imported declarations.

```raz
public struct Client {
    i64 handle;
}

struct PackageState {
    i64 generation;
}

private fn validate_handle(i64 handle) -> bool {
    return handle >= 0;
}
```

Other namespaces are made visible with `import`. An import may be qualified through an alias:

```raz
namespace app;

import util::math;
import storage::filesystem as fs;

fn use_storage(fs::File& file) -> i64 {
    return fs::size(file);
}
```

An aliased import is qualified-only: it does not inject the imported declarations into unqualified lookup. A normal import participates in unqualified lookup. Exactly one imported match may supply an unqualified name; multiple imported matches are ambiguous and rejected. Qualified references such as `util::math::identity` or `fs::File` select the target namespace explicitly.

Libraries may build façade APIs with public re-exports:

```raz
namespace net;
public import transport::tcp;
```

A consumer importing `net` may resolve public declarations re-exported by `transport::tcp` without directly depending on or importing that implementation namespace. Re-export traversal observes the original declaration visibility; package-internal and private declarations never become public merely because an import is public.

Project loading is deterministic. Dependency cycles, conflicting package identities, duplicate module namespace ownership, invalid/ambiguous import aliases, and executable `main` declarations outside the configured entry module are rejected. Dependency interfaces contain only public API declarations and carry package/module namespace identity; package-internal semantic surfaces are available only while compiling modules of the same package. External ABI declarations retain their explicit native names. Toolchain packages that require a physical source order may provide `source-order.txt`.

## 3. Primitive and compound types

The stable scalar families are:

| Family | Types |
| --- | --- |
| Signed integer | `i8`, `i16`, `i32`, `i64` |
| Unsigned integer | `u8`, `u16`, `u32`, `u64`, `usize` |
| Floating point | `f32`, `f64` |
| Logic/text scalar | `bool`, `char` |
| Owned text | `string` |

Compound forms include structs, tuples, fixed arrays, slices, references, raw pointers, payload enums, function pointers, closures, generic types, and object-safe dynamic trait objects.

Fixed arrays use type-first array declarators:

```raz
i64 values[4] = [10, 20, 30, 40];
```

## 4. Structs and enums

Struct fields are type-first:

```raz
struct Point {
    i64 x;
    i64 y;
}

Point point = Point(20, 22);
```

Enums may contain unit or payload variants:

```raz
enum Message {
    Quit,
    Number(i64),
    Pair(i64, bool),
}
```

Enum construction names the variant:

```raz
Message message = Message.Number(42);
```

## 5. Expressions and operators

Raz provides arithmetic, comparison, logical, bitwise, shift, assignment, compound assignment, calls, field projection, indexing, dereference, address/borrow expressions, casts, block expressions, and the stable control-flow forms.

Numeric conversion uses postfix `as` when an explicit conversion is required:

```raz
i64 count = 42;
f64 precise = count as f64;
```

The frontend materializes typed conversions before Forge operations. Forge is never expected to infer a source-language numeric coercion from mismatched operands.

`&&` and `||` are short-circuit logical operators. Comparisons produce `bool`.

## 6. Control flow

Stable structured control flow includes `if`, `else`, `while`, `for`, integer ranges, `break`, `continue`, `return`, `match`, and lexical `defer`.

```raz
i64 total = 0;
for value in 0..4 {
    total += value;
}
```

Inclusive ranges use `..=`.

`defer` schedules work for lexical scope exit in LIFO order, including normal early exits:

```raz
fn work(i64 value) -> i64 {
    defer value += 1;
    return value;
}
```

## 7. Pattern matching

`match` is exhaustive for supported enum patterns unless a wildcard arm covers the remaining cases.

```raz
fn evaluate(Message message) -> i64 {
    match message {
        Message.Quit => {
            return 0;
        },
        Message.Number(value) => {
            return value;
        },
        Message.Pair(left, enabled) => {
            if (enabled) {
                return left;
            }
            return 0;
        },
    }
}
```

Payload bindings are scoped to their arm.

## 8. Ownership, moves, and references

Values follow move semantics unless their type satisfies Raz's copy rules. `move` explicitly transfers ownership when required:

```raz
Resource second = move first;
```

Shared references use `T&`; mutable references use `T&mut`:

```raz
fn read(i64& value) -> i64 {
    return *value;
}

fn increment(i64&mut value) {
    *value += 1;
}
```

Borrow checking covers overlapping mutable/shared access, use after move, partial moves, field borrows, reborrows, mutation through shared references, invalid lifetime escape, and references that cannot safely survive an async suspension. Loans are non-lexical: a local reference stops constraining its source after its final use rather than remaining active until the closing brace. This permits mutation after an unused borrow or after the last read while still rejecting genuinely overlapping aliases.

Safe Raz also tracks reference provenance through aggregate construction. A local borrow cannot escape indirectly inside a returned struct, tuple, or array, including when the aggregate is first assigned to a local and returned later. Reference and slice bindings are non-rebindable aliases; create a new binding instead of silently changing the storage a live alias designates.

## 9. Deterministic destruction

User-defined resources implement `Drop`:

```raz
struct FileHandle {
    i64 handle;
}

impl Drop for FileHandle {
    fn drop(FileHandle&mut self) {
        self;
    }
}
```

The compiler elaborates cleanup into MIR so destruction follows ownership and control flow rather than relying on a tracing garbage collector. MIR is then verified as an ownership boundary: storage lifetime, whole-value moves, shared/exclusive borrows, and final drops are represented explicitly enough for the compiler to reject malformed use-after-move/drop control-flow before either native backend runs.

## 10. Generics and traits

Functions and user types may be generic. Trait bounds participate in semantic checking and static dispatch:

```raz
trait Measurable {
    fn measure(Self& self) -> i64;
}

fn read<T: Measurable>(T& value) -> i64 {
    return value.measure();
}
```

Stable trait functionality includes:

- generic functions and types;
- multiple bounds and supertraits;
- default methods;
- associated types and associated constants;
- generic implementations;
- coherence/overlap checking;
- const generics;
- static dispatch; and
- object-safe dynamic dispatch.

Trait resolution follows a coherence-first contract. A concrete positive or negative implementation may not overlap a generic implementation that structurally covers the same target, regardless of declaration order. Compiler-owned traits (`Copy`, `Clone`, and `Drop`) cannot be implemented or negated for compiler-owned primitive/structural types; a local user trait may still be implemented for such a type. Associated-type projections nested inside enclosing generic arguments are normalized selectively before compatibility checking, while ordinary generic arguments stay on the fast path. Trait proof queries maintain an active-obligation set; re-entering the same obligation is treated as not proven, which guarantees deterministic termination for recursive solver paths. Coherence is established when implementations are registered, so resolution performs a deterministic unique lookup without re-proving global uniqueness on every query.

Generic static dispatch is monomorphized into concrete native code.

## 11. Iteration

`for` supports fixed arrays, integer ranges, borrowed arrays, custom `Iterator` implementations, and `IntoIterator` where the relevant stable contracts are satisfied.

```raz
i64 values[4] = [1, 2, 3, 4];
for value in &mut values {
    *value += 10;
}
```

Temporary iterable cleanup and ownership behavior follow the same lifetime/drop model as ordinary values.

## 12. Compile-time programming

Raz supports deterministic constant expressions, const functions, const generics, `comptime` blocks, compile-time assertions/loops, selected reflection, layout facilities, and supported derive operations.

```raz
const i64 BASE = 40 + 2;

comptime {
    assert(BASE == 42);
}
```

Compile-time execution is bounded and deterministic. Stable compile-time behavior may not depend on ambient nondeterministic process state.

## 13. Closures and function pointers

Function-pointer types use `fn(...) -> ...` syntax. Closures may capture by shared borrow, mutable borrow, or move, subject to ordinary ownership rules.

```raz
fn apply(fn(i64)->i64 operation, i64 value) -> i64 {
    return operation(value);
}
```

Callable kinds distinguish reusable shared callables, mutable callables, and once-only callables. Non-capturing closures may coerce to compatible function-pointer types.

## 14. Async

Stable structured async includes `async fn`, `spawn`, and `await`:

```raz
async fn child() -> i64 {
    return 2;
}

async fn pipeline() -> i64 {
    i64 task = spawn child();
    i64 result = await task;
    return 40 + result;
}
```

The compiler tracks values live across suspension points. Owned values can be carried by compiler-generated async frames; references are rejected when the source lifetime cannot safely span a suspension.

## 15. Unsafe code and native ABI

Raw pointers and native operations remain explicit. `extern` declarations form the runtime/FFI boundary. Native helpers are intended for OS, ABI, atomic, SIMD, and similar host operations rather than for implementing ordinary language semantics outside Raz.

Unsafe operations are accepted only in the stable forms recognized by the compiler and conformance suite.

## 16. Packages and dependencies

The normal package manifest is `raz.toml`:

```toml
[package]
name = "hello"
version = "1.0.0"
kind = "executable"
source = "src"
entry = "src/main.rz"

[dependencies]
```

Path dependencies are loaded recursively. The project graph rejects cycles, deduplicates repeated diamond dependencies, and orders dependency sources before dependent sources.

## 17. Compilation semantics

The production path is:

```text
Raz source
  -> lexer / parser / semantic analysis
  -> typed HIR
  -> MIR + ownership/drop elaboration
  -> Forge IR
  -> Forge verification and optimization
  -> machine lowering / register allocation
  -> native ELF64 or COFF object
  -> platform link
```

The use of Forge does not change Raz's source-level semantics. Backend substitutions are not part of the Raz 1.0 production contract.

## 18. Diagnostics and implementation freedom

Programs outside the stable surface must be rejected deterministically rather than silently assigned unspecified semantics. Implementations may optimize freely provided observable behavior, ownership guarantees, ABI contracts, deterministic compile-time behavior, and accepted program semantics remain unchanged.

Stable compatibility is defined jointly by this document, `LANGUAGE-STABILITY.md`, and the repository's conformance suite.

## MIR lifetime regions

After HIR ownership checking, Raz lowers reference aliases into MIR borrow bindings. The MIR region solver derives each borrow's live region from actual CFG uses rather than lexical scope. Reborrows form child regions constrained by the parent loan; loop backedges are included when a borrow is loop-carried; and async suspension points are recorded explicitly. A local borrow may cross suspension only when its source storage is stabilized in the async frame. Caller-provided reference parameters are treated as caller-owned regions and may cross suspension subject to the function lifetime contract. Backends never see unchecked regions: MIR verification rejects a region that can outlive a moved, dropped, or dead source projection.
