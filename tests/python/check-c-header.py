#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations
import argparse, subprocess, tempfile, shutil
from pathlib import Path

def run(cmd, cwd=None):
    return subprocess.run([str(x) for x in cmd], cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--raz', required=True); ns=ap.parse_args(); raz=Path(ns.raz).resolve()
    with tempfile.TemporaryDirectory(prefix='raz-c-header-') as td:
        root=Path(td); src=root/'export.rz'; hdr=root/'raz_export.h'; roundtrip=root/'roundtrip.rz'
        src.write_text('''@repr(C)\npublic struct RazPoint {\n    i32 x;\n    i32 y;\n    u8 tag[4];\n}\n\n@repr(C)\npublic enum RazColor {\n    Red = 1,\n    Green = 5,\n}\n\n@abi(C)\npublic fn raz_add(u32 left, u32 right) -> u32 {\n    return left + right;\n}\n\n@abi(C)\npublic fn raz_point_x(RazPoint*const point) -> i32 {\n    return point.x;\n}\n\npublic fn internal_helper(i32 value) -> i32 { return value; }\n''', encoding='utf-8')
        checked=run([raz,'check',src]); assert checked.returncode==0, checked.stdout
        gen=run([raz,'c-header',src,hdr]); assert gen.returncode==0, gen.stdout
        h=hdr.read_text(encoding='utf-8')
        required=['#pragma once','#include <stdint.h>','typedef struct RazPoint','int32_t x;','uint8_t tag[4];','typedef enum RazColor','Red = 1','uint32_t raz_add(uint32_t left, uint32_t right);','int32_t raz_point_x(const RazPoint* point);']
        for needle in required: assert needle in h, needle+' missing\n'+h
        assert 'internal_helper' not in h, h
        cc=shutil.which('cc') or shutil.which('clang')
        if cc:
            probe=root/'probe.c'; probe.write_text('#include "raz_export.h"\nint main(void) { RazPoint p = {0}; return (int)p.x; }\n', encoding='utf-8')
            c=run([cc,'-std=c11','-Wall','-Wextra','-Werror','-fsyntax-only',probe], cwd=root); assert c.returncode==0, c.stdout
        bg=run([raz,'bindgen',hdr,roundtrip]); assert bg.returncode==0, bg.stdout
        rt=run([raz,'check',roundtrip]); assert rt.returncode==0, rt.stdout
        r=roundtrip.read_text(encoding='utf-8'); assert 'public struct RazPoint' in r and '@abi(C)extern fn raz_add' in r, r

        # Package/directory mode combines explicit C exports from unopened source files,
        # preserves callback declarators, and ignores generated target/ state.
        pkg=root/'pkg'; (pkg/'src').mkdir(parents=True); (pkg/'target'/'generated').mkdir(parents=True)
        (pkg/'src'/'types.rz').write_text('''@repr(C)
public struct RazEvent {
    i32 code;
}
''', encoding='utf-8')
        (pkg/'src'/'api.rz').write_text('''@abi(C)
public fn raz_register(fn(i32, i8*const) -> void callback, i32 flags) -> i32 {
    return flags;
}
''', encoding='utf-8')
        (pkg/'target'/'generated'/'ignored.rz').write_text('''@abi(C)
public fn should_not_export() -> i32 { return 1; }
''', encoding='utf-8')
        package_header=root/'package.h'; package_roundtrip=root/'package_roundtrip.rz'
        pg=run([raz,'c-header',pkg,package_header]); assert pg.returncode==0, pg.stdout
        ph=package_header.read_text(encoding='utf-8')
        assert 'typedef struct RazEvent' in ph, ph
        assert 'int32_t raz_register(void (*callback)(int32_t, const int8_t*), int32_t flags);' in ph, ph
        assert 'should_not_export' not in ph, ph
        if cc:
            probe2=root/'package_probe.c'; probe2.write_text('#include "package.h"\nint main(void) { return 0; }\n', encoding='utf-8')
            c2=run([cc,'-std=c11','-Wall','-Wextra','-Werror','-fsyntax-only',probe2], cwd=root); assert c2.returncode==0, c2.stdout
        bg2=run([raz,'bindgen',package_header,package_roundtrip]); assert bg2.returncode==0, bg2.stdout
        rt2=run([raz,'check',package_roundtrip]); assert rt2.returncode==0, rt2.stdout
        pr=package_roundtrip.read_text(encoding='utf-8'); assert 'fn(i32, i8*const) callback' in pr, pr
    print('c-header: PASS (single-file/package exports + callbacks + C compiler syntax + bindgen round-trip)')
    return 0
if __name__=='__main__': raise SystemExit(main())
