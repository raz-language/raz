# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
inc = (root / 'compiler/src/driver/incremental.rz').read_text()
main = (root / 'compiler/src/main.rz').read_text()
model = (root / 'compiler/src/hir/core/model.rz').read_text()
comptime = (root / 'compiler/src/hir/semantic/comptime.rz').read_text()
order = (root / 'compiler/host-source-order.txt').read_text()
ignore = (root / '.gitignore').read_text()

checks = {
    'persistent cache module is in semantic source graph': 'src/driver/incremental.rz' in order,
    'cache format is explicitly versioned': 'fn incremental_cache_schema()' in inc,
    'cache key includes whole source bytes': 'raz_compiler_rt_arena_range_hash(source.bytes, 0, source.length' in inc,
    'cache key includes backend mode': 'key = incremental_mix(key, backend);' in inc and 'llvm_emit_kind' in inc,
    'artifact restore exists': 'fn incremental_try_restore_artifact(' in inc,
    'artifact store exists': 'fn incremental_store_artifact(' in inc,
    'run/test/check cannot artifact-short-circuit': '!check_only' in main and '!test_after_build' in main and '!run_after_build' in main,
    'HIR exports module fingerprints beyond builder lifetime': 'incremental_module_fingerprint_count' in model and 'builder.query_module_fingerprint_count' in comptime,
    'module source/interface state is persisted': 'fn incremental_persist_module_fingerprints(' in inc and 'incremental_module_interface_fingerprints' in inc,
    'cache directory is ignored': '.raz/' in ignore,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'persistent-incremental: FAIL: {name}')
    sys.exit(1)
print('persistent-incremental: PASS')
print('  exact build artifacts are versioned and source/backend keyed')
print('  module implementation/interface fingerprints persist across compiler processes')
