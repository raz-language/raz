// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/semantic/type.hpp"

#include <charconv>

namespace raz::compiler {

Type builtin_type(std::string_view name) {
  Type type;
  type.name = std::string(name);
  if (name == "void") type.kind = TypeKind::void_type;
  else if (name == "bool") type.kind = TypeKind::bool_type;
  else if (name == "string") type.kind = TypeKind::string_type;
  else if (name == "f16" || name == "f32" || name == "f64") type.kind = TypeKind::floating_point;
  else if (name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
           name == "i128" || name == "isize" || name == "int") type.kind = TypeKind::signed_integer;
  else if (name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
           name == "u128" || name == "usize" || name == "uint" || name == "byte" || name == "char") {
    type.kind = TypeKind::unsigned_integer;
  }
  return type;
}

std::optional<ReferenceType> parse_reference_type(std::string_view name) {
  const auto amp = name.rfind('&');
  if (amp == std::string_view::npos || amp == 0) return std::nullopt;
  ReferenceType result;
  result.referent_type = std::string(name.substr(0, amp));
  auto suffix = name.substr(amp + 1);
  const auto mut = suffix.find("mut");
  result.mutable_reference = mut != std::string_view::npos;
  const auto quote = suffix.find('\'');
  if (quote != std::string_view::npos) {
    auto end = quote + 1;
    while (end < suffix.size() && suffix[end] != 'm') ++end;
    result.lifetime = std::string(suffix.substr(quote, end - quote));
  }
  return result;
}

std::optional<RawPointerType> parse_raw_pointer_type(std::string_view name) {
  bool mutable_pointer = false;
  if (name.ends_with("*mut")) {
    mutable_pointer = true;
    name.remove_suffix(4);
  } else if (name.ends_with("*const")) {
    name.remove_suffix(6);
  } else if (name.ends_with("*")) {
    name.remove_suffix(1);
  } else {
    return std::nullopt;
  }

  if (name.empty()) return std::nullopt;
  return RawPointerType{std::string(name), mutable_pointer};
}

std::optional<DynamicTraitType> parse_dynamic_trait_type(std::string_view name) {
  constexpr std::string_view prefix = "dyn ";
  if (!name.starts_with(prefix) || name.size() == prefix.size()) return std::nullopt;
  const auto trait = name.substr(prefix.size());
  if (trait.find_first_of("<>()[]*& ,") != std::string_view::npos) return std::nullopt;
  return DynamicTraitType{std::string(trait)};
}

std::optional<FunctionType> parse_function_type(std::string_view name) {
  if (!name.starts_with("fn(") ) return std::nullopt;
  const auto close = name.find(")->");
  if (close == std::string_view::npos || close < 3 || close + 3 > name.size()) return std::nullopt;
  FunctionType result;
  const auto body = name.substr(3, close - 3);
  result.return_type = std::string(name.substr(close + 3));
  if (result.return_type.empty()) return std::nullopt;
  if (body.empty()) return result;
  std::size_t start = 0;
  int angle_depth = 0;
  int paren_depth = 0;
  int bracket_depth = 0;
  for (std::size_t index = 0; index <= body.size(); ++index) {
    const bool at_end = index == body.size();
    const char ch = at_end ? ',' : body[index];
    if (!at_end) {
      if (ch == '<') ++angle_depth;
      else if (ch == '>') --angle_depth;
      else if (ch == '(') ++paren_depth;
      else if (ch == ')') --paren_depth;
      else if (ch == '[') ++bracket_depth;
      else if (ch == ']') --bracket_depth;
    }
    if (ch == ',' && angle_depth == 0 && paren_depth == 0 && bracket_depth == 0) {
      if (index == start) return std::nullopt;
      result.parameter_types.emplace_back(body.substr(start, index - start));
      start = index + 1;
    }
  }
  return result;
}

std::optional<CallableType> parse_callable_type(std::string_view name) {
  CallableType result;
  std::string_view prefix;
  if (name.starts_with("Fn(")) { result.kind = CallableKind::shared; prefix = "Fn("; }
  else if (name.starts_with("FnMut(")) { result.kind = CallableKind::mutable_call; prefix = "FnMut("; }
  else if (name.starts_with("FnOnce(")) { result.kind = CallableKind::once; prefix = "FnOnce("; }
  else return std::nullopt;
  const auto close = name.find(")->", prefix.size());
  if (close == std::string_view::npos || close + 3 > name.size()) return std::nullopt;
  const auto body = name.substr(prefix.size(), close - prefix.size());
  result.return_type = std::string(name.substr(close + 3));
  if (result.return_type.empty()) return std::nullopt;
  if (body.empty()) return result;
  std::size_t start = 0;
  int angle_depth = 0;
  int paren_depth = 0;
  int bracket_depth = 0;
  for (std::size_t index = 0; index <= body.size(); ++index) {
    const bool at_end = index == body.size();
    const char ch = at_end ? ',' : body[index];
    if (!at_end) {
      if (ch == '<') ++angle_depth;
      else if (ch == '>') --angle_depth;
      else if (ch == '(') ++paren_depth;
      else if (ch == ')') --paren_depth;
      else if (ch == '[') ++bracket_depth;
      else if (ch == ']') --bracket_depth;
    }
    if (ch == ',' && angle_depth == 0 && paren_depth == 0 && bracket_depth == 0) {
      if (index == start) return std::nullopt;
      result.parameter_types.emplace_back(body.substr(start, index - start));
      start = index + 1;
    }
  }
  return result;
}

std::string callable_type_name(CallableKind kind, const std::vector<std::string>& parameters, std::string_view result) {
  std::string name = kind == CallableKind::shared ? "Fn(" : (kind == CallableKind::mutable_call ? "FnMut(" : "FnOnce(");
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (index != 0) name += ',';
    name += parameters[index];
  }
  name += ")->";
  name += result;
  return name;
}

bool callable_kinds_compatible(CallableKind actual, CallableKind expected) noexcept {
  if (expected == CallableKind::once) return true;
  if (expected == CallableKind::mutable_call) return actual != CallableKind::once;
  return actual == CallableKind::shared;
}

std::string function_type_name(const std::vector<std::string>& parameters, std::string_view result) {
  std::string name = "fn(";
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (index != 0) name += ',';
    name += parameters[index];
  }
  name += ")->";
  name += result;
  return name;
}

bool reference_types_compatible(std::string_view actual, std::string_view expected) {
  const auto a = parse_reference_type(actual);
  const auto e = parse_reference_type(expected);
  if (!a || !e) return actual == expected;
  return a->referent_type == e->referent_type &&
         (!e->mutable_reference || a->mutable_reference);
}

std::optional<SliceType> parse_slice_type(std::string_view name) {
  bool mutable_slice = false;
  if (name.ends_with("[]mut")) {
    mutable_slice = true;
    name.remove_suffix(5);
  } else if (name.ends_with("[]")) {
    name.remove_suffix(2);
  } else {
    return std::nullopt;
  }

  if (name.empty()) return std::nullopt;
  return SliceType{std::string(name), mutable_slice};
}

std::optional<FixedArrayType> parse_fixed_array_type(std::string_view name) {
  const auto left = name.rfind('[');
  if (left == std::string_view::npos || name.empty() || name.back() != ']' || left == 0) return std::nullopt;
  const auto count_text = name.substr(left + 1, name.size() - left - 2);
  if (count_text.empty()) return std::nullopt;
  std::uint64_t length = 0;
  const auto result = std::from_chars(count_text.data(), count_text.data() + count_text.size(), length);
  if (result.ec != std::errc{} || result.ptr != count_text.data() + count_text.size() || length == 0) return std::nullopt;
  return FixedArrayType{std::string(name.substr(0, left)), length};
}

std::optional<TupleType> parse_tuple_type(std::string_view name) {
  if (name.size() < 2 || name.front() != '(' || name.back() != ')') return std::nullopt;
  TupleType result;
  const auto body = name.substr(1, name.size() - 2);
  if (body.empty()) return result;
  std::size_t start = 0;
  int angle_depth = 0;
  int paren_depth = 0;
  int bracket_depth = 0;
  for (std::size_t index = 0; index <= body.size(); ++index) {
    const bool at_end = index == body.size();
    const char ch = at_end ? ',' : body[index];
    if (!at_end) {
      if (ch == '<') ++angle_depth;
      else if (ch == '>') --angle_depth;
      else if (ch == '(') ++paren_depth;
      else if (ch == ')') --paren_depth;
      else if (ch == '[') ++bracket_depth;
      else if (ch == ']') --bracket_depth;
    }
    if (ch == ',' && angle_depth == 0 && paren_depth == 0 && bracket_depth == 0) {
      if (index == start) return std::nullopt;
      result.element_types.emplace_back(body.substr(start, index - start));
      start = index + 1;
    }
  }
  return result;
}

bool is_builtin_type(std::string_view name) noexcept { return builtin_type(name).valid(); }

}  // namespace raz::compiler
