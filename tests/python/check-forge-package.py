#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
forge = root / "src" / "forge"
required = [
    forge / "CMakeLists.txt",
    forge / "include/forge-c/forge.h",
    forge / "include/forge/ir/module.hpp",
    forge / "include/forge/ir/builder.hpp",
    forge / "include/forge/target/data_layout.hpp",
    forge / "include/forge/target/abi.hpp",
    forge / "src/capi/forge.cpp",
    forge / "src/target/data_layout.cpp",
    forge / "src/target/abi.cpp",
    forge / "src/transforms/scalar.cpp",
    forge / "src/machine/register_allocation.cpp",
    forge / "src/codegen/x86_64/encoder.cpp",
]
missing = [str(path.relative_to(root)) for path in required if not path.is_file()]
if missing:
    print("missing required C++ Forge production-backend files:", file=sys.stderr)
    for item in missing:
        print(f"  {item}", file=sys.stderr)
    raise SystemExit(1)

header = (forge / "include/forge-c/forge.h").read_text(encoding="utf-8")
match = re.search(r"#define\s+FORGE_C_API_VERSION\s+(\d+)", header)
if not match:
    raise SystemExit("Forge C API version macro is missing")

cmake = (forge / "CMakeLists.txt").read_text(encoding="utf-8")
if "add_library(forge" not in cmake or "add_library(Forge::forge ALIAS forge)" not in cmake:
    raise SystemExit("Forge library target/alias is missing")

print(f"Forge C++ production package complete (C API v{match.group(1)})")
