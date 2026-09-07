#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Keep canonical compiler packages nested while flattening only frozen Stage-0's seed view."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BOOTSTRAP = ROOT / "tools" / "bootstrap.py"

spec = importlib.util.spec_from_file_location("raz_bootstrap_layout_test", BOOTSTRAP)
assert spec is not None and spec.loader is not None
bootstrap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bootstrap)

packages = (
    ("raz_driver", "driver", "compiler_main.rz"),
    ("raz_lexer", "lexer", "lexer.rz"),
    ("raz_parser", "parser", "parser.rz"),
)

for package_name, implementation_dir, representative in packages:
    canonical = ROOT / "compiler" / "src" / package_name / "src"
    assert (canonical / "lib.rz").is_file(), f"{package_name}: missing src/lib.rz"
    assert (canonical / implementation_dir / representative).is_file(), (
        f"{package_name}: canonical implementation is not nested under src/{implementation_dir}/"
    )
    assert not (canonical / representative).exists(), (
        f"{package_name}: canonical implementation leaked back into flat src/"
    )

with tempfile.TemporaryDirectory(prefix="raz-stage0-seed-layout-") as temp:
    seed = Path(temp)
    bootstrap.prepare_seed_compiler_project(seed)

    # The copied canonical project starts nested.
    for package_name, implementation_dir, representative in packages:
        source = seed / "src" / package_name / "src"
        assert (source / implementation_dir / representative).is_file()
        assert not (source / representative).exists()

    bootstrap._flatten_stage0_seed_package_sources(seed)

    # Frozen Stage-0 sees the historical direct-module shape, but the facade is retained.
    for package_name, implementation_dir, representative in packages:
        source = seed / "src" / package_name / "src"
        assert (source / "lib.rz").is_file(), f"{package_name}: seed facade disappeared"
        assert (source / representative).is_file(), f"{package_name}: seed implementation was not flattened"
        assert not (source / implementation_dir).exists(), f"{package_name}: nested seed implementation survived"

    driver_lib = (seed / "src" / "raz_driver" / "src" / "lib.rz").read_text(encoding="utf-8")
    assert "public import raz_driver::compiler_main;" in driver_lib

# The transformation must never mutate canonical compiler source.
for package_name, implementation_dir, representative in packages:
    canonical = ROOT / "compiler" / "src" / package_name / "src"
    assert (canonical / implementation_dir / representative).is_file()
    assert not (canonical / representative).exists()

print("stage0-seed-source-layout: PASS (canonical nested; disposable Stage-0 view flat)")
