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
    forge / "include/forge/platform/data_layout.hpp",
    forge / "include/forge/platform/abi.hpp",
    forge / "src/capi/forge.cpp",
    forge / "src/platform/data_layout.cpp",
    forge / "src/platform/abi.cpp",
    forge / "src/transforms/scalar.cpp",
    forge / "src/machine/register_allocation.cpp",
    forge / "src/codegen/x86_64/encoder.cpp",
    forge / "include/forge/codegen/aarch64/encoder.hpp",
    forge / "include/forge/codegen/aarch64/register_allocation.hpp",
    forge / "src/codegen/aarch64/register_allocation.cpp",
    forge / "src/codegen/aarch64/encoder.cpp",
    forge / "src/object/elf_aarch64.cpp",
    forge / "include/forge/object/macho.hpp",
    forge / "src/object/macho_aarch64.cpp",
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
