// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

void SemanticAnalyzer::analyze_statement(const SyntaxNode& node, HirFunction& function,
                                         const std::string& return_type) {
  if (node.kind == SyntaxKind::block_statement) {
    push_scope();
    for (std::size_t index = 0; index < node.children.size(); ++index) {
      analyze_statement(node.children[index], function, return_type);
      release_dead_references(node.children, index);
    }

    pop_scope();
    return;
  }

  if (node.kind == SyntaxKind::unsafe_statement) {
    ++unsafe_depth_;
    if (!node.children.empty()) analyze_statement(node.children.front(), function, return_type);
    --unsafe_depth_;
    return;
  }

  if (node.kind == SyntaxKind::comptime_statement) { analyze_comptime(node); return; }
  if (node.kind == SyntaxKind::const_declaration) {
    const auto [declared_type, name] = split_typed_name(node.label);
    if (!type_exists(declared_type)) diagnostics_.error("D2002", node.range, "unknown constant type '" + declared_type + "'");
    if (node.children.empty()) { diagnostics_.error("D2208", node.range, "constant requires an initializer"); return; }
    auto value = evaluate_constant(node.children.front());
    if (!value.first.empty() && value.first != declared_type && !(is_numeric(value.first) && is_numeric(declared_type))) diagnostics_.error("D2209", node.range, "constant type mismatch");
    if (!declare(Symbol{SymbolKind::constant, name, declared_type, {}, {}, node.range, false, {}, {}, {}, {}, {}})) diagnostics_.error("D2004", node.range, "duplicate local declaration '" + name + "'");
    constants_[name] = {declared_type, value.second};
    return;
  }

  if (node.kind == SyntaxKind::variable_declaration) {
    auto [type_name, name] = split_typed_name(node.label);
    std::string inferred_initializer_type;
    if (type_name == "auto") {
      if (node.children.empty()) {
        diagnostics_.error("D2121", node.range, "auto variable '" + name + "' requires an initializer");
      } else {
        inferred_initializer_type = analyze_expression(node.children.front());
        if (inferred_initializer_type.empty()) {
          diagnostics_.error("D2122", node.children.front().range,
                             "cannot infer the type of auto variable '" + name + "'");
        } else {
          type_name = inferred_initializer_type;
        }
      }
    }

    request_instantiation(type_name);
    const bool closure_type =
        ((type_name.rfind("Fn<", 0) == 0) || (type_name.rfind("FnMut<", 0) == 0) ||
         (type_name.rfind("FnOnce<", 0) == 0)) && !type_name.empty() && type_name.back() == '>';
    if (!closure_type && !type_exists(type_name)) diagnostics_.error("D2002", node.range, "unknown variable type '" + type_name + "'");
    if (!declare(Symbol{SymbolKind::variable, name, type_name, {}, {}, node.range, true, {}, {}, {}, {}, {}})) {
      diagnostics_.error("D2004", node.range, "duplicate local declaration '" + name + "'");
    }

    if (!node.children.empty()) {
      const auto& initializer_node = node.children.front();
      if (initializer_node.kind == SyntaxKind::array_expression) {
        const auto array = parse_fixed_array_type(type_name);
        if (!array) {
          diagnostics_.error("D2025", initializer_node.range, "array literal requires a fixed-array destination");
        } else {
          if (initializer_node.children.size() != array->length) {
            diagnostics_.error("D2026", initializer_node.range, "array initializer for '" + type_name +
                               "' requires " + std::to_string(array->length) + " element(s), received " +
                               std::to_string(initializer_node.children.size()));
          }
          for (const auto& element : initializer_node.children) {
            const auto actual = analyze_expression(element);
            if (!actual.empty() && actual != array->element_type &&
                !(is_numeric(actual) && is_numeric(array->element_type))) {
              diagnostics_.error("D2027", element.range, "array element expects '" + array->element_type +
                                 "', received '" + actual + "'");
            }
          }
        }
      } else {
        const auto initializer = inferred_initializer_type.empty()
            ? analyze_expression(initializer_node) : inferred_initializer_type;
        if (initializer_node.kind == SyntaxKind::closure_expression) {
          const auto closure_name = closure_function_name(initializer_node);
          if (initializer_node.modifier == "move") {
            for (const auto& capture : closure_captures_[closure_name]) mark_moved(capture, initializer_node.range);
          } else if (initializer_node.modifier == "ref" || initializer_node.modifier == "mut") {
            const bool mutable_borrow = initializer_node.modifier == "mut";
            for (const auto& capture : closure_captures_[closure_name]) {
              const auto binding = name + ".capture." + capture;
              begin_borrow(capture, mutable_borrow, binding, initializer_node.range);
              if (!declared_names_.empty()) declared_names_.back().push_back(binding);
            }
          }
        }
        if (parse_reference_type(type_name) &&
            initializer_node.kind == SyntaxKind::unary_expression &&
            (initializer_node.label == "&" || initializer_node.label == "&mut") &&
            !initializer_node.children.empty()) {
          const bool mutable_borrow = parse_reference_type(type_name)->mutable_reference;
          if (!begin_reborrow(initializer_node.children.front(), mutable_borrow, name, initializer_node.range)) {
            const auto place = place_path(initializer_node.children.front());
            if (!place.empty()) begin_borrow(place, mutable_borrow, name, initializer_node.range);
          }
        } else if (const auto slice = parse_slice_type(type_name);
                   slice && initializer_node.kind == SyntaxKind::unary_expression &&
                   (initializer_node.label == "&" || initializer_node.label == "&mut") &&
                   !initializer_node.children.empty()) {
          const auto place = place_path(initializer_node.children.front());
          if (!place.empty()) begin_borrow(place, slice->mutable_slice, name, initializer_node.range);
        }
        bool dynamic_trait_compatible = false;
        if (const auto dynamic_trait = parse_dynamic_trait_type(type_name)) {
          if (initializer == type_name) {
            dynamic_trait_compatible = true;
          } else if (const auto reference = parse_reference_type(initializer)) {
            dynamic_trait_compatible = implements_trait(reference->referent_type, dynamic_trait->trait_name);
            if (!dynamic_trait_compatible) {
              diagnostics_.error("D2270", initializer_node.range, "type '" + reference->referent_type +
                                 "' does not implement object trait '" + dynamic_trait->trait_name + "'");
            }
          } else if (!initializer.empty()) {
            diagnostics_.error("D2271", initializer_node.range,
                               "dynamic trait object initialization requires a reference value");
          }
        }
        if (!initializer.empty() && type_exists(type_name) && initializer != type_name &&
            !dynamic_trait_compatible &&
            !closure_coerces_to_function(initializer_node, type_name) &&
            !closure_coerces_to_callable(initializer_node, type_name) &&
            !callable_identity_compatible(initializer, type_name) &&
            !reference_types_compatible(initializer, type_name) &&
            !slice_types_compatible(initializer, type_name) &&
            !implicit_numeric_family_compatible(initializer, type_name)) {
          diagnostics_.error("D2005", initializer_node.range,
                             "cannot initialize '" + type_name + "' with '" + initializer + "'");
        }
      }
    }

    if (!node.children.empty()) {
      const auto origin = aggregate_local_reference_origin(node.children.front());
      if (!origin.empty()) aggregate_reference_origins_[name] = origin;
      else aggregate_reference_origins_.erase(name);
    }
    function.locals.push_back({name, type_name, node.range});
    return;
  }

  if (node.kind == SyntaxKind::defer_statement) {
    if (node.children.empty()) {
      diagnostics_.error("D2049", node.range, "defer requires a statement");
      return;
    }

    if (contains_deferred_control_transfer(node.children.front())) {
      diagnostics_.error("D2050", node.range, "deferred code cannot contain return, break, or continue");
      return;
    }

    analyze_statement(node.children.front(), function, return_type);
    return;
  }

  if (node.kind == SyntaxKind::return_statement) {
    const auto actual = node.children.empty() ? "void" : analyze_expression(node.children.front());
    if (!node.children.empty() && node.children.front().kind == SyntaxKind::closure_expression &&
        (node.children.front().modifier == "ref" || node.children.front().modifier == "mut")) {
      diagnostics_.error("D2126", node.children.front().range,
                         "a closure borrowing local state cannot escape its defining function; use move capture for an owned escaping closure");
    }

    if (!node.children.empty() && parse_reference_type(actual)) {
      const auto origin = reference_origin(node.children.front());
      if (origin.empty()) {
        // Safe code may only return references whose lexical lifetime origin can be
        // proven. Unsafe code is the explicit escape hatch for references formed
        // from raw-pointer provenance (for example typed collection storage).
        if (unsafe_depth_ == 0) {
          diagnostics_.error("D2066", node.children.front().range,
                             "cannot prove the lifetime origin of the returned reference");
        }
      } else {
        const auto* owner = lookup(root_name(origin));
        if (owner != nullptr && owner->kind == SymbolKind::variable) {
          diagnostics_.error("D2064", node.children.front().range,
                             "cannot return a reference to local storage '" + root_name(origin) + "'");
        } else if (owner != nullptr && owner->kind == SymbolKind::parameter) {
          current_reference_return_origins_.insert(root_name(origin));
        }
      }
    }

    if (!node.children.empty() && !parse_reference_type(actual)) {
      const auto escaped_origin = aggregate_local_reference_origin(node.children.front());
      if (!escaped_origin.empty()) {
        diagnostics_.error(
            "D2064", node.children.front().range,
            "cannot return an aggregate containing a reference to local storage '" +
                root_name(escaped_origin) + "'");
      }
    }

    if (!actual.empty() && actual != return_type &&
        !(node.children.size() == 1 && (closure_coerces_to_function(node.children.front(), return_type) ||
                                         closure_coerces_to_callable(node.children.front(), return_type) ||
                                         callable_identity_compatible(actual, return_type))) &&
        !reference_types_compatible(actual, return_type) &&
        !implicit_numeric_family_compatible(actual, return_type)) {
      diagnostics_.error("D2006", node.range, "return type '" + actual + "' does not match '" + return_type + "'");
    }
    return;
  }

  if (node.kind == SyntaxKind::if_statement) {
    if (!node.children.empty()) {
      const auto condition = analyze_expression(node.children.front());
      if (!condition.empty() && condition != "bool")
        diagnostics_.error("D2007", node.children.front().range, "condition must have type 'bool'");
    }

    const auto entry = ownership_snapshot();
    std::vector<std::unordered_set<std::string>> branch_states;
    if (node.children.size() >= 2) {
      restore_ownership(entry);
      analyze_statement(node.children[1], function, return_type);
      branch_states.push_back(moved_values_);
    }

    if (node.children.size() >= 3) {
      restore_ownership(entry);
      analyze_statement(node.children[2], function, return_type);
      branch_states.push_back(moved_values_);
    } else {
      branch_states.push_back(entry.moved_values);
    }

    restore_ownership(entry);
    moved_values_ = union_moved_states(branch_states);
    return;
  }

  if (node.kind == SyntaxKind::while_statement) {
    if (!node.children.empty()) {
      const auto condition = analyze_expression(node.children.front());
      if (!condition.empty() && condition != "bool")
        diagnostics_.error("D2007", node.children.front().range, "condition must have type 'bool'");
    }

    const auto entry = ownership_snapshot();
    if (node.children.size() >= 2) {
      analyze_statement(node.children[1], function, return_type);
      const auto body_state = moved_values_;
      restore_ownership(entry);
      moved_values_ = union_moved_states({entry.moved_values, body_state});
    }
    return;
  }

  if (node.kind == SyntaxKind::for_statement) {
    if (node.label.empty() || node.children.size() < 2) {
      diagnostics_.error("D2113", node.range, "for loop requires 'name in iterable' syntax");
      return;
    }
    const auto iterable_type = analyze_expression(node.children.front());
    const auto iterable_reference = parse_reference_type(iterable_type);
    const auto array = parse_fixed_array_type(
        iterable_reference ? iterable_reference->referent_type : iterable_type);
    std::string element_type;
    const auto trait_short_name_matches = [](std::string_view full, std::string_view short_name) {
      if (full == short_name) return true;
      if (full.size() > short_name.size() + 2 && full.ends_with(std::string("__") + std::string(short_name))) return true;
      if (full.size() > short_name.size() + 2 && full.ends_with(std::string("::") + std::string(short_name))) return true;
      return false;
    };
    const auto resolve_protocol_trait = [&](std::string_view short_name) -> std::string {
      if (trait_contracts_.contains(std::string(short_name))) return std::string(short_name);
      std::string found;
      for (const auto& [trait_name, ignored] : trait_contracts_) {
        if (!trait_short_name_matches(trait_name, short_name)) continue;
        if (!found.empty() && found != trait_name) return {};
        found = trait_name;
      }
      return found;
    };
    const auto bound_has_protocol = [&](const std::vector<std::string>& bounds, std::string_view short_name) {
      return std::any_of(bounds.begin(), bounds.end(), [&](const std::string& bound) {
        return trait_short_name_matches(bound, short_name);
      });
    };
    const auto iterator_trait = resolve_protocol_trait("Iterator");
    const auto into_iterator_trait = resolve_protocol_trait("IntoIterator");
    if (array) {
      element_type = array->element_type;
      if (iterable_reference) {
        element_type += iterable_reference->mutable_reference ? "&mut" : "&";
      }
    } else if (iterable_type.starts_with("Range<") && iterable_type.ends_with(">")) {
      element_type = iterable_type.substr(6, iterable_type.size() - 7);
    } else {
      auto generic_iterable = iterable_type;
      if (const auto reference = parse_reference_type(generic_iterable)) generic_iterable = reference->referent_type;
      const auto generic_bounds = current_generic_bounds_.find(generic_iterable);
      if (generic_bounds != current_generic_bounds_.end() &&
          bound_has_protocol(generic_bounds->second, "Iterator")) {
        element_type = generic_iterable + "::Iterator::Item";
      } else if (generic_bounds != current_generic_bounds_.end() &&
                 bound_has_protocol(generic_bounds->second, "IntoIterator")) {
        element_type = generic_iterable + "::IntoIterator::Item";
      } else if (!iterator_trait.empty() && implements_trait(iterable_type, iterator_trait)) {
      const auto binding = resolve_associated_type_binding(iterable_type, iterator_trait, "Item");
      if (!binding) {
        diagnostics_.error("D2115", node.children.front().range,
                           "Iterator implementation for '" + iterable_type + "' does not bind Item");
        return;
      }
      element_type = normalize_associated_type(*binding);
    } else if (!into_iterator_trait.empty() && implements_trait(iterable_type, into_iterator_trait)) {
      const auto iterator_binding = resolve_associated_type_binding(iterable_type, into_iterator_trait, "IntoIter");
      const auto item_binding = resolve_associated_type_binding(iterable_type, into_iterator_trait, "Item");
      if (!iterator_binding || !item_binding) {
        diagnostics_.error("D2117", node.children.front().range,
                           "IntoIterator implementation for '" + iterable_type + "' must bind Item and IntoIter");
        return;
      }
      const auto iterator_type = normalize_associated_type(*iterator_binding);
      if (iterator_trait.empty() || !implements_trait(iterator_type, iterator_trait)) {
        diagnostics_.error("D2118", node.children.front().range,
                           "IntoIter type '" + iterator_type + "' does not implement Iterator");
        return;
      }
      const auto iterator_item = resolve_associated_type_binding(iterator_type, iterator_trait, "Item");
      if (!iterator_item || normalize_associated_type(*iterator_item) != normalize_associated_type(*item_binding)) {
        diagnostics_.error("D2119", node.children.front().range,
                           "IntoIterator Item does not match IntoIter::Item for '" + iterable_type + "'");
        return;
      }
      element_type = normalize_associated_type(*item_binding);
      } else {
        diagnostics_.error("D2114", node.children.front().range,
                           "for iteration requires a fixed array, integer range, Iterator, IntoIterator, or a matching generic bound; received '" + iterable_type + "'");
        return;
      }
    }
    const auto entry = ownership_snapshot();
    push_scope();
    if (iterable_reference && array && node.children.front().kind == SyntaxKind::unary_expression &&
        !node.children.front().children.empty()) {
      const auto hidden_binding = "__for_borrow_" + std::to_string(node.range.begin.offset);
      if (declare(Symbol{SymbolKind::variable, hidden_binding, iterable_type, {}, {}, node.range,
                         false, {}, {}, {}, {}, {}})) {
        const auto place = place_path(node.children.front().children.front());
        if (!place.empty()) {
          begin_borrow(place, iterable_reference->mutable_reference, hidden_binding, node.range);
        }
      }
    }

    if (!declare(Symbol{SymbolKind::variable, node.label, element_type, {}, {}, node.range, false, {}, {}, {}, {}, {}})) {
      diagnostics_.error("D2004", node.range, "duplicate loop binding '" + node.label + "'");
    }

    analyze_statement(node.children[1], function, return_type);
    const auto body_state = moved_values_;
    pop_scope();
    restore_ownership(entry);
    moved_values_ = union_moved_states({entry.moved_values, body_state});
    return;
  }

  if (node.kind == SyntaxKind::match_statement) {
    if (node.children.empty()) return;
    const auto subject_type = analyze_expression(node.children.front());
    const auto subject_place = place_path(node.children.front());
    const HirEnum* enumeration = resolve_enum(subject_type);
    if (enumeration == nullptr) {
      diagnostics_.error("D2034", node.children.front().range, "match currently requires an enum value");
    }
    std::unordered_map<std::string, bool> covered;
    bool wildcard = false;
    const auto entry = ownership_snapshot();
    std::vector<std::unordered_set<std::string>> arm_states;
    for (std::size_t index = 1; index < node.children.size(); ++index) {
      const auto& arm = node.children[index];
      if (arm.kind != SyntaxKind::match_arm) continue;
      restore_ownership(entry);
      push_scope();
      if (arm.label == "_") {
        if (wildcard) diagnostics_.error("D2035", arm.range, "duplicate wildcard match arm");
        wildcard = true;
      } else if (!arm.children.empty()) {
        const auto& pattern = arm.children.front();
        std::string variant_name;
        const SyntaxNode* member = nullptr;
        const SyntaxNode* binding_call = nullptr;
        if (pattern.kind == SyntaxKind::member_expression) {
          variant_name = pattern.label;
          member = &pattern;
        } else if (pattern.kind == SyntaxKind::call_expression && !pattern.children.empty() &&
                   pattern.children.front().kind == SyntaxKind::member_expression) {
          variant_name = pattern.children.front().label;
          member = &pattern.children.front();
          binding_call = &pattern;
        }
        if (member != nullptr && enumeration != nullptr) {
          if (!member->children.empty() && member->children.front().kind == SyntaxKind::name_expression &&
              member->children.front().label != subject_type) {
            diagnostics_.error("D2036", member->range, "match pattern type does not match '" + subject_type + "'");
          }
          const auto variant = std::find_if(enumeration->variants.begin(), enumeration->variants.end(),
              [&](const HirEnumVariant& candidate) { return candidate.name == variant_name; });
          if (variant == enumeration->variants.end()) {
            diagnostics_.error("D2039", member->range, "enum '" + subject_type + "' has no variant '" + variant_name + "'");
          } else {
            if (!covered.emplace(variant_name, true).second) diagnostics_.error("D2037", member->range, "duplicate match arm for '" + variant_name + "'");
            const std::size_t binding_count = binding_call == nullptr ? 0 : binding_call->children.size() - 1;
            if (binding_count != variant->payload_types.size()) {
              diagnostics_.error("D2043", pattern.range, "pattern '" + subject_type + "." + variant_name +
                                 "' expects " + std::to_string(variant->payload_types.size()) + " binding(s), received " +
                                 std::to_string(binding_count));
            }
            if (binding_call != nullptr) {
              for (std::size_t binding_index = 1; binding_index < binding_call->children.size() &&
                   binding_index - 1 < variant->payload_types.size(); ++binding_index) {
                const auto& binding = binding_call->children[binding_index];
                if (binding.kind != SyntaxKind::name_expression) {
                  diagnostics_.error("D2044", binding.range, "payload pattern must be a binding name or '_'");
                  continue;
                }
                if (binding.label == "_") continue;
                const auto& payload_type = variant->payload_types[binding_index - 1];
                Symbol symbol{SymbolKind::variable, binding.label, payload_type, {}, {}, binding.range, false, {}, {}, {}, {}, {}};
                if (!declare(std::move(symbol))) diagnostics_.error("D2045", binding.range, "duplicate payload binding '" + binding.label + "'");
                if (!subject_place.empty() && !is_copy_type(payload_type)) {
                  mark_moved_place(enum_payload_place(subject_place, variant_name, binding_index - 1),
                                   payload_type, binding.range);
                }
              }
            }
          }
        } else {
          const auto pattern_type = analyze_expression(pattern);
          if (!subject_type.empty() && !pattern_type.empty() && pattern_type != subject_type) {
            diagnostics_.error("D2036", pattern.range, "match pattern type '" + pattern_type + "' does not match '" + subject_type + "'");
          }
        }
      }
      if (!arm.children.empty()) analyze_statement(arm.children.back(), function, return_type);
      pop_scope();
      arm_states.push_back(moved_values_);
    }

    restore_ownership(entry);
    if (!arm_states.empty()) moved_values_ = union_moved_states(arm_states);
    if (enumeration != nullptr && !wildcard) {
      for (const auto& variant : enumeration->variants) {
        if (!covered.contains(variant.name)) {
          diagnostics_.error("D2038", node.range, "non-exhaustive match; missing variant '" + subject_type + "." + variant.name + "'");
          break;
        }
      }
    }
    return;
  }

  if (node.kind == SyntaxKind::expression_statement) {
    if (!node.children.empty()) (void)analyze_expression(node.children.front());
    return;
  }

  for (const auto& child : node.children) {
    if (child.kind >= SyntaxKind::name_expression && child.kind <= SyntaxKind::parenthesized_expression) (void)analyze_expression(child);
    else analyze_statement(child, function, return_type);
  }
}

namespace {
std::string substitute_generic_type(const std::string& type_name,
                                    const std::unordered_map<std::string, std::string>& substitutions);
}

std::string SemanticAnalyzer::analyze_expression(const SyntaxNode& node) {
  if (node.kind == SyntaxKind::struct_expression) {
    const auto [base, arguments] = split_generic_name(node.label);
    const auto layout_it = type_layouts_.find(arguments.empty() ? node.label : base);
    if (layout_it == type_layouts_.end()) {
      diagnostics_.error("D2020", node.range, "unknown struct type '" + node.label + "'");
      for (const auto& field : node.children) if (!field.children.empty()) (void)analyze_expression(field.children.front());
      return {};
    }

    request_instantiation(node.label);
    std::unordered_map<std::string, std::string> substitutions;
    if (!arguments.empty()) {
      const auto params = generic_types_.find(base);
      if (params == generic_types_.end() || params->second.size() != arguments.size()) {
        diagnostics_.error("D2020", node.range, "generic struct initializer '" + node.label + "' has the wrong number of type arguments");
      } else {
        for (std::size_t i = 0; i < arguments.size(); ++i) substitutions.emplace(params->second[i], arguments[i]);
      }
    }
    std::unordered_set<std::string> seen;
    for (const auto& field : node.children) {
      if (!seen.insert(field.label).second) {
        diagnostics_.error("D2022", field.range, "duplicate field '" + field.label + "' in struct initializer");
        continue;
      }
      const auto expected_field = std::find_if(layout_it->second.fields.begin(), layout_it->second.fields.end(),
          [&](const HirField& candidate) { return candidate.name == field.label; });
      if (expected_field == layout_it->second.fields.end()) {
        diagnostics_.error("D2023", field.range, "struct '" + node.label + "' has no field '" + field.label + "'");
        if (!field.children.empty()) (void)analyze_expression(field.children.front());
        continue;
      }
      const auto actual = field.children.empty() ? std::string{} : analyze_expression(field.children.front());
      const auto expected = substitutions.empty() ? expected_field->type_name : substitute_generic_type(expected_field->type_name, substitutions);
      if (!actual.empty() && actual != expected && !(is_numeric(actual) && is_numeric(expected))) {
        diagnostics_.error("D2021", field.range, "struct field '" + field.label + "' expects '" + expected + "', received '" + actual + "'");
      }
    }

    for (const auto& expected_field : layout_it->second.fields) {
      if (!seen.contains(expected_field.name))
        diagnostics_.error("D2024", node.range, "missing field '" + expected_field.name + "' in struct initializer for '" + node.label + "'");
    }
    return node.label;
  }

  if (node.kind == SyntaxKind::name_expression) {
    if (const auto intrinsic = primitive_integer_constant(node.label)) return intrinsic->first;
    if (const auto constant = constants_.find(node.label); constant != constants_.end()) return constant->second.first;

    if (node.label == "true" || node.label == "false") return "bool";
    if (node.label == "null") return {};
    const auto* symbol = lookup(node.label);
    if (reference_bindings_.contains(node.label)) used_reference_bindings_.insert(node.label);
    if (symbol == nullptr) {
      if (analyzing_closure_) {
        diagnostics_.error("D2120", node.range, "closure capture of '" + node.label +
                           "' is not an implicit stable capture; pass the value as a closure parameter");
      } else {
        diagnostics_.error("D2008", node.range, "unknown name '" + node.label + "'");
      }
      return {};
    }

    if (symbol->kind == SymbolKind::function) {
      return function_type_name(symbol->parameter_types, symbol->type_name);
    }

    if (is_moved_place(node.label)) {
      diagnostics_.error("D2054", node.range, "use of moved value '" + node.label + "'");
    }

    if (suspended_references_.contains(node.label)) {
      diagnostics_.error("D2065", node.range, "cannot use reference '" + node.label + "' while it is reborrowed");
    }
    return symbol->type_name;
  }

  if (node.kind == SyntaxKind::tuple_expression) {
    std::string type_name = "(";
    for (std::size_t index = 0; index < node.children.size(); ++index) {
      if (index != 0) type_name += ",";
      type_name += analyze_expression(node.children[index]);
    }
    type_name += ")";
    ensure_tuple_layout(type_name, node.range);
    return type_name;
  }

  if (node.kind == SyntaxKind::array_expression) {
    std::string element_type;
    for (const auto& element : node.children) {
      const auto actual = analyze_expression(element);
      if (element_type.empty()) element_type = actual;
      else if (!actual.empty() && actual != element_type && !(is_numeric(actual) && is_numeric(element_type))) {
        diagnostics_.error("D2028", element.range, "array literal elements have incompatible types");
      }
    }
    return element_type.empty() ? std::string{} : element_type + "[]";
  }

  if (node.kind == SyntaxKind::literal_expression) {
    if (!node.label.empty() && node.label.front() == '"') return "string";
    if (!node.label.empty() && node.label.front() == '\'') return "char";
    return node.label.find_first_of(".eE") == std::string::npos ? "i64" : "f64";
  }

  if (node.kind == SyntaxKind::closure_expression) {
    const auto name = closure_function_name(node);
    const auto* symbol = lookup(name);
    if (symbol == nullptr || symbol->kind != SymbolKind::function) {
      diagnostics_.error("D2117", node.range, "closure function metadata is unavailable");
      return {};
    }

    if (move_closure_functions_.contains(name)) return "FnOnce<" + name + ">";
    if (mutable_closure_functions_.contains(name)) return "FnMut<" + name + ">";
    return "Fn<" + name + ">";
  }

  if (node.kind == SyntaxKind::parenthesized_expression) {
    return node.children.empty() ? std::string{} : analyze_expression(node.children.front());
  }

  if (node.kind == SyntaxKind::cast_expression) {
    const auto source = node.children.empty() ? std::string{} : analyze_expression(node.children.front());
    const auto target = node.label;
    if (source.empty()) return target;
    const auto source_pointer = parse_raw_pointer_type(source);
    const auto target_pointer = parse_raw_pointer_type(target);
    const auto pointer_sized_integer = [](std::string_view type) {
      return type == "usize" || type == "isize" || type == "u64" || type == "i64" || type == "uint" || type == "int";
    };
    const bool raw_pointer_cast =
        (source_pointer && target_pointer) ||
        (source_pointer && pointer_sized_integer(target)) ||
        (target_pointer && pointer_sized_integer(source));
    if (raw_pointer_cast) {
      if (unsafe_depth_ == 0) diagnostics_.error("D2250", node.range, "raw-pointer casts require an unsafe block or unsafe function");
      return target;
    }

    if (!native_cast_type(source) || !native_cast_type(target)) {
      diagnostics_.error("D2289", node.range, "'as' requires scalar numeric/bool types or an unsafe raw-pointer cast");
      return target;
    }

    if (!supported_numeric_cast(source, target)) {
      diagnostics_.error("D2290", node.range, "numeric cast from '" + source + "' to '" + target + "' is not supported by the native backend");
    }
    return target;
  }

  if (node.kind == SyntaxKind::try_expression) {
    if (node.children.empty()) return {};
    const auto operand_type = analyze_expression(node.children.front());
    const auto [base, arguments] = split_generic_name(operand_type);
    const auto propagation_name = unqualified_type_name(base);
    if ((propagation_name != "Result" && propagation_name != "Option") || arguments.empty() ||
        resolve_enum(operand_type) == nullptr) {
      diagnostics_.error("D2047", node.range, "the '?' operator requires Result<T,E> or Option<T>");
      return {};
    }

    if (!propagation_types_compatible(operand_type, current_return_type_)) {
      diagnostics_.error("D2048", node.range,
                         "the '?' operator requires an enclosing " + propagation_name +
                             " return type with a compatible failure payload");
    }
    return arguments.front();
  }

  if (node.kind == SyntaxKind::unary_expression) {
    if (node.label == "await") {
      const auto operand = node.children.empty() ? std::string{} : analyze_expression(node.children.front());
      if (async_depth_ == 0) {
        diagnostics_.error("D2257", node.range, "await is only valid inside an async function");
      } else {
        // Async frames may move between polls. A reference into an ordinary local would
        // therefore become self-referential once both the referent and the reference are
        // spilled into the frame. Non-lexical release removes references that are dead
        // before this statement, so every remaining local-origin binding is live across
        // the suspension point. Parameter-origin references retain caller-owned storage
        // and are permitted subject to the function's declared lifetime contract.
        for (const auto& [binding, reference] : reference_bindings_) {
          const auto& origin = reference.first;
          const auto* owner = lookup(root_name(origin));
          if (owner != nullptr && owner->kind == SymbolKind::variable) {
            diagnostics_.error("D2264", node.range,
                               "reference '" + binding + "' to local storage '" +
                                   root_name(origin) + "' cannot remain live across await");
          }
        }
      }
      return operand;
    }

    if (node.label == "spawn") {
      const auto operand = node.children.empty() ? std::string{} : analyze_expression(node.children.front());
      if (async_depth_ == 0) diagnostics_.error("D2258", node.range, "spawn is only valid inside an async function");
      return operand;
    }

    if (node.label == "&" || node.label == "&mut") {
      if (node.children.empty()) return {};
      if (node.children.front().kind == SyntaxKind::unary_expression && node.children.front().label == "*") {
        const auto& dereference = node.children.front();
        const auto pointer_type = dereference.children.empty() ? std::string{} : analyze_expression(dereference.children.front());
        if (const auto pointer = parse_raw_pointer_type(pointer_type)) {
          if (unsafe_depth_ == 0) diagnostics_.error("D2250", node.range, "borrowing through a raw pointer requires an unsafe block or unsafe function");
          if (node.label == "&mut" && !pointer->mutable_pointer) {
            diagnostics_.error("D2058", node.range, "mutable borrow requires a mutable raw pointer");
          }
          return pointer->pointee_type + (node.label == "&mut" ? "&mut" : "&");
        }
      }
      const auto place = place_path(node.children.front());
      if (place.empty()) {
        diagnostics_.error("D2058", node.range, "borrowing requires an addressable local, field, or indexed element");
        return {};
      }
      const auto owner = root_name(place);
      const auto* symbol = lookup(owner);
      if (symbol == nullptr) {
        diagnostics_.error("D2008", node.range, "unknown name '" + owner + "'");
        return {};
      }
      if (moved_values_.contains(owner)) {
        diagnostics_.error("D2054", node.range, "use of moved value '" + owner + "'");
      }
      const auto referent = analyze_expression(node.children.front());
      return referent + (node.label == "&mut" ? "&mut" : "&");
    }

    if (node.label == "*") {
      const auto operand = node.children.empty() ? std::string{} : analyze_expression(node.children.front());
      if (const auto reference = parse_reference_type(operand)) return reference->referent_type;
      if (const auto pointer = parse_raw_pointer_type(operand)) {
        if (unsafe_depth_ == 0) diagnostics_.error("D2250", node.range, "raw-pointer dereference requires an unsafe block or unsafe function");
        return pointer->pointee_type;
      }
      diagnostics_.error("D2059", node.range, "dereference requires a reference or raw-pointer value");
      return {};
    }

    if (node.label == "move") {
      if (node.children.empty()) {
        diagnostics_.error("D2051", node.range, "move requires an addressable local, field, indexed element, or raw-pointer dereference");
        return {};
      }
      // Raw-pointer storage has no lexical ownership root to mark moved, but it
      // is still an addressable place. This is the primitive needed by typed
      // allocation containers to transfer a T out of manually managed storage.
      if (node.children.front().kind == SyntaxKind::unary_expression && node.children.front().label == "*") {
        const auto type = analyze_expression(node.children.front());
        if (!type.empty()) return type;
      }
      const auto place = place_path(node.children.front());
      if (place.empty()) {
        diagnostics_.error("D2051", node.range, "move requires an addressable local, field, indexed element, or raw-pointer dereference");
        return {};
      }
      const auto type = analyze_expression(node.children.front());
      mark_moved_place(place, type, node.range);
      return type;
    }
    const auto operand = node.children.empty() ? std::string{} : analyze_expression(node.children.front());
    if (node.label == "!" && !operand.empty() && operand != "bool") {
      diagnostics_.error("D2013", node.range, "logical negation requires a bool operand");
      return "bool";
    }

    if (node.label == "~" && !operand.empty() && !is_integral(operand)) {
      diagnostics_.error("D2014", node.range, "bitwise complement requires an integer operand");
    }

    if ((node.label == "+" || node.label == "-") && !operand.empty() && !is_numeric(operand)) {
      diagnostics_.error("D2015", node.range, "numeric unary operator requires a numeric operand");
    }
    return node.label == "!" ? "bool" : operand;
  }

  if (node.kind == SyntaxKind::binary_expression) {
    const auto left = node.children.empty() ? std::string{} : analyze_expression(node.children[0]);
    const auto right = node.children.size() < 2 ? std::string{} : analyze_expression(node.children[1]);
    if (node.label == ".." || node.label == "..=") {
      if ((!left.empty() && !is_integral(left)) || (!right.empty() && !is_integral(right))) {
        diagnostics_.error("D2115", node.range, "range bounds must have integer types");
      }
      if (!left.empty() && !right.empty() && left != right) {
        diagnostics_.error("D2116", node.range, "range bounds must have matching integer types");
      }
      return "Range<" + (left.empty() ? right : left) + ">";
    }

    if (node.label == "&&" || node.label == "||") {
      if ((!left.empty() && left != "bool") || (!right.empty() && right != "bool")) {
        diagnostics_.error("D2016", node.range, "logical operators require bool operands");
      }
      return "bool";
    }
    const bool comparison = node.label == "==" || node.label == "!=" || node.label == "<" ||
                            node.label == "<=" || node.label == ">" || node.label == ">=";
    const bool bitwise = node.label == "&" || node.label == "|" || node.label == "^" ||
                         node.label == "<<" || node.label == ">>";
    if (bitwise && ((!left.empty() && !is_integral(left)) || (!right.empty() && !is_integral(right)))) {
      diagnostics_.error("D2017", node.range, "bitwise and shift operators require integer operands");
    }

    if (!left.empty() && !right.empty() && left != right &&
        !implicit_numeric_family_compatible(left, right)) {
      diagnostics_.error("D2009", node.range, "binary operands have incompatible types '" + left + "' and '" + right + "'");
    }
    return comparison ? "bool" : (left.empty() ? right : left);
  }

  if (node.kind == SyntaxKind::assignment_expression) {
    if (node.children.size() < 2) return {};
    if (node.children[0].kind == SyntaxKind::unary_expression && node.children[0].label == "*" &&
        !node.children[0].children.empty()) {
      const auto reference_type = analyze_expression(node.children[0].children.front());
      const auto reference = parse_reference_type(reference_type);
      const auto pointer = parse_raw_pointer_type(reference_type);
      if (!((reference && reference->mutable_reference) || (pointer && pointer->mutable_pointer))) {
        diagnostics_.error("D2060", node.children[0].range, "assignment through an immutable reference or raw pointer is not allowed");
      }
      if (pointer && unsafe_depth_ == 0) {
        diagnostics_.error("D2250", node.children[0].range, "raw-pointer dereference requires an unsafe block or unsafe function");
      }
    }
    bool through_reference = node.children[0].kind == SyntaxKind::unary_expression &&
                             node.children[0].label == "*";
    const auto assigned_place = place_path(node.children[0]);
    if (!through_reference && !assigned_place.empty()) {
      const auto* root_symbol = lookup(root_name(assigned_place));
      if (root_symbol != nullptr) {
        if (const auto reference = parse_reference_type(root_symbol->type_name))
          through_reference = reference->mutable_reference;
      }
    }

    if (!through_reference && node.children[0].kind == SyntaxKind::index_expression &&
        !node.children[0].children.empty()) {
      const auto base_place = place_path(node.children[0].children.front());
      const auto* base_symbol = base_place.empty() ? nullptr : lookup(root_name(base_place));
      if (base_symbol != nullptr) {
        if (const auto slice = parse_slice_type(base_symbol->type_name); slice && !slice->mutable_slice) {
          diagnostics_.error("D2155", node.children[0].range,
                             "assignment through a shared slice is not allowed");
        }
      }
    }

    if (node.label == "=" && node.children[0].kind == SyntaxKind::name_expression) {
      const auto* binding = lookup(node.children[0].label);
      if (binding != nullptr &&
          (parse_reference_type(binding->type_name) || parse_slice_type(binding->type_name))) {
        diagnostics_.error(
            "D2061", node.children[0].range,
            "reference and slice bindings cannot be rebound; create a new binding instead");
      }
    }

    if (!through_reference && !assigned_place.empty() && has_active_borrow(assigned_place)) {
      diagnostics_.error("D2061", node.children[0].range, "cannot assign to '" + assigned_place + "' while it is borrowed");
    }

    if (node.label == "=" && !assigned_place.empty()) {
      // Direct assignment is a reinitialization operation. Clear the moved state
      // for this exact place before resolving its type so `field = value` can
      // restore a previously moved field without permitting use of the old value.
      mark_initialized(assigned_place);
    }
    std::string left;
    if (node.children[0].kind == SyntaxKind::name_expression) {
      const auto* symbol = lookup(node.children[0].label);
      if (symbol == nullptr) diagnostics_.error("D2008", node.children[0].range, "unknown name '" + node.children[0].label + "'");
      else left = symbol->type_name;
    } else {
      left = analyze_expression(node.children[0]);
    }
    const auto right = analyze_expression(node.children[1]);
    if (node.label == "=" && node.children[0].kind == SyntaxKind::name_expression) {
      const auto origin = aggregate_local_reference_origin(node.children[1]);
      if (!origin.empty()) aggregate_reference_origins_[node.children[0].label] = origin;
      else aggregate_reference_origins_.erase(node.children[0].label);
    }

    if (!left.empty() && !right.empty() && left != right && !slice_types_compatible(right, left) &&
        !implicit_numeric_family_compatible(right, left)) {
      diagnostics_.error("D2010", node.range, "assignment type mismatch between '" + left + "' and '" + right + "'");
    }
    return left;
  }

  if (node.kind == SyntaxKind::member_expression) {
    if (node.children.empty()) return {};
    if (node.modifier == "scoped" && node.children.front().kind == SyntaxKind::name_expression) {
      if (const auto intrinsic = primitive_integer_constant(node.children.front().label + "::" + node.label))
        return intrinsic->first;
    }

    if (node.children.front().kind == SyntaxKind::name_expression) {
      const auto enum_name = node.children.front().label;
      if (const auto* enumeration = resolve_enum(enum_name); enumeration != nullptr) {
        if (node.modifier != "scoped") diagnostics_.error("D2039", node.range, "enum variants must use '::' type access");
        for (const auto& variant : enumeration->variants) {
          if (variant.name == node.label) return enum_name;
        }
        diagnostics_.error("D2039", node.range, "enum '" + enum_name + "' has no variant '" + node.label + "'");
        return {};
      }
    }
    const auto member_place = place_path(node);
    if (!member_place.empty() && is_moved_place(member_place)) {
      diagnostics_.error("D2054", node.range, "use of moved value '" + member_place + "'");
    }
    std::string base_type;
    if (node.children.front().kind == SyntaxKind::name_expression) {
      const auto* base_symbol = lookup(node.children.front().label);
      if (base_symbol == nullptr) diagnostics_.error("D2008", node.children.front().range,
                                                     "unknown name '" + node.children.front().label + "'");
      else base_type = base_symbol->type_name;
    } else {
      base_type = analyze_expression(node.children.front());
    }

    if (const auto reference = parse_reference_type(base_type)) base_type = reference->referent_type;
    if (parse_slice_type(base_type) && node.label == "length") return "usize";
    const auto layout = type_layouts_.find(base_type);
    if (layout == type_layouts_.end()) {
      const auto [generic_base, arguments] = split_generic_name(base_type);
      const auto template_layout = type_layouts_.find(generic_base);
      const auto parameters = generic_types_.find(generic_base);
      if (template_layout != type_layouts_.end() && parameters != generic_types_.end() &&
          parameters->second.size() == arguments.size()) {
        std::unordered_map<std::string, std::string> substitutions;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
          substitutions.emplace(parameters->second[index], arguments[index]);
        }
        for (const auto& field : template_layout->second.fields) {
          if (field.name == node.label) return substitute_generic_type(field.type_name, substitutions);
        }
        diagnostics_.error("D2019", node.range, "type '" + base_type + "' has no field '" + node.label + "'");
        return {};
      }
      diagnostics_.error("D2018", node.range, "type '" + base_type + "' has no fields");
      return {};
    }

    for (const auto& field : layout->second.fields) if (field.name == node.label) return field.type_name;
    diagnostics_.error("D2019", node.range, "type '" + base_type + "' has no field '" + node.label + "'");
    return {};
  }

  if (node.kind == SyntaxKind::index_expression) {
    if (node.children.size() < 2) return {};
    const auto indexed_place = place_path(node);
    if (!indexed_place.empty() && is_moved_place(indexed_place)) {
      diagnostics_.error("D2054", node.range, "use of moved value '" + indexed_place + "'");
    }
    std::string base_type;
    if (node.children[0].kind == SyntaxKind::name_expression) {
      const auto* base_symbol = lookup(node.children[0].label);
      if (base_symbol == nullptr) diagnostics_.error("D2008", node.children[0].range,
                                                     "unknown name '" + node.children[0].label + "'");
      else base_type = base_symbol->type_name;
    } else {
      base_type = analyze_expression(node.children[0]);
    }
    const auto index_type = analyze_expression(node.children[1]);
    if (!index_type.empty() && !is_integral(index_type)) {
      diagnostics_.error("D2022", node.children[1].range, "array index must have an integer type");
    }
    const auto array = parse_fixed_array_type(base_type);
    const auto symbolic_array = split_symbolic_array_type(base_type);
    const auto slice = parse_slice_type(base_type);
    if (!array && !symbolic_array && !slice) {
      diagnostics_.error("D2023", node.range, "type '" + base_type + "' is not indexable");
      return {};
    }

    if (array && node.children[1].kind == SyntaxKind::literal_expression) {
      try {
        const auto value = std::stoull(node.children[1].label);
        if (value >= array->length) diagnostics_.error("D2024", node.children[1].range, "constant array index is out of bounds");
      } catch (...) {
      }
    }

    if (array) return array->element_type;
    if (symbolic_array) return symbolic_array->first;
    return slice->element_type;
  }

  if (node.kind == SyntaxKind::call_expression) {
    if (node.children.empty()) return {};
    const auto& callee = node.children.front();
    if (callee.kind == SyntaxKind::closure_expression) {
      const auto name = closure_function_name(callee);
      if (callee.modifier == "move") {
        for (const auto& capture : closure_captures_[name]) mark_moved(capture, callee.range);
      }
      const auto* symbol = lookup(name);
      if (symbol == nullptr || symbol->kind != SymbolKind::function) {
        diagnostics_.error("D2117", callee.range, "closure function metadata is unavailable");
        return {};
      }
      const auto capture_count = closure_captures_[name].size();
      const auto explicit_count = symbol->parameter_types.size() >= capture_count
          ? symbol->parameter_types.size() - capture_count : 0;
      const auto actual_count = node.children.size() - 1;
      if (actual_count != explicit_count) {
        diagnostics_.error("D2118", node.range, "closure expects " +
                           std::to_string(explicit_count) + " argument(s), received " +
                           std::to_string(actual_count));
      }
      for (std::size_t index = 1; index < node.children.size(); ++index) {
        const auto actual = analyze_expression(node.children[index]);
        const auto parameter_index = capture_count + index - 1;
        if (parameter_index < symbol->parameter_types.size()) {
          const auto& expected = symbol->parameter_types[parameter_index];
          if (!actual.empty() && actual != expected &&
              !reference_types_compatible(actual, expected) &&
              !(is_numeric(actual) && is_numeric(expected))) {
            diagnostics_.error("D2119", node.children[index].range,
                               "closure argument expects '" + expected + "', received '" + actual + "'");
          }
        }
      }
      return symbol->type_name;
    }

    if (callee.kind == SyntaxKind::name_expression) {
      const auto* closure_binding = lookup(callee.label);
      if (closure_binding != nullptr && closure_binding->kind == SymbolKind::variable &&
          ((closure_binding->type_name.rfind("Fn<", 0) == 0) ||
           (closure_binding->type_name.rfind("FnMut<", 0) == 0) ||
           (closure_binding->type_name.rfind("FnOnce<", 0) == 0)) && closure_binding->type_name.back() == '>') {
        const auto open = closure_binding->type_name.find('<');
        const auto function_name = closure_binding->type_name.substr(open + 1, closure_binding->type_name.size() - open - 2);
        const bool closure_already_consumed = moved_values_.contains(callee.label);
        if (closure_already_consumed) {
          diagnostics_.error("D2125", callee.range, "FnOnce closure '" + callee.label + "' has already been called");
        }
        const auto* function = lookup(function_name);
        if (function == nullptr || function->kind != SymbolKind::function) {
          diagnostics_.error("D2123", callee.range, "closure binding metadata is unavailable");
          return {};
        }
        const auto capture_count = closure_captures_[function_name].size();
        const auto explicit_count = function->parameter_types.size() >= capture_count
            ? function->parameter_types.size() - capture_count : 0;
        const auto actual_count = node.children.size() - 1;
        if (actual_count != explicit_count) {
          diagnostics_.error("D2118", node.range, "closure expects " +
                             std::to_string(explicit_count) + " argument(s), received " +
                             std::to_string(actual_count));
        }
        for (std::size_t index = 1; index < node.children.size(); ++index) {
          const auto actual = analyze_expression(node.children[index]);
          const auto parameter_index = capture_count + index - 1;
          if (parameter_index < function->parameter_types.size()) {
            const auto& expected = function->parameter_types[parameter_index];
            if (!actual.empty() && actual != expected && !reference_types_compatible(actual, expected) &&
                !(is_numeric(actual) && is_numeric(expected))) {
              diagnostics_.error("D2119", node.children[index].range,
                                 "closure argument expects '" + expected + "', received '" + actual + "'");
            }
          }
        }
        if (move_closure_functions_.contains(function_name) && !closure_already_consumed) {
          mark_moved(callee.label, callee.range);
        }
        return function->type_name;
      }
    }

    if (callee.kind == SyntaxKind::name_expression) {
      const auto* callable = lookup(callee.label);
      if (callable != nullptr && (callable->kind == SymbolKind::variable || callable->kind == SymbolKind::parameter)) {
        if (const auto function = parse_function_type(callable->type_name)) {
          const auto actual_count = node.children.size() - 1;
          if (actual_count != function->parameter_types.size()) {
            diagnostics_.error("D2260", node.range, "function pointer expects " +
                               std::to_string(function->parameter_types.size()) + " argument(s), received " +
                               std::to_string(actual_count));
          }
          for (std::size_t index = 1; index < node.children.size(); ++index) {
            const auto actual = analyze_expression(node.children[index]);
            const auto parameter_index = index - 1;
            if (parameter_index < function->parameter_types.size()) {
              const auto& expected = function->parameter_types[parameter_index];
              if (!actual.empty() && actual != expected && !reference_types_compatible(actual, expected) &&
                  !(is_numeric(actual) && is_numeric(expected))) {
                diagnostics_.error("D2261", node.children[index].range,
                                   "function pointer argument expects '" + expected + "', received '" + actual + "'");
              }
            }
          }
          return function->return_type;
        }
        if (const auto closure = parse_callable_type(callable->type_name)) {
          const auto actual_count = node.children.size() - 1;
          if (actual_count != closure->parameter_types.size()) {
            diagnostics_.error("D2262", node.range, "callable expects " +
                               std::to_string(closure->parameter_types.size()) + " argument(s), received " +
                               std::to_string(actual_count));
          }
          for (std::size_t index = 1; index < node.children.size(); ++index) {
            const auto actual = analyze_expression(node.children[index]);
            const auto parameter_index = index - 1;
            if (parameter_index < closure->parameter_types.size()) {
              const auto& expected = closure->parameter_types[parameter_index];
              if (!actual.empty() && actual != expected && !reference_types_compatible(actual, expected) &&
                  !(is_numeric(actual) && is_numeric(expected))) {
                diagnostics_.error("D2263", node.children[index].range,
                                   "callable argument expects '" + expected + "', received '" + actual + "'");
              }
            }
          }
          if (closure->kind == CallableKind::once) {
            if (moved_values_.contains(callee.label)) {
              diagnostics_.error("D2125", callee.range, "FnOnce callable '" + callee.label + "' has already been called");
            } else {
              mark_moved(callee.label, callee.range);
            }
          }
          return closure->return_type;
        }
      }
    }

    if (callee.kind == SyntaxKind::member_expression && !callee.children.empty() &&
        !(callee.children.front().kind == SyntaxKind::name_expression &&
          resolve_enum(callee.children.front().label) != nullptr)) {
      const bool type_qualified = callee.children.front().kind == SyntaxKind::name_expression &&
                                  type_exists(callee.children.front().label);
      auto receiver_type = type_qualified ? callee.children.front().label : analyze_expression(callee.children.front());
      if (const auto reference = parse_reference_type(receiver_type)) receiver_type = reference->referent_type;
      const TraitMethodContract* selected_contract = nullptr;
      std::string selected_trait;
      const auto dynamic_receiver = parse_dynamic_trait_type(receiver_type);
      const auto inherent_key = receiver_type + "::" + callee.label;
      std::optional<TraitMethodContract> generic_inherent_contract;
      bool generic_inherent_associated = false;
      const TraitMethodContract* inherent_contract = nullptr;
      bool inherent_associated = false;
      if (const auto inherent = inherent_method_contracts_.find(inherent_key);
          inherent != inherent_method_contracts_.end()) {
        inherent_contract = &inherent->second;
        inherent_associated = inherent_associated_functions_.contains(inherent_key);
      } else if (!split_generic_name(receiver_type).second.empty()) {
        for (const auto& implementation : generic_trait_implementations_) {
          if (!implementation.trait_name.empty()) continue;
          const auto method = implementation.methods.find(callee.label);
          if (method == implementation.methods.end()) continue;
          std::unordered_map<std::string, std::string> substitutions;
          if (!match_generic_trait_target(implementation, receiver_type, substitutions)) continue;
          TraitMethodContract contract;
          contract.name = callee.label;
          contract.return_type = normalize_associated_type(
              substitute_generic_type(function_return_type(method->second.label), substitutions));
          const auto attributes = split_attributes(method->second.modifier);
          contract.attributes.insert(attributes.begin(), attributes.end());
          contract.range = method->second.range;
          bool has_receiver = false;
          for (const auto& parameter : method->second.children) {
            if (parameter.kind != SyntaxKind::parameter) continue;
            auto [parameter_type, parameter_name] = split_typed_name(parameter.label);
            parameter_type = normalize_associated_type(substitute_generic_type(parameter_type, substitutions));
            contract.parameter_types.push_back(parameter_type);
            if (contract.parameter_types.size() == 1 && parameter_name == "self") {
              auto receiver = parameter_type;
              if (const auto reference = parse_reference_type(receiver)) receiver = reference->referent_type;
              if (receiver != receiver_type) {
                diagnostics_.error("D2129", parameter.range,
                                   "generic inherent method receiver must resolve to '" + receiver_type + "'");
              } else {
                has_receiver = true;
              }
            }
          }
          generic_inherent_associated = !has_receiver;
          generic_inherent_contract = std::move(contract);
          inherent_contract = &*generic_inherent_contract;
          inherent_associated = generic_inherent_associated;
          request_instantiation(receiver_type);
          break;
        }
      }
      if (inherent_contract != nullptr) {
        const bool associated = inherent_associated;
        if (type_qualified && associated && callee.modifier != "scoped") {
          diagnostics_.error("D2130", callee.range, "associated function '" + callee.label + "' must use '::' type access");
        }
        if (type_qualified != associated) {
          diagnostics_.error("D2131", callee.range, associated
              ? "associated function '" + callee.label + "' must be called through type '" + receiver_type + "'"
              : "method '" + callee.label + "' requires a value receiver");
        }
        const auto receiver_slots = associated ? 0ULL : 1ULL;
        const auto expected_count = inherent_contract->parameter_types.size() >= receiver_slots
            ? inherent_contract->parameter_types.size() - receiver_slots : 0;
        const auto actual_count = node.children.size() - 1;
        if (actual_count != expected_count) diagnostics_.error("D2132", node.range,
            "inherent call '" + callee.label + "' expects " + std::to_string(expected_count) +
            " argument(s), received " + std::to_string(actual_count));
        for (std::size_t index = 1; index < node.children.size(); ++index) {
          const auto actual = analyze_expression(node.children[index]);
          const auto parameter_index = index - 1 + receiver_slots;
          if (parameter_index < inherent_contract->parameter_types.size()) {
            const auto& expected = inherent_contract->parameter_types[parameter_index];
            if (!actual.empty() && actual != expected && !reference_types_compatible(actual, expected) &&
                !(is_numeric(actual) && is_numeric(expected))) {
              diagnostics_.error("D2133", node.children[index].range,
                                 "inherent method argument expects '" + expected + "', received '" + actual + "'");
            }
          }
        }
        for (const auto& effect : current_function_attributes_) {
          if ((effect == "pure" || effect == "no_panic" || effect == "no_alloc" || effect == "deterministic") &&
              !inherent_contract->attributes.contains(effect)) {
            diagnostics_.error("D2233", node.range, "@" + effect + " function calls inherent method '" +
                               callee.label + "', which does not guarantee @" + effect);
          }
        }
        return inherent_contract->return_type;
      }
      if (dynamic_receiver) {
        const auto contracts = trait_contracts_.find(dynamic_receiver->trait_name);
        if (contracts != trait_contracts_.end()) {
          for (const auto& contract : contracts->second) {
            if (contract.name == callee.label) {
              selected_contract = &contract;
              selected_trait = dynamic_receiver->trait_name;
              break;
            }
          }
        }
      } else if (const auto bounds = current_generic_bounds_.find(receiver_type); bounds != current_generic_bounds_.end()) {
        std::vector<std::string> expanded;
        std::function<void(const std::string&)> expand = [&](const std::string& trait_name) {
          if (const auto alias = trait_aliases_.find(trait_name); alias != trait_aliases_.end()) {
            for (const auto& target : alias->second) expand(target);
          } else if (std::find(expanded.begin(), expanded.end(), trait_name) == expanded.end()) {
            expanded.push_back(trait_name);
          }
        };
        for (const auto& trait_name : bounds->second) expand(trait_name);
        for (const auto& trait_name : expanded) {
          const auto contracts = trait_contracts_.find(trait_name);
          if (contracts == trait_contracts_.end()) continue;
          for (const auto& contract : contracts->second) if (contract.name == callee.label) {
            if (selected_contract != nullptr) diagnostics_.error("D2100", callee.range, "ambiguous trait method '" + callee.label + "'");
            selected_contract = &contract; selected_trait = trait_name;
          }
        }
      } else {
        for (const auto& [trait_name, contracts] : trait_contracts_) {
          if (!implements_trait(receiver_type, trait_name)) continue;
          for (const auto& contract : contracts) if (contract.name == callee.label) {
            if (selected_contract != nullptr) diagnostics_.error("D2100", callee.range, "ambiguous trait method '" + callee.label + "'");
            selected_contract = &contract; selected_trait = trait_name;
          }
        }
      }
      if (selected_contract == nullptr) {
        diagnostics_.error("D2099", callee.range, "type '" + receiver_type + "' has no trait method '" + callee.label + "'");
        for (std::size_t index = 1; index < node.children.size(); ++index) (void)analyze_expression(node.children[index]);
        return {};
      }
      const auto expected_count = selected_contract->parameter_types.empty() ? 0 : selected_contract->parameter_types.size() - 1;
      const auto actual_count = node.children.size() - 1;
      if (actual_count != expected_count) diagnostics_.error("D2101", node.range, "method '" + callee.label + "' expects " + std::to_string(expected_count) + " argument(s), received " + std::to_string(actual_count));
      for (std::size_t index = 1; index < node.children.size(); ++index) {
        const auto actual = analyze_expression(node.children[index]);
        if (index < selected_contract->parameter_types.size()) {
          auto expected = selected_contract->parameter_types[index];
          if (expected.starts_with("Self")) expected.replace(0, 4, receiver_type);
          if (const auto binding = resolve_associated_type_binding(receiver_type, selected_trait, expected)) {
            expected = normalize_associated_type(*binding);
          }
          if (!actual.empty() && actual != expected && !reference_types_compatible(actual, expected) && !slice_types_compatible(actual, expected) &&
                !(is_numeric(actual) && is_numeric(expected)))
            diagnostics_.error("D2102", node.children[index].range, "method argument expects '" + expected + "', received '" + actual + "'");
        }
      }
      for (const auto& effect : current_function_attributes_) {
        if ((effect == "pure" || effect == "no_panic" || effect == "no_alloc" || effect == "deterministic") &&
            !selected_contract->attributes.contains(effect)) {
          diagnostics_.error("D2234", node.range, "@" + effect + " function calls trait method '" +
                             callee.label + "', which does not guarantee @" + effect);
        }
      }
      auto result = selected_contract->return_type;
      if (result.starts_with("Self")) result.replace(0, 4, receiver_type);
      if (const auto binding = resolve_associated_type_binding(receiver_type, selected_trait, result)) {
        result = normalize_associated_type(*binding);
      }
      return result;
    }

    if (callee.kind == SyntaxKind::name_expression) {
      const auto [constructor_base, constructor_arguments] = split_generic_name(callee.label);
      const auto generic = generic_types_.find(constructor_base);
      if (generic != generic_types_.end() && !constructor_arguments.empty()) {
        if (constructor_arguments.size() != generic->second.size()) {
          diagnostics_.error("D2020", node.range, "generic constructor '" + callee.label + "' has the wrong number of type arguments");
        }
        for (std::size_t index = 1; index < node.children.size(); ++index) (void)analyze_expression(node.children[index]);
        request_instantiation(callee.label);
        return callee.label;
      }
    }
    const auto constructor = type_layouts_.find(callee.label);
    if (callee.kind == SyntaxKind::name_expression && constructor != type_layouts_.end()) {
      diagnostics_.error("D2020", node.range, "normal struct '" + callee.label + "' must use named-field initialization: Type { field: value }");
      const auto argument_count = node.children.size() - 1;
      if (argument_count != constructor->second.fields.size()) {
        diagnostics_.error("D2020", node.range, "constructor '" + callee.label + "' expects " +
                           std::to_string(constructor->second.fields.size()) + " argument(s), received " +
                           std::to_string(argument_count));
      }
      for (std::size_t index = 1; index < node.children.size(); ++index) {
        const auto actual = analyze_expression(node.children[index]);
        if (index - 1 < constructor->second.fields.size()) {
          const auto& expected = constructor->second.fields[index - 1].type_name;
          if (!actual.empty() && actual != expected && !(is_numeric(actual) && is_numeric(expected))) {
            diagnostics_.error("D2021", node.children[index].range, "constructor field expects '" + expected + "', received '" + actual + "'");
          }
        }
      }
      return callee.label;
    }

    if (callee.kind == SyntaxKind::member_expression && !callee.children.empty() &&
        callee.children.front().kind == SyntaxKind::name_expression) {
      const auto enum_name = callee.children.front().label;
      if (const auto* enumeration = resolve_enum(enum_name); enumeration != nullptr) {
        const auto variant = std::find_if(enumeration->variants.begin(), enumeration->variants.end(),
                                          [&](const HirEnumVariant& candidate) { return candidate.name == callee.label; });
        if (variant == enumeration->variants.end()) {
          diagnostics_.error("D2039", callee.range, "enum '" + enum_name + "' has no variant '" + callee.label + "'");
          return {};
        }
        const auto argument_count = node.children.size() - 1;
        if (argument_count != variant->payload_types.size()) {
          diagnostics_.error("D2041", node.range, "variant constructor '" + enum_name + "." + callee.label +
                             "' expects " + std::to_string(variant->payload_types.size()) + " argument(s), received " +
                             std::to_string(argument_count));
        }
        for (std::size_t index = 1; index < node.children.size(); ++index) {
          const auto actual = analyze_expression(node.children[index]);
          if (index - 1 < variant->payload_types.size()) {
            const auto& expected = variant->payload_types[index - 1];
            if (!actual.empty() && actual != expected && !(is_numeric(actual) && is_numeric(expected))) {
              diagnostics_.error("D2042", node.children[index].range, "variant payload expects '" + expected +
                                 "', received '" + actual + "'");
            }
          }
        }
        return enum_name;
      }
    }

    if (callee.kind != SyntaxKind::name_expression) {
      for (std::size_t index = 1; index < node.children.size(); ++index) (void)analyze_expression(node.children[index]);
      return {};
    }

    if (callee.label == "clone") {
      if (node.children.size() != 2) {
        diagnostics_.error("D2086", node.range, "clone expects exactly one argument");
        for (std::size_t index = 1; index < node.children.size(); ++index) (void)analyze_expression(node.children[index]);
        return {};
      }
      const auto type = analyze_expression(node.children[1]);
      if (!type.empty() && !is_clone_type(type)) {
        diagnostics_.error("D2087", node.children[1].range, "type '" + type + "' does not implement Clone");
      }
      return type;
    }
    const auto [function_base, generic_arguments] = split_generic_name(callee.label);
    if (function_base == "size_of" || function_base == "align_of") {
      if (generic_arguments.size() != 1 || node.children.size() != 1) {
        diagnostics_.error("D2290", node.range, function_base + "<T>() expects exactly one type argument and no value arguments");
        return {};
      }
      const auto& reflected_type = generic_arguments.front();
      const bool generic_reflected_type = current_generic_bounds_.contains(reflected_type);
      if (!generic_reflected_type && !type_exists(reflected_type)) {
        diagnostics_.error("D2217", node.range, "unknown reflected type '" + reflected_type + "'");
        return {};
      }
      if (!generic_reflected_type) request_instantiation(reflected_type);
      return "usize";
    }
    const auto* symbol = lookup(function_base);
    if (symbol == nullptr || symbol->kind != SymbolKind::function) {
      diagnostics_.error("D2011", callee.range, "unknown function '" + callee.label + "'");
      return {};
    }

    if (const auto attrs = declared_function_attributes_.find(function_base);
        attrs != declared_function_attributes_.end() && attrs->second.contains("unsafe") && unsafe_depth_ == 0) {
      diagnostics_.error("D2251", node.range, "call to unsafe function '" + function_base + "' requires an unsafe block or unsafe function");
    }

    if (symbol->generic_parameters.empty() && !generic_arguments.empty()) {
      diagnostics_.error("D2043", callee.range, "function '" + function_base + "' is not generic");
    }

    if (!symbol->generic_parameters.empty() && generic_arguments.empty()) {
      diagnostics_.error("D2044", callee.range, "generic function '" + function_base + "' requires explicit type arguments");
      return {};
    }

    if (generic_arguments.size() != symbol->generic_parameters.size()) {
      diagnostics_.error("D2045", callee.range, "generic function '" + function_base + "' expects " +
                         std::to_string(symbol->generic_parameters.size()) + " type argument(s), received " +
                         std::to_string(generic_arguments.size()));
      return {};
    }
    std::unordered_map<std::string, std::string> substitutions;
    for (std::size_t index = 0; index < generic_arguments.size(); ++index) {
      const bool const_argument = index < symbol->generic_const_types.size() &&
          !symbol->generic_const_types[index].empty();
      auto argument = generic_arguments[index];
      if (const_argument) {
        const auto value = evaluate_const_integer_expression(argument, [&](std::string_view identifier) -> std::optional<std::int64_t> {
          if (const auto constant = constants_.find(std::string(identifier)); constant != constants_.end() &&
              is_integral(constant->second.first)) {
            try { return std::stoll(constant->second.second); } catch (...) { return std::nullopt; }
          }
          return std::nullopt;
        });
        if (!value || *value <= 0) {
          diagnostics_.error("D2240", callee.range, "const generic argument '" + generic_arguments[index] +
                             "' must be a positive compile-time integer expression");
        } else {
          argument = std::to_string(*value);
        }
      } else if (!type_exists(argument)) {
        diagnostics_.error("D2002", callee.range, "unknown generic argument type '" + argument + "'");
      }
      substitutions.emplace(symbol->generic_parameters[index], argument);
      if (!const_argument && index < symbol->generic_bounds.size()) {
        for (const auto& bound : symbol->generic_bounds[index]) {
          if (!trait_contracts_.contains(bound) && !trait_aliases_.contains(bound) && !trait_supertraits_.contains(bound) &&
              bound != "Copy" && bound != "Clone" && bound != "Drop") {
            diagnostics_.error("D2096", callee.range, "unknown trait bound '" + bound + "'");
          } else if (!implements_trait(generic_arguments[index], bound)) {
            diagnostics_.error("D2097", callee.range, "type '" + generic_arguments[index] + "' does not satisfy trait bound '" + bound + "'");
          }
        }
      }
      if (!const_argument) request_instantiation(argument);
    }
    const auto resolve_specialized_type = [&](const std::string& type_name) {
      return normalize_associated_type(substitute_generic_type(type_name, substitutions));
    };
    const auto argument_count = node.children.size() - 1;
    if (argument_count != symbol->parameter_types.size()) {
      diagnostics_.error("D2012", node.range, "function '" + callee.label + "' expects " +
                         std::to_string(symbol->parameter_types.size()) + " argument(s), received " +
                         std::to_string(argument_count));
    }

    for (std::size_t index = 1; index < node.children.size(); ++index) {
      const auto actual = analyze_expression(node.children[index]);
      if (index - 1 < symbol->parameter_types.size()) {
        const auto expected = resolve_specialized_type(symbol->parameter_types[index - 1]);
        if (!actual.empty() && actual != expected &&
            !closure_coerces_to_function(node.children[index], expected) &&
            !closure_coerces_to_callable(node.children[index], expected) &&
            !callable_identity_compatible(actual, expected) &&
            !reference_types_compatible(actual, expected) &&
            !slice_types_compatible(actual, expected) &&
            !implicit_numeric_family_compatible(actual, expected)) {
          diagnostics_.error("D2046", node.children[index].range, "generic argument expects '" + expected +
                             "', received '" + actual + "'");
        }
      }
    }

    if (!generic_arguments.empty()) request_function_instantiation(callee.label);
    return resolve_specialized_type(symbol->type_name);
  }

  for (const auto& child : node.children) (void)analyze_expression(child);
  return {};
}
