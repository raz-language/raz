// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/ir/hir/hir.hpp"
#include "compiler/semantic/symbol.hpp"
#include "compiler/syntax/syntax_tree.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace raz::compiler {

class DiagnosticEngine;

class SemanticAnalyzer final {
 public:
  explicit SemanticAnalyzer(DiagnosticEngine& diagnostics);
  [[nodiscard]] HirModule analyze(const SyntaxTree& tree);

 private:
  struct Scope final { std::unordered_map<std::string, Symbol> symbols; };
  struct BorrowState final { std::size_t shared = 0; bool mutable_borrow = false; };
  struct TraitMethodContract final {
    std::string name;
    std::string return_type;
    std::vector<std::string> parameter_types;
    std::unordered_set<std::string> attributes;
    SourceRange range{};
  };

  struct GenericTraitImplementation final {
    std::string trait_name;
    std::string target_pattern;
    std::vector<std::string> parameters;
    std::vector<std::vector<std::string>> bounds;
    std::vector<std::string> const_types;
    std::unordered_map<std::string, std::string> associated_type_bindings;
    std::unordered_map<std::string, std::pair<std::string, std::string>> associated_const_bindings;
    std::unordered_map<std::string, SyntaxNode> methods;
    SourceRange range{};
  };

  struct OwnershipSnapshot final {
    std::unordered_set<std::string> moved_values;
    std::unordered_map<std::string, BorrowState> borrows;
    std::unordered_map<std::string, std::pair<std::string, bool>> reference_bindings;
    std::unordered_map<std::string, std::string> reborrow_parents;
    std::unordered_set<std::string> suspended_references;
    std::unordered_map<std::string, std::string> aggregate_reference_origins;
  };

  void declare_top_level(const SyntaxNode& node, HirModule& module);
  void analyze_function(const SyntaxNode& node, HirModule& module);
  void analyze_trait_implementation(const SyntaxNode& node, HirModule& module);
  void analyze_statement(const SyntaxNode& node, HirFunction& function,
                         const std::string& return_type);
  [[nodiscard]] std::string analyze_expression(const SyntaxNode& node);
  [[nodiscard]] bool type_exists(const std::string& name) const;
  [[nodiscard]] const HirEnum* resolve_enum(const std::string& name);
  [[nodiscard]] const Symbol* lookup(const std::string& name) const;
  [[nodiscard]] bool is_copy_type(const std::string& type_name) const;
  [[nodiscard]] bool is_clone_type(const std::string& type_name) const;
  [[nodiscard]] bool implements_trait(const std::string& type_name, const std::string& trait_name) const;
  [[nodiscard]] bool match_generic_trait_target(
      const GenericTraitImplementation& implementation, const std::string& concrete_type,
      std::unordered_map<std::string, std::string>& substitutions) const;
  [[nodiscard]] bool generic_trait_patterns_overlap(
      const GenericTraitImplementation& left, const GenericTraitImplementation& right) const;
  [[nodiscard]] std::optional<std::string> resolve_associated_type_binding(
      const std::string& type_name, const std::string& trait_name, const std::string& item_name) const;
  [[nodiscard]] std::optional<std::pair<std::string, std::string>> resolve_associated_const_binding(
      const std::string& type_name, const std::string& trait_name, const std::string& item_name) const;
  [[nodiscard]] std::string normalize_associated_type(std::string type_name) const;
  void mark_moved(const std::string& name, SourceRange range);
  void mark_moved_place(const std::string& place, const std::string& type_name, SourceRange range);
  void mark_initialized(const std::string& name);
  [[nodiscard]] bool is_moved_place(const std::string& place) const;
  void begin_borrow(const std::string& place, bool mutable_borrow, const std::string& binding, SourceRange range);
  [[nodiscard]] std::string place_path(const SyntaxNode& node) const;
  [[nodiscard]] static std::string root_name(const std::string& place);
  [[nodiscard]] static bool places_overlap(const std::string& left, const std::string& right);
  [[nodiscard]] bool has_active_borrow(const std::string& place) const;
  void release_borrow(const std::string& binding);
  [[nodiscard]] bool begin_reborrow(const SyntaxNode& operand, bool mutable_borrow,
                                    const std::string& binding, SourceRange range);
  [[nodiscard]] std::string reference_origin(const SyntaxNode& node) const;
  [[nodiscard]] std::string aggregate_local_reference_origin(const SyntaxNode& node) const;
  [[nodiscard]] static bool syntax_uses_name(const SyntaxNode& node, const std::string& name);
  void release_dead_references(const std::vector<SyntaxNode>& statements, std::size_t completed_index);
  bool declare(Symbol symbol);
  void push_scope();
  void pop_scope();
  static std::pair<std::string, std::string> split_typed_name(const std::string& label);
  static std::string function_return_type(const std::string& label);
  void materialize_generic_instantiations(HirModule& module);
  void materialize_generic_functions(HirModule& module);
  void materialize_generic_trait_implementations(HirModule& module);
  void request_instantiation(const std::string& type_name);
  void ensure_tuple_layout(const std::string& type_name, SourceRange range = {});
  void ensure_slice_layout(const std::string& type_name, SourceRange range = {});
  void request_function_instantiation(const std::string& function_name);
  [[nodiscard]] std::pair<std::string, std::string> evaluate_constant(const SyntaxNode& node);
  enum class ComptimeFlow { normal, break_loop, continue_loop };
  void analyze_comptime(const SyntaxNode& node);
  [[nodiscard]] ComptimeFlow execute_comptime_statement(const SyntaxNode& node);
  [[nodiscard]] ComptimeFlow execute_comptime_block(const SyntaxNode& block);
  [[nodiscard]] bool assign_comptime_place(const SyntaxNode& target, const std::pair<std::string, std::string>& value);
  void collect_closure_declarations(const SyntaxNode& node, std::vector<SyntaxNode>& closures);
  [[nodiscard]] static std::string closure_function_name(const SyntaxNode& node);
  [[nodiscard]] bool closure_coerces_to_function(const SyntaxNode& node, const std::string& destination) const;
  [[nodiscard]] bool closure_coerces_to_callable(const SyntaxNode& node, const std::string& destination) const;
  [[nodiscard]] bool callable_identity_compatible(const std::string& actual, const std::string& expected) const;

  DiagnosticEngine& diagnostics_;
  std::unordered_map<std::string, Symbol> globals_;
  std::unordered_map<std::string, std::pair<std::string, std::string>> constants_;
  std::unordered_map<std::string, SyntaxNode> const_functions_;
  std::unordered_map<std::string, std::unordered_set<std::string>> declared_function_attributes_;
  std::unordered_set<std::string> current_function_attributes_;
  std::size_t const_eval_depth_ = 0;
  std::size_t comptime_steps_ = 0;
  std::vector<Scope> scopes_;
  std::unordered_map<std::string, HirType> type_layouts_;
  std::unordered_map<std::string, HirEnum> enum_types_;
  std::unordered_map<std::string, std::vector<std::string>> generic_types_;
  std::unordered_map<std::string, std::vector<std::string>> generic_const_types_;
  std::unordered_map<std::string, std::vector<TraitMethodContract>> trait_contracts_;
  std::unordered_set<std::string> object_safe_traits_;
  std::unordered_map<std::string, std::vector<std::string>> trait_supertraits_;
  std::unordered_map<std::string, std::vector<std::string>> trait_aliases_;
  std::vector<GenericTraitImplementation> generic_trait_implementations_;
  std::unordered_set<std::string> negative_trait_pairs_;
  std::unordered_map<std::string, std::unordered_map<std::string, SyntaxNode>> trait_default_methods_;
  std::unordered_map<std::string, std::unordered_map<std::string, SourceRange>> trait_associated_types_;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> trait_associated_constants_;
  std::unordered_map<std::string, std::unordered_set<std::string>> trait_implementors_;
  std::unordered_set<std::string> implemented_trait_pairs_;
  std::unordered_map<std::string, std::string> trait_method_functions_;
  std::unordered_map<std::string, TraitMethodContract> inherent_method_contracts_;
  std::unordered_set<std::string> inherent_associated_functions_;
  std::unordered_map<std::string, std::string> associated_type_bindings_;
  std::unordered_map<std::string, std::pair<std::string, std::string>> associated_const_bindings_;
  std::unordered_map<std::string, std::vector<std::string>> current_generic_bounds_;
  std::unordered_set<std::string> requested_instantiations_;
  std::unordered_set<std::string> structural_type_names_;
  std::unordered_set<std::string> requested_function_instantiations_;
  std::unordered_set<std::string> explicit_copy_types_;
  std::unordered_set<std::string> explicit_drop_types_;
  std::unordered_set<std::string> explicit_clone_types_;
  std::string current_return_type_;
  [[nodiscard]] OwnershipSnapshot ownership_snapshot() const;
  void restore_ownership(const OwnershipSnapshot& snapshot);
  static std::unordered_set<std::string> union_moved_states(
      const std::vector<std::unordered_set<std::string>>& states);

  std::unordered_set<std::string> moved_values_;
  std::unordered_map<std::string, BorrowState> borrows_;
  std::unordered_map<std::string, std::pair<std::string, bool>> reference_bindings_;
  std::unordered_map<std::string, std::string> reborrow_parents_;
  std::unordered_set<std::string> suspended_references_;
  std::unordered_set<std::string> current_reference_return_origins_;
  std::unordered_map<std::string, std::string> aggregate_reference_origins_;
  std::unordered_set<std::string> used_reference_bindings_;
  std::vector<std::vector<std::string>> declared_names_;
  std::unordered_set<std::string> closure_function_names_;
  std::unordered_map<std::string, std::vector<std::string>> closure_captures_;
  std::unordered_set<std::string> move_closure_functions_;
  std::unordered_set<std::string> shared_closure_functions_;
  std::unordered_set<std::string> mutable_closure_functions_;
  bool analyzing_closure_ = false;
  std::size_t unsafe_depth_ = 0;
  std::size_t async_depth_ = 0;
};

}  // namespace raz::compiler
