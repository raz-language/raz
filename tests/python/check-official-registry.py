#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXPECTED = "https://raw.githubusercontent.com/raz-language/packages/main"


def fail(message: str) -> None:
    raise SystemExit(f"official-registry: FAIL: {message}")


def main() -> int:
    transport = (ROOT / "compiler/src/raz_driver/src/registry_transport.rz").read_text(encoding="utf-8")
    registry = (ROOT / "compiler/src/raz_driver/src/registry.rz").read_text(encoding="utf-8")
    package = (ROOT / "compiler/src/raz_driver/src/package.rz").read_text(encoding="utf-8")
    cli = (ROOT / "compiler/src/raz_driver/src/cli.rz").read_text(encoding="utf-8")
    commands = (ROOT / "compiler/src/raz_driver/src/commands.rz").read_text(encoding="utf-8")
    main_source = (ROOT / "compiler/src/raz_driver/src/compiler_main.rz").read_text(encoding="utf-8")

    if f'string value = "{EXPECTED}";' not in transport:
        fail("could not locate registry_default_base literal")
    declared = len(EXPECTED)
    if f"capacity < {declared}" not in transport or f"while (i < {declared})" not in transport or f"return {declared};" not in transport:
        fail("registry_default_base length guards do not match its literal")

    if "package_add_official_registry_command" not in registry:
        fail("official registry add resolver is missing")
    for command, function in (("search", "registry_search_command"), ("info", "registry_info_command"), ("outdated", "registry_outdated_command")):
        if function not in registry:
            fail(f"{command} registry implementation is missing")
        if function not in commands:
            fail(f"{command} is not dispatched by the production CLI")
        if command not in cli:
            fail(f"{command} command is not discoverable in CLI source")
    if "driver_command_dispatch(cli_argc, cli_command" not in main_source:
        fail("production CLI does not invoke the auxiliary command dispatcher")
    if "fn package_registry_prepare_build(" not in registry:
        fail("ordinary project builds do not have an exact-lock dependency hydration preflight")
    if "package_registry_prepare_build(cli_manifest_path, cli_manifest_length)" not in main_source:
        fail("build/check/run/test do not automatically hydrate locked registry packages")
    if "registry_fetch_lock_data(lock_data, data_length)" not in registry:
        fail("build preflight does not hydrate exact lockfile entries")
    if "registry_official_publish_metadata" not in registry or "registry_official_publish_requested" not in registry:
        fail("official publish metadata validation is missing")
    if "old_length = raz_compiler_rt_read_ascii(fp, path_length, old, 1048576)" not in registry:
        fail("registry cache is not preserving existing dependency rows")
    if "registry_project_state_prepare(0, fp, 20)" not in registry:
        fail("registry cache is not using the canonical target/raz.cache path")
    if "bool official = argc == 3" not in package or "package_add_official_registry_command(alias, al, section_kind)" not in package:
        fail("raz add does not accept the one-argument official package form")
    if "fn registry_bare_constraint(" not in registry or "bool same_name = al == nl" not in registry:
        fail("same-name official dependencies do not use compact manifest constraints")
    if "registry_bare_constraint(dependency, dependency_length)" not in (ROOT / "compiler/src/raz_driver/src/project.rz").read_text(encoding="utf-8"):
        fail("project loader does not resolve compact registry constraints through raz.lock")
    if "prepared_submission = true" not in registry or 'cli_write_literal("Prepared ")' not in registry:
        fail("credential-free raz publish does not preserve PR-ready staging")
    if "registry_github_publish_archive(name, nl, version, vl, archive, al)" not in registry:
        fail("official publishing is not wired to GitHub Contents publishing")
    if "fn registry_publish_token(" not in transport or "GITHUB_TOKEN" not in transport:
        # The source stores environment names as byte arrays; accept the byte-level marker below.
        github_token_bytes = "i64 github_key[12] = [71, 73, 84, 72, 85, 66, 95, 84, 79, 75, 69, 78]"
        if github_token_bytes not in transport:
            fail("GitHub token fallback is missing from registry transport")
    if "fn registry_base64_encode(" not in transport:
        fail("GitHub Contents publishing has no in-Raz base64 encoder")

    publish_path = bytes([116,97,114,103,101,116,47,112,117,98,108,105,115,104,47]).decode("ascii")
    if publish_path != "target/publish/":
        fail("internal test error decoding publish path")
    if 'string target = "target/";' not in transport or "registry_path_prefix_literal(path, length, target)" not in transport:
        fail("target/ is not excluded from deterministic package trees")

    print(f"official-registry: PASS ({EXPECTED}; static GitHub reads + authenticated Contents publishing + PR fallback)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
