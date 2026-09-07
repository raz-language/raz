#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Source gate for browser-only Wasm function graph compaction."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WASM = ROOT / "compiler" / "src" / "raz_codegen_wasm" / "src" / "wasm"
writer = (WASM / "writer.rz").read_text(encoding="utf-8")
codegen = (WASM / "codegen.rz").read_text(encoding="utf-8")
closures = (WASM / "closures.rz").read_text(encoding="utf-8")
async_src = (WASM / "async.rz").read_text(encoding="utf-8")
runtime_memory = (WASM / "runtime_memory.rz").read_text(encoding="utf-8")
cfg = (WASM / "cfg.rz").read_text(encoding="utf-8")
wasi = (WASM / "wasi.rz").read_text(encoding="utf-8")

checks = {
    "persistent browser reachability map": "wasm_browser_reachable_functions" in writer,
    "HIR function old-to-compact map": "wasm_browser_function_map" in writer and "wasm_browser_function_ordinal" in writer,
    "closure old-to-compact map": "wasm_browser_closure_map" in writer and "wasm_browser_closure_ordinal" in writer,
    "async old-to-compact map": "wasm_browser_async_map" in writer and "wasm_browser_async_ordinal" in writer,
    "single defined-index translator": "fn wasm_browser_defined_ordinal" in writer and "fn wasm_defined_function_index" in writer and "fn wasm_defined_table_index" in writer,
    "layout teardown destroys arenas": "fn wasm_browser_release_layout" in writer and writer.count("raz_compiler_rt_arena_destroy(wasm_browser_") >= 4,
    "layout prepared before imports": "wasm_browser_prepare_layout(source, hir, mir) &&\n            wasm_browser_prepare_imports(source, hir, mir)" in codegen,
    "per-function type entries compacted": "emitted_function_types = wasm_browser_emitted_function_count" in codegen and "!wasm_browser_function_reachable(function_index)" in codegen,
    "function section emits compact count": "emitted_defined_count = wasm_browser_emitted_defined_count" in codegen,
    "code section skips unreachable definitions": "wasm_browser_mode() && !wasm_browser_function_reachable(function_index)" in codegen and "continue;" in codegen,
    "old trap-placeholder strategy removed": "Preserve the existing function-index mapping" not in codegen,
    "table sized from compact definitions": codegen.count("table_function_count = wasm_browser_emitted_defined_count") >= 2,
    "table element indices compact": "wasm_host_import_count() + function_index" in codegen,
    "direct function calls remapped": "wasm_u32(out, wasm_wasi_defined_function(raz_compiler_rt_arena_get(mir.op_a, ip)))" in codegen,
    "first-class function refs remapped": "wasm_defined_table_index(raz_compiler_rt_arena_get(mir.op_a, ip))" in codegen,
    "closure adapters remapped": "wasm_defined_table_index(old_defined_index)" in closures and "wasm_defined_function_index(closure_function)" in closures,
    "async poll table indices remapped": "wasm_defined_table_index(wasm_async_poll_defined_index" in async_src,
    "runtime allocator calls remapped": runtime_memory.count("wasm_defined_function_index(") >= 5,
    "WASI helper delegates to shared remap": "return wasm_defined_function_index(function_index);" in wasi,
    "browser table DCE is reachability aware": "wasm_browser_layout_ready()" in cfg and "wasm_browser_function_reachable(function_index)" in cfg,
    "synthesized realloc dependencies retained": "wasm_runtime_memory_realloc(source, hir, function_index)" in codegen and "wasm_runtime_memory_find_alloc" in codegen and "wasm_runtime_memory_find_dealloc" in codegen,
    "synthesized future dependencies retained": "wasm_future_create(source, hir, function_index)" in codegen and "wasm_future_destroy(source, hir, function_index)" in codegen,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f"web-wasm-function-compaction: missing {name}")
    raise SystemExit(1)

print(f"web-wasm-function-compaction: PASS ({len(checks)} layout/remap invariants)")
