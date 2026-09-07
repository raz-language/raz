# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path


def web_codegen_source(root: Path) -> str:
    web = root / 'compiler' / 'src' / 'raz_codegen_web' / 'src' / 'web'
    names = ['support.rz', 'runtime_analysis.rz', 'browser_host_js.rz', 'runtime_js.rz', 'codegen.rz']
    return '\n'.join((web / name).read_text(encoding='utf-8') for name in names)


def web_bundle_source(root: Path) -> str:
    driver = root / 'compiler' / 'src' / 'raz_driver' / 'src' / 'driver'
    names = ['web_bundle.rz', 'web_bundle_finalize.rz', 'web_host_prune.rz']
    return '\n'.join((driver / name).read_text(encoding='utf-8') for name in names)
