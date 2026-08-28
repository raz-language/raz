#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Static contracts for fast one-generation bootstrap and optional fixed-point verification."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
bootstrap = (root / "tools" / "bootstrap.py").read_text(encoding="utf-8")
release = (root / "tools" / "run-release-gate-from-bootstrap.py").read_text(encoding="utf-8")
windows = (root / "docs" / "WINDOWS-BUILD.md").read_text(encoding="utf-8")
qualification = (root / "docs" / "RELEASE-QUALIFICATION.md").read_text(encoding="utf-8")

checks = {
    "normal bootstrap has one self-host generation": 'self_host_dir = qualification / "repro-1"' in bootstrap and "for generation in range" not in bootstrap,
    "second generation is explicit verification only": 'if args.verify_reproducibility:' in bootstrap and 'verify_dir = qualification / "repro-2"' in bootstrap,
    "verification flag is user controlled": '"--verify-reproducibility"' in bootstrap,
    "fixed-point comparison remains explicit when requested": "Compiler fixed-point verification failed" in bootstrap,
    "self-host project preserves target cache": "def prepare_self_host_build(" in bootstrap and 'if child.name == "target"' in bootstrap,
    "native rebuild invalidates self-host cache": "prepare_self_host_build(self_host_dir, compiler_project, stage0_rebuilt)" in bootstrap,
    "seed project preserves target cache": "def prepare_seed_compiler_project(" in bootstrap and 'if child.name == "target"' in bootstrap,
    "source edits invalidate whole-project artifact cache": "def _refresh_bootstrap_input_cache(" in bootstrap and '"artifact.bin"' in bootstrap and '"project.source"' in bootstrap,
    "bootstrap source stamp is content-addressed": "bootstrap-input.sha256" in bootstrap and "hashlib.sha256()" in bootstrap,
    "normal cached Stage-0 seed build is incremental": "if stage0_rebuilt:" in bootstrap and 'seed_command.append("--force")' in bootstrap,
    "seed build streams compiler diagnostics": 'f"Stage-0 compiler -> Raz seed (O{args.seed_opt})"' in bootstrap and "seed_command[1:]" in bootstrap,
    "seed artifact follows bin layout": 'compiler_project / "target" / seed_profile / "bin" / f"raz-compiler{EXE}"' in bootstrap,
    "all bootstrap generations use canonical profile layout": 'PROFILE_OUTPUT_DIRECTORIES = ("bin", "lib", "obj", "ir", "modules", "packages")' in bootstrap and 'stage_layout["obj"] / f"raz-compiler{OBJ}"' in bootstrap and 'stage_layout["bin"] / f"raz-compiler{EXE}"' in bootstrap and 'verify_layout["obj"] / f"raz-compiler{OBJ}"' in bootstrap and 'verify_layout["bin"] / f"raz-compiler{EXE}"' in bootstrap,
    "self-host generations use release profile": 'generation_args = [\n            "build",\n            "--release",' in bootstrap and 'verify_args = [\n                "build", "--release",' in bootstrap,
    "normal bootstrap does not force compiler phase tracing": 'RAZ_COMPILER_PHASE_TRACE' not in bootstrap,
    "self-host stages relocatable Forge support": "shutil.copy2(bridge, lib_dir / bridge.name)" in bootstrap and "shutil.copy2(forge, lib_dir / forge.name)" in bootstrap,
    "legacy bootstrap scratch is migrated away": 'BOOTSTRAP_LEGACY_SCRATCH_NAMES = {"host-source-order.txt", "stage1-diagnostic.txt"}' in bootstrap and 'remove_legacy_bootstrap_scratch(build_dir)' in bootstrap,
    "release helper uses retained production compiler": "qualification / 'release' / 'bin'" in release and "repro-1" not in release,
    "retained compiler uses public raz name": 'retain_user_facing_compiler(staged_profile)' in bootstrap and 'final_compiler = qualification / "release" / "bin" / f"raz{EXE}"' in bootstrap,
    "retained profile prunes empty artifact directories": 'prune_empty_directories(staged_profile)' in bootstrap,
    "build summary is relocatable and names object digest precisely": 'retained_relative = Path("release") / "bin" / f"raz{EXE}"' in bootstrap and 'f"Production compiler: {retained_relative.as_posix()}\\n"' in bootstrap and 'Self-host object SHA-256' in bootstrap,
    "Windows docs use retained release compiler": "target/bootstrap/release/bin/raz.exe" in windows,
    "release docs describe optional verification": "verify-reproducibility" in qualification,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    print("bootstrap-fixed-point: FAIL")
    for name in failed:
        print(f"  - {name}")
    sys.exit(1)
print(f"bootstrap-fixed-point: PASS ({len(checks)} contracts)")
