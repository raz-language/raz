#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import hashlib
import http.server
import os
import platform
import shutil
import subprocess
import tarfile
import threading
import zipfile
from pathlib import Path

from compiler_test_driver import build_test_compiler


def run(args: list[str], cwd: Path, env: dict[str, str], expect: int = 0) -> subprocess.CompletedProcess[str]:
    p = subprocess.run(args, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != expect:
        raise RuntimeError(
            f"command failed {p.returncode} expected {expect}: {' '.join(args)}\n"
            f"stdout:\n{p.stdout}\nstderr:\n{p.stderr}"
        )
    return p


def host_platform() -> str:
    system = platform.system().lower()
    machine = platform.machine().lower()
    if machine in {"amd64", "x86_64"}:
        arch = "x86_64"
    elif machine in {"arm64", "aarch64"}:
        arch = "aarch64"
    else:
        raise RuntimeError(f"unsupported test architecture: {machine}")
    if system == "windows":
        os_name = "windows"
    elif system == "linux":
        os_name = "linux"
    elif system == "darwin":
        os_name = "macos"
    else:
        raise RuntimeError(f"unsupported test OS: {system}")
    return f"{os_name}-{arch}"


def make_archive(serve: Path, version: str, target: str) -> Path:
    base = f"raz-{version}-{target}"
    payload = serve / "payload" / base
    (payload / "bin").mkdir(parents=True)
    (payload / "VERSION").write_text(version + "\n", encoding="utf-8")
    command = payload / ("raz.exe" if os.name == "nt" else "raz")
    command.write_text("fake raz toolchain\n", encoding="utf-8")
    if os.name != "nt":
        command.chmod(0o755)

    if os.name == "nt":
        archive = serve / f"{base}.zip"
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            for file in sorted(payload.rglob("*")):
                if file.is_file():
                    zf.write(file, file.relative_to(serve / "payload").as_posix())
    else:
        archive = serve / f"{base}.tar.gz"
        with tarfile.open(archive, "w:gz") as tf:
            tf.add(payload, arcname=base)
    return archive


def build_compiler(ns: argparse.Namespace, work: Path, env: dict[str, str]) -> Path:
    if ns.compiler:
        return Path(ns.compiler).resolve()
    return build_test_compiler(Path(ns.root).resolve(), work, ns.raz, ns.linker, env)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--compiler")
    ap.add_argument("--raz")
    ap.add_argument("--root")
    ap.add_argument("--work", required=True)
    ap.add_argument("--linker")
    ns = ap.parse_args()

    work = Path(ns.work).resolve()
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    env = os.environ.copy()
    compiler = build_compiler(ns, work / "compiler-build", env)

    command = work / ("razup.exe" if os.name == "nt" else "razup")
    shutil.copy2(compiler, command)
    if os.name != "nt":
        command.chmod(0o755)

    version = "9.9.9"
    target = host_platform()
    serve = work / "serve"
    serve.mkdir()
    archive = make_archive(serve, version, target)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()

    class QuietHandler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, fmt: str, *args: object) -> None:
            pass

        def redirect(self, target: str) -> None:
            host, port = self.server.server_address[:2]
            self.send_response(302)
            self.send_header("Location", f"http://{host}:{port}/{target}")
            self.end_headers()

        def do_GET(self) -> None:
            if self.path == "/stable.txt":
                self.redirect("published-stable.txt")
                return
            if self.path == "/artifact":
                self.redirect(archive.name)
                return
            super().do_GET()

    handler = lambda *args, **kwargs: QuietHandler(*args, directory=str(serve), **kwargs)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        port = server.server_address[1]
        url = f"http://127.0.0.1:{port}/artifact"
        (serve / "published-stable.txt").write_text(
            "schema=razup-channel-v1\n"
            "status=published\n"
            "channel=stable\n"
            f"version={version}\n"
            f"{target}.url={url}\n"
            f"{target}.sha256={digest}\n",
            encoding="utf-8",
        )
        (serve / "nightly.txt").write_text(
            "schema=razup-channel-v1\n"
            "status=published\n"
            "channel=nightly\n"
            f"version={version}\n"
            f"{target}.url={url}\n"
            f"{target}.sha256={'0' * 64}\n",
            encoding="utf-8",
        )

        home = work / "home"
        tool_env = env.copy()
        tool_env["RAZUP_HOME"] = str(home)
        tool_env["RAZUP_CHANNEL_BASE"] = f"http://127.0.0.1:{port}"

        version_result = run([str(command), "--version"], work, tool_env)
        assert "razup 1.0.0" in version_result.stdout

        invalid = run([str(command), "install", "../escape"], work, tool_env, expect=1)
        assert "invalid toolchain version" in invalid.stderr
        assert not (work / "escape-linux-x86_64").exists()

        update = run([str(command), "update", "stable"], work, tool_env)
        identity = f"{version}-{target}"
        assert f"installed toolchain: {identity}" in update.stdout
        assert f"default toolchain: {identity}" in update.stdout
        assert (home / "current" / "VERSION").read_text(encoding="utf-8").strip() == version
        assert (home / "toolchains" / identity / "VERSION").exists()
        assert not (home / "tmp").exists(), "temporary extraction directory was not cleaned"

        listing = run([str(command), "toolchain", "list"], work, tool_env).stdout
        assert f"{identity}|stable" in listing
        show = run([str(command), "show"], work, tool_env).stdout
        assert f"default: {identity}" in show
        current_bin = str(home / "current" / "bin")
        assert current_bin in run([str(command), "env"], work, tool_env).stdout

        bad = run([str(command), "install", "nightly"], work, tool_env, expect=1)
        assert "SHA-256 verification failed" in bad.stderr

        removed = run([str(command), "uninstall", version], work, tool_env)
        assert f"removed toolchain: {identity}" in removed.stdout
        assert not (home / "current").exists()
        assert not (home / "settings.txt").exists()
        assert identity not in (home / "toolchains.txt").read_text(encoding="utf-8")
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)

    print("razup-runtime: PASS (channel download + SHA-256 + extraction + selection + uninstall)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
