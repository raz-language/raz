#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re, sys
sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[1]
from selfhost_sources import ordered_sources
source_text = '\n'.join(path.read_text(encoding='utf-8') for path in ordered_sources(root))
allow_path = root / 'scripts' / 'native-boundary-allowlist.txt'
allow = {line.strip() for line in allow_path.read_text().splitlines() if line.strip() and not line.lstrip().startswith('#')}

# These Stage-1 compatibility surfaces have been migrated into the Raz compiler.
# Their reappearance in native runtime code would be an architectural regression.
migrated_stage1_symbols = {
    "raz_rt_stage1_arena_create", "raz_rt_stage1_arena_destroy",
    "raz_rt_stage1_arena_get", "raz_rt_stage1_arena_set",
    "raz_rt_stage1_arena_range_equal", "raz_rt_stage1_arena_range_hash",
    "raz_rt_stage1_arena_copy", "raz_rt_stage1_ref_create",
    "raz_rt_stage1_ref_get", "raz_rt_stage1_ref_set",
    "raz_rt_stage1_process_argc", "raz_rt_stage1_process_arg",
    "raz_rt_stage1_stdio_write_ascii", "raz_rt_stage1_env_get_ascii",
    "raz_rt_stage1_path_exists_ascii", "raz_rt_stage1_process_run_ascii",
    "raz_rt_stage1_tcp_connect_ascii", "raz_rt_stage1_socket_set_timeout_millis",
    "raz_rt_stage1_socket_send_ascii", "raz_rt_stage1_socket_receive_ascii",
    "raz_rt_stage1_socket_close", "raz_rt_stage1_tls_available",
    "raz_rt_stage1_tls_client_create_ascii", "raz_rt_stage1_tls_feed_ascii",
    "raz_rt_stage1_tls_drain_ascii", "raz_rt_stage1_tls_pending_encrypted",
    "raz_rt_stage1_tls_handshake", "raz_rt_stage1_tls_handshake_finished",
    "raz_rt_stage1_tls_write_plain_ascii", "raz_rt_stage1_tls_read_plain_ascii",
    "raz_rt_stage1_tls_shutdown", "raz_rt_stage1_tls_destroy",
    "raz_rt_stage1_write_ascii", "raz_rt_stage1_read_ascii",
    "raz_rt_stage1_normalize_path", "raz_rt_stage1_list_files_recursive",
    "raz_rt_stage1_tree_hash_ascii", "raz_rt_stage1_tree_list_ascii",
    "raz_rt_stage1_copy_tree_ascii", "raz_rt_stage1_create_dir_ascii",
    "raz_rt_stage1_remove_path_ascii",
    "raz_rt_stage1_tool_available", "raz_rt_stage1_host_platform",
    "raz_rt_stage1_ed25519_keygen_ascii", "raz_rt_stage1_ed25519_public_ascii",
    "raz_rt_stage1_ed25519_sign_ascii", "raz_rt_stage1_ed25519_verify_ascii",
}
runtime_root = root / "src" / "runtime"
runtime_files = sorted(runtime_root.glob("*.cpp"))
runtime_text = "\n".join(path.read_text(encoding="utf-8") for path in runtime_files)
native_stage1 = sorted(set(re.findall(r"\b(raz_rt_stage1_[A-Za-z0-9_]+)\s*\(", runtime_text)))
if native_stage1:
    print("ERROR: Stage-1-specific native ABI is forbidden; keep Stage-1 adaptation in Raz:")
    for name in native_stage1: print("  " + name)
    sys.exit(1)
regressed = sorted(name for name in migrated_stage1_symbols if re.search(r"\b" + re.escape(name) + r"\s*\(", runtime_text))
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
