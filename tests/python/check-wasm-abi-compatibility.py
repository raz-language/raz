#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
memory = (ROOT / 'compiler/src/backend/wasm/memory.rz').read_text()
globals_ = (ROOT / 'compiler/src/backend/wasm/globals.rz').read_text()
async_ = (ROOT / 'compiler/src/backend/wasm/async.rz').read_text()
writer = (ROOT / 'compiler/src/backend/wasm/writer.rz').read_text()
runtime = (ROOT / 'compiler/src/backend/wasm/runtime_memory.rz').read_text()
doc = (ROOT / 'docs/WASM-ABI-v1.md').read_text()

checks = {
    'static base 1024': 'return 1024;' in memory,
    'aggregate slot 8': re.search(r'fn wasm_memory_slot_size\(\).*?return 8;', memory, re.S) is not None,
    'global ref base': 'return -1048577;' in memory and '-1048577 - global_index' in globals_,
    'reserved errno global': 'fn wasm_last_error_global_index()' in globals_ and 'return 1;' in globals_,
    'future header 64': 'return 64;' in async_,
    'future state +0': re.search(r'fn wasm_async_frame_state_offset\(\).*?return 0;', async_, re.S) is not None,
    'future result +8': re.search(r'fn wasm_async_frame_result_offset\(\).*?return 8;', async_, re.S) is not None,
    'future poll +16': re.search(r'fn wasm_async_frame_poll_offset\(\).*?return 16;', async_, re.S) is not None,
    'future slots +24': re.search(r'fn wasm_async_frame_slot_count_offset\(\).*?return 24;', async_, re.S) is not None,
    'future status +32': re.search(r'fn wasm_async_frame_status_offset\(\).*?return 32;', async_, re.S) is not None,
    'host import count stable': 'return 24;' in writer,
    'allocator metadata separated': '512' in runtime,
    'wasm32 ceiling guarded': '4294967295' in memory or '4294967295' in runtime,
    'abi spec identifies v1': '# Raz WebAssembly ABI v1' in doc,
    'abi spec reference tag': '-1048577' in doc,
    'abi spec future layout': '`+16`' in doc and '`+32`' in doc and '`+64`' in doc,
    'abi spec low memory': 'Bytes `0..511`' in doc and 'byte `512`' in doc and 'byte `1024`' in doc,
}
missing = [name for name, ok in checks.items() if not ok]
if missing:
    raise SystemExit('wasm-abi-compatibility: FAIL: ' + ', '.join(missing))
print('wasm-abi-compatibility: PASS (Raz WebAssembly ABI v1 implementation/layout contract)')
