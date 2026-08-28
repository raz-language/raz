# Raz Language Specification 1.0

Status: normative for the Raz 1.0 stable language surface.

Raz is a statically typed native systems programming language. The frontend owns language semantics through typed HIR and MIR; the production compiler then lowers to Forge IR for verification, optimization, machine lowering, and native object emission.

This specification is the normative prose reference. [Language stability](LANGUAGE-STABILITY.md) defines what the 1.x compatibility promise covers, and the repository conformance suite is the executable compatibility reference. Where this document and the conformance suite disagree, the disagreement is a defect in one of them and should be reported.

## Contents

| | Section |
|---|---|
| 1 | [Source files and declarations](#1-source-files-and-declarations) |
| 2 | [Lexical structure](#2-lexical-structure) |
| 3 | [Namespaces, modules, imports, and visibility](#3-namespaces-modules-imports-and-visibility) |
| 4 | [Primitive and compound types](#4-primitive-and-compound-types) |
| 5 | [Structs and enums](#5-structs-and-enums) |
| 6 | [Expressions and operators](#6-expressions-and-operators) |
| 7 | [Control flow](#7-control-flow) |
| 8 | [Pattern matching](#8-pattern-matching) |
| 9 | [Ownership, moves, and references](#9-ownership-moves-and-references) |
| 10 | [Deterministic destruction](#10-deterministic-destruction) |
| 11 | [Generics and traits](#11-generics-and-traits) |
| 12 | [Iteration](#12-iteration) |
| 13 | [Compile-time programming](#13-compile-time-programming) |
| 14 | [Closures and function pointers](#14-closures-and-function-pointers) |
| 15 | [Async](#15-async) |
| 16 | [Unsafe code and native ABI](#16-unsafe-code-and-native-abi) |
| 17 | [Packages and dependencies](#17-packages-and-dependencies) |
| 18 | [Compilation semantics](#18-compilation-semantics) |
| 19 | [Diagnostics and implementation freedom](#19-diagnostics-and-implementation-freedom) |
| A | [MIR lifetime regions](#appendix-a-mir-lifetime-regions) |
| B | [Grammar summary](#appendix-b-grammar-summary) |
| C | [Glossary](#appendix-c-glossary) |

### Notation

Language constructs are written in `code` style. Grammar fragments use `<placeholder>` for a nonterminal, `[...]` for an optional element, and `...` for repetition. Diagnostic codes are written in their stable `D####` form; see the [CLI reference](CLI.md#diagnostics) for categories and rendering.

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

### 1.1 Attributes

A declaration may be prefixed with one or more attributes, each written `@name` or `@name(arguments)`.

```raz
@repr(C)struct NativeHeader {
    u8 kind;
    u32 length;
}

@pure@no_panic@no_alloc@deterministic fn leaf(i64 value) -> i64 {
    return value + 1;
}
```

The stable attributes are:

| Attribute | Applies to | Effect |
|---|---|---|
| `@repr(C)` | struct | Lay the type out under the platform C ABI |
| `@packed` | struct | Remove inter-field padding; alignment becomes 1 |
| `@align(N)` | struct | Raise alignment to `N`, which must be a positive power of two |
| `@derive(...)` | struct · enum | Generate supported implementations, such as `Copy` and `Clone` |
| `@pure` | fn | The function has no observable side effects |
| `@no_panic` | fn | The function does not panic |
| `@no_alloc` | fn | The function does not allocate |
| `@deterministic` | fn | The function does not depend on ambient nondeterministic state |
| `@abi(...)` · `@link_name(...)` | extern fn | Native calling convention and external symbol name |
| `@target_feature(...)` | fn | Required target feature for the function body |

Effect attributes propagate: a function declaring an effect may only call functions that also satisfy it. `@align` with a non-power-of-two argument is rejected at the attribute rather than at use.

## 2. Lexical structure

A source file is a sequence of bytes that is converted to a token stream before parsing. Lexical errors are reported in the `lexer` diagnostic category (`D0000`-`D0999`).

### 2.1 Whitespace and comments

Whitespace separates tokens and is otherwise insignificant. Raz is not whitespace-sensitive and statement termination is explicit.

Two comment forms are recognized:

```raz
// line comment: runs to the end of the line

/* block comment:
   runs to the matching delimiter */
```

Block comments **nest**. `/* outer /* inner */ still a comment */` is one comment, and an unterminated block comment is rejected as `D0001` rather than silently consuming the rest of the file.

A line comment beginning with exactly three slashes is a **documentation comment**:

```raz
/// Compact 128-bit UUID value stored in RFC 9562 byte order.
public struct Uuid {
    u64 high;
    u64 low;
}
```

Documentation comments attach to the declaration that follows them and are collected by `raz doc`. The declaration forms that carry documentation are `fn`, `struct`, `enum`, `trait`, and `const`, each optionally prefixed with `public`. Documentation comments are otherwise ordinary comments and do not affect compilation.

### 2.2 Identifiers

An identifier begins with an alphabetic character, `_`, or a byte at or above `0x80`, and continues with alphanumeric characters, `_`, or bytes at or above `0x80`. Identifiers are case-sensitive.

Because the continuation set includes bytes at or above `0x80`, non-ASCII identifiers are accepted; portable source is expected to be UTF-8.

### 2.3 Keywords

The following words are reserved and may not be used as ordinary identifiers:

```text
as        async     auto      await     break     case      comptime
const     continue  defer     derive    dyn       else      enum
extern    false     fn        for       global    if        impl
import    in        match     move      mut       namespace null
private   public    ref       return    self      spawn     static
struct    thread_local        trait     true      type      union
unsafe    while
```

`Self` is reserved as the implementing-type name inside trait and impl bodies.

`Copy`, `Clone`, and `Drop` name compiler-owned traits. They are not keywords, but they carry compiler meaning: they cannot be implemented or negated for compiler-owned primitive and structural types (see [§11](#11-generics-and-traits)).

Attribute-style words recognized in their specific positions include `link_name`, `target_feature`, and the ABI strings accepted after `extern` (see [§16](#16-unsafe-code-and-native-abi)).

### 2.4 Literals

**Integer literals** are decimal by default and accept base prefixes:

| Form | Base | Example |
|---|---|---|
| *(no prefix)* | 10 | `42` |
| `0x` · `0X` | 16 | `0xFF` |
| `0o` · `0O` | 8 | `0o755` |
| `0b` · `0B` | 2 | `0b1010_1010` |

The `_` digit separator is permitted anywhere among the digits and is ignored. A base prefix must be followed by at least one digit; `0x` alone is rejected as `D0002`.

**Floating-point literals** are decimal only. A fractional part requires a digit after the `.`, which is what keeps `0..4` lexing as a range rather than a malformed float. An exponent uses `e` or `E` with an optional sign and requires at least one digit, otherwise `D0003`.

```raz
f64 ratio = 1.5;
f64 scaled = 6.02e23;
f64 small = 1.0E-9;
```

Base-prefixed literals have no fractional or exponent form.

**String literals** are delimited by `"` and may not span a line; an unterminated string is `D0004`. **Character literals** are delimited by `'` and contain exactly one character or one escape sequence, otherwise `D0005` or `D0006`.

The recognized escape sequences each encode exactly one byte:

| Escape | Byte | Meaning |
|---|---|---|
| `\n` | 10 | line feed |
| `\r` | 13 | carriage return |
| `\t` | 9 | horizontal tab |
| `\0` | 0 | null |
| `\<c>` | *`<c>`* | the character itself, covering `\\`, `\"`, and `\'` |

There is no `\u{...}` or `\xNN` form in the 1.0 surface. Non-ASCII text in a string literal is carried through as its source bytes.

**Boolean literals** are `true` and `false`.

### 2.5 Operators and punctuation

| Category | Tokens |
|---|---|
| Grouping | `(` `)` `{` `}` `[` `]` |
| Separators | `,` `;` `:` `::` `.` `..` `..=` `...` |
| Arrows and markers | `->` `=>` `?` `@` `#` |
| Generic delimiters | `<` `>` around type arguments |
| Arithmetic | `+` `-` `*` `/` `%` |
| Bitwise and shift | `&` `\|` `^` `~` `<<` `>>` |
| Comparison | `==` `!=` `<` `<=` `>` `>=` |
| Logical | `&&` `\|\|` `!` |
| Assignment | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` |
| Increment and decrement | `++` `--` |

`&` introduces a shared borrow and `&mut` a mutable borrow in expression position; `*` is both multiplication and dereference; `..` and `..=` build exclusive and inclusive ranges; `?` propagates a non-success result (see [§8.1](#81-absence-and-recoverable-errors)); `->` introduces a return type and `=>` separates a match arm from its body.

A type-argument list is delimited by `<` and `>`. An explicit generic call is recognized only when a balanced type-argument list is immediately followed by `(`, which keeps `left < right` unambiguous as a comparison.

### 2.6 Precedence and associativity

Operators bind as follows, from loosest to tightest. Every binary operator is left-associative; assignment is right-associative.

| Level | Operators | Associativity |
|---|---|---|
| 1 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | right |
| 2 | `..` `..=` | left |
| 3 | `\|\|` | left |
| 4 | `&&` | left |
| 5 | `\|` | left |
| 6 | `^` | left |
| 7 | `&` | left |
| 8 | `==` `!=` | left |
| 9 | `<` `<=` `>` `>=` | left |
| 10 | `<<` `>>` | left |
| 11 | `+` `-` | left |
| 12 | `*` `/` `%` | left |
| 13 | prefix `-` `!` `~` `*` `&` `&mut` `move` | right |
| 14 | postfix `as` `?`, call `()`, index `[]`, field `.`, path `::` | left |

Two consequences are worth stating explicitly because they differ from some other C-family languages:

- **Bitwise operators bind more loosely than comparison.** `a & b == c` parses as `a & (b == c)`. Parenthesize when the bitwise result is the comparison operand.
- **`as` binds tighter than any binary operator.** `count as f64 / total` converts `count` and then divides. Parenthesize when a conversion is combined with a prefix operator, since `-count as f64` reads ambiguously to a human even where the grammar is decided.

Assignment is accepted only where an expression is parsed from the top — statement position, or inside grouping and argument delimiters. It is not accepted as the operand of a binary operator.

## 3. Namespaces, modules, imports, and visibility

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

## 4. Primitive and compound types

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

### 4.1 Layout and alignment

Scalar sizes and alignments are those of the target ABI rather than values fixed by the language. They are observable at compile time through `size_of<T>()` and `align_of<T>()`, and at run time through `core::abi`, which also reports byte order.

Aggregate layout is chosen by the implementation unless an attribute constrains it. A struct's alignment is the maximum alignment of its fields, its size is a multiple of its alignment, and padding may be inserted between fields and at the end.

The layout attributes from [§1.1](#11-attributes) make layout explicit and are checkable in a `comptime` block:

```raz
@repr(C)struct NativeHeader {
    u8 kind;
    u32 length;
}

@packed struct PackedHeader {
    u8 kind;
    u32 length;
}

@align(32)struct CacheLineValue {
    i64 value;
}

comptime {
    assert(size_of<NativeHeader>() == 8);
    assert(align_of<NativeHeader>() == 4);
    assert(size_of<PackedHeader>() == 5);
    assert(align_of<PackedHeader>() == 1);
    assert(size_of<CacheLineValue>() == 32);
    assert(align_of<CacheLineValue>() == 32);
}
```

`@repr(C)` is required whenever a type crosses the native ABI boundary, since the default layout is not guaranteed to match the platform C layout. `@packed` removes padding and therefore produces fields that may be unaligned; reading them through raw pointers is subject to the target's unaligned-access rules.

## 5. Structs and enums

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

## 6. Expressions and operators

Raz provides arithmetic, comparison, logical, bitwise, shift, assignment, compound assignment, calls, field projection, indexing, dereference, address/borrow expressions, casts, block expressions, and the stable control-flow forms. The token inventory is given in [§2.5](#25-operators-and-punctuation) and binding strength in [§2.6](#26-precedence-and-associativity); this section covers evaluation rules.

Numeric conversion uses postfix `as` when an explicit conversion is required:

```raz
i64 count = 42;
f64 precise = count as f64;
```

The frontend materializes typed conversions before Forge operations. Forge is never expected to infer a source-language numeric coercion from mismatched operands.

`&&` and `||` are short-circuit logical operators. Comparisons produce `bool`.

### 6.1 Conversions and coercions

Raz does not insert numeric conversions. Every change of numeric type is written with `as`, including widening, which keeps the cost and the truncation risk visible at the point where they occur. Mixed-type operands are a type error (`D2009`) rather than a silent promotion.

The conversions the language does apply implicitly are structural rather than numeric:

| Coercion | Where it applies |
|---|---|
| Reborrow `T&mut` → `T&` | Passing a mutable reference where a shared reference is expected |
| Reborrow of an existing reference | Passing a reference through to another call |
| Non-capturing closure → function pointer | Assigning or passing a closure that captures nothing |
| Concrete type → trait object | Where an object-safe `dyn` trait type is expected |

No other implicit conversion exists in the 1.0 surface. In particular there is no bool-to-integer conversion, no integer-to-pointer conversion, and no user-defined implicit conversion mechanism.

## 7. Control flow

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

## 8. Pattern matching

`match` is exhaustive for enum patterns unless an unguarded wildcard arm covers the remaining cases. Patterns may recursively destructure enum payloads and named struct fields. A terminal `..` ignores the remaining payloads or fields at the current destructuring level.

```raz
fn evaluate(Packet packet) -> i64 {
    match packet {
        Packet::Message(Header { mode: Mode::Ready(ref value), .. }) if *value > 0 => {
            return *value;
        },
        Packet::Message(Header { mode: Mode::Ready(value), .. }) => {
            return value;
        },
        Packet::Message(Header { mode: Mode::Idle, .. }) => {
            return 0;
        },
    }
}
```

Payload bindings are scoped to their arm. Plain bindings and `move name` bind by value. `ref name` creates a shared borrow of the matched place, while `ref mut name` creates an exclusive borrow and permits mutation through `*name`. Pattern borrows use the same path-sensitive loan rules as expression borrows: disjoint payload or struct-field paths may be borrowed independently, while overlapping mutable/shared paths conflict. Guarded arms do not contribute to exhaustiveness because their condition is evaluated at runtime.

### 8.1 Absence and recoverable errors

Absence and recoverable failure are ordinary typed enum values rather than a separate exception mechanism. The library forms are shaped as:

```raz
enum Option<T> {
    None,
    Some(T),
}

enum Result<T, E> {
    Ok(T),
    Error(E),
}
```

Postfix `?` propagates the non-success path and evaluates to the success payload on the success path:

```raz
fn relay(bool fail) -> Result<i64, i64> {
    i64 value = produce(fail)?;
    return Result<i64, i64>.Ok(value + 1);
}
```

`?` is valid only where the enclosing function's return type can carry the propagated non-success value. Recoverable failure therefore stays visible in the function signature, and no implementation is permitted to substitute unwinding for this model.

## 9. Ownership, moves, and references

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

## 10. Deterministic destruction

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

## 11. Generics and traits

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

## 12. Iteration

`for` supports fixed arrays, integer ranges, borrowed arrays, custom `Iterator` implementations, and `IntoIterator` where the relevant stable contracts are satisfied.

```raz
i64 values[4] = [1, 2, 3, 4];
for value in &mut values {
    *value += 10;
}
```

Temporary iterable cleanup and ownership behavior follow the same lifetime/drop model as ordinary values.

## 13. Compile-time programming

Raz supports deterministic constant expressions, const functions, const generics, `comptime` blocks, compile-time assertions/loops, selected reflection, layout facilities, and supported derive operations.

```raz
const i64 BASE = 40 + 2;

comptime {
    assert(BASE == 42);
}
```

Compile-time execution is bounded and deterministic. Stable compile-time behavior may not depend on ambient nondeterministic process state.

## 14. Closures and function pointers

Function-pointer types use `fn(...) -> ...` syntax. Closures may capture by shared borrow, mutable borrow, or move, subject to ordinary ownership rules.

```raz
fn apply(fn(i64)->i64 operation, i64 value) -> i64 {
    return operation(value);
}
```

Callable kinds distinguish reusable shared callables, mutable callables, and once-only callables. Non-capturing closures may coerce to compatible function-pointer types.

## 15. Async

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

## 16. Unsafe code and native ABI

Raw pointers and native operations remain explicit. `extern` declarations form the runtime/FFI boundary. Native helpers are intended for OS, ABI, atomic, SIMD, and similar host operations rather than for implementing ordinary language semantics outside Raz.

Unsafe operations are accepted only in the stable forms recognized by the compiler and conformance suite.

## 17. Packages and dependencies

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

## 18. Compilation semantics

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

## 19. Diagnostics and implementation freedom

Programs outside the stable surface must be rejected deterministically rather than silently assigned unspecified semantics. Implementations may optimize freely provided observable behavior, ownership guarantees, ABI contracts, deterministic compile-time behavior, and accepted program semantics remain unchanged.

Stable compatibility is defined jointly by this document, `LANGUAGE-STABILITY.md`, and the repository's conformance suite.

## Appendix A: MIR lifetime regions

After HIR ownership checking, Raz lowers reference aliases into MIR borrow bindings. The MIR region solver derives each borrow's live region from actual CFG uses rather than lexical scope. Reborrows form child regions constrained by the parent loan; loop backedges are included when a borrow is loop-carried; and async suspension points are recorded explicitly. A local borrow may cross suspension only when its source storage is stabilized in the async frame. Caller-provided reference parameters are treated as caller-owned regions and may cross suspension subject to the function lifetime contract. Backends never see unchecked regions: MIR verification rejects a region that can outlive a moved, dropped, or dead source projection.

## Appendix B: Grammar summary

Informative summary of the stable surface syntax. `<x>` is a nonterminal, `[x]` is optional, `x...` is repetition, and `|` separates alternatives. Where this summary and the normative sections disagree, the sections govern.

```text
<source-file>   ::= [<namespace-decl>] <item>...

<namespace-decl>::= "namespace" <path> ";"
                  | "namespace" <path> "{" <item>... "}"

<item>          ::= [<attribute>...] [<visibility>] <declaration>
<visibility>    ::= "public" | "private"
<attribute>     ::= "@" <identifier> [ "(" <attribute-args> ")" ]

<declaration>   ::= <import> | <function> | <struct> | <enum> | <trait>
                  | <impl> | <const> | <type-alias> | <extern-block>

<import>        ::= ["public"] "import" <path> ["as" <identifier>] ";"
<path>          ::= <identifier> ("::" <identifier>)...

<function>      ::= ["async"] "fn" <identifier> [<generics>]
                    "(" [<parameters>] ")" ["->" <type>] <block>
<parameters>    ::= <type> <identifier> ("," <type> <identifier>)...
<generics>      ::= "<" <generic-param> ("," <generic-param>)... ">"
<generic-param> ::= <identifier> [":" <bounds>] | "const" <type> <identifier>
<bounds>        ::= <path> ("+" <path>)...

<struct>        ::= "struct" <identifier> [<generics>] "{" <field>... "}"
<field>         ::= <type> <identifier> ["[" <expression> "]"] ";"

<enum>          ::= "enum" <identifier> [<generics>] "{" <variant> ("," <variant>)... "}"
<variant>       ::= <identifier> [ "(" <type> ("," <type>)... ")" ]

<trait>         ::= "trait" <identifier> [<generics>] "{" <trait-item>... "}"
<trait-item>    ::= <function-signature> ";" | <function> | <associated-type>
                  | <associated-const>

<impl>          ::= "impl" [<generics>] [<path> "for"] <type> "{" <function>... "}"

<type>          ::= <path> [<type-args>]
                  | <type> "&" | <type> "&mut"
                  | <type> "*const" | <type> "*mut"
                  | "fn" "(" [<type> ("," <type>)...] ")" ["->" <type>]
                  | "dyn" <path>
                  | "(" <type> ("," <type>)... ")"

<block>         ::= "{" <statement>... "}"
<statement>     ::= <local-decl> | <expression> ";" | <if> | <while> | <for>
                  | <match> | <return> | <break> | <continue> | <defer>
                  | <comptime> | <unsafe-block> | <block>

<local-decl>    ::= <type> <identifier> ["[" <expression> "]"] ["=" <expression>] ";"
<if>            ::= "if" "(" <expression> ")" <block> ["else" (<if> | <block>)]
<while>         ::= "while" "(" <expression> ")" <block>
<for>           ::= "for" <identifier> "in" <expression> <block>
<match>         ::= "match" <expression> "{" <match-arm>... "}"
<match-arm>     ::= <pattern> ["if" <expression>] "=>" (<block> | <expression>) [","]
<pattern>       ::= <path> ["(" <pattern-item> ("," <pattern-item>)* ["," ".."] ")"] | "_"
<pattern-item>  ::= <binding> | <pattern> | <struct-pattern> | "_"
<struct-pattern>::= <path> "{" <struct-pattern-field> ("," <struct-pattern-field>)* ["," ".."] "}"
<struct-pattern-field> ::= ["move" | "ref" ["mut"]] <identifier>
                       | <identifier> ":" <pattern-item>
<binding>       ::= ["move" | "ref" ["mut"]] <identifier>
<return>        ::= "return" [<expression>] ";"
<defer>         ::= "defer" <statement>
<comptime>      ::= "comptime" <block>
<unsafe-block>  ::= "unsafe" <block>

<expression>    ::= see §2.6 for precedence and associativity
```

## Appendix C: Glossary

| Term | Meaning |
|---|---|
| **Borrow** | A reference to storage owned elsewhere. Shared (`T&`) borrows may coexist; a mutable (`T&mut`) borrow is exclusive. |
| **Coherence** | The rule that a concrete implementation may not overlap a generic implementation covering the same target, independent of declaration order. |
| **Conformance suite** | The executable compatibility reference. Where prose and suite disagree, one of them is defective. |
| **Drop** | Deterministic destruction of an owned value at the end of its lifetime, elaborated into MIR rather than performed by a collector. |
| **Effect attribute** | `@pure`, `@no_panic`, `@no_alloc`, or `@deterministic`; a checked and propagated property of a function. |
| **Forge** | The default native backend, linked in-process, consuming verified MIR. |
| **HIR** | Typed high-level IR produced after semantic analysis. |
| **Loan** | A tracked borrow with a region derived from actual uses. |
| **MIR** | Backend-neutral mid-level IR, verified as the ownership boundary before any backend runs. |
| **Monomorphization** | Generating concrete code per generic instantiation, giving static dispatch its cost model. |
| **Move** | Transfer of ownership; the source binding becomes unusable. |
| **Namespace** | The source-level declaration identity boundary, declared with `namespace`. |
| **Non-lexical loan** | A borrow whose region ends at its final use rather than at the enclosing scope's brace. |
| **Object safety** | The property that lets a trait be used as a `dyn` trait object. |
| **Provenance** | The origin storage a reference designates, tracked through aggregates so borrows cannot escape indirectly. |
| **Reborrow** | Deriving a new reference from an existing one, forming a child region constrained by the parent loan. |
| **RXE** | The portable bytecode target and its container format. |
| **Suspension point** | An `await` in an async function, where liveness across the suspension constrains what may be held. |
