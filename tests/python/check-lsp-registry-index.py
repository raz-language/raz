#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Exercise production LSP indexing of resolved registry packages in the local store."""
from __future__ import annotations

import argparse, json, os, subprocess, tempfile
from pathlib import Path
from typing import Any


def frame(message: dict[str, Any]) -> bytes:
    payload=json.dumps(message,separators=(",",":")).encode()
    return f"Content-Length: {len(payload)}\r\n\r\n".encode()+payload


def parse_frames(data: bytes) -> list[dict[str, Any]]:
    out=[]; off=0
    while off < len(data):
        end=data.find(b"\r\n\r\n",off)
        if end<0: break
        header=data[off:end].decode("ascii",errors="replace")
        length=next(int(x.split(":",1)[1].strip()) for x in header.split("\r\n") if x.lower().startswith("content-length:"))
        start=end+4; out.append(json.loads(data[start:start+length])); off=start+length
    return out


def main() -> int:
    ap=argparse.ArgumentParser(); ap.add_argument("--raz",required=True); ns=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix="raz-lsp-registry-") as td:
        base=Path(td); app=base/"app"; home=base/"home"; app.mkdir(); (home/"store").mkdir(parents=True)
        checksum="0123456789abcdef"; pkg=home/"store"/checksum; pkg.mkdir()
        (pkg/"raz.toml").write_text('[package]\nname = "widget"\nversion = "1.2.3"\n',encoding="utf-8")
        dep=pkg/"widget.rz"; dep.write_text('fn registry_helper() -> i64 { return 19; }\n',encoding="utf-8")
        (app/"raz.toml").write_text('[package]\nname = "app"\nversion = "0.1.0"\n\n[dependencies]\nwidget = "^1.2.0"\n',encoding="utf-8")
        (app/"raz.lock").write_text('version = 1\n\n[[package]]\nname = "widget"\nversion = "1.2.3"\nkind = "static-library"\npath = "registry:'+checksum+'"\nsource = "registry"\nchecksum = "'+checksum+'"\n',encoding="utf-8")
        mainfile=app/"main.rz"; mainfile.write_text('fn main() -> i64 { return registry_helper(); }\n',encoding="utf-8")
        uri=mainfile.resolve().as_uri(); root=app.resolve().as_uri()
        text=mainfile.read_text(); col=text.index("registry_helper")+2
        messages=[
            {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":root,"capabilities":{}}},
            {"jsonrpc":"2.0","method":"initialized","params":{}},
            {"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":{"textDocument":{"uri":uri},"position":{"line":0,"character":col}}},
            {"jsonrpc":"2.0","id":3,"method":"workspace/symbol","params":{"query":"registry_helper"}},
            {"jsonrpc":"2.0","id":4,"method":"shutdown","params":{}},
            {"jsonrpc":"2.0","method":"exit","params":{}},
        ]
        env=os.environ.copy(); env["RAZ_HOME"]=str(home)
        proc=subprocess.run([str(Path(ns.raz).resolve()),"lsp"],input=b"".join(frame(m) for m in messages),stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env,cwd=app,timeout=30)
        if proc.returncode != 0: raise SystemExit(proc.stderr.decode(errors="replace"))
        frames=parse_frames(proc.stdout); byid={m.get("id"):m for m in frames if "id" in m}
        definition=byid[2].get("result")
        assert definition and definition.get("uri") == dep.resolve().as_uri(), definition
        symbols=byid[3].get("result") or []
        assert any(x.get("name")=="registry_helper" and x.get("location",{}).get("uri")==dep.resolve().as_uri() for x in symbols), symbols
    print("lsp-registry-index: PASS")
    return 0

if __name__ == "__main__": raise SystemExit(main())
