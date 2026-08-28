#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""End-to-end protocol qualification for the Raz-written production LSP."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from typing import Any


def frame(message: dict[str, Any]) -> bytes:
    payload = json.dumps(message, separators=(",", ":")).encode("utf-8")
    return f"Content-Length: {len(payload)}\r\n\r\n".encode("ascii") + payload


def parse_frames(data: bytes) -> list[dict[str, Any]]:
    messages: list[dict[str, Any]] = []
    offset = 0
    while offset < len(data):
        header_end = data.find(b"\r\n\r\n", offset)
        if header_end < 0:
            raise AssertionError(f"truncated LSP header at byte {offset}: {data[offset:offset+120]!r}")
        header = data[offset:header_end].decode("ascii")
        length: int | None = None
        for line in header.split("\r\n"):
            if line.lower().startswith("content-length:"):
                length = int(line.split(":", 1)[1].strip())
        if length is None:
            raise AssertionError(f"missing Content-Length: {header!r}")
        start = header_end + 4
        end = start + length
        if end > len(data):
            raise AssertionError("truncated LSP payload")
        messages.append(json.loads(data[start:end]))
        offset = end
    return messages


def response(messages: list[dict[str, Any]], identifier: int) -> dict[str, Any]:
    for message in messages:
        if message.get("id") == identifier:
            return message
    raise AssertionError(f"missing response id={identifier}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raz", required=True)
    args = parser.parse_args()

    uri = "file:///production-lsp.rz"
    invalid = (
        "fn main() -> i64 {\n"
        "    i64 value = 1\n"
        "    return value;\n"
        "}\n"
    )
    valid = (
        "fn main() -> i64 {\n"
        "    i64 value = 1;\n"
        "    return value;\n"
        "}\n"
    )

    requests: list[dict[str, Any]] = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {"jsonrpc": "2.0", "method": "initialized", "params": {}},
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {"textDocument": {"uri": uri, "languageId": "raz", "version": 1, "text": invalid}},
        },
        {
            "jsonrpc": "2.0",
            "id": 2,
            "method": "textDocument/completion",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 1, "character": 4}},
        },
        {"jsonrpc": "2.0", "id": 3, "method": "textDocument/documentSymbol", "params": {"textDocument": {"uri": uri}}},
        {
            "jsonrpc": "2.0",
            "id": 4,
            "method": "textDocument/formatting",
            "params": {"textDocument": {"uri": uri}, "options": {"tabSize": 4, "insertSpaces": True}},
        },
        {"jsonrpc": "2.0", "id": 5, "method": "textDocument/foldingRange", "params": {"textDocument": {"uri": uri}}},
        {
            "jsonrpc": "2.0",
            "id": 6,
            "method": "textDocument/selectionRange",
            "params": {"textDocument": {"uri": uri}, "positions": [{"line": 1, "character": 4}]},
        },
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didChange",
            "params": {
                "textDocument": {"uri": uri, "version": 2},
                "contentChanges": [{"text": valid}],
            },
        },
        {"jsonrpc": "2.0", "method": "textDocument/didClose", "params": {"textDocument": {"uri": uri}}},
        {"jsonrpc": "2.0", "id": 7, "method": "shutdown", "params": None},
        {"jsonrpc": "2.0", "method": "exit"},
    ]

    process = subprocess.run(
        [args.raz, "lsp"],
        input=b"".join(frame(request) for request in requests),
        capture_output=True,
        timeout=20,
    )
    if process.returncode != 0:
        print(process.stderr.decode(errors="replace"), file=sys.stderr)
        return process.returncode or 1
    if process.stderr:
        raise AssertionError(f"LSP wrote to stderr: {process.stderr.decode(errors='replace')}")

    messages = parse_frames(process.stdout)
    capabilities = response(messages, 1)["result"]["capabilities"]
    assert capabilities["textDocumentSync"] == {"openClose": True, "change": 1}
    assert capabilities["documentFormattingProvider"] is True
    assert capabilities["documentSymbolProvider"] is True
    assert capabilities["foldingRangeProvider"] is True
    assert capabilities["selectionRangeProvider"] is True
    assert "completionProvider" in capabilities
    assert capabilities["codeActionProvider"]["codeActionKinds"] == ["quickfix", "source.fixAll.raz"]

    completions = response(messages, 2)["result"]["items"]
    labels = {item["label"] for item in completions}
    assert {"fn", "struct", "match", "return", "unsafe"} <= labels

    symbols = response(messages, 3)["result"]
    assert symbols and symbols[0]["name"] == "main" and symbols[0]["kind"] == 12

    edits = response(messages, 4)["result"]
    assert edits and edits[0]["newText"].startswith("fn main()")

    folds = response(messages, 5)["result"]
    assert folds and folds[0]["startLine"] == 0 and folds[0]["endLine"] >= 2

    selection = response(messages, 6)["result"]
    assert selection and selection[0]["range"]["start"] == {"line": 0, "character": 0}

    published = [
        message for message in messages
        if message.get("method") == "textDocument/publishDiagnostics"
        and message.get("params", {}).get("uri") == uri
    ]
    assert len(published) >= 3, published
    assert published[0]["params"]["diagnostics"], published[0]
    first = published[0]["params"]["diagnostics"][0]
    assert first["severity"] == 1
    assert first["code"] == "D1001"
    assert published[-2]["params"]["diagnostics"] == [], published[-2]
    assert published[-1]["params"]["diagnostics"] == [], published[-1]

    assert response(messages, 7)["result"] is None
    print("production-lsp: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
