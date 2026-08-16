#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re, sys
sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "tools"))
from compiler_sources import ordered_sources
source_text = '\n'.join(path.read_text(encoding='utf-8') for path in ordered_sources(root))
allow_path = root / 'tests' / 'data' / 'native-boundary-allowlist.txt'
allow = {line.strip() for line in allow_path.read_text().splitlines() if line.strip() and not line.lstrip().startswith('#')}

# These Stage-1 compatibility surfaces have been migrated into the Raz compiler.
# Their reappearance in native runtime code would be an architectural regression.
migrated_seed_symbols = {
    "raz_compiler_rt_arena_create", "raz_compiler_rt_arena_destroy",
    "raz_compiler_rt_arena_get", "raz_compiler_rt_arena_set",
    "raz_compiler_rt_arena_range_equal", "raz_compiler_rt_arena_range_hash",
    "raz_compiler_rt_arena_copy", "raz_compiler_rt_ref_create",
    "raz_compiler_rt_ref_get", "raz_compiler_rt_ref_set",
    "raz_compiler_rt_process_argc", "raz_compiler_rt_process_arg",
    "raz_compiler_rt_stdio_write_ascii", "raz_compiler_rt_env_get_ascii",
    "raz_compiler_rt_path_exists_ascii", "raz_compiler_rt_process_run_ascii",
    "raz_compiler_rt_process_run_argv_ascii", "raz_compiler_rt_process_run_argv_cwd_ascii",
    "raz_compiler_rt_tcp_connect_ascii", "raz_compiler_rt_socket_set_timeout_millis",
    "raz_compiler_rt_socket_send_ascii", "raz_compiler_rt_socket_receive_ascii",
    "raz_compiler_rt_socket_close", "raz_compiler_rt_tls_available",
    "raz_compiler_rt_tls_client_create_ascii", "raz_compiler_rt_tls_feed_ascii",
    "raz_compiler_rt_tls_drain_ascii", "raz_compiler_rt_tls_pending_encrypted",
    "raz_compiler_rt_tls_handshake", "raz_compiler_rt_tls_handshake_finished",
    "raz_compiler_rt_tls_write_plain_ascii", "raz_compiler_rt_tls_read_plain_ascii",
    "raz_compiler_rt_tls_shutdown", "raz_compiler_rt_tls_destroy",
    "raz_compiler_rt_write_ascii", "raz_compiler_rt_read_ascii",
    "raz_compiler_rt_normalize_path", "raz_compiler_rt_list_files_recursive",
    "raz_compiler_rt_tree_hash_ascii", "raz_compiler_rt_tree_list_ascii",
    "raz_compiler_rt_copy_tree_ascii", "raz_compiler_rt_create_dir_ascii",
    "raz_compiler_rt_remove_path_ascii",
    "raz_compiler_rt_tool_available", "raz_compiler_rt_host_platform",
    "raz_compiler_rt_ed25519_keygen_ascii", "raz_compiler_rt_ed25519_public_ascii",
    "raz_compiler_rt_ed25519_sign_ascii", "raz_compiler_rt_ed25519_verify_ascii",
}
runtime_root = root / "src" / "runtime"
runtime_files = sorted(runtime_root.glob("*.cpp"))
runtime_text = "\n".join(path.read_text(encoding="utf-8") for path in runtime_files)
native_seed = sorted(set(re.findall(r"\b(raz_compiler_rt_[A-Za-z0-9_]+)\s*\(", runtime_text)))
if native_seed:
    print("ERROR: Stage-1-specific native ABI is forbidden; keep Stage-1 adaptation in Raz:")
    for name in native_seed: print("  " + name)
    sys.exit(1)
regressed = sorted(name for name in migrated_seed_symbols if re.search(r"\b" + re.escape(name) + r"\s*\(", runtime_text))
if regressed:
    print("ERROR: migrated Stage-1 policy returned to native runtime:")
    for name in regressed: print("  " + name)
    sys.exit(1)
externs = set(re.findall(r'^extern\s+fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(', source_text, re.M))
unknown = sorted(externs - allow)
unused = sorted(allow - externs)
if unknown:
    print('ERROR: unclassified native ABI dependencies:')
    for name in unknown: print('  ' + name)
    sys.exit(1)
if unused:
    print('ERROR: stale native ABI allowlist entries:')
    for name in unused: print('  ' + name)
    sys.exit(1)
print(f'native-boundary: PASS ({len(externs)} permanent ABI shims)')
