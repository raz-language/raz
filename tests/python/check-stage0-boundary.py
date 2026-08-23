#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "src/bootstrap/tools/raz/main.cpp").read_text(encoding="utf-8")
CLI = (ROOT / "src/bootstrap/tools/raz/detail/cli_options.hpp").read_text(encoding="utf-8")
DETAIL = ROOT / "src/bootstrap/tools/raz/detail"
BOOTSTRAP = (ROOT / "tools/bootstrap.py").read_text(encoding="utf-8")
CMAKE = (ROOT / "src/bootstrap/CMakeLists.txt").read_text(encoding="utf-8")

assert 'command == "lsp"' not in MAIN
assert '"lsp", "raz lsp"' not in CLI
assert not (DETAIL / "lsp_server.hpp").exists()
assert not (DETAIL / "lsp_semantics.hpp").exists()
for removed in (
    "project_commands.hpp", "auxiliary_commands.hpp", "dispatch_helpers.hpp", "stage0_tooling_helpers.hpp",
):
    assert not (DETAIL / removed).exists(), f"production Stage-0 helper returned: {removed}"

PRODUCTION_COMMANDS = {
    "run", "test", "bench", "profile", "coverage", "fuzz", "new", "init", "clean",
    "metadata", "graph", "doctor", "cache", "lock", "verify", "fmt", "lint", "doc",
    "spec", "diagnostics", "sbom", "audit", "package", "publish", "install", "uninstall", "lsp",
}
for command in PRODUCTION_COMMANDS:
    assert f'options.command == "{command}"' not in MAIN
    assert f'{{"{command}",' not in CLI
assert 'options.command != "build" && options.command != "check" && options.command != "version"' in CLI
assert '#include "detail/auxiliary_commands.hpp"' not in MAIN
assert '#include "detail/project_commands.hpp"' not in MAIN
assert '#include "detail/stage0_tooling_helpers.hpp"' not in MAIN
assert '#include "detail/dispatch_helpers.hpp"' not in MAIN
assert 'raz-stage0' in BOOTSTRAP
assert 'OUTPUT_NAME "raz-stage0"' in CMAKE
assert 'OUTPUT_NAME "razc-stage0"' in CMAKE
print("stage0-boundary: PASS (Stage-0 CLI is build/check only; production tooling lives in Raz)")
