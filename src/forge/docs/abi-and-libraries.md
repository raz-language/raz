# Native ABI classification and libraries

Forge exposes frontend-visible ABI classification and native library workflows for x86-64 targets.

## Aggregate classification

Include:

```cpp
#include <forge/platform/abi.hpp>
```

Classify a named aggregate:

```cpp
const auto classification = forge::target::classify_aggregate(
    module,
    forge::ir::AggregateRefKind::structure,
    "Pair",
    forge::target::NativeAbi::system_v_x86_64
);
```

System V classifications use up to two eightbyte classes:

- `integer`
- `sse`
- `memory`

Windows x64 directly passes aggregates of 1, 2, 4, or 8 bytes in an integer register. Other aggregate sizes are classified as indirect.

Function-level classification reports parameter classifications, integer and floating register usage, stack bytes, and variadic state.

```cpp
const auto abi = forge::target::classify_function(
    module,
    function,
    forge::target::NativeAbi::windows_x64
);
```

## Function metadata

Forge IR accepts calling-convention and symbol metadata:

```text
variadic extern hidden c func @printf(%format: ptr) -> i32
internal fast func @helper(%value: i64) -> i64 {
entry:
  return %value
}
weak global @counter: i64 = 0
```

Supported calling-convention keywords are:

- `c`
- `systemv`
- `win64`
- `fast`

Supported symbol modifiers are:

- `internal`
- `weak`
- `hidden`

Metadata survives canonical printing and binary IR serialization. The current x86-64 object pipeline uses one target ABI per emitted module; per-function mixed-ABI native lowering remains future work.

## Static libraries

Create objects and a deterministic archive:

```sh
forge compile math.fir --format=elf -o math.o
forge archive create -o libmath.a math.o
```

The archive writer:

- Extracts defined symbols from ELF64 and COFF AMD64 objects
- Emits a native linker symbol index
- Supports long member names
- Uses deterministic timestamps, ownership, permissions, and ordering

The same archive format can contain COFF objects and use a `.lib` extension with compatible Windows toolchains.

## Shared libraries

Forge can orchestrate the host linker:

```sh
forge compile math.fir --format=elf -o math.o
forge link-shared --linker=c++ -o libmath.so math.o
```

Additional linker arguments use `--link-arg=`:

```sh
forge link-shared --linker=clang++ --link-arg=-Wl,--no-undefined \
  -o libmath.so math.o
```

On Windows, use a Clang-compatible linker driver and a `.dll` output path.

## Current aggregate boundary

Forge preserves a pointer-oriented internal aggregate representation while explicit native calling conventions lower eligible aggregate parameters and returns through System V AMD64 or Windows x64 register classes. Larger or ABI-indirect aggregates use hidden result storage.
