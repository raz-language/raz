#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Regression contracts for bootstrap runtime dependency discovery."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BOOTSTRAP = ROOT / "tools" / "bootstrap.py"

spec = importlib.util.spec_from_file_location("raz_bootstrap", BOOTSTRAP)
if spec is None or spec.loader is None:
    raise SystemExit(f"could not load {BOOTSTRAP}")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def write_cache(build: Path, *, openssl: str, include_hint: str = "") -> None:
    lines = [f"RAZ_RUNTIME_OPENSSL_ENABLED:INTERNAL={openssl}"]
    if include_hint:
        lines.append(f"OPENSSL_INCLUDE_DIR:PATH={include_hint}")
    (build / "CMakeCache.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


checks = 0
with tempfile.TemporaryDirectory(prefix="raz-bootstrap-runtime-deps-") as raw:
    root = Path(raw)

    # Windows FindOpenSSL may leave an include-dir cache entry after finding
    # headers only. That must never be interpreted as runtime OpenSSL enablement.
    disabled = root / "disabled"
    disabled.mkdir()
    write_cache(disabled, openssl="OFF", include_hint=r"C:\\Strawberry\\c\\include")
    (disabled / "raz-runtime-link-deps.txt").write_text("", encoding="utf-8")
    if module.load_runtime_link_dependencies(disabled) != []:
        raise AssertionError("OpenSSL-disabled runtime unexpectedly acquired link dependencies")
    checks += 1

    enabled = root / "enabled"
    enabled.mkdir()
    ssl = enabled / "libssl.lib"
    crypto = enabled / "libcrypto.lib"
    ssl.write_bytes(b"ssl")
    crypto.write_bytes(b"crypto")
    write_cache(enabled, openssl="ON")
    (enabled / "raz-runtime-link-deps.txt").write_text(f"{ssl}\n{crypto}\n", encoding="utf-8")
    if module.load_runtime_link_dependencies(enabled) != [str(ssl), str(crypto)]:
        raise AssertionError("OpenSSL-enabled runtime did not preserve exported linker inputs")
    checks += 1

    broken = root / "broken"
    broken.mkdir()
    write_cache(broken, openssl="ON")
    (broken / "raz-runtime-link-deps.txt").write_text("", encoding="utf-8")
    try:
        module.load_runtime_link_dependencies(broken)
    except RuntimeError as exc:
        if "did not export both OpenSSL link dependencies" not in str(exc):
            raise
    else:
        raise AssertionError("OpenSSL-enabled runtime accepted an incomplete dependency manifest")
    checks += 1

    stale = root / "stale"
    stale.mkdir()
    (stale / "CMakeCache.txt").write_text(
        "OPENSSL_INCLUDE_DIR:PATH=C:/stale/include\n", encoding="utf-8"
    )
    try:
        module.load_runtime_link_dependencies(stale)
    except RuntimeError as exc:
        if "RAZ_RUNTIME_OPENSSL_ENABLED" not in str(exc):
            raise
    else:
        raise AssertionError("bootstrap accepted a cache without the authoritative runtime feature export")
    checks += 1

print(f"bootstrap runtime dependency contracts: {checks}/{checks} passed")
