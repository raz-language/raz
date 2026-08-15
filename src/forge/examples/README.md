# Forge examples

The examples are small verified Forge IR modules used for documentation, regression tests, code-quality baselines, ABI validation, and interpreter/JIT differential execution.

Start with:

- `basic.fir` — introductory syntax and module structure
- `interpreter.fir` — reference execution examples
- `native-i64.fir` — native integer code generation
- `calls.fir` — direct calls
- `abi.fir` and `abi-hardening.fir` — calling-convention coverage
- `optimization.fir` — spill, rematerialization, and code-quality baseline

Verify an example:

```sh
forge verify examples/native-i64.fir
```

Run through both execution engines and compare results:

```sh
forge-run --engine=compare examples/interpreter.fir factorial 10
```


## Frontend examples

- `frontend/minilang/` — complete educational language frontend from source text through JIT execution
- `frontend/tiny_frontend.cpp` — minimal IRBuilder example
- `frontend/template/` — standalone CMake consumer template
