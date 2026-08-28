# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
root = Path(__file__).resolve().parents[2]
project = (root / 'compiler/src/raz_driver/src/project.rz').read_text()
main = (root / 'compiler/src/raz_driver/src/compiler_main.rz').read_text()
forge = (root / 'compiler/src/raz_codegen_forge/src/forge/native_functions.rz').read_text()
forge_codegen = (root / 'compiler/src/raz_codegen_forge/src/forge/codegen.rz').read_text()
fingerprints = (root / 'compiler/src/raz_hir/src/hir/query/fingerprints.rz').read_text()
checks = {
    'package object directory': '"/packages/"' in project and 'profile_dir_length' in project,
    'package objects live beside bin obj categories': 'path_dirname(object_dir, object_dir_length, profile_dir, 8192)' in project,
    'runtime dependency libraries': 'raz_runtime_ssl' in project and 'raz_runtime_crypto' in project,
    'package unit ready marker': '"/packages/.units"' in project,
    'multi object linker': 'project_link_package_units' in project and 'raz_compiler_rt_process_run_argv_ascii' in project,
    'partial HIR gated on complete objects': 'project_package_units_cache_ready' in main and 'package_units_reuse' in main,
    'dirty package only emission': 'project_emit_package_units' in main and 'only_dirty' in project,
    'foreign package functions are declarations': 'declaration_only' in forge and 'forge_native_function_owned_by_unit' in forge,
    'entry wrapper root only': 'emit_entry_wrappers' in forge,
    'root module batch emission': 'emit_forge_module_object_batch' in project and 'emit_forge_module_object_batch' in forge_codegen,
    'batch shares Forge preparation': forge_codegen.count('forge_native_build_image_pointer_values(hir, mir, image_pointer_values)') >= 1 and 'One backend call shares immutable Forge support/image analysis' in project,
    'package identity probes segment start': 'i64 probe = start;' in fingerprints and 'end - 1' not in fingerprints[fingerprints.find('hir_query_export_incremental_module_graph'):fingerprints.find('hir_query_export_incremental_module_graph')+2200],
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('package-codegen-units: FAIL: ' + ', '.join(failed))
print('package-codegen-units: PASS')
