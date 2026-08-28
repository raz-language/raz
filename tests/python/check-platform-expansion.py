#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
backend = (ROOT / "compiler/src/raz_driver/src/backend.rz").read_text(encoding="utf-8")
target = (ROOT / "compiler/src/raz_codegen_llvm/src/llvm/target.rz").read_text(encoding="utf-8")
main = (ROOT / "compiler/src/raz_driver/src/compiler_main.rz").read_text(encoding="utf-8")
cli = (ROOT / "compiler/src/raz_driver/src/cli.rz").read_text(encoding="utf-8")
build_driver = (ROOT / "src/bootstrap/tools/raz/detail/build_driver.hpp").read_text(encoding="utf-8")
bootstrap = (ROOT / "tools/bootstrap.py").read_text(encoding="utf-8")
readme = (ROOT / "README.md").read_text(encoding="utf-8")
platform_doc = (ROOT / "docs/PLATFORM-SUPPORT.md").read_text(encoding="utf-8")
tests_cmake = (ROOT / "tests/CMakeLists.txt").read_text(encoding="utf-8")
cross_object_test = ROOT / "tests/python/test-forge-aarch64-cross-object.py"

checks = {
    "unsupported Forge hosts default to LLVM": "host_arch == 1" in backend and "host_platform == 1 || host_platform == 2" in backend and "return 1;" in backend,
    "LLVM target architecture parser exists": "fn llvm_target_architecture" in target and '"aarch64"' in target and '"arm64"' in target,
    "LLVM target platform parser exists": "fn llvm_target_platform" in target and '"linux"' in target and '"apple"' in target,
    "cross-target host runtime is forbidden": "fn llvm_cross_target_executable_requires_runtime" in target and "llvm_target_matches_host(options)" in target,
    "main reports missing target runtime": "cli_print_cross_target_runtime_required" in main,
    "link libraries follow target rather than host": "fn llvm_append_platform_link_libraries" in target,
    "Forge native unsupported-host mis-emission is guarded": "unavailable for this host/object-format pair" in build_driver and "defined(__APPLE__)" in build_driver,
    "Forge AArch64 host emission is architecture-aware": "TargetArchitecture::aarch64" in build_driver and "emit_elf64_aarch64" in build_driver and "emit_macho64_aarch64" in build_driver,
    "AArch64 bootstrap accepts stage0": '"--stage0"' in bootstrap and 'RAZ_STAGE0_COMPILER' in bootstrap,
    "AArch64 bootstrap seeds with LLVM": '"--backend=llvm"' in bootstrap and '"--emit=obj"' in bootstrap,
    "AArch64/macOS self-host generations use LLVM": 'llvm_seed_bootstrap = host_arch == "aarch64" or sys.platform.startswith("darwin")' in bootstrap and 'if llvm_seed_bootstrap:' in bootstrap and 'Self-host backend:' in bootstrap,
    "Linux AArch64 is documented": "aarch64-unknown-linux-gnu" in readme and "aarch64-unknown-linux-gnu" in platform_doc,
    "macOS arm64 is documented": "arm64-apple-macos" in readme and "arm64-apple-macos" in platform_doc,
    "LLVM incremental artifacts include codegen target policy": "fn llvm_codegen_cache_key" in target and "backend_options_key" in main,
    "target discovery function exists": "fn cli_print_targets()" in cli,
    "Raz-to-Forge AArch64 cross-object qualification is permanent": cross_object_test.is_file() and "raz-forge-aarch64-cross-object" in tests_cmake and "test-forge-aarch64-cross-object.py" in tests_cmake,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f"FAIL: {name}")
    raise SystemExit(1)
print(f"platform-expansion: PASS ({len(checks)} contracts)")
