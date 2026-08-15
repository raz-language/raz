// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

std::pair<std::string, std::string> SemanticAnalyzer::split_typed_name(const std::string& label) {
  const auto separator = label.rfind(' ');
  if (separator == std::string::npos) return {label, {}};
  return {label.substr(0, separator), label.substr(separator + 1)};
}

std::string SemanticAnalyzer::function_return_type(const std::string& label) {
  const auto arrow = label.find(" -> ");
  return arrow == std::string::npos ? "void" : label.substr(arrow + 4);
}

bool SemanticAnalyzer::type_exists(const std::string& name) const {
  if (const auto dynamic_trait = parse_dynamic_trait_type(name)) {
    return object_safe_traits_.contains(dynamic_trait->trait_name);
  }

  if (const auto function = parse_function_type(name)) {
    return type_exists(function->return_type) &&
           std::all_of(function->parameter_types.begin(), function->parameter_types.end(),
                       [&](const std::string& parameter) { return type_exists(parameter); });
  }

  if (const auto callable = parse_callable_type(name)) {
    return type_exists(callable->return_type) &&
           std::all_of(callable->parameter_types.begin(), callable->parameter_types.end(),
                       [&](const std::string& parameter) { return type_exists(parameter); });
  }

  if (const auto pointer = parse_raw_pointer_type(name)) return type_exists(pointer->pointee_type);
  if (const auto reference = parse_reference_type(name)) return type_exists(reference->referent_type);
  if (const auto slice = parse_slice_type(name)) return type_exists(slice->element_type);
  if (const auto tuple = parse_tuple_type(name)) {
    return std::all_of(tuple->element_types.begin(), tuple->element_types.end(),
                       [&](const std::string& element) { return type_exists(element); });
  }
  const auto first_projection = name.find("::");
  if (first_projection != std::string::npos) {
    const auto second_projection = name.find("::", first_projection + 2);
    if (second_projection != std::string::npos &&
        name.find("::", second_projection + 2) == std::string::npos) {
      const auto base = name.substr(0, first_projection);
      const auto trait = name.substr(first_projection + 2, second_projection - first_projection - 2);
      const auto item = name.substr(second_projection + 2);
      if (const auto concrete = associated_type_bindings_.find(name);
          concrete != associated_type_bindings_.end()) return type_exists(concrete->second);
      if (const auto bounds = current_generic_bounds_.find(base); bounds != current_generic_bounds_.end() &&
          std::find(bounds->second.begin(), bounds->second.end(), trait) != bounds->second.end()) {
        const auto declared = trait_associated_types_.find(trait);
        return declared != trait_associated_types_.end() && declared->second.contains(item);
      }
      return false;
    }
  }

  if (is_builtin_type(name)) return true;
  if (const auto array = parse_fixed_array_type(name)) return type_exists(array->element_type);
  if (const auto symbolic = split_symbolic_array_type(name)) {
    if (!type_exists(symbolic->first)) return false;
    const auto value = evaluate_const_integer_expression(symbolic->second, [&](std::string_view identifier) -> std::optional<std::int64_t> {
      if (const auto constant = constants_.find(std::string(identifier)); constant != constants_.end() &&
          is_integral(constant->second.first)) {
        try { return std::stoll(constant->second.second); } catch (...) { return std::nullopt; }
      }
      if (current_generic_bounds_.contains(std::string(identifier)) &&
          current_generic_bounds_.at(std::string(identifier)).empty()) return 1;
      return std::nullopt;
    });
    return value.has_value() && *value > 0;
  }
  const auto [base, arguments] = split_generic_name(name);
  if (!arguments.empty()) {
    const auto generic = generic_types_.find(base);
    if (generic == generic_types_.end() || generic->second.size() != arguments.size()) return false;
    const auto const_kinds = generic_const_types_.find(base);
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      const bool const_argument = const_kinds != generic_const_types_.end() &&
          index < const_kinds->second.size() && !const_kinds->second[index].empty();
      if (const_argument) {
        const auto value = evaluate_const_integer_expression(arguments[index], [&](std::string_view identifier) -> std::optional<std::int64_t> {
          if (const auto constant = constants_.find(std::string(identifier)); constant != constants_.end() &&
              is_integral(constant->second.first)) {
            try { return std::stoll(constant->second.second); } catch (...) { return std::nullopt; }
          }
          return std::nullopt;
        });
        if (!value || *value <= 0) return false;
      } else if (!type_exists(arguments[index])) {
        return false;
      }
    }
    return true;
  }
  const auto found = globals_.find(base);
  return found != globals_.end() && found->second.kind == SymbolKind::type;
}

const HirEnum* SemanticAnalyzer::resolve_enum(const std::string& name) {
  if (const auto found = enum_types_.find(name); found != enum_types_.end()) return &found->second;
  const auto [base, arguments] = split_generic_name(name);
  if (arguments.empty()) return nullptr;
  const auto parameters = generic_types_.find(base);
  const auto template_enum = enum_types_.find(base);
  if (parameters == generic_types_.end() || template_enum == enum_types_.end() ||
      parameters->second.size() != arguments.size()) return nullptr;

  std::unordered_map<std::string, std::string> substitutions;
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    substitutions.emplace(parameters->second[index], arguments[index]);
  }
  auto scalar_layout = [&](const std::string& type_name) -> std::pair<std::uint64_t, std::uint32_t> {
    if (const auto layout = type_layouts_.find(type_name); layout != type_layouts_.end())
      return {layout->second.size, layout->second.alignment};
    if (const auto enumeration = enum_types_.find(type_name); enumeration != enum_types_.end())
      return {enumeration->second.size, enumeration->second.alignment};
    if (type_name == "bool" || type_name == "i8" || type_name == "u8" || type_name == "byte") return {1, 1};
    if (type_name == "i16" || type_name == "u16" || type_name == "f16") return {2, 2};
    if (type_name == "i32" || type_name == "u32" || type_name == "f32" || type_name == "char") return {4, 4};
    return {8, 8};
  };

  HirEnum concrete;
  concrete.name = name;
  concrete.generic_arguments = arguments;
  concrete.concrete_instantiation = true;
  concrete.range = template_enum->second.range;
  std::uint64_t maximum_payload_size = 0;
  std::uint32_t maximum_payload_alignment = 1;
  for (const auto& source_variant : template_enum->second.variants) {
    HirEnumVariant variant;
    variant.name = source_variant.name;
    variant.discriminant = source_variant.discriminant;
    variant.range = source_variant.range;
    std::uint64_t payload_cursor = 0;
    for (const auto& source_payload : source_variant.payload_types) {
      const auto payload_type = substitute_generic_type(source_payload, substitutions);
      const auto [size, alignment] = scalar_layout(payload_type);
      payload_cursor = align_up(payload_cursor, alignment);
      variant.payload_offsets.push_back(payload_cursor);
      variant.payload_types.push_back(payload_type);
      payload_cursor += size;
      variant.payload_alignment = std::max(variant.payload_alignment, alignment);
    }
    variant.payload_size = align_up(payload_cursor, variant.payload_alignment);
    maximum_payload_size = std::max(maximum_payload_size, variant.payload_size);
    maximum_payload_alignment = std::max(maximum_payload_alignment, variant.payload_alignment);
    concrete.variants.push_back(std::move(variant));
  }
  concrete.payload_offset = align_up(4, maximum_payload_alignment);
  concrete.alignment = std::max<std::uint32_t>(4, maximum_payload_alignment);
  concrete.size = align_up(concrete.payload_offset + maximum_payload_size, concrete.alignment);
  request_instantiation(name);
  const auto [it, _] = enum_types_.emplace(name, std::move(concrete));
  return &it->second;
}

const Symbol* SemanticAnalyzer::lookup(const std::string& name) const {
  for (auto iterator = scopes_.rbegin(); iterator != scopes_.rend(); ++iterator) {
    const auto found = iterator->symbols.find(name);
    if (found != iterator->symbols.end()) return &found->second;
  }
  const auto found = globals_.find(name);
  return found == globals_.end() ? nullptr : &found->second;
}

bool SemanticAnalyzer::is_copy_type(const std::string& type_name) const {
  return implements_trait(type_name, "Copy");
}

bool SemanticAnalyzer::is_clone_type(const std::string& type_name) const {
  return parse_dynamic_trait_type(type_name).has_value() || implements_trait(type_name, "Clone");
}

bool SemanticAnalyzer::match_generic_trait_target(
    const GenericTraitImplementation& implementation, const std::string& concrete_type,
    std::unordered_map<std::string, std::string>& substitutions) const {
  const std::unordered_set<std::string> parameters(
      implementation.parameters.begin(), implementation.parameters.end());
  std::function<bool(const std::string&, const std::string&)> match =
      [&](const std::string& pattern, const std::string& concrete) -> bool {
    if (parameters.contains(pattern)) {
      const auto [it, inserted] = substitutions.emplace(pattern, concrete);
      return inserted || it->second == concrete;
    }
    const auto [pattern_base, pattern_arguments] = split_generic_name(pattern);
    const auto [concrete_base, concrete_arguments] = split_generic_name(concrete);
    if (!pattern_arguments.empty() || !concrete_arguments.empty()) {
      if (pattern_base != concrete_base || pattern_arguments.size() != concrete_arguments.size()) return false;
      for (std::size_t index = 0; index < pattern_arguments.size(); ++index) {
        if (!match(pattern_arguments[index], concrete_arguments[index])) return false;
      }
      return true;
    }
    return pattern == concrete;
  };
  if (!match(implementation.target_pattern, concrete_type)) return false;
  for (std::size_t index = 0; index < implementation.parameters.size(); ++index) {
    const auto found = substitutions.find(implementation.parameters[index]);
    if (found == substitutions.end()) return false;
    const bool const_parameter = index < implementation.const_types.size() &&
                                 !implementation.const_types[index].empty();
    if (const_parameter) {
      const auto value = evaluate_const_integer_expression(found->second,
          [&](std::string_view identifier) -> std::optional<std::int64_t> {
            const auto constant = constants_.find(std::string(identifier));
            if (constant == constants_.end()) return std::nullopt;
            try { return std::stoll(constant->second.second, nullptr, 0); } catch (...) { return std::nullopt; }
          });
      if (!value.has_value()) return false;
      continue;
    }

    for (const auto& bound : implementation.bounds[index]) {
      if (!implements_trait(found->second, bound)) return false;
    }
  }
  return true;
}

bool SemanticAnalyzer::generic_trait_patterns_overlap(
    const GenericTraitImplementation& left, const GenericTraitImplementation& right) const {
  if (left.trait_name != right.trait_name) return false;
  const std::unordered_set<std::string> left_parameters(left.parameters.begin(), left.parameters.end());
  const std::unordered_set<std::string> right_parameters(right.parameters.begin(), right.parameters.end());
  std::unordered_map<std::string, std::string> bindings;

  const auto variable_key = [&](const std::string& term, bool left_side) -> std::optional<std::string> {
    const auto& parameters = left_side ? left_parameters : right_parameters;
    if (!parameters.contains(term)) return std::nullopt;
    return std::string(left_side ? "L:" : "R:") + term;
  };
  std::function<std::string(std::string)> resolve = [&](std::string term) {
    std::unordered_set<std::string> seen;
    while (term.starts_with("L:") || term.starts_with("R:")) {
      if (!seen.insert(term).second) break;
      const auto found = bindings.find(term);
      if (found == bindings.end()) break;
      term = found->second;
    }
    return term;
  };
  std::function<bool(const std::string&, bool, const std::string&, bool)> unify =
      [&](const std::string& left_term, bool left_side,
          const std::string& right_term, bool right_side) -> bool {
    auto left_variable = variable_key(left_term, left_side);
    auto right_variable = variable_key(right_term, right_side);
    std::string lhs = left_variable ? resolve(*left_variable) : left_term;
    std::string rhs = right_variable ? resolve(*right_variable) : right_term;
    if (lhs == rhs) return true;
    const bool lhs_variable = lhs.starts_with("L:") || lhs.starts_with("R:");
    const bool rhs_variable = rhs.starts_with("L:") || rhs.starts_with("R:");
    if (lhs_variable) { bindings[lhs] = rhs; return true; }
    if (rhs_variable) { bindings[rhs] = lhs; return true; }
    const auto [lhs_base, lhs_arguments] = split_generic_name(lhs);
    const auto [rhs_base, rhs_arguments] = split_generic_name(rhs);
    if (!lhs_arguments.empty() || !rhs_arguments.empty()) {
      if (lhs_base != rhs_base || lhs_arguments.size() != rhs_arguments.size()) return false;
      for (std::size_t index = 0; index < lhs_arguments.size(); ++index) {
        if (!unify(lhs_arguments[index], left_side, rhs_arguments[index], right_side)) return false;
      }
      return true;
    }
    return false;
  };
  return unify(left.target_pattern, true, right.target_pattern, false);
}

bool SemanticAnalyzer::implements_trait(const std::string& type_name, const std::string& trait_name) const {
  const auto pair_key = trait_name + "::" + type_name;
  if (negative_trait_pairs_.contains(pair_key)) return false;

  if (const auto alias = trait_aliases_.find(trait_name); alias != trait_aliases_.end()) {
    return std::all_of(alias->second.begin(), alias->second.end(),
                       [&](const std::string& target) { return implements_trait(type_name, target); });
  }

  if (trait_name == "Copy") {
    if (parse_reference_type(type_name) || parse_slice_type(type_name)) return true;
    if (const auto tuple = parse_tuple_type(type_name)) {
      return std::all_of(tuple->element_types.begin(), tuple->element_types.end(),
                         [&](const std::string& element) { return implements_trait(element, "Copy"); });
    }

    if (parse_fixed_array_type(type_name).has_value()) return false;
    const auto builtin = builtin_type(type_name);
    if (builtin.valid()) return builtin.kind != TypeKind::string_type;
    if (explicit_copy_types_.contains(type_name)) return true;
    const auto enumeration = enum_types_.find(type_name);
    if (enumeration != enum_types_.end() &&
        std::all_of(enumeration->second.variants.begin(), enumeration->second.variants.end(),
                    [](const HirEnumVariant& variant) { return variant.payload_types.empty(); })) return true;
  } else if (trait_name == "Clone") {
    if (const auto callable = parse_callable_type(type_name)) return callable->kind != CallableKind::once;
    if (type_name.starts_with("Fn<") || type_name.starts_with("FnMut<")) return true;
    if (type_name.starts_with("FnOnce<")) return false;
    if (implements_trait(type_name, "Copy") || explicit_clone_types_.contains(type_name)) return true;
    if (const auto tuple = parse_tuple_type(type_name)) {
      return std::all_of(tuple->element_types.begin(), tuple->element_types.end(),
                         [&](const std::string& element) { return implements_trait(element, "Clone"); });
    }

    if (const auto array = parse_fixed_array_type(type_name)) return implements_trait(array->element_type, "Clone");
  } else if (trait_name == "Drop") {
    if (explicit_drop_types_.contains(type_name)) return true;
  } else {
    const auto found = trait_implementors_.find(trait_name);
    if (found != trait_implementors_.end() && found->second.contains(type_name)) return true;
  }

  if (split_generic_name(type_name).second.empty()) return false;
  for (const auto& implementation : generic_trait_implementations_) {
    if (implementation.trait_name != trait_name) continue;
    std::unordered_map<std::string, std::string> substitutions;
    if (match_generic_trait_target(implementation, type_name, substitutions)) return true;
  }
  return false;
}

std::optional<std::string> SemanticAnalyzer::resolve_associated_type_binding(
    const std::string& type_name, const std::string& trait_name,
    const std::string& item_name) const {
  const auto exact = associated_type_bindings_.find(
      type_name + "::" + trait_name + "::" + item_name);
  if (exact != associated_type_bindings_.end()) return exact->second;

  if (split_generic_name(type_name).second.empty()) return std::nullopt;
  for (const auto& implementation : generic_trait_implementations_) {
    if (implementation.trait_name != trait_name) continue;
    const auto binding = implementation.associated_type_bindings.find(item_name);
    if (binding == implementation.associated_type_bindings.end()) continue;
    std::unordered_map<std::string, std::string> substitutions;
    if (match_generic_trait_target(implementation, type_name, substitutions)) {
      return substitute_generic_type(binding->second, substitutions);
    }
  }
  return std::nullopt;
}

std::optional<std::pair<std::string, std::string>> SemanticAnalyzer::resolve_associated_const_binding(
    const std::string& type_name, const std::string& trait_name,
    const std::string& item_name) const {
  const auto exact = associated_const_bindings_.find(
      type_name + "::" + trait_name + "::" + item_name);
  if (exact != associated_const_bindings_.end()) return exact->second;

  if (split_generic_name(type_name).second.empty()) return std::nullopt;
  for (const auto& implementation : generic_trait_implementations_) {
    if (implementation.trait_name != trait_name) continue;
    const auto binding = implementation.associated_const_bindings.find(item_name);
    if (binding == implementation.associated_const_bindings.end()) continue;
    std::unordered_map<std::string, std::string> substitutions;
    if (match_generic_trait_target(implementation, type_name, substitutions)) {
      auto value = binding->second.second;
      for (const auto& [parameter, argument] : substitutions) {
        std::size_t position = 0;
        while ((position = value.find(parameter, position)) != std::string::npos) {
          const bool left_boundary = position == 0 ||
              !(std::isalnum(static_cast<unsigned char>(value[position - 1])) || value[position - 1] == '_');
          const auto finish = position + parameter.size();
          const bool right_boundary = finish == value.size() ||
              !(std::isalnum(static_cast<unsigned char>(value[finish])) || value[finish] == '_');
          if (left_boundary && right_boundary) {
            value.replace(position, parameter.size(), argument);
            position += argument.size();
          } else {
            position += parameter.size();
          }
        }
      }
      return std::pair<std::string, std::string>{
          substitute_generic_type(binding->second.first, substitutions), std::move(value)};
    }
  }
  return std::nullopt;
}

std::string SemanticAnalyzer::normalize_associated_type(std::string type_name) const {
  for (std::size_t iteration = 0; iteration < 16; ++iteration) {
    if (const auto exact = associated_type_bindings_.find(type_name);
        exact != associated_type_bindings_.end()) {
      if (exact->second == type_name) break;
      type_name = exact->second;
      continue;
    }
    const auto first = type_name.find("::");
    if (first == std::string::npos) break;
    const auto second = type_name.find("::", first + 2);
    if (second == std::string::npos) break;
    const auto target = type_name.substr(0, first);
    const auto trait = type_name.substr(first + 2, second - first - 2);
    const auto item = type_name.substr(second + 2);
    const auto resolved = resolve_associated_type_binding(target, trait, item);
    if (!resolved || *resolved == type_name) break;
    type_name = *resolved;
  }
  return type_name;
}
