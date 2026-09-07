#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Guard the growable writer path used by large browser Wasm modules."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMMON = ROOT / "compiler/src/raz_codegen_common/src/codegen_common/writer.rz"
WASM_WRITER = ROOT / "compiler/src/raz_codegen_wasm/src/wasm/writer.rz"
WASM_CODEGEN = ROOT / "compiler/src/raz_codegen_wasm/src/wasm/codegen.rz"
LEXER = ROOT / "compiler/src/raz_lexer/src/lexer/lexer.rz"


def require(text: str, needle: str, failures: list[str], label: str) -> None:
    if needle not in text:
        failures.append(f"{label}: missing {needle!r}")


def main() -> int:
    failures: list[str] = []
    common = COMMON.read_text(encoding="utf-8")
    writer = WASM_WRITER.read_text(encoding="utf-8")
    codegen = WASM_CODEGEN.read_text(encoding="utf-8")
    lexer = LEXER.read_text(encoding="utf-8")

    require(lexer, "public fn raz_compiler_rt_arena_resize(i64 handle, i64 new_count) -> i64", failures, "compiler arena")
    require(lexer, "raz_rt_realloc_aligned(payload, old_bytes, new_bytes, 8)", failures, "compiler arena")
    require(lexer, "return handle;", failures, "stable compiler arena resize")
    require(lexer, "raz_compiler_rt_arena_native_address", failures, "compiler arena native boundary")
    require(common, "public fn codegen_writer_reserve(CodegenWriter&mut out, i64 additional) -> bool", failures, "common writer")
    require(common, "public fn codegen_writer_append(CodegenWriter&mut out, CodegenWriter& source) -> bool", failures, "common writer")
    require(common, "raz_compiler_rt_arena_resize(out.data, next_capacity)", failures, "common writer")
    require(common, "raz_compiler_rt_arena_copy(out.data, out.length, source.data, 0, source.length)", failures, "common writer")
    require(common, "if (!codegen_writer_reserve(out, source.length)) { return false; }", failures, "common writer")
    if "raz_compiler_rt_arena_copy(next_data, 0, out.data" in common:
        failures.append("common writer: reserve regressed to allocate/copy/free growth")
    require(common, "if (!codegen_writer_reserve(out, 1)) { return; }", failures, "common writer")

    require(writer, "codegen_writer_append(out, section);", failures, "wasm writer")
    require(writer, "codegen_writer_reserve(out, section.length + 16)", failures, "wasm writer")
    if "while (i < section.length)" in writer:
        failures.append("wasm writer: section copy regressed to byte-at-a-time loop")

    require(codegen, "codegen_writer_init_capacity(&mut section, 131072);", failures, "wasm code section")
    require(codegen, "Reactive routing is already large enough to exceed 64 KiB", failures, "wasm code section")

    if failures:
        print("wasm-writer-growth: FAIL")
        for failure in failures:
            print("  " + failure)
        return 1
    print("wasm-writer-growth: PASS (stable arena resize + bulk append + >64KiB code-section coverage)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
