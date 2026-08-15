// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

void SemanticAnalyzer::ensure_slice_layout(const std::string& type_name, SourceRange range) {
  const auto slice = parse_slice_type(type_name);
  if (!slice || type_layouts_.contains(type_name)) return;
  HirType type;
  type.name = type_name;
  type.range = range;
  const auto data_type = slice->element_type + (slice->mutable_slice ? "&mut" : "&");
  type.fields.push_back({"$data", data_type, 0, 8, 8, range});
  type.fields.push_back({"length", "usize", 8, 8, 8, range});
  type.size = 16;
  type.alignment = 8;
  type_layouts_[type_name] = type;
  structural_type_names_.insert(type_name);
}

void SemanticAnalyzer::ensure_tuple_layout(const std::string& type_name, SourceRange range) {
  const auto tuple = parse_tuple_type(type_name);
  if (!tuple || type_layouts_.contains(type_name)) return;
  HirType type;
  type.name = type_name;
  type.range = range;
  std::uint64_t offset = 0;
  std::uint32_t maximum_alignment = 1;
  const auto layout_of = [&](const std::string& element, const auto& self) -> std::pair<std::uint64_t, std::uint32_t> {
    if (parse_reference_type(element)) return {8, 8};
    if (const auto nested_tuple = parse_tuple_type(element)) {
      ensure_tuple_layout(element, range);
      const auto found = type_layouts_.find(element);
      if (found != type_layouts_.end()) return {found->second.size, found->second.alignment};
      (void)nested_tuple;
    }

    if (const auto array = parse_fixed_array_type(element)) {
      const auto nested = self(array->element_type, self);
      return {nested.first * array->length, nested.second};
    }

    if (const auto found = type_layouts_.find(element); found != type_layouts_.end())
      return {found->second.size, found->second.alignment};
    if (const auto found = enum_types_.find(element); found != enum_types_.end())
      return {found->second.size, found->second.alignment};
    if (element == "bool" || element == "i8" || element == "u8" || element == "byte") return {1, 1};
    if (element == "i16" || element == "u16" || element == "f16") return {2, 2};
    if (element == "i32" || element == "u32" || element == "f32" || element == "char") return {4, 4};
    return {8, 8};
  };
  for (std::size_t index = 0; index < tuple->element_types.size(); ++index) {
    const auto& element = tuple->element_types[index];
    const auto [size, alignment] = layout_of(element, layout_of);
    offset = align_up(offset, alignment);
    type.fields.push_back({std::to_string(index), element, offset, size, alignment, range});
    offset += size;
    maximum_alignment = std::max(maximum_alignment, alignment);
  }
  type.alignment = maximum_alignment;
  type.size = align_up(offset, maximum_alignment);
  type_layouts_[type_name] = type;
  structural_type_names_.insert(type_name);
}

void SemanticAnalyzer::request_instantiation(const std::string& type_name) {
  if (const auto slice = parse_slice_type(type_name)) {
    ensure_slice_layout(type_name);
    request_instantiation(slice->element_type);
    return;
  }

  if (const auto tuple = parse_tuple_type(type_name)) {
    ensure_tuple_layout(type_name);
    for (const auto& element : tuple->element_types) request_instantiation(element);
    return;
  }

  if (const auto array = parse_fixed_array_type(type_name)) {
    request_instantiation(array->element_type);
    return;
  }
  const auto [base, arguments] = split_generic_name(type_name);
  if (arguments.empty() || !generic_types_.contains(base)) return;
  requested_instantiations_.insert(type_name);
  const auto const_kinds = generic_const_types_.find(base);
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    const bool const_argument = const_kinds != generic_const_types_.end() &&
        index < const_kinds->second.size() && !const_kinds->second[index].empty();
    if (!const_argument) request_instantiation(arguments[index]);
  }
}

namespace {
std::string substitute_generic_type(const std::string& type_name,
                                    const std::unordered_map<std::string, std::string>& substitutions) {
  if (const auto direct = substitutions.find(type_name); direct != substitutions.end()) return direct->second;
  if (const auto projection = type_name.find("::"); projection != std::string::npos) {
    const auto base = type_name.substr(0, projection);
    if (const auto replacement = substitutions.find(base); replacement != substitutions.end()) {
      return replacement->second + type_name.substr(projection);
    }
  }

  if (const auto reference = parse_reference_type(type_name)) {
    auto result = substitute_generic_type(reference->referent_type, substitutions) + "&";
    if (!reference->lifetime.empty()) result += reference->lifetime;
    if (reference->mutable_reference) result += "mut";
    return result;
  }

  if (const auto pointer = parse_raw_pointer_type(type_name)) {
    return substitute_generic_type(pointer->pointee_type, substitutions) +
           (pointer->mutable_pointer ? "*mut" : "*const");
  }

  if (const auto slice = parse_slice_type(type_name)) {
    return substitute_generic_type(slice->element_type, substitutions) + "[]" +
           (slice->mutable_slice ? "mut" : "");
  }

  if (const auto array = parse_fixed_array_type(type_name)) {
    return substitute_generic_type(array->element_type, substitutions) + "[" + std::to_string(array->length) + "]";
  }

  if (const auto symbolic = split_symbolic_array_type(type_name)) {
    auto count = symbolic->second;
    if (const auto found = substitutions.find(count); found != substitutions.end()) count = found->second;
    if (const auto value = evaluate_const_integer_expression(count, [&](std::string_view identifier) -> std::optional<std::int64_t> {
          if (const auto found = substitutions.find(std::string(identifier)); found != substitutions.end()) {
            try { return std::stoll(found->second); } catch (...) { return std::nullopt; }
          }
          return std::nullopt;
        }); value && *value > 0) count = std::to_string(*value);
    return substitute_generic_type(symbolic->first, substitutions) + "[" + count + "]";
  }
  const auto [base, arguments] = split_generic_name(type_name);
  if (arguments.empty()) return type_name;
  std::string result = base + "<";
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    if (index != 0) result += ",";
    result += substitute_generic_type(arguments[index], substitutions);
  }
  result += ">";
  return result;
}
}

void SemanticAnalyzer::materialize_generic_instantiations(HirModule& module) {
  std::function<std::pair<std::uint64_t, std::uint32_t>(const std::string&)> scalar_layout;
  scalar_layout = [&](const std::string& type_name) -> std::pair<std::uint64_t, std::uint32_t> {
    if (const auto array = parse_fixed_array_type(type_name)) {
      const auto [element_size, element_alignment] = scalar_layout(array->element_type);
      return {element_size * array->length, element_alignment};
    }

    if (const auto concrete = type_layouts_.find(type_name); concrete != type_layouts_.end()) {
      return {concrete->second.size, concrete->second.alignment};
    }

    if (const auto enumeration = enum_types_.find(type_name); enumeration != enum_types_.end()) {
      return {enumeration->second.size, enumeration->second.alignment};
    }

    if (type_name == "bool" || type_name == "i8" || type_name == "u8" || type_name == "byte") return {1, 1};
    if (type_name == "i16" || type_name == "u16" || type_name == "f16") return {2, 2};
    if (type_name == "i32" || type_name == "u32" || type_name == "f32" || type_name == "char") return {4, 4};
    return {8, 8};
  };

  std::vector<std::string> pending(requested_instantiations_.begin(), requested_instantiations_.end());
  std::sort(pending.begin(), pending.end());
  for (const auto& concrete_name : pending) {
    // This routine may run more than once because generic method analysis can
    // discover additional concrete helper types. Keep it idempotent, but do not
    // confuse a semantic-cache entry with a HIR materialization. resolve_enum()
    // can create the concrete layout while analyzing `?`; MIR still needs that
    // concrete enum copied into module.enums.
    if (const auto concrete_type = type_layouts_.find(concrete_name); concrete_type != type_layouts_.end()) {
      if (concrete_type->second.concrete_instantiation &&
          std::none_of(module.types.begin(), module.types.end(), [&](const HirType& type) { return type.name == concrete_name; })) {
        module.types.push_back(concrete_type->second);
      }
      continue;
    }

    if (const auto concrete_enum = enum_types_.find(concrete_name); concrete_enum != enum_types_.end()) {
      if (concrete_enum->second.concrete_instantiation &&
          std::none_of(module.enums.begin(), module.enums.end(), [&](const HirEnum& enumeration) { return enumeration.name == concrete_name; })) {
        module.enums.push_back(concrete_enum->second);
      }
      continue;
    }
    const auto [base, arguments] = split_generic_name(concrete_name);
    const auto parameters = generic_types_.find(base);
    if (parameters == generic_types_.end() || parameters->second.size() != arguments.size()) continue;
    std::unordered_map<std::string, std::string> substitutions;
    const auto const_kinds = generic_const_types_.find(base);
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      auto argument = arguments[index];
      const bool const_argument = const_kinds != generic_const_types_.end() &&
          index < const_kinds->second.size() && !const_kinds->second[index].empty();
      if (const_argument) {
        if (const auto value = evaluate_const_integer_expression(argument, [&](std::string_view identifier) -> std::optional<std::int64_t> {
              if (const auto constant = constants_.find(std::string(identifier)); constant != constants_.end() &&
                  is_integral(constant->second.first)) {
                try { return std::stoll(constant->second.second); } catch (...) { return std::nullopt; }
              }
              return std::nullopt;
            }); value && *value > 0) argument = std::to_string(*value);
      }
      substitutions.emplace(parameters->second[index], argument);
    }

    if (const auto template_type = type_layouts_.find(base); template_type != type_layouts_.end()) {
      HirType concrete;
      concrete.name = concrete_name;
      concrete.generic_arguments = arguments;
      concrete.concrete_instantiation = true;
      concrete.range = template_type->second.range;
      std::uint64_t offset = 0;
      std::uint32_t max_alignment = 1;
      for (const auto& template_field : template_type->second.fields) {
        const auto field_type = substitute_generic_type(template_field.type_name, substitutions);
        const auto [size, alignment] = scalar_layout(field_type);
        offset = align_up(offset, alignment);
        concrete.fields.push_back({template_field.name, field_type, offset, size, alignment, template_field.range});
        offset += size;
        max_alignment = std::max(max_alignment, alignment);
      }
      concrete.alignment = max_alignment;
      concrete.size = align_up(offset, max_alignment);
      type_layouts_[concrete_name] = concrete;
      module.types.push_back(std::move(concrete));
      continue;
    }

    const auto template_enum = enum_types_.find(base);
    if (template_enum == enum_types_.end()) continue;
    HirEnum concrete;
    concrete.name = concrete_name;
    concrete.generic_arguments = arguments;
    concrete.concrete_instantiation = true;
    concrete.range = template_enum->second.range;
    std::uint64_t maximum_payload_size = 0;
    std::uint32_t maximum_payload_alignment = 1;
    for (const auto& template_variant : template_enum->second.variants) {
      HirEnumVariant variant;
      variant.name = template_variant.name;
      variant.discriminant = template_variant.discriminant;
      variant.range = template_variant.range;
      std::uint64_t payload_cursor = 0;
      for (const auto& template_payload : template_variant.payload_types) {
        const auto payload_type = substitute_generic_type(template_payload, substitutions);
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
    enum_types_[concrete_name] = concrete;
    module.enums.push_back(std::move(concrete));
  }
}

void SemanticAnalyzer::request_function_instantiation(const std::string& function_name) {
  const auto [base, arguments] = split_generic_name(function_name);
  if (arguments.empty()) return;
  const auto found = globals_.find(base);
  if (found == globals_.end() || found->second.kind != SymbolKind::function) return;
  if (found->second.generic_parameters.size() != arguments.size()) return;
  requested_function_instantiations_.insert(function_name);
}

void SemanticAnalyzer::materialize_generic_trait_implementations(HirModule& module) {
  // Generic methods can discover concrete helper types while they are being
  // specialized (for example HashMap<K,V>::entries() requests
  // HashMapEntryIter<K,V>). Interface serialization is free to reorder impl
  // blocks, so a single forward scan can miss a trait impl whose concrete type
  // is discovered by a later block. Process implementation/concrete pairs to a
  // fixed point; each pair is materialized at most once.
  std::unordered_set<std::string> processed_pairs;
  bool made_progress = true;
  while (made_progress) {
    made_progress = false;
    for (std::size_t implementation_index = 0;
         implementation_index < generic_trait_implementations_.size(); ++implementation_index) {
      const auto& implementation = generic_trait_implementations_[implementation_index];
      std::vector<std::string> concrete_types(requested_instantiations_.begin(), requested_instantiations_.end());
      std::sort(concrete_types.begin(), concrete_types.end());
      for (const auto& concrete : concrete_types) {
        std::unordered_map<std::string, std::string> substitutions;
        if (!match_generic_trait_target(implementation, concrete, substitutions)) continue;
        const auto pair_key = std::to_string(implementation_index) + "\n" + concrete;
        if (!processed_pairs.insert(pair_key).second) continue;
        made_progress = true;
      if (!implementation.trait_name.empty()) {
        if (!implements_trait(concrete, implementation.trait_name)) continue;
        bool supertraits_satisfied = true;
        if (const auto supers = trait_supertraits_.find(implementation.trait_name);
            supers != trait_supertraits_.end()) {
          for (const auto& supertrait : supers->second) {
            if (!implements_trait(concrete, supertrait)) {
              diagnostics_.error("D2114", implementation.range, "implementation of '" +
                                 implementation.trait_name + "' requires supertrait '" + supertrait +
                                 "' for '" + concrete + "'");
              supertraits_satisfied = false;
            }
          }
        }
        if (!supertraits_satisfied) continue;
        const auto already = std::any_of(module.trait_implementations.begin(), module.trait_implementations.end(),
            [&](const HirTraitImplementation& item) {
              return item.trait_name == implementation.trait_name && item.target_type == concrete;
            });
        if (!already) {
          module.trait_implementations.push_back(
              {implementation.trait_name, concrete, {}, {}, implementation.range});
        }
      }
      for (const auto& [name, binding] : implementation.associated_type_bindings) {
        const auto concrete_binding = normalize_associated_type(substitute_generic_type(binding, substitutions));
        const auto key = concrete + "::" + implementation.trait_name + "::" + name;
        associated_type_bindings_[key] = concrete_binding;
        const auto exists = std::any_of(module.associated_type_bindings.begin(), module.associated_type_bindings.end(),
            [&](const HirAssociatedTypeBinding& item) {
              return item.trait_name == implementation.trait_name && item.target_type == concrete && item.name == name;
            });
        if (!exists) module.associated_type_bindings.push_back(
            {implementation.trait_name, concrete, name, concrete_binding, implementation.range});
      }
      std::unordered_map<std::string, std::string> concrete_associated_constants;
      for (const auto& [name, binding] : implementation.associated_const_bindings) {
        const auto concrete_type = normalize_associated_type(substitute_generic_type(binding.first, substitutions));
        auto concrete_value = binding.second;
        for (const auto& [parameter, argument] : substitutions) {
          std::size_t position = 0;
          while ((position = concrete_value.find(parameter, position)) != std::string::npos) {
            const bool left_boundary = position == 0 ||
                !(std::isalnum(static_cast<unsigned char>(concrete_value[position - 1])) || concrete_value[position - 1] == '_');
            const auto end = position + parameter.size();
            const bool right_boundary = end == concrete_value.size() ||
                !(std::isalnum(static_cast<unsigned char>(concrete_value[end])) || concrete_value[end] == '_');
            if (left_boundary && right_boundary) {
              concrete_value.replace(position, parameter.size(), argument);
              position += argument.size();
            } else {
              position += parameter.size();
            }
          }
        }
        if (is_integral(concrete_type)) {
          if (const auto evaluated = evaluate_const_integer_expression(concrete_value); evaluated.has_value()) {
            concrete_value = std::to_string(*evaluated);
          } else {
            diagnostics_.error("D2113", implementation.range, "associated constant '" + name +
                               "' could not be evaluated for '" + concrete + "'");
          }
        }
        concrete_associated_constants.emplace(name, concrete_value);
        const auto key = concrete + "::" + implementation.trait_name + "::" + name;
        associated_const_bindings_[key] = {concrete_type, concrete_value};
        const auto exists = std::any_of(module.associated_const_bindings.begin(), module.associated_const_bindings.end(),
            [&](const HirAssociatedConstBinding& item) {
              return item.trait_name == implementation.trait_name && item.target_type == concrete && item.name == name;
            });
        if (!exists) module.associated_const_bindings.push_back(
            {implementation.trait_name, concrete, name, concrete_type, concrete_value, implementation.range});
      }
      const auto substitute_text = [&](std::string text) {
        for (const auto& [parameter, argument] : substitutions) {
          std::size_t position = 0;
          while ((position = text.find(parameter, position)) != std::string::npos) {
            const bool left_boundary = position == 0 ||
                !(std::isalnum(static_cast<unsigned char>(text[position - 1])) || text[position - 1] == '_');
            const auto end = position + parameter.size();
            const bool right_boundary = end == text.size() ||
                !(std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_');
            if (left_boundary && right_boundary) {
              text.replace(position, parameter.size(), argument);
              position += argument.size();
            } else {
              position += parameter.size();
            }
          }
        }
        std::size_t self_position = 0;
        while ((self_position = text.find("Self", self_position)) != std::string::npos) {
          const bool left_boundary = self_position == 0 ||
              !(std::isalnum(static_cast<unsigned char>(text[self_position - 1])) || text[self_position - 1] == '_');
          const auto end = self_position + 4;
          const bool right_boundary = end == text.size() ||
              !(std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_');
          if (left_boundary && right_boundary) {
            text.replace(self_position, 4, concrete);
            self_position += concrete.size();
          } else {
            self_position += 4;
          }
        }
        for (const auto& [name, binding] : implementation.associated_type_bindings) {
          const auto resolved = normalize_associated_type(substitute_generic_type(binding, substitutions));
          std::size_t position = 0;
          while ((position = text.find(name, position)) != std::string::npos) {
            const bool left_boundary = position == 0 ||
                !(std::isalnum(static_cast<unsigned char>(text[position - 1])) || text[position - 1] == '_');
            const auto end = position + name.size();
            const bool right_boundary = end == text.size() ||
                !(std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_');
            if (left_boundary && right_boundary) {
              text.replace(position, name.size(), resolved);
              position += resolved.size();
            } else {
              position += name.size();
            }
          }
        }
        for (const auto& [name, value] : concrete_associated_constants) {
          std::size_t position = 0;
          while ((position = text.find(name, position)) != std::string::npos) {
            const bool left_boundary = position == 0 ||
                !(std::isalnum(static_cast<unsigned char>(text[position - 1])) || text[position - 1] == '_');
            const auto end = position + name.size();
            const bool right_boundary = end == text.size() ||
                !(std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_');
            if (left_boundary && right_boundary) {
              text.replace(position, name.size(), value);
              position += value.size();
            } else {
              position += name.size();
            }
          }
        }
        return text;
      };
      std::function<void(SyntaxNode&)> specialize_node = [&](SyntaxNode& item) {
        if (item.kind == SyntaxKind::name_expression) {
          if (const auto constant = concrete_associated_constants.find(item.label);
              constant != concrete_associated_constants.end()) {
            item.kind = SyntaxKind::literal_expression;
            item.label = constant->second;
          } else if (const auto substitution = substitutions.find(item.label);
                     substitution != substitutions.end()) {
            const auto parameter = std::find(implementation.parameters.begin(), implementation.parameters.end(), item.label);
            const auto parameter_index = parameter == implementation.parameters.end()
                ? implementation.parameters.size()
                : static_cast<std::size_t>(parameter - implementation.parameters.begin());
            const bool const_parameter = parameter_index < implementation.const_types.size() &&
                                         !implementation.const_types[parameter_index].empty();
            if (const_parameter) {
              item.kind = SyntaxKind::literal_expression;
              item.label = substitution->second;
            } else {
              item.label = substitute_text(item.label);
            }
          } else {
            item.label = substitute_text(item.label);
          }
        } else {
          item.label = substitute_text(item.label);
        }
        for (auto& child : item.children) specialize_node(child);
      };
      if (implementation.trait_name.empty()) {
        for (const auto& [method_name, source_method] : implementation.methods) {
          SyntaxNode generated = source_method;
          specialize_node(generated);
          const auto generated_name = "__raz_inherent_" + concrete + "_" + method_name;
          const auto concrete_return = normalize_associated_type(function_return_type(generated.label));
          generated.label = generated_name + (concrete_return == "void" ? "" : " -> " + concrete_return);
          Symbol symbol;
          symbol.kind = SymbolKind::function;
          symbol.name = generated_name;
          symbol.type_name = concrete_return;
          symbol.declaration = generated.range;
          TraitMethodContract contract;
          contract.name = method_name;
          contract.return_type = concrete_return;
          contract.range = generated.range;
          const auto attributes = split_attributes(generated.modifier);
          contract.attributes.insert(attributes.begin(), attributes.end());
          bool has_receiver = false;
          for (auto& parameter : generated.children) {
            if (parameter.kind != SyntaxKind::parameter) continue;
            auto [parameter_type, parameter_name] = split_typed_name(parameter.label);
            parameter_type = normalize_associated_type(parameter_type);
            parameter.label = parameter_type + " " + parameter_name;
            symbol.parameter_types.push_back(parameter_type);
            contract.parameter_types.push_back(parameter_type);
            if (contract.parameter_types.size() == 1 && parameter_name == "self") {
              auto receiver = parameter_type;
              if (const auto reference = parse_reference_type(receiver)) receiver = reference->referent_type;
              if (receiver != concrete) {
                diagnostics_.error("D2129", parameter.range,
                                   "generic inherent method receiver must resolve to '" + concrete + "'");
              } else {
                has_receiver = true;
              }
            }
          }
          if (!declare(symbol)) {
            diagnostics_.error("D2130", generated.range,
                               "duplicate generated inherent function '" + generated_name + "'");
            continue;
          }
          analyze_function(generated, module);
          const auto key = concrete + "::" + method_name;
          inherent_method_contracts_[key] = contract;
          trait_method_functions_[key] = generated_name;
          if (!has_receiver) inherent_associated_functions_.insert(key);
          const auto exists = std::any_of(module.trait_implementations.begin(), module.trait_implementations.end(),
              [&](const HirTraitImplementation& item) {
                return item.trait_name.empty() && item.target_type == concrete &&
                       item.method_name == method_name && !item.function_name.empty();
              });
          if (!exists) module.trait_implementations.push_back(
              {"", concrete, method_name, generated_name, implementation.range});
        }
        continue;
      }
      if (implementation.trait_name == "Clone") {
        const auto source = implementation.methods.find("clone");
        if (source == implementation.methods.end()) continue;
        SyntaxNode generated = source->second;
        specialize_node(generated);
        const auto generated_name = "__raz_clone_" + concrete + "_clone";
        const auto concrete_return = normalize_associated_type(function_return_type(generated.label));
        generated.label = generated_name + " -> " + concrete_return;
        Symbol symbol;
        symbol.kind = SymbolKind::function;
        symbol.name = generated_name;
        symbol.type_name = concrete_return;
        symbol.declaration = generated.range;
        for (auto& parameter : generated.children) {
          if (parameter.kind != SyntaxKind::parameter) continue;
          auto [parameter_type, parameter_name] = split_typed_name(parameter.label);
          parameter_type = normalize_associated_type(parameter_type);
          parameter.label = parameter_type + " " + parameter_name;
          symbol.parameter_types.push_back(parameter_type);
        }
        if (!declare(symbol)) {
          diagnostics_.error("D2270", generated.range,
                             "duplicate generated generic clone function '" + generated_name + "'");
          continue;
        }
        analyze_function(generated, module);
        const auto method_exists = std::any_of(module.trait_implementations.begin(), module.trait_implementations.end(),
            [&](const HirTraitImplementation& item) {
              return item.trait_name == "Clone" && item.target_type == concrete && item.method_name == "clone";
            });
        if (!method_exists) {
          module.trait_implementations.push_back(
              {"Clone", concrete, "clone", generated_name, implementation.range});
        }
        continue;
      }
      if (implementation.trait_name == "Drop") {
        const auto source = implementation.methods.find("drop");
        if (source == implementation.methods.end()) continue;
        SyntaxNode generated = source->second;
        specialize_node(generated);
        const auto generated_name = "__raz_drop_" + concrete + "_drop";
        generated.label = generated_name;
        Symbol symbol;
        symbol.kind = SymbolKind::function;
        symbol.name = generated_name;
        symbol.type_name = "void";
        symbol.declaration = generated.range;
        for (auto& parameter : generated.children) {
          if (parameter.kind != SyntaxKind::parameter) continue;
          auto [parameter_type, parameter_name] = split_typed_name(parameter.label);
          parameter_type = normalize_associated_type(parameter_type);
          parameter.label = parameter_type + " " + parameter_name;
          symbol.parameter_types.push_back(parameter_type);
        }
        if (!declare(symbol)) {
          diagnostics_.error("D2269", generated.range,
                             "duplicate generated generic drop function '" + generated_name + "'");
          continue;
        }
        analyze_function(generated, module);
        const auto method_exists = std::any_of(module.trait_implementations.begin(), module.trait_implementations.end(),
            [&](const HirTraitImplementation& item) {
              return item.trait_name == "Drop" && item.target_type == concrete && item.method_name == "drop";
            });
        if (!method_exists) {
          module.trait_implementations.push_back(
              {"Drop", concrete, "drop", generated_name, implementation.range});
        }
        continue;
      }
      const auto contract_it = trait_contracts_.find(implementation.trait_name);
      if (contract_it == trait_contracts_.end()) continue;
      for (const auto& contract : contract_it->second) {
        const SyntaxNode* source_method = nullptr;
        if (const auto found = implementation.methods.find(contract.name);
            found != implementation.methods.end()) {
          source_method = &found->second;
        } else if (const auto defaults = trait_default_methods_.find(implementation.trait_name);
                   defaults != trait_default_methods_.end()) {
          if (const auto default_method = defaults->second.find(contract.name);
              default_method != defaults->second.end()) source_method = &default_method->second;
        }
        if (source_method == nullptr) continue;
        SyntaxNode generated = *source_method;
        specialize_node(generated);
        const auto generated_name = "__raz_trait_" + implementation.trait_name + "_" + concrete + "_" + contract.name;
        auto concrete_return = normalize_associated_type(function_return_type(generated.label));
        generated.label = generated_name + (concrete_return == "void" ? "" : " -> " + concrete_return);
        Symbol symbol;
        symbol.kind = SymbolKind::function;
        symbol.name = generated_name;
        symbol.type_name = concrete_return;
        symbol.declaration = generated.range;
        for (auto& parameter : generated.children) {
          if (parameter.kind != SyntaxKind::parameter) continue;
          auto [parameter_type, parameter_name] = split_typed_name(parameter.label);
          parameter_type = normalize_associated_type(parameter_type);
          parameter.label = parameter_type + " " + parameter_name;
          symbol.parameter_types.push_back(parameter_type);
        }
        if (!declare(symbol)) {
          diagnostics_.error("D2098", generated.range, "duplicate generated trait method '" + generated_name + "'");
          continue;
        }
        analyze_function(generated, module);
        trait_method_functions_[concrete + "::" + contract.name] = generated_name;
        const auto method_exists = std::any_of(module.trait_implementations.begin(), module.trait_implementations.end(),
            [&](const HirTraitImplementation& item) {
              return item.trait_name == implementation.trait_name && item.target_type == concrete &&
                     item.method_name == contract.name;
            });
        if (!method_exists) module.trait_implementations.push_back(
            {implementation.trait_name, concrete, contract.name, generated_name, implementation.range});
      }
      }
    }
  }
}

void SemanticAnalyzer::materialize_generic_functions(HirModule& module) {
  std::vector<std::string> pending(requested_function_instantiations_.begin(),
                                   requested_function_instantiations_.end());
  std::sort(pending.begin(), pending.end());
  const auto templates = module.functions;
  for (const auto& concrete_name : pending) {
    const auto [base, arguments] = split_generic_name(concrete_name);
    const auto template_function = std::find_if(
        templates.begin(), templates.end(), [&](const HirFunction& function) {
          return function.name == base && function.generic_template;
        });
    if (template_function == templates.end() ||
        template_function->generic_parameters.size() != arguments.size()) {
      continue;
    }
    std::unordered_map<std::string, std::string> substitutions;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      auto argument = arguments[index];
      const bool const_argument = index < template_function->generic_const_types.size() &&
          !template_function->generic_const_types[index].empty();
      if (const_argument) {
        if (const auto value = evaluate_const_integer_expression(argument, [&](std::string_view identifier) -> std::optional<std::int64_t> {
              if (const auto constant = constants_.find(std::string(identifier)); constant != constants_.end() &&
                  is_integral(constant->second.first)) {
                try { return std::stoll(constant->second.second); } catch (...) { return std::nullopt; }
              }
              return std::nullopt;
            }); value && *value > 0) argument = std::to_string(*value);
      }
      substitutions.emplace(template_function->generic_parameters[index], argument);
    }
    const auto resolve_projection = [&](std::string type) {
      return normalize_associated_type(substitute_generic_type(type, substitutions));
    };
    HirFunction concrete = *template_function;
    concrete.name = concrete_name;
    concrete.generic_arguments = arguments;
    concrete.generic_parameters.clear();
    concrete.generic_template = false;
    concrete.concrete_instantiation = true;
    concrete.return_type = resolve_projection(concrete.return_type);
    for (auto& parameter : concrete.parameters) {
      parameter.type_name = resolve_projection(parameter.type_name);
    }

    for (auto& local : concrete.locals) {
      local.type_name = resolve_projection(local.type_name);
    }
    const auto substitute_label = [&](std::string text) {
      for (const auto& [parameter, argument] : substitutions) {
        std::size_t position = 0;
        while ((position = text.find(parameter, position)) != std::string::npos) {
          const bool left_boundary = position == 0 ||
              !(std::isalnum(static_cast<unsigned char>(text[position - 1])) || text[position - 1] == '_');
          const auto finish = position + parameter.size();
          const bool right_boundary = finish == text.size() ||
              !(std::isalnum(static_cast<unsigned char>(text[finish])) || text[finish] == '_');
          if (left_boundary && right_boundary) {
            text.replace(position, parameter.size(), argument);
            position += argument.size();
          } else {
            position += parameter.size();
          }
        }
      }
      return text;
    };
    std::function<void(SyntaxNode&)> substitute_syntax = [&](SyntaxNode& node) {
      if (node.kind == SyntaxKind::variable_declaration || node.kind == SyntaxKind::parameter ||
          node.kind == SyntaxKind::field_declaration) {
        const auto [type_name, value_name] = split_typed_name(node.label);
        node.label = resolve_projection(type_name) + " " + value_name;
      } else {
        node.label = substitute_label(node.label);
      }
      for (auto& child : node.children) substitute_syntax(child);
    };
    if (concrete.body.has_value()) substitute_syntax(*concrete.body);
    module.functions.push_back(std::move(concrete));
  }
}
