// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace raz::compiler {

enum class TypeKind : std::uint8_t {
  invalid,
  void_type,
  bool_type,
  signed_integer,
  unsigned_integer,
  floating_point,
  string_type,
  user,
  function,
};

struct ReferenceType final {
  std::string referent_type;
  std::string lifetime;
  bool mutable_reference = false;
};

struct RawPointerType final {
  std::string pointee_type;
  bool mutable_pointer = false;
};

enum class CallableKind : std::uint8_t { shared, mutable_call, once };

struct CallableType final {
  CallableKind kind = CallableKind::shared;
  std::vector<std::string> parameter_types;
  std::string return_type;
};

struct DynamicTraitType final {
  std::string trait_name;
};

struct FunctionType final {
  std::vector<std::string> parameter_types;
  std::string return_type;
};

struct FixedArrayType final {
  std::string element_type;
  std::uint64_t length = 0;
};

struct SliceType final {
  std::string element_type;
  bool mutable_slice = false;
};

struct TupleType final {
  std::vector<std::string> element_types;
};

struct Type final {
  TypeKind kind = TypeKind::invalid;
  std::string name;
  std::vector<std::string> parameters;
  std::string result;

  [[nodiscard]] bool valid() const noexcept { return kind != TypeKind::invalid; }
  [[nodiscard]] bool numeric() const noexcept {
    return kind == TypeKind::signed_integer || kind == TypeKind::unsigned_integer ||
           kind == TypeKind::floating_point;
  }
};

[[nodiscard]] Type builtin_type(std::string_view name);
[[nodiscard]] bool is_builtin_type(std::string_view name) noexcept;
[[nodiscard]] std::optional<FixedArrayType> parse_fixed_array_type(std::string_view name);
[[nodiscard]] std::optional<SliceType> parse_slice_type(std::string_view name);
[[nodiscard]] std::optional<TupleType> parse_tuple_type(std::string_view name);
[[nodiscard]] std::optional<ReferenceType> parse_reference_type(std::string_view name);
[[nodiscard]] std::optional<RawPointerType> parse_raw_pointer_type(std::string_view name);
[[nodiscard]] std::optional<DynamicTraitType> parse_dynamic_trait_type(std::string_view name);
[[nodiscard]] std::optional<FunctionType> parse_function_type(std::string_view name);
[[nodiscard]] std::optional<CallableType> parse_callable_type(std::string_view name);
[[nodiscard]] std::string callable_type_name(CallableKind kind, const std::vector<std::string>& parameters, std::string_view result);
[[nodiscard]] bool callable_kinds_compatible(CallableKind actual, CallableKind expected) noexcept;
[[nodiscard]] std::string function_type_name(const std::vector<std::string>& parameters, std::string_view result);
[[nodiscard]] bool reference_types_compatible(std::string_view actual, std::string_view expected);

}  // namespace raz::compiler
