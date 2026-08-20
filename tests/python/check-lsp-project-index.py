#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Exercise the production LSP's disk-backed project index."""
from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path
from typing import Any


def frame(message: dict[str, Any]) -> bytes:
    payload = json.dumps(message, separators=(",", ":")).encode("utf-8")
    return f"Content-Length: {len(payload)}\r\n\r\n".encode() + payload


def parse_frames(data: bytes) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    offset = 0
    while offset < len(data):
        header_end = data.find(b"\r\n\r\n", offset)
        if header_end < 0:
            break
        header = data[offset:header_end].decode("ascii", errors="replace")
        length = next(int(line.split(":", 1)[1].strip()) for line in header.split("\r\n") if line.lower().startswith("content-length:"))
        start = header_end + 4
        result.append(json.loads(data[start : start + length]))
        offset = start + length
    return result


def by_id(messages: list[dict[str, Any]], identifier: int) -> dict[str, Any]:
    return next(message for message in messages if message.get("id") == identifier)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raz", required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="raz-lsp-project-") as temporary:
        base = Path(temporary)
        root = base / "app"
        dependency = base / "core"
        root.mkdir()
        dependency.mkdir()
        (root / "raz.toml").write_text('[package]\nname = "lsp_project"\nversion = "0.1.0"\n\n[dependencies]\ncore = "../core"\n', encoding="utf-8")
        (dependency / "raz.toml").write_text('[package]\nname = "core"\nversion = "0.1.0"\n', encoding="utf-8")
        dependency_source = dependency / "core.rz"
        dependency_source.write_text("fn external_helper() -> i64 { return 11; }\n", encoding="utf-8")
        library = root / "lib.rz"
        consumer = root / "consumer.rz"
        library.write_text("fn helper() -> i64 { return 7; }\n", encoding="utf-8")
        consumer.write_text("fn consume() -> i64 { return helper() + external_helper(); }\n", encoding="utf-8")
        # Generated state must never leak into the semantic workspace.
        generated = root / "target" / "debug"
        generated.mkdir(parents=True)
        (generated / "ghost.rz").write_text("fn ghost() -> i64 { return 0; }\n", encoding="utf-8")

        root_uri = root.as_uri()
        lib_uri = library.as_uri()
        consumer_uri = consumer.as_uri()
        dependency_uri = dependency_source.as_uri()
        overlay = "fn helper2() -> i64 { return 9; }\n"
        requests = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"rootUri": root_uri}},
            # Neither source document is opened: these requests must use disk index state.
            {"jsonrpc": "2.0", "id": 2, "method": "textDocument/definition", "params": {"textDocument": {"uri": consumer_uri}, "position": {"line": 0, "character": 29}}},
            {"jsonrpc": "2.0", "id": 3, "method": "textDocument/references", "params": {"textDocument": {"uri": lib_uri}, "position": {"line": 0, "character": 4}, "context": {"includeDeclaration": True}}},
            {"jsonrpc": "2.0", "id": 4, "method": "textDocument/rename", "params": {"textDocument": {"uri": lib_uri}, "position": {"line": 0, "character": 4}, "newName": "assist"}},
            {"jsonrpc": "2.0", "id": 5, "method": "workspace/symbol", "params": {"query": "helper"}},
            {"jsonrpc": "2.0", "id": 6, "method": "workspace/symbol", "params": {"query": "ghost"}},
            {"jsonrpc": "2.0", "id": 11, "method": "textDocument/definition", "params": {"textDocument": {"uri": consumer_uri}, "position": {"line": 0, "character": 40}}},
            {"jsonrpc": "2.0", "id": 12, "method": "workspace/symbol", "params": {"query": "external_helper"}},
            # Overlay the indexed file, then close it. The disk declaration must return.
            {"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": lib_uri, "text": overlay}}},
            {"jsonrpc": "2.0", "id": 7, "method": "workspace/symbol", "params": {"query": "helper2"}},
            {"jsonrpc": "2.0", "method": "textDocument/didClose", "params": {"textDocument": {"uri": lib_uri}}},
            {"jsonrpc": "2.0", "id": 8, "method": "workspace/symbol", "params": {"query": "helper"}},
            {"jsonrpc": "2.0", "id": 9, "method": "workspace/symbol", "params": {"query": "helper2"}},
            {"jsonrpc": "2.0", "id": 10, "method": "shutdown", "params": None},
            {"jsonrpc": "2.0", "method": "exit"},
        ]
        process = subprocess.run([args.raz, "lsp"], input=b"".join(frame(request) for request in requests), capture_output=True)
        if process.returncode != 0:
            print(process.stderr.decode(errors="replace"))
            return 1
        messages = parse_frames(process.stdout)

        definition = by_id(messages, 2)["result"]
        assert definition["uri"] == lib_uri and definition["range"]["start"] == {"line": 0, "character": 3}, definition
        references = by_id(messages, 3)["result"]
        assert {entry["uri"] for entry in references} == {lib_uri, consumer_uri}, references
        rename = by_id(messages, 4)["result"]["changes"]
        assert set(rename) == {lib_uri, consumer_uri}, rename
        assert any(item["name"] == "helper" and item["location"]["uri"] == lib_uri for item in by_id(messages, 5)["result"])
        assert by_id(messages, 6)["result"] == [], "generated target/ source leaked into workspace index"
        dependency_definition = by_id(messages, 11)["result"]
        assert dependency_definition["uri"] == dependency_uri, dependency_definition
        assert [item["name"] for item in by_id(messages, 12)["result"]] == ["external_helper"]
        assert [item["name"] for item in by_id(messages, 7)["result"]] == ["helper2"]
        assert any(item["name"] == "helper" and item["location"]["uri"] == lib_uri for item in by_id(messages, 8)["result"])
        assert by_id(messages, 9)["result"] == [], "didClose failed to restore saved disk state"

    print("lsp-project-index: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
