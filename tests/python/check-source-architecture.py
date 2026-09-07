#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Consolidated source architecture and ownership qualification.

Keep structural invariants in one place instead of adding a Python file for every
compiler/library refactor. Behavioral tests remain in their focused test files.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def check_package_source_cleanliness() -> None:
    """Keep canonical compiler/stdlib package roots free of generated build state."""
    problems: list[str] = []
    for root in (ROOT / "compiler" / "src", ROOT / "library"):
        if not root.is_dir():
            continue
        for name in ("target", ".raz", "__pycache__"):
            for path in root.rglob(name):
                if path.is_dir():
                    problems.append(str(path.relative_to(ROOT)))
    if problems:
        details = "\n".join(f"  generated package-local state: {item}" for item in sorted(set(problems)))
        raise SystemExit(f"package-source-cleanliness: FAIL\n{details}")
    print("package-source-cleanliness: PASS")


def check_runtime_cxx20() -> None:
    """Reject runtime source patterns deprecated or invalid under C++20."""
    runtime = ROOT / "src" / "runtime"
    problems: list[str] = []
    for path in runtime.rglob("*"):
        if not path.is_file() or path.suffix not in {".cpp", ".hpp", ".h"}:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if "std::filesystem::u8path" in text:
            problems.append(f"deprecated C++20 filesystem path conversion: {path.relative_to(ROOT)}")
    if problems:
        raise SystemExit("runtime-cxx20: FAIL\n" + "\n".join(f"  {item}" for item in problems))
    print("runtime-cxx20: PASS")


def check_codegen_backend_isolation() -> None:
    """Enforce sibling backend dependency direction and neutral shared support ownership."""
    from pathlib import Path
    
    ROOT = Path(__file__).resolve().parents[2]
    SRC = ROOT / "compiler" / "src"
    failed: list[str] = []
    
    backends = {
        "raz_codegen_forge": "forge",
        "raz_codegen_llvm": "llvm",
        "raz_codegen_wasm": "wasm",
        "raz_codegen_rxe": "rxe",
    }
    common = SRC / "raz_codegen_common"
    required_common = (
        common / "src" / "codegen_common" / "writer.rz",
        common / "src" / "codegen_common" / "abi.rz",
        common / "src" / "codegen_common" / "analysis.rz",
        common / "src" / "codegen_common" / "symbols.rz",
    )
    if not (common / "raz.toml").is_file() or any(not path.is_file() for path in required_common):
        failed.append("raz_codegen_common does not own writer + analysis + symbol support")
    else:
        combined = "\n".join(path.read_text(encoding="utf-8") for path in required_common)
        for marker in (
            "public struct CodegenWriter",
            "raz_compiler_runtime_link_args_i64",
            "async_mir_supported",
            "find_main_function",
            "writer_function_name",
            "writer_module_global_needs_identity_suffix",
        ):
            if marker not in combined:
                failed.append(f"neutral codegen support missing {marker}")
        for package, namespace in backends.items():
            if package in combined or f"{namespace}::" in combined:
                failed.append(f"raz_codegen_common imports backend implementation {package}")
    
    # Real backends are siblings. No backend implementation may import or depend on
    # another backend implementation. The web package is an application-target
    # orchestrator and may consume the Wasm backend explicitly.
    for package, namespace in backends.items():
        root = SRC / package
        manifest = (root / "raz.toml").read_text(encoding="utf-8")
        for other_package, other_namespace in backends.items():
            if package == other_package:
                continue
            if other_package in manifest:
                failed.append(f"{package} depends on sibling backend {other_package}")
            for source in (root / "src").rglob("*.rz"):
                text = source.read_text(encoding="utf-8")
                if f"import {other_namespace}::" in text:
                    failed.append(
                        f"{source.relative_to(ROOT)} imports sibling backend namespace {other_namespace}::"
                    )
                    break
        if package != "raz_codegen_forge" and 'common = "../raz_codegen_common"' not in manifest:
            failed.append(f"{package} is missing raz_codegen_common")
    
    web_root = SRC / "raz_codegen_web"
    web_manifest = (web_root / "raz.toml").read_text(encoding="utf-8")
    if 'common = "../raz_codegen_common"' not in web_manifest:
        failed.append("raz_codegen_web is missing raz_codegen_common")
    if 'wasm = "../raz_codegen_wasm"' not in web_manifest:
        failed.append("web must retain its explicit Wasm application-target dependency")
    if "raz_codegen_forge" in web_manifest or "raz_codegen_llvm" in web_manifest or "raz_codegen_rxe" in web_manifest:
        failed.append("web must not depend on native/RXE backend implementations")
    
    if failed:
        print("codegen-backend-isolation: FAIL")
        for item in failed:
            print("  " + item)
        raise SystemExit(1)
    print("codegen-backend-isolation: PASS (Forge/LLVM/WASM/RXE siblings; shared writer/ABI/analysis/symbols in common)")

def check_wasm_host_layout() -> None:
    from pathlib import Path
    
    root = Path(__file__).resolve().parents[2]
    wasm = root / "compiler/src/raz_codegen_wasm/src/wasm"
    wasi = (wasm / "wasi.rz").read_text(encoding="utf-8")
    filesystem = (wasm / "wasi_filesystem.rz").read_text(encoding="utf-8")
    support = (wasm / "wasi_support.rz").read_text(encoding="utf-8")
    browser = (wasm / "browser_host.rz").read_text(encoding="utf-8")
    host = (wasm / "host_support.rz").read_text(encoding="utf-8")
    lib = (root / "compiler/src/raz_codegen_wasm/src/lib.rz").read_text(encoding="utf-8")
    
    assert len(wasi.splitlines()) < 2700, "wasi.rz grew back beyond the filesystem boundary"
    assert '"raz_web"' not in wasi, "browser import ABI leaked back into wasi.rz"
    for needle in [
        "wasm_browser_logical_import",
        "wasm_browser_runtime_function_supported",
        "wasm_browser_emit_imports",
        "wasm_browser_emit_runtime_body",
        "wasm_browser_emit_import_wrapper",
    ]:
        assert needle in browser, f"browser host missing {needle}"
    for needle in ["raz_web_dom_set_text", "raz_web_storage_set", "raz_web_history_push", "raz_web_timer_set_timeout"]:
        assert needle in browser and needle not in wasi, f"browser ABI ownership violation: {needle}"
    for needle in ["wasm_host_name_is_literal", "wasm_host_name_is"]:
        assert needle in host, f"shared host support missing {needle}"
    for needle in [
        "wasm_wasi_emit_find_preopen",
        "wasm_wasi_emit_find_named_preopen",
        "wasm_wasi_emit_find_preopen_for_path",
        "wasm_wasi_emit_resolve_preopen",
        "wasm_wasi_emit_file_open_body",
        "wasm_wasi_emit_file_seek_body",
        "wasm_wasi_emit_path_filestat_body",
        "wasm_wasi_emit_path_exists_body",
    ]:
        assert needle in filesystem and f"fn {needle}" not in wasi, f"WASI filesystem ownership violation: {needle}"
    for needle in ["wasm_wasi_last_error_global", "wasm_wasi_capture_errno", "wasm_wasi_set_last_error", "wasm_wasi_emit_i32_load"]:
        assert needle in support and f"fn {needle}" not in wasi, f"WASI support ownership violation: {needle}"
    for needle in [
        "public import raz_codegen_wasm::browser_host;",
        "public import raz_codegen_wasm::host_support;",
        "public import raz_codegen_wasm::wasi_filesystem;",
        "public import raz_codegen_wasm::wasi_support;",
    ]:
        assert needle in lib, f"WASM package missing export: {needle}"
    assert "public import raz_codegen_wasm::browser_host;" in wasi
    assert "public import raz_codegen_wasm::wasi_filesystem;" in wasi
    assert "public import raz_codegen_wasm::wasi_support;" in wasi
    print(f"wasm-host-layout: PASS (wasi={len(wasi.splitlines())} lines, filesystem={len(filesystem.splitlines())}, wasi-support={len(support.splitlines())}, browser={len(browser.splitlines())}, host-support={len(host.splitlines())})")

def check_web_library_layout() -> None:
    from pathlib import Path
    
    ROOT = Path(__file__).resolve().parents[2]
    web = ROOT / "library" / "web" / "src"
    lib = (web / "lib.rz").read_text(encoding="utf-8")
    page = (web / "page.rz").read_text(encoding="utf-8")
    page_modules = {
        name: (web / name).read_text(encoding="utf-8")
        for name in (
            "page_head.rz",
            "page_body.rz",
            "page_forms.rz",
            "page_interactivity.rz",
            "page_output.rz",
        )
    }
    client = (web / "client_host.rz").read_text(encoding="utf-8")
    css = (web / "page_css.rz").read_text(encoding="utf-8")
    routes = (web / "routes.rz").read_text(encoding="utf-8")
    html = (web / "html_support.rz").read_text(encoding="utf-8")
    web_root = ROOT / "library" / "web"
    source_order = (web_root / "source-order.txt").read_text(encoding="utf-8").splitlines()
    ordered = [line.strip() for line in source_order if line.strip() and not line.lstrip().startswith("#")]
    missing = [entry for entry in ordered if not (web_root / entry).is_file()]
    assert not missing, f"web source-order references missing files: {missing}"
    assert (web_root / "build" / "fs.rz").is_file(), "web build filesystem module is missing"
    assert (web_root / "build" / "format.rz").is_file(), "web build formatting module is missing"
    
    assert len(lib.encode("utf-8")) < 4096, f"web lib facade grew to {len(lib.encode('utf-8'))} bytes"
    assert "public import web::page;" in lib
    assert "public import web::routes;" in lib
    for forbidden in ("public struct Page", "public impl Page", "write_client_for_path", "static_route_output", "append_css_rule"):
        assert forbidden not in lib, f"implementation leaked back into lib.rz: {forbidden}"
    assert "public struct Page" in page and "public impl Page" in page
    assert len(page.encode("utf-8")) < 8192, f"page.rz coordinator grew to {len(page.encode('utf-8'))} bytes"
    page_ownership = {
        "page_head.rz": ("public fn description(", "public fn stylesheet(", "public fn raw_css("),
        "page_body.rz": ("public fn h1(", "public fn element_attr(", "public fn image("),
        "page_forms.rz": ("public fn input(", "public fn form(", "public fn layout(", "public fn textarea("),
        "page_interactivity.rz": ("public fn lazy_module_on(", "public fn lazy_wasm_on(", "public fn on("),
        "page_output.rz": ("fn write_page_to(", "public fn write_route(", "public fn prepare_dist(", "public fn write_component_route("),
    }
    for name, tokens in page_ownership.items():
        text = page_modules[name]
        for token in tokens:
            assert token in text, f"web page ownership missing {token} in {name}"
            assert token not in page, f"web page implementation leaked back into page.rz: {token}"
        assert len(text.encode("utf-8")) < 32768, f"web page module {name} too large: {len(text.encode('utf-8'))} bytes"
    expected_page_order = [
        "src/page.rz",
        "src/page_head.rz",
        "src/page_body.rz",
        "src/page_forms.rz",
        "src/page_interactivity.rz",
        "src/page_output.rz",
    ]
    page_positions = [ordered.index(entry) if entry in ordered else -1 for entry in expected_page_order]
    assert all(pos >= 0 for pos in page_positions), f"web source-order missing split page modules: {expected_page_order}"
    assert page_positions == sorted(page_positions), "web page modules are not in dependency order"
    assert "write_client_for_path" in client
    assert "append_css_rule" in css
    assert "public fn route_path" in routes and "public fn write_redirect" in routes
    assert "public fn append_escaped_to" in html

    ui_root = web_root / "ui"
    ui_facade = (ui_root / "ui.rz").read_text(encoding="utf-8")
    ui_modules = {
        name: (ui_root / name).read_text(encoding="utf-8")
        for name in ("core.rz", "state.rz", "render.rz", "elements.rz", "components.rz", "routing.rz", "http.rz")
    }
    assert len(ui_facade.encode("utf-8")) < 1024, f"web UI facade grew to {len(ui_facade.encode('utf-8'))} bytes"
    for token in ("public struct Element", "public struct Component", "public fn http_get", "public fn route_is", "impl Element"):
        assert token not in ui_facade, f"UI implementation leaked back into ui/ui.rz: {token}"
    assert "public struct Element" in ui_modules["core.rz"]
    assert "public fn state_i64" in ui_modules["state.rz"]
    assert "fn buffer_reserve" in ui_modules["render.rz"]
    assert "impl Element" in ui_modules["elements.rz"]
    assert "impl Component" in ui_modules["components.rz"]
    assert "public fn route_is" in ui_modules["routing.rz"]
    assert "public fn http_get" in ui_modules["http.rz"]
    for name, text in ui_modules.items():
        assert len(text.encode("utf-8")) < 65536, f"web UI module {name} is still too large: {len(text.encode('utf-8'))} bytes"
    expected_ui_order = [f"ui/{name}" for name in ("core.rz", "state.rz", "render.rz", "elements.rz", "components.rz", "routing.rz", "http.rz", "ui.rz")]
    positions = [ordered.index(entry) if entry in ordered else -1 for entry in expected_ui_order]
    assert all(pos >= 0 for pos in positions), f"web source-order missing split UI modules: {expected_ui_order}"
    assert positions == sorted(positions), "web UI modules are not in dependency order"
    print(f"web-library-layout: PASS (lib={len(lib.encode('utf-8'))} bytes; page={len(page.splitlines())} lines; page split across 5 modules; ui-facade={len(ui_facade.encode('utf-8'))} bytes; UI split across 7 modules)")

def check_driver_project_layout() -> None:
    """Keep project-driver ownership split by responsibility."""
    from pathlib import Path
    
    ROOT = Path(__file__).resolve().parents[2]
    DRIVER = ROOT / "compiler/src/raz_driver/src/driver"
    project = (DRIVER / "project.rz").read_text(encoding="utf-8")
    native = (DRIVER / "project_native.rz").read_text(encoding="utf-8")
    web = (DRIVER / "project_web_manifest.rz").read_text(encoding="utf-8")
    lib = (DRIVER.parent / "lib.rz").read_text(encoding="utf-8")
    
    required_native = (
        "struct ProjectNativeBuild",
        "fn project_prepare_default_native_build(",
        "fn project_default_native_paths(",
        "fn project_package_units_cache_ready(",
        "fn project_link_package_units(",
        "fn project_emit_package_units(",
    )
    required_web = (
        "fn project_manifest_web_mode(",
        "fn project_web_title(",
        "fn project_web_output_index(",
    )
    required_project = (
        "fn append_project_manifest(",
        "fn append_project_topological_sources(",
        "fn append_project_source_order(",
        "fn project_optional_dependency_enabled(",
        "fn project_load_sources_incremental(",
    )
    
    for token in required_native:
        if token not in native:
            raise SystemExit(f"driver-project-layout: FAIL: native ownership missing {token}")
    for token in required_web:
        if token not in web:
            raise SystemExit(f"driver-project-layout: FAIL: web manifest ownership missing {token}")
    for token in required_project:
        if token not in project:
            raise SystemExit(f"driver-project-layout: FAIL: source-graph ownership missing {token}")
    
    for token in required_native + required_web:
        if token in project:
            raise SystemExit(f"driver-project-layout: FAIL: project.rz reabsorbed extracted responsibility {token}")
    
    if "public import raz_driver::project_native;" not in lib:
        raise SystemExit("driver-project-layout: FAIL: native project module not exported")
    if "public import raz_driver::project_web_manifest;" not in lib:
        raise SystemExit("driver-project-layout: FAIL: web manifest module not exported")
    
    project_lines = len(project.splitlines())
    if project_lines > 3600:
        raise SystemExit(f"driver-project-layout: FAIL: project.rz grew back to {project_lines} lines")
    
    print(
        "driver-project-layout: PASS "
        f"(project={project_lines} lines, native={len(native.splitlines())}, web-manifest={len(web.splitlines())})"
    )

def check_driver_cli_layout() -> None:
    """Keep CLI command/help mechanics separate from build/run execution policy."""
    from pathlib import Path
    
    ROOT = Path(__file__).resolve().parents[2]
    DRIVER = ROOT / "compiler/src/raz_driver/src/driver"
    cli = (DRIVER / "cli.rz").read_text(encoding="utf-8")
    help_mod = (DRIVER / "cli_help.rz").read_text(encoding="utf-8")
    support = (DRIVER / "cli_support.rz").read_text(encoding="utf-8")
    lib = (DRIVER.parent / "lib.rz").read_text(encoding="utf-8")
    
    required_help = (
        "fn cli_detect_command(",
        "fn cli_command_help_requested(",
        "fn cli_help_topic(",
        "fn cli_print_general_help(",
        "fn cli_print_command_help(",
        "fn cli_print_unknown_command_at(",
        "fn cli_print_unknown_option(",
    )
    required_support = (
        "fn cli_write_stream(",
        "fn cli_write_literal_stream(",
        "fn cli_write_arena_stream(",
        "fn cli_write_packed(",
        "fn cli_arg_equals(",
        "fn cli_arg_equals_literal(",
        "fn cli_load_arg(",
    )
    required_execution = (
        "fn cli_run_native_artifact(",
        "fn cli_print_completion_summary(",
        "fn cli_render_frontend_diagnostic(",
        "fn cli_clean_project(",
        "fn cli_init_project(",
        "fn cli_new_project(",
    )
    
    for token in required_help:
        if token not in help_mod:
            raise SystemExit(f"driver-cli-layout: FAIL: help ownership missing {token}")
    for token in required_support:
        if token not in support:
            raise SystemExit(f"driver-cli-layout: FAIL: support ownership missing {token}")
    for token in required_execution:
        if token not in cli:
            raise SystemExit(f"driver-cli-layout: FAIL: execution ownership missing {token}")
    for token in required_help + required_support:
        if token in cli:
            raise SystemExit(f"driver-cli-layout: FAIL: cli.rz reabsorbed extracted responsibility {token}")
    
    for module in ("cli_support", "cli_help"):
        if f"public import raz_driver::{module};" not in lib:
            raise SystemExit(f"driver-cli-layout: FAIL: {module} not exported")
    
    cli_lines = len(cli.splitlines())
    if cli_lines > 2900:
        raise SystemExit(f"driver-cli-layout: FAIL: cli.rz grew back to {cli_lines} lines")
    
    print(
        "driver-cli-layout: PASS "
        f"(cli={cli_lines} lines, help={len(help_mod.splitlines())}, support={len(support.splitlines())})"
    )

def check_driver_registry_layout() -> None:
    from pathlib import Path
    
    ROOT = Path(__file__).resolve().parents[2]
    SRC = ROOT / "compiler" / "src" / "raz_driver" / "src" / "driver"
    
    registry = (SRC / "registry.rz").read_text(encoding="utf-8")
    semver = (SRC / "registry_semver.rz").read_text(encoding="utf-8")
    support = (SRC / "registry_support.rz").read_text(encoding="utf-8")
    index = (SRC / "registry_index.rz").read_text(encoding="utf-8")
    state = (SRC / "registry_state.rz").read_text(encoding="utf-8")
    tracking = (SRC / "registry_tracking.rz").read_text(encoding="utf-8")
    resolver = (SRC / "registry_resolver.rz").read_text(encoding="utf-8")
    lib = (SRC.parent / "lib.rz").read_text(encoding="utf-8")
    
    for token in ("registry_parse_semver", "registry_constraint_matches", "registry_semver_compare"):
        if token not in semver:
            raise SystemExit(f"driver-registry-layout: FAIL: semver ownership missing {token}")
        if f"fn {token}" in registry:
            raise SystemExit(f"driver-registry-layout: FAIL: registry.rz reabsorbed {token}")
    for token in ("registry_token", "registry_bytes_equal"):
        if token not in support:
            raise SystemExit(f"driver-registry-layout: FAIL: support ownership missing {token}")
    for token in ("registry_latest_index_version", "registry_version_greater", "registry_contains_folded"):
        if token not in index:
            raise SystemExit(f"driver-registry-layout: FAIL: index ownership missing {token}")
    for token in ("registry_project_state_path", "registry_project_state_prepare"):
        if token not in state:
            raise SystemExit(f"driver-registry-layout: FAIL: state ownership missing {token}")
        if f"fn {token}" in registry:
            raise SystemExit(f"driver-registry-layout: FAIL: registry.rz reabsorbed {token}")
    for token in ("registry_spec_parse", "registry_tracking_spec_parse", "registry_write_tracking", "registry_sync_tracking_from_manifest"):
        if token not in tracking:
            raise SystemExit(f"driver-registry-layout: FAIL: tracking ownership missing {token}")
        if f"fn {token}" in registry:
            raise SystemExit(f"driver-registry-layout: FAIL: registry.rz reabsorbed {token}")
    for token in ("registry_resolve_mode", "registry_resolve", "registry_write_cache", "registry_fetch_package_lock"):
        if token not in resolver:
            raise SystemExit(f"driver-registry-layout: FAIL: resolver ownership missing {token}")
        if f"fn {token}(" in registry or f"public fn {token}(" in registry:
            raise SystemExit(f"driver-registry-layout: FAIL: registry.rz reabsorbed {token}")
    for module in ("registry_semver", "registry_support", "registry_index", "registry_state", "registry_tracking", "registry_resolver"):
        if f"public import raz_driver::{module};" not in lib:
            raise SystemExit(f"driver-registry-layout: FAIL: {module} not exported")
    lines = len(registry.splitlines())
    if lines >= 3300:
        raise SystemExit(f"driver-registry-layout: FAIL: registry.rz grew back to {lines} lines")
    print(f"driver-registry-layout: PASS (registry={lines} lines; semver/support/index/state/tracking/resolver split)")

def check_hir_statement_layout() -> None:
    """Keep statement primitives and match lowering out of the generic statement dispatcher."""
    from pathlib import Path
    
    ROOT = Path(__file__).resolve().parents[2]
    SEM = ROOT / "compiler/src/raz_hir/src/hir/semantic"
    statements = (SEM / "statements.rz").read_text(encoding="utf-8")
    support = (SEM / "statement_support.rz").read_text(encoding="utf-8")
    match = (SEM / "match_statements.rz").read_text(encoding="utf-8")
    lib = (ROOT / "compiler/src/raz_hir/src/lib.rz").read_text(encoding="utf-8")
    
    support_owned = (
        "fn hir_add_block(",
        "fn hir_add_statement(",
        "fn hir_capture_block(",
        "fn hir_skip_if_chain(",
    )
    match_owned = (
        "fn token_is_wildcard(",
        "fn hir_match_is_exhaustive(",
        "fn hir_match_parse_value_pattern(",
        "fn hir_match_parse_payload_pattern(",
        "fn hir_parse_match_statement(",
    )
    statement_owned = (
        "fn hir_parse_assignment(",
        "fn hir_parse_local(",
        "fn hir_parse_defer_statement(",
        "fn hir_parse_unsafe_statement(",
        "fn hir_parse_for_statement(",
        "fn hir_parse_block_statements(",
    )
    
    for token in support_owned:
        if token not in support:
            raise SystemExit(f"hir-statement-layout: FAIL: support ownership missing {token}")
        if token in statements:
            raise SystemExit(f"hir-statement-layout: FAIL: statements.rz reabsorbed support {token}")
    for token in match_owned:
        if token not in match:
            raise SystemExit(f"hir-statement-layout: FAIL: match ownership missing {token}")
        if token in statements:
            raise SystemExit(f"hir-statement-layout: FAIL: statements.rz reabsorbed match semantics {token}")
    for token in statement_owned:
        if token not in statements:
            raise SystemExit(f"hir-statement-layout: FAIL: statement dispatcher ownership missing {token}")
    
    for module in ("statement_support", "match_statements"):
        if f"public import raz_hir::semantic::{module};" not in lib:
            raise SystemExit(f"hir-statement-layout: FAIL: {module} not exported")
    
    statement_lines = len(statements.splitlines())
    if statement_lines > 2800:
        raise SystemExit(f"hir-statement-layout: FAIL: statements.rz grew back to {statement_lines} lines")
    
    print(
        "hir-statement-layout: PASS "
        f"(statements={statement_lines} lines, support={len(support.splitlines())}, match={len(match.splitlines())})"
    )

def check_hir_comptime_layout() -> None:
    """Keep the compile-time evaluator separate from HIR comptime orchestration."""
    from pathlib import Path
    
    ROOT = Path(__file__).resolve().parents[2]
    SEM = ROOT / "compiler/src/raz_hir/src/hir/semantic"
    comptime = (SEM / "comptime.rz").read_text(encoding="utf-8")
    eval_src = (SEM / "comptime_eval.rz").read_text(encoding="utf-8")
    lib = (ROOT / "compiler/src/raz_hir/src/lib.rz").read_text(encoding="utf-8")
    
    eval_owned = (
        "fn hir_apply_comptime_assignment(",
        "fn hir_eval_comptime_node(",
        "fn hir_execute_comptime_block(",
        "fn hir_eval_const_engine(",
        "fn hir_eval_constexpr_node(",
        "fn hir_fold_constants(",
    )
    comptime_owned = (
        "fn hir_parse_comptime_block(",
        "fn hir_parse_pending_attributes(",
        "fn hir_predeclare_aggregate_graph(",
        "fn hir_materialize_generic_drop_impl(",
        "fn hir_prepare_closure_blocks(",
        "public fn build_hir_with_check_hints(",
        "public fn build_hir(",
    )
    
    for token in eval_owned:
        if token not in eval_src:
            raise SystemExit(f"hir-comptime-layout: FAIL: evaluator ownership missing {token}")
        if token in comptime:
            raise SystemExit(f"hir-comptime-layout: FAIL: comptime.rz reabsorbed evaluator {token}")
    for token in comptime_owned:
        if token not in comptime:
            raise SystemExit(f"hir-comptime-layout: FAIL: orchestration ownership missing {token}")
    
    if "public import raz_hir::semantic::comptime_eval;" not in comptime:
        raise SystemExit("hir-comptime-layout: FAIL: comptime.rz does not import evaluator")
    if "public import raz_hir::semantic::comptime_eval;" not in lib:
        raise SystemExit("hir-comptime-layout: FAIL: evaluator not exported by raz_hir")
    
    comptime_lines = len(comptime.splitlines())
    if comptime_lines > 2200:
        raise SystemExit(f"hir-comptime-layout: FAIL: comptime.rz grew back to {comptime_lines} lines")
    
    print(
        "hir-comptime-layout: PASS "
        f"(comptime={comptime_lines} lines, evaluator={len(eval_src.splitlines())})"
    )

def check_hir_expression_layout() -> None:
    """Keep expression support, call lowering, and precedence logic out of core primary dispatch."""
    from pathlib import Path
    
    ROOT = Path(__file__).resolve().parents[2]
    SEM = ROOT / "compiler/src/raz_hir/src/hir/semantic"
    expr = (SEM / "expressions.rz").read_text(encoding="utf-8")
    support = (SEM / "expression_support.rz").read_text(encoding="utf-8")
    operators = (SEM / "expression_operators.rz").read_text(encoding="utf-8")
    calls = (SEM / "expression_calls.rz").read_text(encoding="utf-8")
    postfix = (SEM / "expression_postfix.rz").read_text(encoding="utf-8")
    lib = (ROOT / "compiler/src/raz_hir/src/lib.rz").read_text(encoding="utf-8")
    
    support_owned = (
        "fn hir_primitive_integer_constant(",
        "fn hir_parse_temporary_method_chain(",
    )
    operator_owned = (
        "fn hir_parse_cast(",
        "fn hir_parse_product(",
        "fn hir_parse_sum(",
        "fn hir_parse_shift(",
        "fn hir_parse_relational(",
        "fn hir_parse_equality(",
        "fn hir_parse_bit_and(",
        "fn hir_parse_bit_xor(",
        "fn hir_parse_bit_or(",
        "fn hir_parse_logical_and(",
    )
    call_owned = (
        "fn hir_validate_and_emit_call(",
    )
    postfix_owned = (
        "fn hir_parse_member_postfix(",
        "fn hir_apply_propagation_postfix(",
        "fn hir_apply_index_postfix(",
    )
    core_owned = (
        "fn hir_parse_named_struct_literal(",
        "fn hir_parse_primary(",
        "fn hir_parse_expression(",
    )
    
    for token in support_owned:
        if token not in support:
            raise SystemExit(f"hir-expression-layout: FAIL: expression support missing {token}")
        if token in expr or token in operators:
            raise SystemExit(f"hir-expression-layout: FAIL: support helper duplicated outside expression_support: {token}")
    for token in operator_owned:
        if token not in operators:
            raise SystemExit(f"hir-expression-layout: FAIL: operator lowering missing {token}")
        if token in expr:
            raise SystemExit(f"hir-expression-layout: FAIL: expressions.rz reabsorbed operator helper {token}")
    for token in call_owned:
        if token not in calls:
            raise SystemExit(f"hir-expression-layout: FAIL: call lowering missing {token}")
        if token in expr or token in operators or token in support:
            raise SystemExit(f"hir-expression-layout: FAIL: call helper duplicated outside expression_calls: {token}")
    for token in postfix_owned:
        if token not in postfix:
            raise SystemExit(f"hir-expression-layout: FAIL: postfix lowering missing {token}")
        if token in expr or token in operators or token in support or token in calls:
            raise SystemExit(f"hir-expression-layout: FAIL: postfix helper duplicated outside expression_postfix: {token}")
    for token in core_owned:
        if token not in expr:
            raise SystemExit(f"hir-expression-layout: FAIL: core expression dispatch missing {token}")
    
    for module in ("expression_support", "expression_operators", "expression_calls", "expression_postfix"):
        imp = f"public import raz_hir::semantic::{module};"
        if imp not in expr:
            raise SystemExit(f"hir-expression-layout: FAIL: expressions.rz does not import {module}")
        if imp not in lib:
            raise SystemExit(f"hir-expression-layout: FAIL: {module} not exported by raz_hir")
    
    lines = len(expr.splitlines())
    if lines > 1900:
        raise SystemExit(f"hir-expression-layout: FAIL: expressions.rz grew back to {lines} lines")
    
    print(
        "hir-expression-layout: PASS "
        f"(expressions={lines} lines, support={len(support.splitlines())}, operators={len(operators.splitlines())}, calls={len(calls.splitlines())}, postfix={len(postfix.splitlines())})"
    )

def check_hir_ownership_layout() -> None:
    """Keep ownership path and flow analysis separate from borrow/call orchestration."""
    from pathlib import Path
    
    ROOT = Path(__file__).resolve().parents[2]
    SEM = ROOT / "compiler/src/raz_hir/src/hir/semantic"
    ownership = (SEM / "ownership.rz").read_text(encoding="utf-8")
    paths = (SEM / "ownership_paths.rz").read_text(encoding="utf-8")
    flow = (SEM / "ownership_flow.rz").read_text(encoding="utf-8")
    lib = (ROOT / "compiler/src/raz_hir/src/lib.rz").read_text(encoding="utf-8")
    
    path_owned = (
        "fn hir_local_index_from_slot(",
        "fn hir_node_path(",
        "fn hir_paths_overlap(",
        "fn hir_path_is_moved(",
        "fn hir_record_predrop(",
        "fn hir_mark_path_moved(",
    )
    flow_owned = (
        "fn hir_flow_local_state_capacity(",
        "fn hir_flow_check_node(",
        "fn hir_flow_check_block(",
        "fn hir_flow_check_root_statement(",
        "fn hir_validate_control_flow_moves(",
    )
    ownership_owned = (
        "fn hir_find_function(",
        "fn hir_validate_borrow(",
        "fn hir_parse_dyn_trait_call(",
        "fn hir_parse_inherent_call(",
        "fn hir_initialize_closure_function(",
        "fn hir_validate_reference_returns(",
    )
    
    for token in path_owned:
        if token not in paths:
            raise SystemExit(f"hir-ownership-layout: FAIL: path ownership missing {token}")
        if token in ownership or token in flow:
            raise SystemExit(f"hir-ownership-layout: FAIL: path helper duplicated outside ownership_paths: {token}")
    for token in flow_owned:
        if token not in flow:
            raise SystemExit(f"hir-ownership-layout: FAIL: flow ownership missing {token}")
        if token in ownership:
            raise SystemExit(f"hir-ownership-layout: FAIL: ownership.rz reabsorbed flow helper {token}")
    for token in ownership_owned:
        if token not in ownership:
            raise SystemExit(f"hir-ownership-layout: FAIL: borrow/call ownership missing {token}")
    
    for module in ("ownership_paths", "ownership_flow"):
        imp = f"public import raz_hir::semantic::{module};"
        if imp not in ownership:
            raise SystemExit(f"hir-ownership-layout: FAIL: ownership.rz does not import {module}")
        if imp not in lib:
            raise SystemExit(f"hir-ownership-layout: FAIL: {module} not exported by raz_hir")
    
    ownership_lines = len(ownership.splitlines())
    if ownership_lines > 2550:
        raise SystemExit(f"hir-ownership-layout: FAIL: ownership.rz grew back to {ownership_lines} lines")
    
    print(
        "hir-ownership-layout: PASS "
        f"(ownership={ownership_lines} lines, paths={len(paths.splitlines())}, flow={len(flow.splitlines())})"
    )

def main() -> None:
    checks = (
        check_package_source_cleanliness,
        check_runtime_cxx20,
        check_codegen_backend_isolation,
        check_wasm_host_layout,
        check_web_library_layout,
        check_driver_project_layout,
        check_driver_cli_layout,
        check_driver_registry_layout,
        check_hir_statement_layout,
        check_hir_comptime_layout,
        check_hir_expression_layout,
        check_hir_ownership_layout,
    )
    for check in checks:
        check()
    print("source-architecture: PASS (12 consolidated checks)")


if __name__ == "__main__":
    main()
