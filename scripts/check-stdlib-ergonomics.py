#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys
root = Path(__file__).resolve().parents[1]
errors = []
def require(rel, needles):
    text = (root / rel).read_text(encoding='utf-8')
    for needle in needles:
        if needle not in text:
            errors.append(f"{rel}: missing {needle!r}")
def require_tree(rel, needles):
    base = root / rel
    files = [base] if base.is_file() else sorted(p for p in base.rglob('*') if p.is_file() and p.suffix in {'.cpp', '.hpp'})
    text = '\n'.join(p.read_text(encoding='utf-8') for p in files)
    for needle in needles:
        if needle not in text:
            errors.append(f"{rel}: missing {needle!r}")
require('library/alloc/string/string.rz', ['append_string', 'insert_bytes', 'remove_range', 'valid_utf8_bytes', 'is_valid_utf8'])
require('library/collections/vector/vector.rz', ['fn pop(Vector<T>&mut self) -> Option<T>', 'fn remove(Vector<T>&mut self, i64 index) -> Option<T>'])
require('library/collections/deque/deque.rz', ['fn pop_front(Deque<T>&mut self) -> Option<T>', 'fn pop_back(Deque<T>&mut self) -> Option<T>'])
require('library/collections/hash_set/hash_set.rz', ['fn take(HashSet<T>&mut self, T& value) -> Option<T>'])
require('library/collections/hash_map/hash_map.rz', ['fn remove(HashMap<K, V>&mut self, K& key) -> Option<V>'])
require('library/std/path/buf/buf.rz', ['public struct PathBuf', 'fn normalize(PathBuf&mut self)', 'fn push(PathBuf&mut self'])
require('library/std/random/random.rz', ['raz_rt_random_fill', 'public struct Rng', 'fn range_i64'])
require('library/std/env/owned/owned.rz', ['get_string', 'current_dir_string', 'Result<String, EnvError>'])
require('library/std/process/owned/owned.rz', ['argument(i64 index) -> Option<String>'])
require('library/std/fs/text/text.rz', ['read_to_string', 'InvalidUtf8', 'write_string'])
require('library/std/time/time.rz', ['public struct Duration', 'public struct Instant', 'public fn elapsed'])
require_tree('src/runtime', ['raz_rt_random_fill', 'raz_rt_random_seed'])
require_tree('src/bootstrap/compiler/lowering/hir_to_mir', ['return_move_path', 'erase_drop_flags'])
require('compiler/src/mir/lowering.rz', ['return_transfer_kind', 'Returning an owning non-Copy aggregate local/parameter transfers'])
require_tree('src/bootstrap/tools/raz', ['append_package_modules_dependency_order'])
if errors:
    print('stdlib ergonomics audit: FAIL')
    for error in errors: print('  ' + error)
    sys.exit(1)
print('stdlib ergonomics audit: PASS')
print('  collections: owning Option removals')
print('  owned stdlib: path/env/process/text/random/time')
print('  ownership: implicit move on owning returns + lexical drop flags')
