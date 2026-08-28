#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
cli = (ROOT / 'compiler/src/raz_driver/src/cli.rz').read_text(encoding='utf-8')
project = (ROOT / 'compiler/src/raz_driver/src/project.rz').read_text(encoding='utf-8')

checks = {
    'status detail recognizes project manifests': 'path_has_toml_extension(detail, detail_length)' in cli,
    'status detail reads manifest contents': 'raz_compiler_rt_read_ascii(detail, detail_length, manifest, 65536)' in cli,
    'status detail renders package name': 'manifest_package_name(manifest, manifest_length, name, 4096)' in cli,
    'status detail renders package version': 'manifest_package_version(manifest, manifest_length, version, 256)' in cli,
    'status detail emits cargo-style v marker': 'cli_write_literal_stream(1, " v");' in cli,
    'status path falls back for direct source input': 'cli_write_arena_stream(1, detail, detail_length);' in cli,
    'project parser has package-version reader': 'fn manifest_package_version(' in project,
    'generic status routes through package-aware detail': 'cli_print_status_detail(detail, detail_length);' in cli,
    'dependency package status resolves identity marker': 'cli_find_package_identity(input, name, name_length' in cli,
    'dependency package status uses common name/version renderer': 'cli_print_status_name_version(kind, display_name, display_length, version, version_length);' in cli,
    'normal compilation announces every dependency package': 'cli_print_dependency_compile_statuses(cli_command, &input, cli_manifest_path, cli_manifest_length);' in (ROOT / 'compiler/src/raz_driver/src/compiler_main.rz').read_text(encoding='utf-8'),
    'project emits package version provenance': 'fn append_package_version_marker(' in project,
    'project emits source provenance': 'fn append_source_origin_marker_range(' in project,
    'update prints resolved package versions': 'cli_print_status_name_version(11, name, nl, version, vl);' in (ROOT / 'compiler/src/raz_driver/src/registry.rz').read_text(encoding='utf-8'),
    'update has a completion status helper': 'fn package_registry_update_finish(i64 status)' in (ROOT / 'compiler/src/raz_driver/src/registry.rz').read_text(encoding='utf-8'),
    'build has a linking status': 'cli_print_status(12, cli_manifest_path, cli_manifest_length);' in (ROOT / 'compiler/src/raz_driver/src/compiler_main.rz').read_text(encoding='utf-8'),
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'FAIL: {name}')
    raise SystemExit(1)
print(f'CLI package status: PASS ({len(checks)} checks)')
