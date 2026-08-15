#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def run(args: list[str], *, env: dict[str, str] | None = None, expect: int | None = 0,
        input_bytes: bytes | None = None) -> subprocess.CompletedProcess:
    proc = subprocess.run(args, text=input_bytes is None, input=input_bytes,
                          capture_output=True, env=env)
    if expect is not None and proc.returncode != expect:
        print("command failed:", " ".join(args), file=sys.stderr)
        out = proc.stdout.decode(errors="replace") if isinstance(proc.stdout, bytes) else proc.stdout
        err = proc.stderr.decode(errors="replace") if isinstance(proc.stderr, bytes) else proc.stderr
        print(out, file=sys.stderr)
        print(err, file=sys.stderr)
        raise SystemExit(1)
    return proc


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        print(f"cli-output-diagnostics: FAIL: missing {label}: {needle!r}", file=sys.stderr)
        print(text, file=sys.stderr)
        raise SystemExit(1)


def lsp_wire(messages: list[dict]) -> bytes:
    wire = bytearray()
    for message in messages:
        body = json.dumps(message, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        wire.extend(f"Content-Length: {len(body)}\r\n\r\n".encode("ascii"))
        wire.extend(body)
    return bytes(wire)


def lsp_responses(raw: bytes) -> list[dict]:
    responses: list[dict] = []
    offset = 0
    while offset < len(raw):
        header_end = raw.find(b"\r\n\r\n", offset)
        if header_end < 0:
            break
        header = raw[offset:header_end].decode("ascii", errors="replace")
        length = None
        for line in header.split("\r\n"):
            if line.lower().startswith("content-length:"):
                length = int(line.split(":", 1)[1].strip())
                break
        if length is None:
            raise SystemExit("cli-output-diagnostics: FAIL: malformed LSP response header")
        body_start = header_end + 4
        body = raw[body_start:body_start + length]
        responses.append(json.loads(body.decode("utf-8")))
        offset = body_start + length
    return responses


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raz", required=True)
    parser.add_argument("--razc", required=True)
    parser.add_argument("--work", required=True)
    args = parser.parse_args()

    work = Path(args.work)
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    bad = work / "bad.rz"
    bad.write_text("fn main() -> i64 {\n    i64 value = missing_name;\n    return value;\n}\n", encoding="utf-8")

    env = os.environ.copy()
    env["RAZ_COLOR"] = "never"
    diagnostic = run([args.razc, str(bad)], env=env, expect=None)
    if diagnostic.returncode == 0:
        print("cli-output-diagnostics: FAIL: invalid program unexpectedly succeeded", file=sys.stderr)
        return 1
    rendered = diagnostic.stderr
    require(rendered, "error[D2008]: unknown name 'missing_name'", "diagnostic header")
    require(rendered, f"--> {bad}:2:17", "source location")
    require(rendered, "i64 value = missing_name;", "source excerpt")
    require(rendered, "^~~~~~~~~~~~", "source marker")
    require(rendered, "= help:", "diagnostic help")
    if "\x1b[" in rendered:
        print("cli-output-diagnostics: FAIL: color leaked into RAZ_COLOR=never diagnostics", file=sys.stderr)
        return 1

    short = run([args.razc, "--diagnostic-format", "short", str(bad)], env=env, expect=None)
    require(short.stderr, f"{bad}:2:17: error[D2008]: unknown name 'missing_name'", "short diagnostic")

    machine = run([args.razc, "--diagnostic-format", "json", str(bad)], env=env, expect=None)
    if machine.stderr.strip():
        print("cli-output-diagnostics: FAIL: JSON diagnostics wrote to stderr", file=sys.stderr)
        return 1
    report = json.loads(machine.stdout)
    if report.get("schema") != "raz-diagnostics-v1" or report.get("error_count") != 1:
        print("cli-output-diagnostics: FAIL: invalid direct diagnostic schema", file=sys.stderr)
        return 1
    item = report["diagnostics"][0]
    if item["file"] != str(bad) or item["code"] != "D2008" or item["range"]["start"]["line"] != 1:
        print("cli-output-diagnostics: FAIL: direct diagnostic source mapping", file=sys.stderr)
        return 1

    fix_source = work / "fix.rz"
    fix_source.write_text("fn main() -> i64 {\n    i64 value = 1\n    return value;\n}\n", encoding="utf-8")
    fix = run([args.razc, "--diagnostic-format", "json", str(fix_source)], env=env, expect=None)
    fix_report = json.loads(fix.stdout)
    fixes = fix_report["diagnostics"][0].get("fixes", [])
    if not fixes or fixes[0].get("replacement") != ";":
        print("cli-output-diagnostics: FAIL: parser insertion fix missing", file=sys.stderr)
        return 1

    warning_source = work / "warning.rz"
    warning_source.write_text(
        "fn main() -> i64 {\n    i64 value = 7;\n    i64 copied = move value;\n    return copied;\n}\n",
        encoding="utf-8",
    )
    warned = run([args.razc, "--check", str(warning_source)], env=env, expect=0)
    require(warned.stderr, "warning[D2052]", "default compiler warning")
    denied = run([args.razc, "--check", "--deny", "D2052", str(warning_source)], env=env, expect=None)
    if denied.returncode == 0:
        print("cli-output-diagnostics: FAIL: --deny D2052 did not fail", file=sys.stderr)
        return 1
    require(denied.stderr, "error[D2052]", "denied compiler warning")
    allowed = run([args.razc, "--check", "--deny-warnings", "--allow", "D2052", str(warning_source)], env=env, expect=0)
    if "D2052" in allowed.stderr:
        print("cli-output-diagnostics: FAIL: ordered warning override did not suppress D2052", file=sys.stderr)
        return 1

    catalog = run([args.raz, "diagnostics", "--diagnostic-format", "json"], expect=0)
    catalog_json = json.loads(catalog.stdout)
    if catalog_json.get("schema") != "raz-diagnostic-catalog-v1" or "D2052" not in catalog_json.get("codes", []):
        print("cli-output-diagnostics: FAIL: diagnostic catalog missing D2052", file=sys.stderr)
        return 1
    if "semantic" not in {item.get("name") for item in catalog_json.get("categories", [])}:
        print("cli-output-diagnostics: FAIL: diagnostic catalog missing semantic category", file=sys.stderr)
        return 1

    project = work / "demo"
    created = run([args.raz, "new", str(project), "--color", "never"])
    require(created.stdout, "Created", "new status")

    colored = run([args.raz, "build", str(project), "--color", "always"])
    require(colored.stdout, "Finished", "build summary")
    if "\x1b[" not in colored.stdout:
        print("cli-output-diagnostics: FAIL: --color always did not emit ANSI styling", file=sys.stderr)
        return 1

    quiet = run([args.raz, "build", str(project), "--quiet", "--color", "never"])
    if quiet.stdout.strip() or quiet.stderr.strip():
        print("cli-output-diagnostics: FAIL: --quiet emitted output", file=sys.stderr)
        return 1

    project_main = project / "src" / "main.rz"
    project_main.write_text("fn main() -> i64 {\n    i64 value = missing_name;\n    return value;\n}\n", encoding="utf-8")
    project_json = run([args.raz, "check", str(project), "--force", "--diagnostic-format", "json"], expect=None)
    if project_json.stderr.strip():
        print("cli-output-diagnostics: FAIL: project JSON diagnostics wrote to stderr", file=sys.stderr)
        return 1
    aggregate = json.loads(project_json.stdout)
    if aggregate.get("schema") != "raz-project-diagnostics-v1" or aggregate.get("success") is not False:
        print("cli-output-diagnostics: FAIL: invalid project diagnostic schema", file=sys.stderr)
        return 1
    project_diag = aggregate["reports"][0]["diagnostics"][0]
    expected_byte = project_main.read_bytes().index(b"missing_name")
    if project_diag["file"] != str(project_main) or project_diag["range"]["start"]["line"] != 1 or project_diag["range"]["start"]["byte_offset"] != expected_byte:
        print("cli-output-diagnostics: FAIL: project diagnostic did not map back to source", file=sys.stderr)
        return 1

    # LSP must use the real compiler and UTF-16 positions. The emoji occupies
    # two UTF-16 code units but four UTF-8 bytes; a missing semicolon after it
    # catches accidental byte-column regressions.
    uri = "file:///tmp/raz-lsp-diagnostic.rz"
    lsp_text = 'fn main() -> i64 {\n    string text = "😀" i64 value = 1;\n    return 0;\n}\n'
    expected_character = len('    string text = "😀" '.encode("utf-16-le")) // 2
    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": uri, "languageId": "raz", "version": 1, "text": lsp_text}}},
        {"jsonrpc": "2.0", "id": 2, "method": "textDocument/codeAction", "params": {"textDocument": {"uri": uri}, "range": {"start": {"line": 0, "character": 0}, "end": {"line": 3, "character": 0}}, "context": {"diagnostics": []}}},
        {"jsonrpc": "2.0", "method": "textDocument/didClose", "params": {"textDocument": {"uri": uri}}},
        {"jsonrpc": "2.0", "id": 3, "method": "shutdown", "params": None},
        {"jsonrpc": "2.0", "method": "exit", "params": None},
    ]
    lsp = run([args.raz, "lsp"], input_bytes=lsp_wire(messages), expect=0)
    responses = lsp_responses(lsp.stdout)
    initialized = next(item for item in responses if item.get("id") == 1)
    sync = initialized["result"]["capabilities"]["textDocumentSync"]
    if not isinstance(sync, dict) or sync.get("openClose") is not True:
        print("cli-output-diagnostics: FAIL: LSP full-sync capability missing", file=sys.stderr)
        return 1
    publishes = [item for item in responses if item.get("method") == "textDocument/publishDiagnostics"]
    if len(publishes) < 2 or not publishes[0]["params"]["diagnostics"] or publishes[-1]["params"]["diagnostics"] != []:
        print("cli-output-diagnostics: FAIL: LSP publish/clear diagnostics behavior", file=sys.stderr)
        return 1
    lsp_diag = publishes[0]["params"]["diagnostics"][0]
    if lsp_diag.get("code") != "D1001" or lsp_diag["range"]["start"]["line"] != 1 or lsp_diag["range"]["start"]["character"] != expected_character:
        print("cli-output-diagnostics: FAIL: LSP compiler/UTF-16 diagnostic mapping", file=sys.stderr)
        print(json.dumps(lsp_diag, indent=2), file=sys.stderr)
        return 1
    actions = next(item for item in responses if item.get("id") == 2)["result"]
    if not any(action.get("kind") == "quickfix" and action.get("edit") for action in actions):
        print("cli-output-diagnostics: FAIL: LSP compiler quick-fix missing", file=sys.stderr)
        return 1

    print("cli-output-diagnostics: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
