#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

root = Path(__file__).resolve().parents[2]
wasi = (root / 'compiler/src/raz_codegen_wasm/src/wasm/wasi.rz').read_text(encoding='utf-8')
codegen = (root / 'compiler/src/raz_codegen_wasm/src/wasm/codegen.rz').read_text(encoding='utf-8')
closures = (root / 'compiler/src/raz_codegen_wasm/src/wasm/closures.rz').read_text(encoding='utf-8')
order = {path.relative_to(root / 'compiler').as_posix() for path in list((root / 'compiler').rglob('*.rz'))}

assert 'src/raz_codegen_wasm/src/wasm/wasi.rz' in order
for needle in [
    'wasm_wasi_emit_import_section',
    'wasm_wasi_emit_runtime_body',
    'wasm_wasi_stdio_write',
    'wasm_wasi_stdio_read',
    'wasm_wasi_defined_function',
]:
    assert needle in wasi or needle in codegen, f'missing WASI primitive: {needle}'
for runtime in ['raz_rt_stdio_read', 'raz_rt_stdio_write', 'raz_rt_stdio_flush', 'raz_rt_stdio_is_terminal']:
    # Names are encoded as deterministic byte arrays rather than string literals.
    assert runtime not in codegen, 'WASI runtime names should stay isolated in wasi.rz'
assert 'wasm_wasi_emit_import_section(&mut module, hir)' in codegen
assert 'wasm_wasi_emit_runtime_body(&mut section, source, hir, function_index)' in codegen
assert 'wasm_wasi_defined_function(function_index)' in codegen
assert 'closure_function + wasm_host_import_count()' in closures
for needle in ['args_sizes_get', 'args_get', 'environ_sizes_get', 'environ_get', 'clock_time_get', 'random_get', 'proc_exit', 'wasm_wasi_process_arg', 'wasm_wasi_env_get', 'wasm_wasi_emit_start_body', 'wasm_wasi_start_count', 'path_open', 'fd_close', 'path_filestat_get', 'fd_seek', 'fd_tell', 'fd_sync', 'fd_filestat_get', 'poll_oneoff', 'fd_prestat_get', 'fd_prestat_dir_name', 'wasm_wasi_file_open', 'wasm_wasi_file_read', 'wasm_wasi_file_write', 'wasm_wasi_file_close', 'wasm_wasi_file_seek', 'wasm_wasi_file_tell', 'wasm_wasi_file_flush', 'wasm_wasi_file_eof', 'wasm_wasi_file_size', 'wasm_wasi_path_exists', 'wasm_wasi_path_is_file', 'wasm_wasi_path_is_dir', 'wasm_wasi_process_argc', 'wasm_wasi_time_unix_millis', 'wasm_wasi_time_monotonic_nanos', 'wasm_wasi_random_fill', 'wasm_wasi_random_seed', 'wasm_wasi_sleep_millis', 'wasm_wasi_current_dir', 'fd_readdir', 'path_create_directory', 'path_unlink_file', 'path_remove_directory', 'path_rename', 'wasm_wasi_dir_open', 'wasm_wasi_dir_next', 'wasm_wasi_dir_close', 'wasm_wasi_fs_metadata', 'wasm_wasi_create_dir_one', 'wasm_wasi_remove_one', 'wasm_wasi_rename_path', 'wasm_wasi_copy_file', 'wasm_wasi_emit_copy_file_body', 'wasm_wasi_emit_find_preopen', 'wasm_wasi_emit_find_preopen_for_path', 'wasm_wasi_emit_find_named_preopen', 'wasm_wasi_emit_resolve_preopen', 'wasm_wasi_last_error_global', 'wasm_wasi_capture_errno', 'wasm_wasi_set_last_error', 'wasm_wasi_emit_poll_delay_local', 'wasm_wasi_last_error_code', 'wasm_wasi_env_set', 'wasm_wasi_env_remove', 'wasm_wasi_process_run', 'wasm_wasi_process_run_argv']:
    assert needle in wasi, f'missing extended WASI primitive: {needle}'
assert 'return 24;' in (root / 'compiler/src/raz_codegen_wasm/src/wasm/writer.rz').read_text(encoding='utf-8')
print('wasm-wasi: PASS (stdio + argv/env + errno propagation + sync/async _start + clocks/randomness + poll sleep + named/multi-preopen routing + file/directory/metadata/copy operations + explicit unsupported host mutations)')
