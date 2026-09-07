#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / 'tools' / 'bootstrap.py').read_text(encoding='utf-8')
checks = {
    'canonical modular compiler is named explicitly': 'qualified_compiler = compiler_package_layout / "target" / "release" / "bin" / f"raz-compiler{EXE}"' in text,
    'canonical modular compiler existence is enforced': 'Canonical modular compiler is missing' in text,
    'canonical modular compiler cache is retained': 'prepare_modular_compiler_build(compiler_package_layout)' in text and 'The modular compiler is the production artifact developers keep between' in text,
    'canonical modular compiler is version-validated': 'Validate canonical modular compiler' in text,
    'web qualification uses canonical modular compiler': 'str(qualified_compiler),\n                "--work-root"' in text,
    'bootstrap generation remains separate self-host proof': 'Validate Raz self-host compiler' in text and 'str(self_host_compiler), "--version"' in text,
    'seed Wasm stub tracks import-reporting emitter': 'public fn emit_web_wasm_module_roots_imports(' in text,
    'seed driver has local import-reporting emitter': 'fn emit_web_wasm_module_roots_imports(Source& source' in text,
    'seed web package exposes runtime capability analysis': 'public fn web_runtime_capabilities(Source& source, HirModule& hir, MirModule& mir) -> i64' in text,
    'seed driver has local runtime capability analysis': 'fn web_runtime_capabilities(Source& source, HirModule& hir, MirModule& mir) -> i64' in text,
    'seed host-prune bitmap helper is preserved': 'seed_web_host' in text and 'web_browser_import_enabled' in text,
    'folded CLI keeps web diagnostics resolvable': 'web_diagnostics = driver_src / "web_diagnostics.rz"' in text and 'public import raz_driver::cli;' in text,
    'folded static host pruner gets local seed helper': 'web_host_prune = driver_src / "web_host_prune.rz"' in text,
    'Stage-0 rewrites nested driver sources before compatibility folding': 'driver_src = driver_root / "driver"' in text and '/ "driver" / "compiler_main.rz"' in text and '/ "driver" / "backend.rz"' in text,
    'frozen Stage-0 gets disposable flat driver lexer parser sources': '_flatten_stage0_seed_package_sources' in text and '("raz_driver", "driver")' in text and '("raz_lexer", "lexer")' in text and '("raz_parser", "parser")' in text and 'source.replace(destination)' in text,
    'canonical nested compiler sources are not flattened in place': 'compiler_project / "src" / package_name / "src"' in text and 'Canonical compiler packages keep ``src/lib.rz``' in text,
    'Stage-0 driver facade remains at src/lib.rz': 'driver_lib = driver_root / "lib.rz"' in text,
    'full executable authoring qualification is wired': 'check-web-authoring.py' in text,
    'full executable forms qualification is wired': 'check-web-forms.py' in text,
    'full executable routing qualification is wired': 'check-web-routing.py' in text,
    'full executable browser API qualification is wired': 'check-web-browser-api.py' in text,
    'full executable dev-server qualification is wired': 'check-web-dev.py' in text,
    'static component qualifications are wired': 'check-web-static-components.py' in text and 'check-web-static-component-guard.py' in text,
}
failed=[name for name,ok in checks.items() if not ok]
if failed:
    for name in failed: print(f'bootstrap-modular-web-qualification: FAIL: {name}')
    raise SystemExit(1)
print(f'bootstrap-modular-web-qualification: PASS ({len(checks)} checks)')
