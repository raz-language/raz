# Command-line reference

Forge installs six command-line tools.

## `forge`

Primary driver for verification, optimization, and native object compilation.

```sh
forge verify input.fir
forge compile input.fir -O2 --format=elf -o output.o
forge compile input.fir -O2 --arch=aarch64 --format=elf -o output-aarch64.o
```

## `forge-opt`

Runs the optimization pipeline and can report pass statistics and timings.

```sh
forge-opt input.fir -O3 --stats --pass-timing
```

## `forge-as` and `forge-dis`

Convert between textual and binary Forge IR.

```sh
forge-as input.fir -o module.fbc
forge-dis module.fbc -o module.fir
```

## `forge-codegen`

Displays machine lowering, allocation, encoder statistics, and code-quality metrics. `--arch=aarch64` selects the correctness-first AArch64 encoder; physical-allocation reporting is currently x86-64-only.

## `forge-run`

Executes a function with the interpreter, JIT, or differential engine.

```sh
forge-run --engine=compare input.fir function_name 1 2
```

## Optimization levels

- `-O0`: minimal transformation
- `-O1`: low-cost cleanup
- `-O2`: standard production optimization
- `-O3`: aggressive optimization
- `-Os`: optimize for size
- `-Oz`: minimize code size

Use each tool's `--help` output as the authoritative option list for the installed version.

## Static archives

```sh
forge archive create -o libmath.a math.o helpers.o
```

Forge extracts ELF64 or COFF AMD64 symbols and writes a deterministic native archive index.

## Shared libraries

```sh
forge link-shared --linker=c++ -o libmath.so math.o helpers.o
```

Pass additional arguments with `--link-arg=<argument>`.
