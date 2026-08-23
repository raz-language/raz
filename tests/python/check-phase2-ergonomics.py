#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
fmt = (ROOT / 'library/std/fmt/fmt.rz').read_text(encoding='utf-8')
stdio = (ROOT / 'library/std/io/stdio.rz').read_text(encoding='utf-8')
cli = (ROOT / 'compiler/src/driver/cli.rz').read_text(encoding='utf-8')
interp = (ROOT / 'compiler/src/mir/interpreter.rz').read_text(encoding='utf-8')
main = (ROOT / 'compiler/src/main.rz').read_text(encoding='utf-8')
testing = (ROOT / 'library/std/testing/testing.rz').read_text(encoding='utf-8')

checks = {
    'binary formatting': 'public fn append_binary_u64' in fmt and 'public fn format_binary_u64' in fmt,
    'octal formatting': 'public fn append_octal_u64' in fmt and 'public fn format_octal_u64' in fmt,
    'bool formatting': 'public fn format_bool' in fmt,
    'padded unsigned formatting': 'public fn append_padded_u64' in fmt,
    'String stdout helpers': 'public fn print_string' in stdio and 'public fn println_string' in stdio,
    'typed stdout helpers': 'public fn println_i64' in stdio and 'public fn println_u64' in stdio and 'public fn println_bool' in stdio,
    'stderr text helpers': 'public fn eprint_string' in stdio and 'public fn eprintln_string' in stdio,
    'test filter parser': 'fn cli_test_filter_option' in cli and 'string prefix = "--filter=";' in cli,
    'test filter wired to runner': 'execute_mir_tests(&input, &hir, &mut mir, test_filter, test_filter_length)' in main,
    'per-test status': 'mir_test_write_name' in interp and 'passed += 1;' in interp and 'failed += 1;' in interp,
    'test summary': 'mir_bench_write_literal(" passed; ")' in interp and 'first_failure' in interp,
    'testing expectations': 'public fn expect_i64_eq' in testing and 'public fn expect_u64_eq' in testing and 'public fn expect_bool_eq' in testing,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    print('phase2-ergonomics: FAIL')
    for name in failed:
        print('  ' + name)
    raise SystemExit(1)
print(f'phase2-ergonomics: PASS ({len(checks)} contracts)')
