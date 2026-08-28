# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

import importlib.util
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
bootstrap_path = ROOT / "tools" / "bootstrap.py"
bootstrap = bootstrap_path.read_text(encoding="utf-8")
runtime_cmake = (ROOT / "src" / "runtime" / "CMakeLists.txt").read_text(encoding="utf-8")
bridge = (ROOT / "src" / "bootstrap" / "compiler" / "backend" / "forge" / "forge_bridge.cpp").read_text(encoding="utf-8")
checks = {
    "bootstrap reads Raz-owned OpenSSL state": "RAZ_RUNTIME_OPENSSL_ENABLED" in bootstrap,
    "bootstrap prefers canonical runtime dependency paths": "RAZ_RUNTIME_OPENSSL_SSL_LIBRARY" in bootstrap and "RAZ_RUNTIME_OPENSSL_CRYPTO_LIBRARY" in bootstrap,
    "bootstrap repairs legacy caches without rebuilding Stage-0": "repair_runtime_dependency_cache" in bootstrap and "Refresh Stage-0 CMake metadata" in bootstrap,
    "runtime exports canonical dependency paths": "RAZ_RUNTIME_OPENSSL_SSL_LIBRARY" in runtime_cmake and "RAZ_RUNTIME_OPENSSL_CRYPTO_LIBRARY" in runtime_cmake,
    "runtime resolves imported target linker files": "IMPORTED_IMPLIB_RELEASE" in runtime_cmake and "IMPORTED_LOCATION_RELEASE" in runtime_cmake,
    "bootstrap stages dependency libraries": "raz_runtime_ssl" in bootstrap and "raz_runtime_crypto" in bootstrap,
    "bridge discovers installed dependency libraries": "raz_runtime_ssl" in bridge and "raz_runtime_crypto" in bridge,
    "no runtime dependency manifest": "raz-runtime-link-deps" not in bootstrap and "raz-runtime-link-deps" not in bridge,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("bootstrap-runtime-deps: FAIL: " + ", ".join(failed))

# Behavioral migration check: an OpenSSL-enabled cache with only Raz-owned
# dependency entries must be sufficient.  This reproduces the Windows failure
# mode where FindOpenSSL's OPENSSL_* cache variables are absent.
spec = importlib.util.spec_from_file_location("raz_bootstrap_runtime_deps", bootstrap_path)
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    ssl = root / ("ssl.lib" if module.IS_WINDOWS else "libssl.a")
    crypto = root / ("crypto.lib" if module.IS_WINDOWS else "libcrypto.a")
    ssl.write_bytes(b"ssl")
    crypto.write_bytes(b"crypto")
    (root / "CMakeCache.txt").write_text(
        "RAZ_RUNTIME_OPENSSL_ENABLED:INTERNAL=ON\n"
        f"RAZ_RUNTIME_OPENSSL_SSL_LIBRARY:FILEPATH={ssl}\n"
        f"RAZ_RUNTIME_OPENSSL_CRYPTO_LIBRARY:FILEPATH={crypto}\n",
        encoding="utf-8",
    )
    resolved = module.load_runtime_link_dependencies(root)
    if resolved != [str(ssl), str(crypto)]:
        raise SystemExit(f"bootstrap-runtime-deps: FAIL: canonical cache resolution returned {resolved!r}")

# Simulate a reusable cache created by an older bootstrap where OpenSSL was
# enabled but no dependency path was persisted.  Mock only the configure
# subprocess: the repair routine itself must choose a configure-only command,
# after which normal dependency loading must succeed.
with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    ssl = root / ("ssl.lib" if module.IS_WINDOWS else "libssl.a")
    crypto = root / ("crypto.lib" if module.IS_WINDOWS else "libcrypto.a")
    ssl.write_bytes(b"ssl")
    crypto.write_bytes(b"crypto")
    cache = root / "CMakeCache.txt"
    cache.write_text("RAZ_RUNTIME_OPENSSL_ENABLED:INTERNAL=ON\n", encoding="utf-8")
    commands = []
    original_run = module.run
    def fake_run(label, command, cwd=module.ROOT, env=None):
        commands.append((label, list(command)))
        cache.write_text(
            "RAZ_RUNTIME_OPENSSL_ENABLED:INTERNAL=ON\n"
            f"RAZ_RUNTIME_OPENSSL_SSL_LIBRARY:FILEPATH={ssl}\n"
            f"RAZ_RUNTIME_OPENSSL_CRYPTO_LIBRARY:FILEPATH={crypto}\n",
            encoding="utf-8",
        )
    module.run = fake_run
    try:
        module.repair_runtime_dependency_cache(root, "cmake", {})
    finally:
        module.run = original_run
    if len(commands) != 1:
        raise SystemExit(f"bootstrap-runtime-deps: FAIL: repair invoked {len(commands)} commands")
    repair_command = commands[0][1]
    if "--build" in repair_command or "-S" not in repair_command or "-B" not in repair_command:
        raise SystemExit(f"bootstrap-runtime-deps: FAIL: repair was not configure-only: {repair_command!r}")
    resolved = module.load_runtime_link_dependencies(root)
    if resolved != [str(ssl), str(crypto)]:
        raise SystemExit(f"bootstrap-runtime-deps: FAIL: repaired cache resolution returned {resolved!r}")

print(f"bootstrap-runtime-deps: PASS ({len(checks) + 2} checks)")
