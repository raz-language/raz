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
            raise RuntimeError(
                f"The existing CMake cache refers to a missing compiler: {cached}. "
                "Run bootstrap.bat -Clean once to regenerate the host build."
            )
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


def source_order() -> list[str]:
    order = []
    for raw in (ROOT / "compiler" / "host-source-order.txt").read_text(encoding="utf-8").splitlines():
        item = raw.strip().replace("\\", "/")
        if item and not item.startswith("#"):
            if not item.startswith("src/"):
                raise RuntimeError(f"Invalid compiler source-order entry: {item}")
            order.append(item)
    if not order or order[-1] != "src/main.rz":
        raise RuntimeError("compiler/host-source-order.txt must be non-empty and end with src/main.rz.")
    return order


def _link_or_copy_stage_source(source: str, destination: str) -> str:
    """Hard-link immutable compiler inputs when possible; copy as a fallback."""
    try:
        os.link(source, destination)
        return destination
    except OSError:
        return shutil.copy2(source, destination)


def prepare_reproducibility_build(build_dir: Path, order: list[str]) -> None:
    if build_dir.exists():
        shutil.rmtree(build_dir)
    # Reproducibility stages only read the canonical compiler sources. Hard-link
    # those immutable inputs instead of copying the complete compiler tree for
    # every generation. Generated .raz/target state remains stage-local.
    shutil.copytree(
        ROOT / "compiler",
        build_dir,
        ignore=shutil.ignore_patterns(".raz", "target"),
        copy_function=_link_or_copy_stage_source,
    )
    (build_dir / "source-order.txt").write_text("\n".join(order) + "\n", encoding="ascii")


def invoke_compiler(label: str, compiler: Path, build_dir: Path, args: list[str], interval: int) -> None:
    print(f"[RUN] {label}\n      {compiler} {' '.join(args)}", flush=True)
    diagnostic = build_dir / "compiler-diagnostic.txt"
    diagnostic.unlink(missing_ok=True)
    proc = subprocess.Popen([str(compiler), *args], cwd=build_dir)
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
    print(f"      completed in {time.monotonic() - started:.3f}s", flush=True)


def link_stage(compiler: str, obj: Path, runtime: Path, bridge: Path, forge: Path, output: Path, env: dict[str, str], runtime_deps: list[str]) -> None:
    name = Path(compiler).name.lower()
    if IS_WINDOWS and name in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}:
        # Reproducibility executables are the Raz compiler itself.  Reserve
        # 32 MiB on Windows to match the compiler-specific host link contract;
        # ordinary Raz programs continue to use the smaller default elsewhere.
        args = [compiler, "/nologo", str(obj), str(runtime), str(bridge), str(forge), *runtime_deps, "ws2_32.lib", "bcrypt.lib", "crypt32.lib", f"/Fe:{output}", "/link", "/STACK:33554432"]
    elif IS_WINDOWS:
        args = [compiler, str(obj), str(runtime), str(bridge), str(forge), *runtime_deps, "-o", str(output), "-lws2_32", "-lbcrypt", "-lcrypt32"]
    else:
        args = [compiler, str(obj), str(runtime), str(bridge), str(forge), *runtime_deps, "-o", str(output), "-pthread"]
        if sys.platform.startswith("linux"):
            args.append("-ldl")
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
        "--repro-opt",
        choices=("0", "1", "2", "3", "s", "z"),
        default=None,
        help="Forge optimization level for self-host reproducibility generations (default: 2 for release, 0 for debug)",
    )
    parser.add_argument("--jobs", "-Jobs", type=int, default=int(config.get("jobs", max(1, os.cpu_count() or 1))))
    parser.add_argument("--status-interval", type=int, default=int(config.get("status-interval", 15)))
    parser.add_argument("--clean", "-Clean", action="store_true", default=bool(config.get("clean", False)))
    parser.add_argument("--run-tests", "-RunTests", action="store_true", default=bool(config.get("run-tests", False)))
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
    repair_future_timestamps(ROOT)
    cmake = require_command("cmake", env)
    require_command("ninja", env)
    ctest = require_command("ctest", env) if args.run_tests else ""
    host_build = ROOT / "build" / args.host_preset
    qualification = ROOT / "build" / "compiler-qualification"
    if args.clean:
        shutil.rmtree(host_build, ignore_errors=True)
        shutil.rmtree(qualification, ignore_errors=True)
    qualification.mkdir(parents=True, exist_ok=True)

    banner(f"Configure and build host toolchain ({platform.system()})")
    compiler, fresh = choose_compiler(host_build, env, ROOT / "build" / ".toolchain-preflight")
    print(f"C++ compiler: {compiler}\nJobs        : {args.jobs}")
    configure = [cmake, "--preset", args.host_preset]
    if fresh:
        configure.append(f"-DCMAKE_CXX_COMPILER={compiler}")
    run("Configure host toolchain", configure, env=env)
    run("Build host compiler/runtime/Forge", [cmake, "--build", "--preset", args.host_preset, "--parallel", str(args.jobs), "--target", "raz_host", "razc_host", "raz_runtime", "raz_forge_bridge", "forge"], env=env)
    compiler = read_cache(host_build, "CMAKE_CXX_COMPILER")
    env["RAZ_LINKER"] = compiler

    host_driver = find_artifact(host_build, [f"raz-host{EXE}"])
    runtime = find_artifact(host_build, ["raz_runtime.lib", "libraz_runtime.a"])
    bridge = find_artifact(host_build, ["raz_forge_bridge.lib", "libraz_forge_bridge.a"])
    forge = find_artifact(host_build, ["forge.lib", "libforge.a"])

    # raz_runtime is a static archive. Any provider libraries used while
    # building it must be repeated at the final reproducibility-build link boundary.
    # CMake exports the exact imported target files it selected into a manifest;
    # do not guess OpenSSL cache variable names because they vary by package and
    # platform configuration.
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

    openssl_enabled = read_cache(host_build, "OPENSSL_FOUND", required=False).upper() in {"1", "ON", "TRUE", "YES"}
    if not openssl_enabled:
        # FindOpenSSL commonly exposes OPENSSL_INCLUDE_DIR even when it does not
        # cache OPENSSL_FOUND. The runtime compile command is the final fallback
        # signal because RAZ_HAVE_OPENSSL is only defined when OpenSSL_FOUND.
        openssl_enabled = bool(read_cache(host_build, "OPENSSL_INCLUDE_DIR", required=False))
    if openssl_enabled and len(runtime_deps) < 2:
        raise RuntimeError(
            "OpenSSL is enabled for raz_runtime, but CMake did not export both "
            "OpenSSL link dependencies. Reconfigure the host build instead of "
            "attempting a broken reproducibility-build link."
        )

    if runtime_deps:
        print("Runtime link dependencies:")
        for dep in runtime_deps:
            print(f"  {dep}")

    order = source_order()
    print(f"Production compiler source: {len(order)} Raz modules")

    banner("Construct production Raz compiler")
    compiler_project = qualification / "compiler-project"
    shutil.rmtree(compiler_project, ignore_errors=True)
    shutil.copytree(ROOT / "compiler", compiler_project, ignore=shutil.ignore_patterns(".raz", "target"))
    # The compatibility-pinned host compiler predates semantic-module interfaces. Construct the production compiler from a
    # disposable legacy view: strip compiler-only module namespace/import edges
    # and materialize source-order.txt only in this temporary copy.
    #
    # RXE and WebAssembly are production backends, but the host construction path only
    # needs Forge/LLVM to construct the production compiler. Keeping the optional backends out of
    # this one host-compatible compiler candidate prevents the compatibility-pinned native host frontend from
    # crossing its peak-memory ceiling as the Raz-owned compiler grows. The production compiler and every reproducibility build compile the complete canonical source tree.
    for optional_backend in ("wasm", "rxe"):
        shutil.rmtree(compiler_project / "src" / "backend" / optional_backend, ignore_errors=True)
    legacy_order = [
        item for item in (compiler_project / "host-source-order.txt").read_text(encoding="utf-8").splitlines()
        if not item.startswith("src/backend/wasm/") and not item.startswith("src/backend/rxe/")
    ]
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
    for source in (compiler_project / "src").rglob("*.rz"):
        lines = source.read_text(encoding="utf-8").splitlines()
        lines = [line for line in lines if not line.startswith("namespace raz_compiler_") and not line.startswith("public import raz_compiler_") and not line.startswith("import raz_compiler_")]
        source.write_text("\n".join(lines) + "\n", encoding="utf-8")
    (compiler_project / "source-order.txt").write_text("\n".join(legacy_order) + "\n", encoding="ascii")
    run("Host compiler -> production compiler", [str(host_driver), "build", str(compiler_project), "--target", "host", "--profile", args.bootstrap_profile, "--force"], env=env)
    built = compiler_project / "target" / "host" / args.bootstrap_profile / f"raz-compiler{EXE}"
    if not built.is_file():
        raise RuntimeError(f"Production compiler was not produced: {built}")
    candidate_dir = qualification / "candidate"
    candidate_dir.mkdir(parents=True, exist_ok=True)
    candidate_compiler = candidate_dir / f"raz-compiler{EXE}"
    shutil.copy2(built, candidate_compiler)
    if not IS_WINDOWS:
        candidate_compiler.chmod(candidate_compiler.stat().st_mode | 0o111)

    generated: list[tuple[int, Path, Path, str]] = []
    previous = candidate_compiler
    for generation in range(1, 4):
        banner(f"Reproducibility build {generation}")
        directory = qualification / f"repro-{generation}"
        prepare_reproducibility_build(directory, order)
        obj = directory / f"compiler{OBJ}"
        invoke_compiler(
            f"Compile reproducibility generation {generation}",
            previous,
            directory,
            [
                "build",
                "--backend=forge",
                "--forge-native",
                "--forge-structured-only",
                f"--opt={repro_opt}",
                "raz.toml",
                obj.name,
            ],
            args.status_interval,
        )
        if not obj.is_file() or obj.stat().st_size < 500_000:
            raise RuntimeError(f"Reproducibility object is missing or unexpectedly small: {obj}")
        exe = directory / f"raz-compiler{EXE}"
        link_stage(compiler, obj, runtime, bridge, forge, exe, env, runtime_deps)
        run(f"Validate reproducibility build {generation}", [str(exe), "--version"], cwd=directory, env=env)
        generated.append((generation, obj, exe, digest(obj)))
        previous = exe

    banner("Verify deterministic compiler reproducibility")
    sizes = {item[1].stat().st_size for item in generated}
    hashes = {item[3] for item in generated}
    if len(sizes) != 1 or len(hashes) != 1:
        raise RuntimeError("Compiler reproducibility failed: generated native objects differ.")
    fixed_hash = generated[0][3]
    fixed_size = generated[0][1].stat().st_size
    print(f"Reproducibility verified ({fixed_size} object bytes)\nSHA-256: {fixed_hash}")

    if args.run_tests:
        banner("Run CTest qualification")
        run("CTest", [ctest, "--test-dir", str(host_build), "--output-on-failure", "-j", str(args.jobs)], env=env)

    summary = qualification / "BUILD-SUMMARY.txt"
    summary.write_text(
        "Raz compiler construction and reproducibility qualification succeeded.\n\n"
        f"Platform: {platform.platform()}\nHost preset: {args.host_preset}\nBootstrap profile: {args.bootstrap_profile}\nRepro optimization: {repro_opt}\nC++ compiler: {compiler}\n\n"
        + "\n".join(f"Reproducibility build {generation}: {exe}" for generation, _, exe, _ in generated)
        + f"\n\nFixed-point object bytes: {fixed_size}\nFixed-point SHA-256: {fixed_hash}\nDeterministic convergence: yes\n",
        encoding="utf-8",
    )
    banner("COMPILER QUALIFICATION COMPLETE")
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
