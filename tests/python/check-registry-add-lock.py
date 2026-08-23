#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
package = (ROOT / 'compiler/src/driver/package.rz').read_text(encoding='utf-8')
registry = (ROOT / 'compiler/src/driver/registry.rz').read_text(encoding='utf-8')
project = (ROOT / 'compiler/src/driver/project.rz').read_text(encoding='utf-8')
incremental = (ROOT / 'compiler/src/driver/incremental.rz').read_text(encoding='utf-8')

checks = {
    'root raz.toml lock path uses exact 10-byte length': 'package_lock_collect(\n        mp,\n        10,' in package,
    'dependency rewrite writes exact 10-byte raz.toml path': 'raz_compiler_rt_write_ascii(mp, 10, out, cursor)' in package,
    'registry dependencies can resolve through derived cache': 'public fn registry_cached_root(' in registry,
    'registry cache lookup reconstructs content-addressed path from checksum': 'registry_store_path(checksum, checksum_len, output, capacity)' in registry,
    'lock builder prefers resolved registry root before path join': 'i64 drl = registry_cached_root(dep_alias, dal, dep_root, 8192);' in package and 'if (drl <= 0)' in package,
    'official add caches resolution before lock rebuild': registry.find('s = registry_write_cache(') < registry.find('s = registry_rebuild_lock_from_resolved_cache()'),
    'legacy 11-byte root lock call removed': 'package_lock_collect(\n        mp,\n        11,' not in package,
    'legacy 11-byte manifest write removed': 'raz_compiler_rt_write_ascii(mp, 11, out, cursor)' not in package,
    'dep_alias cleanup remains scoped to lock collector allocation': package.count('i64 dep_alias = raz_compiler_rt_arena_create(4096);') == 1 and package.count('raz_compiler_rt_arena_destroy(dep_alias);') == 1,
    'tree metadata traversal resolves registry alias through verified store cache': 'i64 drl = registry_cached_root(alias, al, dep_root, 8192);' in package,
    'tree metadata traversal preserves path dependency fallback': package.count('if (drl <= 0) {') >= 2 and 'drl = path_join(root, root_length, dep, dl, dep_root, 8192);' in package,
    'build constraint validation resolves registry alias through verified store cache': 'i64 rl = registry_cached_root(alias, alias_length, root, 8192);' in registry,
    'build constraint validation preserves local path dependency fallback': 'i64 dl = registry_root_dependency_path(alias, alias_length, dependency, 8192);' in registry and 'rl = path_join(root, 1, dependency, dl, manifest_path, 8192);' in registry,
    'semver greater-equal remains inclusive': 'if (mode == 3) {\n        return cmp >= 0;' in registry,
    'project cache records sibling lockfile input': 'fn project_record_lock_input(' in project and 'project_record_input(state, lock_path, lock_length)' in project,
    'lockfile path join uses arena handle rather than array reference': 'i64 lock_name = raz_compiler_rt_arena_create(8);' in project and 'path_join(root, root_length, lock_name, 8, lock_path, 8192)' in project and '&lock_name' not in project,
    'project assembly records lockfile after manifest root resolution': '!project_record_lock_input(state, root, root_length)' in project,
    'lockfile input is optional for path-only projects': 'raz_compiler_rt_path_exists_ascii(lock_path, lock_length) == 0' in project,
    'incremental cache schema invalidates pre-target-layout caches': 'fn incremental_cache_schema() -> i64 {\n    return 6;\n}' in incremental,
    'project package cache lives directly under target': 'string bytes = "target/raz.cache";' in registry,
    'project registry tracking lives directly under target': 'string bytes = "target/raz.registry";' in registry,
    'legacy package-manager state is migrated on first access': 'fn registry_project_state_prepare(' in registry and 'raz_compiler_rt_copy_file_ascii(r16, r16_length, output, length)' in registry and 'raz_compiler_rt_copy_file_ascii(root_legacy, root_length, output, length)' in registry,
    'package lock cache lookup uses canonical project-state helper': 'registry_project_state_prepare(0, path, 20)' in registry,
    'Git materializations live under target': 'string parent_bytes = "./target/git";' in package,
    'ordinary build preflight rehydrates Git cache': 'status = package_git_fetch_tracked();' in registry,
    'locked registry packages reuse shared store before index lookup': registry.find('registry_store_path(checksum, checksum_length, locked_store, 8192)') < registry.find('registry_resolve_mode(\n        name,\n        name_length,\n        version,'),
    'offline locked build avoids registry index when shared store is present': 'if (registry_offline()) {\n        return 64;' in registry,
    'project assembly cache fallback uses canonical target/raz.cache path': 'fn project_registry_cache_path(' in project and 'i64 cache_path_length = project_registry_cache_path(cache_path, 20);' in project,
}
failed=[name for name,ok in checks.items() if not ok]
if failed:
    for name in failed: print('FAIL:', name)
    raise SystemExit(1)
print(f'registry add/lock transaction: PASS ({len(checks)} checks)')
