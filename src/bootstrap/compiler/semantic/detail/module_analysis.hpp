// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

HirModule SemanticAnalyzer::analyze(const SyntaxTree& tree) {
  globals_.clear();
  scopes_.clear();
  moved_values_.clear();
  borrows_.clear();
  reference_bindings_.clear();
  reborrow_parents_.clear();
  suspended_references_.clear();
  borrows_.clear();
  reference_bindings_.clear();
  declared_names_.clear();
  type_layouts_.clear();
  constants_.clear();
  const_functions_.clear();
  declared_function_attributes_.clear();
  const_eval_depth_ = 0;
  comptime_steps_ = 0;
  enum_types_.clear();
  generic_types_.clear();
  trait_contracts_.clear();
  object_safe_traits_.clear();
  trait_supertraits_.clear();
  trait_aliases_.clear();
  generic_trait_implementations_.clear();
  negative_trait_pairs_.clear();
  trait_default_methods_.clear();
  trait_associated_types_.clear();
  trait_associated_constants_.clear();
  trait_implementors_.clear();
  implemented_trait_pairs_.clear();
  trait_method_functions_.clear();
  inherent_method_contracts_.clear();
  inherent_associated_functions_.clear();
  associated_type_bindings_.clear();
  current_generic_bounds_.clear();
  requested_instantiations_.clear();
  structural_type_names_.clear();
  requested_function_instantiations_.clear();
  explicit_copy_types_.clear();
  explicit_drop_types_.clear();
  explicit_clone_types_.clear();
  closure_function_names_.clear();
  closure_captures_.clear();
  move_closure_functions_.clear();
  shared_closure_functions_.clear();
  mutable_closure_functions_.clear();
  analyzing_closure_ = false;
  HirModule module;
  globals_.emplace("print", Symbol{SymbolKind::function, "print", "void", {"string"}, {}, {}, false, {}, {}, {}, {}, {}});
  for (const auto& child : tree.root().children) declare_top_level(child, module);
  for (const auto& child : tree.root().children) {
    if (child.kind == SyntaxKind::function_declaration) {
      const auto attributes = split_attributes(child.modifier);
      const auto arrow = child.label.find(" -> ");
      const auto header = arrow == std::string::npos ? child.label : child.label.substr(0, arrow);
      const auto function_name = split_generic_name(header).first;
      declared_function_attributes_[function_name] = {attributes.begin(), attributes.end()};
      if (std::find(attributes.begin(), attributes.end(), "const") != attributes.end()) {
        const_functions_[function_name] = child;
      }
    }
  }

  for (const auto& child : tree.root().children) {
    if (child.kind == SyntaxKind::const_declaration) {
      const auto [declared_type, name] = split_typed_name(child.label);
      if (!type_exists(declared_type)) diagnostics_.error("D2002", child.range, "unknown constant type '" + declared_type + "'");
      if (child.children.empty()) { diagnostics_.error("D2208", child.range, "constant requires an initializer"); continue; }
      auto value = evaluate_constant(child.children.front());
      if (!value.first.empty() && value.first != declared_type && !(is_numeric(value.first) && is_numeric(declared_type)))
        diagnostics_.error("D2209", child.range, "constant type mismatch");
      if (constants_.contains(name) || globals_.contains(name)) diagnostics_.error("D2001", child.range, "duplicate top-level declaration '" + name + "'");
      else {
        constants_[name] = {declared_type, value.second};
        globals_.emplace(name, Symbol{SymbolKind::constant, name, declared_type, {}, {}, child.range, false, {}, {}, {}, {}, {}});
        module.constants.push_back({name, declared_type, value.second, child.range});
      }
    }
  }

  for (const auto& child : tree.root().children) {
    if ((child.kind != SyntaxKind::struct_declaration && child.kind != SyntaxKind::enum_declaration) || child.modifier.empty()) continue;
    const auto [type_name, _arguments] = split_generic_name(child.label);
    for (const auto& attribute : split_attributes(child.modifier)) {
      if (!attribute.starts_with("derive(") || attribute.back() != ')') continue;
      const auto body = attribute.substr(7, attribute.size() - 8);
      std::size_t begin = 0;
      while (begin <= body.size()) {
        const auto end = body.find('+', begin);
        const auto trait = body.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (trait == "Copy") {
          explicit_copy_types_.insert(type_name);
          trait_implementors_["Copy"].insert(type_name);
          implemented_trait_pairs_.insert("Copy::" + type_name);
          module.trait_implementations.push_back({"Copy", type_name, {}, {}, child.range});
        } else if (trait == "Clone") {
          explicit_clone_types_.insert(type_name);
          trait_implementors_["Clone"].insert(type_name);
          implemented_trait_pairs_.insert("Clone::" + type_name);
          module.trait_implementations.push_back({"Clone", type_name, {}, {}, child.range});
        } else if (!trait.empty()) {
          const bool known_trait = trait_contracts_.contains(trait) || trait_aliases_.contains(trait) || trait_supertraits_.contains(trait);
          const bool has_methods = trait_contracts_.contains(trait) && !trait_contracts_.at(trait).empty();
          const bool has_associated_types = trait_associated_types_.contains(trait) && !trait_associated_types_.at(trait).empty();
          const bool has_associated_constants = trait_associated_constants_.contains(trait) && !trait_associated_constants_.at(trait).empty();
          const bool has_supertraits = trait_supertraits_.contains(trait) && !trait_supertraits_.at(trait).empty();
          const bool is_alias = trait_aliases_.contains(trait);
          if (!known_trait) {
            diagnostics_.error("D2218", child.range, "unknown derived trait '" + trait + "'");
          } else if (has_methods || has_associated_types || has_associated_constants || has_supertraits || is_alias) {
            diagnostics_.error("D2235", child.range,
                               "trait '" + trait + "' cannot be derived automatically because it is not a marker trait");
          } else {
            const auto pair = trait + "::" + type_name;
            if (!implemented_trait_pairs_.insert(pair).second) {
              diagnostics_.error("D2236", child.range,
                                 "duplicate derived implementation of '" + trait + "' for '" + type_name + "'");
            } else {
              trait_implementors_[trait].insert(type_name);
              module.trait_implementations.push_back({trait, type_name, {}, {}, child.range});
            }
          }
        }
        if (end == std::string::npos) break;
        begin = end + 1;
      }
    }
  }

  // Predeclare Copy markers before closure capture discovery so explicitly Copy
  // aggregates can be snapshotted into allocation-free closure environments.
  for (const auto& child : tree.root().children) {
    if (child.kind != SyntaxKind::impl_declaration) continue;
    const auto separator = child.label.find(" for ");
    if (separator == std::string::npos || child.label.substr(0, separator) != "Copy") continue;
    explicit_copy_types_.insert(child.label.substr(separator + 5));
  }
  std::vector<SyntaxNode> closure_declarations;
  const auto collect_names = [](const SyntaxNode& root, const std::unordered_map<std::string, std::string>& visible,
                                const std::unordered_set<std::string>& excluded) {
    std::vector<std::string> captures;
    std::unordered_set<std::string> seen;
    std::function<void(const SyntaxNode&)> walk = [&](const SyntaxNode& current) {
      if (current.kind == SyntaxKind::name_expression && visible.contains(current.label) &&
          !excluded.contains(current.label) && seen.insert(current.label).second) {
        captures.push_back(current.label);
      }
      for (const auto& child : current.children) walk(child);
    };
    walk(root);
    return captures;
  };
  std::function<void(const SyntaxNode&, std::unordered_map<std::string, std::string>)> collect_from_node;
  collect_from_node = [&](const SyntaxNode& current, std::unordered_map<std::string, std::string> visible) {
    if (current.kind == SyntaxKind::closure_expression) {
      SyntaxNode function;
      function.kind = SyntaxKind::function_declaration;
      function.range = current.range;
      function.label = current.label;
      std::unordered_set<std::string> excluded;
      for (const auto& child : current.children) {
        if (child.kind == SyntaxKind::parameter) excluded.insert(split_typed_name(child.label).second);
      }
      const auto captures = collect_names(current, visible, excluded);
      for (const auto& capture : captures) {
        const auto type = visible.at(capture);
        if (current.modifier != "move" && current.modifier != "ref" && current.modifier != "mut" &&
            !is_builtin_type(type) && !parse_reference_type(type) && !is_copy_type(type)) {
          diagnostics_.error("D2124", current.range,
                             "capturing resource-bearing value '" + capture +
                             "' requires an explicit move or borrow capture mode");
          continue;
        }
        SyntaxNode parameter;
        parameter.kind = SyntaxKind::parameter;
        parameter.range = current.range;
        auto capture_parameter_type = type;
        if (current.modifier == "ref") capture_parameter_type += "&";
        if (current.modifier == "mut") capture_parameter_type += "&mut";
        parameter.label = capture_parameter_type + " " + capture;
        function.children.push_back(std::move(parameter));
        closure_captures_[closure_function_name(current)].push_back(capture);
      }
      function.children.insert(function.children.end(), current.children.begin(), current.children.end());
      if (current.modifier == "move") move_closure_functions_.insert(closure_function_name(current));
      if (current.modifier == "ref") shared_closure_functions_.insert(closure_function_name(current));
      if (current.modifier == "mut") mutable_closure_functions_.insert(closure_function_name(current));
      closure_declarations.push_back(std::move(function));
      return;
    }

    if (current.kind == SyntaxKind::block_statement) {
      auto block_visible = std::move(visible);
      for (const auto& child : current.children) {
        collect_from_node(child, block_visible);
        if (child.kind == SyntaxKind::variable_declaration) {
          auto [type, name] = split_typed_name(child.label);
          if (type != "auto") block_visible[name] = type;
        }
      }
      return;
    }

    for (const auto& child : current.children) collect_from_node(child, visible);
  };
  for (const auto& child : tree.root().children) {
    if (child.kind != SyntaxKind::function_declaration) continue;
    std::unordered_map<std::string, std::string> visible;
    for (const auto& part : child.children) {
      if (part.kind == SyntaxKind::parameter) {
        const auto [type, name] = split_typed_name(part.label);
        visible[name] = type;
      }
    }

    collect_from_node(child, std::move(visible));
  }

  for (const auto& closure : closure_declarations) {
    closure_function_names_.insert(closure_function_name(closure));
    declare_top_level(closure, module);
  }

  for (const auto& child : tree.root().children) {
    if (child.kind == SyntaxKind::function_declaration) analyze_function(child, module);
    else if (child.kind == SyntaxKind::impl_declaration) analyze_trait_implementation(child, module);
  }
  // Execute top-level compile-time blocks only after implementation metadata is
  // complete, so typed reflection observes the entire module regardless of
  // declaration order.
  for (const auto& child : tree.root().children) {
    if (child.kind == SyntaxKind::comptime_statement) analyze_comptime(child);
  }

  for (const auto& closure : closure_declarations) {
    analyzing_closure_ = true;
    analyze_function(closure, module);
    analyzing_closure_ = false;
  }

  for (const auto& type : explicit_copy_types_) {
    if (explicit_drop_types_.contains(type)) {
      diagnostics_.error("D2068", {}, "type '" + type + "' cannot implement both Copy and Drop");
    }
    const auto layout = type_layouts_.find(type);
    if (layout != type_layouts_.end()) {
      for (const auto& field : layout->second.fields) {
        if (!is_copy_type(field.type_name)) {
          diagnostics_.error("D2069", field.range, "Copy type '" + type + "' contains non-Copy field '" + field.name + "'");
        }
      }
    }
    const auto enumeration = enum_types_.find(type);
    if (enumeration != enum_types_.end()) {
      for (const auto& variant : enumeration->second.variants) {
        for (const auto& payload_type : variant.payload_types) {
          if (!is_copy_type(payload_type)) {
            diagnostics_.error("D2069", variant.range, "Copy enum '" + type + "' variant '" + variant.name +
                               "' contains non-Copy payload '" + payload_type + "'");
          }
        }
      }
    }
  }

  for (const auto& type : explicit_clone_types_) {
    const auto layout = type_layouts_.find(type);
    if (layout != type_layouts_.end()) {
      for (const auto& field : layout->second.fields) {
        if (!is_clone_type(field.type_name)) {
          diagnostics_.error("D2085", field.range, "Clone type '" + type + "' contains non-Clone field '" + field.name + "'");
        }
      }
    }
    const auto enumeration = enum_types_.find(type);
    if (enumeration != enum_types_.end()) {
      for (const auto& variant : enumeration->second.variants) {
        for (const auto& payload_type : variant.payload_types) {
          if (!is_clone_type(payload_type)) {
            diagnostics_.error("D2085", variant.range, "Clone enum '" + type + "' variant '" + variant.name +
                               "' contains non-Clone payload '" + payload_type + "'");
          }
        }
      }
    }
  }
  {
    std::vector<std::string> names(structural_type_names_.begin(), structural_type_names_.end());
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
      const auto found = type_layouts_.find(name);
      if (found != type_layouts_.end() &&
          std::none_of(module.types.begin(), module.types.end(), [&](const HirType& type) { return type.name == name; })) {
        module.types.push_back(found->second);
      }
    }
  }

  materialize_generic_instantiations(module);
  materialize_generic_trait_implementations(module);
  // Generic method specialization can request helper aggregate types that were
  // not reachable before the first instantiation pass (for example an iterator
  // returned by Vector<T>::iter). Materialize that late work before MIR.
  materialize_generic_instantiations(module);
  materialize_generic_functions(module);
  return module;
}

void SemanticAnalyzer::analyze_trait_implementation(const SyntaxNode& node, HirModule& module) {
  std::string implementation_label = node.label;
  std::vector<std::string> implementation_parameters;
  std::vector<std::vector<std::string>> implementation_bounds;
  std::vector<std::string> implementation_const_types;
  if (implementation_label.starts_with("<")) {
    int depth = 0;
    std::size_t close = std::string::npos;
    for (std::size_t index = 0; index < implementation_label.size(); ++index) {
      if (implementation_label[index] == '<') ++depth;
      else if (implementation_label[index] == '>' && --depth == 0) { close = index; break; }
    }

    if (close == std::string::npos) {
      diagnostics_.error("D2142", node.range, "unterminated generic implementation parameter list");
      return;
    }
    const auto [ignored, raw_parameters] = split_generic_name(implementation_label.substr(0, close + 1));
    for (const auto& raw : raw_parameters) {
      auto detail = parse_generic_parameter_detail(raw);
      if (detail.name.empty()) {
        diagnostics_.error("D2250", node.range, "invalid generic implementation parameter '" + raw + "'");
        return;
      }
      implementation_parameters.push_back(std::move(detail.name));
      implementation_bounds.push_back(std::move(detail.bounds));
      implementation_const_types.push_back(std::move(detail.const_type));
    }
    implementation_label = implementation_label.substr(close + 1);
    if (!implementation_label.empty() && implementation_label.front() == ' ') implementation_label.erase(0, 1);
  }
  const bool negative_implementation = implementation_label.starts_with("!");
  if (negative_implementation) implementation_label.erase(0, 1);

  const auto separator = implementation_label.find(" for ");
  if (separator == std::string::npos) {
    if (negative_implementation) {
      diagnostics_.error("D2143", node.range, "negative inherent implementations are outside the stable Raz 1.0 language");
      return;
    }
    const auto target_type = implementation_label;
    if (!implementation_parameters.empty()) {
      for (std::size_t parameter_index = 0; parameter_index < implementation_bounds.size(); ++parameter_index) {
        const auto& bounds = implementation_bounds[parameter_index];
        if (parameter_index < implementation_const_types.size() &&
            !implementation_const_types[parameter_index].empty() && !bounds.empty()) {
          diagnostics_.error("D2251", node.range, "const generic implementation parameters cannot declare trait bounds");
          return;
        }
        for (const auto& bound : bounds) {
          if (bound != "Copy" && bound != "Clone" && bound != "Drop" &&
              !trait_contracts_.contains(bound) && !trait_aliases_.contains(bound) && !trait_supertraits_.contains(bound)) {
            diagnostics_.error("D2154", node.range, "unknown generic implementation bound '" + bound + "'");
            return;
          }
        }
      }
      const auto [target_base, target_arguments] = split_generic_name(target_type);
      if (target_arguments.empty() || !generic_types_.contains(target_base)) {
        diagnostics_.error("D2147", node.range, "generic inherent implementation target must be a generic type pattern");
        return;
      }
      const auto declared = generic_types_.find(target_base);
      if (declared == generic_types_.end() || declared->second.size() != target_arguments.size()) {
        diagnostics_.error("D2252", node.range, "generic implementation target has the wrong number of arguments");
        return;
      }
      const auto declared_const = generic_const_types_.find(target_base);
      std::unordered_set<std::string> used_parameters;
      std::function<void(const std::string&)> collect_parameters = [&](const std::string& argument) {
        if (std::find(implementation_parameters.begin(), implementation_parameters.end(), argument) !=
            implementation_parameters.end()) {
          used_parameters.insert(argument);
          return;
        }
        const auto [nested_base, nested_arguments] = split_generic_name(argument);
        (void)nested_base;
        for (const auto& nested : nested_arguments) collect_parameters(nested);
      };
      for (std::size_t index = 0; index < target_arguments.size(); ++index) {
        collect_parameters(target_arguments[index]);
        const auto parameter = std::find(implementation_parameters.begin(), implementation_parameters.end(), target_arguments[index]);
        const bool target_const = declared_const != generic_const_types_.end() &&
                                  index < declared_const->second.size() && !declared_const->second[index].empty();
        if (parameter != implementation_parameters.end()) {
          const auto parameter_index = static_cast<std::size_t>(parameter - implementation_parameters.begin());
          const bool pattern_const = parameter_index < implementation_const_types.size() &&
                                     !implementation_const_types[parameter_index].empty();
          if (pattern_const != target_const) {
            diagnostics_.error("D2253", node.range,
                               "generic implementation parameter kind does not match target argument '" +
                               target_arguments[index] + "'");
            return;
          }
          if (pattern_const && implementation_const_types[parameter_index] != declared_const->second[index]) {
            diagnostics_.error("D2254", node.range,
                               "const generic implementation parameter type must be '" +
                               declared_const->second[index] + "'");
            return;
          }
        } else if (target_const) {
          if (!evaluate_const_integer_expression(target_arguments[index]).has_value()) {
            diagnostics_.error("D2255", node.range,
                               "generic implementation const pattern must be a concrete compile-time integer or parameter");
            return;
          }
        }
      }
      for (const auto& parameter : implementation_parameters) {
        if (!used_parameters.contains(parameter)) {
          diagnostics_.error("D2257", node.range,
                             "generic implementation parameter '" + parameter +
                             "' is not constrained by the target pattern");
          return;
        }
      }
      std::unordered_map<std::string, SyntaxNode> generic_methods;
      for (const auto& child : node.children) {
        if (child.kind != SyntaxKind::function_declaration) {
          diagnostics_.error("D2127", child.range, "generic inherent implementations may only contain functions");
          return;
        }
        const auto arrow = child.label.find(" -> ");
        const auto header = arrow == std::string::npos ? child.label : child.label.substr(0, arrow);
        const auto [method_name, ignored] = split_generic_name(header);
        if (!generic_methods.emplace(method_name, child).second) {
          diagnostics_.error("D2128", child.range, "duplicate inherent method '" + method_name + "'");
          return;
        }
      }
      GenericTraitImplementation candidate{
          "", target_type, implementation_parameters, implementation_bounds, implementation_const_types,
          {}, {}, generic_methods, node.range};
      const auto overlap = std::find_if(generic_trait_implementations_.begin(),
          generic_trait_implementations_.end(), [&](const GenericTraitImplementation& existing) {
            if (!existing.trait_name.empty()) return false;
            if (!generic_trait_patterns_overlap(existing, candidate)) return false;
            for (const auto& [name, _] : existing.methods) if (candidate.methods.contains(name)) return true;
            return false;
          });
      if (overlap != generic_trait_implementations_.end()) {
        diagnostics_.error("D2150", node.range,
                           "overlapping generic inherent implementation for '" + target_type + "'");
        return;
      }
      generic_trait_implementations_.push_back(std::move(candidate));
      for (const auto& [method_name, _] : generic_methods)
        module.trait_implementations.push_back({"", target_type, method_name, {}, node.range});
      return;
    }

    if (!type_exists(target_type)) {
      diagnostics_.error("D2126", node.range, "unknown inherent implementation target '" + target_type + "'");
      return;
    }
    std::unordered_set<std::string> method_names;
    for (const auto& child : node.children) {
      if (child.kind != SyntaxKind::function_declaration) {
        diagnostics_.error("D2127", child.range, "inherent implementations may only contain functions");
        continue;
      }
      const auto arrow = child.label.find(" -> ");
      const auto header = arrow == std::string::npos ? child.label : child.label.substr(0, arrow);
      const auto [method_name, ignored] = split_generic_name(header);
      if (!method_names.insert(method_name).second) {
        diagnostics_.error("D2128", child.range, "duplicate inherent method '" + method_name + "'");
        continue;
      }
      TraitMethodContract contract;
      contract.name = method_name;
      contract.return_type = function_return_type(child.label);
      for (const auto& attribute : split_attributes(child.modifier)) contract.attributes.insert(attribute);
      contract.range = child.range;
      SyntaxNode generated = child;
      const auto generated_name = "__raz_inherent_" + target_type + "_" + method_name;
      generated.label = generated_name + (contract.return_type == "void" ? "" : " -> " + contract.return_type);
      Symbol symbol;
      symbol.kind = SymbolKind::function;
      symbol.name = generated_name;
      symbol.type_name = contract.return_type;
      symbol.declaration = generated.range;
      bool has_receiver = false;
      for (const auto& parameter : child.children) {
        if (parameter.kind != SyntaxKind::parameter) continue;
        const auto [parameter_type, parameter_name] = split_typed_name(parameter.label);
        contract.parameter_types.push_back(parameter_type);
        symbol.parameter_types.push_back(parameter_type);
        if (contract.parameter_types.size() == 1 && parameter_name == "self") {
          auto receiver = parameter_type;
          if (const auto reference = parse_reference_type(receiver)) receiver = reference->referent_type;
          if (receiver != target_type) {
            diagnostics_.error("D2129", parameter.range,
                               "inherent method receiver must be '" + target_type + "', '" +
                               target_type + "&', or '" + target_type + "&mut'");
          } else {
            has_receiver = true;
          }
        }
      }
      if (!declare(symbol)) {
        diagnostics_.error("D2130", generated.range, "duplicate generated inherent function '" + generated_name + "'");
        continue;
      }
      analyze_function(generated, module);
      const auto key = target_type + "::" + method_name;
      inherent_method_contracts_[key] = contract;
      trait_method_functions_[key] = generated_name;
      if (!has_receiver) inherent_associated_functions_.insert(key);
      module.trait_implementations.push_back({"", target_type, method_name, generated_name, node.range});
    }
    return;
  }
  const auto trait_name = implementation_label.substr(0, separator);
  const auto target_type = implementation_label.substr(separator + 5);
  const auto implementation_key = trait_name + "::" + target_type;

  if (trait_aliases_.contains(trait_name)) {
    diagnostics_.error("D2144", node.range, "trait alias '" + trait_name + "' cannot be implemented directly");
    return;
  }

  if (negative_implementation) {
    if (!implementation_parameters.empty()) {
      diagnostics_.error("D2145", node.range, "generic negative implementations are outside the stable Raz 1.0 language");
      return;
    }

    if (!node.children.empty()) {
      diagnostics_.error("D2146", node.range, "negative implementations cannot declare items");
      return;
    }

    if (!type_exists(target_type)) {
      diagnostics_.error("D2070", node.range, "unknown implementation target '" + target_type + "'");
      return;
    }

    if (implemented_trait_pairs_.contains(implementation_key)) {
      diagnostics_.error("D2151", node.range,
                         "negative implementation conflicts with an existing positive implementation of '" +
                         trait_name + "' for '" + target_type + "'");
      return;
    }
    negative_trait_pairs_.insert(implementation_key);
    module.trait_implementations.push_back({"!" + trait_name, target_type, {}, {}, node.range});
    return;
  }

  if (!implementation_parameters.empty()) {
    if (trait_name != "Copy" && trait_name != "Clone" && trait_name != "Drop" &&
        !trait_contracts_.contains(trait_name)) {
      diagnostics_.error("D2071", node.range, "unknown trait '" + trait_name + "'");
      return;
    }

    for (std::size_t parameter_index = 0; parameter_index < implementation_bounds.size(); ++parameter_index) {
      const auto& bounds = implementation_bounds[parameter_index];
      if (parameter_index < implementation_const_types.size() && !implementation_const_types[parameter_index].empty() && !bounds.empty()) {
        diagnostics_.error("D2251", node.range, "const generic implementation parameters cannot declare trait bounds");
        return;
      }
      for (const auto& bound : bounds) {
        if (bound != "Copy" && bound != "Clone" && bound != "Drop" &&
            !trait_contracts_.contains(bound) && !trait_aliases_.contains(bound) && !trait_supertraits_.contains(bound)) {
          diagnostics_.error("D2154", node.range, "unknown generic implementation bound '" + bound + "'");
          return;
        }
      }
    }
    const auto [target_base, target_arguments] = split_generic_name(target_type);
    if (target_arguments.empty() || !generic_types_.contains(target_base)) {
      diagnostics_.error("D2147", node.range, "generic implementation target must be a generic type pattern");
      return;
    }
    std::unordered_map<std::string, std::string> generic_associated_types;
    std::unordered_map<std::string, std::pair<std::string, std::string>> generic_associated_constants;
    std::unordered_map<std::string, SyntaxNode> generic_methods;
    for (const auto& child : node.children) {
      if (child.kind == SyntaxKind::associated_type_declaration) {
        const auto equal = child.label.find('=');
        const auto name = child.label.substr(0, equal);
        if (equal == std::string::npos) {
          diagnostics_.error("D2103", child.range, "associated type implementation requires '= Type'");
          return;
        }
        generic_associated_types.emplace(name, child.label.substr(equal + 1));
        continue;
      }
      if (child.kind == SyntaxKind::associated_const_declaration) {
        const auto colon = child.label.find(':');
        const auto equal = child.label.find('=');
        if (colon == std::string::npos || equal == std::string::npos || colon > equal) {
          diagnostics_.error("D2105", child.range, "associated constant implementation requires a type and value");
          return;
        }
        const auto name = child.label.substr(0, colon);
        generic_associated_constants.emplace(
            name, std::pair<std::string, std::string>{
                      child.label.substr(colon + 1, equal - colon - 1), child.label.substr(equal + 1)});
        continue;
      }
      if (child.kind == SyntaxKind::function_declaration) {
        const auto arrow = child.label.find(" -> ");
        const auto header = arrow == std::string::npos ? child.label : child.label.substr(0, arrow);
        const auto [method_name, ignored] = split_generic_name(header);
        if (!generic_methods.emplace(method_name, child).second) {
          diagnostics_.error("D2092", child.range, "duplicate implementation method '" + method_name + "'");
          return;
        }
        continue;
      }
      diagnostics_.error("D2148", child.range, "unsupported item in generic trait implementation");
      return;
    }

    if (trait_name == "Clone") {
      if (!generic_associated_types.empty() || !generic_associated_constants.empty() ||
          generic_methods.size() > 1 || (!generic_methods.empty() && !generic_methods.contains("clone"))) {
        diagnostics_.error("D2265", node.range, "generic Clone implementations may only declare fn clone");
        return;
      }
      if (const auto clone = generic_methods.find("clone"); clone != generic_methods.end()) {
        std::vector<std::string> parameter_types;
        for (const auto& parameter : clone->second.children) {
          if (parameter.kind == SyntaxKind::parameter) parameter_types.push_back(split_typed_name(parameter.label).first);
        }
        const auto expected_receiver = target_type + "&";
        if (parameter_types.size() != 1 || parameter_types.front() != expected_receiver ||
            function_return_type(clone->second.label) != target_type) {
          diagnostics_.error("D2266", clone->second.range,
                             "generic clone must take exactly one '" + expected_receiver +
                             " self' parameter and return '" + target_type + "'");
          return;
        }
      }
    }

    if (trait_name == "Drop") {
      if (!generic_associated_types.empty() || !generic_associated_constants.empty() ||
          generic_methods.size() != 1 || !generic_methods.contains("drop")) {
        diagnostics_.error("D2267", node.range, "generic Drop implementation requires exactly fn drop");
        return;
      }
      const auto& drop = generic_methods.at("drop");
      std::vector<std::string> parameter_types;
      for (const auto& parameter : drop.children) {
        if (parameter.kind == SyntaxKind::parameter) parameter_types.push_back(split_typed_name(parameter.label).first);
      }
      const auto expected_receiver = target_type + "&mut";
      if (parameter_types.size() != 1 || parameter_types.front() != expected_receiver ||
          function_return_type(drop.label) != "void") {
        diagnostics_.error("D2268", drop.range,
                           "generic drop must take exactly one '" + expected_receiver +
                           " self' parameter and return void");
        return;
      }
    }

    if (trait_name != "Copy" && trait_name != "Clone" && trait_name != "Drop") {
      const auto contract_it = trait_contracts_.find(trait_name);
      if (contract_it != trait_contracts_.end()) {
        for (const auto& contract : contract_it->second) {
          if (!generic_methods.contains(contract.name)) {
            const auto defaults = trait_default_methods_.find(trait_name);
            if (defaults == trait_default_methods_.end() || !defaults->second.contains(contract.name)) {
              diagnostics_.error("D2093", node.range, "implementation of '" + trait_name +
                                 "' is missing method '" + contract.name + "'");
            }
          }
        }
        for (const auto& [name, method] : generic_methods) {
          const auto contract = std::find_if(contract_it->second.begin(), contract_it->second.end(),
              [&](const TraitMethodContract& candidate) { return candidate.name == name; });
          if (contract == contract_it->second.end()) {
            diagnostics_.error("D2095", method.range, "method '" + name +
                               "' is not declared by trait '" + trait_name + "'");
            continue;
          }
          const auto resolve_template_type = [&](std::string type) {
            if (type.starts_with("Self")) type.replace(0, 4, target_type);
            if (const auto binding = generic_associated_types.find(type);
                binding != generic_associated_types.end()) type = binding->second;
            return type;
          };
          std::vector<std::string> actual_parameters;
          for (const auto& child : method.children) {
            if (child.kind == SyntaxKind::parameter) {
              actual_parameters.push_back(split_typed_name(child.label).first);
            }
          }
          std::vector<std::string> expected_parameters;
          for (auto type : contract->parameter_types) {
            expected_parameters.push_back(resolve_template_type(std::move(type)));
          }
          const auto expected_return = resolve_template_type(contract->return_type);
          if (actual_parameters != expected_parameters || function_return_type(method.label) != expected_return) {
            diagnostics_.error("D2094", method.range, "method '" + name + "' does not match trait contract (expected return '" +
                               expected_return + "', got '" + function_return_type(method.label) + "')");
          }
          const auto attributes = split_attributes(method.modifier);
          const std::unordered_set<std::string> attribute_set(attributes.begin(), attributes.end());
          for (const auto& effect : contract->attributes) {
            if (!attribute_set.contains(effect)) {
              diagnostics_.error("D2232", method.range, "method '" + name +
                                 "' must preserve trait contract @" + effect);
            }
          }
        }
      }
    }

    if (const auto required = trait_associated_types_.find(trait_name);
        required != trait_associated_types_.end()) {
      for (const auto& [name, range] : required->second) {
        if (!generic_associated_types.contains(name)) {
          diagnostics_.error("D2107", node.range, "implementation of '" + trait_name +
                             "' is missing associated type '" + name + "'");
        }
      }
      for (const auto& [name, type] : generic_associated_types) {
        if (!required->second.contains(name)) {
          diagnostics_.error("D2109", node.range, "associated type '" + name +
                             "' is not declared by trait '" + trait_name + "'");
        }
      }
    }

    if (const auto required = trait_associated_constants_.find(trait_name);
        required != trait_associated_constants_.end()) {
      for (const auto& [name, expected_type] : required->second) {
        const auto found = generic_associated_constants.find(name);
        if (found == generic_associated_constants.end()) {
          diagnostics_.error("D2110", node.range, "implementation of '" + trait_name +
                             "' is missing associated constant '" + name + "'");
        } else if (found->second.first != expected_type) {
          diagnostics_.error("D2111", node.range, "associated constant '" + name + "' has the wrong type");
        }
      }
      for (const auto& [name, binding] : generic_associated_constants) {
        if (!required->second.contains(name)) {
          diagnostics_.error("D2112", node.range, "associated constant '" + name +
                             "' is not declared by trait '" + trait_name + "'");
        }
      }
    }

    if (const auto declared = generic_types_.find(target_base); declared != generic_types_.end()) {
      if (declared->second.size() != target_arguments.size()) {
        diagnostics_.error("D2252", node.range, "generic implementation target has the wrong number of arguments");
        return;
      }
      const auto declared_const = generic_const_types_.find(target_base);
      std::unordered_set<std::string> used_parameters;
      std::function<bool(const std::string&)> collect_parameters = [&](const std::string& argument) {
        if (std::find(implementation_parameters.begin(), implementation_parameters.end(), argument) !=
            implementation_parameters.end()) {
          used_parameters.insert(argument);
          return true;
        }
        const auto [nested_base, nested_arguments] = split_generic_name(argument);
        (void)nested_base;
        bool contains_parameter = false;
        for (const auto& nested : nested_arguments) {
          contains_parameter = collect_parameters(nested) || contains_parameter;
        }
        return contains_parameter;
      };
      for (std::size_t index = 0; index < target_arguments.size(); ++index) {
        const bool argument_contains_parameter = collect_parameters(target_arguments[index]);
        const auto parameter = std::find(implementation_parameters.begin(), implementation_parameters.end(), target_arguments[index]);
        const bool target_const = declared_const != generic_const_types_.end() &&
                                  index < declared_const->second.size() && !declared_const->second[index].empty();
        if (parameter != implementation_parameters.end()) {
          const auto parameter_index = static_cast<std::size_t>(parameter - implementation_parameters.begin());
          const bool pattern_const = parameter_index < implementation_const_types.size() &&
                                     !implementation_const_types[parameter_index].empty();
          if (pattern_const != target_const) {
            diagnostics_.error("D2253", node.range,
                               "generic implementation parameter kind does not match target argument '" +
                               target_arguments[index] + "'");
            return;
          }
          if (pattern_const && implementation_const_types[parameter_index] != declared_const->second[index]) {
            diagnostics_.error("D2254", node.range,
                               "const generic implementation parameter type must be '" +
                               declared_const->second[index] + "'");
            return;
          }
          continue;
        }
        if (target_const) {
          if (!evaluate_const_integer_expression(target_arguments[index]).has_value()) {
            diagnostics_.error("D2255", node.range,
                               "generic implementation const pattern must be a concrete compile-time integer or parameter");
            return;
          }
        } else if (argument_contains_parameter) {
          const auto [nested_base, nested_arguments] = split_generic_name(target_arguments[index]);
          if (nested_arguments.empty() || !generic_types_.contains(nested_base)) {
            diagnostics_.error("D2256", node.range,
                               "invalid nested generic implementation pattern '" +
                               target_arguments[index] + "'");
            return;
          }
        } else if (!type_exists(target_arguments[index])) {
          diagnostics_.error("D2256", node.range,
                             "unknown concrete type in generic implementation pattern '" +
                             target_arguments[index] + "'");
          return;
        }
      }
      for (const auto& parameter : implementation_parameters) {
        if (!used_parameters.contains(parameter)) {
          diagnostics_.error("D2257", node.range,
                             "generic implementation parameter '" + parameter +
                             "' is not constrained by the target pattern");
          return;
        }
      }
    }
    GenericTraitImplementation candidate{
        trait_name, target_type, implementation_parameters, implementation_bounds,
        implementation_const_types, generic_associated_types, generic_associated_constants,
        generic_methods, node.range};
    const auto overlap = std::find_if(generic_trait_implementations_.begin(),
        generic_trait_implementations_.end(), [&](const GenericTraitImplementation& existing) {
          return generic_trait_patterns_overlap(existing, candidate);
        });
    if (overlap != generic_trait_implementations_.end()) {
      diagnostics_.error("D2150", node.range,
                         "overlapping generic implementation of '" + trait_name + "' for '" + target_type + "'");
      return;
    }
    generic_trait_implementations_.push_back(std::move(candidate));
    module.trait_implementations.push_back({trait_name, target_type, {}, {}, node.range});
    return;
  }

  if (negative_trait_pairs_.contains(implementation_key)) {
    diagnostics_.error("D2152", node.range,
                       "positive implementation conflicts with a negative implementation of '" +
                       trait_name + "' for '" + target_type + "'");
    return;
  }

  if (!implemented_trait_pairs_.insert(implementation_key).second) {
    diagnostics_.error("D2134", node.range, "duplicate implementation of '" + trait_name + "' for '" + target_type + "'");
    return;
  }

  if (!type_exists(target_type)) {
    diagnostics_.error("D2070", node.range, "unknown implementation target '" + target_type + "'");
    return;
  }

  if (trait_name == "Copy") {
    if (!node.children.empty()) {
      diagnostics_.error("D2071", node.range, "Copy is a marker trait and cannot declare methods");
    }
    explicit_copy_types_.insert(target_type);
    module.trait_implementations.push_back({trait_name, target_type, {}, {}, node.range});
    return;
  }

  if (trait_name == "Clone") {
    if (explicit_clone_types_.contains(target_type)) {
      diagnostics_.error("D2079", node.range, "duplicate Clone implementation for '" + target_type + "'");
      return;
    }
    const SyntaxNode* clone_method = nullptr;
    for (const auto& child : node.children) {
      if (child.kind == SyntaxKind::function_declaration && child.label.starts_with("clone")) {
        if (clone_method != nullptr) diagnostics_.error("D2080", child.range, "Clone implementation declares multiple clone methods");
        clone_method = &child;
      } else {
        diagnostics_.error("D2081", child.range, "Clone implementation may only declare fn clone");
      }
    }

    if (clone_method == nullptr) {
      diagnostics_.error("D2082", node.range, "Clone implementation requires fn clone");
      return;
    }
    std::vector<const SyntaxNode*> parameters;
    for (const auto& child : clone_method->children) if (child.kind == SyntaxKind::parameter) parameters.push_back(&child);
    const std::string expected = target_type + "&";
    if (parameters.size() != 1 || split_typed_name(parameters.front()->label).first != expected) {
      diagnostics_.error("D2083", clone_method->range, "clone must take exactly one '" + expected + " self' parameter");
      return;
    }

    if (function_return_type(clone_method->label) != target_type) {
      diagnostics_.error("D2084", clone_method->range, "clone must return '" + target_type + "'");
      return;
    }
    SyntaxNode generated = *clone_method;
    generated.label = "__raz_clone_" + target_type + " -> " + target_type;
    Symbol symbol;
    symbol.kind = SymbolKind::function;
    symbol.name = "__raz_clone_" + target_type;
    symbol.type_name = target_type;
    symbol.parameter_types.push_back(expected);
    symbol.declaration = generated.range;
    if (!declare(symbol)) {
      diagnostics_.error("D2088", generated.range, "duplicate generated clone function for '" + target_type + "'");
      return;
    }
    explicit_clone_types_.insert(target_type);
    analyze_function(generated, module);
    module.trait_implementations.push_back({trait_name, target_type, "clone", symbol.name, node.range});
    return;
  }

  if (trait_name != "Drop") {
    const auto contract_it = trait_contracts_.find(trait_name);
    if (contract_it == trait_contracts_.end()) {
      diagnostics_.error("D2091", node.range, "unknown trait '" + trait_name + "'");
      return;
    }

    if (const auto supers = trait_supertraits_.find(trait_name); supers != trait_supertraits_.end()) {
      for (const auto& supertrait : supers->second) {
        if (!implements_trait(target_type, supertrait)) {
          diagnostics_.error("D2114", node.range, "implementation of '" + trait_name +
                             "' requires supertrait '" + supertrait + "' for '" + target_type + "'");
        }
      }
    }
    std::unordered_map<std::string, const SyntaxNode*> methods;
    std::unordered_map<std::string, const SyntaxNode*> associated_types;
    std::unordered_map<std::string, const SyntaxNode*> associated_constants;
    for (const auto& child : node.children) {
      if (child.kind == SyntaxKind::associated_type_declaration) {
        const auto equal = child.label.find('=');
        const auto name = child.label.substr(0, equal);
        if (equal == std::string::npos) diagnostics_.error("D2103", child.range, "associated type implementation requires '= Type'");
        else if (!associated_types.emplace(name, &child).second) diagnostics_.error("D2104", child.range, "duplicate associated type binding '" + name + "'");
        continue;
      }
      if (child.kind == SyntaxKind::associated_const_declaration) {
        const auto colon = child.label.find(':');
        const auto equal = child.label.find('=');
        const auto name = child.label.substr(0, colon);
        if (equal == std::string::npos) diagnostics_.error("D2105", child.range, "associated constant implementation requires a value");
        else if (!associated_constants.emplace(name, &child).second) diagnostics_.error("D2106", child.range, "duplicate associated constant binding '" + name + "'");
        continue;
      }
      if (child.kind != SyntaxKind::function_declaration) continue;
      const auto arrow = child.label.find(" -> ");
      const auto header = arrow == std::string::npos ? child.label : child.label.substr(0, arrow);
      const auto [method_name, ignored] = split_generic_name(header);
      if (!methods.emplace(method_name, &child).second) {
        diagnostics_.error("D2092", child.range, "duplicate implementation method '" + method_name + "'");
      }
    }
    const auto required_types = trait_associated_types_.find(trait_name);
    if (required_types != trait_associated_types_.end()) {
      for (const auto& [name, ignored_range] : required_types->second) {
        const auto found = associated_types.find(name);
        if (found == associated_types.end()) { diagnostics_.error("D2107", node.range, "implementation of '" + trait_name + "' is missing associated type '" + name + "'"); continue; }
        const auto equal = found->second->label.find('=');
        const auto type = found->second->label.substr(equal + 1);
        if (!type_exists(type)) diagnostics_.error("D2108", found->second->range, "unknown associated type binding '" + type + "'");
        module.associated_type_bindings.push_back({trait_name, target_type, name, type, found->second->range});
        associated_type_bindings_[target_type + "::" + trait_name + "::" + name] = type;
      }
      for (const auto& [name, item] : associated_types) if (!required_types->second.contains(name)) diagnostics_.error("D2109", item->range, "associated type '" + name + "' is not declared by trait '" + trait_name + "'");
    }
    const auto required_consts = trait_associated_constants_.find(trait_name);
    if (required_consts != trait_associated_constants_.end()) {
      for (const auto& [name, expected_type] : required_consts->second) {
        const auto found = associated_constants.find(name);
        if (found == associated_constants.end()) { diagnostics_.error("D2110", node.range, "implementation of '" + trait_name + "' is missing associated constant '" + name + "'"); continue; }
        const auto colon = found->second->label.find(':'); const auto equal = found->second->label.find('=');
        const auto actual_type = found->second->label.substr(colon + 1, equal - colon - 1);
        const auto value = found->second->label.substr(equal + 1);
        if (actual_type != expected_type) diagnostics_.error("D2111", found->second->range, "associated constant '" + name + "' has the wrong type");
        module.associated_const_bindings.push_back({trait_name, target_type, name, actual_type, value, found->second->range});
        associated_const_bindings_[target_type + "::" + trait_name + "::" + name] = {actual_type, value};
      }
      for (const auto& [name, item] : associated_constants) if (!required_consts->second.contains(name)) diagnostics_.error("D2112", item->range, "associated constant '" + name + "' is not declared by trait '" + trait_name + "'");
    }

    for (const auto& contract : contract_it->second) {
      const auto found = methods.find(contract.name);
      if (found == methods.end()) {
        const auto defaults = trait_default_methods_.find(trait_name);
        if (defaults == trait_default_methods_.end() || !defaults->second.contains(contract.name)) {
          diagnostics_.error("D2093", node.range, "implementation of '" + trait_name + "' is missing method '" + contract.name + "'");
        }
        continue;
      }
      const auto* method = found->second;
      std::vector<std::string> actual_parameters;
      for (const auto& child : method->children) {
        if (child.kind == SyntaxKind::parameter) actual_parameters.push_back(split_typed_name(child.label).first);
      }
      std::vector<std::string> expected_parameters;
      for (auto type : contract.parameter_types) {
        if (type.starts_with("Self")) type.replace(0, 4, target_type);
        if (const auto binding = associated_types.find(type); binding != associated_types.end()) {
          const auto equal = binding->second->label.find('=');
          if (equal != std::string::npos) type = binding->second->label.substr(equal + 1);
        }
        expected_parameters.push_back(std::move(type));
      }
      auto expected_return = contract.return_type;
      if (expected_return.starts_with("Self")) expected_return.replace(0, 4, target_type);
      if (const auto binding = associated_types.find(expected_return); binding != associated_types.end()) {
        const auto equal = binding->second->label.find('=');
        if (equal != std::string::npos) expected_return = binding->second->label.substr(equal + 1);
      }
      if (actual_parameters != expected_parameters || function_return_type(method->label) != expected_return) {
        diagnostics_.error("D2094", method->range, "method '" + contract.name + "' does not match trait contract");
      }
      const auto method_attributes = split_attributes(method->modifier);
      const std::unordered_set<std::string> method_attribute_set(method_attributes.begin(), method_attributes.end());
      for (const auto& effect : contract.attributes) {
        if (!method_attribute_set.contains(effect)) {
          diagnostics_.error("D2232", method->range, "method '" + contract.name +
                             "' must preserve trait contract @" + effect);
        }
      }
    }

    for (const auto& [name, method] : methods) {
      const bool declared = std::any_of(contract_it->second.begin(), contract_it->second.end(),
          [&](const TraitMethodContract& contract) { return contract.name == name; });
      if (!declared) diagnostics_.error("D2095", method->range, "method '" + name + "' is not declared by trait '" + trait_name + "'");
    }
    trait_implementors_[trait_name].insert(target_type);
    for (const auto& contract : contract_it->second) {
      const auto found = methods.find(contract.name);
      const SyntaxNode* source_method = found == methods.end() ? nullptr : found->second;
      if (source_method == nullptr) {
        const auto defaults = trait_default_methods_.find(trait_name);
        if (defaults != trait_default_methods_.end()) {
          const auto default_method = defaults->second.find(contract.name);
          if (default_method != defaults->second.end()) source_method = &default_method->second;
        }
      }
      if (source_method == nullptr) continue;
      SyntaxNode generated = *source_method;
      const auto generated_name = "__raz_trait_" + trait_name + "_" + target_type + "_" + contract.name;
      auto concrete_return = function_return_type(source_method->label);
      if (concrete_return.starts_with("Self")) concrete_return.replace(0, 4, target_type);
      if (const auto binding = associated_types.find(concrete_return); binding != associated_types.end()) {
        const auto equal = binding->second->label.find('=');
        if (equal != std::string::npos) concrete_return = binding->second->label.substr(equal + 1);
      }
      generated.label = generated_name + (concrete_return == "void" ? "" : " -> " + concrete_return);
      Symbol symbol;
      symbol.kind = SymbolKind::function;
      symbol.name = generated_name;
      symbol.type_name = concrete_return;
      symbol.declaration = generated.range;
      for (auto& parameter : generated.children) {
        if (parameter.kind != SyntaxKind::parameter) continue;
        auto [parameter_type, parameter_name] = split_typed_name(parameter.label);
        if (parameter_type.starts_with("Self")) parameter_type.replace(0, 4, target_type);
        if (const auto binding = associated_types.find(parameter_type); binding != associated_types.end()) {
          const auto equal = binding->second->label.find('=');
          if (equal != std::string::npos) parameter_type = binding->second->label.substr(equal + 1);
        }
        parameter.label = parameter_type + " " + parameter_name;
        symbol.parameter_types.push_back(parameter_type);
      }
      if (!declare(symbol)) {
        diagnostics_.error("D2098", generated.range, "duplicate generated trait method '" + generated_name + "'");
        continue;
      }
      analyze_function(generated, module);
      trait_method_functions_[target_type + "::" + contract.name] = generated_name;
      module.trait_implementations.push_back({trait_name, target_type, contract.name, generated_name, node.range});
    }
    return;
  }

  if (explicit_drop_types_.contains(target_type)) {
    diagnostics_.error("D2072", node.range, "duplicate Drop implementation for '" + target_type + "'");
    return;
  }
  const SyntaxNode* drop_method = nullptr;
  for (const auto& child : node.children) {
    if (child.kind == SyntaxKind::function_declaration && child.label.starts_with("drop")) {
      if (drop_method != nullptr) diagnostics_.error("D2073", child.range, "Drop implementation declares multiple drop methods");
      drop_method = &child;
    } else {
      diagnostics_.error("D2074", child.range, "Drop implementation may only declare fn drop");
    }
  }

  if (drop_method == nullptr) {
    diagnostics_.error("D2075", node.range, "Drop implementation requires fn drop");
    return;
  }
  std::vector<const SyntaxNode*> parameters;
  for (const auto& child : drop_method->children) if (child.kind == SyntaxKind::parameter) parameters.push_back(&child);
  const std::string expected = target_type + "&mut";
  if (parameters.size() != 1 || split_typed_name(parameters.front()->label).first != expected) {
    diagnostics_.error("D2076", drop_method->range, "drop must take exactly one '" + expected + " self' parameter");
    return;
  }

  if (function_return_type(drop_method->label) != "void") {
    diagnostics_.error("D2077", drop_method->range, "drop must return void");
    return;
  }
  SyntaxNode generated = *drop_method;
  generated.label = "__raz_drop_" + target_type;
  Symbol symbol;
  symbol.kind = SymbolKind::function;
  symbol.name = generated.label;
  symbol.type_name = "void";
  symbol.parameter_types.push_back(expected);
  symbol.declaration = generated.range;
  if (!declare(symbol)) {
    diagnostics_.error("D2078", generated.range, "duplicate generated drop function for '" + target_type + "'");
    return;
  }
  explicit_drop_types_.insert(target_type);
  analyze_function(generated, module);
  module.trait_implementations.push_back({trait_name, target_type, "drop", generated.label, node.range});
}

void SemanticAnalyzer::declare_top_level(const SyntaxNode& node, HirModule& module) {
  if (node.kind == SyntaxKind::const_declaration || node.kind == SyntaxKind::comptime_statement) return;
  if (node.kind == SyntaxKind::enum_declaration) {
    const auto [enum_name, raw_generic_parameters] = split_generic_name(node.label);
    std::vector<std::string> generic_parameters;
    std::vector<std::string> generic_const_types;
    for (const auto& raw : raw_generic_parameters) {
      const auto detail = parse_generic_parameter_detail(raw);
      generic_parameters.push_back(detail.name);
      generic_const_types.push_back(detail.const_type);
    }
    Symbol symbol{SymbolKind::type, enum_name, enum_name, {}, {}, node.range, false, {}, {}, {}, {}, {}};
    if (!declare(symbol)) {
      diagnostics_.error("D2001", node.range, "duplicate top-level declaration '" + enum_name + "'");
      return;
    }

    if (!generic_parameters.empty()) {
      generic_types_[enum_name] = generic_parameters;
      generic_const_types_[enum_name] = generic_const_types;
    }
    HirEnum enumeration;
    enumeration.name = enum_name;
    enumeration.generic_parameters = generic_parameters;
    enumeration.generic_const_types = generic_const_types;
    enumeration.range = node.range;
    std::unordered_map<std::string, bool> names;
    std::unordered_map<std::int64_t, bool> discriminants;
    for (const auto& child : node.children) {
      if (child.kind != SyntaxKind::enum_variant) continue;
      const auto equal = child.label.rfind('=');
      const auto [variant_name, payload_types] = split_variant_payload(child.label);
      std::int64_t value = 0;
      if (equal != std::string::npos) {
        try { value = std::stoll(child.label.substr(equal + 1)); }
        catch (...) { diagnostics_.error("D2030", child.range, "invalid enum discriminant"); }
      }
      if (!names.emplace(variant_name, true).second) {
        diagnostics_.error("D2031", child.range, "duplicate enum variant '" + variant_name + "'");
      }
      if (!discriminants.emplace(value, true).second) {
        diagnostics_.error("D2032", child.range, "duplicate enum discriminant " + std::to_string(value));
      }
      for (const auto& payload_type : payload_types) {
        bool is_parameter = std::find(generic_parameters.begin(), generic_parameters.end(), payload_type) != generic_parameters.end();
        if (const auto symbolic = split_symbolic_array_type(payload_type)) {
          is_parameter = is_parameter ||
              std::find(generic_parameters.begin(), generic_parameters.end(), symbolic->first) != generic_parameters.end() ||
              std::find(generic_parameters.begin(), generic_parameters.end(), symbolic->second) != generic_parameters.end();
        }
        if (!is_parameter && !type_exists(payload_type)) {
          diagnostics_.error("D2040", child.range, "unknown enum payload type '" + payload_type + "'");
        }
      }
      HirEnumVariant variant;
      variant.name = variant_name;
      variant.payload_types = payload_types;
      variant.discriminant = value;
      variant.range = child.range;
      enumeration.variants.push_back(std::move(variant));
    }

    if (enumeration.variants.empty()) diagnostics_.error("D2033", node.range, "enum must declare at least one variant");
    std::uint64_t maximum_payload_size = 0;
    std::uint32_t maximum_payload_alignment = 1;
    for (auto& variant : enumeration.variants) {
      std::uint64_t payload_cursor = 0;
      for (const auto& payload_type : variant.payload_types) {
        bool generic_payload = std::find(generic_parameters.begin(), generic_parameters.end(), payload_type) != generic_parameters.end();
        if (const auto symbolic = split_symbolic_array_type(payload_type)) {
          generic_payload = generic_payload ||
              std::find(generic_parameters.begin(), generic_parameters.end(), symbolic->first) != generic_parameters.end() ||
              std::find(generic_parameters.begin(), generic_parameters.end(), symbolic->second) != generic_parameters.end();
        }
        std::uint64_t size = generic_payload ? 8ULL :
            (enum_types_.contains(payload_type) ? enum_types_.at(payload_type).size :
             payload_type == "bool" || payload_type == "i8" || payload_type == "u8" || payload_type == "byte" ? 1ULL :
             payload_type == "i16" || payload_type == "u16" || payload_type == "f16" ? 2ULL :
             payload_type == "i32" || payload_type == "u32" || payload_type == "f32" || payload_type == "char" ? 4ULL : 8ULL);
        std::uint32_t alignment = static_cast<std::uint32_t>(size);
        if (const auto nested = type_layouts_.find(payload_type); nested != type_layouts_.end()) {
          size = nested->second.size;
          alignment = nested->second.alignment;
        }
        payload_cursor = align_up(payload_cursor, alignment);
        variant.payload_offsets.push_back(payload_cursor);
        payload_cursor += size;
        variant.payload_alignment = std::max(variant.payload_alignment, alignment);
      }
      variant.payload_size = align_up(payload_cursor, variant.payload_alignment);
      maximum_payload_size = std::max(maximum_payload_size, variant.payload_size);
      maximum_payload_alignment = std::max(maximum_payload_alignment, variant.payload_alignment);
    }
    enumeration.payload_offset = align_up(4, maximum_payload_alignment);
    enumeration.alignment = std::max<std::uint32_t>(4, maximum_payload_alignment);
    enumeration.size = align_up(enumeration.payload_offset + maximum_payload_size, enumeration.alignment);
    enum_types_.emplace(enumeration.name, enumeration);
    module.enums.push_back(std::move(enumeration));
    return;
  }

  if (node.kind == SyntaxKind::trait_declaration) {
    const auto equal = node.label.find('=');
    const auto colon = equal == std::string::npos ? node.label.find(':') : std::string::npos;
    const auto trait_header = equal != std::string::npos ? node.label.substr(0, equal) :
                              (colon == std::string::npos ? node.label : node.label.substr(0, colon));
    const auto [trait_name, raw_parameters] = split_generic_name(trait_header);
    std::vector<std::string> supertraits;
    std::vector<std::string> alias_targets;
    const auto split_traits = [](const std::string& value) {
      std::vector<std::string> result;
      std::size_t start = 0;
      while (start <= value.size()) {
        const auto plus = value.find('+', start);
        result.push_back(value.substr(start, plus == std::string::npos ? std::string::npos : plus - start));
        if (plus == std::string::npos) break;
        start = plus + 1;
      }
      return result;
    };
    if (colon != std::string::npos) supertraits = split_traits(node.label.substr(colon + 1));
    if (equal != std::string::npos) alias_targets = split_traits(node.label.substr(equal + 1));
    std::vector<std::string> generic_parameters;
    for (const auto& raw : raw_parameters) generic_parameters.push_back(split_generic_parameter_spec(raw).first);
    Symbol symbol;
    symbol.kind = SymbolKind::type;
    symbol.name = trait_name;
    symbol.type_name = trait_name;
    symbol.declaration = node.range;
    if (!declare(symbol)) {
      diagnostics_.error("D2001", node.range, "duplicate top-level declaration '" + trait_name + "'");
      return;
    }
    HirTrait trait;
    trait.name = trait_name;
    trait.generic_parameters = generic_parameters;
    trait.supertraits = supertraits;
    trait.alias_targets = alias_targets;
    trait_supertraits_[trait_name] = supertraits;
    if (!alias_targets.empty()) {
      if (!generic_parameters.empty()) {
        diagnostics_.error("D2140", node.range, "parameterized trait aliases are outside the stable Raz 1.0 language");
      }
      bool alias_valid = true;
      for (const auto& target : alias_targets) {
        if (target == trait_name) {
          diagnostics_.error("D2153", node.range, "trait alias '" + trait_name + "' cannot reference itself");
          alias_valid = false;
        } else if (!trait_contracts_.contains(target) && !trait_aliases_.contains(target) &&
                   target != "Copy" && target != "Clone" && target != "Drop") {
          diagnostics_.error("D2141", node.range, "unknown trait alias target '" + target + "'");
          alias_valid = false;
        }
      }
      if (alias_valid) trait_aliases_[trait_name] = alias_targets;
      module.traits.push_back(std::move(trait));
      return;
    }

    for (const auto& supertrait : supertraits) {
      if (!trait_contracts_.contains(supertrait) && supertrait != "Copy" && supertrait != "Clone" && supertrait != "Drop") {
        diagnostics_.error("D2113", node.range, "unknown supertrait '" + supertrait + "'");
      }
    }
    trait.range = node.range;
    // Ensure associated-item-only traits participate in trait lookup even when
    // they declare no methods.
    (void)trait_contracts_[trait_name];
    std::unordered_set<std::string> method_names;
    for (const auto& child : node.children) {
      if (child.kind == SyntaxKind::associated_type_declaration) {
        if (!trait_associated_types_[trait_name].emplace(child.label, child.range).second)
          diagnostics_.error("D2100", child.range, "duplicate associated type '" + child.label + "'");
        else trait.associated_types.push_back({child.label, child.range});
        continue;
      }
      if (child.kind == SyntaxKind::associated_const_declaration) {
        const auto associated_colon = child.label.find(':');
        const auto associated_equal = child.label.find('=');
        const auto name = child.label.substr(0, associated_colon);
        const auto type = child.label.substr(associated_colon + 1, (associated_equal == std::string::npos ? child.label.size() : associated_equal) - associated_colon - 1);
        if (!type_exists(type)) diagnostics_.error("D2101", child.range, "unknown associated constant type '" + type + "'");
        if (!trait_associated_constants_[trait_name].emplace(name, type).second)
          diagnostics_.error("D2102", child.range, "duplicate associated constant '" + name + "'");
        else trait.associated_constants.push_back({name, type, child.range});
        continue;
      }
      if (child.kind != SyntaxKind::function_declaration) {
        diagnostics_.error("D2089", child.range, "trait declarations may only contain methods or associated items");
        continue;
      }
      const auto arrow = child.label.find(" -> ");
      const auto header = arrow == std::string::npos ? child.label : child.label.substr(0, arrow);
      const auto [method_name, ignored] = split_generic_name(header);
      if (!method_names.insert(method_name).second) {
        diagnostics_.error("D2090", child.range, "duplicate trait method '" + method_name + "'");
        continue;
      }
      TraitMethodContract contract;
      contract.name = method_name;
      contract.return_type = function_return_type(child.label);
      for (const auto& attribute : split_attributes(child.modifier)) contract.attributes.insert(attribute);
      contract.range = child.range;
      HirTraitMethod hir_method;
      hir_method.name = method_name;
      hir_method.return_type = contract.return_type;
      hir_method.has_default = std::any_of(child.children.begin(), child.children.end(),
          [](const SyntaxNode& candidate) { return candidate.kind == SyntaxKind::block_statement; });
      hir_method.range = child.range;
      if (hir_method.has_default) trait_default_methods_[trait_name][method_name] = child;
      for (const auto& parameter : child.children) {
        if (parameter.kind != SyntaxKind::parameter) continue;
        const auto [type_name, name] = split_typed_name(parameter.label);
        contract.parameter_types.push_back(type_name);
        hir_method.parameters.push_back({name, type_name, parameter.range});
      }
      hir_method.vtable_slot = static_cast<std::uint32_t>(trait.methods.size());
      trait_contracts_[trait_name].push_back(contract);
      trait.methods.push_back(std::move(hir_method));
    }
    bool object_safe = generic_parameters.empty() && supertraits.empty() &&
                       trait.associated_types.empty() && trait.associated_constants.empty() &&
                       trait.alias_targets.empty() && trait.methods.size() <= 8;
    if (object_safe) {
      for (const auto& contract : trait_contracts_[trait_name]) {
        if (contract.parameter_types.empty()) { object_safe = false; break; }
        const auto& receiver = contract.parameter_types.front();
        if (receiver != "Self&" && receiver != "Self&mut") {
          object_safe = false;
          break;
        }
        // The first native trait-object ABI intentionally uses pointer receivers with i64
        // method arguments/results. Reject unsupported signatures instead of miscompiling them.
        if (contract.return_type != "i64") { object_safe = false; break; }
        if (std::any_of(contract.parameter_types.begin() + 1, contract.parameter_types.end(),
                        [](const std::string& type) { return type != "i64"; })) { object_safe = false; break; }
      }
    }

    if (object_safe) object_safe_traits_.insert(trait_name);
    trait.object_safe = object_safe;
    module.traits.push_back(std::move(trait));
    return;
  }

  if (node.kind == SyntaxKind::struct_declaration) {
    const auto [declared_name, raw_generic_parameters] = split_generic_name(node.label);
    std::vector<std::string> generic_parameters;
    std::vector<std::string> generic_const_types;
    for (const auto& raw : raw_generic_parameters) {
      const auto detail = parse_generic_parameter_detail(raw);
      generic_parameters.push_back(detail.name);
      generic_const_types.push_back(detail.const_type);
    }
    Symbol symbol{SymbolKind::type, declared_name, declared_name, {}, {}, node.range, false, {}, {}, {}, {}, {}};
    if (!declare(symbol)) {
      diagnostics_.error("D2001", node.range, "duplicate top-level declaration '" + declared_name + "'");
    } else {
      if (!generic_parameters.empty()) {
        generic_types_[declared_name] = generic_parameters;
        generic_const_types_[declared_name] = generic_const_types;
      }
      HirType type;
      type.name = declared_name;
      type.generic_parameters = generic_parameters;
      type.generic_const_types = generic_const_types;
      type.range = node.range;
      type.attributes = split_attributes(node.modifier);
      bool packed_layout = false;
      std::uint32_t requested_alignment = 0;
      for (const auto& attribute : type.attributes) {
        if (attribute == "packed") {
          packed_layout = true;
          continue;
        }
        if (attribute.starts_with("repr(") && attribute.back() == ')') {
          const auto representation = attribute.substr(5, attribute.size() - 6);
          if (representation != "C" && representation != "Raz") {
            diagnostics_.error("D2225", node.range, "unsupported struct representation '" + representation + "'");
          } else {
            type.representation = representation;
          }
          continue;
        }
        if (attribute.starts_with("align(") && attribute.back() == ')') {
          try {
            const auto value = static_cast<std::uint32_t>(std::stoul(attribute.substr(6, attribute.size() - 7)));
            if (value == 0 || (value & (value - 1)) != 0 || value > 4096) {
              diagnostics_.error("D2226", node.range, "@align requires a power of two between 1 and 4096");
            } else {
              requested_alignment = value;
            }
          } catch (...) {
            diagnostics_.error("D2226", node.range, "@align requires an integer power-of-two argument");
          }
          continue;
        }
        if (attribute.starts_with("derive(")) continue;
        diagnostics_.error("D2227", node.range, "unknown struct attribute '@" + attribute + "'");
      }
      type.packed = packed_layout;
      type.requested_alignment = requested_alignment;
      std::uint64_t offset = 0;
      std::uint32_t max_alignment = 1;
      for (const auto& child : node.children) {
        if (child.kind != SyntaxKind::field_declaration) continue;
        const auto [field_type, field_name] = split_typed_name(child.label);
        bool generic_field = std::find(generic_parameters.begin(), generic_parameters.end(), field_type) != generic_parameters.end();
        if (const auto symbolic = split_symbolic_array_type(field_type)) {
          generic_field = generic_field ||
              std::find(generic_parameters.begin(), generic_parameters.end(), symbolic->first) != generic_parameters.end() ||
              std::find(generic_parameters.begin(), generic_parameters.end(), symbolic->second) != generic_parameters.end();
        }
        if (!generic_field && !type_exists(field_type)) diagnostics_.error("D2002", child.range, "unknown field type '" + field_type + "'");
        std::uint64_t size = generic_field ? 8ULL : 0;
        std::uint32_t alignment = 1;
        if (generic_field) {
          alignment = 8;
        } else if (const auto array = parse_fixed_array_type(field_type)) {
          const auto nested = type_layouts_.find(array->element_type);
          const auto element_size = nested != type_layouts_.end() ? nested->second.size :
              (enum_types_.contains(array->element_type) ? 4ULL :
               array->element_type == "bool" || array->element_type == "i8" || array->element_type == "u8" || array->element_type == "byte" ? 1ULL :
               array->element_type == "i16" || array->element_type == "u16" ? 2ULL :
               array->element_type == "i32" || array->element_type == "u32" || array->element_type == "f32" || array->element_type == "char" ? 4ULL : 8ULL);
          const auto element_alignment = nested != type_layouts_.end() ? nested->second.alignment : static_cast<std::uint32_t>(element_size);
          size = element_size * array->length;
          alignment = element_alignment;
        } else if (const auto nested = type_layouts_.find(field_type); nested != type_layouts_.end()) {
          size = nested->second.size;
          alignment = nested->second.alignment;
        } else {
          size = enum_types_.contains(field_type) ? 4ULL :
                 field_type == "bool" || field_type == "i8" || field_type == "u8" || field_type == "byte" ? 1ULL :
                 field_type == "i16" || field_type == "u16" ? 2ULL :
                 field_type == "i32" || field_type == "u32" || field_type == "f32" || field_type == "char" ? 4ULL : 8ULL;
          alignment = static_cast<std::uint32_t>(size);
        }
        const auto effective_alignment = packed_layout ? 1U : alignment;
        offset = align_up(offset, effective_alignment);
        type.fields.push_back({field_name, field_type, offset, size, effective_alignment, child.range});
        offset += size;
        if (effective_alignment > max_alignment) max_alignment = effective_alignment;
      }
      if (requested_alignment > max_alignment) max_alignment = requested_alignment;
      type.alignment = max_alignment;
      type.size = align_up(offset, max_alignment);
      type_layouts_.emplace(type.name, type);
      module.types.push_back(std::move(type));
    }
    return;
  }

  if (node.kind != SyntaxKind::function_declaration) return;
  Symbol symbol;
  symbol.kind = SymbolKind::function;
  const auto arrow = node.label.find(" -> ");
  const auto function_header = arrow == std::string::npos ? node.label : node.label.substr(0, arrow);
  const auto [function_name, all_function_parameters] = split_generic_name(function_header);
  std::vector<std::string> function_generics;
  for (const auto& parameter : all_function_parameters) {
    if (!parameter.empty() && parameter.front() == '\'') {
      symbol.lifetime_parameters.push_back(parameter);
    } else {
      const auto detail = parse_generic_parameter_detail(parameter);
      function_generics.push_back(detail.name);
      symbol.generic_bounds.push_back(detail.bounds);
      symbol.generic_const_types.push_back(detail.const_type);
    }
  }
  symbol.name = function_name;
  symbol.type_name = function_return_type(node.label);
  symbol.declaration = node.range;
  for (const auto& child : node.children) {
    if (child.kind != SyntaxKind::parameter) continue;
    const auto parameter_type = split_typed_name(child.label).first;
    symbol.parameter_types.push_back(parameter_type);
    const auto reference = parse_reference_type(parameter_type);
    symbol.parameter_lifetimes.push_back(reference ? reference->lifetime : std::string{});
  }

  if (const auto reference = parse_reference_type(symbol.type_name)) symbol.return_lifetime = reference->lifetime;
  symbol.generic_parameters = function_generics;
  if (!declare(symbol)) diagnostics_.error("D2001", node.range, "duplicate top-level declaration '" + symbol.name + "'");
}

void SemanticAnalyzer::analyze_function(const SyntaxNode& node, HirModule& module) {
  HirFunction function;
  const auto arrow = node.label.find(" -> ");
  const auto function_header = arrow == std::string::npos ? node.label : node.label.substr(0, arrow);
  const auto [function_name, all_function_parameters] = split_generic_name(function_header);
  std::vector<std::string> generic_parameters;
  for (const auto& parameter : all_function_parameters) {
    if (!parameter.empty() && parameter.front() == '\'') {
      function.lifetime_parameters.push_back(parameter);
    } else {
      const auto detail = parse_generic_parameter_detail(parameter);
      generic_parameters.push_back(detail.name);
      function.generic_bounds.push_back(detail.bounds);
      function.generic_const_types.push_back(detail.const_type);
    }
  }
  function.name = function_name;
  function.attributes = split_attributes(node.modifier);
  const auto saved_unsafe_depth = unsafe_depth_;
  const auto saved_async_depth = async_depth_;
  unsafe_depth_ = std::find(function.attributes.begin(), function.attributes.end(), "unsafe") != function.attributes.end() ? 1 : 0;
  async_depth_ = std::find(function.attributes.begin(), function.attributes.end(), "async") != function.attributes.end() ? 1 : 0;
  current_function_attributes_.clear();
  current_function_attributes_.insert(function.attributes.begin(), function.attributes.end());
  for (const auto& attribute : function.attributes) {
    const auto name = attribute_name(attribute);
    if (name != "pure" && name != "no_panic" && name != "no_alloc" &&
        name != "deterministic" && name != "const" && name != "inline" && name != "cold" && name != "hot" && name != "extern" && name != "unsafe" && name != "async" &&
        name != "abi" && name != "link_name" && name != "no_mangle" && name != "target_feature" && name != "interface") {
      diagnostics_.error("D2214", node.range, "unknown function attribute '@" + attribute + "'");
    }
  }
  function.is_external =
      std::find(function.attributes.begin(), function.attributes.end(), "extern") != function.attributes.end() ||
      std::find(function.attributes.begin(), function.attributes.end(), "interface") != function.attributes.end();
  function.is_async = std::find(function.attributes.begin(), function.attributes.end(), "async") != function.attributes.end();
  for (const auto& attribute : function.attributes) {
    const auto name = attribute_name(attribute);
    const auto argument = attribute_argument(attribute);
    if (name == "abi") {
      if (argument != "C" && argument != "system" && argument != "Raz") diagnostics_.error("D2252", node.range, "unsupported ABI '" + argument + "'");
      else function.abi = argument;
    } else if (name == "link_name") {
      if (!function.is_external) diagnostics_.error("D2253", node.range, "@link_name is only valid on extern functions");
      if (argument.empty()) diagnostics_.error("D2254", node.range, "@link_name requires a non-empty symbol name");
      else function.external_name = argument;
    } else if (name == "target_feature") {
      if (argument != "sse2" && argument != "avx2" && argument != "neon") diagnostics_.error("D2255", node.range, "unsupported target feature '" + argument + "'");
      if (std::find(function.attributes.begin(), function.attributes.end(), "unsafe") == function.attributes.end()) diagnostics_.error("D2256", node.range, "@target_feature function must be unsafe");
    }
  }

  if (function.is_external && function.abi == "Raz") function.abi = "C";
  function.generic_parameters = generic_parameters;
  function.generic_template = !generic_parameters.empty();
  current_generic_bounds_.clear();
  for (std::size_t index = 0; index < generic_parameters.size(); ++index) {
    if (index < function.generic_bounds.size()) current_generic_bounds_[generic_parameters[index]] = function.generic_bounds[index];
  }
  function.return_type = function_return_type(node.label);
  current_return_type_ = function.return_type;
  moved_values_.clear();
  borrows_.clear();
  reference_bindings_.clear();
  reborrow_parents_.clear();
  suspended_references_.clear();
  current_reference_return_origins_.clear();
  aggregate_reference_origins_.clear();
  used_reference_bindings_.clear();
  declared_names_.clear();
  request_instantiation(function.return_type);
  function.range = node.range;
  const auto is_generic_parameter = [&](const std::string& type) {
    auto candidate = type;
    if (const auto reference = parse_reference_type(candidate)) candidate = reference->referent_type;
    if (const auto array = parse_fixed_array_type(candidate)) candidate = array->element_type;
    if (const auto symbolic = split_symbolic_array_type(candidate)) {
      return std::find(generic_parameters.begin(), generic_parameters.end(), symbolic->first) != generic_parameters.end() ||
             std::find(generic_parameters.begin(), generic_parameters.end(), symbolic->second) != generic_parameters.end();
    }

    return std::find(generic_parameters.begin(), generic_parameters.end(), candidate) != generic_parameters.end();
  };
  if (!type_exists(function.return_type) && !is_generic_parameter(function.return_type)) {
    diagnostics_.error("D2002", node.range, "unknown return type '" + function.return_type + "'");
  }

  push_scope();
  for (const auto& child : node.children) {
    if (child.kind != SyntaxKind::parameter) continue;
    const auto [type_name, name] = split_typed_name(child.label);
    if (!type_exists(type_name) && !is_generic_parameter(type_name)) diagnostics_.error("D2002", child.range, "unknown parameter type '" + type_name + "'");
    Symbol symbol{SymbolKind::parameter, name, type_name, {}, {}, child.range, false, {}, {}, {}, {}, {}};
    if (!declare(symbol)) diagnostics_.error("D2003", child.range, "duplicate parameter '" + name + "'");
    if (const auto reference = parse_reference_type(type_name)) {
      const bool mutable_reference = reference->mutable_reference;
      reference_bindings_[name] = {name, mutable_reference};
      auto& state = borrows_[name];
      if (mutable_reference) state.mutable_borrow = true;
      else ++state.shared;
    }

    request_instantiation(type_name);
    function.parameters.push_back({name, type_name, child.range});
  }

  for (const auto& child : node.children) {
    if (child.kind == SyntaxKind::block_statement) {
      function.body = child;
      analyze_statement(child, function, function.return_type);
    }
  }

  if (function.is_external && function.body.has_value()) {
    diagnostics_.error("D2220", function.range, "extern function must not have a body");
  }
  const bool interface_signature =
      std::find(function.attributes.begin(), function.attributes.end(), "interface") != function.attributes.end();
  if (!function.is_external && !function.body.has_value() && !interface_signature) {
    diagnostics_.error("D2221", function.range, "non-extern function requires a body");
  }

  if (function.body.has_value()) {
    const auto has_attribute = [&](const std::string& name) {
      return std::find(function.attributes.begin(), function.attributes.end(), name) != function.attributes.end();
    };
    if (has_attribute("no_panic") &&
        (syntax_contains_kind(*function.body, SyntaxKind::index_expression) ||
         syntax_contains_kind(*function.body, SyntaxKind::try_expression))) {
      diagnostics_.error("D2215", function.range,
                         "@no_panic function contains an operation that may panic");
    }

    if (has_attribute("pure") &&
        (syntax_contains_call(*function.body, "print") ||
         syntax_contains_kind(*function.body, SyntaxKind::unsafe_statement))) {
      diagnostics_.error("D2216", function.range,
                         "@pure function performs an observable side effect");
    }
    std::unordered_set<std::string> direct_calls;
    collect_direct_named_calls(*function.body, direct_calls);
    direct_calls.erase(function.name);
    direct_calls.erase("assert");
    direct_calls.erase("size_of");
    direct_calls.erase("align_of");
    direct_calls.erase("field_count");
    direct_calls.erase("variant_count");
    direct_calls.erase("attribute_count");
    direct_calls.erase("method_count");
    direct_calls.erase("method_name");
    direct_calls.erase("method_return_type_name");
    direct_calls.erase("method_parameter_count");
    direct_calls.erase("method_parameter_type_name");
    direct_calls.erase("method_has_attribute");
    direct_calls.erase("trait_method_count");
    direct_calls.erase("trait_method_name");
    direct_calls.erase("trait_method_return_type_name");
    direct_calls.erase("trait_method_parameter_count");
    direct_calls.erase("trait_method_parameter_type_name");
    direct_calls.erase("trait_method_has_attribute");
    direct_calls.erase("associated_type_count");
    direct_calls.erase("associated_type_name");
    direct_calls.erase("associated_const_count");
    direct_calls.erase("associated_const_name");
    direct_calls.erase("associated_const_type_name");
    direct_calls.erase("implements_trait");
    direct_calls.erase("impl_associated_type_count");
    direct_calls.erase("impl_associated_type_name");
    direct_calls.erase("impl_associated_type_binding_name");
    direct_calls.erase("impl_associated_const_count");
    direct_calls.erase("impl_associated_const_name");
    direct_calls.erase("impl_associated_const_type_name");
    direct_calls.erase("impl_associated_const_value");
    direct_calls.erase("payload_offset");
    direct_calls.erase("field_offset");
    direct_calls.erase("field_size");
    direct_calls.erase("field_align");
    direct_calls.erase("has_attribute");
    direct_calls.erase("variant_discriminant");
    direct_calls.erase("is_copy");
    direct_calls.erase("is_clone");
    direct_calls.erase("needs_drop");
    direct_calls.erase("type_name");
    for (const auto& callee : direct_calls) {
      const auto attributes = declared_function_attributes_.find(callee);
      if (attributes == declared_function_attributes_.end()) continue;
      const auto callee_has = [&](const std::string& effect) { return attributes->second.contains(effect); };
      if (has_attribute("pure") && !callee_has("pure")) {
        diagnostics_.error("D2228", function.range,
            "@pure function calls '" + callee + "', which is not declared @pure");
      }
      if (has_attribute("no_panic") && !callee_has("no_panic")) {
        diagnostics_.error("D2229", function.range,
            "@no_panic function calls '" + callee + "', which is not declared @no_panic");
      }
      if (has_attribute("no_alloc") && !callee_has("no_alloc")) {
        diagnostics_.error("D2230", function.range,
            "@no_alloc function calls '" + callee + "', which is not declared @no_alloc");
      }
      if (has_attribute("deterministic") && !callee_has("deterministic")) {
        diagnostics_.error("D2231", function.range,
            "@deterministic function calls '" + callee + "', which is not declared @deterministic");
      }
    }
  }

  if (const auto returned_reference = parse_reference_type(function.return_type); returned_reference) {
    if (current_reference_return_origins_.size() > 1) {
      bool related = !returned_reference->lifetime.empty();
      for (const auto& origin : current_reference_return_origins_) {
        const auto* parameter = lookup(origin);
        const auto parameter_reference = parameter ? parse_reference_type(parameter->type_name) : std::nullopt;
        if (!parameter_reference || parameter_reference->lifetime != returned_reference->lifetime) { related = false; break; }
      }
      if (!related) diagnostics_.error("D2067", node.range,
          "reference return has multiple parameter origins; an explicit shared lifetime relationship is required");
    }
  }

  pop_scope();
  current_return_type_.clear();
  current_generic_bounds_.clear();
  current_function_attributes_.clear();
  unsafe_depth_ = saved_unsafe_depth;
  async_depth_ = saved_async_depth;
  module.functions.push_back(std::move(function));
}
