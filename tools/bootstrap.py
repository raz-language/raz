#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Portable Raz bootstrap driver for Windows, Linux, and macOS."""
from __future__ import annotations

import argparse
import hashlib
import os
import platform
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import tomllib
import traceback
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IS_WINDOWS = os.name == "nt"
EXE = ".exe" if IS_WINDOWS else ""
OBJ = ".obj" if IS_WINDOWS else ".o"


BOOTSTRAP_LEGACY_SCRATCH_NAMES = {"host-source-order.txt", "stage1-diagnostic.txt"}
PROFILE_OUTPUT_DIRECTORIES = ("bin", "lib", "obj", "ir", "modules", "packages")
STAGE0_PORTABLE_DIGEST = ".raz-stage0-portable.sha256"


def ensure_profile_output_layout(project_root: Path, profile: str) -> dict[str, Path]:
    """Create the same target/<profile> layout used by normal Raz project builds.

    Bootstrap generations must not invent a second artifact layout.  Keeping this
    helper in the Python driver mirrors both the C++ Stage-0 build driver and the
    Raz production compiler's project-native path bridge.
    """
    profile_root = project_root / "target" / profile
    paths = {name: profile_root / name for name in PROFILE_OUTPUT_DIRECTORIES}
    for path in paths.values():
        path.mkdir(parents=True, exist_ok=True)
    return paths


def remove_legacy_bootstrap_scratch(project_root: Path) -> int:
    """Delete obsolete bootstrap marker files that may survive incremental targets.

    Older bootstrap/compiler revisions wrote these files into compiler projects.
    target/ is intentionally preserved between runs, so simply removing their
    writers is insufficient: existing workspaces need a one-time migration cleanup.
    """
    removed = 0
    if not project_root.exists():
        return removed
    for name in BOOTSTRAP_LEGACY_SCRATCH_NAMES:
        for path in project_root.rglob(name):
            if not path.is_file():
                continue
            try:
                path.unlink()
                removed += 1
            except OSError as exc:
                raise RuntimeError(f"Could not remove legacy bootstrap scratch artifact {path}: {exc}") from exc
    return removed


def remove_legacy_flat_profile_artifacts(project_root: Path, profile: str) -> int:
    """Remove pre-canonical repro artifacts from target/<profile> itself.

    Passes before the unified target-layout contract placed the recursive compiler
    object and executable directly in the profile root.  Normal Raz builds have
    always used obj/ and bin/, so delete only those known legacy bootstrap names.
    """
    profile_root = project_root / "target" / profile
    removed = 0
    for name in (f"compiler{OBJ}", f"raz-compiler{EXE}"):
        path = profile_root / name
        if path.is_file():
            path.unlink()
            removed += 1
    return removed


def normalized_host_arch() -> str:
    machine = platform.machine().lower()
    if machine in {"x86_64", "amd64"}:
        return "x86_64"
    if machine in {"aarch64", "arm64"}:
        return "aarch64"
    return machine or "unknown"


def load_bootstrap_config() -> dict[str, object]:
    """Load optional repository-local bootstrap.toml settings.

    Command-line flags continue to win.  Keeping configuration in the repository
    mirrors mature compiler bootstrap workflows without introducing a second
    bootstrap driver.
    """
    path = ROOT / "bootstrap.toml"
    if not path.is_file():
        example = ROOT / "bootstrap.example.toml"
        if example.is_file():
            print("WARNING: bootstrap.toml was not found; using built-in defaults.")
            print("HELP: copy bootstrap.example.toml to bootstrap.toml to customize the build.")
        return {}
    try:
        data = tomllib.loads(path.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise RuntimeError(f"Could not read {path}: {exc}") from exc
    section = data.get("bootstrap", {})
    if not isinstance(section, dict):
        raise RuntimeError("bootstrap.toml [bootstrap] must be a table.")
    return section


def _normalize_env(env: dict[str, str]) -> dict[str, str]:
    """Return a subprocess-safe environment.

    Windows environment-variable names are case-insensitive, while Python dicts
    are not.  Canonicalizing names prevents stale ``Path``/``PATH`` duplicates
    from defeating a freshly imported Visual Studio developer environment.
    """
    if not IS_WINDOWS:
        return dict(env)
    merged: dict[str, str] = {}
    for key, value in env.items():
        merged[key.upper()] = value
    return merged


def _env_get(env: dict[str, str], name: str, default: str | None = None) -> str | None:
    if IS_WINDOWS:
        return env.get(name.upper(), default)
    return env.get(name, default)


def _which(name: str, env: dict[str, str]) -> str | None:
    return shutil.which(name, path=_env_get(env, "PATH"))


def _prepend_env_path(env: dict[str, str], name: str, entries: list[Path]) -> None:
    existing = _env_get(env, name, "") or ""
    values = [str(p) for p in entries if p.is_dir()]
    if existing:
        values.append(existing)
    env[name.upper() if IS_WINDOWS else name] = os.pathsep.join(values)


def banner(text: str) -> None:
    print("\n" + "=" * 78)
    print(f"  {text}")
    print("=" * 78, flush=True)


def run(label: str, command: list[str], cwd: Path = ROOT, env: dict[str, str] | None = None) -> None:
    print(f"[RUN] {label}\n      {' '.join(command)}", flush=True)
    result = subprocess.run(command, cwd=cwd, env=env)
    if result.returncode:
        raise RuntimeError(f"{label} failed with exit code {result.returncode}.")


def require_command(name: str, env: dict[str, str] | None = None) -> str:
    active = env if env is not None else os.environ.copy()
    found = _which(name, active) if IS_WINDOWS else shutil.which(name, path=active.get("PATH"))
    if not found:
        raise RuntimeError(f"Required tool '{name}' was not found in PATH.")
    # Preserve the invoked filename. Resolving clang++ -> clang can change driver mode.
    return str(Path(found).absolute())


def read_cache(build: Path, key: str, required: bool = True) -> str:
    cache = build / "CMakeCache.txt"
    if not cache.is_file():
        if required:
            raise RuntimeError(f"CMake cache was not produced: {cache}")
        return ""
    prefix = key + ":"
    for raw in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if raw.startswith(prefix) and "=" in raw:
            return raw.split("=", 1)[1].strip()
    if required:
        raise RuntimeError(f"{key} was not found in {cache}.")
    return ""


def _cache_first(host_build: Path, keys: tuple[str, ...]) -> str:
    for key in keys:
        value = read_cache(host_build, key, required=False)
        if value:
            return value
    return ""


def load_runtime_link_dependencies(host_build: Path) -> list[str]:
    """Return exact native libraries required by raz_runtime for recursive links.

    Raz records canonical dependency paths in its own CMake cache variables.
    Legacy FindOpenSSL cache names are accepted as a migration fallback, but are
    deliberately not required: config-package based OpenSSL discovery on Windows
    can provide imported targets without persisting those implementation-detail
    variables in CMakeCache.txt.
    """
    openssl_state = read_cache(host_build, "RAZ_RUNTIME_OPENSSL_ENABLED", required=False).upper()
    true_values = {"1", "ON", "TRUE", "YES"}
    false_values = {"0", "OFF", "FALSE", "NO"}
    if openssl_state not in true_values | false_values:
        raise RuntimeError(
            "CMake did not export RAZ_RUNTIME_OPENSSL_ENABLED. "
            "Reconfigure the host build before recursive compiler linking."
        )
    if openssl_state in false_values:
        return []

    ssl = _cache_first(host_build, (
        "RAZ_RUNTIME_OPENSSL_SSL_LIBRARY",
        "OPENSSL_SSL_LIBRARY",
        "OPENSSL_SSL_LIBRARY_RELEASE",
        "SSL_EAY_LIBRARY_RELEASE",
        "SSL_EAY_LIBRARY_DEBUG",
    ))
    crypto = _cache_first(host_build, (
        "RAZ_RUNTIME_OPENSSL_CRYPTO_LIBRARY",
        "OPENSSL_CRYPTO_LIBRARY",
        "OPENSSL_CRYPTO_LIBRARY_RELEASE",
        "LIB_EAY_LIBRARY_RELEASE",
        "LIB_EAY_LIBRARY_DEBUG",
    ))
    if not ssl or not crypto:
        missing = []
        if not ssl:
            missing.append("SSL")
        if not crypto:
            missing.append("Crypto")
        raise RuntimeError(
            "OpenSSL support is enabled, but Raz's cached runtime linker path(s) "
            f"for {', '.join(missing)} are missing. Reconfigure the host build to "
            "refresh dependency metadata."
        )

    result = [ssl, crypto]
    for value in result:
        dep = Path(value)
        if not dep.is_file():
            raise RuntimeError(f"CMake resolved an OpenSSL runtime dependency to a missing file: {dep}")
    return result


def repair_runtime_dependency_cache(host_build: Path, cmake: str, env: dict[str, str]) -> None:
    """Refresh only CMake metadata when an otherwise reusable cache predates it.

    This intentionally does *not* delete or rebuild Stage-0.  A configure-only
    refresh teaches an existing cache about Raz-owned OpenSSL linker variables,
    preserving the expensive host compiler/runtime/Forge artifacts.
    """
    openssl_state = read_cache(host_build, "RAZ_RUNTIME_OPENSSL_ENABLED", required=False).upper()
    if openssl_state not in {"1", "ON", "TRUE", "YES"}:
        return
    ssl = _cache_first(host_build, ("RAZ_RUNTIME_OPENSSL_SSL_LIBRARY", "OPENSSL_SSL_LIBRARY"))
    crypto = _cache_first(host_build, ("RAZ_RUNTIME_OPENSSL_CRYPTO_LIBRARY", "OPENSSL_CRYPTO_LIBRARY"))
    if ssl and crypto and Path(ssl).is_file() and Path(crypto).is_file():
        return

    print("Stage-0 cache: refreshing runtime dependency metadata (no rebuild)", flush=True)
    run(
        "Refresh Stage-0 CMake metadata",
        [
            cmake,
            "-UOPENSSL_SSL_LIBRARY",
            "-UOPENSSL_CRYPTO_LIBRARY",
            "-URAZ_RUNTIME_OPENSSL_SSL_LIBRARY",
            "-URAZ_RUNTIME_OPENSSL_CRYPTO_LIBRARY",
            "-S", str(ROOT),
            "-B", str(host_build),
        ],
        env=env,
    )


def repair_future_timestamps(root: Path) -> int:
    now = time.time()
    threshold = now + 30
    replacement = now - 120
    fixed = 0
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in {"build", ".git"}]
        for name in files:
            p = Path(base) / name
            try:
                if p.stat().st_mtime > threshold:
                    os.utime(p, (replacement, replacement))
                    fixed += 1
            except OSError:
                pass
    if fixed:
        print(f"Repaired {fixed} future-dated source timestamp(s).")
    return fixed


def _find_vs_devcmd(env: dict[str, str]) -> Path | None:
    """Find VsDevCmd.bat across current and future Visual Studio layouts."""
    candidates: list[Path] = []

    # Prefer vswhere because it understands VS instance metadata and Preview/new
    # major-version layouts. Also accept a vswhere already present on PATH.
    on_path = shutil.which("vswhere.exe", path=_env_get(env, "PATH")) or shutil.which("vswhere", path=_env_get(env, "PATH"))
    if on_path:
        candidates.append(Path(on_path))
    for var in ("ProgramFiles(x86)", "ProgramFiles"):
        base = _env_get(env, var)
        if base:
            candidates.append(Path(base) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe")

    seen: set[str] = set()
    for vswhere in candidates:
        key = os.path.normcase(str(vswhere))
        if key in seen or not vswhere.is_file():
            continue
        seen.add(key)

        # First ask for a VC-capable instance. If component metadata is stale or
        # incomplete (seen with newer/Preview VS releases), fall back to latest
        # installed instance and validate VsDevCmd ourselves.
        queries = [
            ["-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
            ["-latest", "-products", "*", "-property", "installationPath"],
        ]
        for query in queries:
            proc = subprocess.run([str(vswhere), *query], capture_output=True, text=True, env=env)
            for raw in proc.stdout.splitlines():
                install = raw.strip()
                if not install:
                    continue
                candidate = Path(install) / "Common7" / "Tools" / "VsDevCmd.bat"
                if candidate.is_file():
                    return candidate

    # Do not assume the year is 2022. VS 18+ and Preview installations use the
    # same root hierarchy. rglob also covers uncommon channel/edition nesting.
    roots: list[Path] = []
    for var in ("ProgramFiles", "ProgramFiles(x86)"):
        base = _env_get(env, var)
        if base:
            roots.append(Path(base) / "Microsoft Visual Studio")
    system_drive = _env_get(env, "SystemDrive", "C:")
    roots.extend([
        Path(system_drive + r"\Program Files\Microsoft Visual Studio"),
        Path(system_drive + r"\Program Files (x86)\Microsoft Visual Studio"),
    ])

    matches: list[Path] = []
    seen_roots: set[str] = set()
    for root in roots:
        key = os.path.normcase(str(root))
        if key in seen_roots or not root.is_dir():
            continue
        seen_roots.add(key)
        try:
            matches.extend(p for p in root.rglob("VsDevCmd.bat") if p.parent.name.lower() == "tools" and p.parent.parent.name.lower() == "common7")
        except OSError:
            continue
    if matches:
        # Prefer newest major/channel path lexically; vswhere path was already
        # preferred above when available.
        return sorted(matches, key=lambda p: os.path.normcase(str(p)), reverse=True)[0]
    return None


def _resolve_executable(candidate: str, env: dict[str, str]) -> str | None:
    """Resolve a compiler/tool using the portable bootstrap toolchain contract."""
    if not candidate:
        return None
    path = Path(candidate)
    if path.is_file():
        return str(path.absolute())
    found = _which(candidate, env)
    return str(Path(found).absolute()) if found else None


def _test_cxx20_toolchain(compiler: str, scratch: Path, env: dict[str, str]) -> tuple[bool, str]:
    """Compile the C++20 preflight used by the portable bootstrap driver."""
    scratch.mkdir(parents=True, exist_ok=True)
    source = scratch / "raz-cxx20-preflight.cpp"
    obj = scratch / "raz-cxx20-preflight.obj"
    source.write_text(
        "#include <optional>\n"
        "#include <string_view>\n"
        "#include <vector>\n"
        "int raz_cxx20_preflight() {\n"
        "    std::optional<std::string_view> value{\"raz\"};\n"
        "    std::vector<int> values{1, 2, 3};\n"
        "    return value ? static_cast<int>(value->size() + values.size()) : 0;\n"
        "}\n",
        encoding="ascii",
    )
    obj.unlink(missing_ok=True)
    name = Path(compiler).name.lower()
    if name in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}:
        command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/c", str(source), f"/Fo{obj}"]
    else:
        command = [compiler, "-std=c++20", "-c", str(source), "-o", str(obj)]
    proc = subprocess.run(command, capture_output=True, text=True, env=env)
    ok = proc.returncode == 0 and obj.is_file()
    diagnostic = "\n".join((proc.stdout + "\n" + proc.stderr).strip().splitlines()[-8:])
    return ok, diagnostic


def _latest_version_dir(root: Path) -> Path | None:
    if not root.is_dir():
        return None
    candidates = [p for p in root.iterdir() if p.is_dir()]
    if not candidates:
        return None
    def key(p: Path) -> tuple[int, ...]:
        parts: list[int] = []
        for piece in p.name.replace("-", ".").split("."):
            try:
                parts.append(int(piece))
            except ValueError:
                parts.append(-1)
        return tuple(parts)
    return max(candidates, key=key)


def _repair_windows_toolchain_env(env: dict[str, str], devcmd: Path) -> dict[str, str]:
    """Fill MSVC/SDK paths directly when VsDevCmd metadata is incomplete.

    This is intentionally a fallback, not the primary path.  It uses the same
    Visual Studio installation selected for VsDevCmd and the latest installed
    MSVC toolset / Windows 10 SDK, then lets the real C++20 compile probe decide
    whether the result is usable.
    """
    if not IS_WINDOWS:
        return env

    result = _normalize_env(env)
    # .../<VS>/<version>/<edition>/Common7/Tools/VsDevCmd.bat -> installation root
    install = devcmd.parent.parent.parent
    msvc = _latest_version_dir(install / "VC" / "Tools" / "MSVC")

    pf86 = _env_get(result, "PROGRAMFILES(X86)") or _env_get(result, "PROGRAMFILES") or r"C:\Program Files (x86)"
    sdk_root = Path(pf86) / "Windows Kits" / "10"
    sdk_include_ver = _latest_version_dir(sdk_root / "Include")
    sdk_lib_ver = _latest_version_dir(sdk_root / "Lib")
    sdk_bin_ver = _latest_version_dir(sdk_root / "bin")

    include_dirs: list[Path] = []
    lib_dirs: list[Path] = []
    path_dirs: list[Path] = []

    if msvc:
        result["VCTOOLSINSTALLDIR"] = str(msvc) + os.sep
        result["VCINSTALLDIR"] = str(install / "VC") + os.sep
        include_dirs.append(msvc / "include")
        lib_dirs.append(msvc / "lib" / "x64")
        path_dirs.extend([msvc / "bin" / "Hostx64" / "x64", msvc / "bin" / "Hostx86" / "x64"])

    if sdk_include_ver:
        result["WINDOWSSDKDIR"] = str(sdk_root) + os.sep
        result["WINDOWSSDKVERSION"] = sdk_include_ver.name + "\\"
        include_dirs.extend([
            sdk_include_ver / "ucrt",
            sdk_include_ver / "shared",
            sdk_include_ver / "um",
            sdk_include_ver / "winrt",
            sdk_include_ver / "cppwinrt",
        ])
    if sdk_lib_ver:
        lib_dirs.extend([sdk_lib_ver / "ucrt" / "x64", sdk_lib_ver / "um" / "x64"])
    if sdk_bin_ver:
        path_dirs.append(sdk_bin_ver / "x64")

    _prepend_env_path(result, "INCLUDE", include_dirs)
    _prepend_env_path(result, "LIB", lib_dirs)
    _prepend_env_path(result, "LIBPATH", lib_dirs)
    _prepend_env_path(result, "PATH", path_dirs)
    return result


def import_visual_studio_environment(env: dict[str, str]) -> dict[str, str]:
    """Import the same x64 VS developer environment used by the legacy bootstrap."""
    env = _normalize_env(env)
    if not IS_WINDOWS:
        return env

    # Match the old bootstrap: only skip VsDevCmd when a complete developer
    # environment is already active. Merely having LLVM on PATH is not enough.
    if _env_get(env, "VSCMD_VER") and _env_get(env, "VCTOOLSINSTALLDIR") and _env_get(env, "WINDOWSSDKDIR"):
        print(f"Visual Studio environment already active ({_env_get(env, 'VSCMD_VER')}).")
        return env

    devcmd = _find_vs_devcmd(env)
    if devcmd is None:
        raise RuntimeError(
            "A usable Visual Studio C++ build environment could not be found. "
            "Raz needs the MSVC standard library and Windows SDK even when clang is selected."
        )

    print(f"Initializing Visual Studio toolchain: {devcmd}")
    comspec = _env_get(env, "COMSPEC") or "cmd.exe"
    with tempfile.TemporaryDirectory(prefix="raz-vsenv-") as temp_dir:
        temp = Path(temp_dir)
        wrapper = temp / "import-vs-env.cmd"
        dump = temp / "environment.txt"
        wrapper.write_text(
            "@echo off\r\n"
            f'call "{devcmd}" -no_logo -arch=x64 -host_arch=x64 >nul\r\n'
            "if errorlevel 1 exit /b %errorlevel%\r\n"
            f'set > "{dump}"\r\n',
            encoding="ascii",
        )
        proc = subprocess.run([comspec, "/d", "/c", str(wrapper)], cwd=temp, capture_output=True, text=True, env=env)
        if proc.returncode:
            detail = (proc.stderr or proc.stdout).strip()
            raise RuntimeError(f"Failed to initialize Visual Studio using {devcmd}. {detail}".strip())
        if not dump.is_file():
            raise RuntimeError(f"Visual Studio environment capture did not produce {dump}.")
        merged = dict(env)
        for line in dump.read_text(encoding="utf-8", errors="replace").splitlines():
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            if key:
                merged[key.upper()] = value

    merged = _normalize_env(merged)

    # VsDevCmd is normally sufficient. If a newer VS channel omits some metadata
    # or paths, reconstruct them from the exact installation we just discovered.
    if not _env_get(merged, "INCLUDE") or not _env_get(merged, "LIB"):
        print("Visual Studio environment was incomplete; reconstructing MSVC/Windows SDK paths from the installation.")
        merged = _repair_windows_toolchain_env(merged, devcmd)

    # Show the paths that matter when diagnosing bootstrap failures.
    include = _env_get(merged, "INCLUDE", "") or ""
    lib = _env_get(merged, "LIB", "") or ""
    print(f"MSVC/SDK include paths: {len([p for p in include.split(os.pathsep) if p])}")
    print(f"MSVC/SDK library paths: {len([p for p in lib.split(os.pathsep) if p])}")
    return merged


def choose_compiler(build: Path, env: dict[str, str], scratch: Path) -> tuple[str, bool]:
    """Use the legacy bootstrap selection order and validate every candidate."""
    cache = build / "CMakeCache.txt"
    if cache.is_file():
        cached = read_cache(build, "CMAKE_CXX_COMPILER")
        compiler = _resolve_executable(cached, env)
        if not compiler:
            print(
                f"Discarding stale host CMake cache; cached compiler is unavailable: {cached}",
                flush=True,
            )
            shutil.rmtree(build, ignore_errors=True)
            return choose_compiler(build, env, scratch)
        print(f"Validating cached C++ compiler: {compiler}")
        ok, diagnostic = _test_cxx20_toolchain(compiler, scratch, env)
        if not ok:
            raise RuntimeError(
                f"The compiler recorded in {build} cannot compile the C++20 standard library.\n"
                f"Compiler: {compiler}\n\n{diagnostic}\n\n"
                "Run bootstrap.bat -Clean after updating the toolchain."
            )
        return compiler, False

    names: list[str] = []
    if _env_get(env, "CXX"):
        names.append(_env_get(env, "CXX") or "")
    # Exact legacy preference: clang-cl, then cl, then clang++.
    names += ["clang-cl.exe", "cl.exe", "clang++.exe"] if IS_WINDOWS else ["clang++", "g++", "c++"]
    seen: set[str] = set()
    diagnostics: list[str] = []
    for candidate in names:
        compiler = _resolve_executable(candidate, env)
        if not compiler:
            continue
        key = os.path.normcase(compiler)
        if key in seen:
            continue
        seen.add(key)
        print(f"Checking C++20 toolchain: {compiler}")
        ok, diagnostic = _test_cxx20_toolchain(compiler, scratch, env)
        if ok:
            return compiler, True
        diagnostics.append(f"{compiler}\n{diagnostic}")

    details = "\n\n".join(diagnostics)
    raise RuntimeError(f"No working C++20 compiler was found.\n\n{details}".rstrip())

def find_artifact(root: Path, names: list[str], avoid: Path | None = None) -> Path:
    """Locate the first of `names` below `root`, preferring hits outside `avoid`.

    `avoid` names the staging directory that support archives get copied into.
    Those copies live inside the tree this search walks, so a build that already
    staged once would otherwise rediscover the stale copy in preference to the
    archive CMake just rebuilt. A relocated tree has only the staged copy, so an
    empty preferred set still falls back to it.
    """
    for name in names:
        hits = [hit.resolve() for hit in root.rglob(name)]
        if avoid is not None:
            hits = [hit for hit in hits if hit.parent != avoid] or hits
        if hits:
            return hits[0]
    raise RuntimeError(f"Could not find any of {names} below {root}.")


def find_optional_artifact(root: Path, names: list[str], avoid: Path | None = None) -> Path | None:
    try:
        return find_artifact(root, names, avoid)
    except RuntimeError:
        return None


def _canonical_compiler_source_files(include_non_rz: bool = False) -> list[Path]:
    """Return the one root entry plus files owned by nested ``raz_*`` packages.

    ``compiler/src`` is both the executable package's source directory and the
    container for rustc-style path-dependency packages.  A previous migration
    briefly left the old monolithic ``backend/``, ``driver/``, ``frontend/``,
    ``hir/``, and ``mir/`` trees beside the new ``raz_*`` packages.  Recursively
    scanning all of ``compiler/src`` therefore compiled both implementations and
    injected the same runtime extern declarations twice during Stage-0.

    Keep bootstrap's canonical input set explicit: ``src/main.rz`` belongs to the
    root executable, and every nested compiler package must be named ``raz_*`` and
    own a ``raz.toml`` manifest.  Other top-level directories are not semantic
    inputs to the production compiler.
    """
    source_root = ROOT / "compiler" / "src"
    entry = source_root / "main.rz"
    if not entry.is_file():
        raise RuntimeError("compiler/src/main.rz is missing from the production compiler package graph.")

    files: list[Path] = [entry]
    for package_root in sorted(source_root.iterdir()):
        if not package_root.is_dir() or not package_root.name.startswith("raz_"):
            continue
        if not (package_root / "raz.toml").is_file():
            continue
        for path in sorted(package_root.rglob("*")):
            if not path.is_file() or "target" in path.parts:
                continue
            if include_non_rz or path.suffix == ".rz":
                files.append(path)
    return files


def compiler_modules() -> list[Path]:
    """Discover only the canonical modular Raz compiler source set."""
    return _canonical_compiler_source_files(include_non_rz=False)


def _stage0_source_digest() -> str:
    """Fingerprint the native bootstrap/toolchain sources independently of location."""
    digest = hashlib.sha256()
    roots = [ROOT / "CMakeLists.txt", ROOT / "CMakePresets.json", ROOT / "cmake", ROOT / "src"]
    files: list[Path] = []
    for item in roots:
        if item.is_file():
            files.append(item)
        elif item.is_dir():
            files.extend(path for path in item.rglob("*") if path.is_file())
    for path in sorted(set(files)):
        if any(part in {"build", "target", ".git", "__pycache__"} for part in path.parts):
            continue
        relative = path.relative_to(ROOT).as_posix().encode("utf-8")
        digest.update(len(relative).to_bytes(4, "little"))
        digest.update(relative)
        digest.update(path.read_bytes())
    return digest.hexdigest()


def stage0_portable_cache_matches_sources(host_build: Path) -> bool:
    marker = host_build / STAGE0_PORTABLE_DIGEST
    if not marker.is_file():
        return False
    try:
        return marker.read_text(encoding="utf-8").strip() == _stage0_source_digest()
    except OSError:
        return False


def write_stage0_portable_cache_marker(host_build: Path) -> None:
    (host_build / STAGE0_PORTABLE_DIGEST).write_text(_stage0_source_digest() + "\n", encoding="utf-8")


def staged_runtime_link_dependencies(compiler_exe: Path) -> list[str]:
    """Return relocatable runtime provider libraries staged beside a compiler."""
    lib_dir = compiler_exe.parent.parent / "lib"
    if IS_WINDOWS:
        ssl_names = ("raz_runtime_ssl.lib", "raz_runtime_ssl.a")
        crypto_names = ("raz_runtime_crypto.lib", "raz_runtime_crypto.a")
    else:
        ssl_names = ("libraz_runtime_ssl.a", "libraz_runtime_ssl.so")
        crypto_names = ("libraz_runtime_crypto.a", "libraz_runtime_crypto.so")
    ssl = next((lib_dir / name for name in ssl_names if (lib_dir / name).is_file()), None)
    crypto = next((lib_dir / name for name in crypto_names if (lib_dir / name).is_file()), None)
    if ssl is None and crypto is None:
        return []
    if ssl is None or crypto is None:
        raise RuntimeError(f"Portable Stage-0 runtime dependency set is incomplete under {lib_dir}.")
    return [str(ssl), str(crypto)]


def resolve_cached_cxx_or_fallback(cached: str, env: dict[str, str], scratch: Path) -> str:
    """Resolve a cached host compiler after workspace relocation without touching CMake state."""
    candidates = [cached, Path(cached).name if cached else ""]
    if _env_get(env, "CXX"):
        candidates.append(_env_get(env, "CXX") or "")
    candidates += ["clang-cl.exe", "cl.exe", "clang++.exe"] if IS_WINDOWS else ["clang++", "g++", "c++"]
    seen: set[str] = set()
    for candidate in candidates:
        if not candidate:
            continue
        resolved = _resolve_executable(candidate, env)
        if not resolved:
            continue
        key = os.path.normcase(resolved)
        if key in seen:
            continue
        seen.add(key)
        ok, _ = _test_cxx20_toolchain(resolved, scratch, env)
        if ok:
            return resolved
    raise RuntimeError("No working C++ linker fallback is available for the retained Stage-0 toolchain.")


def stage0_cache_matches_workspace(host_build: Path) -> bool:
    """Return whether a CMake Stage-0 cache belongs to this workspace location."""
    cache = host_build / "CMakeCache.txt"
    if not cache.is_file():
        return False
    cached_source = read_cache(host_build, "CMAKE_HOME_DIRECTORY", required=False)
    cached_build = read_cache(host_build, "CMAKE_CACHEFILE_DIR", required=False)
    if not cached_source or not cached_build:
        return False
    try:
        return (
            Path(cached_source).resolve() == ROOT.resolve()
            and Path(cached_build).resolve() == host_build.resolve()
        )
    except OSError:
        return False


def cached_stage0_artifacts(host_build: Path) -> dict[str, Path] | None:
    """Return a complete reusable Stage-0 artifact set, or None if incomplete.

    Stage 0 is deliberately frozen compatibility machinery. Re-running CMake/Ninja
    on every self-host cycle adds latency without improving the Raz-owned compiler.
    A normal bootstrap therefore reuses an existing Stage-0 build verbatim.
    `--clean` or `--rebuild-stage0` is the explicit opt-in path for rebuilding it.
    """
    # CMake/Ninja metadata is inherently workspace-local, but the Stage-0
    # binaries themselves are portable once their runtime/Forge support has been
    # staged beside raz-stage0. Accept a relocated host tree only when its
    # location-independent source digest proves that portable artifact set is
    # current; a normal in-place CMake cache remains reusable without the marker.
    relocated = not stage0_cache_matches_workspace(host_build)
    if relocated and not stage0_portable_cache_matches_sources(host_build):
        return None

    required: dict[str, list[str]] = {
        "driver": [f"raz-stage0{EXE}"],
        "compat": [f"razc-stage0{EXE}"],
        "runtime": ["raz_runtime.lib", "libraz_runtime.a"],
        "bridge": ["raz_forge_bridge.lib", "libraz_forge_bridge.a"],
        "forge": ["forge.lib", "libforge.a"],
    }
    if IS_WINDOWS:
        required["oblink"] = [f"oblink{EXE}"]
    found: dict[str, Path] = {}
    staging_lib: Path | None = None
    for key, names in required.items():
        artifact = find_optional_artifact(host_build, names, avoid=staging_lib)
        if artifact is None or not artifact.is_file():
            return None
        found[key] = artifact
        if key == "driver":
            # Staging puts the support archives in the profile's lib directory
            # beside the driver, which is inside the tree searched here. Skip it
            # so an in-place cache keeps resolving the canonical CMake outputs.
            staging_lib = artifact.parent.parent / "lib"

    if relocated:
        lib_dir = found["driver"].parent.parent / "lib"
        required_staged = [found["runtime"].name, found["bridge"].name, found["forge"].name]
        for name in required_staged:
            if not (lib_dir / name).is_file():
                return None
        if IS_WINDOWS and not (found["driver"].parent / f"oblink{EXE}").is_file():
            return None

    # Optional runtime providers are staged beside the compiler for relocated
    # reuse; an in-place CMake cache can still recover the canonical originals.
    return found


def _link_or_copy_stage_source(source: str, destination: str) -> str:
    """Hard-link immutable compiler inputs when possible; copy as a fallback."""
    try:
        os.link(source, destination)
        return destination
    except OSError:
        return shutil.copy2(source, destination)


def _canonical_compiler_inputs() -> list[Path]:
    """Return only files that are semantic inputs to the compiler project.

    Bootstrap workspaces must not mirror arbitrary files from ``compiler/``.
    Keeping the input set explicit prevents generated diagnostics, profiling
    traces, or obsolete ordering metadata from leaking into target/bootstrap.
    """
    root = ROOT / "compiler"
    inputs: list[Path] = []
    for name in ("raz.toml", "raz.lock"):
        path = root / name
        if path.is_file():
            inputs.append(path)
    # The compiler mirrors rustc's workspace shape: a tiny binary plus focused
    # path-dependency packages under compiler/src/raz_*.  Do not recursively copy
    # arbitrary sibling directories from compiler/src: stale monolithic compiler
    # trees there would become root-package modules and collide with the real
    # package interfaces during Stage-0.
    inputs.extend(_canonical_compiler_source_files(include_non_rz=True))
    return sorted(set(inputs))


def _copy_compiler_project_inputs(build_dir: Path, copy_function=shutil.copy2) -> None:
    """Materialize the canonical compiler project without copying root artifacts."""
    root = ROOT / "compiler"
    for source in _canonical_compiler_inputs():
        relative = source.relative_to(root)
        destination = build_dir / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        copy_function(str(source), str(destination))


def _compiler_source_digest() -> str:
    """Fingerprint canonical compiler inputs independently of project caches."""
    root = ROOT / "compiler"
    digest = hashlib.sha256()
    for path in _canonical_compiler_inputs():
        relative = path.relative_to(root).as_posix().encode("utf-8")
        digest.update(len(relative).to_bytes(4, "little"))
        digest.update(relative)
        digest.update(path.read_bytes())
    return digest.hexdigest()


def _refresh_bootstrap_input_cache(build_dir: Path, current_digest: str) -> None:
    """Invalidate only whole-project cache layers when compiler inputs changed.

    Module fingerprint/MIR state remains valuable after an edit, but project.source
    and completed native artifacts describe the previous source tree and must not
    survive a body-only change. Keeping those files was able to make bootstrap
    report a false Fresh result after backend implementation edits.
    """
    cache = build_dir / "target" / "cache"
    cache.mkdir(parents=True, exist_ok=True)
    stamp = cache / "bootstrap-input.sha256"
    previous = stamp.read_text(encoding="utf-8").strip() if stamp.is_file() else ""
    if previous and previous != current_digest:
        for name in ("project.source", "project.meta", "build.key", "artifact.bin", "check.key"):
            (cache / name).unlink(missing_ok=True)
    stamp.write_text(current_digest + "\n", encoding="utf-8")


def _stage0_context_fields(path: Path, struct_name: str) -> list[str]:
    """Extract scalar context fields for the frozen Stage-0 compatibility view."""
    text = path.read_text(encoding="utf-8")
    match = re.search(rf"public struct {re.escape(struct_name)} \{{(?P<body>.*?)\n\}}", text, flags=re.S)
    if match is None:
        raise RuntimeError(f"Could not find {struct_name} in {path}.")
    fields = []
    for line in match.group("body").splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("//"):
            fields.append("    " + stripped)
    return fields


def _flatten_stage0_semantic_contexts(seed_middle: Path) -> None:
    """Flatten new semantic contexts only for the frozen C++ Stage-0 seed.

    The canonical Raz compiler owns explicit HirQueryContext and
    MirOwnershipContext values. Frozen Stage-0 cannot carry these nested public
    package types through its generated package interface, so its disposable
    compatibility package temporarily restores their scalar fields directly on
    HirBuilder/MirModule. The Raz-owned self-host immediately rebuilds the
    canonical explicit-context graph.
    """
    hir_context = seed_middle / "src" / "hir" / "query" / "context.rz"
    if not hir_context.is_file():
        # Canonical query storage lives in the independent raz_query package.
        # The frozen Stage-0 seed still flattens it into HirBuilder, so consume
        # the copied sibling package directly instead of reintroducing query
        # ownership into canonical HIR.
        hir_context = seed_middle.parent / "raz_query" / "src" / "query" / "context.rz"
    mir_context = seed_middle / "src" / "mir" / "ownership" / "context.rz"
    hir_fields = _stage0_context_fields(hir_context, "HirQueryContext")
    mir_fields = _stage0_context_fields(mir_context, "MirOwnershipContext")
    hir_model = seed_middle / "src" / "hir" / "core" / "model.rz"
    mir_model = seed_middle / "src" / "mir" / "core" / "model.rz"
    text = hir_model.read_text(encoding="utf-8")
    text = text.replace("    HirQueryContext queries;", "\n".join(hir_fields))
    hir_model.write_text(text, encoding="utf-8")
    text = mir_model.read_text(encoding="utf-8")
    text = text.replace("    MirOwnershipContext ownership;", "\n".join(mir_fields))
    mir_model.write_text(text, encoding="utf-8")
    for source_file in (seed_middle / "src").rglob("*.rz"):
        text = source_file.read_text(encoding="utf-8")
        text = text.replace(".queries.", ".")
        text = text.replace(".ownership.", ".")
        text = text.replace("public import querydb::query::context;\n", "")
        text = text.replace("import querydb::query::context;\n", "")
        source_file.write_text(text, encoding="utf-8")


def _prepare_stage0_middle_compat(compiler_project: Path) -> None:
    """Merge semantic middle-end packages only in the frozen Stage-0 view.

    Canonical Raz keeps HIR, MIR, query state, and borrow checking as explicit
    packages. Frozen Stage-0 predates cross-package compiler IR interfaces, so
    its disposable seed presents HIR + MIR + borrowck as one rustc_middle-like
    package. The first Raz-owned self-host rebuild restores the canonical graph.
    """
    seed_hir = compiler_project / "src" / "raz_hir"
    seed_mir = compiler_project / "src" / "raz_mir"
    seed_borrowck = compiler_project / "src" / "raz_borrowck"
    seed_mir_opt = compiler_project / "src" / "raz_mir_opt"
    seed_middle = compiler_project / "src" / "raz_middle"
    if not seed_hir.is_dir() or not seed_mir.is_dir():
        return

    shutil.rmtree(seed_middle, ignore_errors=True)
    (seed_middle / "src").mkdir(parents=True, exist_ok=True)
    shutil.copytree(seed_hir / "src" / "hir", seed_middle / "src" / "hir")

    # Canonical HIR keeps focused semantic subsystems in sibling modules. Frozen
    # Stage-0 predates reliable sibling-symbol propagation inside the merged middle
    # package, so fold those implementations back into their historical owners only
    # in this disposable compatibility view.
    semantic = seed_middle / "src" / "hir" / "semantic"
    statements = semantic / "statements.rz"
    statement_parts = (semantic / "statement_support.rz", semantic / "match_statements.rz")
    if statements.is_file() and all(path.is_file() for path in statement_parts):
        statements_text = statements.read_text(encoding="utf-8")
        statements_text = statements_text.replace("public import raz_hir::semantic::statement_support;\n", "")
        statements_text = statements_text.replace("public import raz_hir::semantic::match_statements;\n", "")
        statements_text = statements_text.rstrip()
        for part in statement_parts:
            part_text = part.read_text(encoding="utf-8")
            marker = part_text.find("\n\n", part_text.find("namespace "))
            if marker < 0:
                raise RuntimeError(f"Could not prepare Stage-0 HIR statement compatibility view: {part.name}")
            folded_body = part_text[marker + 2 :]
            folded_body = "\n".join(
                line for line in folded_body.splitlines()
                if not line.startswith("import ") and not line.startswith("public import ")
            )
            statements_text += (
                f"\n\n// Stage-0 compatibility: folded {part.name}.\n"
                + folded_body.rstrip()
            )
            part.unlink()
        statements.write_text(statements_text + "\n", encoding="utf-8")

    comptime = semantic / "comptime.rz"
    comptime_eval = semantic / "comptime_eval.rz"
    if comptime.is_file() and comptime_eval.is_file():
        comptime_text = comptime.read_text(encoding="utf-8")
        comptime_text = comptime_text.replace("public import raz_hir::semantic::comptime_eval;\n", "")
        part_text = comptime_eval.read_text(encoding="utf-8")
        marker = part_text.find("\n\n", part_text.find("namespace "))
        if marker < 0:
            raise RuntimeError("Could not prepare Stage-0 HIR comptime compatibility view")
        folded_body = part_text[marker + 2 :]
        folded_body = "\n".join(
            line for line in folded_body.splitlines()
            if not line.startswith("import ") and not line.startswith("public import ")
        )
        # Evaluator definitions must precede the parsing/orchestration functions that
        # call them, so place the folded body immediately before canonical comptime.
        header_end = comptime_text.find("\n\n", comptime_text.find("namespace "))
        if header_end < 0:
            raise RuntimeError("Could not locate Stage-0 comptime namespace header")
        prefix = comptime_text[: header_end + 2]
        suffix = comptime_text[header_end + 2 :]
        comptime.write_text(
            prefix
            + "public import raz_hir::traits::solver;\n\n"
            + "// Stage-0 compatibility: folded comptime_eval.rz.\n"
            + folded_body.rstrip()
            + "\n\n"
            + suffix.lstrip(),
            encoding="utf-8",
        )
        comptime_eval.unlink()

    expressions = semantic / "expressions.rz"
    expression_support = semantic / "expression_support.rz"
    expression_operators = semantic / "expression_operators.rz"
    expression_calls = semantic / "expression_calls.rz"
    expression_postfix = semantic / "expression_postfix.rz"
    if (
        expressions.is_file() and
        expression_support.is_file() and
        expression_operators.is_file() and
        expression_calls.is_file() and
        expression_postfix.is_file()
    ):
        expressions_text = expressions.read_text(encoding="utf-8")
        expressions_text = expressions_text.replace("public import raz_hir::semantic::expression_support;\n", "")
        expressions_text = expressions_text.replace("public import raz_hir::semantic::expression_operators;\n", "")
        expressions_text = expressions_text.replace("public import raz_hir::semantic::expression_calls;\n", "")
        expressions_text = expressions_text.replace("public import raz_hir::semantic::expression_postfix;\n", "")

        def folded_expression_body(path: Path) -> str:
            part_text = path.read_text(encoding="utf-8")
            marker = part_text.find("\n\n", part_text.find("namespace "))
            if marker < 0:
                raise RuntimeError(f"Could not prepare Stage-0 HIR expression compatibility view: {path.name}")
            return "\n".join(
                line for line in part_text[marker + 2 :].splitlines()
                if not line.startswith("import ") and not line.startswith("public import ")
            ).strip()

        header_end = expressions_text.find("\n\n", expressions_text.find("namespace "))
        parse_expression = expressions_text.find("fn hir_parse_expression(")
        if header_end < 0 or parse_expression < 0:
            raise RuntimeError("Could not locate Stage-0 expression compatibility insertion points")
        prefix = expressions_text[: header_end + 2]
        body = expressions_text[header_end + 2 : parse_expression]
        tail = expressions_text[parse_expression:]
        expressions.write_text(
            prefix
            + "// Stage-0 compatibility: folded expression_support.rz.\n"
            + folded_expression_body(expression_support)
            + "\n\n"
            + body.lstrip()
            + "\n// Stage-0 compatibility: folded expression_operators.rz.\n"
            + folded_expression_body(expression_operators)
            + "\n\n// Stage-0 compatibility: folded expression_calls.rz.\n"
            + folded_expression_body(expression_calls)
            + "\n\n// Stage-0 compatibility: folded expression_postfix.rz.\n"
            + folded_expression_body(expression_postfix)
            + "\n\n"
            + tail.lstrip(),
            encoding="utf-8",
        )
        expression_support.unlink()
        expression_operators.unlink()
        expression_calls.unlink()
        expression_postfix.unlink()

    ownership = semantic / "ownership.rz"
    ownership_paths = semantic / "ownership_paths.rz"
    ownership_flow = semantic / "ownership_flow.rz"
    if ownership.is_file() and ownership_paths.is_file() and ownership_flow.is_file():
        ownership_text = ownership.read_text(encoding="utf-8")
        ownership_text = ownership_text.replace("public import raz_hir::semantic::ownership_paths;\n", "")
        ownership_text = ownership_text.replace("public import raz_hir::semantic::ownership_flow;\n", "")

        def folded_ownership_body(path: Path) -> str:
            part_text = path.read_text(encoding="utf-8")
            marker = part_text.find("\n\n", part_text.find("namespace "))
            if marker < 0:
                raise RuntimeError(f"Could not prepare Stage-0 HIR ownership compatibility view: {path.name}")
            return "\n".join(
                line for line in part_text[marker + 2 :].splitlines()
                if not line.startswith("import ") and not line.startswith("public import ")
            ).strip()

        # Path helpers are used by both the canonical ownership body and the flow
        # analysis, so define them before either consumer in the frozen view. Flow
        # analysis may remain last because its public entry point is consumed later
        # by comptime/HIR orchestration.
        header_end = ownership_text.find("\n\n", ownership_text.find("namespace "))
        if header_end < 0:
            raise RuntimeError("Could not locate Stage-0 ownership namespace header")
        prefix = ownership_text[: header_end + 2]
        suffix = ownership_text[header_end + 2 :]
        ownership.write_text(
            prefix
            + "// Stage-0 compatibility: folded ownership_paths.rz.\n"
            + folded_ownership_body(ownership_paths)
            + "\n\n"
            + suffix.rstrip()
            + "\n\n// Stage-0 compatibility: folded ownership_flow.rz.\n"
            + folded_ownership_body(ownership_flow)
            + "\n",
            encoding="utf-8",
        )
        ownership_paths.unlink()
        ownership_flow.unlink()

    shutil.copytree(seed_mir / "src" / "mir", seed_middle / "src" / "mir")
    if seed_borrowck.is_dir():
        shutil.copytree(seed_borrowck / "src" / "borrowck", seed_middle / "src" / "borrowck")
    if seed_mir_opt.is_dir():
        shutil.copytree(seed_mir_opt / "src" / "mir_opt" / "transform", seed_middle / "src" / "mir" / "transform")

    _flatten_stage0_semantic_contexts(seed_middle)
    for source_file in (seed_middle / "src").rglob("*.rz"):
        source = source_file.read_text(encoding="utf-8")
        source = source.replace("raz_hir::", "raz_middle::hir::")
        source = source.replace("raz_mir::", "raz_middle::mir::")
        source = source.replace("raz_borrowck::", "raz_middle::borrowck::")
        source = source.replace("raz_mir_opt::", "raz_middle::mir::")
        source = source.replace("public import mir_opt::", "public import raz_middle::mir::")
        source = source.replace("import mir_opt::", "import raz_middle::mir::")
        source = source.replace("public import hir::", "public import raz_middle::hir::")
        source = source.replace("import hir::", "import raz_middle::hir::")
        source = source.replace("public import mir::", "public import raz_middle::mir::")
        source = source.replace("import mir::", "import raz_middle::mir::")
        source_file.write_text(source, encoding="utf-8")

    exports: list[str] = []
    for package, old_prefix, new_prefix in (
        (seed_hir, "raz_hir::", "raz_middle::hir::"),
        (seed_mir, "raz_mir::", "raz_middle::mir::"),
        (seed_borrowck, "raz_borrowck::", "raz_middle::borrowck::"),
        (seed_mir_opt, "raz_mir_opt::", "raz_middle::mir::"),
    ):
        lib = package / "src" / "lib.rz"
        if not lib.is_file():
            continue
        for line in lib.read_text(encoding="utf-8").splitlines():
            if line.startswith("public import " + old_prefix):
                if package == seed_hir and (
                    "semantic::statement_support;" in line or
                    "semantic::match_statements;" in line or
                    "semantic::comptime_eval;" in line or
                    "semantic::ownership_paths;" in line or
                    "semantic::ownership_flow;" in line or
                    "semantic::expression_support;" in line or
                    "semantic::expression_operators;" in line or
                    "semantic::expression_calls;" in line or
                    "semantic::expression_postfix;" in line
                ):
                    continue
                exports.append(line.replace(old_prefix, new_prefix))

    (seed_middle / "src" / "lib.rz").write_text(
        "// Copyright 2026 Mario Vinciguerra\n// SPDX-License-Identifier: Apache-2.0\n\n"
        "namespace raz_middle;\n\n"
        "public import frontend::parser;\n"
        + "\n".join(exports) + "\n",
        encoding="utf-8",
    )
    (seed_middle / "raz.toml").write_text(
        "# Copyright 2026 Mario Vinciguerra\n# SPDX-License-Identifier: Apache-2.0\n\n"
        "[package]\nname = \"raz-middle\"\nversion = \"1.0.0\"\nkind = \"static-library\"\n"
        "source = \"src\"\nentry = \"src/lib.rz\"\n\n"
        "[dependencies]\nfrontend = \"../raz_parser\"\n\n"
        "[profile.debug]\noptimization = 0\ndebug = true\nincremental = true\n\n"
        "[profile.release]\noptimization = 2\ndebug = false\nincremental = true\n",
        encoding="utf-8",
    )

    shutil.rmtree(seed_hir)
    shutil.rmtree(seed_mir)
    shutil.rmtree(seed_borrowck, ignore_errors=True)
    shutil.rmtree(seed_mir_opt, ignore_errors=True)
    shutil.rmtree(compiler_project / "src" / "raz_query", ignore_errors=True)

    for package_name in (
        "raz_codegen_common", "raz_codegen_forge", "raz_codegen_llvm", "raz_codegen_wasm",
        "raz_codegen_rxe", "raz_codegen_web", "raz_driver",
    ):
        manifest = compiler_project / "src" / package_name / "raz.toml"
        manifest_text = manifest.read_text(encoding="utf-8")
        manifest_text = re.sub(r'^hir = "../raz_hir"\r?\n', '', manifest_text, flags=re.M)
        manifest_text = re.sub(r'^mir = "../raz_mir"\r?\n', '', manifest_text, flags=re.M)
        manifest_text = re.sub(r'^borrowck = "../raz_borrowck"\r?\n', '', manifest_text, flags=re.M)
        manifest_text = re.sub(r'^mir_opt = "../raz_mir_opt"\r?\n', '', manifest_text, flags=re.M)
        if 'middle = "../raz_middle"' not in manifest_text:
            manifest_text = manifest_text.replace("[dependencies]\n", "[dependencies]\nmiddle = \"../raz_middle\"\n", 1)
        manifest.write_text(manifest_text, encoding="utf-8")
        for source_file in (compiler_project / "src" / package_name / "src").rglob("*.rz"):
            source = source_file.read_text(encoding="utf-8")
            source = source.replace("public import hir::", "public import middle::hir::")
            source = source.replace("import hir::", "import middle::hir::")
            source = source.replace("public import mir::", "public import middle::mir::")
            source = source.replace("import mir::", "import middle::mir::")
            source = source.replace("public import borrowck::", "public import middle::borrowck::")
            source = source.replace("public import mir_opt::", "public import middle::mir::")
            source = source.replace("import borrowck::", "import middle::borrowck::")
            source = source.replace("import mir_opt::", "import middle::mir::")
            source_file.write_text(source, encoding="utf-8")


def _prepare_stage0_native_codegen_compat(compiler_project: Path) -> None:
    """Merge canonical Forge+LLVM packages only in the frozen Stage-0 view.

    Stage-0 cannot reliably compose the full canonical native-backend package
    interfaces. Canonical Raz keeps Forge and LLVM as sibling packages sharing
    raz_codegen_common; the disposable seed sees one native-codegen package and
    the Raz-owned self-host immediately restores the real graph.
    """
    seed_forge = compiler_project / "src" / "raz_codegen_forge"
    seed_llvm = compiler_project / "src" / "raz_codegen_llvm"
    seed_common = compiler_project / "src" / "raz_codegen_common"
    seed_native = compiler_project / "src" / "raz_codegen_native"
    if not seed_forge.is_dir() or not seed_llvm.is_dir():
        return

    shutil.rmtree(seed_native, ignore_errors=True)
    (seed_native / "src").mkdir(parents=True, exist_ok=True)
    shutil.copytree(seed_forge / "src" / "forge", seed_native / "src" / "forge")
    shutil.copytree(seed_llvm / "src" / "llvm", seed_native / "src" / "llvm")
    if seed_common.is_dir():
        shutil.copytree(seed_common / "src" / "codegen_common", seed_native / "src" / "common")

    for source_file in (seed_native / "src").rglob("*.rz"):
        text = source_file.read_text(encoding="utf-8")
        text = text.replace("raz_codegen_forge::", "raz_codegen_native::forge::")
        text = text.replace("raz_codegen_llvm::", "raz_codegen_native::llvm::")
        text = text.replace("raz_codegen_common::", "raz_codegen_native::common::")
        text = text.replace("public import forge::", "public import raz_codegen_native::forge::")
        text = text.replace("import forge::", "import raz_codegen_native::forge::")
        source_file.write_text(text, encoding="utf-8")

    exports: list[str] = []
    for package, prefix in ((seed_forge, "raz_codegen_native::forge::"), (seed_llvm, "raz_codegen_native::llvm::")):
        lib = package / "src" / "lib.rz"
        for line in lib.read_text(encoding="utf-8").splitlines():
            if line.startswith("public import raz_codegen_"):
                suffix = line.split("::", 1)[1]
                if package == seed_forge:
                    suffix = suffix.replace("forge::", "", 1)
                else:
                    suffix = suffix.replace("llvm::", "", 1)
                exports.append("public import " + prefix + suffix)

    (seed_native / "src" / "lib.rz").write_text(
        "// Copyright 2026 Mario Vinciguerra\n// SPDX-License-Identifier: Apache-2.0\n\n"
        "namespace raz_codegen_native;\n\n"
        "public import frontend::parser;\n"
        "public import middle::hir::core::model;\n"
        "public import middle::mir::core::model;\n"
        "public import raz_codegen_native::common::writer;\n"
        "public import raz_codegen_native::common::abi;\n"
        "public import raz_codegen_native::common::analysis;\n"
        "public import raz_codegen_native::common::symbols;\n"
        + "\n".join(exports) + "\n",
        encoding="utf-8",
    )
    (seed_native / "raz.toml").write_text(
        "# Copyright 2026 Mario Vinciguerra\n# SPDX-License-Identifier: Apache-2.0\n\n"
        "[package]\nname = \"raz-codegen-native\"\nversion = \"1.0.0\"\nkind = \"static-library\"\n"
        "source = \"src\"\nentry = \"src/lib.rz\"\n\n"
        "[dependencies]\nlexer = \"../raz_lexer\"\nfrontend = \"../raz_parser\"\nmiddle = \"../raz_middle\"\n\n"
        "[profile.debug]\noptimization = 0\ndebug = true\nincremental = true\n\n"
        "[profile.release]\noptimization = 2\ndebug = false\nincremental = true\n",
        encoding="utf-8",
    )
    shutil.rmtree(seed_forge)
    shutil.rmtree(seed_llvm)

    driver_manifest = compiler_project / "src" / "raz_driver" / "raz.toml"
    manifest_text = driver_manifest.read_text(encoding="utf-8")
    manifest_text = re.sub(r'^forge = "../raz_codegen_forge"\r?\n', '', manifest_text, flags=re.M)
    manifest_text = re.sub(r'^llvm = "../raz_codegen_llvm"\r?\n', '', manifest_text, flags=re.M)
    if 'native = "../raz_codegen_native"' not in manifest_text:
        manifest_text = manifest_text.replace("[dependencies]\n", "[dependencies]\nnative = \"../raz_codegen_native\"\n", 1)
    driver_manifest.write_text(manifest_text, encoding="utf-8")
    for source_file in (compiler_project / "src" / "raz_driver" / "src").rglob("*.rz"):
        text = source_file.read_text(encoding="utf-8")
        text = text.replace("public import forge::", "public import native::forge::")
        text = text.replace("import forge::", "import native::forge::")
        text = text.replace("public import llvm::", "public import native::llvm::")
        text = text.replace("import llvm::", "import native::llvm::")
        source_file.write_text(text, encoding="utf-8")



def _prepare_stage0_driver_project_compat(compiler_project: Path) -> None:
    """Fold newer split driver modules only in the frozen Stage-0 seed view.

    Canonical/self-host Raz keeps project policy and registry version/index helpers
    in focused modules. Frozen Stage-0 does not reliably propagate newly split
    sibling symbols through the raz_driver package re-export, so the disposable
    seed view restores the historical co-located shape without changing canonical
    source.
    """
    driver_root = compiler_project / "src" / "raz_driver" / "src"
    driver_src = driver_root / "driver"
    project = driver_src / "project.rz"
    native = driver_src / "project_native.rz"
    web_manifest = driver_src / "project_web_manifest.rz"
    if not project.is_file() or not native.is_file() or not web_manifest.is_file():
        return

    project_text = project.read_text(encoding="utf-8")
    # Stage-0 does not reliably forward the nested native package's writer
    # re-export through raz_driver::backend/host_support. Make that dependency
    # explicit only in the compatibility view for modules that use ForgeWriter.
    if "public import native::forge::writer;" not in project_text:
        project_text = project_text.replace(
            "public import raz_driver::path;\n",
            "public import raz_driver::path;\npublic import native::forge::writer;\n",
            1,
        )
    incremental = driver_src / "incremental.rz"
    incremental_text = incremental.read_text(encoding="utf-8")
    if "public import native::forge::writer;" not in incremental_text:
        incremental_text = incremental_text.replace(
            "public import raz_driver::host_support;\n",
            "public import raz_driver::host_support;\npublic import native::forge::writer;\n",
            1,
        )
        incremental.write_text(incremental_text, encoding="utf-8")

    def implementation_body(path: Path, landmark: str) -> str:
        text = path.read_text(encoding="utf-8")
        start = text.find(landmark)
        if start < 0:
            raise RuntimeError(f"Could not prepare Stage-0 project compatibility view: {path.name}")
        return text[start:].rstrip() + "\n"

    web_body = implementation_body(web_manifest, "fn project_copy_quoted_value(")
    native_body = implementation_body(native, "struct ProjectNativeBuild")
    project.write_text(
        project_text.rstrip()
        + "\n\n// Stage-0 compatibility: folded web manifest module.\n"
        + web_body
        + "\n// Stage-0 compatibility: folded native project module.\n"
        + native_body,
        encoding="utf-8",
    )
    web_manifest.unlink()
    native.unlink()

    driver_lib = driver_root / "lib.rz"
    lib_text = driver_lib.read_text(encoding="utf-8")
    lib_text = lib_text.replace("public import raz_driver::project_native;\n", "")
    lib_text = lib_text.replace("public import raz_driver::project_web_manifest;\n", "")

    registry = driver_src / "registry.rz"
    registry_parts = (
        driver_src / "registry_semver.rz",
        driver_src / "registry_support.rz",
        driver_src / "registry_index.rz",
        driver_src / "registry_state.rz",
        driver_src / "registry_tracking.rz",
        driver_src / "registry_resolver.rz",
    )
    if registry.is_file() and all(path.is_file() for path in registry_parts):
        registry_text = registry.read_text(encoding="utf-8").rstrip()
        for part in registry_parts:
            part_text = part.read_text(encoding="utf-8")
            marker = part_text.find("\n\n", part_text.find("namespace "))
            if marker < 0:
                raise RuntimeError(f"Could not prepare Stage-0 registry compatibility view: {part.name}")
            folded_body = part_text[marker + 2 :]
            folded_body = "\n".join(
                line for line in folded_body.splitlines()
                if not line.startswith("import ") and not line.startswith("public import ")
            )
            registry_text += (
                f"\n\n// Stage-0 compatibility: folded {part.name}.\n"
                + folded_body.rstrip()
            )
            part.unlink()
        registry.write_text(registry_text + "\n", encoding="utf-8")
        for module in ("registry_semver", "registry_support", "registry_index", "registry_state", "registry_tracking", "registry_resolver"):
            lib_text = lib_text.replace(f"public import raz_driver::{module};\n", "")

    cli = driver_src / "cli.rz"
    cli_parts = (driver_src / "cli_support.rz", driver_src / "cli_help.rz")
    if cli.is_file() and all(path.is_file() for path in cli_parts):
        cli_text = cli.read_text(encoding="utf-8")
        cli_text = cli_text.replace("public import raz_driver::cli_support;\n", "")
        cli_text = cli_text.replace("public import raz_driver::cli_help;\n", "")
        cli_text = cli_text.rstrip()
        for part in cli_parts:
            part_text = part.read_text(encoding="utf-8")
            marker = part_text.find("\n\n", part_text.find("namespace "))
            if marker < 0:
                raise RuntimeError(f"Could not prepare Stage-0 CLI compatibility view: {part.name}")
            folded_body = part_text[marker + 2 :]
            folded_body = "\n".join(
                line for line in folded_body.splitlines()
                if not line.startswith("import ") and not line.startswith("public import ")
            )
            cli_text += (
                f"\n\n// Stage-0 compatibility: folded {part.name}.\n"
                + folded_body.rstrip()
            )
            part.unlink()
        cli.write_text(cli_text + "\n", encoding="utf-8")
        for module in ("cli_support", "cli_help"):
            lib_text = lib_text.replace(f"public import raz_driver::{module};\n", "")

        # web_diagnostics is a focused canonical module, but the frozen Stage-0
        # compatibility view folds cli_support into cli.rz. Point the disposable
        # seed module at that folded public surface so its diagnostic writer
        # remains resolvable without changing the canonical package graph.
        web_diagnostics = driver_src / "web_diagnostics.rz"
        if web_diagnostics.is_file():
            diagnostics_text = web_diagnostics.read_text(encoding="utf-8")
            diagnostics_text = diagnostics_text.replace(
                "public import raz_driver::cli_support;\n",
                "public import raz_driver::cli;\n",
                1,
            )
            web_diagnostics.write_text(diagnostics_text, encoding="utf-8")

    # Frozen Stage-0 also cannot reliably resolve the browser-host bitmap helper
    # through the stubbed web dependency from a newly split driver module. The
    # seed never executes web emission, so use a local signature-compatible helper
    # only in this disposable compatibility view.
    web_host_prune = driver_src / "web_host_prune.rz"
    if web_host_prune.is_file():
        prune_text = web_host_prune.read_text(encoding="utf-8")
        prune_text = prune_text.replace("import web::browser_host_js;\n", "", 1)
        seed_helper = (
            "\nfn web_browser_import_enabled(i64 import_mask, i64 import_mask_high, i64 logical_import) -> bool { return false; }\n"
        )
        marker = "// Static-first pages write a marker per browser host binding."
        if seed_helper.strip() not in prune_text and marker in prune_text:
            prune_text = prune_text.replace(marker, seed_helper + "\n" + marker, 1)
        web_host_prune.write_text(prune_text, encoding="utf-8")

    driver_lib.write_text(lib_text, encoding="utf-8")



def _flatten_stage0_seed_package_sources(compiler_project: Path) -> None:
    """Present newly nested compiler packages in the flat shape frozen Stage-0 understands.

    Canonical compiler packages keep ``src/lib.rz`` plus implementation modules in
    a nested directory.  The frozen C++ Stage-0 predates that package-source
    layout and discovers package modules only directly beneath ``source``.  Flatten
    only the disposable bootstrap view; canonical/self-host sources remain nested.
    """
    packages = (
        ("raz_driver", "driver"),
        ("raz_lexer", "lexer"),
        ("raz_parser", "parser"),
    )
    for package_name, implementation_dir in packages:
        source_root = compiler_project / "src" / package_name / "src"
        nested_root = source_root / implementation_dir
        if not nested_root.is_dir():
            continue
        for source in sorted(nested_root.rglob("*.rz")):
            relative = source.relative_to(nested_root)
            if len(relative.parts) != 1:
                raise RuntimeError(
                    f"Frozen Stage-0 compatibility cannot flatten nested module path: {source}"
                )
            destination = source_root / source.name
            if destination.exists():
                raise RuntimeError(
                    f"Frozen Stage-0 compatibility module collision: {destination}"
                )
            source.replace(destination)
        shutil.rmtree(nested_root)

def prepare_seed_compiler_project(build_dir: Path) -> None:
    """Refresh seed sources while preserving Raz's project-local incremental cache.

    The Stage-0 -> seed build is part of normal developer bootstrap and should not
    throw away `target/cache` on every invocation.  Remove the previous source
    view so deleted/renamed modules cannot linger, but retain target/ so the Raz
    build driver can reuse exact unchanged module/HIR/MIR/native artifacts.
    """
    build_dir.mkdir(parents=True, exist_ok=True)
    for child in build_dir.iterdir():
        if child.name == "target":
            continue
        if child.is_dir() and not child.is_symlink():
            shutil.rmtree(child)
        else:
            child.unlink(missing_ok=True)
    _copy_compiler_project_inputs(build_dir)
    remove_legacy_bootstrap_scratch(build_dir)
    _refresh_bootstrap_input_cache(build_dir, _compiler_source_digest())


def prepare_self_host_build(build_dir: Path, seed_project: Path, reset_cache: bool) -> None:
    """Refresh the normal self-host workspace while retaining safe incremental state.

    The normal bootstrap performs one Raz-owned rebuild.  Keeping its target/cache
    makes repeated bootstraps cheap: unchanged input can restore the native artifact,
    while changed compiler sources invalidate through Raz's normal fingerprints.
    A Stage-0/native-toolchain rebuild clears the workspace because backend/runtime
    inputs may have changed without changing Raz source text.
    """
    current_digest = _compiler_source_digest()
    if reset_cache and build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)
    for child in build_dir.iterdir():
        if child.name == "target":
            continue
        if child.is_dir() and not child.is_symlink():
            shutil.rmtree(child)
        else:
            child.unlink(missing_ok=True)
    _copy_compiler_project_inputs(build_dir, _link_or_copy_stage_source)
    remove_legacy_bootstrap_scratch(build_dir)

    # Seed only the assembled-project input cache on a first self-host build.
    # This avoids re-walking/re-reading 100+ unchanged modules but deliberately
    # does not copy native artifacts, MIR state, or semantic output from Stage 0.
    source_cache = seed_project / "target" / "cache"
    target_cache = build_dir / "target" / "cache"
    if source_cache.is_dir():
        target_cache.mkdir(parents=True, exist_ok=True)
        for name in ("project.meta", "project.source"):
            src = source_cache / name
            dst = target_cache / name
            if src.is_file() and not dst.is_file():
                shutil.copy2(src, dst)
    _refresh_bootstrap_input_cache(build_dir, current_digest)


def prepare_modular_compiler_build(build_dir: Path) -> None:
    """Refresh canonical modular compiler sources while retaining its build cache.

    The modular compiler is the production artifact developers keep between
    passes. Preserve target/ so unchanged package objects and the built compiler
    survive packaging/reuse, while refreshing canonical sources and invalidating
    whole-project cache keys when compiler inputs actually change.
    """
    build_dir.mkdir(parents=True, exist_ok=True)
    for child in build_dir.iterdir():
        if child.name == "target":
            continue
        if child.is_dir() and not child.is_symlink():
            shutil.rmtree(child)
        else:
            child.unlink(missing_ok=True)
    _copy_compiler_project_inputs(build_dir, _link_or_copy_stage_source)
    remove_legacy_bootstrap_scratch(build_dir)
    _refresh_bootstrap_input_cache(build_dir, _compiler_source_digest())


def prepare_reproducibility_verification(build_dir: Path) -> None:
    """Create an independent second generation for explicit reproducibility checks."""
    if build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)
    _copy_compiler_project_inputs(build_dir, _link_or_copy_stage_source)
    remove_legacy_bootstrap_scratch(build_dir)


def bootstrap_seed_jobs(requested: int) -> int:
    """Choose seed concurrency conservatively because Stage-0 semantic workers are memory-heavy."""
    total = 0
    try:
        if IS_WINDOWS:
            import ctypes

            class MEMORYSTATUSEX(ctypes.Structure):
                _fields_ = [
                    ("dwLength", ctypes.c_ulong),
                    ("dwMemoryLoad", ctypes.c_ulong),
                    ("ullTotalPhys", ctypes.c_ulonglong),
                    ("ullAvailPhys", ctypes.c_ulonglong),
                    ("ullTotalPageFile", ctypes.c_ulonglong),
                    ("ullAvailPageFile", ctypes.c_ulonglong),
                    ("ullTotalVirtual", ctypes.c_ulonglong),
                    ("ullAvailVirtual", ctypes.c_ulonglong),
                    ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
                ]

            status = MEMORYSTATUSEX()
            status.dwLength = ctypes.sizeof(MEMORYSTATUSEX)
            if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
                total = int(status.ullTotalPhys)
        elif hasattr(os, "sysconf"):
            pages = os.sysconf("SC_PHYS_PAGES")
            page_size = os.sysconf("SC_PAGE_SIZE")
            total = int(pages) * int(page_size)
    except (OSError, ValueError, AttributeError):
        total = 0

    if total <= 0:
        return max(1, min(requested, 4))
    # The frozen Stage-0 frontend can transiently use multiple GiB on large
    # dependency closures. Reserve ~3 GiB per concurrent seed worker. Normal
    # self-host builds use the Raz compiler and keep the user's requested jobs.
    memory_jobs = max(1, total // (3 * 1024 * 1024 * 1024))
    return max(1, min(requested, int(memory_jobs), 8))


def invoke_compiler(label: str, compiler: Path, build_dir: Path, args: list[str], interval: int, env: dict[str, str] | None = None) -> None:
    remove_legacy_bootstrap_scratch(build_dir)
    print(f"[RUN] {label}\n      {compiler} {' '.join(args)}", flush=True)
    diagnostic = build_dir / "compiler-diagnostic.txt"
    diagnostic.unlink(missing_ok=True)
    proc = subprocess.Popen([str(compiler), *args], cwd=build_dir, env=env)
    started = time.monotonic()
    last_diag = ""
    last_report = 0.0
    while proc.poll() is None:
        time.sleep(0.25)
        try:
            raw = diagnostic.read_text(encoding="utf-8").strip()
        except OSError:
            raw = ""
        if raw and raw != last_diag:
            last_diag = raw
            print(f"      [{time.monotonic() - started:6.1f}s] compiler diagnostic: {raw}", flush=True)
        elapsed = time.monotonic() - started
        if interval > 0 and elapsed - last_report >= interval:
            last_report = elapsed
            state = f"last diagnostic: {last_diag}" if last_diag else "waiting for compiler diagnostic"
            print(f"      [{elapsed:6.1f}s] still compiling; {state}", flush=True)
    if proc.returncode:
        raise RuntimeError(f"{label} failed with exit code {proc.returncode}. Diagnostic: {last_diag or 'none'}")
    diagnostic.unlink(missing_ok=True)
    removed = remove_legacy_bootstrap_scratch(build_dir)
    if removed:
        print(f"      removed {removed} legacy bootstrap scratch artifact(s)", flush=True)
    print(f"      completed in {time.monotonic() - started:.3f}s", flush=True)



def stage_support_file(source: Path, destination: Path) -> None:
    """Copy a support file into its staged location, tolerating a self-copy.

    A relocated host tree discovers these files where they were already staged,
    so source and destination name the same file. POSIX copies reject that and
    Windows fails it with a sharing violation, but the intended end state --
    the file present at the destination -- already holds.
    """
    if source.resolve() == destination.resolve():
        return
    shutil.copy2(source, destination)


def stage_compiler_runtime_support(compiler_exe: Path, runtime: Path, bridge: Path, forge: Path, host_build: Path, oblink: Path | None = None) -> None:
    """Stage relocatable native-link support beside a produced Raz compiler.

    The installed layout is self-describing: the Raz runtime and any optional
    third-party archives/import libraries live together under release/lib.
    """
    profile_root = compiler_exe.parent.parent
    lib_dir = profile_root / "lib"
    lib_dir.mkdir(parents=True, exist_ok=True)
    stage_support_file(runtime, lib_dir / runtime.name)
    # Self-hosted compiler package objects call the narrow C++ Forge bridge.
    # Stage both static archives in the same relocatable lib directory so a
    # compiler moved out of the CMake tree can link its next generation.
    stage_support_file(bridge, lib_dir / bridge.name)
    stage_support_file(forge, lib_dir / forge.name)
    # Windows package-unit linking uses the bundled ObLink executable. Keep it
    # beside the self-hosted compiler so the driver can resolve the linker from
    # its own installation rather than depending on the bootstrap cwd/PATH.
    if os.name == "nt" and oblink is not None and oblink.is_file():
        stage_support_file(oblink, compiler_exe.parent / oblink.name)
    deps = load_runtime_link_dependencies(host_build)
    if deps:
        ssl = Path(deps[0])
        crypto = Path(deps[1])
        if os.name == "nt":
            suffix = ".lib" if ssl.suffix.lower() == ".lib" else ".a"
            stage_support_file(ssl, lib_dir / f"raz_runtime_ssl{suffix}")
            suffix = ".lib" if crypto.suffix.lower() == ".lib" else ".a"
            stage_support_file(crypto, lib_dir / f"raz_runtime_crypto{suffix}")
        else:
            ssl_suffix = ".a" if ssl.suffix == ".a" else ".so"
            crypto_suffix = ".a" if crypto.suffix == ".a" else ".so"
            stage_support_file(ssl, lib_dir / f"libraz_runtime_ssl{ssl_suffix}")
            stage_support_file(crypto, lib_dir / f"libraz_runtime_crypto{crypto_suffix}")


def link_stage(compiler: str, obj: Path, runtime: Path, bridge: Path, forge: Path, output: Path, env: dict[str, str], runtime_deps: list[str], linker: Path | None = None) -> None:
    # The reproducibility generations are the only links in the bootstrap that
    # do not go through the raz driver, so without this they would reach for the
    # host C++ compiler even though the bundled linker is what ships. Keep the
    # C++ driver as the fallback for hosts ObLink does not target yet.
    if linker is not None and linker.is_file():
        args = [str(linker), str(obj), str(runtime), str(bridge), str(forge), *runtime_deps,
                "-l", "ws2_32", "-l", "bcrypt", "-l", "crypt32",
                "--stack", "33554432", "-o", str(output)]
        try:
            run(f"Link {output.name}", args, cwd=output.parent, env=env)
            return
        except RuntimeError as error:
            # The raz build driver falls back to the configured external linker
            # when ObLink cannot complete an image; keep the same contract here
            # so an unsupported input shape does not fail the whole bootstrap.
            print(f"      ObLink could not complete this image ({error}); using {compiler}", flush=True)
    name = Path(compiler).name.lower()
    if IS_WINDOWS and name in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}:
        # Reproducibility executables are the Raz compiler itself.  Reserve
        # 32 MiB on Windows to match the compiler-specific host link contract;
        # ordinary Raz programs continue to use the smaller default elsewhere.
        args = [compiler, "/nologo", str(obj), str(runtime), str(bridge), str(forge), *runtime_deps, "ws2_32.lib", "bcrypt.lib", "crypt32.lib", f"/Fe:{output}", "/link", "/STACK:33554432"]
    elif IS_WINDOWS:
        args = [compiler, str(obj), str(runtime), str(bridge), str(forge), *runtime_deps, "-o", str(output), "-lws2_32", "-lbcrypt", "-lcrypt32"]
    else:
        args = [compiler, str(obj), str(runtime), str(bridge), str(forge), *runtime_deps, "-o", str(output)]
        if sys.platform.startswith("linux"):
            args.extend(["-pthread", "-ldl"])
        elif not sys.platform.startswith("darwin"):
            args.append("-pthread")
    run(f"Link {output.name}", args, cwd=output.parent, env=env)


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def prepare_bootstrap_final_tree(qualification: Path, canonical_profile: Path) -> tuple[Path, Path]:
    """Stage only the canonical production target for final bootstrap output.

    All compiler-project/candidate/repro/web/regression directories under
    ``target/bootstrap`` are qualification workspaces.  They are valuable on a
    failed build, but retaining them after a successful bootstrap makes target/
    look like a test scratch directory and duplicates hundreds of megabytes of
    source, IR, objects, and caches.

    Build the final tree beside ``qualification`` first so the last known-good
    bootstrap output is not destroyed until staging has succeeded.  The caller
    can add relocatable runtime/linker support and BUILD-SUMMARY.txt to the
    returned staging directory before atomically promoting it.
    """
    if not canonical_profile.is_dir():
        raise RuntimeError(f"Canonical compiler profile is missing: {canonical_profile}")
    parent = qualification.parent
    staging = parent / ".bootstrap-final"
    shutil.rmtree(staging, ignore_errors=True)
    staged_profile = staging / "release"
    shutil.copytree(canonical_profile, staged_profile)
    return staging, staged_profile


def remove_tree_persistently(path: Path, attempts: int = 6) -> bool:
    """Delete a directory tree, tolerating read-only bits and transient locks.

    On Windows a virus scanner or search indexer routinely holds a just-written
    executable open for a moment, and the qualification tree is full of freshly
    linked binaries. A single `rmtree` therefore fails for reasons that resolve
    on their own, so retry briefly and clear read-only attributes as we go.
    """
    def clear_readonly(function, target, _exception):
        try:
            os.chmod(target, stat.S_IWRITE)
            function(target)
        except OSError:
            pass

    for attempt in range(attempts):
        if not path.exists():
            return True
        shutil.rmtree(path, onexc=clear_readonly)
        if not path.exists():
            return True
        time.sleep(0.25 * (attempt + 1))
    return not path.exists()


def promote_bootstrap_final_tree(qualification: Path, staging: Path) -> None:
    """Replace qualification scratch with the completed canonical target tree."""
    if not staging.is_dir() or not (staging / "release").is_dir():
        raise RuntimeError(f"Bootstrap final staging tree is incomplete: {staging}")
    if not remove_tree_persistently(qualification):
        # Something outside this build still holds a qualification artifact open.
        # The staged tree is already complete and validated, so move the scratch
        # aside rather than losing a finished bootstrap to a stray file handle.
        aside = qualification.with_name(qualification.name + ".discard")
        remove_tree_persistently(aside)
        try:
            qualification.replace(aside)
        except OSError as error:
            raise RuntimeError(
                f"Could not clear the qualification tree for promotion: {qualification} ({error})"
            ) from error
        remove_tree_persistently(aside)
    staging.replace(qualification)


def retain_user_facing_compiler(staged_profile: Path) -> Path:
    """Rename the internal compiler package executable to the public `raz` CLI.

    The compiler package intentionally remains named `raz-compiler` so package
    identity, module hashes, and self-host artifacts stay stable.  Only the
    successfully qualified retained product is renamed to the command users run.
    """
    internal = staged_profile / "bin" / f"raz-compiler{EXE}"
    public = staged_profile / "bin" / f"raz{EXE}"
    if not internal.is_file():
        raise RuntimeError(f"Final staged compiler package executable is missing: {internal}")
    public.unlink(missing_ok=True)
    internal.replace(public)
    return public


def prune_empty_directories(root: Path) -> int:
    """Remove empty artifact directories from a completed retained profile."""
    removed = 0
    directories = sorted((path for path in root.rglob("*") if path.is_dir()), key=lambda path: len(path.parts), reverse=True)
    for path in directories:
        try:
            path.rmdir()
            removed += 1
        except OSError:
            pass
    return removed


def main() -> int:
    config = load_bootstrap_config()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap-profile", "-BootstrapProfile", choices=("debug", "release"), default=str(config.get("bootstrap-profile", "debug")))
    parser.add_argument("--host-preset", "-HostPreset", choices=("debug", "release"), default=str(config.get("host-preset", "release")))
    parser.add_argument(
        "--seed-opt",
        choices=("0", "1", "2", "3"),
        default=str(config.get("seed-opt", "1")),
        help="Forge optimization level for the disposable compatibility-host seed compiler (default: 1)",
    )
    parser.add_argument(
        "--repro-opt",
        choices=("0", "1", "2", "3", "s", "z"),
        default=None,
        help="Forge optimization level for self-host reproducibility generations (default: 2 for release, 0 for debug)",
    )
    parser.add_argument("--jobs", "-Jobs", type=int, default=int(config.get("jobs", max(1, os.cpu_count() or 1))))
    parser.add_argument("--status-interval", type=int, default=int(config.get("status-interval", 15)))
    parser.add_argument("--clean", "-Clean", action="store_true", default=bool(config.get("clean", False)))
    parser.add_argument(
        "--rebuild-stage0",
        action="store_true",
        default=bool(config.get("rebuild-stage0", False)),
        help="force regeneration of the cached C++ Stage-0 toolchain",
    )
    parser.add_argument("--run-tests", "-RunTests", action="store_true", default=bool(config.get("run-tests", False)))
    parser.add_argument(
        "--verify-reproducibility",
        action="store_true",
        default=bool(config.get("verify-reproducibility", False)),
        help="build a second independent self-host generation and verify deterministic fixed-point output",
    )
    parser.add_argument(
        "--stage0",
        default=str(config.get("stage0", os.environ.get("RAZ_STAGE0_COMPILER", ""))),
        help="compatible prebuilt Raz compiler used to seed AArch64/macOS LLVM bootstrap (or set RAZ_STAGE0_COMPILER)",
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")

    repro_opt = args.repro_opt
    if repro_opt is None:
        configured_repro_opt = config.get("repro-opt")
        if configured_repro_opt is not None:
            repro_opt = str(configured_repro_opt)
        else:
            repro_opt = "2" if args.bootstrap_profile == "release" else "0"
    if repro_opt not in {"0", "1", "2", "3", "s", "z"}:
        parser.error("--repro-opt/bootstrap.repro-opt must be one of 0, 1, 2, 3, s, z")

    env = import_visual_studio_environment(os.environ.copy())
    host_arch = normalized_host_arch()
    if host_arch not in {"x86_64", "aarch64"}:
        raise RuntimeError(f"Unsupported bootstrap host architecture: {host_arch}")
    llvm_seed_bootstrap = host_arch == "aarch64" or sys.platform.startswith("darwin")
    repair_future_timestamps(ROOT)
    cmake = require_command("cmake", env)
    require_command("ninja", env)
    ctest = require_command("ctest", env) if args.run_tests else ""
    build_root = ROOT / "build"
    host_build = build_root / args.host_preset
    qualification = ROOT / "target" / "bootstrap"
    if args.clean:
        shutil.rmtree(host_build, ignore_errors=True)
        shutil.rmtree(qualification, ignore_errors=True)
    qualification.mkdir(parents=True, exist_ok=True)

    cached_stage0 = None if args.rebuild_stage0 else cached_stage0_artifacts(host_build)
    stage0_rebuilt = cached_stage0 is None
    if cached_stage0 is not None:
        banner(f"Reuse cached Stage-0 toolchain ({platform.system()})")
        compiler = resolve_cached_cxx_or_fallback(
            read_cache(host_build, "CMAKE_CXX_COMPILER"), env, build_root / ".toolchain-preflight"
        )
        print(f"Stage-0 compiler: {cached_stage0['driver']}")
        print(f"C++ linker fallback: {compiler}")
        print("Stage-0 cache: hit (use --rebuild-stage0 or -Clean to regenerate)")
    else:
        if args.rebuild_stage0 and host_build.exists():
            print("Stage-0 rebuild requested; discarding cached host toolchain.", flush=True)
            shutil.rmtree(host_build, ignore_errors=True)
        elif (host_build / "CMakeCache.txt").is_file() and not stage0_cache_matches_workspace(host_build):
            print("Relocated Stage-0 artifacts are stale or predate the portable cache contract; regenerating them.", flush=True)
            shutil.rmtree(host_build, ignore_errors=True)
        banner(f"Configure and build Stage-0 toolchain ({platform.system()})")
        compiler, fresh = choose_compiler(host_build, env, build_root / ".toolchain-preflight")
        print(f"C++ compiler: {compiler}\nJobs        : {args.jobs}")
        configure = [cmake, "--preset", args.host_preset]
        if fresh:
            configure.append(f"-DCMAKE_CXX_COMPILER={compiler}")
        run("Configure Stage-0 toolchain", configure, env=env)
        run("Build Stage-0 compiler/runtime/Forge", [cmake, "--build", "--preset", args.host_preset, "--parallel", str(args.jobs), "--target", "raz_host", "razc_host", "raz_runtime", "raz_forge_bridge", "forge", "oblink"], env=env)
        compiler = read_cache(host_build, "CMAKE_CXX_COMPILER")
        cached_stage0 = cached_stage0_artifacts(host_build)
        if cached_stage0 is None:
            raise RuntimeError("Stage-0 build completed without producing the complete reusable artifact set.")
    if IS_WINDOWS:
        env["RAZ_EXTERNAL_LINKER"] = compiler
        env.pop("RAZ_LINKER", None)  # Bundled ObLink is the Windows default.
    else:
        env["RAZ_LINKER"] = compiler

    host_driver = cached_stage0["driver"]
    # Built alongside Stage 0. ObLink emits PE32+ only, so it
    # is the bundled linker on Windows and nowhere else yet; elsewhere, and when
    # the bundled build is disabled, link_stage falls back to the C++ driver.
    oblink: Path | None = cached_stage0.get("oblink") if IS_WINDOWS else None
    runtime = cached_stage0["runtime"]
    bridge = cached_stage0["bridge"]
    forge = cached_stage0["forge"]

    # raz_runtime is a static archive. Any provider libraries used while
    # building it must be repeated at the final reproducibility-build link boundary.
    # For a relocated portable Stage-0, use the provider copies staged beside the
    # compiler instead of stale absolute paths from CMakeCache.txt.
    if stage0_cache_matches_workspace(host_build):
        repair_runtime_dependency_cache(host_build, cmake, env)
        runtime_deps = load_runtime_link_dependencies(host_build)
        stage_compiler_runtime_support(host_driver, runtime, bridge, forge, host_build, oblink)
        write_stage0_portable_cache_marker(host_build)
    else:
        runtime_deps = staged_runtime_link_dependencies(host_driver)

    # Recursive/candidate compiler qualification runs the compiler from a
    # temporary bootstrap directory rather than its installed bin/ location.
    # Provide the runtime and its transitive native dependencies explicitly so
    # package builds do not depend on installation-relative fallback discovery.
    env["RAZ_RUNTIME_LIBRARY"] = str(runtime)
    if runtime_deps:
        env["RAZ_RUNTIME_LINK_DEPS"] = ";".join(runtime_deps)
    else:
        env.pop("RAZ_RUNTIME_LINK_DEPS", None)

    if runtime_deps:
        print("Runtime link dependencies:")
        for dep in runtime_deps:
            print(f"  {dep}")

    modules = compiler_modules()
    print(f"Production compiler source: {len(modules)} Raz modules")

    banner("Construct production Raz compiler")
    if llvm_seed_bootstrap:
        stage0_text = args.stage0.strip()
        if not stage0_text:
            raise RuntimeError(
                "This host requires a compatible prebuilt Raz stage-0 compiler for LLVM bootstrap. "
                "Pass --stage0 /path/to/raz or set RAZ_STAGE0_COMPILER. "
                "The stage-0 compiler is used only to produce the first LLVM-native compiler object for this host."
            )
        stage0 = Path(stage0_text).expanduser().resolve()
        if not stage0.is_file():
            raise RuntimeError(f"LLVM stage-0 compiler not found: {stage0}")
        run("Validate LLVM stage-0 compiler", [str(stage0), "--version"], env=env)

        compiler_project = qualification / "compiler-project"
        prepare_seed_compiler_project(compiler_project)

        # Frozen Stage-0 predates cross-package compiler IR interfaces.  Keep the
        # canonical Raz-owned compiler split into HIR/MIR/borrowck packages, but present
        # those semantic packages as one disposable rustc_middle-like compatibility
        # package to Stage-0.  The resulting Raz seed immediately rebuilds the
        # canonical package graph in the self-host stage.
        _prepare_stage0_middle_compat(compiler_project)
        _prepare_stage0_native_codegen_compat(compiler_project)
        _prepare_stage0_driver_project_compat(compiler_project)
        _flatten_stage0_seed_package_sources(compiler_project)
        stage_layout = ensure_profile_output_layout(compiler_project, args.bootstrap_profile)
        remove_legacy_flat_profile_artifacts(compiler_project, args.bootstrap_profile)
        stage_object = stage_layout["obj"] / f"raz-compiler{OBJ}"
        invoke_compiler(
            "LLVM stage-0 -> production compiler object",
            stage0,
            compiler_project,
            [
                "build",
                "--backend=llvm",
                "--emit=obj",
                f"--opt={repro_opt}",
                "raz.toml",
                stage_object.relative_to(compiler_project).as_posix(),
            ],
            args.status_interval,
        )
        if not stage_object.is_file() or stage_object.stat().st_size < 100_000:
            raise RuntimeError(f"LLVM production compiler object is missing or unexpectedly small: {stage_object}")

        candidate_dir = qualification / "candidate"
        candidate_dir.mkdir(parents=True, exist_ok=True)
        candidate_compiler = candidate_dir / f"raz-compiler{EXE}"
        link_stage(compiler, stage_object, runtime, bridge, forge, candidate_compiler, env, runtime_deps, oblink)
        run("Validate LLVM production compiler", [str(candidate_compiler), "--version"], env=env)
        if not IS_WINDOWS:
            candidate_compiler.chmod(candidate_compiler.stat().st_mode | 0o111)
    else:
        compiler_project = qualification / "compiler-project"
        prepare_seed_compiler_project(compiler_project)
        # Frozen Stage-0 predates cross-package compiler IR interfaces.  Keep the
        # canonical Raz-owned compiler split into HIR/MIR/borrowck packages, but present
        # those semantic packages as one disposable rustc_middle-like compatibility
        # package to Stage-0.  The resulting Raz seed immediately rebuilds the
        # canonical package graph in the self-host stage.
        _prepare_stage0_middle_compat(compiler_project)
        _prepare_stage0_native_codegen_compat(compiler_project)
        _prepare_stage0_driver_project_compat(compiler_project)
        # Build the production seed through the normal semantic module graph.
        # The canonical compiler keeps each codegen backend in its own package.
        # Frozen Stage-0 only needs Forge/LLVM to construct the Raz-owned seed,
        # so Wasm/RXE/Web are represented by tiny signature-compatible packages
        # in this disposable bootstrap view. Canonical/self-host generations
        # compile the complete backend package graph unchanged.
        def write_seed_backend_stub(package_name: str, module_name: str, functions: str) -> None:
            package_dir = compiler_project / "src" / package_name
            shutil.rmtree(package_dir / "src", ignore_errors=True)
            (package_dir / "src" / module_name).mkdir(parents=True, exist_ok=True)
            namespace = package_name
            module_namespace = f"{namespace}::codegen"
            (package_dir / "src" / module_name / "codegen.rz").write_text(
                "// Copyright 2026 Mario Vinciguerra\n// SPDX-License-Identifier: Apache-2.0\n\n"
                f"namespace {module_namespace};\n\n"
                "public import middle::hir::core::model;\n"
                "public import middle::mir::core::model;\n"
                "public import frontend::parser;\n\n" + functions,
                encoding="utf-8",
            )
            (package_dir / "src" / "lib.rz").write_text(
                "// Copyright 2026 Mario Vinciguerra\n// SPDX-License-Identifier: Apache-2.0\n\n"
                f"namespace {namespace};\n\npublic import {module_namespace};\n",
                encoding="utf-8",
            )
            manifest = package_dir / "raz.toml"
            manifest_text = manifest.read_text(encoding="utf-8")
            deps_start = manifest_text.index("[dependencies]\n")
            profile_start = manifest_text.index("[profile.debug]", deps_start)
            manifest_text = manifest_text[:deps_start] + (
                "[dependencies]\nfrontend = \"../raz_parser\"\nmiddle = \"../raz_middle\"\n\n"
            ) + manifest_text[profile_start:]
            manifest.write_text(manifest_text, encoding="utf-8")

        write_seed_backend_stub(
            "raz_codegen_wasm", "wasm",
            "public fn emit_wasm_module(Source& source, HirModule& hir, MirModule& mir, i64 output_path, i64 output_path_length) -> bool { return false; }\n"
            "public fn emit_web_wasm_module(Source& source, HirModule& hir, MirModule& mir, i64 output_path, i64 output_path_length) -> bool { return false; }\n"
            "public fn emit_web_wasm_module_roots(Source& source, HirModule& hir, MirModule& mir, i64 output_path, i64 output_path_length, i64 browser_roots, i64 browser_roots_length) -> bool { return false; }\n"
            "public fn emit_web_wasm_module_roots_imports(Source& source, HirModule& hir, MirModule& mir, i64 output_path, i64 output_path_length, i64 browser_roots, i64 browser_roots_length, i64&mut out_import_mask, i64&mut out_import_mask_high) -> bool { *out_import_mask = 0; *out_import_mask_high = 0; return false; }\n"
        )
        write_seed_backend_stub(
            "raz_codegen_rxe", "rxe",
            "public fn emit_rxe_module(Source& source, HirModule& hir, MirModule& mir, i64 output_path, i64 output_path_length) -> bool { return false; }\n"
        )
        write_seed_backend_stub(
            "raz_codegen_web", "web",
            "public fn web_runtime_capabilities(Source& source, HirModule& hir, MirModule& mir) -> i64 { return 63; }\n"
            "public fn emit_web_wasm_module(Source& source, HirModule& hir, MirModule& mir, i64 output_path, i64 output_path_length) -> bool { return false; }\n"
            "public fn emit_web_wasm_module_roots(Source& source, HirModule& hir, MirModule& mir, i64 output_path, i64 output_path_length, i64 browser_roots, i64 browser_roots_length) -> bool { return false; }\n"
            "public fn emit_web_application(Source& source, HirModule& hir, MirModule& mir, i64 output_path, i64 output_path_length, i64 title, i64 title_length, i64 custom_index, i64 custom_index_length, i64 extra_css, i64 extra_css_length, i64 extra_javascript, i64 extra_javascript_length, bool release_profile) -> bool { return false; }\n"
        )
        # Static host pruning shares the canonical web browser-import bitmap
        # helper. The disposable Stage-0 web package omits the real JS emitter,
        # so retain this tiny signature-compatible module in the seed view.
        seed_web_host = compiler_project / "src" / "raz_codegen_web" / "src" / "web" / "browser_host_js.rz"
        seed_web_host.write_text(
            "// Copyright 2026 Mario Vinciguerra\n// SPDX-License-Identifier: Apache-2.0\n\n"
            "namespace raz_codegen_web::browser_host_js;\n\n"
            "public fn web_browser_import_enabled(i64 import_mask, i64 import_mask_high, i64 logical_import) -> bool { return false; }\n",
            encoding="utf-8",
        )
        seed_web_lib = compiler_project / "src" / "raz_codegen_web" / "src" / "lib.rz"
        seed_web_lib_text = seed_web_lib.read_text(encoding="utf-8")
        if "public import raz_codegen_web::browser_host_js;" not in seed_web_lib_text:
            seed_web_lib_text += "public import raz_codegen_web::browser_host_js;\n"
            seed_web_lib.write_text(seed_web_lib_text, encoding="utf-8")

        # Frozen Stage-0 does not propagate imported package symbols through a
        # re-exporting driver module reliably. Keep web emission as local seed
        # stubs in compiler_main; canonical sources and the Raz-owned self-host
        # continue to use the separate raz_codegen_web package.
        host_main = compiler_project / "src" / "raz_driver" / "src" / "driver" / "compiler_main.rz"
        main_text = host_main.read_text(encoding="utf-8")
        seed_web_stubs = (
            "\nfn web_runtime_capabilities(Source& source, HirModule& hir, MirModule& mir) -> i64 { return 63; }\n"
            "fn emit_web_wasm_module_roots(Source& source, HirModule& hir, MirModule& mir, i64 output_path, i64 output_path_length, i64 browser_roots, i64 browser_roots_length) -> bool { return false; }\n"
            "fn emit_web_wasm_module_roots_imports(Source& source, HirModule& hir, MirModule& mir, i64 output_path, i64 output_path_length, i64 browser_roots, i64 browser_roots_length, i64&mut out_import_mask, i64&mut out_import_mask_high) -> bool { *out_import_mask = 0; *out_import_mask_high = 0; return false; }\n"
            "fn emit_web_application(Source& source, HirModule& hir, MirModule& mir, i64 output_path, i64 output_path_length, i64 title, i64 title_length, i64 custom_index, i64 custom_index_length, i64 extra_css, i64 extra_css_length, i64 extra_javascript, i64 extra_javascript_length, bool release_profile) -> bool { return false; }\n"
        )
        if "fn emit_web_wasm_module_roots(" not in main_text:
            main_text = main_text.replace("import raz_driver::web_bundle;\n", "import raz_driver::web_bundle;\n" + seed_web_stubs, 1)
        host_main.write_text(main_text, encoding="utf-8")

        host_backend = compiler_project / "src" / "raz_driver" / "src" / "driver" / "backend.rz"
        backend_text = host_backend.read_text(encoding="utf-8")
        # Build the compatibility-host view by syntax landmarks, not indentation.
        backend_text, option_replacements = re.subn(
            r"(?ms)^[ \t]*// --backend=wasm\r?\n.*?(?=^[ \t]*return -1;)",
            "",
            backend_text,
            count=1,
        )
        if option_replacements != 1:
            raise RuntimeError("Could not prepare host-compatible backend option view.")

        for backend_kind, emitter in ((2, "emit_wasm_module"), (3, "emit_rxe_module")):
            pattern = (
                rf"(?ms)^[ \t]*if \(backend == {backend_kind}\) \{{\r?\n"
                rf"[ \t]*return {emitter}\([^;]*\);\r?\n"
                rf"[ \t]*\}}\r?\n"
            )
            backend_text, replacements = re.subn(pattern, "", backend_text, count=1)
            if replacements != 1:
                raise RuntimeError(f"Could not remove compatibility-host {emitter} dispatch.")

        if "emit_wasm_module(" in backend_text or "emit_rxe_module(" in backend_text:
            raise RuntimeError("Optional backend dispatch leaked into compatibility-host compiler view.")
        host_backend.write_text(backend_text, encoding="utf-8")

        # Canonical driver/lexer/parser packages use nested implementation
        # directories. Frozen Stage-0 only understands direct source modules, so
        # flatten those packages in this disposable seed project after all seed
        # compatibility rewrites have been applied.
        _flatten_stage0_seed_package_sources(compiler_project)

        # The compatibility-host compiler is only a seed for the Raz-owned
        # recursive generations. Building that seed at O2 has historically spent
        # minutes in Forge before self-hosting even starts, while an O0 seed makes
        # the 113-module canonical compiler unnecessarily slow to analyze. Use a
        # dedicated fast O1-by-default profile here: final reproducibility builds
        # still use --repro-opt and therefore retain the requested release policy.
        seed_profile = "bootstrap-seed"
        seed_manifest = compiler_project / "raz.toml"
        with seed_manifest.open("a", encoding="utf-8", newline="\n") as stream:
            stream.write(
                f"\n[profile.{seed_profile}]\n"
                f"optimization = {args.seed_opt}\n"
                "debug = false\n"
                "incremental = true\n"
            )
        # The module-batch package build validates the selected profile on each
        # dependency package as well as the root compiler package. Mirror the
        # disposable bootstrap profile into the copied package manifests only;
        # canonical source manifests remain unchanged.
        for package_manifest in (compiler_project / "src").glob("raz_*/raz.toml"):
            package_text = package_manifest.read_text(encoding="utf-8")
            marker = f"[profile.{seed_profile}]"
            if marker not in package_text:
                with package_manifest.open("a", encoding="utf-8", newline="\n") as stream:
                    stream.write(
                        f"\n{marker}\n"
                        f"optimization = {args.seed_opt}\n"
                        "debug = false\n"
                        "incremental = true\n"
                    )
        seed_jobs = bootstrap_seed_jobs(args.jobs)
        print(f"Stage-0 seed jobs: {seed_jobs} (requested {args.jobs})", flush=True)
        seed_command = [
            str(host_driver), "build", str(compiler_project), "--profile", seed_profile,
            "--jobs", str(seed_jobs),
        ]
        # A newly regenerated Stage 0 is a different bootstrap implementation;
        # force one seed rebuild so an artifact produced by an older Stage 0 can
        # never be accepted solely because the Raz source fingerprint is unchanged.
        # With the persistent Stage-0 cache, normal bootstraps omit --force and
        # reuse the seed project's own incremental cache.
        if stage0_rebuilt:
            seed_command.append("--force")
        invoke_compiler(
            f"Stage-0 compiler -> Raz seed (O{args.seed_opt})",
            host_driver,
            compiler_project,
            seed_command[1:],
            args.status_interval,
            env,
        )
        built = compiler_project / "target" / seed_profile / "bin" / f"raz-compiler{EXE}"
        if not built.is_file():
            raise RuntimeError(f"Production compiler was not produced: {built}")
        candidate_dir = qualification / "candidate"
        candidate_dir.mkdir(parents=True, exist_ok=True)
        candidate_compiler = candidate_dir / f"raz-compiler{EXE}"
        shutil.copy2(built, candidate_compiler)
        if not IS_WINDOWS:
            candidate_compiler.chmod(candidate_compiler.stat().st_mode | 0o111)

    # Qualify the language server in the compiler users actually receive. The
    # C++ host remains a bootstrap boundary; editor protocol behavior belongs to
    # the Raz-written candidate and is checked before recursive generations.
    run(
        "Production language server protocol",
        [sys.executable, str(ROOT / "tests" / "python" / "check-production-lsp.py"), "--raz", str(candidate_compiler)],
        env=env,
    )
    run(
        "Production semantic language server protocol",
        [sys.executable, str(ROOT / "tests" / "python" / "check-lsp-semantic-index.py"), "--raz", str(candidate_compiler)],
        env=env,
    )
    run(
        "Production project language server index",
        [sys.executable, str(ROOT / "tests" / "python" / "check-lsp-project-index.py"), "--raz", str(candidate_compiler)],
        env=env,
    )
    run(
        "Production registry language server index",
        [sys.executable, str(ROOT / "tests" / "python" / "check-lsp-registry-index.py"), "--raz", str(candidate_compiler)],
        env=env,
    )
    run(
        "Production command help",
        [sys.executable, str(ROOT / "tests" / "python" / "check-cli-command-help.py"), "--raz", str(candidate_compiler)],
        env=env,
    )
    run(
        "Production C bindgen",
        [sys.executable, str(ROOT / "tests" / "python" / "check-bindgen.py"), "--raz", str(candidate_compiler)],
        env=env,
    )
    run(
        "Production C header export",
        [sys.executable, str(ROOT / "tests" / "python" / "check-c-header.py"), "--raz", str(candidate_compiler)],
        env=env,
    )
    run(
        "Production aggregate ownership regressions",
        [
            sys.executable,
            str(ROOT / "tests" / "python" / "check-production-runtime-regressions.py"),
            "--raz",
            str(candidate_compiler),
            "--work-root",
            str(qualification / "production-runtime-regressions"),
        ],
        env=env,
    )
    run(
        "Production compiler arena ABI",
        [
            sys.executable,
            str(ROOT / "tests" / "python" / "check-compiler-arena-abi.py"),
            "--raz",
            str(candidate_compiler),
            "--work-root",
            str(qualification / "compiler-arena-abi"),
        ],
        env=env,
    )
    generated: list[tuple[int, Path, Path, str]] = []

    # Normal bootstrap performs exactly one Raz-owned self-host generation.
    # That proves the production compiler can compile itself while keeping the
    # common developer path fast. A second *independent* generation is available
    # only when deterministic fixed-point verification is explicitly requested.
    self_host_dir = qualification / "repro-1"
    prepare_self_host_build(self_host_dir, compiler_project, stage0_rebuilt)
    banner("Raz self-host build")
    stage_layout = ensure_profile_output_layout(self_host_dir, args.bootstrap_profile)
    remove_legacy_flat_profile_artifacts(self_host_dir, args.bootstrap_profile)
    obj = stage_layout["obj"] / f"raz-compiler{OBJ}"
    obj_argument = obj.relative_to(self_host_dir).as_posix()
    if llvm_seed_bootstrap:
        generation_args = [
            "build",
            "--release",
            "--backend=llvm",
            "--emit=obj",
            f"--opt={repro_opt}",
            "raz.toml",
            obj_argument,
        ]
    else:
        generation_args = [
            "build",
            "--release",
            "--backend=forge",
            "--forge-native",
            "--forge-structured-only",
            f"--opt={repro_opt}",
            "raz.toml",
            obj_argument,
        ]
    invoke_compiler(
        "Compile Raz self-host generation",
        candidate_compiler,
        self_host_dir,
        generation_args,
        args.status_interval,
        env,
    )
    minimum_object_size = 100_000 if llvm_seed_bootstrap else 500_000
    if not obj.is_file() or obj.stat().st_size < minimum_object_size:
        raise RuntimeError(f"Self-host compiler object is missing or unexpectedly small: {obj}")
    self_host_compiler = stage_layout["bin"] / f"raz-compiler{EXE}"
    link_stage(compiler, obj, runtime, bridge, forge, self_host_compiler, env, runtime_deps, oblink)
    stage_compiler_runtime_support(self_host_compiler, runtime, bridge, forge, host_build, oblink)
    run("Validate Raz self-host compiler", [str(self_host_compiler), "--version"], cwd=self_host_dir, env=env)
    run(
        "Self-host native project output",
        [sys.executable, str(ROOT / "tests" / "python" / "check-selfhost-native-project.py"), "--raz", str(self_host_compiler)],
        env=env,
    )
    run(
        "Self-host native artifact layout",
        [sys.executable, str(ROOT / "tests" / "python" / "check-native-artifact-layout.py"), "--raz", str(self_host_compiler)],
        env=env,
    )
    run(
        "Self-host native global artifact layout",
        [sys.executable, str(ROOT / "tests" / "python" / "check-native-global-artifact-layout.py"), "--raz", str(self_host_compiler)],
        env=env,
    )

    # Build the canonical compiler once through the ordinary modular package
    # linker as well. The bootstrap compiler itself needs Forge bridge/backend
    # support beyond a normal application's runtime, so provide those as
    # explicit bootstrap-only link inputs. The resulting tree is our hard proof
    # that every compiler package (including raz_query, raz_borrowck, and raz_mir_opt) materializes an object
    # under target/<profile>/packages/ rather than falling back to one compiler
    # object.
    compiler_package_layout = qualification / "compiler-package-layout"
    prepare_modular_compiler_build(compiler_package_layout)
    compiler_package_env = dict(env)
    compiler_link_deps = [str(bridge), str(forge)] + list(runtime_deps)
    compiler_package_env["RAZ_RUNTIME_LIBRARY"] = str(runtime)
    compiler_package_env["RAZ_RUNTIME_LINK_DEPS"] = ";".join(compiler_link_deps)
    invoke_compiler(
        "Self-host modular compiler package objects",
        self_host_compiler,
        compiler_package_layout,
        ["build", "--release", "raz.toml"],
        args.status_interval,
        compiler_package_env,
    )
    run(
        "Self-host compiler package artifact layout",
        [
            sys.executable,
            str(ROOT / "tests" / "python" / "check-compiler-native-artifact-layout.py"),
            "--project-root",
            str(compiler_package_layout),
        ],
        env=compiler_package_env,
    )
    # The ordinary modular package build is the canonical production compiler
    # artifact.  Keep qualification on that exact executable: an independently
    # linked whole-program bootstrap generation proves self-hosting, but it is not
    # the artifact developers retain or ship.  Qualifying the modular executable
    # prevents a bootstrap-only generation/cache anomaly from being promoted over
    # an otherwise healthy package-unit compiler.
    qualified_compiler = compiler_package_layout / "target" / "release" / "bin" / f"raz-compiler{EXE}"
    if not qualified_compiler.is_file():
        raise RuntimeError(f"Canonical modular compiler is missing: {qualified_compiler}")
    # Keep the canonical package-layout compiler self-contained immediately, not
    # only after final bootstrap promotion. This makes interrupted/packaged
    # qualification workspaces usable for native builds after relocation.
    stage_compiler_runtime_support(qualified_compiler, runtime, bridge, forge, host_build, oblink)
    run("Validate canonical modular compiler", [str(qualified_compiler), "--version"], cwd=compiler_package_layout, env=compiler_package_env)

    # The Stage-0 compatibility seed deliberately omits the Wasm/RXE backend
    # implementation to keep host construction bounded. Web qualification must
    # therefore run on the first full Raz-owned self-host compiler, which
    # contains the canonical Wasm and reactive web backends.
    for web_label, web_script, work_name in (
        ("Self-host static web target", "check-web-static.py", "web-static"),
        ("Self-host static component rendering", "check-web-static-components.py", "web-static-components"),
        ("Self-host static component browser guard", "check-web-static-component-guard.py", "web-static-component-guard"),
        ("Self-host semantic web authoring", "check-web-authoring.py", "web-authoring"),
        ("Self-host content-addressed web assets", "check-web-assets.py", "web-assets"),
        ("Self-host web forms", "check-web-forms.py", "web-forms"),
        # Run the smallest reactive bundle before routing. On Windows this
        # distinguishes a shared reactive/Wasm emitter fault from a routing-only
        # regression and fails earlier with focused partial-artifact telemetry.
        ("Self-host reactive web target", "check-web-reactive.py", "web-reactive"),
        ("Self-host web routing", "check-web-routing.py", "web-routing"),
        ("Self-host browser API", "check-web-browser-api.py", "web-browser-api"),
        ("Self-host web dev server", "check-web-dev.py", "web-dev"),
        ("Self-host lazy client module chunks", "check-web-lazy-modules.py", "web-lazy-modules"),
        ("Self-host split Raz-WASM chunks", "check-web-wasm-chunks.py", "web-wasm-chunks"),
        ("Self-host interactive web target", "check-web-interactive.py", "web-interactive"),
        ("Self-host pruned browser Wasm imports", "check-web-wasm-import-pruning.py", "web-wasm-import-pruning"),
        ("Self-host compact browser Wasm graph", "check-web-wasm-function-compaction-runtime.py", "web-wasm-function-compaction"),
        ("Self-host pruned reactive JS runtime", "check-web-runtime-pruning-runtime.py", "web-runtime-pruning"),
        ("Self-host scoped component state", "check-web-component-state.py", "web-component-state"),
        ("Self-host browser bundle hardening", "check-web-bundle-hardening.py", "web-bundle-hardening"),
        ("Self-host web bundle analysis budgets", "check-web-bundle-analysis.py", "web-bundle-analysis"),
        ("Self-host combined web production hardening", "check-web-production-hardening-runtime.py", "web-production-hardening"),
        ("Self-host web release artifact contract", "check-web-release-artifact-contract-runtime.py", "web-release-artifact-contract"),
    ):
        run(
            web_label,
            [
                sys.executable,
                str(ROOT / "tests" / "python" / web_script),
                "--raz",
                str(qualified_compiler),
                "--work-root",
                str(qualification / work_name),
            ],
            env=env,
        )

    generated.append((1, obj, self_host_compiler, digest(obj)))

    verification_hash = ""
    verification_size = 0
    deterministic_convergence = "not requested"
    if args.verify_reproducibility:
        banner("Reproducibility verification generation")
        verify_dir = qualification / "repro-2"
        prepare_reproducibility_verification(verify_dir)
        verify_layout = ensure_profile_output_layout(verify_dir, args.bootstrap_profile)
        remove_legacy_flat_profile_artifacts(verify_dir, args.bootstrap_profile)
        verify_obj = verify_layout["obj"] / f"raz-compiler{OBJ}"
        verify_argument = verify_obj.relative_to(verify_dir).as_posix()
        if llvm_seed_bootstrap:
            verify_args = [
                "build", "--release", "--backend=llvm", "--emit=obj", f"--opt={repro_opt}",
                "raz.toml", verify_argument,
            ]
        else:
            verify_args = [
                "build", "--release", "--backend=forge", "--forge-native", "--forge-structured-only",
                f"--opt={repro_opt}", "raz.toml", verify_argument,
            ]
        invoke_compiler(
            "Compile independent reproducibility generation",
            self_host_compiler,
            verify_dir,
            verify_args,
            args.status_interval,
            env,
        )
        if not verify_obj.is_file() or verify_obj.stat().st_size < minimum_object_size:
            raise RuntimeError(f"Reproducibility object is missing or unexpectedly small: {verify_obj}")
        verify_compiler = verify_layout["bin"] / f"raz-compiler{EXE}"
        link_stage(compiler, verify_obj, runtime, bridge, forge, verify_compiler, env, runtime_deps, oblink)
        stage_compiler_runtime_support(verify_compiler, runtime, bridge, forge, host_build, oblink)
        run("Validate reproducibility compiler", [str(verify_compiler), "--version"], cwd=verify_dir, env=env)
        verify_digest = digest(verify_obj)
        generated.append((2, verify_obj, verify_compiler, verify_digest))
        if obj.stat().st_size != verify_obj.stat().st_size or generated[0][3] != verify_digest:
            raise RuntimeError("Compiler fixed-point verification failed: self-host and verification generations differ.")
        verification_hash = verify_digest
        verification_size = verify_obj.stat().st_size
        deterministic_convergence = "yes"
        print(f"Reproducibility verified ({verification_size} object bytes)\nSHA-256: {verification_hash}")

    if args.run_tests:
        banner("Run CTest qualification")
        run("CTest", [ctest, "--test-dir", str(host_build), "--output-on-failure", "-j", str(args.jobs)], env=env)

    # Successful bootstrap output is a product target, not a dump of every
    # qualification workspace.  Preserve scratch trees on failure for debugging,
    # but after every gate passes promote only the canonical modular compiler
    # profile plus the relocatable runtime/linker support it needs.
    canonical_profile = compiler_package_layout / "target" / "release"
    staging, staged_profile = prepare_bootstrap_final_tree(qualification, canonical_profile)
    staged_compiler = retain_user_facing_compiler(staged_profile)
    stage_compiler_runtime_support(staged_compiler, runtime, bridge, forge, host_build, oblink)
    prune_empty_directories(staged_profile)
    run("Validate retained production compiler", [str(staged_compiler), "--version"], cwd=staging, env=env)

    final_compiler = qualification / "release" / "bin" / f"raz{EXE}"
    production_digest = digest(staged_compiler)
    retained_relative = Path("release") / "bin" / f"raz{EXE}"
    summary_text = (
        "Raz compiler construction and self-host qualification succeeded.\n\n"
        f"Platform: {platform.platform()}\n"
        f"Host architecture: {host_arch}\n"
        f"Host toolchain preset: {args.host_preset}\n"
        f"Qualification workspace profile: {args.bootstrap_profile}\n"
        f"Seed compiler profile: {'stage0/LLVM' if llvm_seed_bootstrap else seed_profile}\n"
        f"Seed optimization: {args.seed_opt if not llvm_seed_bootstrap else 'stage0/LLVM'}\n"
        f"Self-host build profile: release\n"
        f"Self-host optimization: {repro_opt}\n"
        f"Self-host backend: {'LLVM' if llvm_seed_bootstrap else 'Forge'}\n"
        f"Retained compiler profile: release\n"
        f"C++ compiler: {compiler}\n\n"
        f"Production compiler: {retained_relative.as_posix()}\n"
        f"Production compiler SHA-256: {production_digest}\n"
        f"Self-host proof: passed\n"
        f"Self-host object bytes: {generated[0][1].stat().st_size}\n"
        f"Self-host object SHA-256: {generated[0][3]}\n"
        f"Deterministic convergence: {deterministic_convergence}\n"
        + ((f"Verified fixed-point object bytes: {verification_size}\nVerified fixed-point SHA-256: {verification_hash}\n") if args.verify_reproducibility else "")
    )
    (staging / "BUILD-SUMMARY.txt").write_text(summary_text, encoding="utf-8")
    promote_bootstrap_final_tree(qualification, staging)
    summary = qualification / "BUILD-SUMMARY.txt"

    banner("COMPILER SELF-HOST QUALIFICATION COMPLETE")
    print(f"Production compiler: {final_compiler}\nArtifacts          : {qualification}\nSummary            : {summary}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nBootstrap interrupted.", file=sys.stderr)
        raise SystemExit(130)
    except Exception as exc:
        print(f"\nBUILD FAILED\n{exc}", file=sys.stderr)
        # An OS-level failure (a locked file, a missing tool) says nothing about
        # where it happened, which is the only thing that makes it actionable.
        if os.environ.get("RAZ_BOOTSTRAP_TRACEBACK"):
            traceback.print_exc()
        raise SystemExit(1)
