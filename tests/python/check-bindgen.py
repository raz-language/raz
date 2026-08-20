#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations
import argparse, subprocess, tempfile
from pathlib import Path

def run(cmd, cwd=None):
    return subprocess.run([str(x) for x in cmd], cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--raz', required=True); ns=ap.parse_args()
    raz=Path(ns.raz).resolve()
    with tempfile.TemporaryDirectory(prefix='raz-bindgen-') as td:
        root=Path(td)
        header=root/'sample.h'
        header.write_text('''#define RAZ_MAGIC 42\n#define RAZ_MASK (1u << 3)\n#define RAZ_HEX 0x10UL\n#define RAZ_SKIP(x) ((x)+1)\n#define RAZ_FEATURE 1\n#if 0\nint raz_inactive(void);\n#else\nint raz_active_else(void);\n#endif\n#ifdef RAZ_FEATURE\nint raz_feature_on(void);\n#endif\n#ifndef RAZ_MISSING\nint raz_missing_off(void);\n#endif\n#if defined(RAZ_FEATURE)\n#define RAZ_SELECTED 0x20UL\n#endif\n#if !defined(RAZ_MISSING)\nint raz_neg_defined(void);\n#endif\n\ntypedef unsigned int raz_u32;\ntypedef const char *raz_cstr;\ntypedef void (*raz_callback)(int code, const char *message);\n\nenum Color {\n COLOR_RED = 1,\n COLOR_GREEN = 5,\n COLOR_BLUE\n};\n\nstruct RazPoint {\n int x;\n int y;\n};\n\nstruct RazOuter {\n int tag;\n struct { int x; int y; } point;\n union { unsigned int word; unsigned char bytes[4]; } payload;\n};\n\nstruct RazFlags {\n unsigned int read:1;\n unsigned int write:1;\n unsigned int execute:1;\n int owner;\n};\n\nunion RazBits {\n unsigned int word;\n unsigned char bytes[4];\n};\n\nraz_u32 raz_add(raz_u32 left, raz_u32 right);\nint raz_register_callback(raz_callback callback);\nlong raz_long_value(long value);\nraz_cstr raz_name(void);\n''', encoding='utf-8')
        unix=root/'unix.rz'; win=root/'windows.rz'
        for abi,out in [('unix',unix),('windows',win)]:
            r=run([raz,'bindgen',header,f'--target-abi={abi}',out])
            assert r.returncode==0, r.stdout
            c=run([raz,'check',out]); assert c.returncode==0, c.stdout
        u=unix.read_text(); w=win.read_text()
        required=['public const i64 RAZ_MAGIC = 42;','public const i64 RAZ_SELECTED = 0x20;','@abi(C)extern fn raz_active_else() -> i32;','@abi(C)extern fn raz_feature_on() -> i32;','@abi(C)extern fn raz_missing_off() -> i32;','@abi(C)extern fn raz_neg_defined() -> i32;','public const i64 RAZ_MASK = (1 << 3);','public const i64 RAZ_HEX = 0x10;','public enum Color','@repr(C)public struct RazPoint','@repr(C)public struct RazOuter','u32[2] point;','u32[1] payload;','u32 __raz_bitfield_storage_0;','i32 owner;','@repr(C)@align(4)public struct RazBits','fn(i32, i8*const) callback','@abi(C)extern fn raz_add(u32 left, u32 right) -> u32;']
        for needle in required: assert needle in u, needle+' missing\n'+u
        assert 'RAZ_SKIP' not in u
        assert 'raz_inactive' not in u
        assert 'raz_u32' not in u
        assert 'raz_long_value(i64 value) -> i64' in u
        assert 'raz_long_value(i32 value) -> i32' in w
        assert '@abi(C)extern fn raz_name() -> i8*const;' in u
    print('bindgen: PASS (aggregates/bitfields/conditionals/macros/typedefs/callbacks + Windows/Unix ABI + generated-source check)')
    return 0
if __name__=='__main__': raise SystemExit(main())
