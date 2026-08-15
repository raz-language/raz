// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

std::string SemanticAnalyzer::root_name(const std::string& place) {
  const auto end = place.find_first_of(".[");
  return place.substr(0, end);
}

static std::string enum_payload_place(const std::string& subject,
                                      const std::string& variant_name,
                                      std::size_t payload_index) {
  if (subject.empty()) return {};
  return subject + ".variant$" + variant_name + "[" +
         std::to_string(payload_index) + "]";
}

bool SemanticAnalyzer::places_overlap(const std::string& left, const std::string& right) {
  if (left == right) return true;
  const auto is_prefix = [](const std::string& prefix, const std::string& value) {
    if (!value.starts_with(prefix) || value.size() == prefix.size()) return false;
    const char boundary = value[prefix.size()];
    return boundary == '.' || boundary == '[';
  };
  if (is_prefix(left, right) || is_prefix(right, left)) return true;
  // Dynamic array indices conservatively overlap every element of the same array.
  const auto wildcard = [](const std::string& value) {
    const auto pos = value.find("[*]");
    return pos == std::string::npos ? value : value.substr(0, pos);
  };
  const auto left_base = wildcard(left);
  const auto right_base = wildcard(right);
  return (left.find("[*]") != std::string::npos || right.find("[*]") != std::string::npos) &&
         (is_prefix(left_base, right) || is_prefix(right_base, left) || left_base == right_base);
}

std::string SemanticAnalyzer::place_path(const SyntaxNode& node) const {
  if (node.kind == SyntaxKind::unary_expression && node.label == "*" &&
      !node.children.empty() && node.children.front().kind == SyntaxKind::name_expression) {
    const auto found = reference_bindings_.find(node.children.front().label);
    return found == reference_bindings_.end() ? std::string{} : found->second.first;
  }

  if (node.kind == SyntaxKind::name_expression) return node.label;
  if (node.kind == SyntaxKind::member_expression && !node.children.empty()) {
    const auto base = place_path(node.children.front());
    return base.empty() ? std::string{} : base + "." + node.label;
  }

  if (node.kind == SyntaxKind::index_expression && node.children.size() >= 2) {
    const auto base = place_path(node.children.front());
    if (base.empty()) return {};
    const auto& index = node.children[1];
    if (index.kind == SyntaxKind::literal_expression) return base + "[" + index.label + "]";
    return base + "[*]";
  }
  return {};
}

bool SemanticAnalyzer::has_active_borrow(const std::string& place) const {
  return std::any_of(borrows_.begin(), borrows_.end(), [&](const auto& entry) {
    return places_overlap(place, entry.first) &&
           (entry.second.shared != 0 || entry.second.mutable_borrow);
  });
}

bool SemanticAnalyzer::is_moved_place(const std::string& place) const {
  return std::any_of(moved_values_.begin(), moved_values_.end(),
                     [&](const std::string& moved) { return places_overlap(place, moved); });
}

void SemanticAnalyzer::mark_moved_place(const std::string& place, const std::string& type_name,
                                         SourceRange range) {
  const auto owner = root_name(place);
  const auto* symbol = lookup(owner);
  if (symbol == nullptr || (symbol->kind != SymbolKind::variable && symbol->kind != SymbolKind::parameter)) {
    diagnostics_.error("D2051", range, "move requires a local variable, parameter, field, or indexed element");
    return;
  }

  if (has_active_borrow(place)) {
    diagnostics_.error("D2055", range, "cannot move '" + place + "' while it is borrowed");
    return;
  }

  if (place != owner && explicit_drop_types_.contains(symbol->type_name)) {
    diagnostics_.error("D2135", range, "cannot partially move '" + place +
                       "' because '" + symbol->type_name + "' implements Drop");
    return;
  }

  if (is_copy_type(type_name)) {
    diagnostics_.warning("D2052", range, "moving Copy value '" + place + "' is equivalent to copying it");
    return;
  }

  if (is_moved_place(place)) {
    diagnostics_.error("D2053", range, "value '" + place + "' has already been moved");
    return;
  }
  moved_values_.insert(place);
}

void SemanticAnalyzer::mark_moved(const std::string& name, SourceRange range) {
  const auto* symbol = lookup(name);
  if (symbol == nullptr) {
    diagnostics_.error("D2051", range, "move requires a local variable or parameter");
    return;
  }

  mark_moved_place(name, symbol->type_name, range);
}

void SemanticAnalyzer::mark_initialized(const std::string& name) {
  for (auto iterator = moved_values_.begin(); iterator != moved_values_.end();) {
    if (*iterator == name || iterator->starts_with(name + ".") || iterator->starts_with(name + "["))
      iterator = moved_values_.erase(iterator);
    else
      ++iterator;
  }
}

SemanticAnalyzer::OwnershipSnapshot SemanticAnalyzer::ownership_snapshot() const {
  return OwnershipSnapshot{moved_values_, borrows_, reference_bindings_,
                           reborrow_parents_, suspended_references_,
                           aggregate_reference_origins_};
}

void SemanticAnalyzer::restore_ownership(const OwnershipSnapshot& snapshot) {
  moved_values_ = snapshot.moved_values;
  borrows_ = snapshot.borrows;
  reference_bindings_ = snapshot.reference_bindings;
  reborrow_parents_ = snapshot.reborrow_parents;
  suspended_references_ = snapshot.suspended_references;
  aggregate_reference_origins_ = snapshot.aggregate_reference_origins;
}

std::unordered_set<std::string> SemanticAnalyzer::union_moved_states(
    const std::vector<std::unordered_set<std::string>>& states) {
  std::unordered_set<std::string> result;
  for (const auto& state : states) result.insert(state.begin(), state.end());

  // Canonicalize projection state after a control-flow join. Once an ancestor
  // place is moved, retaining child projections is redundant and can make
  // later joins depend on traversal order. Keep only the minimal covering set.
  std::vector<std::string> ordered(result.begin(), result.end());
  std::sort(ordered.begin(), ordered.end(), [](const std::string& left, const std::string& right) {
    if (left.size() != right.size()) return left.size() < right.size();
    return left < right;
  });
  std::unordered_set<std::string> normalized;
  for (const auto& place : ordered) {
    const bool covered = std::any_of(normalized.begin(), normalized.end(),
        [&](const std::string& ancestor) { return places_overlap(ancestor, place); });
    if (!covered) normalized.insert(place);
  }
  return normalized;
}

void SemanticAnalyzer::begin_borrow(const std::string& place, bool mutable_borrow,
                                    const std::string& binding, SourceRange range) {
  const auto owner = root_name(place);
  const auto* symbol = lookup(owner);
  if (symbol == nullptr) {
    diagnostics_.error("D2008", range, "unknown name '" + owner + "'");
    return;
  }

  if (is_moved_place(place)) {
    diagnostics_.error("D2054", range, "use of moved value '" + owner + "'");
    return;
  }

  for (const auto& [active_place, state] : borrows_) {
    if (!places_overlap(place, active_place)) continue;
    if (mutable_borrow && (state.mutable_borrow || state.shared != 0)) {
      diagnostics_.error("D2056", range, "cannot mutably borrow '" + place + "' while an overlapping borrow is active");
      return;
    }

    if (!mutable_borrow && state.mutable_borrow) {
      diagnostics_.error("D2057", range, "cannot immutably borrow '" + place + "' while an overlapping mutable borrow is active");
      return;
    }
  }
  auto& state = borrows_[place];
  if (mutable_borrow) state.mutable_borrow = true;
  else ++state.shared;
  reference_bindings_[binding] = {place, mutable_borrow};
}

bool SemanticAnalyzer::begin_reborrow(const SyntaxNode& operand, bool mutable_borrow,
                                      const std::string& binding, SourceRange range) {
  if (operand.kind != SyntaxKind::unary_expression || operand.label != "*" ||
      operand.children.empty() || operand.children.front().kind != SyntaxKind::name_expression) {
    return false;
  }
  const auto& parent = operand.children.front().label;
  const auto found = reference_bindings_.find(parent);
  if (found == reference_bindings_.end()) return false;
  if (suspended_references_.contains(parent)) {
    diagnostics_.error("D2062", range, "cannot reborrow through suspended reference '" + parent + "'");
    return true;
  }

  if (mutable_borrow && !found->second.second) {
    diagnostics_.error("D2063", range, "mutable reborrow requires a mutable reference");
    return true;
  }
  const auto [place, parent_mutable] = found->second;
  if (auto state = borrows_.find(place); state != borrows_.end()) {
    if (parent_mutable) state->second.mutable_borrow = false;
    else if (state->second.shared != 0) --state->second.shared;
    if (!state->second.mutable_borrow && state->second.shared == 0) borrows_.erase(state);
  }
  suspended_references_.insert(parent);
  begin_borrow(place, mutable_borrow, binding, range);
  if (reference_bindings_.contains(binding)) reborrow_parents_[binding] = parent;
  return true;
}

bool SemanticAnalyzer::syntax_uses_name(const SyntaxNode& node, const std::string& name) {
  if (node.kind == SyntaxKind::name_expression && node.label == name) return true;
  return std::any_of(node.children.begin(), node.children.end(),
                     [&](const SyntaxNode& child) { return syntax_uses_name(child, name); });
}

void SemanticAnalyzer::release_dead_references(const std::vector<SyntaxNode>& statements,
                                                std::size_t completed_index) {
  std::vector<std::string> dead;
  for (const auto& [binding, ignored] : reference_bindings_) {
    (void)ignored;
    const auto* symbol = lookup(binding);
    if (symbol == nullptr || symbol->kind != SymbolKind::variable) continue;
    bool used_later = false;
    for (std::size_t index = completed_index + 1; index < statements.size(); ++index) {
      if (syntax_uses_name(statements[index], binding)) { used_later = true; break; }
    }

    if (!used_later) dead.push_back(binding);
  }

  for (const auto& binding : dead) release_borrow(binding);
}

std::string SemanticAnalyzer::reference_origin(const SyntaxNode& node) const {
  if (node.kind == SyntaxKind::name_expression) {
    const auto found = reference_bindings_.find(node.label);
    return found == reference_bindings_.end() ? node.label : found->second.first;
  }

  if (node.kind == SyntaxKind::unary_expression && (node.label == "&" || node.label == "&mut") &&
      !node.children.empty()) return place_path(node.children.front());
  if (node.kind == SyntaxKind::parenthesized_expression && !node.children.empty())
    return reference_origin(node.children.front());
  if (node.kind == SyntaxKind::call_expression && !node.children.empty() &&
      node.children.front().kind == SyntaxKind::name_expression) {
    const auto [function_name, ignored_arguments] = split_generic_name(node.children.front().label);
    (void)ignored_arguments;
    const auto* function = lookup(function_name);
    if (function != nullptr && !function->return_lifetime.empty()) {
      for (std::size_t index = 0; index < function->parameter_lifetimes.size(); ++index) {
        if (function->parameter_lifetimes[index] == function->return_lifetime && index + 1 < node.children.size())
          return reference_origin(node.children[index + 1]);
      }
    }
  }
  return {};
}

std::string SemanticAnalyzer::aggregate_local_reference_origin(const SyntaxNode& node) const {
  auto local_origin = [&](const SyntaxNode& value) -> std::string {
    const auto origin = reference_origin(value);
    if (origin.empty()) return {};
    const auto* owner = lookup(root_name(origin));
    return owner != nullptr && owner->kind == SymbolKind::variable ? origin : std::string{};
  };

  if (node.kind == SyntaxKind::unary_expression &&
      (node.label == "&" || node.label == "&mut")) {
    return local_origin(node);
  }

  if (node.kind == SyntaxKind::name_expression) {
    if (const auto aggregate = aggregate_reference_origins_.find(node.label);
        aggregate != aggregate_reference_origins_.end()) return aggregate->second;
    const auto* symbol = lookup(node.label);
    if (symbol != nullptr && parse_reference_type(symbol->type_name)) return local_origin(node);
    return {};
  }

  if (node.kind == SyntaxKind::call_expression && !node.children.empty() &&
      node.children.front().kind == SyntaxKind::name_expression) {
    const auto [function_name, ignored_arguments] = split_generic_name(node.children.front().label);
    (void)ignored_arguments;
    const auto* function = lookup(function_name);
    if (function != nullptr && parse_reference_type(function->type_name)) return local_origin(node);
    return {};
  }

  if (node.kind == SyntaxKind::parenthesized_expression && !node.children.empty())
    return aggregate_local_reference_origin(node.children.front());

  if (node.kind == SyntaxKind::struct_expression ||
      node.kind == SyntaxKind::tuple_expression ||
      node.kind == SyntaxKind::array_expression ||
      node.kind == SyntaxKind::field_initializer) {
    for (const auto& child : node.children) {
      const auto origin = aggregate_local_reference_origin(child);
      if (!origin.empty()) return origin;
    }
  }
  return {};
}

void SemanticAnalyzer::release_borrow(const std::string& binding) {
  const auto found = reference_bindings_.find(binding);
  if (found == reference_bindings_.end()) return;
  const auto [place, mutable_borrow] = found->second;
  if (auto state = borrows_.find(place); state != borrows_.end()) {
    if (mutable_borrow) state->second.mutable_borrow = false;
    else if (state->second.shared != 0) --state->second.shared;
    if (!state->second.mutable_borrow && state->second.shared == 0) borrows_.erase(state);
  }
  reference_bindings_.erase(found);
  used_reference_bindings_.erase(binding);
  const auto parent = reborrow_parents_.find(binding);
  if (parent != reborrow_parents_.end()) {
    const auto parent_binding = reference_bindings_.find(parent->second);
    if (parent_binding != reference_bindings_.end()) {
      auto& parent_state = borrows_[parent_binding->second.first];
      if (parent_binding->second.second) parent_state.mutable_borrow = true;
      else ++parent_state.shared;
      suspended_references_.erase(parent->second);
    }
    reborrow_parents_.erase(parent);
  }
}

bool SemanticAnalyzer::declare(Symbol symbol) {
  auto& table = scopes_.empty() ? globals_ : scopes_.back().symbols;
  const auto name = symbol.name;
  const auto [_, inserted] = table.emplace(name, std::move(symbol));
  if (inserted && !scopes_.empty() && !declared_names_.empty()) declared_names_.back().push_back(name);
  return inserted;
}

void SemanticAnalyzer::push_scope() {
  scopes_.emplace_back();
  declared_names_.emplace_back();
}

void SemanticAnalyzer::pop_scope() {
  if (!declared_names_.empty()) {
    for (const auto& name : declared_names_.back()) {
      moved_values_.erase(name);
      aggregate_reference_origins_.erase(name);
      release_borrow(name);
    }
    declared_names_.pop_back();
  }
  scopes_.pop_back();
}

std::string SemanticAnalyzer::closure_function_name(const SyntaxNode& node) {
  const auto arrow = node.label.find(" -> ");
  return arrow == std::string::npos ? node.label : node.label.substr(0, arrow);
}

bool SemanticAnalyzer::closure_coerces_to_function(const SyntaxNode& node, const std::string& destination) const {
  if (node.kind != SyntaxKind::closure_expression) return false;
  const auto function_type = parse_function_type(destination);
  if (!function_type) return false;
  const auto name = closure_function_name(node);
  const auto captures = closure_captures_.find(name);
  if (captures != closure_captures_.end() && !captures->second.empty()) return false;
  const auto* symbol = lookup(name);
  if (symbol == nullptr || symbol->kind != SymbolKind::function) return false;
  return symbol->parameter_types == function_type->parameter_types && symbol->type_name == function_type->return_type;
}

bool SemanticAnalyzer::closure_coerces_to_callable(const SyntaxNode& node, const std::string& destination) const {
  if (node.kind != SyntaxKind::closure_expression) return false;
  const auto callable = parse_callable_type(destination);
  if (!callable) return false;
  const auto name = closure_function_name(node);
  const auto* symbol = lookup(name);
  if (symbol == nullptr || symbol->kind != SymbolKind::function) return false;
  const auto captures = closure_captures_.find(name);
  const auto capture_count = captures == closure_captures_.end() ? 0 : captures->second.size();
  if (symbol->parameter_types.size() < capture_count) return false;
  std::vector<std::string> explicit_parameters(symbol->parameter_types.begin() + static_cast<std::ptrdiff_t>(capture_count),
                                               symbol->parameter_types.end());
  CallableKind actual_kind = CallableKind::shared;
  if (move_closure_functions_.contains(name)) actual_kind = CallableKind::once;
  else if (mutable_closure_functions_.contains(name)) actual_kind = CallableKind::mutable_call;
  return callable_kinds_compatible(actual_kind, callable->kind) &&
         explicit_parameters == callable->parameter_types && symbol->type_name == callable->return_type;
}

bool SemanticAnalyzer::callable_identity_compatible(const std::string& actual, const std::string& expected) const {
  const auto callable = parse_callable_type(expected);
  if (!callable) return false;
  CallableKind actual_kind = CallableKind::shared;
  std::string prefix;
  if (actual.rfind("FnOnce<", 0) == 0) { actual_kind = CallableKind::once; prefix = "FnOnce<"; }
  else if (actual.rfind("FnMut<", 0) == 0) { actual_kind = CallableKind::mutable_call; prefix = "FnMut<"; }
  else if (actual.rfind("Fn<", 0) == 0) { actual_kind = CallableKind::shared; prefix = "Fn<"; }
  else return false;
  if (actual.back() != '>') return false;
  const auto function_name = actual.substr(prefix.size(), actual.size() - prefix.size() - 1);
  const auto* symbol = lookup(function_name);
  if (symbol == nullptr || symbol->kind != SymbolKind::function) return false;
  const auto captures = closure_captures_.find(function_name);
  const auto capture_count = captures == closure_captures_.end() ? 0 : captures->second.size();
  if (symbol->parameter_types.size() < capture_count) return false;
  std::vector<std::string> explicit_parameters(symbol->parameter_types.begin() + static_cast<std::ptrdiff_t>(capture_count),
                                               symbol->parameter_types.end());
  return callable_kinds_compatible(actual_kind, callable->kind) &&
         explicit_parameters == callable->parameter_types && symbol->type_name == callable->return_type;
}

void SemanticAnalyzer::collect_closure_declarations(const SyntaxNode& node,
                                                    std::vector<SyntaxNode>& closures) {
  if (node.kind == SyntaxKind::closure_expression) {
    SyntaxNode function;
    function.kind = SyntaxKind::function_declaration;
    function.range = node.range;
    function.label = node.label;
    function.children = node.children;
    closures.push_back(std::move(function));
  }

  for (const auto& child : node.children) collect_closure_declarations(child, closures);
}

std::pair<std::string, std::string> SemanticAnalyzer::evaluate_constant(const SyntaxNode& node) {
