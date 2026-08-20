#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Static contracts for `raz run -- <args>` in the production Raz driver."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "compiler/src/main.rz").read_text(encoding="utf-8")
CLI = (ROOT / "compiler/src/driver/cli.rz").read_text(encoding="utf-8")

checks = {
    "production driver finds exact argument separator": "fn cli_argument_separator_index(i64 argc) -> i64" in CLI,
    "separator is restricted to run": "separator >= 0 && cli_command != 12" in CLI,
    "Raz parsing stops at separator": "*out_cli_argc = separator;" in CLI,
    "program argv starts after separator": "*out_first_program_argument = separator + 1;" in CLI,
    "forwarded argv is shell-free": "raz_compiler_rt_process_run_argv_ascii(" in CLI,
    "forwarded argv uses a NUL-separated blob": "raz_compiler_rt_arena_set(blob, cursor, 0);" in CLI,
    "forwarded argv count is preserved": "argument_count," in CLI,
    "program arguments bypass Raz 32-argument bound": "return *out_cli_argc <= 32;" in CLI,
    "incremental-cache run forwards program argv": "first_program_argument," in MAIN.split("cache_restore_kind != 0", 1)[1].split("cli_release_compilation_arenas", 1)[0],
    "normal native run forwards program argv": "first_program_argument," in MAIN.split("i64 exit_status = cli_maybe_run_artifact", 1)[1],
    "direct-source interpreter does not leak compiler argv": "run_after_build && direct_source && first_program_argument < process_argc" in MAIN,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    print("cli-run-arguments: FAIL")
    for name in failed:
        print(f"  {name}")
    raise SystemExit(1)

print(f"cli-run-arguments: PASS ({len(checks)} contracts)")
