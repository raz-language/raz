// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/parser/parser.hpp"

#include "compiler/diagnostics/diagnostic_engine.hpp"
#include "compiler/source/source_manager.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace raz::compiler {
namespace {

std::string_view token_spelling(TokenKind kind) {
  switch (kind) {
    case TokenKind::left_paren: return "(";
    case TokenKind::right_paren: return ")";
    case TokenKind::left_brace: return "{";
    case TokenKind::right_brace: return "}";
    case TokenKind::left_bracket: return "[";
    case TokenKind::right_bracket: return "]";
    case TokenKind::comma: return ",";
    case TokenKind::dot: return ".";
    case TokenKind::semicolon: return ";";
    case TokenKind::colon: return ":";
    case TokenKind::colon_colon: return "::";
    case TokenKind::arrow: return "->";
    case TokenKind::fat_arrow: return "=>";
    case TokenKind::equal: return "=";
    case TokenKind::less: return "<";
    case TokenKind::greater: return ">";
    default: return {};
  }
}

SyntaxNode node(SyntaxKind kind, SourceLocation begin) {
  SyntaxNode result;
  result.kind = kind;
  result.range.begin = begin;
  result.range.end = begin;
  return result;
}

}  // namespace

Parser::Parser(const SourceManager& sources, DiagnosticEngine& diagnostics,
               std::vector<Token> tokens)
    : sources_(sources), diagnostics_(diagnostics), tokens_(std::move(tokens)) {}

const Token& Parser::current() const noexcept { return peek(0); }
const Token& Parser::previous() const noexcept {
  return tokens_[position_ == 0 ? 0 : position_ - 1];
}

const Token& Parser::peek(std::size_t distance) const noexcept {
  const auto index = position_ + distance;
  return tokens_[index < tokens_.size() ? index : tokens_.size() - 1];
}

bool Parser::at(TokenKind kind) const noexcept { return current().is(kind); }
bool Parser::at_any(std::initializer_list<TokenKind> kinds) const noexcept {
  for (const auto kind : kinds) if (at(kind)) return true;
  return false;
}

const Token& Parser::advance() noexcept {
  if (!at(TokenKind::end_of_file)) ++position_;
  return previous();
}

bool Parser::consume(TokenKind kind) noexcept {
  if (!at(kind)) return false;
  advance();
  return true;
}

Token Parser::expect(TokenKind kind, std::string_view message) {
  if (at(kind)) return advance();
  const SourceRange insertion{current().range.begin, current().range.begin};
  Diagnostic diagnostic;
  diagnostic.severity = DiagnosticSeverity::error;
  diagnostic.code = "D1001";
  diagnostic.message = std::string(message);
  diagnostic.labels.push_back(DiagnosticLabel{current().range, {}, true});
  const auto spelling = token_spelling(kind);
  if (!spelling.empty()) {
    diagnostic.fixes.push_back(DiagnosticFix{insertion, std::string(spelling),
                                             "insert missing token"});
  }
  diagnostics_.report(std::move(diagnostic));
  return Token{kind, insertion};
}

void Parser::report(const Token& token, std::string message) {
  diagnostics_.error("D1001", token.range, std::move(message));
}

std::string Parser::token_text(const Token& token) const {
  return std::string(sources_.slice(token.range));
}

SourceRange Parser::range_from(SourceLocation begin) const noexcept {
  return {begin, previous().range.end};
}

void Parser::synchronize_declaration() {
  while (!at(TokenKind::end_of_file)) {
    if (previous().is(TokenKind::semicolon)) return;
    if (at_any({TokenKind::kw_fn, TokenKind::kw_struct, TokenKind::kw_enum,
                TokenKind::kw_trait, TokenKind::kw_impl, TokenKind::kw_import,
                TokenKind::kw_namespace, TokenKind::kw_public,
                TokenKind::kw_private})) return;
    advance();
  }
}

void Parser::synchronize_statement() {
  while (!at(TokenKind::end_of_file) && !at(TokenKind::right_brace)) {
    if (previous().is(TokenKind::semicolon)) return;
    if (at_any({TokenKind::kw_if, TokenKind::kw_while, TokenKind::kw_for,
                TokenKind::kw_return, TokenKind::kw_break,
                TokenKind::kw_continue, TokenKind::kw_defer,
                TokenKind::kw_unsafe, TokenKind::kw_match})) return;
    advance();
  }
}

SyntaxTree Parser::parse() {
  auto root = node(SyntaxKind::compilation_unit, current().range.begin);
  while (!at(TokenKind::end_of_file)) {
    const auto before = position_;
    root.children.push_back(parse_declaration());
    if (position_ == before) {
      report(current(), "parser made no progress");
      advance();
    }
  }
  root.range.end = current().range.end;
  return SyntaxTree(std::move(root));
}

std::string Parser::parse_attributes() {
  std::string attributes;
  while (consume(TokenKind::at)) {
    const auto attribute = expect(TokenKind::identifier, "expected attribute name after '@'");
    if (!attributes.empty()) attributes += ";";
    attributes += token_text(attribute);
    if (consume(TokenKind::left_paren)) {
      attributes += "(";
      bool first = true;
      while (!at_any({TokenKind::right_paren, TokenKind::end_of_file})) {
        if (!first) { expect(TokenKind::comma, "expected ',' between attribute arguments"); attributes += "+"; }
        if (at(TokenKind::identifier) || at(TokenKind::integer_literal) || at(TokenKind::string_literal)) attributes += token_text(advance());
        else { report(current(), "expected attribute argument"); advance(); }
        first = false;
      }
      expect(TokenKind::right_paren, "expected ')' after attribute arguments");
      attributes += ")";
    }
  }
  return attributes;
}

SyntaxNode Parser::parse_declaration() {
  std::string attributes = parse_attributes();
  std::string visibility;
  if (consume(TokenKind::kw_public)) visibility = "public";
  else if (consume(TokenKind::kw_private)) visibility = "private";
  if (at(TokenKind::kw_public) || at(TokenKind::kw_private)) {
    report(current(), "declaration may specify only one visibility modifier");
    advance();
  }
  SyntaxNode result;
  if (at(TokenKind::kw_import)) result = parse_import();
  else if (at(TokenKind::kw_namespace)) result = parse_namespace();
  else if (at(TokenKind::kw_struct)) result = parse_struct_like(SyntaxKind::struct_declaration);
  else if (at(TokenKind::kw_enum)) result = parse_enum();
  else if (at(TokenKind::kw_trait)) result = parse_struct_like(SyntaxKind::trait_declaration);
  else if (at(TokenKind::kw_impl)) result = parse_impl();
  else if (at(TokenKind::kw_const) && peek(1).is(TokenKind::kw_fn)) {
    advance();
    result = parse_function();
    if (!attributes.empty()) attributes += ";";
    attributes += "const";
  }

  else if (at(TokenKind::kw_const)) result = parse_const_declaration();
  else if (at(TokenKind::kw_comptime)) result = parse_comptime();
  else if (at(TokenKind::kw_async) && peek(1).is(TokenKind::kw_fn)) {
    advance();
    result = parse_function();
    if (!attributes.empty()) attributes += ";";
    attributes += "async";
  }

  else if (at(TokenKind::kw_unsafe) && peek(1).is(TokenKind::kw_async) && peek(2).is(TokenKind::kw_fn)) {
    advance(); advance();
    result = parse_function();
    if (!attributes.empty()) attributes += ";";
    attributes += "unsafe;async";
  }

  else if (at(TokenKind::kw_unsafe) && peek(1).is(TokenKind::kw_fn)) {
    advance();
    result = parse_function();
    if (!attributes.empty()) attributes += ";";
    attributes += "unsafe";
  }

  else if (at(TokenKind::kw_extern) && peek(1).is(TokenKind::kw_fn)) {
    advance();
    result = parse_function();
    if (!attributes.empty()) attributes += ";";
    attributes += "extern";
  }

  else if (at(TokenKind::kw_fn)) result = parse_function();
  else {
    report(current(), "expected a top-level declaration");
    result = node(SyntaxKind::error_node, current().range.begin);
    advance();
    synchronize_declaration();
    result.range.end = previous().range.end;
  }
  auto append_modifier = [&](std::string_view modifier) {
    if (modifier.empty()) return;
    if (!result.modifier.empty()) result.modifier += ";";
    result.modifier += modifier;
  };
  if (!attributes.empty()) append_modifier(attributes);
  append_modifier(visibility);
  return result;
}

SyntaxNode Parser::parse_const_declaration() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::const_declaration, begin);
  const auto type_name = parse_type_name();
  const auto name = expect(TokenKind::identifier, "expected constant name");
  result.label = type_name + " " + token_text(name);
  expect(TokenKind::equal, "expected '=' in constant declaration");
  result.children.push_back(parse_expression());
  expect(TokenKind::semicolon, "expected ';' after constant declaration");
  result.range = range_from(begin);
  return result;
}

SyntaxNode Parser::parse_comptime() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::comptime_statement, begin);
  result.children.push_back(parse_block());
  result.range = range_from(begin);
  return result;
}

SyntaxNode Parser::parse_import() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::import_declaration, begin);
  const auto first = expect(TokenKind::identifier, "expected module name after 'import'");
  result.label = token_text(first);
  while (consume(TokenKind::colon_colon)) {
    const auto part = expect(TokenKind::identifier, "expected module path component");
    result.label += "::" + token_text(part);
  }

  if (consume(TokenKind::kw_as)) {
    const auto alias = expect(TokenKind::identifier, "expected alias name after 'as'");
    result.modifier = "alias(" + token_text(alias) + ")";
  }

  expect(TokenKind::semicolon, "expected ';' after import declaration");
  result.range = range_from(begin);
  return result;
}

SyntaxNode Parser::parse_namespace() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::namespace_declaration, begin);
  const auto name = expect(TokenKind::identifier, "expected namespace name");
  result.label = token_text(name);
  while (consume(TokenKind::colon_colon)) {
    const auto part = expect(TokenKind::identifier, "expected namespace path component");
    result.label += "::" + token_text(part);
  }

  if (consume(TokenKind::semicolon)) {
    result.modifier = "file";
    // File-scoped namespaces own every declaration that follows in this source
    // segment. Project composition inserts a new namespace directive before the
    // next physical module, so this remains deterministic for combined sources.
    while (!at(TokenKind::end_of_file)) result.children.push_back(parse_declaration());
    result.range = range_from(begin);
    return result;
  }

  expect(TokenKind::left_brace, "expected '{' or ';' after namespace name");
  while (!at_any({TokenKind::right_brace, TokenKind::end_of_file})) {
    result.children.push_back(parse_declaration());
  }

  expect(TokenKind::right_brace, "expected '}' after namespace body");
  result.range = range_from(begin);
  return result;
}

SyntaxNode Parser::parse_struct_like(SyntaxKind kind) {
  const auto begin = advance().range.begin;
  auto result = node(kind, begin);
  const auto name = expect(TokenKind::identifier, "expected declaration name");
  result.label = token_text(name) + parse_generic_parameter_list();
  if (kind == SyntaxKind::trait_declaration && consume(TokenKind::equal)) {
    result.label += "=";
    bool first_alias = true;
    do {
      if (!first_alias) result.label += "+";
      const auto aliased = expect(TokenKind::identifier, "expected aliased trait name");
      result.label += token_text(aliased);
      first_alias = false;
    } while (consume(TokenKind::plus));
    expect(TokenKind::semicolon, "expected ';' after trait alias");
    result.range = range_from(begin);
    return result;
  }

  if (kind == SyntaxKind::struct_declaration && consume(TokenKind::left_paren)) {
    std::size_t field_index = 0;
    if (!at(TokenKind::right_paren)) {
      do {
        const auto field_begin = current().range.begin;
        auto field = node(SyntaxKind::field_declaration, field_begin);
        field.label = parse_type_name() + " " + std::to_string(field_index++);
        field.range = range_from(field_begin);
        result.children.push_back(std::move(field));
      } while (consume(TokenKind::comma));
    }
    expect(TokenKind::right_paren, "expected ')' after tuple-struct fields");
    expect(TokenKind::semicolon, "expected ';' after tuple-struct declaration");
    result.range = range_from(begin);
    return result;
  }

  if (kind == SyntaxKind::trait_declaration && consume(TokenKind::colon)) {
    result.label += ":";
    bool first = true;
    do {
      if (!first) result.label += "+";
      const auto supertrait = expect(TokenKind::identifier, "expected supertrait name");
      result.label += token_text(supertrait);
      first = false;
    } while (consume(TokenKind::plus));
  }

  expect(TokenKind::left_brace, "expected '{' after declaration name");
  while (!at_any({TokenKind::right_brace, TokenKind::end_of_file})) {
    const auto attributes = parse_attributes();
    if (at(TokenKind::kw_fn)) {
      auto child = parse_function();
      child.modifier = attributes;
      result.children.push_back(std::move(child));
    } else if (kind == SyntaxKind::trait_declaration && at(TokenKind::kw_type)) {
      auto child = parse_associated_type(); child.modifier = attributes; result.children.push_back(std::move(child));
    } else if (kind == SyntaxKind::trait_declaration && at(TokenKind::kw_const)) {
      auto child = parse_associated_const(); child.modifier = attributes; result.children.push_back(std::move(child));
    } else {
      if (!attributes.empty()) report(current(), "attributes are only supported on trait methods and associated items");
      result.children.push_back(parse_field());
    }
  }

  expect(TokenKind::right_brace, "expected '}' after declaration body");
  (void)consume(TokenKind::semicolon);
  result.range = range_from(begin);
  return result;
}

std::string Parser::parse_generic_parameter_list() {
  if (!consume(TokenKind::less)) return {};
  std::string result = "<";
  bool first = true;
  while (!at_any({TokenKind::greater, TokenKind::end_of_file})) {
    if (!first) { expect(TokenKind::comma, "expected ',' between generic parameters"); result += ","; }
    if (consume(TokenKind::kw_const)) {
      const auto const_type = expect(TokenKind::identifier, "expected const generic parameter type");
      const auto parameter = expect(TokenKind::identifier, "expected const generic parameter name");
      result += "const " + token_text(const_type) + " " + token_text(parameter);
      first = false;
      continue;
    }
    const auto parameter = at(TokenKind::lifetime_identifier) ? advance() : expect(TokenKind::identifier, "expected generic or lifetime parameter name");
    result += token_text(parameter);
    if (consume(TokenKind::colon)) {
      result += ":";
      bool first_bound = true;
      do {
        if (!first_bound) result += "+";
        const auto bound = expect(TokenKind::identifier, "expected trait bound");
        result += token_text(bound);
        first_bound = false;
      } while (consume(TokenKind::plus));
    }
    first = false;
  }

  expect(TokenKind::greater, "expected '>' after generic parameters");
  result += ">";
  return result;
}

std::string Parser::parse_const_expression_text(TokenKind closing) {
  std::string result;
  int parentheses = 0;
  while (!at(TokenKind::end_of_file)) {
    if (parentheses == 0 && (at(closing) || at(TokenKind::comma))) break;
    if (at(TokenKind::left_paren)) ++parentheses;
    else if (at(TokenKind::right_paren)) {
      if (parentheses == 0) break;
      --parentheses;
    }
    result += token_text(advance());
  }
  return result;
}

std::string Parser::parse_type_name() {
  std::string result;
  if (consume(TokenKind::kw_fn)) {
    result = "fn(";
    expect(TokenKind::left_paren, "expected '(' after 'fn' in function type");
    bool first = true;
    if (!at(TokenKind::right_paren)) {
      do {
        if (!first) result += ",";
        result += parse_type_name();
        first = false;
      } while (consume(TokenKind::comma));
    }
    expect(TokenKind::right_paren, "expected ')' after function parameter types");
    result += ")->";
    expect(TokenKind::arrow, "expected '->' in function type");
    result += parse_type_name();
  } else if (consume(TokenKind::kw_dyn)) {
    const auto trait = expect(TokenKind::identifier, "expected trait name after 'dyn'");
    result = "dyn " + token_text(trait);
  } else if (consume(TokenKind::left_paren)) {
    result = "(";
    bool first = true;
    if (!at(TokenKind::right_paren)) {
      do {
        if (!first) result += ",";
        result += parse_type_name();
        first = false;
      } while (consume(TokenKind::comma));
    }
    expect(TokenKind::right_paren, "expected ')' after tuple type");
    result += ")";
  } else {
    const auto base = expect(TokenKind::identifier, "expected type name");
    result = token_text(base);
    if ((result == "Fn" || result == "FnMut" || result == "FnOnce") && consume(TokenKind::left_paren)) {
      result += "(";
      bool first = true;
      if (!at(TokenKind::right_paren)) {
        do {
          if (!first) result += ",";
          result += parse_type_name();
          first = false;
        } while (consume(TokenKind::comma));
      }
      expect(TokenKind::right_paren, "expected ')' after callable parameter types");
      result += ")->";
      expect(TokenKind::arrow, "expected '->' in callable type");
      result += parse_type_name();
    }
  }

  if (consume(TokenKind::less)) {
    result += "<";
    bool first = true;
    while (!at_any({TokenKind::greater, TokenKind::end_of_file})) {
      if (!first) { expect(TokenKind::comma, "expected ',' between type arguments"); result += ","; }
      const bool const_expression = at(TokenKind::integer_literal) || at(TokenKind::left_paren) ||
          at(TokenKind::plus) || at(TokenKind::minus) || at(TokenKind::tilde) ||
          (at(TokenKind::identifier) && (peek(1).is(TokenKind::plus) || peek(1).is(TokenKind::minus) || peek(1).is(TokenKind::star) ||
              peek(1).is(TokenKind::slash) || peek(1).is(TokenKind::percent) || peek(1).is(TokenKind::less_less) ||
              peek(1).is(TokenKind::greater_greater) || peek(1).is(TokenKind::ampersand) ||
              peek(1).is(TokenKind::pipe) || peek(1).is(TokenKind::caret)));
      if (const_expression) result += parse_const_expression_text(TokenKind::greater);
      else if (at(TokenKind::identifier) && (peek(1).is(TokenKind::comma) || peek(1).is(TokenKind::greater)))
        result += token_text(advance());
      else result += parse_type_name();
      first = false;
    }
    expect(TokenKind::greater, "expected '>' after type arguments");
    result += ">";
  }

  while (consume(TokenKind::colon_colon)) {
    const auto segment = expect(TokenKind::identifier, "expected associated type path segment");
    result += "::" + token_text(segment);
  }

  if (consume(TokenKind::left_bracket)) {
    if (consume(TokenKind::right_bracket)) {
      result += "[]";
      if (consume(TokenKind::kw_mut)) result += "mut";
    } else {
      const auto count = parse_const_expression_text(TokenKind::right_bracket);
      if (count.empty()) report(current(), "expected fixed array length constant expression");
      expect(TokenKind::right_bracket, "expected ']' after fixed array length");
      result += "[" + count + "]";
    }
  }

  if (consume(TokenKind::star)) {
    result += "*";
    if (consume(TokenKind::kw_mut)) result += "mut";
    else if (consume(TokenKind::kw_const)) result += "const";
  }

  if (consume(TokenKind::ampersand)) {
    result += "&";
    if (at(TokenKind::lifetime_identifier)) result += token_text(advance());
    if (consume(TokenKind::kw_mut)) result += "mut";
  }
  return result;
}

SyntaxNode Parser::parse_enum() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::enum_declaration, begin);
  const auto name = expect(TokenKind::identifier, "expected enum name");
  result.label = token_text(name) + parse_generic_parameter_list();
  expect(TokenKind::left_brace, "expected '{' after enum name");
  std::int64_t next_discriminant = 0;
  while (!at_any({TokenKind::right_brace, TokenKind::end_of_file})) {
    const auto variant_begin = current().range.begin;
    auto variant = node(SyntaxKind::enum_variant, variant_begin);
    const auto variant_name = expect(TokenKind::identifier, "expected enum variant name");
    variant.label = token_text(variant_name);
    std::vector<std::string> payload_types;
    if (consume(TokenKind::left_paren)) {
      if (!at(TokenKind::right_paren)) {
        do { payload_types.push_back(parse_type_name()); } while (consume(TokenKind::comma));
      }
      expect(TokenKind::right_paren, "expected ')' after enum payload types");
    }
    if (!payload_types.empty()) {
      variant.label += "(";
      for (std::size_t index = 0; index < payload_types.size(); ++index) {
        if (index != 0) variant.label += ",";
        variant.label += payload_types[index];
      }
      variant.label += ")";
    }
    if (consume(TokenKind::equal)) {
      bool negative = consume(TokenKind::minus);
      const auto value = expect(TokenKind::integer_literal, "expected integer enum discriminant");
      variant.label += "=" + std::string(negative ? "-" : "") + token_text(value);
      try { next_discriminant = std::stoll((negative ? "-" : "") + token_text(value)) + 1; }
      catch (...) { next_discriminant = 0; }
    } else {
      variant.label += "=" + std::to_string(next_discriminant++);
    }
    variant.range = range_from(variant_begin);
    result.children.push_back(std::move(variant));
    if (!consume(TokenKind::comma)) break;
  }

  expect(TokenKind::right_brace, "expected '}' after enum body");
  (void)consume(TokenKind::semicolon);
  result.range = range_from(begin);
  return result;
}

SyntaxNode Parser::parse_impl() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::impl_declaration, begin);
  result.label = parse_generic_parameter_list();
  if (!result.label.empty()) result.label += " ";
  if (consume(TokenKind::bang)) result.label += "!";
  if (!at(TokenKind::identifier)) {
    report(current(), "expected type or trait after 'impl'");
    result.label += token_text(advance());
  } else {
    result.label += parse_type_name();
  }

  if (consume(TokenKind::kw_for)) {
    result.label += " for " + parse_type_name();
  }

  expect(TokenKind::left_brace, "expected '{' after impl header");
  while (!at_any({TokenKind::right_brace, TokenKind::end_of_file})) {
    const auto attributes = parse_attributes();
    if (at(TokenKind::kw_unsafe) && peek(1).is(TokenKind::kw_fn)) { advance(); auto child = parse_function(); child.modifier = attributes.empty() ? "unsafe" : attributes + ";unsafe"; result.children.push_back(std::move(child)); }
    else if (at(TokenKind::kw_fn)) { auto child = parse_function(); child.modifier = attributes; result.children.push_back(std::move(child)); }
    else if (at(TokenKind::kw_type)) { auto child = parse_associated_type(); child.modifier = attributes; result.children.push_back(std::move(child)); }
    else if (at(TokenKind::kw_const)) { auto child = parse_associated_const(); child.modifier = attributes; result.children.push_back(std::move(child)); }
    else {
      report(current(), "expected function or associated item declaration in impl block");
      advance();
      synchronize_declaration();
    }
  }

  expect(TokenKind::right_brace, "expected '}' after impl block");
  result.range = range_from(begin);
  return result;
}

SyntaxNode Parser::parse_associated_type() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::associated_type_declaration, begin);
  const auto name = expect(TokenKind::identifier, "expected associated type name");
  result.label = token_text(name);
  if (consume(TokenKind::equal)) result.label += "=" + parse_type_name();
  expect(TokenKind::semicolon, "expected ';' after associated type declaration");
  result.range = range_from(begin);
  return result;
}

SyntaxNode Parser::parse_associated_const() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::associated_const_declaration, begin);
  const auto name = expect(TokenKind::identifier, "expected associated constant name");
  expect(TokenKind::colon, "expected ':' after associated constant name");
  result.label = token_text(name) + ":" + parse_type_name();
  if (consume(TokenKind::equal)) {
    const auto value = parse_const_expression_text(TokenKind::semicolon);
    if (value.empty()) report(current(), "expected associated constant expression");
    result.label += "=" + value;
  }

  expect(TokenKind::semicolon, "expected ';' after associated constant declaration");
  result.range = range_from(begin);
  return result;
}

SyntaxNode Parser::parse_function() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::function_declaration, begin);
  const auto name = expect(TokenKind::identifier, "expected function name");
  result.label = token_text(name) + parse_generic_parameter_list();
  expect(TokenKind::left_paren, "expected '(' after function name");
  if (!at(TokenKind::right_paren)) {
    do { result.children.push_back(parse_parameter()); }
    while (consume(TokenKind::comma) && !at(TokenKind::right_paren));
  }

  expect(TokenKind::right_paren, "expected ')' after parameters");
  if (consume(TokenKind::arrow)) {
    result.label += " -> " + parse_type_name();
  }

  if (consume(TokenKind::semicolon)) {
    result.range = range_from(begin);
    return result;
  }
  result.children.push_back(parse_block());
  result.range = range_from(begin);
  return result;
}

SyntaxNode Parser::parse_parameter() {
  const auto begin = current().range.begin;
  auto result = node(SyntaxKind::parameter, begin);
  std::string type_name = parse_type_name();
  const auto name = expect(TokenKind::identifier, "expected parameter name");
  if (consume(TokenKind::left_bracket)) {
    if (consume(TokenKind::right_bracket)) {
      type_name += "[]";
    } else {
      const auto count = parse_const_expression_text(TokenKind::right_bracket);
      if (count.empty()) report(current(), "expected fixed array length constant expression");
      expect(TokenKind::right_bracket, "expected ']' after fixed array length");
      type_name += "[" + count + "]";
    }
  }
  result.label = type_name + " " + token_text(name);
  result.range = range_from(begin);
  return result;
}

SyntaxNode Parser::parse_field() {
  const auto begin = current().range.begin;
  auto result = node(SyntaxKind::field_declaration, begin);
  std::string type_name = parse_type_name();
  const auto name = expect(TokenKind::identifier, "expected field name");
  if (consume(TokenKind::left_bracket)) {
    if (consume(TokenKind::right_bracket)) {
      type_name += "[]";
    } else {
      const auto count = parse_const_expression_text(TokenKind::right_bracket);
      if (count.empty()) report(current(), "expected fixed array length constant expression");
      expect(TokenKind::right_bracket, "expected ']' after fixed array length");
      type_name += "[" + count + "]";
    }
  }
  result.label = type_name + " " + token_text(name);
  expect(TokenKind::semicolon, "expected ';' after field declaration");
  result.range = range_from(begin);
  return result;
}

SyntaxNode Parser::parse_statement() {
  if (at(TokenKind::left_brace)) return parse_block();
  if (at(TokenKind::kw_if)) return parse_if();
  if (at(TokenKind::kw_while)) return parse_while();
  if (at(TokenKind::kw_for)) return parse_for();
  if (at(TokenKind::kw_return)) return parse_return();
  if (at(TokenKind::kw_defer)) return parse_defer();
  if (at(TokenKind::kw_comptime)) return parse_comptime();
  if (at(TokenKind::kw_const)) return parse_const_declaration();
  if (at(TokenKind::kw_unsafe)) return parse_unsafe();
  if (at(TokenKind::kw_match)) return parse_match();
  if (at(TokenKind::kw_break)) {
    const auto begin = advance().range.begin;
    auto result = node(SyntaxKind::break_statement, begin);
    expect(TokenKind::semicolon, "expected ';' after break");
    result.range = range_from(begin); return result;
  }

  if (at(TokenKind::kw_continue)) {
    const auto begin = advance().range.begin;
    auto result = node(SyntaxKind::continue_statement, begin);
    expect(TokenKind::semicolon, "expected ';' after continue");
    result.range = range_from(begin); return result;
  }

  if (consume(TokenKind::semicolon)) {
    auto result = node(SyntaxKind::empty_statement, previous().range.begin);
    result.range = previous().range; return result;
  }

  return parse_variable_or_expression_statement();
}

SyntaxNode Parser::parse_block() {
  const auto begin = expect(TokenKind::left_brace, "expected '{'").range.begin;
  auto result = node(SyntaxKind::block_statement, begin);
  while (!at_any({TokenKind::right_brace, TokenKind::end_of_file})) {
    const auto before = position_;
    result.children.push_back(parse_statement());
    if (position_ == before) { advance(); synchronize_statement(); }
  }

  expect(TokenKind::right_brace, "expected '}' after block");
  result.range = range_from(begin);
  return result;
}

SyntaxNode Parser::parse_if() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::if_statement, begin);
  expect(TokenKind::left_paren, "expected '(' after 'if'");
  result.children.push_back(parse_expression());
  expect(TokenKind::right_paren, "expected ')' after if condition");
  result.children.push_back(parse_block());
  if (consume(TokenKind::kw_else)) {
    result.children.push_back(at(TokenKind::kw_if) ? parse_if() : parse_block());
  }
  result.range = range_from(begin); return result;
}

SyntaxNode Parser::parse_while() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::while_statement, begin);
  expect(TokenKind::left_paren, "expected '(' after 'while'");
  result.children.push_back(parse_expression());
  expect(TokenKind::right_paren, "expected ')' after while condition");
  result.children.push_back(parse_block());
  result.range = range_from(begin); return result;
}

SyntaxNode Parser::parse_for() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::for_statement, begin);
  if (at(TokenKind::identifier) && peek(1).is(TokenKind::kw_in)) {
    result.label = token_text(advance());
    advance();
    result.children.push_back(parse_expression());
  } else {
    result.children.push_back(parse_expression());
  }
  result.children.push_back(parse_block());
  result.range = range_from(begin); return result;
}

SyntaxNode Parser::parse_return() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::return_statement, begin);
  if (!at(TokenKind::semicolon)) result.children.push_back(parse_expression());
  expect(TokenKind::semicolon, "expected ';' after return statement");
  result.range = range_from(begin); return result;
}

SyntaxNode Parser::parse_defer() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::defer_statement, begin);
  result.children.push_back(parse_statement());
  result.range = range_from(begin); return result;
}

SyntaxNode Parser::parse_unsafe() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::unsafe_statement, begin);
  result.children.push_back(parse_block());
  result.range = range_from(begin); return result;
}

SyntaxNode Parser::parse_match() {
  const auto begin = advance().range.begin;
  auto result = node(SyntaxKind::match_statement, begin);
  const bool saved_struct_literal = allow_struct_literal_;
  allow_struct_literal_ = false;
  result.children.push_back(parse_expression());
  allow_struct_literal_ = saved_struct_literal;
  expect(TokenKind::left_brace, "expected '{' after match value");
  while (!at_any({TokenKind::right_brace, TokenKind::end_of_file})) {
    const auto arm_begin = current().range.begin;
    auto arm = node(SyntaxKind::match_arm, arm_begin);
    if (at(TokenKind::identifier) && token_text(current()) == "_") {
      arm.label = token_text(advance());
    } else {
      arm.children.push_back(parse_expression());
    }
    expect(TokenKind::fat_arrow, "expected '=>' after match pattern");
    arm.children.push_back(at(TokenKind::left_brace) ? parse_block() : parse_statement());
    arm.range = range_from(arm_begin);
    result.children.push_back(std::move(arm));
    (void)consume(TokenKind::comma);
  }

  expect(TokenKind::right_brace, "expected '}' after match arms");
  result.range = range_from(begin);
  return result;
}

bool Parser::looks_like_variable_declaration() const noexcept {
  std::size_t distance = 0;
  if (at(TokenKind::kw_dyn)) {
    return peek(1).is(TokenKind::identifier) && peek(2).is(TokenKind::identifier);
  }

  if (at(TokenKind::kw_fn)) {
    distance = 1;
    if (!peek(distance).is(TokenKind::left_paren)) return false;
    int depth = 0;
    do {
      if (peek(distance).is(TokenKind::left_paren)) ++depth;
      else if (peek(distance).is(TokenKind::right_paren)) --depth;
      ++distance;
    } while (depth > 0 && !peek(distance).is(TokenKind::end_of_file));
    if (depth != 0 || !peek(distance).is(TokenKind::arrow)) return false;
    ++distance;
    // Function return types currently begin with an identifier or tuple.
    if (peek(distance).is(TokenKind::left_paren)) {
      int return_depth = 0;
      do {
        if (peek(distance).is(TokenKind::left_paren)) ++return_depth;
        else if (peek(distance).is(TokenKind::right_paren)) --return_depth;
        ++distance;
      } while (return_depth > 0 && !peek(distance).is(TokenKind::end_of_file));
    } else {
      if (!peek(distance).is(TokenKind::identifier)) return false;
      ++distance;
    }
    return peek(distance).is(TokenKind::identifier);
  }

  if (at(TokenKind::left_paren)) {
    int depth = 0;
    do {
      if (peek(distance).is(TokenKind::left_paren)) ++depth;
      else if (peek(distance).is(TokenKind::right_paren)) --depth;
      ++distance;
    } while (depth > 0 && !peek(distance).is(TokenKind::end_of_file));
    return depth == 0 && peek(distance).is(TokenKind::identifier);
  }

  if (!at(TokenKind::identifier)) return false;
  distance = 1;
  if (peek(distance).is(TokenKind::less)) {
    int depth = 0;
    do {
      if (peek(distance).is(TokenKind::less)) ++depth;
      else if (peek(distance).is(TokenKind::greater)) --depth;
      ++distance;
    } while (depth > 0 && !peek(distance).is(TokenKind::end_of_file));
  }

  if (peek(distance).is(TokenKind::left_bracket)) {
    while (!peek(distance).is(TokenKind::right_bracket) && !peek(distance).is(TokenKind::end_of_file)) ++distance;
    if (peek(distance).is(TokenKind::right_bracket)) ++distance;
  }

  if (peek(distance).is(TokenKind::ampersand)) ++distance;
  if (peek(distance).is(TokenKind::kw_mut)) ++distance;
  return peek(distance).is(TokenKind::identifier);
}

SyntaxNode Parser::parse_variable_or_expression_statement() {
  const auto begin = current().range.begin;
  if (looks_like_variable_declaration()) {
    auto result = node(SyntaxKind::variable_declaration, begin);
    std::string type_name = parse_type_name();
    if (consume(TokenKind::ampersand)) {
      type_name += "&";
      if (consume(TokenKind::kw_mut)) type_name += "mut";
    }
    const auto name = expect(TokenKind::identifier, "expected variable name");
    if (consume(TokenKind::left_bracket)) {
      if (consume(TokenKind::right_bracket)) {
        type_name += "[]";
      } else {
        const auto count = parse_const_expression_text(TokenKind::right_bracket);
        if (count.empty()) report(current(), "expected fixed array length constant expression");
        expect(TokenKind::right_bracket, "expected ']' after fixed array length");
        type_name += "[" + count + "]";
      }
    }
    result.label = type_name + " " + token_text(name);
    if (consume(TokenKind::equal)) result.children.push_back(parse_expression());
    expect(TokenKind::semicolon, "expected ';' after variable declaration");
    result.range = range_from(begin); return result;
  }
  auto result = node(SyntaxKind::expression_statement, begin);
  result.children.push_back(parse_expression());
  expect(TokenKind::semicolon, "expected ';' after expression");
  result.range = range_from(begin); return result;
}

unsigned Parser::binary_precedence(TokenKind kind) const noexcept {
  switch (kind) {
    case TokenKind::dot_dot: case TokenKind::dot_dot_equal: return 1;
    case TokenKind::pipe_pipe: return 2;
    case TokenKind::ampersand_ampersand: return 3;
    case TokenKind::pipe: return 4;
    case TokenKind::caret: return 5;
    case TokenKind::ampersand: return 6;
    case TokenKind::equal_equal: case TokenKind::bang_equal: return 7;
    case TokenKind::less: case TokenKind::less_equal:
    case TokenKind::greater: case TokenKind::greater_equal: return 8;
    case TokenKind::less_less: case TokenKind::greater_greater: return 9;
    case TokenKind::plus: case TokenKind::minus: return 10;
    case TokenKind::star: case TokenKind::slash: case TokenKind::percent: return 11;
    default: return 0;
  }
}

bool Parser::is_assignment(TokenKind kind) const noexcept {
  return kind == TokenKind::equal || kind == TokenKind::plus_equal ||
         kind == TokenKind::minus_equal || kind == TokenKind::star_equal ||
         kind == TokenKind::slash_equal || kind == TokenKind::percent_equal ||
         kind == TokenKind::ampersand_equal || kind == TokenKind::pipe_equal ||
         kind == TokenKind::caret_equal || kind == TokenKind::less_less_equal ||
         kind == TokenKind::greater_greater_equal;
}

SyntaxNode Parser::parse_expression(unsigned minimum_precedence) {
  auto left = parse_postfix(parse_prefix());
  while (true) {
    const auto operator_kind = current().kind;
    if (is_assignment(operator_kind)) {
      if (minimum_precedence > 0) break;
      const auto begin = left.range.begin;
      const auto operation = token_text(advance());
      auto result = node(SyntaxKind::assignment_expression, begin);
      result.label = operation;
      result.children.push_back(std::move(left));
      result.children.push_back(parse_expression());
      result.range = range_from(begin);
      left = std::move(result);
      continue;
    }
    const auto precedence = binary_precedence(operator_kind);
    if (precedence == 0 || precedence < minimum_precedence) break;
    const auto begin = left.range.begin;
    const auto operation = token_text(advance());
    auto result = node(SyntaxKind::binary_expression, begin);
    result.label = operation;
    result.children.push_back(std::move(left));
    result.children.push_back(parse_expression(precedence + 1));
    result.range = range_from(begin);
    left = std::move(result);
  }
  return left;
}

SyntaxNode Parser::parse_prefix() {
  const auto begin = current().range.begin;
  if (at(TokenKind::kw_ref) && peek(1).kind == TokenKind::kw_fn) {
    advance();
    advance();
    auto result = node(SyntaxKind::closure_expression, begin);
    result.modifier = "ref";
    result.label = "__raz_closure_" + std::to_string(begin.offset);
    expect(TokenKind::left_paren, "expected '(' after borrowed closure 'fn'");
    if (!at(TokenKind::right_paren)) {
      do { result.children.push_back(parse_parameter()); }
      while (consume(TokenKind::comma) && !at(TokenKind::right_paren));
    }
    expect(TokenKind::right_paren, "expected ')' after closure parameters");
    if (consume(TokenKind::arrow)) result.label += " -> " + parse_type_name();
    else result.label += " -> void";
    result.children.push_back(parse_block());
    result.range = range_from(begin);
    return result;
  }

  if (at(TokenKind::kw_mut) && peek(1).kind == TokenKind::kw_fn) {
    advance();
    advance();
    auto result = node(SyntaxKind::closure_expression, begin);
    result.modifier = "mut";
    result.label = "__raz_closure_" + std::to_string(begin.offset);
    expect(TokenKind::left_paren, "expected '(' after mutable closure 'fn'");
    if (!at(TokenKind::right_paren)) {
      do { result.children.push_back(parse_parameter()); }
      while (consume(TokenKind::comma) && !at(TokenKind::right_paren));
    }
    expect(TokenKind::right_paren, "expected ')' after closure parameters");
    if (consume(TokenKind::arrow)) result.label += " -> " + parse_type_name();
    else result.label += " -> void";
    result.children.push_back(parse_block());
    result.range = range_from(begin);
    return result;
  }

  if (at(TokenKind::kw_move) && peek(1).kind == TokenKind::kw_fn) {
    advance();
    advance();
    auto result = node(SyntaxKind::closure_expression, begin);
    result.modifier = "move";
    result.label = "__raz_closure_" + std::to_string(begin.offset);
    expect(TokenKind::left_paren, "expected '(' after move closure 'fn'");
    if (!at(TokenKind::right_paren)) {
      do { result.children.push_back(parse_parameter()); }
      while (consume(TokenKind::comma) && !at(TokenKind::right_paren));
    }
    expect(TokenKind::right_paren, "expected ')' after closure parameters");
    if (consume(TokenKind::arrow)) result.label += " -> " + parse_type_name();
    else result.label += " -> void";
    result.children.push_back(parse_block());
    result.range = range_from(begin);
    return result;
  }

  if (consume(TokenKind::kw_fn)) {
    auto result = node(SyntaxKind::closure_expression, begin);
    result.label = "__raz_closure_" + std::to_string(begin.offset);
    expect(TokenKind::left_paren, "expected '(' after closure 'fn'");
    if (!at(TokenKind::right_paren)) {
      do { result.children.push_back(parse_parameter()); }
      while (consume(TokenKind::comma) && !at(TokenKind::right_paren));
    }
    expect(TokenKind::right_paren, "expected ')' after closure parameters");
    if (consume(TokenKind::arrow)) result.label += " -> " + parse_type_name();
    else result.label += " -> void";
    result.children.push_back(parse_block());
    result.range = range_from(begin);
    return result;
  }

  if (at_any({TokenKind::bang, TokenKind::minus, TokenKind::plus,
              TokenKind::tilde, TokenKind::star, TokenKind::ampersand,
              TokenKind::kw_await, TokenKind::kw_spawn, TokenKind::kw_move})) {
    auto result = node(SyntaxKind::unary_expression, begin);
    result.label = token_text(advance());
    if (result.label == "&" && consume(TokenKind::kw_mut)) result.label = "&mut";
    result.children.push_back(parse_postfix(parse_prefix()));
    result.range = range_from(begin); return result;
  }

  if (consume(TokenKind::left_bracket)) {
    auto result = node(SyntaxKind::array_expression, begin);
    if (!at(TokenKind::right_bracket)) {
      do { result.children.push_back(parse_expression()); }
      while (consume(TokenKind::comma));
    }
    expect(TokenKind::right_bracket, "expected ']' after array literal");
    result.range = range_from(begin); return result;
  }

  if (consume(TokenKind::left_paren)) {
    if (consume(TokenKind::right_paren)) {
      auto result = node(SyntaxKind::tuple_expression, begin);
      result.range = range_from(begin);
      return result;
    }
    auto first = parse_expression();
    if (consume(TokenKind::comma)) {
      auto result = node(SyntaxKind::tuple_expression, begin);
      result.children.push_back(std::move(first));
      if (!at(TokenKind::right_paren)) {
        do { result.children.push_back(parse_expression()); }
        while (consume(TokenKind::comma) && !at(TokenKind::right_paren));
      }
      expect(TokenKind::right_paren, "expected ')' after tuple expression");
      result.range = range_from(begin);
      return result;
    }
    auto result = node(SyntaxKind::parenthesized_expression, begin);
    result.children.push_back(std::move(first));
    expect(TokenKind::right_paren, "expected ')' after expression");
    result.range = range_from(begin); return result;
  }

  if (at(TokenKind::identifier) || at_any({TokenKind::kw_true, TokenKind::kw_false,
                                           TokenKind::kw_null})) {
    auto result = node(SyntaxKind::name_expression, begin);
    result.label = token_text(advance());
    result.range = range_from(begin); return result;
  }

  if (at_any({TokenKind::integer_literal, TokenKind::float_literal,
              TokenKind::string_literal, TokenKind::character_literal})) {
    auto result = node(SyntaxKind::literal_expression, begin);
    result.label = token_text(advance()); result.range = range_from(begin); return result;
  }

  report(current(), "expected expression");
  auto result = node(SyntaxKind::error_node, begin);
  if (!at(TokenKind::end_of_file)) advance();
  result.range = range_from(begin); return result;
}

SyntaxNode Parser::parse_postfix(SyntaxNode expression) {
  while (true) {
    const auto begin = expression.range.begin;
    // Explicit generic call syntax is accepted only when a balanced type-argument
    // list is immediately followed by `(`. This keeps `left < right` unambiguous.
    if (expression.kind == SyntaxKind::name_expression && at(TokenKind::less)) {
      std::size_t distance = 0;
      int angle_depth = 0;
      int paren_depth = 0;
      int bracket_depth = 0;
      bool balanced_generic = false;
      while (true) {
        const auto kind = peek(distance).kind;
        if (kind == TokenKind::end_of_file || kind == TokenKind::semicolon ||
            kind == TokenKind::left_brace || kind == TokenKind::right_brace) {
          break;
        }
        if (kind == TokenKind::less) {
          ++angle_depth;
        } else if (kind == TokenKind::greater) {
          if (angle_depth <= 0) break;
          --angle_depth;
        } else if (kind == TokenKind::left_paren) {
          ++paren_depth;
        } else if (kind == TokenKind::right_paren) {
          if (paren_depth == 0) break;
          --paren_depth;
        } else if (kind == TokenKind::left_bracket) {
          ++bracket_depth;
        } else if (kind == TokenKind::right_bracket) {
          if (bracket_depth == 0) break;
          --bracket_depth;
        }
        ++distance;
        if (angle_depth == 0) {
          balanced_generic = true;
          break;
        }
      }
      const bool followed_by_call = balanced_generic && peek(distance).is(TokenKind::left_paren);
      const bool followed_by_member = balanced_generic &&
          (peek(distance).is(TokenKind::dot) || peek(distance).is(TokenKind::colon_colon));
      const bool followed_by_struct = balanced_generic && peek(distance).is(TokenKind::left_brace);
      if (followed_by_call || followed_by_member || followed_by_struct) {
        (void)consume(TokenKind::less);
        std::string generic_name = expression.label + "<";
        bool first = true;
        while (!at_any({TokenKind::greater, TokenKind::end_of_file})) {
          if (!first) { expect(TokenKind::comma, "expected ',' between generic arguments"); generic_name += ","; }
          const bool const_expression = at(TokenKind::integer_literal) || at(TokenKind::left_paren) ||
              at(TokenKind::plus) || at(TokenKind::minus) || at(TokenKind::tilde) ||
              (at(TokenKind::identifier) && (peek(1).is(TokenKind::plus) || peek(1).is(TokenKind::minus) || peek(1).is(TokenKind::star) ||
                  peek(1).is(TokenKind::slash) || peek(1).is(TokenKind::percent) || peek(1).is(TokenKind::less_less) ||
                  peek(1).is(TokenKind::greater_greater) || peek(1).is(TokenKind::ampersand) ||
                  peek(1).is(TokenKind::pipe) || peek(1).is(TokenKind::caret)));
          if (const_expression) generic_name += parse_const_expression_text(TokenKind::greater);
          else generic_name += parse_type_name();
          first = false;
        }
        expect(TokenKind::greater, "expected '>' after generic arguments");
        generic_name += ">";
        expression.label = std::move(generic_name);
        expression.range = range_from(begin);
        continue;
      }
    }
    if (consume(TokenKind::colon_colon)) {
      const auto name = expect(TokenKind::identifier, "expected associated member name after '::'");
      auto member = node(SyntaxKind::member_expression, begin);
      member.label = token_text(name);
      member.modifier = "scoped";
      member.children.push_back(std::move(expression));
      member.range = range_from(begin);
      expression = std::move(member);
      continue;
    }
    if (allow_struct_literal_ && expression.kind == SyntaxKind::name_expression && consume(TokenKind::left_brace)) {
      auto literal = node(SyntaxKind::struct_expression, begin);
      literal.label = expression.label;
      if (!at(TokenKind::right_brace)) {
        do {
          const auto field_name = expect(TokenKind::identifier, "expected field name in struct initializer");
          auto field = node(SyntaxKind::field_initializer, field_name.range.begin);
          field.label = token_text(field_name);
          if (consume(TokenKind::colon)) {
            field.children.push_back(parse_expression());
          } else {
            auto shorthand = node(SyntaxKind::name_expression, field_name.range.begin);
            shorthand.label = field.label;
            shorthand.range = field_name.range;
            field.children.push_back(std::move(shorthand));
          }
          field.range = range_from(field_name.range.begin);
          literal.children.push_back(std::move(field));
        } while (consume(TokenKind::comma) && !at(TokenKind::right_brace));
      }
      expect(TokenKind::right_brace, "expected '}' after struct initializer");
      literal.range = range_from(begin); expression = std::move(literal); continue;
    }
    if (consume(TokenKind::left_paren)) {
      auto call = node(SyntaxKind::call_expression, begin);
      call.children.push_back(std::move(expression));
      if (!at(TokenKind::right_paren)) {
        do { call.children.push_back(parse_expression()); }
        while (consume(TokenKind::comma) && !at(TokenKind::right_paren));
      }
      expect(TokenKind::right_paren, "expected ')' after arguments");
      call.range = range_from(begin); expression = std::move(call); continue;
    }
    if (consume(TokenKind::dot)) {
      auto member = node(SyntaxKind::member_expression, begin);
      member.children.push_back(std::move(expression));
      Token name;
      if (at(TokenKind::identifier) || at(TokenKind::integer_literal)) name = advance();
      else name = expect(TokenKind::identifier, "expected member name or tuple index after '.'");
      member.label = token_text(name);
      member.range = range_from(begin); expression = std::move(member); continue;
    }
    if (consume(TokenKind::left_bracket)) {
      auto index = node(SyntaxKind::index_expression, begin);
      index.children.push_back(std::move(expression));
      index.children.push_back(parse_expression());
      expect(TokenKind::right_bracket, "expected ']' after index");
      index.range = range_from(begin); expression = std::move(index); continue;
    }
    if (consume(TokenKind::question)) {
      auto tried = node(SyntaxKind::try_expression, begin);
      tried.children.push_back(std::move(expression));
      tried.range = range_from(begin); expression = std::move(tried); continue;
    }
    if (consume(TokenKind::kw_as)) {
      auto cast = node(SyntaxKind::cast_expression, begin);
      cast.label = parse_type_name();
      cast.children.push_back(std::move(expression));
      cast.range = range_from(begin); expression = std::move(cast); continue;
    }
    break;
  }
  return expression;
}

}  // namespace raz::compiler
