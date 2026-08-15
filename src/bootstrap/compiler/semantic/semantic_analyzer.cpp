// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/semantic/semantic_analyzer.hpp"

#include "compiler/diagnostics/diagnostic_engine.hpp"
#include "compiler/semantic/type.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <utility>
#include <stdexcept>

#include <optional>

namespace raz::compiler {
namespace {


bool is_numeric(const std::string& type) { return builtin_type(type).numeric(); }

std::optional<std::pair<std::string, std::string>> primitive_integer_constant(std::string_view name) {
  const auto separator = name.find("::");
  if (separator == std::string_view::npos || name.find("::", separator + 2) != std::string_view::npos) return std::nullopt;
  const auto type = name.substr(0, separator);
  const auto member = name.substr(separator + 2);
  if (member != "MIN" && member != "MAX") return std::nullopt;
  const bool minimum = member == "MIN";
  if (type == "i8") return std::pair{std::string("i8"), std::string(minimum ? "-128" : "127")};
  if (type == "i16") return std::pair{std::string("i16"), std::string(minimum ? "-32768" : "32767")};
  if (type == "i32") return std::pair{std::string("i32"), std::string(minimum ? "-2147483648" : "2147483647")};
  if (type == "i64" || type == "int" || type == "isize")
    return std::pair{std::string(type), std::string(minimum ? "-9223372036854775808" : "9223372036854775807")};
  if (type == "u8" || type == "byte") return std::pair{std::string(type), std::string(minimum ? "0" : "255")};
  if (type == "u16") return std::pair{std::string("u16"), std::string(minimum ? "0" : "65535")};
  if (type == "u32") return std::pair{std::string("u32"), std::string(minimum ? "0" : "4294967295")};
  // MIR/bootstrap scalar immediates are signed 64-bit today. Reject u64::MAX
  // rather than truncating it until wide unsigned immediates are represented.
  if (type == "u64" || type == "uint" || type == "usize") {
    if (minimum) return std::pair{std::string(type), std::string("0")};
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<std::uint32_t> decode_character_literal(const std::string& text) {
  if (text.size() < 3 || text.front() != '\'' || text.back() != '\'') return std::nullopt;
  if (text[1] != '\\') {
    if (text.size() != 3) return std::nullopt;
    return static_cast<std::uint32_t>(static_cast<unsigned char>(text[1]));
  }
  if (text.size() != 4) return std::nullopt;
  switch (text[2]) {
    case 'n': return static_cast<std::uint32_t>('\n');
    case 'r': return static_cast<std::uint32_t>('\r');
    case 't': return static_cast<std::uint32_t>('\t');
    case '0': return 0u;
    case '\\': return static_cast<std::uint32_t>('\\');
    case '\'': return static_cast<std::uint32_t>('\'');
    case '"': return static_cast<std::uint32_t>('"');
    default: return static_cast<std::uint32_t>(static_cast<unsigned char>(text[2]));
  }
}

bool implicit_numeric_family_compatible(const std::string& actual, const std::string& expected) {
  const auto actual_kind = builtin_type(actual).kind;
  const auto expected_kind = builtin_type(expected).kind;
  const bool actual_integer = actual_kind == TypeKind::signed_integer || actual_kind == TypeKind::unsigned_integer;
  const bool expected_integer = expected_kind == TypeKind::signed_integer || expected_kind == TypeKind::unsigned_integer;
  if (actual_integer && expected_integer) return true;
  return actual_kind == TypeKind::floating_point && expected_kind == TypeKind::floating_point;
}

bool native_cast_type(const std::string& type) {
  return type == "i8" || type == "i16" || type == "i32" || type == "i64" ||
         type == "isize" || type == "int" || type == "u8" || type == "u16" ||
         type == "u32" || type == "u64" || type == "usize" || type == "uint" ||
         type == "byte" || type == "f32" || type == "f64";
}

bool supported_numeric_cast(const std::string& source, const std::string& target) {
  if (!native_cast_type(source) || !native_cast_type(target)) return false;
  const auto source_kind = builtin_type(source).kind;
  const auto target_kind = builtin_type(target).kind;
  if (source_kind == TypeKind::floating_point && target_kind == TypeKind::unsigned_integer &&
      (target == "u64" || target == "usize" || target == "uint")) return false;
  if (source_kind == TypeKind::unsigned_integer && target_kind == TypeKind::floating_point &&
      (source == "u64" || source == "usize" || source == "uint")) return false;
  return true;
}

std::string encode_aggregate(const std::vector<std::string>& values) {
  // Length-prefix every element so nested aggregate values remain lossless.
  std::string encoded;
  for (const auto& value : values) {
    encoded += std::to_string(value.size());
    encoded.push_back(':');
    encoded += value;
  }
  return encoded;
}

std::vector<std::string> decode_aggregate(const std::string& encoded) {
  std::vector<std::string> values;
  std::size_t cursor = 0;
  while (cursor < encoded.size()) {
    const auto colon = encoded.find(':', cursor);
    if (colon == std::string::npos) return {};
    std::size_t length = 0;
    try {
      length = static_cast<std::size_t>(std::stoull(encoded.substr(cursor, colon - cursor)));
    } catch (...) {
      return {};
    }
    const auto begin = colon + 1;
    if (length > encoded.size() - begin) return {};
    values.push_back(encoded.substr(begin, length));
    cursor = begin + length;
  }
  return values;
}

std::vector<std::string> split_tuple_type(const std::string& type) {
  std::vector<std::string> result;
  if (type.size() < 2 || type.front() != '(' || type.back() != ')') return result;
  std::size_t begin = 1;
  int depth = 0;
  for (std::size_t index = 1; index + 1 < type.size(); ++index) {
    const char character = type[index];
    if (character == '(' || character == '<' || character == '[') ++depth;
    else if (character == ')' || character == '>' || character == ']') --depth;
    else if (character == ',' && depth == 0) {
      result.push_back(type.substr(begin, index - begin));
      begin = index + 1;
    }
  }
  if (begin < type.size() - 1) result.push_back(type.substr(begin, type.size() - 1 - begin));
  return result;
}

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

bool is_integral(const std::string& type) {
  const auto kind = builtin_type(type).kind;
  return kind == TypeKind::signed_integer || kind == TypeKind::unsigned_integer;
}

bool slice_types_compatible(const std::string& actual, const std::string& expected) {
  const auto expected_slice = parse_slice_type(expected);
  if (!expected_slice) return false;
  if (const auto actual_slice = parse_slice_type(actual)) {
    return actual_slice->element_type == expected_slice->element_type &&
           (!expected_slice->mutable_slice || actual_slice->mutable_slice);
  }
  const auto actual_reference = parse_reference_type(actual);
  if (!actual_reference) return false;
  const auto array = parse_fixed_array_type(actual_reference->referent_type);
  return array && array->element_type == expected_slice->element_type &&
         (!expected_slice->mutable_slice || actual_reference->mutable_reference);
}

bool contains_deferred_control_transfer(const SyntaxNode& node) {
  if (node.kind == SyntaxKind::return_statement || node.kind == SyntaxKind::break_statement ||
      node.kind == SyntaxKind::continue_statement) {
    return true;
  }
  return std::any_of(node.children.begin(), node.children.end(), contains_deferred_control_transfer);
}

bool syntax_contains_kind(const SyntaxNode& node, SyntaxKind kind) {
  if (node.kind == kind) return true;
  return std::any_of(node.children.begin(), node.children.end(),
                     [&](const SyntaxNode& child) { return syntax_contains_kind(child, kind); });
}

bool syntax_contains_call(const SyntaxNode& node, const std::string& name) {
  if (node.kind == SyntaxKind::call_expression && !node.children.empty() &&
      node.children.front().kind == SyntaxKind::name_expression && node.children.front().label == name) return true;
  return std::any_of(node.children.begin(), node.children.end(),
                     [&](const SyntaxNode& child) { return syntax_contains_call(child, name); });
}

void collect_direct_named_calls(const SyntaxNode& node, std::unordered_set<std::string>& calls) {
  if (node.kind == SyntaxKind::call_expression && !node.children.empty() &&
      node.children.front().kind == SyntaxKind::name_expression) {
    const auto& label = node.children.front().label;
    const auto open = label.find('<');
    calls.insert(open == std::string::npos ? label : label.substr(0, open));
  }
  for (const auto& child : node.children) collect_direct_named_calls(child, calls);
}

std::string attribute_name(const std::string& attribute) {
  const auto open = attribute.find('(');
  return open == std::string::npos ? attribute : attribute.substr(0, open);
}

std::string attribute_argument(const std::string& attribute) {
  const auto open = attribute.find('(');
  if (open == std::string::npos || attribute.back() != ')') return {};
  auto value = attribute.substr(open + 1, attribute.size() - open - 2);
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
  return value;
}

std::vector<std::string> split_attributes(const std::string& text) {
  std::vector<std::string> result;
  std::size_t begin = 0;
  while (begin < text.size()) {
    const auto end = text.find(';', begin);
    const auto item = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    if (item != "public" && item != "private" && !item.starts_with("alias(")) result.push_back(item);
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return result;
}

std::pair<std::string, std::vector<std::string>> split_generic_name(const std::string& name) {
  const auto open = name.find('<');
  if (open == std::string::npos || name.back() != '>') return {name, {}};
  std::vector<std::string> arguments;
  std::string current;
  int depth = 0;
  for (std::size_t index = open + 1; index + 1 < name.size(); ++index) {
    const char value = name[index];
    if (value == '<') ++depth;
    if (value == '>') --depth;
    if (value == ',' && depth == 0) { arguments.push_back(current); current.clear(); }
    else current += value;
  }
  if (!current.empty()) arguments.push_back(current);
  return {name.substr(0, open), arguments};
}

std::string unqualified_type_name(std::string name) {
  const auto separator = name.rfind("::");
  if (separator != std::string::npos) name = name.substr(separator + 2);
  // Namespace lowering uses stable internal identities such as
  // __raz_ns_core__result__Result.  Propagation is a language-level protocol,
  // so recognize the source declaration name after namespace lowering too.
  if (name.rfind("__raz_ns_", 0) == 0) {
    const auto internal_separator = name.rfind("__");
    if (internal_separator != std::string::npos && internal_separator + 2 < name.size())
      name = name.substr(internal_separator + 2);
  }
  return name;
}

bool propagation_types_compatible(const std::string& operand_type, const std::string& return_type) {
  const auto [operand_base, operand_arguments] = split_generic_name(operand_type);
  const auto [return_base, return_arguments] = split_generic_name(return_type);
  const auto operand_name = unqualified_type_name(operand_base);
  const auto return_name = unqualified_type_name(return_base);
  if (operand_name != return_name) return false;
  if (operand_name == "Option") {
    return operand_arguments.size() == 1 && return_arguments.size() == 1;
  }
  if (operand_name == "Result") {
    return operand_arguments.size() == 2 && return_arguments.size() == 2 &&
           operand_arguments[1] == return_arguments[1];
  }
  return false;
}

std::pair<std::string, std::vector<std::string>> split_generic_parameter_spec(const std::string& spec) {
  const auto colon = spec.find(':');
  if (colon == std::string::npos) return {spec, {}};
  std::vector<std::string> bounds;
  std::string current;
  for (std::size_t index = colon + 1; index <= spec.size(); ++index) {
    if (index == spec.size() || spec[index] == '+') {
      if (!current.empty()) bounds.push_back(current);
      current.clear();
    } else {
      current += spec[index];
    }
  }
  return {spec.substr(0, colon), bounds};
}

struct GenericParameterDetail final {
  std::string name;
  std::vector<std::string> bounds;
  std::string const_type;
  [[nodiscard]] bool is_const() const noexcept { return !const_type.empty(); }
};

GenericParameterDetail parse_generic_parameter_detail(const std::string& spec) {
  if (spec.starts_with("const ")) {
    const auto type_begin = std::string("const ").size();
    const auto separator = spec.find(' ', type_begin);
    if (separator == std::string::npos || separator + 1 >= spec.size()) return {};
    return {spec.substr(separator + 1), {}, spec.substr(type_begin, separator - type_begin)};
  }
  const auto [name, bounds] = split_generic_parameter_spec(spec);
  return {name, bounds, {}};
}

class ConstIntegerExpressionParser final {
 public:
  ConstIntegerExpressionParser(std::string_view text,
      const std::function<std::optional<std::int64_t>(std::string_view)>& resolve)
      : text_(text), resolve_(resolve) {}

  std::optional<std::int64_t> parse() {
    auto value = parse_or();
    skip_space();
    if (!value || position_ != text_.size()) return std::nullopt;
    return value;
  }

 private:
  void skip_space() {
    while (position_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[position_]))) ++position_;
  }
  bool consume(std::string_view token) {
    skip_space();
    if (text_.substr(position_).starts_with(token)) { position_ += token.size(); return true; }
    return false;
  }
  std::optional<std::int64_t> parse_primary() {
    skip_space();
    if (consume("(")) {
      auto value = parse_or();
      if (!consume(")")) return std::nullopt;
      return value;
    }
    if (position_ >= text_.size()) return std::nullopt;
    if (std::isdigit(static_cast<unsigned char>(text_[position_]))) {
      const auto begin = position_;
      while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) ++position_;
      try { return std::stoll(std::string(text_.substr(begin, position_ - begin))); } catch (...) { return std::nullopt; }
    }
    if (std::isalpha(static_cast<unsigned char>(text_[position_])) || text_[position_] == '_') {
      const auto begin = position_++;
      while (position_ < text_.size() &&
             (std::isalnum(static_cast<unsigned char>(text_[position_])) || text_[position_] == '_')) ++position_;
      return resolve_(text_.substr(begin, position_ - begin));
    }
    return std::nullopt;
  }
  std::optional<std::int64_t> parse_unary() {
    if (consume("+")) return parse_unary();
    if (consume("-")) { auto value = parse_unary(); return value ? std::optional<std::int64_t>{-*value} : std::nullopt; }
    if (consume("~")) { auto value = parse_unary(); return value ? std::optional<std::int64_t>{~*value} : std::nullopt; }
    return parse_primary();
  }
  template <typename Next>
  std::optional<std::int64_t> parse_binary(Next next, std::initializer_list<std::string_view> operations) {
    auto left = (this->*next)();
    if (!left) return std::nullopt;
    while (true) {
      std::string_view matched;
      for (const auto operation : operations) if (consume(operation)) { matched = operation; break; }
      if (matched.empty()) break;
      auto right = (this->*next)();
      if (!right) return std::nullopt;
      if (matched == "*") *left *= *right;
      else if (matched == "/") { if (*right == 0) return std::nullopt; *left /= *right; }
      else if (matched == "%") { if (*right == 0) return std::nullopt; *left %= *right; }
      else if (matched == "+") *left += *right;
      else if (matched == "-") *left -= *right;
      else if (matched == "<<") *left <<= *right;
      else if (matched == ">>") *left >>= *right;
      else if (matched == "&") *left &= *right;
      else if (matched == "^") *left ^= *right;
      else if (matched == "|") *left |= *right;
    }
    return left;
  }
  std::optional<std::int64_t> parse_mul() { return parse_binary(&ConstIntegerExpressionParser::parse_unary, {"*", "/", "%"}); }
  std::optional<std::int64_t> parse_add() { return parse_binary(&ConstIntegerExpressionParser::parse_mul, {"+", "-"}); }
  std::optional<std::int64_t> parse_shift() { return parse_binary(&ConstIntegerExpressionParser::parse_add, {"<<", ">>"}); }
  std::optional<std::int64_t> parse_and() { return parse_binary(&ConstIntegerExpressionParser::parse_shift, {"&"}); }
  std::optional<std::int64_t> parse_xor() { return parse_binary(&ConstIntegerExpressionParser::parse_and, {"^"}); }
  std::optional<std::int64_t> parse_or() { return parse_binary(&ConstIntegerExpressionParser::parse_xor, {"|"}); }

  std::string_view text_;
  std::size_t position_ = 0;
  const std::function<std::optional<std::int64_t>(std::string_view)>& resolve_;
};

std::optional<std::int64_t> evaluate_const_integer_expression(
    std::string_view text,
    const std::function<std::optional<std::int64_t>(std::string_view)>& resolve =
        [](std::string_view) -> std::optional<std::int64_t> { return std::nullopt; }) {
  return ConstIntegerExpressionParser(text, resolve).parse();
}

std::optional<std::pair<std::string, std::string>> split_symbolic_array_type(const std::string& name) {
  const auto left = name.rfind('[');
  if (left == std::string::npos || left == 0 || name.empty() || name.back() != ']') return std::nullopt;
  const auto count = name.substr(left + 1, name.size() - left - 2);
  if (count.empty()) return std::nullopt;
  return std::pair<std::string, std::string>{name.substr(0, left), count};
}

std::pair<std::string, std::vector<std::string>> split_variant_payload(const std::string& label) {
  const auto equal = label.rfind('=');
  const auto head = equal == std::string::npos ? label : label.substr(0, equal);
  const auto open = head.find('(');
  if (open == std::string::npos) return {head, {}};
  const auto close = head.rfind(')');
  std::vector<std::string> payloads;
  std::string current;
  int depth = 0;
  for (std::size_t index = open + 1; index < close; ++index) {
    const char value = head[index];
    if (value == '<') ++depth;
    if (value == '>') --depth;
    if (value == ',' && depth == 0) { payloads.push_back(current); current.clear(); }
    else current += value;
  }
  if (!current.empty()) payloads.push_back(current);
  return {head.substr(0, open), payloads};
}

std::string substitute_generic_type(const std::string& type_name,
                                    const std::unordered_map<std::string, std::string>& substitutions);

}  // namespace

SemanticAnalyzer::SemanticAnalyzer(DiagnosticEngine& diagnostics) : diagnostics_(diagnostics) {}


// Semantic analysis is grouped by concern while remaining one translation unit.
#include "compiler/semantic/detail/types_traits.hpp"
#include "compiler/semantic/detail/ownership.hpp"
#include "compiler/semantic/detail/comptime.hpp"
#include "compiler/semantic/detail/module_analysis.hpp"
#include "compiler/semantic/detail/statements_expressions.hpp"
#include "compiler/semantic/detail/generics.hpp"

}  // namespace raz::compiler
