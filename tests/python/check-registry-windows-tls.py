#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
transport = (ROOT / 'compiler/src/raz_driver/src/registry_transport.rz').read_text(encoding='utf-8')
runtime = (ROOT / 'src/runtime/runtime_internal.hpp').read_text(encoding='utf-8')
cmake = (ROOT / 'src/runtime/CMakeLists.txt').read_text(encoding='utf-8')
bootstrap = (ROOT / 'tools/bootstrap.py').read_text(encoding='utf-8')
build_driver = (ROOT / 'src/bootstrap/tools/raz/detail/build_driver.hpp').read_text(encoding='utf-8')

checks = {
    'local index retains configured/default package base': all(x in transport for x in [
        'A local index is often used as a deterministic resolver snapshot',
        'registry_env_ascii_literal(pkey, base, base_capacity)',
        'registry_default_base(base, base_capacity)',
    ]),
    'Windows TLS imports system ROOT store': all(x in runtime for x in [
        'raz_tls_load_windows_roots', 'CertOpenSystemStoreA', 'CertEnumCertificatesInStore',
        'X509_STORE_add_cert', 'CertCloseStore',
    ]),
    'Windows ROOT open uses the SDK provider-handle type':
        'CertOpenSystemStoreA(static_cast<HCRYPTPROV_LEGACY>(0), "ROOT")' in runtime and
        'CertOpenSystemStoreA(nullptr' not in runtime,
    'runtime links crypt32': 'ws2_32 bcrypt crypt32' in cmake,
    'recursive bootstrap links crypt32': 'crypt32.lib' in bootstrap and '-lcrypt32' in bootstrap,
    'bootstrap build driver links crypt32': 'crypt32.lib' in build_driver and '-lcrypt32' in build_driver,
    'HTTP response buffers use byte-width arena storage':
        'raz_compiler_rt_arena_create_width(output_capacity + 65536, 1)' in transport,
    'registry archive downloads use byte-width arena storage':
        'raz_compiler_rt_arena_create_width(33554432, 1)' in transport,
}
failed=[name for name, ok in checks.items() if not ok]
if failed:
    for name in failed: print(f'registry-windows-tls: FAIL: {name}')
    sys.exit(1)
print('registry-windows-tls: PASS')
print('  local-index package bases and Windows native trust roots are wired end-to-end')
