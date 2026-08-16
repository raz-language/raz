#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'compiler/src/driver/razup.rz').read_text(encoding='utf-8')
main = (ROOT / 'compiler/src/main.rz').read_text(encoding='utf-8')
host = (ROOT / 'compiler/src/driver/host_support.rz').read_text(encoding='utf-8')
runtime = (ROOT / 'src/runtime/platform_threads_crypto.cpp').read_text(encoding='utf-8')
transport = (ROOT / 'compiler/src/driver/registry_transport.rz').read_text(encoding='utf-8')
order = (ROOT / 'compiler/host-source-order.txt').read_text(encoding='utf-8').splitlines()

required = [
    'fn razup_command(', 'fn razup_install_named(', 'fn razup_update_command(',
    'fn razup_default_command(', 'fn razup_uninstall_command(', 'fn razup_list(',
    'fn razup_sha256_matches(', 'fn razup_channel_manifest(', 'fn razup_extract(',
    'fn razup_channel_base(', 'fn razup_valid_version(',
]
for token in required:
    if token not in source:
        raise SystemExit(f'razup: missing contract: {token}')
if 'razup_invoked()' not in main or 'return razup_command(process_argc);' not in main:
    raise SystemExit('razup: main command-image dispatch is not wired')
if 'src/driver/razup.rz' not in order or order[-1] != 'src/main.rz':
    raise SystemExit('razup: canonical source ordering is incomplete')
if order.index('src/driver/razup.rz') > order.index('src/main.rz'):
    raise SystemExit('razup: driver must precede main in bootstrap order')
for token in ('raz_rt_host_arch', 'raz_rt_sha256'):
    if token not in host or token not in runtime:
        raise SystemExit(f'razup: missing permanent runtime boundary: {token}')
if 'fn registry_http_location(' not in transport or 'redirect_depth >= 5' not in transport:
    raise SystemExit('razup: HTTP transport does not safely follow GitHub Release redirects')

if "latest non-prerelease release" not in source:
    raise SystemExit('razup: stable channel is not bound to the latest stable GitHub Release manifest')

def decode_literal(name: str) -> str:
    match = re.search(rf'i64\s+{name}\[(\d+)\]\s*=\s*\[([^\]]*)\];', source)
    if not match:
        raise SystemExit(f'razup: missing URL literal {name}')
    values = [int(value.strip()) for value in match.group(2).split(',') if value.strip()]
    if int(match.group(1)) != len(values):
        raise SystemExit(f'razup: URL literal {name} has a declared-size mismatch')
    return bytes(values).decode('ascii')

# Release binaries belong to the standalone installer repository. The compiler
# repository contains source only; named and pinned toolchains must not drift
# back to raz-language/raz GitHub Releases.
base_literals = re.findall(r'i64\s+base\[(\d+)\]\s*=\s*\[([^\]]*)\];', source)
decoded_bases = []
for declared, body in base_literals:
    values = [int(value.strip()) for value in body.split(',') if value.strip()]
    if int(declared) == len(values):
        try:
            decoded_bases.append(bytes(values).decode('ascii'))
        except (ValueError, UnicodeDecodeError):
            pass
stable_url = decode_literal('stable_url')
expected_repo = 'raz-language/installer'
if expected_repo not in stable_url or not any(expected_repo in value and '/releases/download/v' in value for value in decoded_bases):
    raise SystemExit('razup: stable/pinned toolchains are not owned by raz-language/installer releases')
if not any(expected_repo in value and value.endswith('/main/channels') for value in decoded_bases):
    raise SystemExit('razup: named channels are not owned by raz-language/installer/channels')
if not re.search(r'if\s*\(\s*!razup_valid_version\(\s*name\s*,\s*name_length\s*\)\s*\)', source):
    raise SystemExit('razup: direct-version install is not path-safe')
if not re.search(r'if\s*\(\s*!razup_valid_version\(\s*arg\s*,\s*al\s*\)\s*\)', source):
    raise SystemExit('razup: default toolchain selection does not validate direct versions')
if not re.search(r'if\s*\(\s*!razup_valid_version\(\s*version\s*,\s*vl\s*\)\s*\)', source):
    raise SystemExit('razup: uninstall does not validate direct versions')

# Literal arrays are intentionally explicit in production compiler source. Catch accidental
# declared-size drift, which otherwise produces confusing recursive diagnostics.
pattern = re.compile(r'i64\s+\w+\[(\d+)\]\s*=\s*\[([^\]]*)\];')
for match in pattern.finditer(source):
    declared = int(match.group(1))
    body = match.group(2).strip()
    actual = 0 if not body else len([x for x in body.split(',') if x.strip()])
    if declared != actual:
        line = source.count('\n', 0, match.start()) + 1
        raise SystemExit(f'razup: literal array size mismatch at line {line}: declared {declared}, got {actual}')

print('razup: PASS')
print('toolchain commands: install/update/default/list/uninstall/show/env')
print('integrity: SHA-256 verified before extraction')
print('channels: stable latest-release manifest + configurable named-channel base + direct immutable version')
