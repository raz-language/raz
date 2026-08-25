#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]


def manifest(name: str, dependencies: list[str]) -> str:
    lines = [
        "[package]",
        f'name = "{name}"',
        'version = "1.0.0"',
        'kind = "executable"',
        'source = "src"',
        'entry = "src/main.rz"',
        "",
        "[dependencies]",
    ]
    for dependency in dependencies:
        path = (ROOT / "library" / dependency).resolve().as_posix()
        lines.append(f'{dependency} = "{path}"')
    lines.extend(
        [
            "",
            "[profile.debug]",
            "optimization = 0",
            "debug = true",
            "incremental = false",
            "",
        ]
    )
    return "\n".join(lines)


def run_case(raz: Path, work_root: Path, name: str, dependencies: list[str], source: str) -> None:
    case_root = work_root / name
    shutil.rmtree(case_root, ignore_errors=True)
    (case_root / "src").mkdir(parents=True)
    (case_root / "raz.toml").write_text(manifest(name, dependencies), encoding="utf-8", newline="\n")
    (case_root / "src" / "main.rz").write_text(source, encoding="utf-8", newline="\n")

    build = subprocess.run(
        [str(raz), "build", "raz.toml"],
        cwd=case_root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if build.returncode != 0:
        raise SystemExit(
            f"production-runtime-regressions: FAIL {name} build returned {build.returncode}\n"
            f"stdout:\n{build.stdout}\nstderr:\n{build.stderr}"
        )

    executable = case_root / "target" / "debug" / "bin" / (name + (".exe" if sys.platform == "win32" else ""))
    if not executable.is_file():
        raise SystemExit(f"production-runtime-regressions: FAIL {name} executable missing: {executable}")

    result = subprocess.run(
        [str(executable)],
        cwd=case_root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        raise SystemExit(
            f"production-runtime-regressions: FAIL {name} returned {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="Run production aggregate ownership regressions.")
    parser.add_argument("--raz", required=True, type=Path)
    parser.add_argument("--work-root", type=Path, default=ROOT / "target" / "production-runtime-regressions")
    args = parser.parse_args()

    raz = args.raz.resolve()
    if not raz.is_file():
        raise SystemExit(f"production-runtime-regressions: FAIL compiler missing: {raz}")

    aggregate_assignment = r'''// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

import alloc::string;

fn assign_string(String&mut output, String value) {
    *output = move value;
}

fn assign<T>(T&mut output, T value) {
    *output = move value;
}

fn main() -> i64 {
    String concrete = String::from("old");
    String concrete_value = String::from("hello");
    assign_string(&mut concrete, move concrete_value);
    if (!concrete.as_str().equals("hello")) {
        return 1;
    }

    String generic = String::from("old");
    String generic_value = String::from("world");
    assign<String>(&mut generic, move generic_value);
    if (!generic.as_str().equals("world")) {
        return 2;
    }
    return 0;
}
'''

    aggregate_globals = r'''// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

struct Pair {
    i64 left;
    i64 right;
}

global mut Pair pair = Pair {
    left: 1,
    right: 2
};
global mut i64 values[3] = [3, 4, 5];

fn main() -> i64 {
    if (pair.left != 1 || pair.right != 2 || values[1] != 4) {
        return 1;
    }
    pair = Pair {
        left: 8,
        right: 9
    };
    values = [10, 11, 12];
    if (pair.left != 8 || pair.right != 9 || values[2] != 12) {
        return 2;
    }
    return 0;
}
'''

    hash_map_remove = r'''// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

import alloc::string;
import alloc::string::hash;
import collections::hash_map;

fn main() -> i64 {
    HashMap<String, String> map = HashMap<String, String>::new();
    String key = String::from("hello");
    String value = String::from("world");

    if (!map.insert(move key, move value)) {
        return 1;
    }

    String lookup = String::from("hello");
    String removed = String::new();
    if (!map.try_remove(&lookup, &mut removed)) {
        return 2;
    }
    if (!removed.as_str().equals("world")) {
        return 3;
    }
    if (map.len() != 0) {
        return 4;
    }
    if (map.get_ptr(&lookup) != 0) {
        return 5;
    }

    HashMap<String, String> empty = HashMap<String, String>::new();
    if (empty.get_ptr(&lookup) != 0) {
        return 6;
    }
    return 0;
}
'''

    work_root = args.work_root.resolve()
    work_root.mkdir(parents=True, exist_ok=True)
    run_case(raz, work_root, "aggregate_reference_assignment", ["alloc"], aggregate_assignment)
    run_case(raz, work_root, "aggregate_globals", [], aggregate_globals)
    run_case(raz, work_root, "hash_map_string_remove", ["alloc", "core", "collections"], hash_map_remove)
    print("production-runtime-regressions: PASS (aggregate reference assignment + aggregate globals + HashMap<String,String> removal/destruction/missing-pointer lookup)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
