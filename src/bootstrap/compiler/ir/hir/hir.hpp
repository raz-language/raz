// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/source/source_location.hpp"
#include "compiler/syntax/syntax_tree.hpp"

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace raz::compiler {

struct HirConstant final {
  std::string name;
  std::string type_name;
  std::string value;
  SourceRange range{};
};

struct HirVariable final {
  std::string name;
  std::string type_name;
  SourceRange range{};
};

struct HirFunction final {
  std::string name;
  std::vector<std::string> generic_parameters;
  std::vector<std::vector<std::string>> generic_bounds;
  std::vector<std::string> generic_const_types;
  std::vector<std::string> lifetime_parameters;
  std::vector<std::string> generic_arguments;
  bool generic_template = false;
  bool concrete_instantiation = false;
  bool is_external = false;
  bool is_async = false;
  std::string external_name;
  std::string abi = "Raz";
  std::string return_type = "void";
  std::vector<HirVariable> parameters;
  std::vector<HirVariable> locals;
  std::optional<SyntaxNode> body;
  std::vector<std::string> attributes;
  SourceRange range{};
};

struct HirField final {
  std::string name;
  std::string type_name;
  std::uint64_t offset = 0;
  std::uint64_t size = 0;
  std::uint32_t alignment = 1;
  SourceRange range{};
};

struct HirType final {
  std::string name;
  std::vector<std::string> generic_parameters;
  std::vector<std::string> generic_arguments;
  std::vector<std::string> generic_const_types;
  bool concrete_instantiation = false;
  std::string representation = "Raz";
  bool packed = false;
  std::uint32_t requested_alignment = 0;
  std::vector<std::string> attributes;
  std::vector<HirField> fields;
  std::uint64_t size = 0;
  std::uint32_t alignment = 1;
  SourceRange range{};
};


struct HirEnumVariant final {
  std::string name;
  std::vector<std::string> payload_types;
  std::vector<std::uint64_t> payload_offsets;
  std::int64_t discriminant = 0;
  std::uint64_t payload_size = 0;
  std::uint32_t payload_alignment = 1;
  SourceRange range{};
};

struct HirEnum final {
  std::string name;
  std::vector<std::string> generic_parameters;
  std::vector<std::string> generic_arguments;
  std::vector<std::string> generic_const_types;
  std::vector<HirEnumVariant> variants;
  std::uint64_t size = 4;
  std::uint32_t alignment = 4;
  std::uint64_t payload_offset = 4;
  bool concrete_instantiation = false;
  SourceRange range{};
};

struct HirTraitMethod final {
  std::string name;
  std::uint32_t vtable_slot = 0;
  std::string return_type = "void";
  std::vector<HirVariable> parameters;
  bool has_default = false;
  SourceRange range{};
};

struct HirAssociatedType final {
  std::string name;
  SourceRange range{};
};

struct HirAssociatedConst final {
  std::string name;
  std::string type_name;
  SourceRange range{};
};

struct HirTrait final {
  std::string name;
  bool object_safe = false;
  std::vector<std::string> generic_parameters;
  std::vector<std::string> supertraits;
  std::vector<std::string> alias_targets;
  std::vector<HirAssociatedType> associated_types;
  std::vector<HirAssociatedConst> associated_constants;
  std::vector<HirTraitMethod> methods;
  SourceRange range{};
};

struct HirAssociatedTypeBinding final {
  std::string trait_name;
  std::string target_type;
  std::string name;
  std::string type_name;
  SourceRange range{};
};

struct HirAssociatedConstBinding final {
  std::string trait_name;
  std::string target_type;
  std::string name;
  std::string type_name;
  std::string value;
  SourceRange range{};
};

struct HirTraitImplementation final {
  std::string trait_name;
  std::string target_type;
  std::string method_name;
  std::string function_name;
  SourceRange range{};
};

struct HirModule final {
  std::vector<HirConstant> constants;
  std::vector<HirType> types;
  std::vector<HirEnum> enums;
  std::vector<HirFunction> functions;
  std::vector<HirTrait> traits;
  std::vector<HirTraitImplementation> trait_implementations;
  std::vector<HirAssociatedTypeBinding> associated_type_bindings;
  std::vector<HirAssociatedConstBinding> associated_const_bindings;
  void dump(std::ostream& stream) const;
};

}  // namespace raz::compiler
