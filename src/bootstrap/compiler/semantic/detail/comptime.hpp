// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

  if (node.kind == SyntaxKind::literal_expression) {
    if (node.label == "true" || node.label == "false") return {"bool", node.label};
    if (!node.label.empty() && node.label.front() == '"') return {"string", node.label};
    if (!node.label.empty() && node.label.front() == '\'') {
      const auto value = decode_character_literal(node.label);
      if (!value) { diagnostics_.error("D2259", node.range, "invalid character literal"); return {}; }
      return {"char", std::to_string(*value)};
    }

    if (node.label.find('.') != std::string::npos) return {"f64", node.label};
    return {"i64", node.label};
  }

  if (node.kind == SyntaxKind::name_expression) {
    if (node.label == "true" || node.label == "false") return {"bool", node.label};
    if (const auto found = constants_.find(node.label); found != constants_.end()) return found->second;
    diagnostics_.error("D2201", node.range, "unknown compile-time constant '" + node.label + "'");
    return {};
  }

  if (node.kind == SyntaxKind::array_expression) {
    std::vector<std::string> values;
    std::string element_type;
    for (const auto& child : node.children) {
      auto value = evaluate_constant(child);
      if (value.first.empty()) return {};
      if (element_type.empty()) element_type = value.first;
      else if (value.first != element_type && !(is_numeric(value.first) && is_numeric(element_type))) {
        diagnostics_.error("D2250", child.range, "compile-time array elements must have one type");
        return {};
      }
      values.push_back(std::move(value.second));
    }

    if (element_type.empty()) {
      diagnostics_.error("D2251", node.range, "compile-time array literal cannot be empty without a contextual type");
      return {};
    }
    return {element_type + "[" + std::to_string(values.size()) + "]", encode_aggregate(values)};
  }

  if (node.kind == SyntaxKind::tuple_expression) {
    std::vector<std::string> values;
    std::string type = "(";
    for (std::size_t index = 0; index < node.children.size(); ++index) {
      auto value = evaluate_constant(node.children[index]);
      if (value.first.empty()) return {};
      if (index != 0) type += ",";
      type += value.first;
      values.push_back(std::move(value.second));
    }
    type += ")";
    return {type, encode_aggregate(values)};
  }

  if (node.kind == SyntaxKind::struct_expression) {
    const auto [base, arguments] = split_generic_name(node.label);
    const auto layout = type_layouts_.find(arguments.empty() ? node.label : base);
    if (layout == type_layouts_.end()) {
      diagnostics_.error("D2257", node.range, "unknown compile-time struct type '" + node.label + "'");
      return {};
    }
    std::unordered_map<std::string, std::pair<std::string, std::string>> initialized;
    for (const auto& field : node.children) {
      if (field.children.empty()) {
        diagnostics_.error("D2256", field.range, "compile-time struct field requires a value");
        return {};
      }

      if (initialized.contains(field.label)) {
        diagnostics_.error("D2256", field.range, "duplicate compile-time struct field '" + field.label + "'");
        return {};
      }
      auto value = evaluate_constant(field.children.front());
      if (value.first.empty()) return {};
      initialized.emplace(field.label, std::move(value));
    }
    std::unordered_map<std::string, std::string> substitutions;
    if (!arguments.empty()) {
      const auto parameters = generic_types_.find(base);
      if (parameters == generic_types_.end() || parameters->second.size() != arguments.size()) {
        diagnostics_.error("D2257", node.range, "compile-time generic struct initializer has the wrong number of type arguments");
        return {};
      }

      for (std::size_t index = 0; index < arguments.size(); ++index)
        substitutions.emplace(parameters->second[index], arguments[index]);
    }
    std::vector<std::string> values;
    values.reserve(layout->second.fields.size());
    for (const auto& expected_field : layout->second.fields) {
      const auto found = initialized.find(expected_field.name);
      if (found == initialized.end()) {
        diagnostics_.error("D2256", node.range, "missing compile-time struct field '" + expected_field.name + "'");
        return {};
      }
      const auto expected = substitutions.empty() ? expected_field.type_name
                                                   : substitute_generic_type(expected_field.type_name, substitutions);
      const auto& actual = found->second.first;
      if (actual != expected && !(is_numeric(actual) && is_numeric(expected))) {
        diagnostics_.error("D2258", node.range, "compile-time struct field '" + expected_field.name +
                           "' expects '" + expected + "', received '" + actual + "'");
        return {};
      }
      values.push_back(found->second.second);
    }

    if (initialized.size() != layout->second.fields.size()) {
      diagnostics_.error("D2256", node.range, "compile-time struct initializer contains an unknown field");
      return {};
    }
    return {node.label, encode_aggregate(values)};
  }

  if (node.kind == SyntaxKind::index_expression && node.children.size() == 2) {
    auto aggregate = evaluate_constant(node.children[0]);
    auto index = evaluate_constant(node.children[1]);
    if (!is_numeric(index.first)) {
      diagnostics_.error("D2252", node.children[1].range, "compile-time aggregate index must be an integer");
      return {};
    }
    try {
      const auto parsed = std::stoll(index.second);
      const auto array = parse_fixed_array_type(aggregate.first);
      if (!array || parsed < 0 || static_cast<std::uint64_t>(parsed) >= array->length) {
        diagnostics_.error("D2253", node.range, "compile-time array index is out of range");
        return {};
      }
      const auto values = decode_aggregate(aggregate.second);
      return {array->element_type, values[static_cast<std::size_t>(parsed)]};
    } catch (...) {
      diagnostics_.error("D2252", node.children[1].range, "compile-time aggregate index must be an integer");
      return {};
    }
  }

  if (node.kind == SyntaxKind::member_expression && node.children.size() == 1) {
    auto aggregate = evaluate_constant(node.children.front());
    if (const auto enumeration = enum_types_.find(aggregate.first); enumeration != enum_types_.end()) {
      const auto values = decode_aggregate(aggregate.second);
      if (values.size() < 2) {
        diagnostics_.error("D2262", node.range, "malformed compile-time enum value");
        return {};
      }

      if (node.label == "discriminant") return {"i64", values[0]};
      if (node.label == "variant") return {"string", "\"" + values[1] + "\""};
      try {
        const auto index = std::stoll(node.label);
        if (index < 0 || static_cast<std::size_t>(index) + 2 >= values.size()) throw std::out_of_range("enum-payload");
        const auto variant = std::find_if(enumeration->second.variants.begin(), enumeration->second.variants.end(),
            [&](const HirEnumVariant& candidate) { return candidate.name == values[1]; });
        if (variant == enumeration->second.variants.end() || static_cast<std::size_t>(index) >= variant->payload_types.size())
          throw std::out_of_range("enum-payload-type");
        return {variant->payload_types[static_cast<std::size_t>(index)], values[static_cast<std::size_t>(index) + 2]};
      } catch (...) {
        diagnostics_.error("D2263", node.range, "compile-time enum member must be 'variant', 'discriminant', or a valid payload index");
        return {};
      }
    }

    if (const auto structure = type_layouts_.find(aggregate.first); structure != type_layouts_.end()) {
      const auto field = std::find_if(structure->second.fields.begin(), structure->second.fields.end(),
                                      [&](const HirField& candidate) { return candidate.name == node.label; });
      if (field == structure->second.fields.end()) {
        diagnostics_.error("D2255", node.range, "compile-time struct value has no field '" + node.label + "'");
        return {};
      }
      const auto index = static_cast<std::size_t>(std::distance(structure->second.fields.begin(), field));
      const auto values = decode_aggregate(aggregate.second);
      if (index >= values.size()) {
        diagnostics_.error("D2256", node.range, "malformed compile-time struct value");
        return {};
      }
      return {field->type_name, values[index]};
    }
    const auto types = split_tuple_type(aggregate.first);
    try {
      const auto index = std::stoll(node.label);
      if (index < 0 || static_cast<std::size_t>(index) >= types.size()) throw std::out_of_range("tuple");
      const auto values = decode_aggregate(aggregate.second);
      if (static_cast<std::size_t>(index) >= values.size()) throw std::out_of_range("tuple-value");
      return {types[static_cast<std::size_t>(index)], values[static_cast<std::size_t>(index)]};
    } catch (...) {
      diagnostics_.error("D2254", node.range, "compile-time tuple member must be a valid positional index");
      return {};
    }
  }

  if (node.kind == SyntaxKind::call_expression && !node.children.empty()) {
    const auto& callee_node = node.children.front();
    if (callee_node.kind == SyntaxKind::member_expression && !callee_node.children.empty() &&
        callee_node.children.front().kind == SyntaxKind::name_expression) {
      const auto enum_name = callee_node.children.front().label;
      if (const auto* enumeration = resolve_enum(enum_name); enumeration != nullptr) {
        const auto variant = std::find_if(enumeration->variants.begin(), enumeration->variants.end(),
            [&](const HirEnumVariant& candidate) { return candidate.name == callee_node.label; });
        if (variant == enumeration->variants.end()) {
          diagnostics_.error("D2259", callee_node.range, "compile-time enum has no variant '" + callee_node.label + "'");
          return {};
        }

        if (node.children.size() - 1 != variant->payload_types.size()) {
          diagnostics_.error("D2260", node.range, "compile-time enum constructor argument count mismatch");
          return {};
        }
        std::vector<std::string> values{std::to_string(variant->discriminant), variant->name};
        for (std::size_t index = 1; index < node.children.size(); ++index) {
          auto value = evaluate_constant(node.children[index]);
          const auto& expected = variant->payload_types[index - 1];
          if (value.first != expected && !(is_numeric(value.first) && is_numeric(expected))) {
            diagnostics_.error("D2261", node.children[index].range, "compile-time enum payload expects '" + expected + "', received '" + value.first + "'");
            return {};
          }
          values.push_back(std::move(value.second));
        }
        return {enum_name, encode_aggregate(values)};
      }
    }

    if (callee_node.kind == SyntaxKind::name_expression) {
      const auto& callee = callee_node.label;
      if (const auto structure = type_layouts_.find(callee); structure != type_layouts_.end()) {
        if (node.children.size() - 1 != structure->second.fields.size()) {
          diagnostics_.error("D2257", node.range, "compile-time constructor '" + callee + "' argument count mismatch");
          return {};
        }
        std::vector<std::string> values;
        for (std::size_t index = 1; index < node.children.size(); ++index) {
          auto value = evaluate_constant(node.children[index]);
          const auto& expected = structure->second.fields[index - 1].type_name;
          if (value.first != expected && !(is_numeric(value.first) && is_numeric(expected))) {
            diagnostics_.error("D2258", node.children[index].range, "compile-time constructor field expects '" + expected + "', received '" + value.first + "'");
            return {};
          }
          values.push_back(std::move(value.second));
        }
        return {callee, encode_aggregate(values)};
      }

      if (const auto function = const_functions_.find(callee); function != const_functions_.end()) {
      if (++const_eval_depth_ > 128) {
        --const_eval_depth_;
        diagnostics_.error("D2219", node.range, "compile-time evaluation recursion limit exceeded");
        return {};
      }
      std::vector<const SyntaxNode*> parameters;
      const SyntaxNode* body = nullptr;
      for (const auto& child : function->second.children) {
        if (child.kind == SyntaxKind::parameter) parameters.push_back(&child);
        else if (child.kind == SyntaxKind::block_statement) body = &child;
      }

      if (node.children.size() - 1 != parameters.size()) {
        --const_eval_depth_;
        diagnostics_.error("D2220", node.range, "const function argument count mismatch");
        return {};
      }
      const auto saved_constants = constants_;
      for (std::size_t index = 0; index < parameters.size(); ++index) {
        const auto [type_name, name] = split_typed_name(parameters[index]->label);
        auto value = evaluate_constant(node.children[index + 1]);
        if (!value.first.empty() && value.first != type_name && !(is_numeric(value.first) && is_numeric(type_name)))
          diagnostics_.error("D2221", node.children[index + 1].range, "const function argument type mismatch");
        constants_[name] = {type_name, value.second};
      }

      struct ConstExecResult {
        enum class Flow { normal, returned, break_loop, continue_loop } flow = Flow::normal;
        std::pair<std::string, std::string> value;
      };
      std::function<ConstExecResult(const SyntaxNode&)> execute;
      execute = [&](const SyntaxNode& statement) -> ConstExecResult {
        if (++comptime_steps_ > 100000) {
          diagnostics_.error("D2240", statement.range, "compile-time execution exceeded 100000 steps");
          return {};
        }
        if (statement.kind == SyntaxKind::block_statement) {
          for (const auto& child : statement.children) {
            auto result = execute(child);
            if (result.flow != ConstExecResult::Flow::normal) return result;
          }
          return {};
        }
        if (statement.kind == SyntaxKind::const_declaration || statement.kind == SyntaxKind::variable_declaration) {
          const auto [type_name, name] = split_typed_name(statement.label);
          if (statement.children.empty()) {
            diagnostics_.error("D2208", statement.range, "compile-time variable requires an initializer");
            return {};
          }
          auto value = evaluate_constant(statement.children.front());
          if (!value.first.empty() && value.first != type_name && !(is_numeric(value.first) && is_numeric(type_name)))
            diagnostics_.error("D2209", statement.range, "compile-time variable type mismatch");
          constants_[name] = {type_name, value.second};
          return {};
        }
        if (statement.kind == SyntaxKind::return_statement) {
          return {ConstExecResult::Flow::returned, statement.children.empty() ? std::pair<std::string, std::string>{"void", {}} : evaluate_constant(statement.children.front())};
        }
        if (statement.kind == SyntaxKind::break_statement) return {ConstExecResult::Flow::break_loop, {}};
        if (statement.kind == SyntaxKind::continue_statement) return {ConstExecResult::Flow::continue_loop, {}};
        if (statement.kind == SyntaxKind::if_statement && statement.children.size() >= 2) {
          auto condition = evaluate_constant(statement.children.front());
          if (condition.first != "bool") { diagnostics_.error("D2222", statement.children.front().range, "const function if condition requires bool"); return {}; }
          if (condition.second == "true") return execute(statement.children[1]);
          if (statement.children.size() > 2) return execute(statement.children[2]);
          return {};
        }
        if (statement.kind == SyntaxKind::while_statement && statement.children.size() == 2) {
          while (true) {
            auto condition = evaluate_constant(statement.children[0]);
            if (condition.first != "bool") { diagnostics_.error("D2222", statement.children[0].range, "const function while condition requires bool"); return {}; }
            if (condition.second != "true") break;
            auto result = execute(statement.children[1]);
            if (result.flow == ConstExecResult::Flow::returned) return result;
            if (result.flow == ConstExecResult::Flow::break_loop) break;
            if (result.flow == ConstExecResult::Flow::continue_loop) continue;
          }
          return {};
        }
        if (statement.kind == SyntaxKind::for_statement && statement.children.size() == 2 && !statement.label.empty()) {
          const auto& range = statement.children[0];
          if (range.kind != SyntaxKind::binary_expression || (range.label != ".." && range.label != "..=")) {
            diagnostics_.error("D2241", range.range, "const function for requires an integer range");
            return {};
          }
          auto begin = evaluate_constant(range.children[0]);
          auto end = evaluate_constant(range.children[1]);
          if (!is_numeric(begin.first) || !is_numeric(end.first)) { diagnostics_.error("D2241", range.range, "const function for range bounds must be integers"); return {}; }
          const auto first = std::stoll(begin.second, nullptr, 0);
          const auto last = std::stoll(end.second, nullptr, 0);
          const auto old = constants_.find(statement.label);
          const std::optional<std::pair<std::string, std::string>> saved = old == constants_.end() ? std::nullopt : std::optional(old->second);
          const auto limit = range.label == "..=" ? last + 1 : last;
          for (auto index = first; index < limit; ++index) {
            constants_[statement.label] = {"i64", std::to_string(index)};
            auto result = execute(statement.children[1]);
            if (result.flow == ConstExecResult::Flow::returned) { if (saved) constants_[statement.label] = *saved; else constants_.erase(statement.label); return result; }
            if (result.flow == ConstExecResult::Flow::break_loop) break;
          }
          if (saved) constants_[statement.label] = *saved; else constants_.erase(statement.label);
          return {};
        }
        if (statement.kind == SyntaxKind::expression_statement && !statement.children.empty()) {
          const auto& expression = statement.children.front();
          if (expression.kind == SyntaxKind::assignment_expression && expression.children.size() == 2) {
            auto result = evaluate_constant(expression.children[1]);
            if (expression.label != "=") {
              SyntaxNode binary; binary.kind = SyntaxKind::binary_expression; binary.range = expression.range;
              binary.label = expression.label.substr(0, expression.label.size() - 1);
              binary.children = {expression.children[0], expression.children[1]};
              result = evaluate_constant(binary);
            }
            (void)assign_comptime_place(expression.children[0], result);
            return {};
          }
          (void)evaluate_constant(expression);
          return {};
        }
        diagnostics_.error("D2223", statement.range, "statement is not allowed in a const function");
        return {};
      };
      auto execution = body ? execute(*body) : ConstExecResult{};
      auto result = execution.flow == ConstExecResult::Flow::returned ? execution.value : std::pair<std::string, std::string>{};
      constants_ = saved_constants;
      --const_eval_depth_;
      if (result.first.empty()) diagnostics_.error("D2224", node.range, "const function did not produce a value");
      return result;
    }
    const auto open = callee.find('<');
    if (open != std::string::npos && !callee.empty() && callee.back() == '>') {
      const auto operation = callee.substr(0, open);
      const auto generic_text = callee.substr(open + 1, callee.size() - open - 2);
      const auto split_reflection_arguments = [](const std::string& text) {
        const auto trim_argument = [](std::string value) {
          const auto first = value.find_first_not_of(" \t\r\n");
          if (first == std::string::npos) return std::string{};
          const auto last = value.find_last_not_of(" \t\r\n");
          return value.substr(first, last - first + 1);
        };
        std::vector<std::string> arguments;
        std::size_t start = 0;
        int depth = 0;
        for (std::size_t index = 0; index < text.size(); ++index) {
          const char character = text[index];
          if (character == '<' || character == '[' || character == '(') ++depth;
          else if (character == '>' || character == ']' || character == ')') --depth;
          else if (character == ',' && depth == 0) {
            arguments.push_back(trim_argument(text.substr(start, index - start)));
            start = index + 1;
          }
        }
        arguments.push_back(trim_argument(text.substr(start)));
        return arguments;
      };
      const auto reflection_arguments = split_reflection_arguments(generic_text);
      const auto type_name = reflection_arguments.empty() ? std::string{} : reflection_arguments.front();
      const bool implementation_reflection =
          operation == "implements_trait" ||
          operation == "impl_associated_type_count" || operation == "impl_associated_type_name" ||
          operation == "impl_associated_type_binding_name" ||
          operation == "impl_associated_const_count" || operation == "impl_associated_const_name" ||
          operation == "impl_associated_const_type_name" || operation == "impl_associated_const_value";
      if (implementation_reflection) {
        if (reflection_arguments.size() != 2 || !type_exists(reflection_arguments[0]) ||
            !trait_contracts_.contains(reflection_arguments[1])) {
          diagnostics_.error("D2285", node.range, operation + "<T, Trait>() requires a concrete type and a trait");
          return {};
        }
        const auto& target_type = reflection_arguments[0];
        const auto& trait_name = reflection_arguments[1];
        if (operation == "implements_trait")
          return {"bool", implements_trait(target_type, trait_name) ? "true" : "false"};
        if (!implements_trait(target_type, trait_name)) {
          diagnostics_.error("D2286", node.range, "type '" + target_type + "' does not implement trait '" + trait_name + "'");
          return {};
        }
        std::vector<std::pair<std::string, std::string>> type_bindings;
        if (const auto declared = trait_associated_types_.find(trait_name); declared != trait_associated_types_.end()) {
          for (const auto& [name, ignored] : declared->second) {
            if (const auto binding = resolve_associated_type_binding(target_type, trait_name, name))
              type_bindings.emplace_back(name, normalize_associated_type(*binding));
          }
        }
        std::sort(type_bindings.begin(), type_bindings.end());
        std::vector<std::pair<std::string, std::pair<std::string, std::string>>> const_bindings;
        if (const auto declared = trait_associated_constants_.find(trait_name); declared != trait_associated_constants_.end()) {
          for (const auto& [name, ignored] : declared->second) {
            if (const auto binding = resolve_associated_const_binding(target_type, trait_name, name))
              const_bindings.emplace_back(name, *binding);
          }
        }
        std::sort(const_bindings.begin(), const_bindings.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
        const auto reflected_index = [&]() -> std::optional<std::size_t> {
          if (node.children.size() != 2) return std::nullopt;
          auto value = evaluate_constant(node.children[1]);
          if (!is_numeric(value.first)) return std::nullopt;
          try {
            const auto parsed = std::stoll(value.second);
            if (parsed < 0) return std::nullopt;
            return static_cast<std::size_t>(parsed);
          } catch (...) { return std::nullopt; }
        };
        if (operation == "impl_associated_type_count") return {"usize", std::to_string(type_bindings.size())};
        if (operation == "impl_associated_const_count") return {"usize", std::to_string(const_bindings.size())};
        const auto index = reflected_index();
        if (operation.starts_with("impl_associated_type_")) {
          if (!index || *index >= type_bindings.size()) {
            diagnostics_.error("D2287", node.range, "associated type binding index is out of range for '" + target_type + " as " + trait_name + "'");
            return {};
          }
          return {"string", "\"" + (operation == "impl_associated_type_name" ? type_bindings[*index].first : type_bindings[*index].second) + "\""};
        }
        if (!index || *index >= const_bindings.size()) {
          diagnostics_.error("D2288", node.range, "associated constant binding index is out of range for '" + target_type + " as " + trait_name + "'");
          return {};
        }
        const auto& binding = const_bindings[*index];
        if (operation == "impl_associated_const_name") return {"string", "\"" + binding.first + "\""};
        if (operation == "impl_associated_const_type_name") return {"string", "\"" + binding.second.first + "\""};
        return binding.second;
      }
      const bool generic_reflected_type = current_generic_bounds_.contains(type_name);
      if (reflection_arguments.size() != 1 || (!type_exists(type_name) && !generic_reflected_type)) {
        diagnostics_.error("D2217", node.range, "unknown reflected type '" + generic_text + "'");
        return {};
      }
      // Generic templates are analyzed before their concrete specializations are
      // materialized. Reflection on a template parameter therefore carries only
      // its result type during the template pass; specialize_node rewrites T to
      // the concrete argument and the generated method is analyzed again with
      // the exact layout before MIR lowering.
      if (generic_reflected_type) {
        if (operation == "size_of" || operation == "align_of") return {"usize", "8"};
        if (operation == "is_copy" || operation == "is_clone" || operation == "needs_drop") return {"bool", "false"};
      }
      std::function<std::pair<std::uint64_t, std::uint32_t>(const std::string&)> layout_of;
      layout_of = [&](const std::string& type) -> std::pair<std::uint64_t, std::uint32_t> {
        if (const auto found = type_layouts_.find(type); found != type_layouts_.end()) return {found->second.size, found->second.alignment};
        if (const auto found = enum_types_.find(type); found != enum_types_.end()) return {found->second.size, found->second.alignment};
        if (const auto array = parse_fixed_array_type(type)) {
          const auto element = layout_of(array->element_type);
          return {element.first * array->length, element.second};
        }
        if (type == "bool" || type == "i8" || type == "u8" || type == "byte") return {1, 1};
        if (type == "i16" || type == "u16" || type == "f16") return {2, 2};
        if (type == "i32" || type == "u32" || type == "f32" || type == "char") return {4, 4};
        if (type == "void") return {0, 1};
        return {8, 8};
      };
      const auto [size, alignment] = layout_of(type_name);
      if (operation == "size_of") return {"usize", std::to_string(size)};
      if (operation == "align_of") return {"usize", std::to_string(alignment)};
      if (operation == "field_count") {
        const auto found = type_layouts_.find(type_name);
        return {"usize", std::to_string(found == type_layouts_.end() ? 0 : found->second.fields.size())};
      }

      if (operation == "variant_count") {
        const auto found = enum_types_.find(type_name);
        return {"usize", std::to_string(found == enum_types_.end() ? 0 : found->second.variants.size())};
      }

      if (operation == "attribute_count") {
        const auto found = type_layouts_.find(type_name);
        return {"usize", std::to_string(found == type_layouts_.end() ? 0 : found->second.attributes.size())};
      }

      if (operation == "payload_offset") {
        const auto found = enum_types_.find(type_name);
        if (found == enum_types_.end()) {
          diagnostics_.error("D2232", node.range, "payload_offset<T>() requires a payload enum type");
          return {};
        }
        return {"usize", std::to_string(found->second.payload_offset)};
      }

      if (operation == "is_copy") return {"bool", is_copy_type(type_name) ? "true" : "false"};
      if (operation == "is_clone") return {"bool", is_clone_type(type_name) ? "true" : "false"};
      if (operation == "needs_drop") {
        const bool needs_drop = !is_copy_type(type_name) &&
            (explicit_drop_types_.contains(type_name) || type_layouts_.contains(type_name) || enum_types_.contains(type_name));
        return {"bool", needs_drop ? "true" : "false"};
      }
      const auto string_argument = [&]() -> std::optional<std::string> {
        if (node.children.size() != 2) return std::nullopt;
        auto value = evaluate_constant(node.children[1]);
        if (value.first != "string" || value.second.size() < 2 || value.second.front() != '"' || value.second.back() != '"') return std::nullopt;
        return value.second.substr(1, value.second.size() - 2);
      };
      if (operation == "field_offset" || operation == "field_size" || operation == "field_align") {
        const auto field_name = string_argument();
        if (!field_name) {
          diagnostics_.error("D2233", node.range, operation + "<T>() expects one string field name");
          return {};
        }
        const auto found = type_layouts_.find(type_name);
        if (found == type_layouts_.end()) {
          diagnostics_.error("D2234", node.range, operation + "<T>() requires a structure type");
          return {};
        }
        const auto field = std::find_if(found->second.fields.begin(), found->second.fields.end(),
            [&](const HirField& candidate) { return candidate.name == *field_name; });
        if (field == found->second.fields.end()) {
          diagnostics_.error("D2235", node.range, "unknown reflected field '" + *field_name + "' on type '" + type_name + "'");
          return {};
        }
        if (operation == "field_offset") return {"usize", std::to_string(field->offset)};
        if (operation == "field_size") return {"usize", std::to_string(field->size)};
        return {"usize", std::to_string(field->alignment)};
      }

      if (operation == "has_attribute") {
        const auto attribute = string_argument();
        if (!attribute) {
          diagnostics_.error("D2236", node.range, "has_attribute<T>() expects one string attribute name");
          return {};
        }
        const auto found = type_layouts_.find(type_name);
        if (found == type_layouts_.end()) return {"bool", "false"};
        const auto present = std::any_of(found->second.attributes.begin(), found->second.attributes.end(),
            [&](const std::string& candidate) {
              return candidate == *attribute || candidate.starts_with(*attribute + "(");
            });
        return {"bool", present ? "true" : "false"};
      }
      const auto index_argument = [&]() -> std::optional<std::size_t> {
        if (node.children.size() != 2) return std::nullopt;
        auto value = evaluate_constant(node.children[1]);
        if (value.first.empty() || !is_numeric(value.first)) return std::nullopt;
        try {
          const auto parsed = std::stoll(value.second);
          if (parsed < 0) return std::nullopt;
          return static_cast<std::size_t>(parsed);
        } catch (...) {
          return std::nullopt;
        }
      };
      if (operation == "field_name" || operation == "field_type_name") {
        const auto index = index_argument();
        if (!index) {
          diagnostics_.error("D2240", node.range, operation + "<T>() expects one nonnegative integer index");
          return {};
        }
        const auto found = type_layouts_.find(type_name);
        if (found == type_layouts_.end()) {
          diagnostics_.error("D2241", node.range, operation + "<T>() requires a structure type");
          return {};
        }
        if (*index >= found->second.fields.size()) {
          diagnostics_.error("D2242", node.range, "reflected field index is out of range for type '" + type_name + "'");
          return {};
        }
        const auto& field = found->second.fields[*index];
        return {"string", "\"" + (operation == "field_name" ? field.name : field.type_name) + "\""};
      }
      const auto reflected_methods = [&]() {
        std::vector<TraitMethodContract> methods;
        const auto prefix = type_name + "::";
        for (const auto& [qualified, contract] : inherent_method_contracts_) {
          if (qualified.starts_with(prefix)) methods.push_back(contract);
        }
        std::sort(methods.begin(), methods.end(), [](const TraitMethodContract& left, const TraitMethodContract& right) {
          return left.name < right.name;
        });
        return methods;
      };
      const auto reflected_trait_methods = [&]() {
        std::vector<TraitMethodContract> methods;
        if (const auto found = trait_contracts_.find(type_name); found != trait_contracts_.end()) methods = found->second;
        std::sort(methods.begin(), methods.end(), [](const TraitMethodContract& left, const TraitMethodContract& right) {
          return left.name < right.name;
        });
        return methods;
      };
      const auto reflected_associated_types = [&]() {
        std::vector<std::string> names;
        if (const auto found = trait_associated_types_.find(type_name); found != trait_associated_types_.end()) {
          for (const auto& [name, ignored] : found->second) names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        return names;
      };
      const auto reflected_associated_constants = [&]() {
        std::vector<std::pair<std::string, std::string>> items;
        if (const auto found = trait_associated_constants_.find(type_name); found != trait_associated_constants_.end()) {
          for (const auto& [name, item_type] : found->second) items.emplace_back(name, item_type);
        }
        std::sort(items.begin(), items.end());
        return items;
      };
      if (operation == "trait_method_count") {
        if (!trait_contracts_.contains(type_name)) {
          diagnostics_.error("D2275", node.range, "trait_method_count<T>() requires a trait");
          return {};
        }
        return {"usize", std::to_string(reflected_trait_methods().size())};
      }

      if (operation == "trait_method_name" || operation == "trait_method_return_type_name" ||
          operation == "trait_method_parameter_count") {
        const auto index = index_argument();
        if (!index) {
          diagnostics_.error("D2276", node.range, operation + "<T>() expects one nonnegative integer method index");
          return {};
        }
        const auto methods = reflected_trait_methods();
        if (!trait_contracts_.contains(type_name) || *index >= methods.size()) {
          diagnostics_.error("D2277", node.range, "reflected trait method index is out of range for trait '" + type_name + "'");
          return {};
        }
        const auto& method = methods[*index];
        if (operation == "trait_method_name") return {"string", "\"" + method.name + "\""};
        if (operation == "trait_method_return_type_name") return {"string", "\"" + method.return_type + "\""};
        return {"usize", std::to_string(method.parameter_types.size())};
      }

      if (operation == "trait_method_parameter_type_name") {
        if (node.children.size() != 3) {
          diagnostics_.error("D2278", node.range, "trait_method_parameter_type_name<T>() expects method and parameter indices");
          return {};
        }
        auto method_value = evaluate_constant(node.children[1]);
        auto parameter_value = evaluate_constant(node.children[2]);
        try {
          if (!is_numeric(method_value.first) || !is_numeric(parameter_value.first)) throw std::invalid_argument("index");
          const auto method_index = std::stoll(method_value.second);
          const auto parameter_index = std::stoll(parameter_value.second);
          const auto methods = reflected_trait_methods();
          if (method_index < 0 || parameter_index < 0 ||
              static_cast<std::size_t>(method_index) >= methods.size() ||
              static_cast<std::size_t>(parameter_index) >= methods[method_index].parameter_types.size()) throw std::out_of_range("index");
          return {"string", "\"" + methods[method_index].parameter_types[parameter_index] + "\""};
        } catch (...) {
          diagnostics_.error("D2279", node.range, "reflected trait method parameter index is out of range for trait '" + type_name + "'");
          return {};
        }
      }

      if (operation == "trait_method_has_attribute") {
        if (node.children.size() != 3) {
          diagnostics_.error("D2280", node.range, "trait_method_has_attribute<T>() expects a method index and attribute name");
          return {};
        }
        auto method_value = evaluate_constant(node.children[1]);
        auto attribute_value = evaluate_constant(node.children[2]);
        try {
          if (!is_numeric(method_value.first) || attribute_value.first != "string" || attribute_value.second.size() < 2)
            throw std::invalid_argument("arguments");
          const auto method_index = std::stoll(method_value.second);
          const auto methods = reflected_trait_methods();
          if (method_index < 0 || static_cast<std::size_t>(method_index) >= methods.size()) throw std::out_of_range("index");
          const auto attribute = attribute_value.second.substr(1, attribute_value.second.size() - 2);
          return {"bool", methods[method_index].attributes.contains(attribute) ? "true" : "false"};
        } catch (...) {
          diagnostics_.error("D2280", node.range, "invalid trait method attribute reflection for trait '" + type_name + "'");
          return {};
        }
      }

      if (operation == "associated_type_count") {
        if (!trait_contracts_.contains(type_name)) { diagnostics_.error("D2281", node.range, "associated_type_count<T>() requires a trait"); return {}; }
        return {"usize", std::to_string(reflected_associated_types().size())};
      }

      if (operation == "associated_type_name") {
        const auto index = index_argument();
        const auto names = reflected_associated_types();
        if (!index || *index >= names.size()) { diagnostics_.error("D2282", node.range, "reflected associated type index is out of range for trait '" + type_name + "'"); return {}; }
        return {"string", "\"" + names[*index] + "\""};
      }

      if (operation == "associated_const_count") {
        if (!trait_contracts_.contains(type_name)) { diagnostics_.error("D2283", node.range, "associated_const_count<T>() requires a trait"); return {}; }
        return {"usize", std::to_string(reflected_associated_constants().size())};
      }

      if (operation == "associated_const_name" || operation == "associated_const_type_name") {
        const auto index = index_argument();
        const auto items = reflected_associated_constants();
        if (!index || *index >= items.size()) { diagnostics_.error("D2284", node.range, "reflected associated constant index is out of range for trait '" + type_name + "'"); return {}; }
        return {"string", "\"" + (operation == "associated_const_name" ? items[*index].first : items[*index].second) + "\""};
      }

      if (operation == "method_count") {
        return {"usize", std::to_string(reflected_methods().size())};
      }

      if (operation == "method_name" || operation == "method_return_type_name" ||
          operation == "method_parameter_count") {
        const auto index = index_argument();
        if (!index) {
          diagnostics_.error("D2270", node.range, operation + "<T>() expects one nonnegative integer method index");
          return {};
        }
        const auto methods = reflected_methods();
        if (*index >= methods.size()) {
          diagnostics_.error("D2271", node.range, "reflected method index is out of range for type '" + type_name + "'");
          return {};
        }
        const auto& method = methods[*index];
        if (operation == "method_name") return {"string", "\"" + method.name + "\""};
        if (operation == "method_return_type_name") return {"string", "\"" + method.return_type + "\""};
        return {"usize", std::to_string(method.parameter_types.size())};
      }

      if (operation == "method_parameter_type_name") {
        if (node.children.size() != 3) {
          diagnostics_.error("D2272", node.range, "method_parameter_type_name<T>() expects method and parameter indices");
          return {};
        }
        auto method_value = evaluate_constant(node.children[1]);
        auto parameter_value = evaluate_constant(node.children[2]);
        if (!is_numeric(method_value.first) || !is_numeric(parameter_value.first)) {
          diagnostics_.error("D2272", node.range, "method_parameter_type_name<T>() expects nonnegative integer indices");
          return {};
        }
        std::size_t method_index = 0;
        std::size_t parameter_index = 0;
        try {
          const auto parsed_method = std::stoll(method_value.second);
          const auto parsed_parameter = std::stoll(parameter_value.second);
          if (parsed_method < 0 || parsed_parameter < 0) throw std::out_of_range("negative");
          method_index = static_cast<std::size_t>(parsed_method);
          parameter_index = static_cast<std::size_t>(parsed_parameter);
        } catch (...) {
          diagnostics_.error("D2272", node.range, "method_parameter_type_name<T>() expects nonnegative integer indices");
          return {};
        }
        const auto methods = reflected_methods();
        if (method_index >= methods.size() || parameter_index >= methods[method_index].parameter_types.size()) {
          diagnostics_.error("D2273", node.range, "reflected method parameter index is out of range for type '" + type_name + "'");
          return {};
        }
        return {"string", "\"" + methods[method_index].parameter_types[parameter_index] + "\""};
      }

      if (operation == "method_has_attribute") {
        if (node.children.size() != 3) {
          diagnostics_.error("D2274", node.range, "method_has_attribute<T>() expects a method index and attribute name");
          return {};
        }
        auto method_value = evaluate_constant(node.children[1]);
        auto attribute_value = evaluate_constant(node.children[2]);
        if (!is_numeric(method_value.first) || attribute_value.first != "string" ||
            attribute_value.second.size() < 2 || attribute_value.second.front() != '"' || attribute_value.second.back() != '"') {
          diagnostics_.error("D2274", node.range, "method_has_attribute<T>() expects a method index and string attribute name");
          return {};
        }
        std::size_t method_index = 0;
        try {
          const auto parsed = std::stoll(method_value.second);
          if (parsed < 0) throw std::out_of_range("negative");
          method_index = static_cast<std::size_t>(parsed);
        } catch (...) {
          diagnostics_.error("D2274", node.range, "method_has_attribute<T>() expects a nonnegative method index");
          return {};
        }
        const auto methods = reflected_methods();
        if (method_index >= methods.size()) {
          diagnostics_.error("D2271", node.range, "reflected method index is out of range for type '" + type_name + "'");
          return {};
        }
        const auto attribute = attribute_value.second.substr(1, attribute_value.second.size() - 2);
        const auto present = methods[method_index].attributes.contains(attribute);
        return {"bool", present ? "true" : "false"};
      }

      if (operation == "attribute_name") {
        const auto index = index_argument();
        if (!index) {
          diagnostics_.error("D2243", node.range, "attribute_name<T>() expects one nonnegative integer index");
          return {};
        }
        const auto found = type_layouts_.find(type_name);
        if (found == type_layouts_.end() || *index >= found->second.attributes.size()) {
          diagnostics_.error("D2244", node.range, "reflected attribute index is out of range for type '" + type_name + "'");
          return {};
        }
        return {"string", "\"" + found->second.attributes[*index] + "\""};
      }

      if (operation == "variant_name" || operation == "variant_payload_count") {
        const auto index = index_argument();
        if (!index) {
          diagnostics_.error("D2245", node.range, operation + "<T>() expects one nonnegative integer index");
          return {};
        }
        const auto found = enum_types_.find(type_name);
        if (found == enum_types_.end()) {
          diagnostics_.error("D2246", node.range, operation + "<T>() requires an enum type");
          return {};
        }
        if (*index >= found->second.variants.size()) {
          diagnostics_.error("D2247", node.range, "reflected variant index is out of range for enum '" + type_name + "'");
          return {};
        }
        const auto& variant = found->second.variants[*index];
        if (operation == "variant_name") return {"string", "\"" + variant.name + "\""};
        return {"usize", std::to_string(variant.payload_types.size())};
      }

      if (operation == "variant_payload_type_name") {
        if (node.children.size() != 3) {
          diagnostics_.error("D2248", node.range, "variant_payload_type_name<T>() expects variant and payload indices");
          return {};
        }
        auto variant_value = evaluate_constant(node.children[1]);
        auto payload_value = evaluate_constant(node.children[2]);
        try {
          if (!is_numeric(variant_value.first) || !is_numeric(payload_value.first)) throw std::invalid_argument("index");
          const auto variant_index = std::stoll(variant_value.second);
          const auto payload_index = std::stoll(payload_value.second);
          const auto found = enum_types_.find(type_name);
          if (found == enum_types_.end() || variant_index < 0 || payload_index < 0 ||
              static_cast<std::size_t>(variant_index) >= found->second.variants.size() ||
              static_cast<std::size_t>(payload_index) >= found->second.variants[static_cast<std::size_t>(variant_index)].payload_types.size()) {
            diagnostics_.error("D2249", node.range, "reflected enum payload index is out of range for '" + type_name + "'");
            return {};
          }
          return {"string", "\"" + found->second.variants[static_cast<std::size_t>(variant_index)].payload_types[static_cast<std::size_t>(payload_index)] + "\""};
        } catch (...) {
          diagnostics_.error("D2248", node.range, "variant_payload_type_name<T>() expects nonnegative integer indices");
          return {};
        }
      }

      if (operation == "variant_discriminant") {
        const auto variant_name = string_argument();
        if (!variant_name) {
          diagnostics_.error("D2237", node.range, "variant_discriminant<T>() expects one string variant name");
          return {};
        }
        const auto found = enum_types_.find(type_name);
        if (found == enum_types_.end()) {
          diagnostics_.error("D2238", node.range, "variant_discriminant<T>() requires an enum type");
          return {};
        }
        const auto variant = std::find_if(found->second.variants.begin(), found->second.variants.end(),
            [&](const HirEnumVariant& candidate) { return candidate.name == *variant_name; });
        if (variant == found->second.variants.end()) {
          diagnostics_.error("D2239", node.range, "unknown reflected variant '" + *variant_name + "' on enum '" + type_name + "'");
          return {};
        }
        return {"i64", std::to_string(variant->discriminant)};
      }

      if (operation == "type_name") return {"string", "\"" + type_name + "\""};
    }
    }
  }

  if (node.kind == SyntaxKind::parenthesized_expression && !node.children.empty()) return evaluate_constant(node.children.front());
  if (node.kind == SyntaxKind::unary_expression && !node.children.empty()) {
    auto value = evaluate_constant(node.children.front());
    if (value.first.empty()) return {};
    try {
      if (node.label == "!") {
        if (value.first != "bool") diagnostics_.error("D2202", node.range, "compile-time '!' requires bool");
        return {"bool", value.second == "true" ? "false" : "true"};
      }
      const auto number = std::stoll(value.second, nullptr, 0);
      if (node.label == "-") return {value.first, std::to_string(-number)};
      if (node.label == "+") return value;
      if (node.label == "~") return {value.first, std::to_string(~number)};
    } catch (...) {
      diagnostics_.error("D2203", node.range, "invalid compile-time unary operand");
    }
    return {};
  }

  if (node.kind == SyntaxKind::binary_expression && node.children.size() == 2) {
    auto left = evaluate_constant(node.children[0]);
    auto right = evaluate_constant(node.children[1]);
    if (left.first.empty() || right.first.empty()) return {};
    if (node.label == "&&" || node.label == "||") {
      if (left.first != "bool" || right.first != "bool") diagnostics_.error("D2204", node.range, "compile-time logical operators require bool");
      const bool l = left.second == "true", r = right.second == "true";
      return {"bool", (node.label == "&&" ? l && r : l || r) ? "true" : "false"};
    }

    if (left.first == right.first && (node.label == "==" || node.label == "!=") &&
        (left.first == "string" || left.first == "bool" || parse_fixed_array_type(left.first) ||
         type_layouts_.contains(left.first) || enum_types_.contains(left.first) ||
         (!left.first.empty() && left.first.front() == '(' && left.first.back() == ')'))) {
      const bool equal = left.second == right.second;
      return {"bool", (node.label == "==" ? equal : !equal) ? "true" : "false"};
    }
    try {
      const auto l = std::stoll(left.second, nullptr, 0);
      const auto r = std::stoll(right.second, nullptr, 0);
      if (node.label == "+") return {left.first, std::to_string(l + r)};
      if (node.label == "-") return {left.first, std::to_string(l - r)};
      if (node.label == "*") return {left.first, std::to_string(l * r)};
      if (node.label == "/") { if (r == 0) { diagnostics_.error("D2205", node.range, "division by zero during compile-time evaluation"); return {}; } return {left.first, std::to_string(l / r)}; }
      if (node.label == "%") { if (r == 0) { diagnostics_.error("D2205", node.range, "remainder by zero during compile-time evaluation"); return {}; } return {left.first, std::to_string(l % r)}; }
      if (node.label == "<<") return {left.first, std::to_string(l << r)};
      if (node.label == ">>") return {left.first, std::to_string(l >> r)};
      if (node.label == "&") return {left.first, std::to_string(l & r)};
      if (node.label == "|") return {left.first, std::to_string(l | r)};
      if (node.label == "^") return {left.first, std::to_string(l ^ r)};
      if (node.label == "==") return {"bool", l == r ? "true" : "false"};
      if (node.label == "!=") return {"bool", l != r ? "true" : "false"};
      if (node.label == "<") return {"bool", l < r ? "true" : "false"};
      if (node.label == "<=") return {"bool", l <= r ? "true" : "false"};
      if (node.label == ">") return {"bool", l > r ? "true" : "false"};
      if (node.label == ">=") return {"bool", l >= r ? "true" : "false"};
    } catch (...) {
      diagnostics_.error("D2206", node.range, "invalid compile-time binary operands");
    }
    return {};
  }
  diagnostics_.error("D2207", node.range, "expression is not compile-time evaluable");
  return {};
}

SemanticAnalyzer::ComptimeFlow SemanticAnalyzer::execute_comptime_block(const SyntaxNode& block) {
  std::vector<std::pair<std::string, std::optional<std::pair<std::string, std::string>>>> saved;
  for (const auto& statement : block.children) {
    if (++comptime_steps_ > 100000) {
      diagnostics_.error("D2240", statement.range, "compile-time execution exceeded 100000 steps");
      return ComptimeFlow::normal;
    }
    const auto flow = execute_comptime_statement(statement);
    if (flow != ComptimeFlow::normal) return flow;
  }
  return ComptimeFlow::normal;
}

bool SemanticAnalyzer::assign_comptime_place(
    const SyntaxNode& target, const std::pair<std::string, std::string>& value) {
  if (target.kind == SyntaxKind::name_expression) {
    const auto found = constants_.find(target.label);
    if (found == constants_.end()) {
      diagnostics_.error("D2264", target.range,
                         "assignment to unknown compile-time variable '" + target.label + "'");
      return false;
    }

    if (!value.first.empty() && value.first != found->second.first &&
        !(is_numeric(value.first) && is_numeric(found->second.first))) {
      diagnostics_.error("D2265", target.range, "compile-time assignment type mismatch");
      return false;
    }
    found->second.second = value.second;
    return true;
  }

  if (target.kind == SyntaxKind::member_expression && target.children.size() == 1) {
    auto aggregate = evaluate_constant(target.children.front());
    if (aggregate.first.empty()) return false;
    auto values = decode_aggregate(aggregate.second);
    std::size_t index = 0;
    std::string element_type;

    if (const auto structure = type_layouts_.find(aggregate.first); structure != type_layouts_.end()) {
      const auto field = std::find_if(structure->second.fields.begin(), structure->second.fields.end(),
          [&](const HirField& candidate) { return candidate.name == target.label; });
      if (field == structure->second.fields.end()) {
        diagnostics_.error("D2266", target.range,
                           "compile-time struct value has no field '" + target.label + "'");
        return false;
      }
      index = static_cast<std::size_t>(std::distance(structure->second.fields.begin(), field));
      element_type = field->type_name;
    } else {
      const auto types = split_tuple_type(aggregate.first);
      try {
        const auto parsed = std::stoll(target.label);
        if (parsed < 0 || static_cast<std::size_t>(parsed) >= types.size()) throw std::out_of_range("tuple");
        index = static_cast<std::size_t>(parsed);
        element_type = types[index];
      } catch (...) {
        diagnostics_.error("D2267", target.range,
                           "compile-time tuple assignment requires a valid positional member");
        return false;
      }
    }

    if (index >= values.size()) {
      diagnostics_.error("D2268", target.range, "malformed compile-time aggregate value");
      return false;
    }

    if (value.first != element_type && !(is_numeric(value.first) && is_numeric(element_type))) {
      diagnostics_.error("D2265", target.range, "compile-time assignment type mismatch");
      return false;
    }
    values[index] = value.second;
    return assign_comptime_place(target.children.front(), {aggregate.first, encode_aggregate(values)});
  }

  if (target.kind == SyntaxKind::index_expression && target.children.size() == 2) {
    auto aggregate = evaluate_constant(target.children.front());
    const auto array = parse_fixed_array_type(aggregate.first);
    if (!array) {
      diagnostics_.error("D2269", target.range,
                         "compile-time indexed assignment requires a fixed array");
      return false;
    }
    auto index_value = evaluate_constant(target.children[1]);
    if (!is_numeric(index_value.first)) {
      diagnostics_.error("D2270", target.children[1].range,
                         "compile-time aggregate index must be an integer");
      return false;
    }
    std::size_t index = 0;
    try {
      const auto parsed = std::stoll(index_value.second, nullptr, 0);
      if (parsed < 0 || static_cast<std::uint64_t>(parsed) >= array->length) throw std::out_of_range("array");
      index = static_cast<std::size_t>(parsed);
    } catch (...) {
      diagnostics_.error("D2271", target.children[1].range,
                         "compile-time aggregate index is out of bounds");
      return false;
    }

    if (value.first != array->element_type && !(is_numeric(value.first) && is_numeric(array->element_type))) {
      diagnostics_.error("D2265", target.range, "compile-time assignment type mismatch");
      return false;
    }
    auto values = decode_aggregate(aggregate.second);
    if (index >= values.size()) {
      diagnostics_.error("D2268", target.range, "malformed compile-time aggregate value");
      return false;
    }
    values[index] = value.second;
    return assign_comptime_place(target.children.front(), {aggregate.first, encode_aggregate(values)});
  }

  diagnostics_.error("D2272", target.range, "invalid compile-time assignment target");
  return false;
}

SemanticAnalyzer::ComptimeFlow SemanticAnalyzer::execute_comptime_statement(const SyntaxNode& statement) {
  if (statement.kind == SyntaxKind::const_declaration || statement.kind == SyntaxKind::variable_declaration) {
    const auto [declared_type, name] = split_typed_name(statement.label);
    if (statement.children.empty()) {
      diagnostics_.error("D2208", statement.range, "compile-time variable requires an initializer");
      return ComptimeFlow::normal;
    }
    auto value = evaluate_constant(statement.children.front());
    if (!value.first.empty() && value.first != declared_type && !(is_numeric(value.first) && is_numeric(declared_type)))
      diagnostics_.error("D2209", statement.range, "compile-time variable type mismatch");
    constants_[name] = {declared_type, value.second};
    return ComptimeFlow::normal;
  }

  if (statement.kind == SyntaxKind::block_statement) return execute_comptime_block(statement);
  if (statement.kind == SyntaxKind::break_statement) return ComptimeFlow::break_loop;
  if (statement.kind == SyntaxKind::continue_statement) return ComptimeFlow::continue_loop;
  if (statement.kind == SyntaxKind::if_statement && statement.children.size() >= 2) {
    const auto condition = evaluate_constant(statement.children[0]);
    if (condition.first != "bool") diagnostics_.error("D2211", statement.children[0].range, "compile-time if condition requires bool");
    if (condition.second == "true") return execute_comptime_statement(statement.children[1]);
    if (statement.children.size() >= 3) return execute_comptime_statement(statement.children[2]);
    return ComptimeFlow::normal;
  }

  if (statement.kind == SyntaxKind::while_statement && statement.children.size() == 2) {
    while (true) {
      if (++comptime_steps_ > 100000) {
        diagnostics_.error("D2240", statement.range, "compile-time execution exceeded 100000 steps");
        return ComptimeFlow::normal;
      }
      const auto condition = evaluate_constant(statement.children[0]);
      if (condition.first != "bool") {
        diagnostics_.error("D2211", statement.children[0].range, "compile-time while condition requires bool");
        return ComptimeFlow::normal;
      }

      if (condition.second != "true") break;
      const auto flow = execute_comptime_statement(statement.children[1]);
      if (flow == ComptimeFlow::break_loop) break;
      if (flow == ComptimeFlow::continue_loop) continue;
    }
    return ComptimeFlow::normal;
  }

  if (statement.kind == SyntaxKind::for_statement && statement.children.size() == 2 && !statement.label.empty()) {
    const auto& range = statement.children[0];
    if (range.kind != SyntaxKind::binary_expression || (range.label != ".." && range.label != "..=")) {
      diagnostics_.error("D2241", range.range, "compile-time for requires an integer range");
      return ComptimeFlow::normal;
    }
    const auto begin = evaluate_constant(range.children[0]);
    const auto end = evaluate_constant(range.children[1]);
    if (!is_numeric(begin.first) || !is_numeric(end.first)) {
      diagnostics_.error("D2241", range.range, "compile-time for range bounds must be integers");
      return ComptimeFlow::normal;
    }
    const auto first = std::stoll(begin.second, nullptr, 0);
    const auto last = std::stoll(end.second, nullptr, 0);
    const auto previous = constants_.find(statement.label);
    const std::optional<std::pair<std::string, std::string>> saved = previous == constants_.end() ? std::nullopt : std::optional(previous->second);
    const auto limit = range.label == "..=" ? last + 1 : last;
    for (auto index = first; index < limit; ++index) {
      if (++comptime_steps_ > 100000) {
        diagnostics_.error("D2240", statement.range, "compile-time execution exceeded 100000 steps");
        break;
      }
      constants_[statement.label] = {"i64", std::to_string(index)};
      const auto flow = execute_comptime_statement(statement.children[1]);
      if (flow == ComptimeFlow::break_loop) break;
      if (flow == ComptimeFlow::continue_loop) continue;
    }

    if (saved) constants_[statement.label] = *saved; else constants_.erase(statement.label);
    return ComptimeFlow::normal;
  }

  if (statement.kind == SyntaxKind::expression_statement && !statement.children.empty()) {
    const auto& expression = statement.children.front();
    if (expression.kind == SyntaxKind::call_expression && !expression.children.empty() &&
        expression.children.front().kind == SyntaxKind::name_expression && expression.children.front().label == "assert") {
      if (expression.children.size() != 2) {
        diagnostics_.error("D2210", expression.range, "comptime assert expects one argument");
        return ComptimeFlow::normal;
      }
      auto value = evaluate_constant(expression.children[1]);
      if (value.first != "bool") diagnostics_.error("D2211", expression.range, "comptime assert requires bool");
      else if (value.second != "true") diagnostics_.error("D2212", expression.range, "compile-time assertion failed");
      return ComptimeFlow::normal;
    }

    if (expression.kind == SyntaxKind::assignment_expression && expression.children.size() == 2) {
      auto result = evaluate_constant(expression.children[1]);
      if (expression.label != "=") {
        SyntaxNode binary;
        binary.kind = SyntaxKind::binary_expression;
        binary.range = expression.range;
        binary.label = expression.label.substr(0, expression.label.size() - 1);
        binary.children = {expression.children[0], expression.children[1]};
        result = evaluate_constant(binary);
      }
      (void)assign_comptime_place(expression.children[0], result);
      return ComptimeFlow::normal;
    }
    (void)evaluate_constant(expression);
    return ComptimeFlow::normal;
  }
  diagnostics_.error("D2213", statement.range, "statement is outside the stable deterministic comptime subset");
  return ComptimeFlow::normal;
}

void SemanticAnalyzer::analyze_comptime(const SyntaxNode& node) {
  if (node.children.empty()) return;
  const auto saved_constants = constants_;
  comptime_steps_ = 0;
  (void)execute_comptime_block(node.children.front());
  constants_ = saved_constants;
}
