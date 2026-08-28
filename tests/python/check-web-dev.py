#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time
import urllib.request
import json

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/examples/web/dev"


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def get(url: str, timeout: float = 1.0) -> str:
    with urllib.request.urlopen(url, timeout=timeout) as response:
        return response.read().decode("utf-8")


def wait_for(url: str, predicate, timeout: float = 12.0) -> str:
    deadline = time.time() + timeout
    last = ""
    while time.time() < deadline:
        try:
            last = get(url)
            if predicate(last):
                return last
        except Exception:
            pass
        time.sleep(0.15)
    raise RuntimeError(f"timeout waiting for {url}; last={last!r}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raz", required=True)
    ap.add_argument("--work-root", required=True)
    args = ap.parse_args()

    raz = Path(args.raz).resolve()
    work = Path(args.work_root).resolve()
    if work.exists():
        shutil.rmtree(work)
    shutil.copytree(FIXTURE, work)

    env = os.environ.copy()
    env["RAZ_HOME"] = str(ROOT)
    runtime = ROOT / "build/release/src/runtime/libraz_runtime.a"
    if runtime.is_file():
        env["RAZ_RUNTIME_LIBRARY"] = str(runtime)

    port = reserve_port()
    command = [str(raz), "dev", "--host=127.0.0.1", f"--port={port}"]
    proc = subprocess.Popen(
        command,
        cwd=work,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    try:
        base = f"http://127.0.0.1:{port}"
        status = wait_for(base + "/__raz/status", lambda value: value.strip() == "1:0")
        rich = json.loads(get(base + "/__raz/status.json"))
        if rich.get("version") != 1 or rich.get("failed") is not False or rich.get("reload") not in ("full", "css"):
            raise RuntimeError(f"invalid rich dev status: {rich!r}")
        html = get(base + "/")
        if "generation one" not in html or "/__raz/status" not in html:
            raise RuntimeError("initial page missing content or live-reload status client")

        source = work / "src/main.rz"
        source.write_text(source.read_text().replace("color: #123456;", "color: #654321;"))
        wait_for(base + "/__raz/status", lambda value: value.strip() == "2:0")
        rich = json.loads(get(base + "/__raz/status.json"))
        if rich.get("reload") != "css":
            raise RuntimeError(f"CSS-only rebuild did not classify as css hot refresh: {rich!r}")

        source.write_text(source.read_text().replace("generation one", "generation two"))
        wait_for(base + "/__raz/status", lambda value: value.strip() == "3:0")
        rich = json.loads(get(base + "/__raz/status.json"))
        if rich.get("version") != 3 or rich.get("failed") is not False or not isinstance(rich.get("build_ms"), int) or rich["build_ms"] < 0:
            raise RuntimeError(f"successful rebuild missing timing metadata: {rich!r}")
        if "generation two" not in get(base + "/"):
            raise RuntimeError("successful rebuild did not update served page")

        good = source.read_text()
        source.write_text(good + "\nthis is invalid raz\n")
        wait_for(base + "/__raz/status", lambda value: value.strip() == "3:1")
        rich = json.loads(get(base + "/__raz/status.json"))
        if rich.get("version") != 3 or rich.get("failed") is not True or not isinstance(rich.get("build_ms"), int):
            raise RuntimeError(f"failed rebuild missing rich status: {rich!r}")
        if "generation two" not in get(base + "/"):
            raise RuntimeError("failed rebuild did not preserve last successful bundle")

        source.write_text(good)
        wait_for(base + "/__raz/status", lambda value: value.strip() == "4:0")
        nested = get(base + "/client/route")
        if "generation two" not in nested:
            raise RuntimeError("History API fallback did not serve the application shell")
    except Exception as exc:
        print(f"web-dev: FAIL: {exc}")
        return 1
    finally:
        proc.terminate()
        try:
            output, _ = proc.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            proc.kill()
            output, _ = proc.communicate(timeout=2)
        if proc.returncode not in (None, 0, -15):
            print(output)

    print("web-dev: PASS (raz dev + watch/rebuild + timed rich status + CSS hot refresh + last-good bundle + reload recovery + SPA fallback)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
