// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/driver/compiler.hpp"

#include "compiler/backend/forge/forge_lowering.hpp"
#include "compiler/diagnostics/diagnostic_engine.hpp"
#include "compiler/lexer/lexer.hpp"
#include "compiler/lexer/token_kind.hpp"
#include "compiler/lowering/hir_to_mir/hir_to_mir.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/semantic/semantic_analyzer.hpp"
#include "compiler/source/source_manager.hpp"
#include "compiler/syntax/namespace_lowering.hpp"
#include "common/terminal.hpp"

#include <forge/ir/context.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>

namespace raz::compiler {
namespace {

void render_driver_error(std::string_view message) {
  const bool color = terminal::color_enabled(std::cerr);
  if (color) std::cerr << terminal::bold << terminal::red;
  std::cerr << "error";
  if (color) std::cerr << terminal::reset;
  std::cerr << ": " << message << '\n';
}

void render_failure_summary(const Session& session, std::size_t errors) {
  if (session.options().diagnostic_format == DiagnosticFormat::json) return;
  const bool color = terminal::color_enabled(std::cerr);
  if (color) std::cerr << terminal::bold << terminal::red;
  std::cerr << "error";
  if (color) std::cerr << terminal::reset;
  const auto& display = session.options().diagnostic_display_path.empty()
      ? session.options().input : session.options().diagnostic_display_path;
  std::cerr << ": could not compile '" << display.string() << "' due to "
            << errors << " previous error" << (errors == 1 ? "" : "s") << '\n';
}

void render_diagnostics(const Session& session, const DiagnosticEngine& diagnostics,
                        const SourceManager& sources) {
  if (!session.options().diagnostic_output.empty()) {
    std::filesystem::create_directories(session.options().diagnostic_output.parent_path());
    std::ofstream report(session.options().diagnostic_output, std::ios::binary | std::ios::trunc);
    if (report) diagnostics.render(report, sources, session.options().diagnostic_format);
    return;
  }
  std::ostream& stream = session.options().diagnostic_format == DiagnosticFormat::json ? std::cout : std::cerr;
  diagnostics.render(stream, sources, session.options().diagnostic_format);
}

std::string declaration_name(const SyntaxNode& node) {
  if (node.kind == SyntaxKind::function_declaration || node.kind == SyntaxKind::struct_declaration ||
      node.kind == SyntaxKind::enum_declaration || node.kind == SyntaxKind::trait_declaration) {
    auto end = node.label.find_first_of("<:= ");
    return node.label.substr(0, end);
  }
  if (node.kind == SyntaxKind::parameter || node.kind == SyntaxKind::field_declaration ||
      node.kind == SyntaxKind::variable_declaration || node.kind == SyntaxKind::const_declaration) {
    const auto split = node.label.rfind(' ');
    return split == std::string::npos ? node.label : node.label.substr(split + 1);
  }
  if (node.kind == SyntaxKind::enum_variant || node.kind == SyntaxKind::associated_type_declaration ||
      node.kind == SyntaxKind::associated_const_declaration) {
    const auto end = node.label.find_first_of("(:=");
    return node.label.substr(0, end);
  }
  if (node.kind == SyntaxKind::namespace_declaration) return node.label;
  return {};
}

std::string declaration_type(const SyntaxNode& node) {
  if (node.kind == SyntaxKind::parameter || node.kind == SyntaxKind::field_declaration ||
      node.kind == SyntaxKind::variable_declaration || node.kind == SyntaxKind::const_declaration) {
    const auto split = node.label.rfind(' ');
    return split == std::string::npos ? std::string{} : node.label.substr(0, split);
  }
  if (node.kind == SyntaxKind::function_declaration) {
    const auto arrow = node.label.find(" -> ");
    return arrow == std::string::npos ? "void" : node.label.substr(arrow + 4);
  }
  return {};
}

SemanticSymbolKind semantic_kind(SyntaxKind kind, bool member) {
  switch (kind) {
    case SyntaxKind::namespace_declaration: return SemanticSymbolKind::namespace_;
    case SyntaxKind::struct_declaration: return SemanticSymbolKind::type;
    case SyntaxKind::enum_declaration: return SemanticSymbolKind::enum_;
    case SyntaxKind::trait_declaration: return SemanticSymbolKind::trait;
    case SyntaxKind::function_declaration: return member ? SemanticSymbolKind::method : SemanticSymbolKind::function;
    case SyntaxKind::field_declaration: return SemanticSymbolKind::field;
    case SyntaxKind::parameter: return SemanticSymbolKind::parameter;
    case SyntaxKind::variable_declaration: return SemanticSymbolKind::variable;
    case SyntaxKind::const_declaration:
    case SyntaxKind::associated_const_declaration: return SemanticSymbolKind::constant;
    case SyntaxKind::enum_variant: return SemanticSymbolKind::enum_variant;
    default: return SemanticSymbolKind::variable;
  }
}

SourceRange identifier_token_range(const SourceManager& sources, const std::vector<Token>& tokens,
                                   SourceRange within, std::string_view name, bool last = false) {
  SourceRange result{};
  for (const auto& token : tokens) {
    if (token.kind != TokenKind::identifier || !token.range.valid()) continue;
    if (token.range.begin.file != within.begin.file || token.range.begin.offset < within.begin.offset ||
        token.range.end.offset > within.end.offset) continue;
    if (sources.slice(token.range) != name) continue;
    result = token.range;
    if (!last) return result;
  }
  return result;
}

bool range_contains(SourceRange outer, SourceRange inner) {
  return outer.valid() && inner.valid() && outer.begin.file == inner.begin.file &&
         outer.begin.offset <= inner.begin.offset && inner.end.offset <= outer.end.offset;
}

std::string symbol_detail(const SyntaxNode& node, std::string_view name, std::string_view type_name) {
  switch (node.kind) {
    case SyntaxKind::function_declaration: return "fn " + node.label;
    case SyntaxKind::struct_declaration: return "struct " + node.label;
    case SyntaxKind::enum_declaration: return "enum " + node.label;
    case SyntaxKind::trait_declaration: return "trait " + node.label;
    case SyntaxKind::parameter: return std::string(type_name) + " " + std::string(name);
    case SyntaxKind::field_declaration: return std::string(type_name) + " " + std::string(name);
    case SyntaxKind::variable_declaration: return std::string(type_name) + " " + std::string(name);
    case SyntaxKind::const_declaration: return "const " + std::string(type_name) + " " + std::string(name);
    case SyntaxKind::enum_variant: return std::string(name);
    case SyntaxKind::namespace_declaration: return "namespace " + node.label;
    default: return std::string(name);
  }
}

void collect_semantic_declarations(const SyntaxNode& node, const SourceManager& sources,
                                   const std::vector<Token>& tokens, FrontendAnalysis& analysis,
                                   SourceRange lexical_scope, std::string container = {}, bool member = false) {
  const bool declaration = node.kind == SyntaxKind::namespace_declaration ||
      node.kind == SyntaxKind::struct_declaration || node.kind == SyntaxKind::enum_declaration ||
      node.kind == SyntaxKind::trait_declaration || node.kind == SyntaxKind::function_declaration ||
      node.kind == SyntaxKind::parameter || node.kind == SyntaxKind::field_declaration ||
      node.kind == SyntaxKind::variable_declaration || node.kind == SyntaxKind::const_declaration ||
      node.kind == SyntaxKind::associated_const_declaration || node.kind == SyntaxKind::enum_variant;
  std::string next_container = container;
  SourceRange next_scope = lexical_scope.valid() ? lexical_scope : node.range;
  bool children_are_members = member;
  if (declaration) {
    const auto name = declaration_name(node);
    if (!name.empty()) {
      const auto exact = identifier_token_range(sources, tokens, node.range, name, node.kind == SyntaxKind::field_declaration || node.kind == SyntaxKind::parameter || node.kind == SyntaxKind::variable_declaration || node.kind == SyntaxKind::const_declaration);
      SemanticSymbol symbol;
      symbol.name = name;
      symbol.kind = semantic_kind(node.kind, member);
      symbol.type_name = declaration_type(node);
      symbol.detail = symbol_detail(node, name, symbol.type_name);
      symbol.container = container;
      symbol.declaration = exact.valid() ? exact : node.range;
      symbol.scope = lexical_scope.valid() ? lexical_scope : node.range;
      const auto index = analysis.symbols.size();
      analysis.symbols.push_back(std::move(symbol));
      analysis.occurrences.push_back({name, analysis.symbols.back().declaration, index, true});
      if (node.kind == SyntaxKind::struct_declaration || node.kind == SyntaxKind::enum_declaration ||
          node.kind == SyntaxKind::trait_declaration) {
        next_container = name;
        next_scope = node.range;
        children_are_members = true;
      } else if (node.kind == SyntaxKind::function_declaration) {
        next_container = name;
        next_scope = node.range;
        children_are_members = false;
      }
    }
  }
  for (const auto& child : node.children) {
    collect_semantic_declarations(child, sources, tokens, analysis, next_scope, next_container, children_are_members);
  }
}

std::optional<std::size_t> resolve_semantic_symbol(const FrontendAnalysis& analysis, std::string_view name,
                                                   SourceRange use, std::string_view container = {}) {
  std::optional<std::size_t> best;
  std::uint64_t best_scope = std::numeric_limits<std::uint64_t>::max();
  for (std::size_t index = 0; index < analysis.symbols.size(); ++index) {
    const auto& symbol = analysis.symbols[index];
    if (symbol.name != name) continue;
    if (!container.empty() && symbol.container != container) continue;
    if (!range_contains(symbol.scope, use)) continue;
    if ((symbol.kind == SemanticSymbolKind::variable || symbol.kind == SemanticSymbolKind::parameter) &&
        symbol.declaration.begin.offset > use.begin.offset) continue;
    const auto scope_size = static_cast<std::uint64_t>(symbol.scope.size());
    if (!best.has_value() || scope_size < best_scope ||
        (scope_size == best_scope && symbol.declaration.begin.offset <= use.begin.offset &&
         analysis.symbols[*best].declaration.begin.offset < symbol.declaration.begin.offset)) {
      best = index;
      best_scope = scope_size;
    }
  }
  if (!best.has_value() && !container.empty()) return resolve_semantic_symbol(analysis, name, use);
  return best;
}

void collect_semantic_occurrences(const SyntaxNode& node, const SourceManager& sources,
                                  const std::vector<Token>& tokens, FrontendAnalysis& analysis) {
  if (node.kind == SyntaxKind::name_expression) {
    const auto range = identifier_token_range(sources, tokens, node.range, node.label, true);
    if (range.valid()) analysis.occurrences.push_back({node.label, range, resolve_semantic_symbol(analysis, node.label, range), false});
  } else if (node.kind == SyntaxKind::member_expression) {
    const auto range = identifier_token_range(sources, tokens, node.range, node.label, true);
    std::string container;
    if (node.modifier == "scoped" && !node.children.empty() && node.children.front().kind == SyntaxKind::name_expression)
      container = node.children.front().label;
    if (range.valid()) analysis.occurrences.push_back({node.label, range, resolve_semantic_symbol(analysis, node.label, range, container), false});
  }
  for (const auto& child : node.children) collect_semantic_occurrences(child, sources, tokens, analysis);
}

void build_semantic_index(const SyntaxTree& syntax, FrontendAnalysis& analysis) {
  collect_semantic_declarations(syntax.root(), analysis.sources, analysis.tokens, analysis, syntax.root().range);
  collect_semantic_occurrences(syntax.root(), analysis.sources, analysis.tokens, analysis);
  std::sort(analysis.occurrences.begin(), analysis.occurrences.end(), [](const auto& left, const auto& right) {
    if (left.range.begin.file != right.range.begin.file) return left.range.begin.file < right.range.begin.file;
    if (left.range.begin.offset != right.range.begin.offset) return left.range.begin.offset < right.range.begin.offset;
    return left.declaration && !right.declaration;
  });
}

SemanticSymbol* find_index_symbol(FrontendAnalysis& analysis, std::string_view name,
                                  SemanticSymbolKind kind, SourceRange within = {}) {
  SemanticSymbol* best = nullptr;
  for (auto& symbol : analysis.symbols) {
    if (symbol.name != name || symbol.kind != kind) continue;
    if (within.valid() && !range_contains(within, symbol.declaration)) continue;
    if (best == nullptr || symbol.declaration.begin.offset < best->declaration.begin.offset) best = &symbol;
  }
  return best;
}

void enrich_semantic_index(const HirModule& hir, FrontendAnalysis& analysis) {
  for (const auto& type : hir.types) {
    if (auto* symbol = find_index_symbol(analysis, type.name, SemanticSymbolKind::type)) {
      symbol->detail = "struct " + type.name;
      if (!type.generic_parameters.empty()) {
        symbol->detail += "<";
        for (std::size_t i = 0; i < type.generic_parameters.size(); ++i) {
          if (i != 0) symbol->detail += ", ";
          symbol->detail += type.generic_parameters[i];
        }
        symbol->detail += ">";
      }
    }
    for (const auto& field : type.fields) {
      if (auto* symbol = find_index_symbol(analysis, field.name, SemanticSymbolKind::field, type.range)) {
        symbol->type_name = field.type_name; symbol->detail = field.type_name + " " + field.name;
      }
    }
  }
  for (const auto& enumeration : hir.enums) {
    if (auto* symbol = find_index_symbol(analysis, enumeration.name, SemanticSymbolKind::enum_))
      symbol->detail = "enum " + enumeration.name;
  }
  for (const auto& constant : hir.constants) {
    if (auto* symbol = find_index_symbol(analysis, constant.name, SemanticSymbolKind::constant)) {
      symbol->type_name = constant.type_name; symbol->detail = "const " + constant.type_name + " " + constant.name;
    }
  }
  for (const auto& function : hir.functions) {
    auto* function_symbol = find_index_symbol(analysis, function.name,
        function.name.find("::") == std::string::npos ? SemanticSymbolKind::function : SemanticSymbolKind::method);
    if (function_symbol == nullptr) function_symbol = find_index_symbol(analysis, function.name, SemanticSymbolKind::function);
    std::string detail = "fn " + function.name + "(";
    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
      if (i != 0) detail += ", ";
      detail += function.parameters[i].type_name + " " + function.parameters[i].name;
    }
    detail += ") -> " + function.return_type;
    if (function_symbol != nullptr) { function_symbol->type_name = function.return_type; function_symbol->detail = detail; }
    for (const auto& parameter : function.parameters) {
      if (auto* symbol = find_index_symbol(analysis, parameter.name, SemanticSymbolKind::parameter, function.range)) {
        symbol->type_name = parameter.type_name; symbol->detail = parameter.type_name + " " + parameter.name;
      }
    }
    for (const auto& local : function.locals) {
      if (auto* symbol = find_index_symbol(analysis, local.name, SemanticSymbolKind::variable, function.range)) {
        symbol->type_name = local.type_name; symbol->detail = local.type_name + " " + local.name;
      }
    }
  }
}

}  // namespace

int Compiler::run(const Session& session) const {
  SourceManager sources;
  DiagnosticEngine diagnostics;
  diagnostics.set_policy(session.options().diagnostic_policy);
  std::string load_error;
  const auto file = sources.load_file(session.options().input, load_error);
  if (!file.has_value()) {
    render_driver_error(load_error);
    return 1;
  }

  if (!session.options().diagnostic_display_path.empty()) {
    sources.set_diagnostic_mapping(*file, session.options().diagnostic_display_path,
                                   session.options().diagnostic_line_delta,
                                   session.options().diagnostic_byte_delta);
  }

  if (session.options().input.extension() != ".rz") {
    render_driver_error("Raz source files must use the .rz extension");
    return 1;
  }

  Lexer lexer(sources, diagnostics, *file);
  auto tokens = lexer.lex_all();

  if (session.options().emit_tokens) {
    for (const auto& token : tokens) {
      const auto location = sources.line_column(token.range.begin);
      std::cout << location.line << ':' << location.column << ' '
                << token_kind_name(token.kind);
      const auto spelling = sources.slice(token.range);
      if (!spelling.empty()) {
        std::cout << " `" << spelling << '`';
      }
      std::cout << '\n';
    }
    render_diagnostics(session, diagnostics, sources);
    if (diagnostics.has_errors()) {
      render_failure_summary(session, diagnostics.error_count());
      return 1;
    }
    return 0;
  }

  if (!diagnostics.has_errors()) {
    Parser parser(sources, diagnostics, tokens);
    const auto syntax = parser.parse();
    if (session.options().emit_ast) {
      syntax.dump(std::cout, sources);
      render_diagnostics(session, diagnostics, sources);
      if (diagnostics.has_errors()) {
        render_failure_summary(session, diagnostics.error_count());
        return 1;
      }
      return 0;
    }
    if (!diagnostics.has_errors()) {
      const auto lowered_syntax = lower_namespaces(syntax, diagnostics);
      SemanticAnalyzer semantic(diagnostics);
      const auto hir = semantic.analyze(lowered_syntax);
      if (session.options().emit_hir) hir.dump(std::cout);
      if (!diagnostics.has_errors() && (session.options().emit_mir || session.options().emit_forge_ir)) {
        HirToMirLowering mir_lowering(diagnostics);
        const auto mir = mir_lowering.lower(hir);
        if (session.options().emit_mir) mir.dump(std::cout);
        if (!diagnostics.has_errors() && session.options().emit_forge_ir) {
          ForgeLowering forge_lowering(diagnostics);
          const auto forge_ir = forge_lowering.lower_and_print(mir);
          if (!session.options().output.empty()) {
            std::filesystem::create_directories(session.options().output.parent_path());
            std::ofstream output(session.options().output, std::ios::binary | std::ios::trunc);
            if (!output) {
              render_driver_error(std::string("unable to create output: ") + session.options().output.string());
              return 1;
            }
            output << forge_ir;
          } else {
            std::cout << forge_ir;
          }
        }
      }
    }
  }

  render_diagnostics(session, diagnostics, sources);
  if (diagnostics.has_errors()) {
    render_failure_summary(session, diagnostics.error_count());
    return 1;
  }

  // The frontend is intentionally verified before a Forge context is created.
  // Later passes will lower verified Raz MIR through this backend boundary.
  forge::ir::Context forge_context;
  (void)forge_context;

  if (!session.options().suppress_success_output &&
      session.options().diagnostic_format != DiagnosticFormat::json &&
      !session.options().emit_tokens && !session.options().emit_ast &&
      !session.options().emit_hir && !session.options().emit_mir &&
      !session.options().emit_forge_ir) {
    const bool color = terminal::color_enabled(std::cout);
    if (color) std::cout << terminal::bold << terminal::green;
    std::cout << "Checked";
    if (color) std::cout << terminal::reset;
    std::cout << " " << session.options().input.string() << " (" << tokens.size() << " tokens)\n";
  }
  return 0;
}

FrontendAnalysis Compiler::analyze_text(std::filesystem::path path,
                                        std::string text) const {
  FrontendAnalysis analysis;
  const auto file = analysis.sources.add_virtual_file(std::move(path), std::move(text));
  Lexer lexer(analysis.sources, analysis.diagnostics, file);
  auto tokens = lexer.lex_all();
  analysis.token_count = tokens.size();
  analysis.tokens = tokens;
  if (analysis.diagnostics.has_errors()) return analysis;

  Parser parser(analysis.sources, analysis.diagnostics, std::move(tokens));
  const auto syntax = parser.parse();
  build_semantic_index(syntax, analysis);
  if (analysis.diagnostics.has_errors()) return analysis;

  const auto lowered_syntax = lower_namespaces(syntax, analysis.diagnostics);
  if (analysis.diagnostics.has_errors()) return analysis;

  SemanticAnalyzer semantic(analysis.diagnostics);
  const auto hir = semantic.analyze(lowered_syntax);
  if (analysis.diagnostics.has_errors()) return analysis;
  enrich_semantic_index(hir, analysis);

  HirToMirLowering mir_lowering(analysis.diagnostics);
  (void)mir_lowering.lower(hir);
  return analysis;
}

}  // namespace raz::compiler
