#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Keep build-time route substitution ownership-simple and crash-diagnosable."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ROUTES = ROOT / "library/web/src/routes.rz"
ROUTING_TEST = ROOT / "tests/python/check-web-routing.py"
RUNTIME_TEST = ROOT / "tests/python/check-production-runtime-regressions.py"
WEB_CODEGEN = ROOT / "compiler/src/raz_codegen_web/src/web/codegen.rz"
DRIVER_MAIN = ROOT / "compiler/src/raz_driver/src/driver/compiler_main.rz"


def main() -> int:
    failures: list[str] = []
    routes = ROUTES.read_text(encoding="utf-8")
    routing_test = ROUTING_TEST.read_text(encoding="utf-8")
    runtime_test = RUNTIME_TEST.read_text(encoding="utf-8")
    web_codegen = WEB_CODEGEN.read_text(encoding="utf-8")
    driver_main = DRIVER_MAIN.read_text(encoding="utf-8")

    required_routes = [
        "i64 value_length = raz_rt_cstr_len(value);",
        "usize value_data = raz_rt_cstr_ptr(value);",
        "return replace_range_bytes(path, start, end - start, value_data, value_length);",
    ]
    for item in required_routes:
        if item not in routes:
            failures.append(f"routes.rz missing in-place route substitution invariant: {item}")

    forbidden_routes = [
        "String next = String::with_capacity",
        "*path = move next;",
    ]
    for item in forbidden_routes:
        if item in routes:
            failures.append(f"routes.rz reintroduced temporary aggregate replacement: {item}")

    required_routing_test = [
        'print("web-routing: building static routing fixture", flush=True)',
        'print("web-routing: building reactive routing fixture", flush=True)',
        'env["RAZ_COMPILER_PHASE_TRACE"] = "1"',
        'web-routing: partial reactive artifact state:',
        '("app.wasm", assets / "app.wasm")',
        '("app.js", assets / "app.js")',
        '("app.css", assets / "app.css")',
        '("index.html", dist / "index.html")',
    ]
    for item in required_routing_test:
        if item not in routing_test:
            failures.append(f"routing runtime qualification missing crash telemetry: {item}")


    # Reactive compiler emission must remain stage-by-stage. The Windows routing
    # access violation happened after MIR persistence; explicit artifact stages
    # ensure a failed child leaves enough state to identify Wasm vs JS vs CSS vs
    # index/finalization without changing normal successful output.
    ordered_codegen = [
        "success = emit_web_wasm_module_imports(",
        "success = web_write_loader(",
        "success = web_write_css(",
        "success = web_write_index(",
    ]
    last = -1
    for item in ordered_codegen:
        current = web_codegen.find(item)
        if current < 0 or current <= last:
            failures.append(f"reactive web emission is not explicitly ordered: {item}")
        last = current
    if "&&\n        emit_web_wasm_module_imports(" in web_codegen:
        failures.append("reactive web emission reintroduced monolithic short-circuit artifact chain")

    required_driver_trace = [
        'compiler_phase_trace("web-runtime-analysis"',
        'compiler_phase_trace("web-application-start"',
        'compiler_phase_trace("web-application-emit"',
    ]
    for item in required_driver_trace:
        if item not in driver_main:
            failures.append(f"reactive driver missing phase checkpoint: {item}")

    required_runtime = [
        'web_route_binding = r\'\'\'',
        'run_case(raz, work_root, "web_route_binding", ["web"], web_route_binding)',
        'web::route_bind(&mut blog, "year", "2026")',
        'web::route_bind_splat(&mut docs, "path", "guides/install")',
    ]
    for item in required_runtime:
        if item not in runtime_test:
            failures.append(f"native runtime regressions missing route ownership case: {item}")

    if failures:
        print("web-routing-ownership: FAIL")
        for failure in failures:
            print("  " + failure)
        return 1
    print("web-routing-ownership: PASS (in-place route substitution + staged reactive emission + native ownership regression + crash telemetry)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
