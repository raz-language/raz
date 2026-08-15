// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/source/source_location.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace raz::compiler {

enum class SymbolKind : std::uint8_t { type, function, parameter, variable, constant };

struct Symbol final {
  SymbolKind kind = SymbolKind::variable;
  std::string name;
  std::string type_name;
  std::vector<std::string> parameter_types;
  std::vector<std::string> generic_parameters;
  SourceRange declaration{};
  bool mutable_value = false;
  std::vector<std::string> lifetime_parameters;
  std::vector<std::string> parameter_lifetimes;
  std::string return_lifetime;
  std::vector<std::vector<std::string>> generic_bounds;
  std::vector<std::string> generic_const_types;
};

}  // namespace raz::compiler
