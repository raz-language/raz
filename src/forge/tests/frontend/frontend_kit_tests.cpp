// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/frontend/frontend.hpp"
#include "forge/ir/context.hpp"
#include "forge/ir/printer.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    forge::frontend::SourceManager sources;
    const auto source = sources.add("sample.mini", "fn main() {\n  return missing;\n}\n");
    require(sources.position(source, 14).line == 2, "source line mapping");
    require(sources.line_text(source, 2) == "  return missing;", "source line text");

    forge::frontend::DiagnosticEngine diagnostics(sources);
    auto& diagnostic = diagnostics.report(forge::DiagnosticSeverity::error, "F2001", {source, 21, 28},
                                          "unknown symbol 'missing'");
    diagnostics.note(diagnostic, "declare the value before it is used");
    diagnostics.fix(diagnostic, {source, 21, 28}, "value");
    const auto rendered = diagnostics.render();
    require(rendered.find("sample.mini:2:") != std::string::npos, "diagnostic location");
    require(rendered.find("error[F2001]") != std::string::npos, "diagnostic code");
    require(rendered.find("replace `missing` with `value`") != std::string::npos, "diagnostic fix-it");

    forge::frontend::SemanticContext semantics(sources, diagnostics);
    require(semantics.declare_function("main", {forge::ir::i64_type(), {}, {source, 0, 2}}),
            "declare function");
    require(!semantics.declare_function("main", {forge::ir::i64_type(), {}, {source, 0, 2}}),
            "reject duplicate function");
    require(semantics.find_function("main") != nullptr, "find function");

    forge::frontend::Symbol symbol{forge::frontend::SymbolKind::variable, forge::ir::i64_type(), "%value", {source, 0, 1}};
    require(semantics.symbols().declare("value", symbol), "declare symbol");
    {
        forge::frontend::ScopeGuard scope(semantics.symbols());
        require(semantics.symbols().declare("value", {forge::frontend::SymbolKind::variable,
                                                       forge::ir::i32_type(), "%inner", {source, 0, 1}}),
                "shadow symbol");
        require(semantics.symbols().lookup("value")->ir_value == "%inner", "inner lookup");
    }

    require(semantics.symbols().lookup("value")->ir_value == "%value", "outer lookup");

    forge::ir::Context context;
    auto& module = context.create_module("frontend-kit");
    forge::ir::IRBuilder builder(context, module);
    const auto function = builder.create_function_handle("main", forge::ir::i64_type());
    const auto entry = builder.create_block_handle(function, "entry");
    builder.position_at_end(entry);
    const auto one = builder.create_constant(forge::ir::i1_type(), "true");
    forge::frontend::ControlFlowBuilder control(builder, function);
    control.create_if(one,
                      [&] { builder.create_return(builder.create_constant(forge::ir::i64_type(), "1")); },
                      [&] { builder.create_return(builder.create_constant(forge::ir::i64_type(), "0")); });
    require(builder.verify().empty(), "control-flow builder emits valid IR");
    const auto text = forge::ir::print_module(module);
    require(text.find("  if.then.0:") != std::string::npos, "then block emitted");
    require(text.find("  if.else.1:") != std::string::npos, "else block emitted");

    std::cout << "frontend integration tests passed\n";
    return 0;
}
