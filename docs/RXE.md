# RXE Backend

RXE (Raz Executable) is Raz's deterministic bytecode target. It consumes the same verified MIR as Forge and LLVM and is implemented entirely in Raz under `compiler/src/backend/rxe/`.

## RXE v7 execution contract

- `--backend=rxe` selects RXE (backend kind 2).
- Canonical instructions are 8 bytes: `opcode:u8 dst:u8 src0:u8 src1:u8 imm:u32`, little-endian.
- RXE exposes 32 architectural registers.
- MIR SSA values have deterministic frame slots as the correctness baseline. A deterministic CFG-aware linear-scan pass assigns proven MIR-result slot intervals to `r6-r31`, including safe loop-local intervals. Loop-carried values that load before a defining store remain frame-backed.
- The file begins with a fixed 104-byte deterministic v7 header followed by 16-byte function records, code, an i64 constant pool, 16-byte global-image records, aggregate layouts, callable-signature metadata, 24-byte exports, and a canonical 16-byte basic-block directory.
- No timestamps, pointer-derived identifiers, randomized ordering, or native ABI details enter the RXE file.
- Unsupported MIR capabilities fail explicitly. RXE never falls back to Forge or LLVM.

## RXE core ISA implemented

The compiler lowering currently covers:

- constants, parameters, locals and deterministic MIR value slots;
- integer add/subtract/multiply/divide/remainder, bitwise operations and shifts;
- explicit 8/16/32-bit truncation and sign extension so sub-64-bit scalar behavior is represented by RXE instructions rather than host-machine behavior;
- comparisons, boolean operations and select;
- unconditional and conditional control flow;
- direct Raz calls with explicit argument staging;
- first-class function references and indirect function calls;
- escaping closure construction and closure calls, including explicit capture argument staging;
- local/global references, index references, loads and stores;
- indexed aggregate/array memory loads and stores;
- deterministic RXE allocation/free operations;
- deterministic module globals and serialized global initialization image;
- string/global references;
- scalar casts for integer targets;
- machine-local register forwarding and peephole canonicalization before verification.

Hardware floating-point MIR is currently rejected by RXE rather than inheriting host floating-point behavior. Async/future operations and native/OS capabilities remain capability-gated.

RXE intentionally does not yet define blockchain state, accounts, gas, transaction context, consensus, crypto host calls, or validator services. Those belong to later execution-environment capability layers rather than the portable core ISA.

## Binary layout

```
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

The header records counts plus byte offsets/sizes for code, constants, globals, aggregate layouts, and callable signatures. Function records include code start/count, frame-slot count, argument count, and the architectural register count discovered after register promotion. Global records contain the deterministic initial i64 image, flags, and zeroed reserved bytes.

## Compiler pipeline

```
Raz source
  -> HIR
  -> verified MIR
  -> RXE lowering
  -> RXE CFG-aware linear-scan allocation / conservative spill promotion
  -> RXE machine canonicalization
  -> RXE verifier
  -> deterministic RXE writer
  -> .rxe
```

MIR remains the semantic boundary. RXE does not reuse Forge or LLVM lowering and does not reimplement Raz type/ownership semantics.

## ABI direction

Direct calls use explicit `ARG` staging followed by `CALL function_index`. Indirect calls use a first-class `FUNC_REF` value plus `CALL_INDIRECT`. Closures use `MAKE_CLOSURE` with compiler-known capture staging and `CALL_CLOSURE`. Argument and capture counts are bounded by the one-byte canonical operand fields in RXE v1.

The production VM may predecode and fuse these operations later; canonical RXE bytes remain stable and deterministic.

## Remaining compiler-side work

The next RXE work should focus on phi-aware loop allocation, bytecode-level aggregate-copy fusion, richer slice bounds metadata, export/ABI descriptors, and executable differential conformance automation. Blockchain-specific operations remain intentionally reserved until the portable core is mature.

## RXE format v2: aggregate layouts

RXE format version 2 makes aggregate shape part of the executable artifact. The
last eight bytes of the fixed 64-byte header now contain the aggregate-layout
section offset and size. The section contains 16-byte layout descriptors followed
by 16-byte structure-field descriptors.

A layout descriptor records kind (`1` structure, `2` enum image), field start,
field count, and slot count. Structure-field descriptors record primitive type,
nested structure identity, fixed-array extent, and reference/slice/function flags.
Enums use the same slot-image representation as MIR: slot zero is the tag and the
remaining slots hold the largest payload image.

MIR aggregate allocation (`op 30`) is encoded by RXE v2 with the low 16 bits of
the instruction immediate holding slot count and the high 16 bits holding
`layout + 1` (`0` means an untyped array/image allocation). This lets an
independent loader verify structure allocations without access to HIR.

## Signed integer semantics

RXE v2 distinguishes signed operations that cannot safely inherit host-machine
interpretation. Signed division/remainder, arithmetic right shift, and signed
relational comparisons have dedicated opcodes. Unsigned operations retain their
own canonical opcodes. Sub-64-bit normalization remains explicit via truncation
and sign-extension instructions.

## Compiler reference tools

`disasm.rz` provides a deterministic numeric assembly dump for backend and VM
bring-up. It deliberately prints canonical opcode numbers and operands, so the
binary ISA does not depend on a human-readable mnemonic table.

`reference.rz` now provides a shared Raz-native reference state with tagged, host-pointer-free heap/global/closure handles. It executes allocation/free, indexed memory, references, first-class functions, indirect calls, escaping closures with captured values, signed and unsigned integer operations, truncation/sign extension, globals, direct calls, and control flow. Bounds, use-after-free, invalid handles, bad indirect targets, call depth, and instruction limits fail deterministically. It remains a differential-test oracle against MIR, not the blockchain VM.


## Pass 5: reference memory and register allocation

The reference executor owns a deterministic arena-backed memory oracle shared by recursive RXE calls. Heap references are tagged integer handles, never host pointers. Each heap cell records its allocation owner; bounds checks therefore remain valid for interior `INDEX_REF` handles, and freed owners invalidate every derived reference. Closure objects similarly use tagged handles and a deterministic capture image. `CALL_CLOSURE` appends stored captures after explicit arguments to match Raz closure parameter layout.

A deterministic linear-scan allocator now assigns SSA-result frame slots to architectural registers `r6-r31` when a function has no backward control-flow edge. The original slot representation remains the semantic fallback for spills and loops. A secondary resident-slot forwarding pass still removes redundant reloads where safe. This improves RXE density without requiring speculative loop liveness.

The verifier additionally constrains branch targets to their owning function, validates slot/argument indexes, checks direct-call arity, and validates register-valued immediates used by `SELECT` and `INDEX_STORE`.

## Pass 6: CFG liveness, callable signatures, arrays and aggregate copying

RXE format version 4 adds verifier-visible callable signatures. The deterministic
header is now 96 bytes and records a dedicated signature section after aggregate
layouts. The signature section contains direct-function descriptors, interned
function-type descriptors used by `CALL_INDIRECT`/`CALL_CLOSURE`, direct function
parameter descriptors, and function-type parameter descriptors. References,
slices, function values, fixed arrays, nested structures and return-value shape
are encoded as canonical flags/type metadata; a VM does not need Raz HIR to
validate callable arity and shape.

The register allocator now performs conservative CFG-aware interval allocation
for backward edges. Stable slot-to-register mappings replace the pass-5 mapping
scheme that could retain a stale mapping after a register was recycled. Values
that are provably defined before use can remain in `r6-r31` across loops; slots
whose loop body loads before its first store remain frame-backed as loop-carried
values until phi-aware allocation is introduced.

Fixed-array allocation distinguishes three canonical allocation kinds: generic
images, structures, and fixed arrays. Primitive arrays may carry no nested layout;
arrays of structures carry the element layout, while extent remains independent
of element structure size. MIR ownership/drop-state markers (`32`/`33`) lower to
runtime-neutral zero values instead of making RXE reject otherwise valid aggregate
programs.

`MEM_COPY` now has deterministic overlap-safe reference-executor semantics for
aggregate images. It copies a verified slot count between live heap references
with memmove behavior and never exposes host addresses. This is the semantic
primitive future machine-local copy fusion can target while MIR continues to own
high-level clone/copy decisions.

`examples/backends/rxe/` now carries focused conformance fixtures for loop
liveness, primitive fixed arrays, slices, indirect calls and structure copies.
They are source fixtures for local differential/self-host testing; lightweight
repository validation only checks that the compiler-side coverage remains wired.


## RXE v4 callable identity

RXE v4 extends every direct-function signature descriptor with a deterministic
32-bit FNV-1a hash of the Raz source function name and the retained Raz ABI kind.
This gives deployment tooling and an independent VM verifier a stable callable
identity without serializing source strings into the hot executable tables.

The register allocator also recognizes a proven loop-carried frame value as a
machine-level phi when a preheader definition exists. One stable r6-r31 register
then carries the value around the backedge; ambiguous loop entries remain spilled.

## Pass 8: bounded slices, exports and module identity

RXE format v5 keeps the 96-byte header and uses its final three u32 words for a two-lane semantic module fingerprint and the export count. The export table follows the signature section and contains 16-byte deterministic descriptors `(function_index, name_hash, abi_kind, flags, reserved)` sorted by stable name hash. Its offset is derived from the signature section, avoiding another header pointer.

Slice values retain the existing two-slot Raz representation `(data_ref, length)`, but slice indexing is no longer indistinguishable from raw aggregate indexing in RXE. MIR preserves the originating slice value as non-semantic metadata and the RXE backend emits `slice.load` / `slice.store`; the Raz reference executor rejects negative or `index >= length` accesses before resolving the data reference. Raw fixed-array/aggregate indexing continues to use `index.load` / `index.store`.

The differential oracle now accepts an argument arena and executes the same values through both `execute_mir_function` and the RXE reference executor. RXE disassembly also prints stable opcode mnemonics, the module fingerprint, and export count to make C-VM bring-up comparisons readable.


## Pass 9: RXE v6 selectors, capabilities, full fingerprint and block directory

RXE format v6 upgrades exported callable identity from a name-only hash to a
signature-aware 64-bit selector represented as two deterministic 32-bit lanes.
The selector mixes source-name identity, ABI kind, return shape, and every
canonical parameter descriptor. The original name hash remains available for
diagnostics/tooling, while export lookup is sorted by `(selector_hi, selector_lo,
function_index)`. Duplicate 64-bit selectors are verifier errors rather than
ambiguous callable aliases.

The previously reserved module flags word is now a canonical ISA feature bitmap.
It is derived after machine optimization and records whether the executable uses
calls, closures, reference/aggregate memory, globals, bounded slices, and signed
integer semantics. The verifier independently recomputes the bitmap from the
final opcode stream. A future loader can therefore reject unsupported executable
features before building an execution image.

The two-lane module fingerprint now covers the complete semantic executable image:
format/ISA version, feature flags, function/code records, instructions, constants,
globals, aggregate layouts, direct and indirect callable metadata, exports, and
canonical basic-block descriptors. Verification independently recomputes the
fingerprint, so mutating metadata without updating executable identity is rejected.

RXE v6 also serializes a canonical basic-block directory after the export table.
Each 16-byte block record contains `(code_start, code_count, successor0, successor1)`.
Per-function block counts fit in the existing 16-byte function directory record.
The verifier checks that blocks exactly partition their owning function and that
successors agree with `jmp`, `brnz`, return/trap, and fallthrough semantics. This
lets the production C loader consume a verified CFG directly instead of rediscovering
it from bytecode.

## Pass 10: fixed-point liveness and proof-driven machine optimization

RXE allocation now uses the canonical basic-block directory as an input, not merely
as post-codegen metadata. For each function the compiler builds frame-slot use/def
sets and solves live-in/live-out to a fixed point over the serialized CFG. Linear
scan intervals are extended through every block where a value is live, allowing a
single architectural register to carry the value through branch joins and loop
backedges. A slot with no materializable incoming definition remains frame-backed.

After allocation, the same dataflow removes `store.slot` instructions whose value
is dead on every successor path. This optimization applies only to compiler frame
slots; addressable locals, globals, references and heap state are never treated as
dead frame storage.

RXE also has a deliberately strict aggregate-copy fusion. A contiguous element
copy run can become `mem.copy` only when source and destination register provenance
tracks to distinct `alloc` instructions in the same basic block. Unknown provenance,
merges, calls or possible aliasing disable the optimization. The compiler retains
source element loads so observable result registers remain unchanged and removes
only redundant destination stores. This keeps RXE's overlap behavior exact while
reducing repeated heap writes when the proof succeeds.
## Pass 11: architectural register liveness and decoded-uop preparation

RXE machine optimization now performs a second fixed-point dataflow analysis after physical register allocation. The analysis computes per-basic-block architectural-register use/def and live-in/live-out sets across the canonical serialized CFG. This allows dead pure register producers to be replaced by `nop` while preserving any instruction whose execution can trap, access memory, call code, allocate, or otherwise remain observable even when its result register is unused.

Block-local constant propagation folds only values representable by the canonical `movi` u32 immediate. Wider constants remain in the constant pool and potentially trapping operations are not folded. After alias-proven aggregate-copy fusion, redundant source element loads may be removed only when register liveness proves their values dead and the preceding `mem.copy` has already validated the full source range under the distinct-allocation proof.

The compiler also detects common two-instruction patterns as advisory superinstruction candidates for a future validator's decoded micro-op image. These candidates are not serialized and do not change RXE format v6 or ISA v1 semantics.



## RXE backend pass 12: independent decode and freeze preparation

RXE format v7 fixes a decode ambiguity discovered while building the independent Raz-side binary decoder. The v6 header carried only the aggregate-layout section byte size, so an implementation could not know how many 16-byte layout records versus 16-byte field records occupied that section without compiler-side type information. v7 grows the header from 96 to 104 bytes and adds explicit `layout_count` and `layout_field_count` words. ISA semantics remain v1.

`decoder.rz` consumes only serialized RXE bytes. It validates magic/version, canonical section geometry, function/code partitioning, opcode/register bounds, reserved-zero fields, and exact file length. The compiler now re-opens every emitted `.rxe`, decodes it, canonically re-encodes it, and requires byte-for-byte identity before reporting backend success. `scripts/generate-rxe-spec.py` generates `docs/RXE-v1-FORMAT.md` directly from the implementation's format/ISA constants and opcode definitions.
