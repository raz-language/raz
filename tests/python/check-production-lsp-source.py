#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Static ownership contract for the shipped Raz-written LSP command."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
CLI = (ROOT / "compiler/src/raz_driver/src/cli.rz").read_text(encoding="utf-8")
COMMANDS = (ROOT / "compiler/src/raz_driver/src/commands.rz").read_text(encoding="utf-8")
LSP = (ROOT / "compiler/src/raz_driver/src/lsp.rz").read_text(encoding="utf-8")
RUNTIME = (ROOT / "src/runtime/files_process.cpp").read_text(encoding="utf-8")
ORDER = {path.relative_to(ROOT / "compiler").as_posix() for path in list((ROOT / "compiler").rglob("*.rz"))}
BOOTSTRAP = (ROOT / "tools/bootstrap.py").read_text(encoding="utf-8")

checks = {
    "production CLI owns lsp command": 'string candidate_lsp = "lsp"' in CLI and 'if (cli_arg_equals_literal(value, length, "lsp"))' in CLI,
    "production dispatcher routes lsp": "return lsp_command(process_argc);" in COMMANDS,
    "LSP is in production source graph": "src/raz_driver/src/lsp.rz" in ORDER,
    "LSP uses native stdio boundary": "extern fn raz_rt_stdio_read" in LSP and "extern fn raz_rt_stdio_flush" in LSP,
    "LSP forces byte-exact stdio": "extern fn raz_rt_stdio_set_binary" in LSP and "raz_rt_stdio_set_binary(0)" in LSP and "raz_rt_stdio_set_binary(1)" in LSP and "_setmode(descriptor, _O_BINARY)" in RUNTIME,
    "LSP framing is Raz-owned": "fn lsp_read_message(" in LSP and "fn lsp_send(" in LSP,
    "LSP diagnostics use compiler HIR": "build_hir(source, &mut hir)" in LSP,
    "LSP formatting uses canonical formatter": "tooling_format_source(" in LSP,
    "LSP keeps unsaved documents in memory": "struct LspDocuments" in LSP and "fn lsp_document_set(" in LSP,
    "LSP indexes unopened workspace modules": "fn lsp_workspace_load(" in LSP and "raz_compiler_rt_list_files_recursive" in LSP,
    "LSP restores disk state after overlays close": "fn lsp_document_restore_disk(" in LSP,
    "LSP indexes resolved registry store packages": "fn lsp_lock_registry_dependencies(" in LSP and "registry_store_path(" in LSP,
    "LSP caches HIR-derived semantic summaries": "fn lsp_semantic_cache_rebuild(" in LSP and "semantic_counts" in LSP,
    "LSP semantic navigation is HIR-backed": "hir.local_function_indices" in LSP and "hir.parameter_function_indices" in LSP and "hir.function_name_offsets" in LSP,
    "LSP advertises semantic capabilities": "semanticTokensProvider" in LSP and "codeActionProvider" in LSP and all(handler in LSP for handler in ("lsp_send_hover(", "lsp_send_definition(", "lsp_send_references(", "lsp_send_rename(", "lsp_send_semantic_tokens(", "lsp_send_inlay_hints(", "lsp_send_workspace_symbols(")),
    "completion JSON uses label objects": 'fn lsp_send_completion' in LSP and 'string a = "{\\\"label\\\":"' in LSP and 'string z = ",\\\"kind\\\":14}"' in LSP,
    "LSP has invalid-workspace lexical fallback": "spelling && !valid && !target.local" in LSP,
    "bootstrap qualifies shipped LSP": "check-production-lsp.py" in BOOTSTRAP,
    "bootstrap qualifies semantic LSP": "check-lsp-semantic-index.py" in BOOTSTRAP,
    "bootstrap qualifies project LSP index": "check-lsp-project-index.py" in BOOTSTRAP,
    "bootstrap qualifies registry LSP index": "check-lsp-registry-index.py" in BOOTSTRAP,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    print("production-lsp-source: FAIL")
    for name in failed:
        print(f"  - {name}")
    sys.exit(1)
print(f"production-lsp-source: PASS ({len(checks)} contracts)")
