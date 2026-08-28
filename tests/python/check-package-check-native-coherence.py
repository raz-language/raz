#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse, os, shutil, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def run(cmd, cwd, env):
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raz", required=True)
    ap.add_argument("--work-root", required=True)
    args = ap.parse_args()
    raz = Path(args.raz).resolve(); work = Path(args.work_root).resolve()
    if work.exists(): shutil.rmtree(work)
    (work / "dep/src").mkdir(parents=True); (work / "app/src").mkdir(parents=True)
    (work / "dep/raz.toml").write_text('''[package]\nname="dep"\nversion="1.0.0"\nkind="static-library"\nentry="src/lib.rz"\n\n[dependencies]\n''', encoding="utf-8")
    dep = work / "dep/src/lib.rz"
    dep.write_text('''namespace dep;\npublic fn value() -> i64 { return 1; }\n''', encoding="utf-8")
    (work / "app/raz.toml").write_text('''[package]\nname="app"\nversion="1.0.0"\nkind="executable"\nentry="src/main.rz"\n\n[dependencies]\ndep="../dep"\n\n[profile.release]\noptimization=2\ndebug=false\nincremental=true\n''', encoding="utf-8")
    (work / "app/src/main.rz").write_text('''import dep;\nfn main() -> i64 { return dep::value() - 1; }\n''', encoding="utf-8")
    env = os.environ.copy(); env["RAZ_HOME"] = str(ROOT)
    runtime = ROOT / "build/release/src/runtime/libraz_runtime.a"
    if runtime.is_file(): env["RAZ_RUNTIME_LIBRARY"] = str(runtime)
    first = run([str(raz), "build", "--release", "--forge-native", "--forge-structured-only"], work / "app", env)
    if first.returncode != 0:
        print(first.stdout); return 1
    exe = work / "app/target/release/bin/app"
    initial = run([str(exe)], work / "app", env)
    if initial.returncode != 0:
        print("package-check-native-coherence: initial artifact is wrong"); return 1

    dep.write_text('''namespace dep;\npublic fn value() -> i64 { return 2; }\n''', encoding="utf-8")
    checked = run([str(raz), "check", "--release", "--forge-native", "--forge-structured-only"], work / "app", env)
    if checked.returncode != 0:
        print(checked.stdout); return 1
    rebuilt = run([str(raz), "build", "--release", "--forge-native", "--forge-structured-only"], work / "app", env)
    if rebuilt.returncode != 0:
        print(rebuilt.stdout); return 1
    if "Fresh dep v1.0.0" in rebuilt.stdout:
        print("package-check-native-coherence: stale dependency object was declared Fresh after check")
        print(rebuilt.stdout); return 1
    updated = run([str(exe)], work / "app", env)
    if updated.returncode != 1:
        print("package-check-native-coherence: native artifact did not incorporate dependency source changed before check")
        print(rebuilt.stdout); return 1
    marker = work / "app/target/release/packages/.units"
    if not marker.is_file() or " 3\n" not in marker.read_text(encoding="utf-8").splitlines()[0] + "\n":
        print("package-check-native-coherence: native package marker schema was not upgraded")
        return 1
    print("package-check-native-coherence: PASS (check fingerprints cannot make stale native package objects Fresh)")
    return 0

if __name__ == "__main__": raise SystemExit(main())
