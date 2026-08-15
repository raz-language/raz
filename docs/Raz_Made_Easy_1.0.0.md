# Raz Made Easy

**Raz 1.0.0 - A practical guide to the Raz systems programming language**

Language design by **Mario Vinciguerra**.

Raz is a statically typed native systems programming language built for predictable performance, deterministic resource management, ownership safety, and direct control over native software. Raz 1.0 lowers source through typed HIR and backend-neutral MIR, then dispatches to either Forge, the default bundled native backend, or the Raz-written LLVM IR backend. Forge emits native objects in-process; the LLVM backend emits textual LLVM IR in Raz and uses an external Clang/LLVM toolchain when native objects or executables are requested.

This guide replaces the earlier alpha-era edition and reflects the current Raz 1.0 compiler, syntax, package model, standard library, Forge and LLVM backends, ownership system, compile-time facilities, async model, and tooling.

---

## Chapter 1 - What Raz Is

Raz is designed for software where native performance and explicit behavior matter: compilers, runtimes, servers, storage engines, networking software, command-line tools, game/runtime infrastructure, databases, blockchain nodes, and other latency- or throughput-sensitive applications.

The language combines familiar systems-programming control with a modern semantic model:

| Area | Raz 1.0 |
| --- | --- |
| Execution | Native machine code through Forge or LLVM/Clang |
| Memory model | Ownership, moves, borrows, deterministic drop |
| Managed runtime | No mandatory tracing garbage collector |
| Abstraction | Generics, traits, associated items, closures, iterators |
| Data modeling | Structs, tuples, arrays, slices, payload enums, `match` |
| Compile time | Constants, const functions/generics, `comptime`, reflection facilities |
| Concurrency | Threads, atomics, synchronization, tasks/futures, channels |
| Async | `async fn`, `spawn`, `await`, reactor and async I/O foundations |
| Native boundary | Explicit `extern`, raw pointers, unsafe operations |
| Project model | `raz.toml`, path/registry dependencies, lockfiles, tests, formatting, and docs |

Raz aims to make low-level code easy to reason about without hiding the cost model behind a large managed runtime.

### A language first, not a backend demo

Raz was also built as a serious real-world frontend for **[Forge](https://github.com/Ascension-Digital-Technologies/Forge)**, a compact native compiler infrastructure project. Forge remains the default bundled backend and is linked in-process. Raz also includes a first-class LLVM IR emitter written in Raz itself, allowing the same frontend and MIR semantics to target the wider LLVM ecosystem without bundling LLVM's C++ source tree.

Both paths produce native code. Performance depends on Raz's semantic lowering, the selected backend and optimization pipeline, the standard library/runtime boundary, and the target machine. Forge keeps a compact in-process integration surface; LLVM mode emits LLVM IR internally and delegates native code generation to an installed external Clang/LLVM toolchain.

---

## Chapter 2 - Create and Build a Project

Raz projects use a lowercase `raz.toml` manifest and `.rz` source files.

```text
hello/
|-- raz.toml
`-- src/
    `-- main.rz
```

A minimal manifest looks like this:

```toml
[package]
name = "hello"
version = "1.0.0"
kind = "executable"
source = "src"
entry = "src/main.rz"

[dependencies]
```

The normal command-line workflow is:

```text
raz check
raz build
raz run
raz test
raz fmt
raz lint
raz doc
```

Useful project commands include:

| Command | Purpose |
| --- | --- |
| `raz new NAME` | Create a new package |
| `raz init` | Initialize a package in the current directory |
| `raz check` | Parse, resolve, type-check, and validate |
| `raz build` | Build a native artifact |
| `raz run` | Lower and run an executable program |
| `raz test` | Discover and run `test_` functions |
| `raz fmt` | Format Raz source |
| `raz lint` | Run semantic validation |
| `raz doc` | Generate API documentation |
| `raz add` / `raz remove` | Change package dependencies |
| `raz update` | Re-resolve tracked registry constraints |
| `raz graph` | Show the dependency graph |
| `raz metadata` | Print package metadata |
| `raz pack` | Create a deterministic `.dpk` package archive |
| `raz publish` | Prepare a submission for the official registry, or publish to an explicit private registry |
| `raz lock` | Regenerate `raz.lock` |
| `raz registry` | Inspect registry resolution |
| `raz doctor` | Inspect the local toolchain environment |
| `raz backends` / `raz targets` | Inspect compiler backend support |

`razc` is the lower-level compiler frontend used for direct compiler and conformance workflows.

---

## Chapter 3 - Your First Program

Raz uses **type-first declarations**. Statements end in semicolons and blocks use braces.

```raz
fn main() -> i64 {
    string message = "Hello from Raz";
    i64 answer = 40 + 2;
    print(message);
    return answer - 42;
}
```

A function declaration places each parameter type before its name:

```raz
public fn add(i64 left, i64 right) -> i64 {
    return left + right;
}
```

A function with no return value can omit the arrow result where accepted by the stable grammar:

```raz
fn increment(i64& mut value) {
    *value += 1;
}
```

Comments use familiar syntax:

```raz
// A line comment.

/* A block comment can
   span multiple lines. */

/// Documentation comments are attached to public API declarations.
```

---

## Chapter 4 - Variables and Types

Ordinary local bindings do not require `let` or `var`. The type comes first:

```raz
i64 count = 42;
bool ready = true;
f64 ratio = 0.5;
string label = "Raz";
```

The stable scalar families include:

| Family | Types |
| --- | --- |
| Signed integers | `i8`, `i16`, `i32`, `i64` |
| Unsigned integers | `u8`, `u16`, `u32`, `u64`, `usize` |
| Floating point | `f32`, `f64` |
| Logic | `bool` |
| Character | `char` |
| Built-in text value | `string` |

Explicit conversions use `as` when the compiler requires a conversion:

```raz
i64 count = 42;
f64 precise = count as f64;
```

Raz keeps numeric coercions in the frontend. The backend receives already-typed operations rather than guessing source-language conversion rules.

---

## Chapter 5 - Arrays, Slices, References, and Pointers

Fixed arrays use a type-first declarator:

```raz
i64 values[4] = [10, 20, 30, 40];
i64 first = values[0];
```

A shared reference is written `T&`; a mutable reference is `T& mut`:

```raz
fn read(i64& value) -> i64 {
    return *value;
}

fn increment(i64& mut value) {
    *value += 1;
}
```

Borrow expressions use `&` and `&mut`:

```raz
i64 value = 41;
i64& view = &value;
i64& mut edit = &mut value;
```

Slices provide a borrowed view over contiguous data. Fixed arrays and slices remain bounds-aware on safe indexing paths.

Raw pointer types are explicit, for example `i8*mut` and `i8*const`. Raw pointer operations belong at the language's unsafe/native boundary rather than silently appearing in safe code.

---

## Chapter 6 - Expressions and Control Flow

Raz provides the usual arithmetic, comparison, logical, bitwise, shift, and assignment operators.

```raz
i64 subtotal = 20 * 3;
bool affordable = subtotal <= 100;

if (affordable) {
    print("approved");
} else {
    print("declined");
}
```

Loops are structured and explicit:

```raz
i64 count = 0;
while (count < 5) {
    count += 1;
}
```

`for` works with arrays, ranges, borrowed arrays, and types that participate in the iterator contracts:

```raz
i64 total = 0;
for value in 0..4 {
    total += value;
}

for value in 0..=4 {
    total += value;
}
```

`break`, `continue`, and `return` have ordinary structured-control semantics.

---

## Chapter 7 - Structs and Methods

Struct fields are type-first:

```raz
struct Point {
    i64 x;
    i64 y;
}
```

Construct values with the type name:

```raz
Point point = Point(20, 22);
```

Inherent methods live in `impl` blocks:

```raz
impl Point {
    fn sum(Point& self) -> i64 {
        return self.x + self.y;
    }

    fn translate(Point& mut self, i64 dx, i64 dy) {
        self.x += dx;
        self.y += dy;
    }
}
```

The receiver type makes ownership and mutation permission explicit rather than relying on an invisible implicit receiver mode.

---

## Chapter 8 - Enums and Exhaustive Match

Enums may contain unit variants or payloads:

```raz
enum Message {
    Quit,
    Number(i64),
    Pair(i64, bool),
}
```

Construct a payload variant by naming it:

```raz
Message message = Message.Number(42);
```

`match` can bind payload values and is checked for exhaustiveness over supported enum patterns:

```raz
fn evaluate(Message message) -> i64 {
    match message {
        Message.Quit => {
            return 0;
        },
        Message.Number(value) => {
            return value;
        },
        Message.Pair(value, enabled) => {
            if (enabled) {
                return value;
            }
            return 0;
        },
    }
}
```

A wildcard arm can cover remaining supported cases when appropriate.

---

## Chapter 9 - Option, Result, and `?`

Absence and recoverable errors can be represented as ordinary typed enum values. A simplified pair of definitions looks like this:

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

Postfix `?` propagates the non-success path and yields the success payload on the success path:

```raz
fn produce(bool fail) -> Result<i64, i64> {
    if (fail) {
        return Result<i64, i64>.Error(9);
    }
    return Result<i64, i64>.Ok(42);
}

fn relay(bool fail) -> Result<i64, i64> {
    i64 value = produce(fail)?;
    return Result<i64, i64>.Ok(value + 1);
}
```

This keeps recoverable failure visible in the function type rather than making exceptions the primary error model.

---

## Chapter 10 - Ownership and Moves

Owned values have a clear lifetime and owner. A move transfers that ownership:

```raz
struct Resource {
    i64 handle;
}

fn consume() -> i64 {
    Resource first = Resource(42);
    Resource second = move first;
    return second.handle;
}
```

After a non-copy value is moved, using the old source is rejected. The compiler also tracks partial moves, field moves, and control-flow-sensitive ownership states.

The important model is simple:

- one active owner is responsible for an owned resource;
- `move` transfers ownership;
- `T&` borrows shared access;
- `T& mut` borrows exclusive mutable access;
- reborrows cannot outlive the source borrow; and
- references cannot escape a lifetime the source value cannot satisfy.

---

## Chapter 11 - Borrowing and Lifetimes

Shared and mutable borrows are checked for conflicts:

```raz
fn increment_twice(i64& mut input) -> i64 {
    {
        i64& mut child = &mut*input;
        *child += 1;
    }

    *input += 1;
    return *input;
}
```

Raz performs non-lexical lifetime analysis on supported borrow patterns, so a borrow can end after its last use rather than always lasting until the closing brace.

The compiler rejects, among other cases:

- use after move;
- two overlapping mutable borrows;
- mutation through a shared borrow;
- whole-object access that conflicts with a field borrow;
- invalid reference return/escape;
- borrowed captures that outlive their source; and
- references that cannot safely cross an async suspension.

---

## Chapter 12 - `Drop` and `defer`

Raz provides deterministic cleanup without requiring a tracing garbage collector.

A resource type can implement `Drop`:

```raz
struct Resource {
    i64 handle;
}

impl Drop for Resource {
    fn drop(Resource& mut self) {
        // Release the resource represented by self.handle.
        self;
    }
}
```

The compiler elaborates destruction into MIR according to ownership and control flow.

`defer` schedules work for lexical scope exit in LIFO order:

```raz
fn calculate(i64 input) -> i64 {
    i64 value = input;
    defer value += 1;

    {
        defer value *= 2;
        value += 3;
    }

    return value;
}
```

`Drop` is appropriate for type-owned cleanup. `defer` is useful for local scope cleanup and paired operations.

---

## Chapter 13 - Generics

Raz generic functions and data types are statically specialized where static dispatch is used.

```raz
fn identity<T>(T value) -> T {
    return value;
}

struct Pair<T, U> {
    T first;
    U second;
}
```

Const generics can place compile-time values in a type:

```raz
struct Buffer<T, const usize N> {
    T values[N];
}
```

Generic static dispatch is monomorphized into concrete code. This keeps the abstraction in the source language without requiring a universal boxed runtime representation.

---

## Chapter 14 - Traits and Associated Items

Traits define reusable semantic contracts:

```raz
trait Measurable {
    fn measure(Self& self) -> i64;
}

struct Metric {
    i64 value;
}

impl Measurable for Metric {
    fn measure(Metric& self) -> i64 {
        return self.value;
    }
}

fn read<T: Measurable>(T& value) -> i64 {
    return value.measure();
}
```

Traits may define associated types and constants:

```raz
trait Storage {
    type Item;
    const WIDTH: usize;
    fn get(Self& self, i64 index) -> i64;
}
```

Raz 1.0 includes generic implementations, multiple bounds, supertraits, default methods, coherence checking, associated items, and static trait dispatch.

Object-safe traits can also be used dynamically when dynamic dispatch is explicitly requested:

```raz
fn use_render(dyn Render object) -> i64 {
    return object.render();
}
```

---

## Chapter 15 - Iterators and Ranges

Raz's `for` syntax is not limited to built-in arrays. Custom iterator types can implement the iterator contracts:

```raz
trait Iterator {
    type Item;
    fn next(Self& mut self) -> bool;
    fn current(Self& self) -> Item;
}

struct Counter {
    i64 current_value;
    i64 end;
}

impl Iterator for Counter {
    type Item = i64;

    fn next(Counter& mut self) -> bool {
        if (self.current_value < self.end) {
            self.current_value += 1;
            return true;
        }
        return false;
    }

    fn current(Counter& self) -> i64 {
        return self.current_value - 1;
    }
}
```

`IntoIterator` allows a container to define the iterator created for a `for` loop. Temporary iterable cleanup follows the same ownership/drop rules as other values.

---

## Chapter 16 - Compile-Time Programming

Compile-time execution is deterministic and bounded.

```raz
const i64 BASE = 40 + 2;
const i64 MASK = (1 << 4) - 1;

comptime {
    assert(BASE == 42);
    assert(MASK == 15);
}
```

The stable compile-time surface includes constants, const functions, const generics, compile-time assertions/loops, selected reflection and layout facilities, and supported derive operations.

The compiler intentionally rejects compile-time behavior that would make builds depend on ambient nondeterministic process state.

---

## Chapter 17 - Closures and Callable Values

Raz closures participate in the ownership system. Capture mode communicates how the environment is used.

Shared capture:

```raz
i64 read_value = 40;
auto read = ref fn() -> i64 {
    return *read_value;
};
```

Mutable capture:

```raz
i64 mutable_value = 0;
auto increment = mut fn() -> i64 {
    *mutable_value += 1;
    return *mutable_value;
};
```

Move capture:

```raz
Resource resource = Resource(1);
auto consume = move fn() -> i64 {
    return resource.handle;
};
```

The callable model distinguishes shared/reusable callables, mutable callables, and once-only owned callables. Non-capturing closures can participate in compatible function-pointer workflows.

---

## Chapter 18 - Async, Tasks, and `await`

Structured async syntax is part of Raz 1.0:

```raz
async fn compute(i64 value) -> i64 {
    return value + 1;
}

async fn pipeline() -> i64 {
    i64 child = spawn compute(41);
    i64 ready = await child;
    return ready;
}
```

The compiler lowers async state into compiler-managed frames and tracks values live across suspension points. Ownership rules still apply: an owned value can move into an async frame, while an unsafe reference escape across `await` is rejected.

The standard library/runtime provides futures, tasks, timers, cancellation, worker execution, readiness-reactor support, and async socket/filesystem foundations. Raz does not force one application scheduler design onto every program.

---

## Chapter 19 - Modules, Packages, and Dependencies

Imports use `::` module paths:

```raz
import util::math;
```

A package can contain multiple source files:

```text
src/
|-- main.rz
`-- util/
    `-- math.rz
```

Path dependencies are declared in `raz.toml` and loaded recursively. Project construction is deterministic; cycles are rejected and repeated diamond dependencies are deduplicated.

Most packages use deterministic source discovery. Packages such as the compiler that require one specific physical concatenation order can provide `source-order.txt`. That metadata keeps build ordering separate from filenames.

`raz.lock` records the resolved package graph and manifest fingerprints deterministically. Registry dependencies retain their semantic-version constraints separately, while the project manifest points at the verified package-store entry selected for the current resolution.

Official registry packages resolve from the GitHub-backed [`raz-language/packages`](https://github.com/raz-language/packages) repository, with local snapshots, private HTTP/HTTPS registries, and mirror fallback still supported. Registry packages are materialized into a shared content-addressed store. Set `RAZ_PACKAGE_STORE` or `RAZ_HOME` to choose the store location, or `RAZ_OFFLINE=1` to require a cached registry snapshot and an already-present, integrity-valid store entry. `raz add foo` and `raz add foo@^1.2.0` use the official registry. `raz pack` creates deterministic `.dpk` archives; ordinary `raz publish` prepares a `.raz-publish/` submission for the GitHub registry, while explicit private registries retain filesystem or HTTP/HTTPS publishing.

---

## Chapter 20 - Standard Library

Raz's production standard library is written in Raz and divided into three layers:

```text
library/core   allocator-free language/runtime foundations
library/alloc  owned heap types and allocation-backed containers
library/std    hosted OS, I/O, concurrency, async, and networking APIs
```

### Core

`core` includes option/result foundations, pointer helpers, slices, iterators, callable/trait-object support, atomics, ABI helpers, hardware queries, SIMD hooks, and bulk memory operations.

### Alloc

`alloc` includes `Box`, owned `String`, and `RawVec` foundations. Raz 1.0 uses geometric capacity growth for strings/vectors so repeated small growth is amortized rather than reallocating on each append-sized request. Allocation-size arithmetic is checked before calls into the allocator so very large requests fail instead of silently wrapping.

### Std

`std` includes environment, filesystem, process, I/O, time, threads, synchronization, tasks/futures, cancellation, channels, timers, networking, readiness-reactor support, and async socket/filesystem foundations.

The design rule is important: ordinary algorithms should stay in Raz. Native code under `src/runtime` exists for real OS, ABI, atomic, SIMD, allocation, and bulk-memory boundaries rather than as a second hidden standard library.

---

## Chapter 21 - Concurrency and Synchronization

Raz 1.0 exposes low-level primitives suitable for building higher-level concurrency models:

- ordered atomics and fences;
- mutexes and reader/writer locks;
- condition variables;
- semaphores, barriers, and latches;
- one-time initialization;
- threads and hardware-thread discovery;
- bounded MPMC channels;
- futures, tasks, worker execution, timers; and
- cooperative cancellation.

The standard library stays close enough to host primitives that servers and runtimes can build application-specific scheduling and batching rather than paying for one mandatory global runtime.

---

## Chapter 22 - Networking and I/O

Networking foundations include TCP, UDP, DNS, nonblocking mode, endpoint inspection, timeouts, socket buffer tuning, exact reads, send-all operations, and readiness-driven async building blocks.

The performance rule for I/O-heavy software is to avoid unnecessary crossings and allocations. Raz 1.0 exposes low-level primitives as stable building blocks; buffering, vectored/scatter-gather I/O, buffer pools, framed byte builders, and specialized event-loop policy belong in Raz library layers rather than hidden native shims.

For filesystem workloads, synchronous and asynchronous foundations are both present. Application code should pick the model that matches its workload instead of assuming async is always faster.

---

## Chapter 23 - Unsafe Code and Native Interop

Raz keeps native boundaries explicit. `extern` declarations expose functions implemented by the host runtime or a linked native library.

```raz
extern fn raz_rt_hardware_threads() -> i64;

fn main() -> i64 {
    i64 threads = raz_rt_hardware_threads();
    return threads - threads;
}
```

Raw pointers, pointer casts/dereferences, and native calls that require additional trust belong in the supported unsafe forms.

A good systems-programming rule applies: make the unsafe boundary small, document the invariant, validate at the edge, and return to ordinary typed Raz code immediately.

---

## Chapter 24 - The Compiler Pipeline

Raz owns source-language semantics through MIR:

```text
Raz source
    |
    v
Lexer / parser
    |
    v
Semantic analysis + ownership + traits + generics
    |
    v
Typed HIR
    |
    v
MIR + cleanup/drop elaboration
    |
    +---------------------------+
    |                           |
    v                           v
Forge backend               LLVM IR emitter
(bundled C++ library)       (written in Raz)
    |                           |
    v                           v
Forge optimize/lower        Textual LLVM IR
    |                           |
    v                           v
Native object              External clang/clang++
    |                           |
    +-------------+-------------+
                  |
                  v
        Native executable/library
```

This boundary is deliberate. Raz owns what a Raz program *means* through backend-neutral MIR. Forge owns its verified IR, optimization, ABI lowering, register allocation, encoding, JIT infrastructure, and deterministic native object production. The Raz LLVM backend owns LLVM IR construction and target/toolchain orchestration while leaving LLVM optimization and machine-code generation to the external LLVM/Clang installation.

Forge was created to give language frontends a compact native backend surface instead of requiring every frontend to integrate a very large compiler framework. Raz remains a substantial real frontend for that design, while the LLVM path provides an alternate production backend without duplicating language semantics.

There is no C emitter. Forge and LLVM are explicit backends; LLVM never silently falls back to Forge, and "LLVM IR emitter: built-in" means the emitter is part of the Raz compiler - not that LLVM itself is vendored into the repository.

---

## Chapter 25 - Native Performance

Raz performance comes from the complete pipeline rather than one marketing claim.

### Language-level choices

- no mandatory tracing GC for ordinary owned values;
- deterministic cleanup;
- monomorphized static generics/traits;
- explicit dynamic dispatch;
- borrowed slices/references that avoid ownership changes;
- ownership-aware closure environments; and
- compiler-lowered async state machines.

### Library-level choices

- geometric capacity growth for owned strings and vector foundations;
- direct bulk copy/move/fill at the native memory boundary;
- low-level atomics and synchronization;
- nonblocking/reactor primitives; and
- hardware/SIMD capability hooks.

### Backend-level choices

Forge can improve scalar optimization, alias analysis, memory promotion, instruction selection, register allocation, block layout, vectorization opportunities, and target support independently of Raz syntax.

When benchmarking, record the target machine, optimization level, generated IR/object size, allocation behavior, throughput/latency, and correctness result. Optimize general mechanisms rather than special-casing one benchmark shape.

---

## Chapter 26 - Diagnostics, Formatting, and Testing

Compiler diagnostics are part of the language experience. Raz tracks source locations through the frontend and exposes stable diagnostic behavior through the conformance suite.

The repository formatter is deterministic and idempotent:

```text
raz fmt
raz fmt --check
```

Tests are ordinary Raz programs under `tests/`:

```raz
fn main() -> i64 {
    i64 value = 40;
    value += 2;
    assert(value == 42);
    return 0;
}
```

The project toolchain also provides backend/target discovery, compiler query profiling, package metadata and graph inspection, deterministic dependency locking, registry resolution, formatting, testing, and API documentation generation.

---

## Chapter 27 - A Complete Small Program

The following example combines data modeling, traits, generics, borrowing, matching, and deterministic values without requiring a managed runtime.

```raz
trait Score {
    fn score(Self& self) -> i64;
}

struct Reading {
    i64 left;
    i64 right;
}

impl Score for Reading {
    fn score(Reading& self) -> i64 {
        return self.left + self.right;
    }
}

enum Check<T> {
    Ok(T),
    Failed(i64),
}

fn validate<T: Score>(T& value) -> Check<i64> {
    i64 result = value.score();
    if (result == 42) {
        return Check<i64>.Ok(result);
    }
    return Check<i64>.Failed(result);
}

fn main() -> i64 {
    Reading reading = Reading(20, 22);

    match validate<Reading>(&reading) {
        Check<i64>.Ok(value) => {
            return value - 42;
        },
        Check<i64>.Failed(value) => {
            return value;
        },
    }
}
```

---

## Chapter 28 - Quick Reference

| Need | Raz syntax |
| --- | --- |
| Local | `i64 value = 42;` |
| Function | `fn add(i64 a, i64 b) -> i64 { ... }` |
| Public API | `public fn read(...) -> i64 { ... }` |
| Struct field | `i64 value;` |
| Fixed array | `i64 values[4] = [1, 2, 3, 4];` |
| Shared reference type | `T&` |
| Mutable reference type | `T& mut` |
| Shared borrow | `&value` |
| Mutable borrow | `&mut value` |
| Dereference | `*value` |
| Move | `move value` |
| Generic function | `fn read<T>(T value) -> T` |
| Trait bound | `fn read<T: Trait>(T& value)` |
| Dynamic trait | `dyn Trait object` |
| Range | `0..10` |
| Inclusive range | `0..=10` |
| Compile time | `comptime { ... }` |
| Async function | `async fn work() -> i64` |
| Spawn | `spawn work()` |
| Await | `await task` |
| Deferred action | `defer cleanup();` |
| Import | `import util::math;` |
| External function | `extern fn native_call(...) -> i64;` |

---

## Chapter 29 - Where to Go Next

A productive learning path is:

1. Build a one-file command-line program.
2. Add a struct and an inherent `impl` block.
3. Use shared and mutable references to work with values without moving them.
4. Introduce an enum and exhaustive `match`.
5. Add a generic function and a trait implementation.
6. Use `Drop` or `defer` for a real resource.
7. Split the project into modules.
8. Add tests and a benchmark.
9. Explore channels/tasks/networking if the program needs concurrency.
10. Use unsafe/native interop only where the host boundary truly requires it.

Raz 1.0 is intended to make native systems programming direct: explicit where the machine matters, safe by default where the compiler can prove the rules, and small enough architecturally that the full source-to-machine-code path remains understandable.

---

## Credits

**Raz language design:** Mario Vinciguerra.

**Forge backend:** [Ascension Digital Technologies / Forge](https://github.com/Ascension-Digital-Technologies/Forge). Forge is a separate project and is vendored under its own Apache-2.0 license when included with Raz.

**LLVM backend:** the LLVM IR emitter and orchestration layer are implemented in Raz under `compiler/src/backend/llvm/`. LLVM itself is not vendored; native LLVM-mode object and executable emission requires an external Clang/LLVM toolchain.

Raz 1.0 was built in part to prove Forge as a practical native alternative for language frontend developers who want verified IR, optimization, machine lowering, and native object generation without taking on LLVM's integration scale. The LLVM path complements that design with access to LLVM targets and optimization infrastructure. Raz remains a native systems language, and real performance is an end-to-end responsibility shared by frontend lowering, the selected backend, the runtime, and the standard library.
