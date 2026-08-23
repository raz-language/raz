#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Static contracts for native project build artifact layout."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]

checks = 0

def require(path: str, needle: str, label: str) -> None:
    global checks
    text = (ROOT / path).read_text(encoding="utf-8")
    if needle not in text:
        raise AssertionError(f"{label}: missing {needle!r} in {path}")
    checks += 1

require("compiler/src/driver/project.rz", "fn project_default_native_paths(", "native path helper")
require("compiler/src/driver/project.rz", "raz_compiler_project_native_path_i64(", "platform artifact path bridge")
require("compiler/src/driver/project.rz", "raz_compiler_rt_path_exists_ascii(build.object_path, build.object_length) == 0", "native object existence invariant")
require("compiler/src/driver/project.rz", "raz_compiler_rt_path_exists_ascii(artifact_path, artifact_length) != 0", "final executable existence invariant")
require("compiler/src/driver/cli.rz", "fn cli_print_native_path_failure()", "native path diagnostic")
require("compiler/src/driver/cli.rz", "fn cli_print_backend_emit_failure()", "backend output diagnostic")
require("compiler/src/driver/cli.rz", "fn cli_print_native_link_failure()", "native link diagnostic")
require("compiler/src/driver/cli.rz", "fn cli_print_project_source_failure()", "project source assembly diagnostic")
# `build` and `run` both produce the default native artifact: run links and
# launches the same executable rather than interpreting the program.
require("compiler/src/main.rz", "(cli_command == 3 || cli_command == 12) && project_manifest && cli_output_path == 0", "project default artifact selection")
require("compiler/src/main.rz", "i64 exit_status = cli_maybe_run_artifact(", "run launches the linked artifact")
require("compiler/src/driver/cli.rz", "fn cli_run_native_artifact(", "native artifact launcher")
require("compiler/src/main.rz", "ProjectNativeBuild native_build;", "native project state")
require("compiler/src/main.rz", "forge_native = true;", "Forge object emission")
require("compiler/src/driver/project.rz", "fn project_native_build_finish(", "Forge executable finalization")
require("compiler/src/driver/project.rz", "raz_compiler_forge_link_executable_i64(", "Forge executable link")
require("compiler/src/backend/forge/writer.rz", "extern fn raz_compiler_project_native_path_i64(", "platform artifact bridge declaration")
require("compiler/src/backend/forge/writer.rz", "extern fn raz_compiler_forge_link_executable_i64(", "Raz link bridge declaration")
require("src/bootstrap/compiler/backend/forge/forge_bridge.cpp", "raz_compiler_project_native_path_i64(", "native artifact platform bridge")
require("src/bootstrap/compiler/backend/forge/forge_bridge.cpp", 'profile_root / "obj"', "native object subdirectory")
require("src/bootstrap/compiler/backend/forge/forge_bridge.cpp", 'package_name + ".exe"', "Windows executable suffix")
require("src/bootstrap/compiler/backend/forge/forge_bridge.cpp", 'package_name + ".obj"', "Windows object suffix")
require("src/bootstrap/tools/raz/detail/build_driver.hpp", 'project_bin_root(graph, options)', "executable artifact category")
require("src/bootstrap/tools/raz/detail/build_driver.hpp", 'project_library_root(graph, options)', "library artifact category")
require("src/bootstrap/tools/raz/detail/build_driver.hpp", 'project_object_root(graph, options)', "object artifact category")
require("src/bootstrap/tools/raz/detail/build_driver.hpp", 'project_ir_root(graph, options)', "IR artifact category")
require("src/bootstrap/tools/raz/detail/build_driver.hpp", 'project_module_root(graph, options)', "module metadata category")
require("src/bootstrap/tools/raz/detail/build_driver.hpp", 'project_package_root(graph, options)', "package artifact category")
require("src/bootstrap/tools/raz/detail/build_driver.hpp", 'ensure_project_output_layout(graph, options)', "profile category layout creation")
require("src/bootstrap/compiler/backend/forge/forge_bridge.cpp", 'environment_value("RAZ_LINKER")', "configurable native linker")
require("src/bootstrap/compiler/backend/forge/forge_bridge.cpp", "RAZ_OBLINK_PATH", "bundled ObLink bridge default")
require("src/bootstrap/tools/raz/detail/build_driver.hpp", "oblink_driver", "ObLink project-driver detection")
require("src/bootstrap/tools/raz/detail/build_driver.hpp", 'environment_value("RAZ_EXTERNAL_LINKER")', "explicit bootstrap fallback linker")
require("src/bootstrap/CMakeLists.txt", 'RAZ_OBLINK_PATH="$<TARGET_FILE:oblink>"', "ObLink executable injected into bootstrap")
require("src/bootstrap/compiler/backend/forge/forge_bridge.cpp", 'environment_value("RAZ_RUNTIME_LIBRARY")', "runtime override")
require("src/bootstrap/compiler/backend/forge/forge_bridge.cpp", "std::filesystem::is_regular_file(output) ? 1 : 0", "linked artifact validation")
require("src/bootstrap/CMakeLists.txt", 'RAZ_RUNTIME_LIBRARY_PATH="$<TARGET_FILE:raz_runtime>"', "bridge runtime path")
require("compiler/src/driver/cli.rz", "fn cli_release_option(", "release option")
require("docs/CLI.md", "target/debug/bin/<package>", "CLI native artifact docs")
require("docs/GETTING-STARTED.md", "target/debug/bin/hello.exe", "getting-started Windows artifact docs")

require("src/bootstrap/tools/raz/detail/cli_options.hpp", 'std::string target = "host";', "implicit host target default")
require("src/bootstrap/tools/raz/detail/build_driver.hpp", 'if (options.target == "host") return target_root / options.profile;', "host profile output flattening")
require("README.md", "Forge `.fir` output is reserved for explicit backend/emission workflows", "README FIR policy")

project = (ROOT / "compiler/src/driver/project.rz").read_text(encoding="utf-8")
if "debug_native[" in project or "release_native[" in project:
    raise AssertionError("platform-native path spelling must not be rebuilt in Raz")
checks += 1

# The legacy fallback may continue to exist for direct backend/debug workflows,
# but a normal manifest build must synthesize a concrete native output path
# before backend emission.
main = (ROOT / "compiler/src/main.rz").read_text(encoding="utf-8")
codegen = (ROOT / "compiler/src/backend/forge/codegen.rz").read_text(encoding="utf-8")
legacy_fir = "i64 path[17] = [115, 116, 97, 103, 101, 49, 45, 111, 117, 116, 112, 117, 116, 46, 102, 105, 114];"
if legacy_fir not in codegen:
    raise AssertionError("explicit/legacy Forge FIR fallback unexpectedly disappeared")
checks += 1
prepare = main.index("project_prepare_default_native_build(")
emit = main.index("emit_backend_module(")
if prepare >= emit:
    raise AssertionError("native project output path must be synthesized before backend emission")
checks += 1


# Windows fallback must stay in the MSVC ABI world.  The bridge used to pass
# GNU -lws2_32/-lbcrypt/-lcrypt32 options to clang-cl and unconditionally add
# Strawberry OpenSSL archives, which could pull an incompatible MinGW CRT.
bridge = (ROOT / "src/bootstrap/compiler/backend/forge/forge_bridge.cpp").read_text(encoding="utf-8")
if 'retry << shell_quote(fallback_path) << " /nologo /MD "' not in bridge:
    raise SystemExit("missing MSVC-style clang-cl fallback command")
if '<< " ws2_32.lib bcrypt.lib crypt32.lib /Fe:"' not in bridge:
    raise SystemExit("missing MSVC Windows system libraries in bridge fallback")

if 'append_runtime_link_dependencies(retry);' not in bridge:
    raise SystemExit("Windows fallback must preserve raz_runtime transitive dependencies")
if 'external_linker_path()' not in bridge:
    raise SystemExit("Windows fallback linker must use automatic tool discovery")
print(f"project native output contracts: {checks}/{checks} passed")
