// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cassert>
#include <string>
#include "forge/diagnostics/format.hpp"
#include "forge/ir/parser.hpp"

int main() {
    const std::string source = "module @demo {\n  func @broken() -> i64 {\n    entry:\n      %x = const i64 @\n  }\n}\n";
    auto parsed = forge::ir::parse_module(source);
    if (parsed.ok() || parsed.diagnostics.empty()) return 1;
    const auto rendered = forge::diagnostics::render(parsed.diagnostics.front(), {"broken.fir", source});
    assert(rendered.find("broken.fir:4:") != std::string::npos);
    assert(rendered.find("error:") != std::string::npos);
    assert(rendered.find("%x = const i64 @") != std::string::npos);
    assert(rendered.find('^') != std::string::npos);

    forge::Diagnostic generic{forge::DiagnosticSeverity::warning, "generic warning", {}};
    const auto fallback = forge::diagnostics::render(generic);
    assert(fallback == "warning: generic warning\n");
}
