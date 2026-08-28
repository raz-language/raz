# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
forge = root / "src" / "forge"

# Keep this gate tied to the canonical C++ Forge backend actually shipped by Raz.
# It intentionally checks broad backend capabilities rather than exact optimizer
# output, which belongs to Forge's own CTest suite.
checks = {
    "C API v16 is canonical": ("include/forge-c/forge.h", "#define FORGE_C_API_VERSION 16"),
    "structured operation C API exists": ("src/capi/forge.cpp", "forge_block_append_operation"),
    "structured globals C API exists": ("src/capi/forge.cpp", "forge_module_add_global"),
    "structured aggregates C API exists": ("src/capi/forge.cpp", "forge_module_add_struct"),
    "machine lowering resolves globals": ("src/machine/lower.cpp", "Opcode::load_global_address"),
    "machine lowering resolves functions": ("src/machine/lower.cpp", "Opcode::load_function_address"),
    "indirect calls are lowered": ("src/machine/lower.cpp", "Opcode::call_indirect_i64"),
    "aggregate returns are lowered": ("src/machine/lower.cpp", "Opcode::return_aggregate"),
    "live ranges split around calls": ("src/machine/register_allocation.cpp", "split_live_ranges_around_calls"),
    "machine optimizer exists": ("src/machine/optimize.cpp", "optimize"),
    "System V ABI classification exists": ("src/platform/abi.cpp", "NativeAbi::system_v_x86_64"),
    "Windows x64 ABI classification exists": ("src/platform/abi.cpp", "NativeAbi::windows_x64"),
    "AAPCS64 ABI classification exists": ("src/platform/abi.cpp", "NativeAbi::aapcs64"),
    "AArch64 register allocator exists": ("src/codegen/aarch64/register_allocation.cpp", "allocate_registers"),
    "AArch64 allocator uses callee-saved integer bank": ("src/codegen/aarch64/register_allocation.cpp", "integer_pool{19, 20, 21, 22, 23, 24, 25, 26, 27, 28}"),
    "AArch64 canonical machine combines exist": ("src/machine/optimize.cpp", "optimize_aarch64_canonical_module"),
    "AArch64 encoder exists": ("src/codegen/aarch64/encoder.cpp", "namespace forge::codegen::aarch64"),
    "AArch64 ELF writer exists": ("src/object/elf_aarch64.cpp", "em_aarch64 = 183"),
    "AArch64 Mach-O writer exists": ("src/object/macho_aarch64.cpp", "mh_magic_64 = 0xFEEDFACF"),
    "Darwin arm64 C API exists": ("include/forge-c/forge.h", "FORGE_ABI_DARWIN_ARM64"),
    "AArch64 allocator copy coalescing exists": ("src/codegen/aarch64/register_allocation.cpp", "coalesced_copy_count"),
    "AArch64 spill-slot coloring exists": ("src/codegen/aarch64/register_allocation.cpp", "reused_spill_slot_count"),
    "AArch64 immediate selection exists": ("src/machine/optimize.cpp", 'instruction.symbol = comparison ? "$cmpimm" : "$imm"'),
    "AArch64 NEON integer encoder exists": ("src/codegen/aarch64/encoder.cpp", "emit_neon_integer_binary"),
    "AArch64 alias-safe NEON SLP exists": ("src/machine/optimize.cpp", "first.source_base != first.destination_base"),
    "AArch64 NEON vector stats exist": ("include/forge/codegen/aarch64/encoder.hpp", "neon_vector_operation_count"),
    "AArch64 full-width vector allocation exists": ("src/codegen/aarch64/register_allocation.cpp", "vector_pool{16, 17, 18, 19, 20, 21, 22, 23}"),
    "AArch64 call-crossing Q values are stack-homed": ("src/codegen/aarch64/register_allocation.cpp", "vector_crosses_call"),
    "AArch64 128-bit spill homes exist": ("src/codegen/aarch64/register_allocation.cpp", "return 16U"),
    "AArch64 v128 stack encoding exists": ("src/codegen/aarch64/encoder.cpp", "O::load_stack_v128"),
    "AArch64 NEON reduction encoding exists": ("src/codegen/aarch64/encoder.cpp", "emit_neon_reduce_add"),
    "AArch64 packed chain encoding exists": ("src/codegen/aarch64/encoder.cpp", "binary_i32_contiguous_chain"),
    "AArch64 packed DAG encoding exists": ("src/codegen/aarch64/encoder.cpp", "binary_i32_contiguous_dag"),
    "AArch64 automatic reduction formation exists": ("src/machine/optimize.cpp", "reduce_add_i32_contiguous"),
    "AArch64 alias resolution precedes vreg compaction": ("src/machine/optimize.cpp", "forest once *before* any vreg compaction"),
    "ELF relocations are emitted": ("src/object/elf.cpp", "global_relocations"),
    "COFF relocations are emitted": ("src/object/coff.cpp", "global_relocations"),
    "archive support exists": ("src/object/archive.cpp", "Archive"),
    "native linking support exists": ("src/object/native_link.cpp", "NativeLink"),
    "O3 pipeline exists": ("src/pass/pipeline.cpp", "OptimizationLevel::o3"),
    "Os pipeline exists": ("src/pass/pipeline.cpp", "OptimizationLevel::os"),
    "Oz pipeline exists": ("src/pass/pipeline.cpp", "OptimizationLevel::oz"),
    "large-module O2 compile-time guard exists": ("src/capi/forge.cpp", "kLargeModuleFunctionThreshold = 1024"),
    "large-module O2 guard preserves ordinary modules": ("src/capi/forge.cpp", "module->value->functions().size() >= kLargeModuleFunctionThreshold"),
}

raz_checks = {
    "compiler decimal writer handles i64 minimum": (
        root / "compiler" / "src" / "raz_codegen_forge" / "src" / "forge" / "writer.rz",
        "value == (-9223372036854775807 - 1)",
    ),
    "Forge call mismatch diagnostic reports producer": (
        forge / "src" / "ir" / "verifier.cpp",
        "produced by",
    ),
    "legacy Forge fallback emits typed f64 reference loads": (
        root / "compiler" / "src" / "raz_codegen_forge" / "src" / "forge" / "function_codegen.rz",
        "writer_runtime_name(out, 11)",
    ),
    "legacy Forge fallback uses ordinary stage1 runtime ABI": (
        root / "compiler" / "src" / "raz_codegen_forge" / "src" / "forge" / "writer.rz",
        "fn emit_stage1_legacy_runtime_declarations",
    ),
    "runtime exposes allocation-free stage1 references": (
        root / "src" / "runtime" / "core.cpp",
        "std::int64_t raz_rt_stage1_ref_create(std::int64_t frame, std::int64_t slot)",
    ),
    "runtime preserves f64 bits through stage1 references": (
        root / "src" / "runtime" / "core.cpp",
        "double raz_rt_stage1_ref_get_f64(std::int64_t reference)",
    ),
    "structured Forge uses compact opcode bridge": (
        root / "compiler" / "src" / "raz_codegen_forge" / "src" / "forge" / "native_support.rz",
        "raz_compiler_forge_block_append_operation_word_i64",
    ),
    "Forge O0 object path skips redundant optimizer verification": (
        root / "src" / "bootstrap" / "compiler" / "backend" / "forge" / "forge_bridge.cpp",
        "if (optimization_level != 0)",
    ),
    "Forge structure identity decisions are cached": (
        root / "compiler" / "src" / "raz_codegen_forge" / "src" / "forge" / "writer.rz",
        "struct_identity_suffix_cache",
    ),
    "Forge synthetic boolean registers use disjoint namespace": (
        root / "compiler" / "src" / "raz_codegen_forge" / "src" / "forge" / "writer.rz",
        "fn writer_true_register() -> i64",
    ),
    "structured Forge builds a function reachability graph": (
        root / "compiler" / "src" / "raz_codegen_forge" / "src" / "forge" / "native_functions.rz",
        "backend_build_function_reachability_for_module(",
    ),
    "structured Forge filters ordinary function declarations by reachability": (
        root / "compiler" / "src" / "raz_codegen_forge" / "src" / "forge" / "native_functions.rz",
        "bool reachable_function =",
    ),
    "structured Forge filters function bodies by reachability": (
        root / "compiler" / "src" / "raz_codegen_forge" / "src" / "forge" / "native_functions.rz",
        "raz_compiler_rt_arena_get(reachable_functions, function_index) != 0",
    ),
    "structured Forge filters callable adapters by reachable owners": (
        root / "compiler" / "src" / "raz_codegen_forge" / "src" / "forge" / "native_operations.rz",
        "i64 reachable_functions",
    ),
}

failed = []
for label, (rel, needle) in checks.items():
    path = forge / rel
    if not path.is_file():
        failed.append(f"{label} (missing {rel})")
        continue
    if needle not in path.read_text(encoding="utf-8"):
        failed.append(label)

for label, (path, needle) in raz_checks.items():
    if not path.is_file() or needle not in path.read_text(encoding="utf-8"):
        failed.append(label)

writer_path = root / "compiler" / "src" / "raz_codegen_forge" / "src" / "forge" / "writer.rz"
if writer_path.is_file() and "value + 1 == 0" in writer_path.read_text(encoding="utf-8"):
    failed.append("legacy broken i64-min decimal guard removed")

# Never borrow plausible MIR instruction ids as backend-only SSA sentinels.
# The self-hosted compiler has already grown beyond the old 999998/999999
# constants, which made otherwise valid Forge IR contain duplicate SSA names.
forge_backend_root = root / "compiler" / "src" / "raz_codegen_forge" / "src" / "forge"
for path in forge_backend_root.glob("*.rz"):
    text = path.read_text(encoding="utf-8")
    if "999998" in text or "999999" in text:
        failed.append(f"hardcoded Forge SSA sentinel removed ({path.name})")

if failed:
    for label in failed:
        print(f"FAIL: {label}")
    raise SystemExit(1)

print(f"Forge backend integrity: PASS ({len(checks)} C++ checks + {len(raz_checks)} Raz compiler checks)")
