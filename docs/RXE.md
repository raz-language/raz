# RXE Backend

RXE (Raz Executable) is Raz's deterministic bytecode target. It consumes verified MIR and is implemented in Raz under `compiler/src/raz_codegen_rxe/src/rxe/`. RXE is independent from Forge and LLVM lowering and does not redefine source-language type, ownership, or lifetime semantics.

## Execution contract

RXE format v7 and ISA v1 define the portable executable contract.

- `--backend=rxe` selects RXE.
- Canonical instructions are 8 bytes: `opcode:u8 dst:u8 src0:u8 src1:u8 imm:u32`, little-endian.
- RXE exposes 32 architectural registers.
- MIR SSA values have deterministic frame slots as the correctness baseline.
- CFG-aware linear-scan allocation promotes proven value intervals into `r6-r31`; values that cannot be proven safe remain frame-backed.
- The executable begins with a fixed 104-byte header and uses deterministic section ordering.
- No timestamps, pointer-derived identifiers, randomized ordering, or native ABI addresses enter the file.
- Unsupported MIR capabilities are explicit errors. RXE never falls back to another backend.

## Compiler pipeline

```text
Raz source
  -> HIR
  -> verified MIR
  -> RXE lowering
  -> CFG and liveness analysis
  -> register allocation and conservative spill promotion
  -> machine canonicalization
  -> RXE verifier
  -> deterministic writer
  -> independent decode and canonical re-encode verification
  -> .rxe
```

MIR is the semantic boundary. RXE receives already-decided ownership cleanup, aggregate shape, call signatures, globals, and reference semantics.

## ISA coverage

RXE lowering covers:

- constants, parameters, locals, and deterministic MIR value slots;
- integer add, subtract, multiply, divide, remainder, bitwise operations, and shifts;
- explicit 8/16/32-bit truncation and sign extension;
- signed and unsigned integer comparisons and arithmetic semantics;
- boolean operations and select;
- unconditional and conditional control flow;
- direct calls with explicit argument staging;
- first-class function references and indirect calls;
- escaping closure construction and closure calls with explicit capture staging;
- local and global references, index references, loads, and stores;
- indexed aggregate and fixed-array memory operations;
- deterministic allocation and free operations;
- deterministic module globals and serialized global initialization images;
- string and global references;
- integer-target scalar casts;
- bounded slice loads and stores;
- overlap-safe aggregate memory copy;
- machine-level register forwarding, constant propagation, and conservative canonicalization.

Hardware floating-point MIR is rejected rather than inheriting host floating-point behavior. Async/future operations and native/OS capabilities are capability-gated.

RXE does not define blockchain state, accounts, gas, transaction context, consensus, validator services, or chain-specific cryptographic host calls. Those belong to execution-environment capability layers above the portable core ISA.

## Binary layout

```text
104-byte header
16-byte function records
8-byte canonical instructions
i64 constant pool
16-byte global image records
aggregate layout records
callable signature records
24-byte export records
16-byte basic-block records
```

The header records format and ISA versions, feature flags, section counts, section geometry, aggregate-layout counts, module identity, export metadata, and control-flow metadata.

Function records contain code range, frame-slot count, argument count, block count, and the architectural register count used after allocation. Global records contain the deterministic initial i64 image, flags, and zeroed reserved bytes.

## Aggregate layout

Aggregate shape is part of the executable image. Layout descriptors encode structure and enum images, field ranges, slot counts, nested structure identity, fixed-array extent, primitive type information, and reference/slice/function flags.

Enums use the same slot-image representation as MIR: slot zero is the tag and the following slots hold the largest payload image.

Aggregate allocation encodes slot count and layout identity directly in the RXE instruction so an independent loader can verify structure allocations without HIR.

Fixed arrays distinguish generic images, structures, and fixed-array allocations. Primitive arrays do not require a nested layout; arrays of structures carry the element layout while extent remains independent from element structure size.

## References and memory

The Raz-native reference executor uses tagged integer handles rather than host pointers. Heap references, globals, and closure handles therefore remain deterministic across processes and machines.

Each heap cell records its allocation owner. Bounds validation remains correct for interior `INDEX_REF` handles, and freeing an owner invalidates all derived references. Closure objects use the same deterministic handle model with a canonical capture image.

`MEM_COPY` validates complete source and destination ranges and uses overlap-safe semantics equivalent to `memmove`. Host addresses are never serialized or exposed through the bytecode contract.

Slices retain Raz's two-slot `(data_ref, length)` representation. `slice.load` and `slice.store` validate `0 <= index < length` before resolving the data reference. Raw fixed-array and aggregate indexing use the separate index operations.

## Callable ABI and identity

Direct calls use explicit `ARG` staging followed by `CALL function_index`. Indirect calls use a first-class `FUNC_REF` value with `CALL_INDIRECT`. Closures use `MAKE_CLOSURE` and `CALL_CLOSURE`; stored captures are appended after explicit arguments to match Raz closure parameter layout.

Callable signature metadata records parameter and return shape independently from HIR. References, slices, fixed arrays, nested structures, function values, and ABI metadata are encoded canonically so an independent VM can verify callable compatibility.

Exports use deterministic 64-bit selectors represented as two 32-bit lanes. A selector incorporates source-name identity, ABI kind, return shape, and canonical parameter metadata. Export lookup is ordered by selector and function index; selector collisions are invalid.

## Register allocation and liveness

Every MIR result has a stable frame-slot representation. The allocator uses the serialized control-flow graph to compute frame-slot use/def and live-in/live-out sets, then assigns safe intervals to `r6-r31`.

Values proven defined before use can remain in registers across joins and loop backedges. Values without a materializable incoming definition remain frame-backed. Loop-carried values can use stable architectural registers when their preheader definition and control-flow lifetime are unambiguous.

After physical allocation, architectural-register liveness identifies dead pure register producers. Operations that can trap, access memory, call code, allocate, or otherwise produce observable behavior are retained even when their result is unused.

Constant propagation is restricted to values representable by canonical instruction encodings. Wider values stay in the constant pool. Potentially trapping arithmetic is not folded unless semantics are preserved exactly.

Aggregate-copy fusion is proof-driven: contiguous element copies can use `MEM_COPY` only when source and destination provenance resolves to distinct allocations. Unknown provenance, merges, calls, or possible aliasing disable the optimization.

## Control-flow directory

The executable stores a canonical basic-block directory. Each 16-byte block record contains:

```text
(code_start, code_count, successor0, successor1)
```

The verifier requires blocks to partition their owning function exactly and checks successors against jump, branch, return, trap, and fallthrough behavior. Loaders can consume this verified CFG without rediscovering block structure from instruction bytes.

## Feature bitmap and module fingerprint

The module feature bitmap records executable use of calls, closures, reference and aggregate memory, globals, bounded slices, and signed integer semantics. The verifier recomputes this bitmap from the final instruction stream.

The two-lane module fingerprint covers the semantic executable image, including format and ISA version, feature flags, functions, instructions, constants, globals, aggregate layouts, callable metadata, exports, and basic-block descriptors. Mutation of executable metadata without a matching identity is rejected.

## Verification and decoding

`decoder.rz` consumes serialized RXE bytes without compiler-side type state. It validates:

- magic and version;
- exact file length;
- canonical section geometry;
- function and code partitioning;
- opcode and register bounds;
- slot and argument indexes;
- branch ownership and targets;
- direct-call arity;
- callable signatures and selectors;
- aggregate metadata;
- reserved-zero fields;
- feature bitmap consistency;
- module fingerprint consistency.

Every emitted `.rxe` is independently decoded and canonically re-encoded. Backend success requires byte-for-byte identity between the original executable and the re-encoded image.

## Reference and inspection tools

`disasm.rz` provides deterministic disassembly using canonical opcode names and operands. It also exposes module fingerprints and export information for VM and tooling diagnostics.

`reference.rz` implements a Raz-native execution oracle for RXE semantics. It supports deterministic memory, references, direct and indirect calls, closures, integer semantics, globals, aggregate operations, control flow, and safety limits. It is used for differential validation against MIR and is separate from any chain-specific VM.

`tests/examples/backends/rxe/` contains conformance fixtures for control flow, arrays, slices, indirect calls, closure behavior, structure copies, and executable identity.

The generated binary-format reference is [RXE-v1-FORMAT.md](RXE-v1-FORMAT.md), and the stable ISA contract is [RXE-ISA-v1.md](RXE-ISA-v1.md).
