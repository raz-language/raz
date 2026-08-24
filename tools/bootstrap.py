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
import subprocess
import sys
import tempfile
import time
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IS_WINDOWS = os.name == "nt"
EXE = ".exe" if IS_WINDOWS else ""
OBJ = ".obj" if IS_WINDOWS else ".o"


BOOTSTRAP_LEGACY_SCRATCH_NAMES = {"host-source-order.txt", "stage1-diagnostic.txt"}
PROFILE_OUTPUT_DIRECTORIES = ("bin", "lib", "obj", "ir", "modules", "packages")


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


def load_runtime_link_dependencies(host_build: Path) -> list[str]:
    """Return native libraries required by raz_runtime for recursive links.

    CMake exports both the exact link inputs and an authoritative feature bit.
    Do not infer OpenSSL enablement from FindOpenSSL implementation details:
    OPENSSL_INCLUDE_DIR can be cached on Windows after only the headers were
    discovered, even though no usable SSL/Crypto library pair was selected.
    """
    runtime_deps: list[str] = []
    runtime_deps_manifest = host_build / "raz-runtime-link-deps.txt"
    if runtime_deps_manifest.is_file():
        for raw in runtime_deps_manifest.read_text(encoding="utf-8", errors="replace").splitlines():
            value = raw.strip()
            if not value:
                continue
            dep = Path(value)
            if not dep.is_file():
                raise RuntimeError(f"CMake resolved a runtime link dependency that does not exist: {dep}")
            runtime_deps.append(str(dep))

    openssl_state = read_cache(host_build, "RAZ_RUNTIME_OPENSSL_ENABLED", required=False).upper()
    true_values = {"1", "ON", "TRUE", "YES"}
    false_values = {"0", "OFF", "FALSE", "NO"}
    if openssl_state not in true_values | false_values:
        raise RuntimeError(
            "CMake did not export RAZ_RUNTIME_OPENSSL_ENABLED. "
            "Reconfigure the host build before recursive compiler linking."
        )
    if openssl_state in true_values and len(runtime_deps) < 2:
        raise RuntimeError(
            "OpenSSL is enabled for raz_runtime, but CMake did not export both "
            "OpenSSL link dependencies. Reconfigure the host build instead of "
            "attempting a broken reproducibility-build link."
        )
    return runtime_deps


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

def find_artifact(root: Path, names: list[str]) -> Path:
    for name in names:
        hits = list(root.rglob(name))
        if hits:
            return hits[0].resolve()
    raise RuntimeError(f"Could not find any of {names} below {root}.")


def find_optional_artifact(root: Path, names: list[str]) -> Path | None:
    try:
        return find_artifact(root, names)
    except RuntimeError:
        return None


def compiler_modules() -> list[Path]:
    """Discover the canonical Raz compiler source set without ordering metadata.

    Semantic imports, not physical file order, define the production compiler.
    Keep this helper intentionally boring: it exists for status/reporting and
    reproducibility workspace population only, never to impose compilation order.
    """
    source_root = ROOT / "compiler" / "src"
    modules = sorted(source_root.rglob("*.rz"))
    entry = source_root / "main.rz"
    if not modules or entry not in modules:
        raise RuntimeError("compiler/src/main.rz is missing from the production compiler source set.")
    return modules


def cached_stage0_artifacts(host_build: Path) -> dict[str, Path] | None:
    """Return a complete reusable Stage-0 artifact set, or None if incomplete.

    Stage 0 is deliberately frozen compatibility machinery. Re-running CMake/Ninja
    on every self-host cycle adds latency without improving the Raz-owned compiler.
    A normal bootstrap therefore reuses an existing Stage-0 build verbatim.
    `--clean` or `--rebuild-stage0` is the explicit opt-in path for rebuilding it.
    """
    cache = host_build / "CMakeCache.txt"
    if not cache.is_file():
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
    for key, names in required.items():
        artifact = find_optional_artifact(host_build, names)
        if artifact is None or not artifact.is_file():
            return None
        found[key] = artifact

    # Runtime link metadata is part of the cached ABI contract. Without it a
    # later reproducibility link could silently omit provider libraries.
    if not (host_build / "raz-runtime-link-deps.txt").is_file():
        return None
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
    inputs.extend(sorted(path for path in (root / "src").rglob("*") if path.is_file()))
    return inputs


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
        compiler = read_cache(host_build, "CMAKE_CXX_COMPILER")
        resolved_compiler = _resolve_executable(compiler, env)
        if resolved_compiler:
            compiler = resolved_compiler
        print(f"Stage-0 compiler: {cached_stage0['driver']}")
        print(f"C++ linker fallback: {compiler}")
        print("Stage-0 cache: hit (use --rebuild-stage0 or -Clean to regenerate)")
    else:
        if args.rebuild_stage0 and host_build.exists():
            print("Stage-0 rebuild requested; discarding cached host toolchain.", flush=True)
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
    runtime_deps = load_runtime_link_dependencies(host_build)

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
                "Pass --stage0 /path/to/raz-compiler or set RAZ_STAGE0_COMPILER. "
                "The stage-0 compiler is used only to produce the first LLVM-native compiler object for this host."
            )
        stage0 = Path(stage0_text).expanduser().resolve()
        if not stage0.is_file():
            raise RuntimeError(f"LLVM stage-0 compiler not found: {stage0}")
        run("Validate LLVM stage-0 compiler", [str(stage0), "--version"], env=env)

        compiler_project = qualification / "compiler-project"
        prepare_seed_compiler_project(compiler_project)
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
        # Build the production seed through the normal semantic module graph.
        # The canonical compiler is intentionally order-independent: preserve namespace/import
        # edges so the host build driver can materialize each module's semantic prerequisites.
        # Never recreate the old source-order.txt concatenation path here.
        #
        # RXE and WebAssembly are production backends, but the host construction path only
        # needs Forge/LLVM to construct the production compiler. Keeping the optional backends out of
        # this one host-compatible compiler candidate prevents the compatibility-pinned native host frontend from
        # crossing its peak-memory ceiling as the Raz-owned compiler grows. The production compiler and every reproducibility build compile the complete canonical source tree.
        for optional_backend in ("wasm", "rxe"):
            shutil.rmtree(compiler_project / "src" / "backend" / optional_backend, ignore_errors=True)
        host_backend = compiler_project / "src" / "driver" / "backend.rz"
        backend_text = host_backend.read_text(encoding="utf-8")
        backend_text = backend_text.replace(
            "public import raz_compiler_backend_rxe_codegen;",
            "public import raz_compiler_backend_llvm_codegen;",
        )

        # Build the compatibility-host view by syntax landmarks, not indentation.
        # `raz fmt` is free to change whitespace, so bootstrap construction must not
        # depend on an exact pretty-printed spelling of these optional backend blocks.
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
            "--backend=llvm",
            "--emit=obj",
            f"--opt={repro_opt}",
            "raz.toml",
            obj_argument,
        ]
    else:
        generation_args = [
            "build",
            "--backend=forge",
            "--forge-native",
            "--forge-structured-only",
            f"--opt={repro_opt}",
            "raz.toml",
            obj_argument,
        ]
    compile_env = dict(env)
    compile_env.setdefault("RAZ_COMPILER_PHASE_TRACE", "1")
    invoke_compiler(
        "Compile Raz self-host generation",
        candidate_compiler,
        self_host_dir,
        generation_args,
        args.status_interval,
        compile_env,
    )
    minimum_object_size = 100_000 if llvm_seed_bootstrap else 500_000
    if not obj.is_file() or obj.stat().st_size < minimum_object_size:
        raise RuntimeError(f"Self-host compiler object is missing or unexpectedly small: {obj}")
    self_host_compiler = stage_layout["bin"] / f"raz-compiler{EXE}"
    link_stage(compiler, obj, runtime, bridge, forge, self_host_compiler, env, runtime_deps, oblink)
    run("Validate Raz self-host compiler", [str(self_host_compiler), "--version"], cwd=self_host_dir, env=env)
    run(
        "Self-host native project output",
        [sys.executable, str(ROOT / "tests" / "python" / "check-selfhost-native-project.py"), "--raz", str(self_host_compiler)],
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
                "build", "--backend=llvm", "--emit=obj", f"--opt={repro_opt}",
                "raz.toml", verify_argument,
            ]
        else:
            verify_args = [
                "build", "--backend=forge", "--forge-native", "--forge-structured-only",
                f"--opt={repro_opt}", "raz.toml", verify_argument,
            ]
        invoke_compiler(
            "Compile independent reproducibility generation",
            self_host_compiler,
            verify_dir,
            verify_args,
            args.status_interval,
            compile_env,
        )
        if not verify_obj.is_file() or verify_obj.stat().st_size < minimum_object_size:
            raise RuntimeError(f"Reproducibility object is missing or unexpectedly small: {verify_obj}")
        verify_compiler = verify_layout["bin"] / f"raz-compiler{EXE}"
        link_stage(compiler, verify_obj, runtime, bridge, forge, verify_compiler, env, runtime_deps, oblink)
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

    summary = qualification / "BUILD-SUMMARY.txt"
    summary.write_text(
        "Raz compiler construction and self-host qualification succeeded.\n\n"
        f"Platform: {platform.platform()}\nHost architecture: {host_arch}\nHost preset: {args.host_preset}\nBootstrap profile: {args.bootstrap_profile}\nSeed optimization: {args.seed_opt if not llvm_seed_bootstrap else 'stage0/LLVM'}\nSelf-host optimization: {repro_opt}\nSelf-host backend: {'LLVM' if llvm_seed_bootstrap else 'Forge'}\nC++ compiler: {compiler}\n\n"
        + "\n".join(f"Self-host generation {generation}: {exe}" for generation, _, exe, _ in generated)
        + f"\n\nSelf-host object bytes: {generated[0][1].stat().st_size}\nSelf-host SHA-256: {generated[0][3]}\nDeterministic convergence: {deterministic_convergence}\n"
        + ((f"Verified fixed-point object bytes: {verification_size}\nVerified fixed-point SHA-256: {verification_hash}\n") if args.verify_reproducibility else ""),
        encoding="utf-8",
    )
    banner("COMPILER SELF-HOST QUALIFICATION COMPLETE")
    print(f"Artifacts: {qualification}\nSummary  : {summary}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nBootstrap interrupted.", file=sys.stderr)
        raise SystemExit(130)
    except Exception as exc:
        print(f"\nBUILD FAILED\n{exc}", file=sys.stderr)
        raise SystemExit(1)
