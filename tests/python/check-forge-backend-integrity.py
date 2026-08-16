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
    "C API v14 is canonical": ("include/forge-c/forge.h", "#define FORGE_C_API_VERSION 14"),
    "structured operation C API exists": ("src/capi/forge.cpp", "forge_block_append_operation"),
    "structured globals C API exists": ("src/capi/forge.cpp", "forge_module_add_global"),
    "structured aggregates C API exists": ("src/capi/forge.cpp", "forge_module_add_struct"),
    "machine lowering resolves globals": ("src/machine/lower.cpp", "Opcode::load_global_address"),
    "machine lowering resolves functions": ("src/machine/lower.cpp", "Opcode::load_function_address"),
    "indirect calls are lowered": ("src/machine/lower.cpp", "Opcode::call_indirect_i64"),
    "aggregate returns are lowered": ("src/machine/lower.cpp", "Opcode::return_aggregate"),
    "live ranges split around calls": ("src/machine/register_allocation.cpp", "split_live_ranges_around_calls"),
    "machine optimizer exists": ("src/machine/optimize.cpp", "optimize"),
    "System V ABI classification exists": ("src/target/abi.cpp", "NativeAbi::system_v_x86_64"),
    "Windows x64 ABI classification exists": ("src/target/abi.cpp", "NativeAbi::windows_x64"),
    "ELF relocations are emitted": ("src/object/elf.cpp", "global_relocations"),
    "COFF relocations are emitted": ("src/object/coff.cpp", "global_relocations"),
    "archive support exists": ("src/object/archive.cpp", "Archive"),
    "native linking support exists": ("src/object/native_link.cpp", "NativeLink"),
    "O3 pipeline exists": ("src/pass/pipeline.cpp", "OptimizationLevel::o3"),
    "Os pipeline exists": ("src/pass/pipeline.cpp", "OptimizationLevel::os"),
    "Oz pipeline exists": ("src/pass/pipeline.cpp", "OptimizationLevel::oz"),
}

raz_checks = {
    "compiler decimal writer handles i64 minimum": (
        root / "compiler" / "src" / "backend" / "forge" / "writer.rz",
        "value == (-9223372036854775807 - 1)",
    ),
    "Forge call mismatch diagnostic reports producer": (
        forge / "src" / "ir" / "verifier.cpp",
        "produced by",
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

writer_path = root / "compiler" / "src" / "backend" / "forge" / "writer.rz"
if writer_path.is_file() and "value + 1 == 0" in writer_path.read_text(encoding="utf-8"):
    failed.append("legacy broken i64-min decimal guard removed")

if failed:
    for label in failed:
        print(f"FAIL: {label}")
    raise SystemExit(1)

print(f"Forge backend integrity: PASS ({len(checks)} C++ checks + {len(raz_checks)} Raz compiler checks)")
