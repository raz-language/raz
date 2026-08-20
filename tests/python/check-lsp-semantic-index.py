#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import json
import subprocess
import sys
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
        length = None
        for line in header.split("\r\n"):
            if line.lower().startswith("content-length:"):
                length = int(line.split(":", 1)[1].strip())
                break
        if length is None:
            raise AssertionError(f"missing Content-Length in {header!r}")
        start = header_end + 4
        payload = data[start : start + length]
        result.append(json.loads(payload))
        offset = start + length
    return result


def by_id(messages: list[dict[str, Any]], identifier: int) -> dict[str, Any]:
    for message in messages:
        if message.get("id") == identifier:
            return message
    raise AssertionError(f"missing LSP response id={identifier}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raz", required=True)
    args = parser.parse_args()

    primary_uri = "file:///semantic.rz"
    primary = (
        "fn add(i64 left, i64 right) -> i64 {\n"
        "    i64 sum = left + right;\n"
        "    return sum;\n"
        "}\n\n"
        "fn main() -> i64 {\n"
        "    i64 sum = 9;\n"
        "    auto inferred = add(sum, 1);\n"
        "    return inferred;\n"
        "}\n"
    )
    lib_uri = "file:///lib.rz"
    lib = "fn helper() -> i64 { return 7; }\n"
    consumer_uri = "file:///consumer.rz"
    consumer = "fn consume() -> i64 { return helper(); }\n"
    bad_uri = "file:///bad.rz"
    bad = "fn bad() -> i64 {\n    i64 value = 1\n    return value;\n}\n"

    requests: list[dict[str, Any]] = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": primary_uri, "text": primary}}},
        {"jsonrpc": "2.0", "id": 2, "method": "textDocument/hover", "params": {"textDocument": {"uri": primary_uri}, "position": {"line": 7, "character": 22}}},
        {"jsonrpc": "2.0", "id": 3, "method": "textDocument/definition", "params": {"textDocument": {"uri": primary_uri}, "position": {"line": 7, "character": 22}}},
        {"jsonrpc": "2.0", "id": 4, "method": "textDocument/references", "params": {"textDocument": {"uri": primary_uri}, "position": {"line": 6, "character": 8}, "context": {"includeDeclaration": True}}},
        {"jsonrpc": "2.0", "id": 5, "method": "textDocument/rename", "params": {"textDocument": {"uri": primary_uri}, "position": {"line": 6, "character": 8}, "newName": "total"}},
        {"jsonrpc": "2.0", "id": 6, "method": "textDocument/completion", "params": {"textDocument": {"uri": primary_uri}, "position": {"line": 8, "character": 8}}},
        {"jsonrpc": "2.0", "id": 7, "method": "textDocument/semanticTokens/full", "params": {"textDocument": {"uri": primary_uri}}},
        {"jsonrpc": "2.0", "id": 8, "method": "textDocument/signatureHelp", "params": {"textDocument": {"uri": primary_uri}, "position": {"line": 7, "character": 29}}},
        {"jsonrpc": "2.0", "id": 9, "method": "textDocument/inlayHint", "params": {"textDocument": {"uri": primary_uri}, "range": {"start": {"line": 0, "character": 0}, "end": {"line": 20, "character": 0}}}},
        {"jsonrpc": "2.0", "id": 10, "method": "textDocument/documentHighlight", "params": {"textDocument": {"uri": primary_uri}, "position": {"line": 6, "character": 8}}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": lib_uri, "text": lib}}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": consumer_uri, "text": consumer}}},
        {"jsonrpc": "2.0", "id": 11, "method": "textDocument/definition", "params": {"textDocument": {"uri": consumer_uri}, "position": {"line": 0, "character": 29}}},
        {"jsonrpc": "2.0", "id": 12, "method": "textDocument/references", "params": {"textDocument": {"uri": lib_uri}, "position": {"line": 0, "character": 4}, "context": {"includeDeclaration": True}}},
        {"jsonrpc": "2.0", "id": 13, "method": "textDocument/rename", "params": {"textDocument": {"uri": lib_uri}, "position": {"line": 0, "character": 4}, "newName": "assist"}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": bad_uri, "text": bad}}},
        {"jsonrpc": "2.0", "id": 14, "method": "textDocument/codeAction", "params": {"textDocument": {"uri": bad_uri}, "range": {"start": {"line": 1, "character": 17}, "end": {"line": 1, "character": 17}}, "context": {"diagnostics": []}}},
        {"jsonrpc": "2.0", "method": "textDocument/didClose", "params": {"textDocument": {"uri": bad_uri}}},
        {"jsonrpc": "2.0", "id": 15, "method": "shutdown", "params": None},
        {"jsonrpc": "2.0", "method": "exit"},
    ]

    process = subprocess.run([args.raz, "lsp"], input=b"".join(frame(request) for request in requests), capture_output=True)
    if process.returncode != 0:
        print(process.stderr.decode(errors="replace"), file=sys.stderr)
        return 1
    messages = parse_frames(process.stdout)

    initialize = by_id(messages, 1)["result"]["capabilities"]
    legend = initialize["semanticTokensProvider"]["legend"]["tokenTypes"]
    assert legend == ["keyword", "function", "type", "variable", "parameter", "property", "enumMember", "namespace"]

    hover = by_id(messages, 2)["result"]
    assert "fn add(i64 left, i64 right) -> i64" in hover["contents"]["value"], hover

    definition = by_id(messages, 3)["result"]
    assert definition["uri"] == primary_uri and definition["range"]["start"] == {"line": 0, "character": 3}, definition

    refs = by_id(messages, 4)["result"]
    ref_lines = [entry["range"]["start"]["line"] for entry in refs]
    assert ref_lines == [6, 7], refs

    rename = by_id(messages, 5)["result"]["changes"][primary_uri]
    rename_lines = [entry["range"]["start"]["line"] for entry in rename]
    assert rename_lines == [6, 7], rename
    assert all(entry["newText"] == "total" for entry in rename)

    completion = by_id(messages, 6)["result"]["items"]
    completion_map = {item["label"]: item for item in completion}
    assert "add" in completion_map and "main" in completion_map and "sum" in completion_map
    assert "fn add(i64 left, i64 right) -> i64" in completion_map["add"]["detail"]

    tokens = by_id(messages, 7)["result"]["data"]
    assert tokens and len(tokens) % 5 == 0, tokens
    assert 1 in tokens[3::5] and 3 in tokens[3::5] and 4 in tokens[3::5], tokens

    signature = by_id(messages, 8)["result"]
    assert signature["signatures"][0]["label"] == "fn add(i64 left, i64 right) -> i64", signature

    hints = by_id(messages, 9)["result"]
    assert any("i64" in hint["label"] for hint in hints), hints

    highlights = by_id(messages, 10)["result"]
    assert [item["range"]["start"]["line"] for item in highlights] == [6, 7], highlights

    cross_definition = by_id(messages, 11)["result"]
    assert cross_definition["uri"] == lib_uri and cross_definition["range"]["start"] == {"line": 0, "character": 3}, cross_definition

    cross_refs = by_id(messages, 12)["result"]
    assert {entry["uri"] for entry in cross_refs} == {lib_uri, consumer_uri}, cross_refs

    cross_rename = by_id(messages, 13)["result"]["changes"]
    assert set(cross_rename) == {lib_uri, consumer_uri}, cross_rename
    assert all(edit["newText"] == "assist" for edits in cross_rename.values() for edit in edits)

    diagnostics = [message for message in messages if message.get("method") == "textDocument/publishDiagnostics" and message.get("params", {}).get("uri") == bad_uri]
    assert diagnostics and diagnostics[0]["params"]["diagnostics"], diagnostics
    assert diagnostics[0]["params"]["diagnostics"][0]["code"] == "E0002", diagnostics[0]
    actions = by_id(messages, 14)["result"]
    assert any(action.get("kind") == "quickfix" and any(edit.get("newText") == ";" for edits in action.get("edit", {}).get("changes", {}).values() for edit in edits) for action in actions), actions
    assert diagnostics[-1]["params"]["diagnostics"] == [], diagnostics[-1]

    print("lsp-semantic-index: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
