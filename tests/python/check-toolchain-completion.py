#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
cli = (ROOT / 'compiler/src/driver/cli.rz').read_text(encoding='utf-8')
commands = (ROOT / 'compiler/src/driver/commands.rz').read_text(encoding='utf-8')
main = (ROOT / 'compiler/src/main.rz').read_text(encoding='utf-8')
interp = (ROOT / 'compiler/src/mir/interpreter.rz').read_text(encoding='utf-8')
registry = (ROOT / 'compiler/src/driver/registry.rz').read_text(encoding='utf-8')
host_main = (ROOT / 'src/bootstrap/tools/raz/main.cpp').read_text(encoding='utf-8')
bootstrap = (ROOT / 'tools/bootstrap.py').read_text(encoding='utf-8')
presets = (ROOT / 'CMakePresets.json').read_text(encoding='utf-8')
build_driver = (ROOT / 'src/bootstrap/tools/raz/detail/build_driver.hpp').read_text(encoding='utf-8')
forge_bridge = (ROOT / 'src/bootstrap/compiler/backend/forge/forge_bridge.cpp').read_text(encoding='utf-8')
runtime_cmake = (ROOT / 'src/runtime/CMakeLists.txt').read_text(encoding='utf-8')
oblink_link = (ROOT / 'src/oblink/src/link.cpp').read_text(encoding='utf-8')
oblink_pe = (ROOT / 'src/oblink/src/pe.cpp').read_text(encoding='utf-8')
oblink_cli = (ROOT / 'src/oblink/tools/oblink/main.cpp').read_text(encoding='utf-8')

checks = {
    'bench command is recognized': 'i64 c_bench[5]' in cli and 'return 40;' in cli,
    'cache command is recognized': 'i64 c_cache[5]' in cli and 'return 41;' in cli,
    'bench has configurable iterations': 'fn cli_bench_iterations_option' in cli and '--iterations' not in cli,  # encoded literal parser is source-owned
    'bench executes MIR functions': 'execute_mir_benches(&input, &hir, &mut mir, bench_iterations)' in main,
    'bench discovers bench_ functions': 'fn execute_mir_benches(' in interp and 'length >= 6' in interp,
    'bench uses monotonic clock': 'raz_compiler_rt_monotonic_nanos()' in interp,
    'shared cache command exists': 'fn registry_cache_command(' in registry,
    'cache command is routed': 'cli_command == 41' in commands and 'registry_cache_command(process_argc)' in commands,
    'clean removes target state': 'raz_compiler_rt_remove_path_ascii(path, 6)' in cli,
    'clean removes the complete target state root': 'raz_compiler_rt_remove_path_ascii(path, 6)' in cli and 'single canonical root' in cli,
    'clean removes legacy pre-R19 state': 'i64 legacy[4] = [46, 114, 97, 122];' in cli and 'raz_compiler_rt_remove_path_ascii(path, 4)' in cli,
    'clean removes build state': 'raz_compiler_rt_remove_path_ascii(path, 5)' in cli,
    'host clean uses target as generated-state root': 'graph.manifest.root / "target" / "cache" / "workspace-v1.state"' in host_main,
    'host clean removes legacy pre-R19 state': 'remove_all(graph.manifest.root / ".raz", clean_error)' in host_main,
    'Raz host build lives under build': 'build_root = ROOT / "build"' in bootstrap and 'host_build = build_root / args.host_preset' in bootstrap,
    'compiler qualification lives under target': 'qualification = ROOT / "target" / "bootstrap"' in bootstrap,
    'compiler-qualification directory removed': 'compiler-qualification' not in bootstrap,
    'Windows bootstrap leaves RAZ_LINKER free for bundled ObLink': 'env.pop("RAZ_LINKER", None)' in bootstrap and 'env["RAZ_EXTERNAL_LINKER"] = compiler' in bootstrap,
    'bootstrap builds bundled ObLink': '"forge", "oblink"' in bootstrap,
    'Windows runtime omits host security-cookie ABI': '/GS-' in runtime_cmake,
    'Windows fallback filters foreign LIB paths': 'windows_native_library_environment' in build_driver and 'lower.find("strawberry")' in build_driver and 'lower.find("mingw")' in build_driver and 'lower.find("msys")' in build_driver,
    'Forge bridge fallback filters foreign LIB paths': 'windows_native_library_environment' in forge_bridge and 'execute_windows_msvc_fallback' in forge_bridge,
    'ObLink fallback sees native_link_command declaration': build_driver.find('std::string native_link_command(const std::vector<std::filesystem::path>& inputs,') < build_driver.find('bool execute_native_link_command('),
    'bootstrap repro artifacts stay in workspace target': 'stage_target = directory / "target" / args.bootstrap_profile' in bootstrap and 'obj = stage_target / f"compiler{OBJ}"' in bootstrap and 'exe = stage_target / f"raz-compiler{EXE}"' in bootstrap,
    'CMake debug preset lives under build': '${sourceDir}/build/debug' in presets,
    'CMake release preset lives under build': '${sourceDir}/build/release' in presets,
    'ObLink parses Microsoft short import objects': 'parse_import_object' in oblink_link and '0xffffU' in oblink_link,
    'ObLink synthesizes PE import and IAT directories': 'import_directory_rva' in oblink_link and 'options.import_rva' in oblink_pe and 'options.iat_rva' in oblink_pe,
    'ObLink honors COFF default libraries': 'collect_directives' in oblink_link and '/defaultlib:' in oblink_link and 'find_library' in oblink_link,
    'ObLink CLI consumes library search options': 'options.library_paths.emplace_back' in oblink_cli and 'options.libraries.emplace_back' in oblink_cli,
    'Raz forwards Windows system libraries to ObLink': '-l ws2_32 -l bcrypt -l crypt32' in build_driver and '-l ws2_32 -l bcrypt -l crypt32' in forge_bridge,
    'ObLink discovers installed MSVC and Windows SDK libraries': 'append_windows_toolchain_paths' in oblink_link and 'Windows Kits' in oblink_link and 'VC\" / \"Tools\" / \"MSVC' in oblink_link,
    'ObLink filters foreign Windows library roots': 'foreign_windows_library_path' in oblink_link and 'strawberry' in oblink_link and 'mingw' in oblink_link,
    'ObLink models weak externals and aliases': 'weak_default_index' in (ROOT / 'src/oblink/include/oblink/coff.hpp').read_text(encoding='utf-8') and '/alternatename:' in oblink_link,
    'ObLink allocates COFF common symbols': 'common_sizes' in oblink_link and 'common_placements' in oblink_link,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'FAIL: {name}')
    raise SystemExit(1)
print(f'toolchain-completion: PASS ({len(checks)} contracts)')
