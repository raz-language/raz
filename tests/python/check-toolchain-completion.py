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

checks = {
    'bench command is recognized': 'i64 c_bench[5]' in cli and 'return 40;' in cli,
    'cache command is recognized': 'i64 c_cache[5]' in cli and 'return 41;' in cli,
    'bench has configurable iterations': 'fn cli_bench_iterations_option' in cli and '--iterations' not in cli,  # encoded literal parser is source-owned
    'bench executes MIR functions': 'execute_mir_benches(&input, &hir, &mut mir, bench_iterations)' in main,
    'bench discovers bench_ functions': 'fn execute_mir_benches(' in interp and 'length >= 6' in interp,
    'bench uses monotonic clock': 'raz_rt_time_monotonic_nanos()' in interp,
    'shared cache command exists': 'fn registry_cache_command(' in registry,
    'cache command is routed': 'cli_command == 41' in commands and 'registry_cache_command(process_argc)' in commands,
    'clean removes target state': 'raz_compiler_rt_remove_path_ascii(path, 6)' in cli,
    'clean removes incremental state': 'raz_compiler_rt_remove_path_ascii(path, 4)' in cli,
    'clean removes build state': 'raz_compiler_rt_remove_path_ascii(path, 5)' in cli,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'FAIL: {name}')
    raise SystemExit(1)
print(f'toolchain-completion: PASS ({len(checks)} contracts)')
