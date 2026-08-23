# C interoperability

Raz exposes C ABI boundaries with `@abi(C)` functions and C-layout aggregates with `@repr(C)`. The `raz bindgen` command converts a practical, deterministic subset of C headers into Raz declarations without requiring Clang or libclang at runtime.

## Generate bindings

```text
raz bindgen <header.h> [--target-abi=windows|unix] [output.rz]
```

If `output.rz` is omitted, bindgen writes the generated source to stdout. `--target-abi=windows` uses the Windows x64 LLP64 data model (`long` is 32-bit). `--target-abi=unix` uses the Unix LP64 data model used by supported x86-64 and AArch64 Unix targets (`long` is 64-bit).

The generated source is intended to be checked into the consuming package when a stable native API is being wrapped. This keeps builds deterministic and avoids requiring the original C headers on every downstream machine.


## Export a Raz API to C

```text
raz c-header <source.rz|directory> [output.h]
```

`raz c-header` accepts either one Raz source file or a directory/package tree. Directory mode recursively combines maintained `.rz` sources into one header while excluding generated `build/` and `target/` trees. It emits only declarations whose source explicitly opts into a C-compatible ABI. C-layout aggregates must be `@repr(C) public`, and callable exports must be `@abi(C) public fn`. Ordinary `public fn` declarations keep Raz's native ABI and internal symbol namespace and are intentionally absent from the generated header.

For example:

```raz
@repr(C)
public struct RazPoint {
    i32 x;
    i32 y;
}

@abi(C)
public fn raz_add(u32 left, u32 right) -> u32 {
    return left + right;
}
```

produces declarations equivalent to:

```c
#include <stdint.h>

typedef struct RazPoint {
    int32_t x;
    int32_t y;
} RazPoint;

uint32_t raz_add(uint32_t left, uint32_t right);
```

An explicit C ABI on a Raz function definition also changes its native symbol spelling: `@abi(C) public fn raz_add` is emitted as the platform symbol `raz_add`, while an ordinary Raz function continues to use the protected internal `__raz_fn_*` namespace. `@link_name` remains the compiler-level explicit symbol override; the current `c-header` source generator intentionally skips multi-attribute export spellings that it cannot represent unambiguously.

The generated header uses `<stdint.h>`/`<stddef.h>` fixed-width C types so its data model is stable across Windows and Unix hosts. The compiler qualifies headers with a C11 parser when a host C compiler is available and then feeds the header back through `raz bindgen` + `raz check` as a bidirectional ABI round-trip.

## Supported declarations

The current production bindgen handles the common C ABI surface used by system libraries:

- scalar integer and floating-point C types;
- `void` and pointer types, including pointer-level constness;
- fixed-size array fields;
- named `struct` declarations as `@repr(C)` Raz structs;
- named `enum` declarations with explicit discriminants;
- named `union` declarations as ABI-preserving aligned raw-storage carriers;
- primitive, pointer, and function-pointer `typedef` declarations, resolved internally when later declarations use the alias;
- object-like integer `#define` constants, including common C integer suffixes (`u`, `l`, `ul`, and case variants) and mask/shift expressions;
- ordinary function prototypes as `@abi(C) extern fn` declarations;
- callback typedefs and inline function-pointer parameters as Raz callable types;
- common same-base integral bitfield runs as explicit opaque storage units that preserve allocation-unit size/alignment without pretending individual Raz fields exist;
- bounded conditional preprocessing for `#if 0/1`, `#ifdef`, `#ifndef`, `#else`, `#endif`, and `defined(NAME)` / `!defined(NAME)` using object-like macros already seen in the header;
- inline anonymous `struct`/`union` fields when their member layout can be proven from supported scalar/array declarations; these lower to named opaque storage fields with the correct size/alignment rather than invented nested-member semantics.

For example, this C header:

```c
typedef unsigned int raz_u32;
typedef const char *raz_cstr;
typedef void (*raz_callback)(int code, const char *message);

struct RazPoint {
    int x;
    int y;
};

raz_u32 raz_add(raz_u32 left, raz_u32 right);
raz_cstr raz_name(void);
```

produces declarations equivalent to:

```raz
@repr(C)
public struct RazPoint {
    i32 x;
    i32 y;
}

@abi(C)
extern fn raz_add(u32 left, u32 right) -> u32;

@abi(C)
extern fn raz_name() -> i8*const;
```

The callback alias is substituted wherever it is used, so generated declarations can use `fn(i32, i8*const)` directly without depending on a separate Raz type-alias feature.

## Union representation

Raz does not currently expose source-level C union declarations. Bindgen therefore does not translate a C union into a normal struct with independent fields, which would be ABI-incorrect. Instead it emits an aligned `@repr(C)` raw-storage carrier sized for the union's largest known member.

This preserves size and alignment at the C boundary while making the representation explicit. Typed accessors can be written manually around the generated storage when required.

## Bitfield representation

For common runs such as adjacent `unsigned int` bitfields, bindgen emits one opaque integer storage unit (for example `u32 __raz_bitfield_storage_0;`) for each C allocation unit. This preserves the aggregate ABI footprint while intentionally not exposing C bitfield access semantics in Raz. Zero-width, mixed-base, oversized, and implementation-specific bitfield layouts remain outside the supported subset.

## Conditional preprocessing

Bindgen evaluates only a deterministic, header-local conditional subset. Object-like macros defined earlier in the same header participate in `#ifdef`, `#ifndef`, and `defined(NAME)` checks; `#if 0` and `#if 1` are supported directly. Inactive branches are discarded before declaration parsing. The LSP/compiler does not invoke an external preprocessor and does not inherit ambient compiler command-line macros. Unsupported `#if` expressions fail generation instead of silently choosing a branch.

## Anonymous aggregate representation

Inline anonymous aggregates such as `struct { int x; int y; } point;` and equivalent inline unions are preserved as opaque fields whose primitive carrier width gives the same proven size and alignment. The field slot/name remains stable, but nested C member access is intentionally not synthesized in Raz. Multiline/recursive anonymous aggregates that require a richer declarator graph remain deferred.

## Deliberately unsupported C surface

Bindgen rejects or skips constructs that cannot yet be represented safely instead of guessing ABI behavior. The current unsupported set includes:

- bitfield layouts outside the supported same-base integral allocation-unit subset;
- variadic function prototypes;
- multiline, recursive, or otherwise complex anonymous/nested aggregate declarators beyond the supported inline opaque-carrier form;
- complex macro expansion and function-like macros;
- conditional preprocessing expressions beyond the bounded header-local subset, including arithmetic/comparison macro evaluation and environment/compiler-defined macro sets;
- compiler-specific vector/extended scalar types not covered by the selected ABI model;
- packed/layout pragmas that cannot be expressed faithfully by the current Raz layout attributes.

Headers that rely heavily on those constructs should currently be wrapped by a small stable C shim whose public header stays within the supported subset.

The current `c-header` exporter is likewise conservative: it covers single-file or package/directory generation, fixed-width scalar/pointer/function signatures, fixed array fields, named `@repr(C)` structs/enums, callback/function-pointer parameters, and default source-name C ABI exports. Generic functions, packed/nonstandard layout attributes, anonymous aggregate synthesis, and complex stacked export attributes remain deferred rather than being emitted with guessed C syntax.

## ABI ownership

Bindgen generates declarations only. It does not link libraries, copy headers, or change native linker policy. Library selection remains a normal Raz build configuration concern, and the generated declarations use Raz's permanent C ABI/runtime boundary.

Generated output should always be validated with:

```text
raz check bindings.rz
```

The compiler repository runs this round-trip automatically for its bindgen qualification fixtures on both supported ABI models.
