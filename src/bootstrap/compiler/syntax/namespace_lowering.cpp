// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/syntax/namespace_lowering.hpp"

#include "compiler/diagnostics/diagnostic_engine.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace raz::compiler {
namespace {

enum class Visibility { package, public_, private_ };
struct SymbolInfo final {
  std::string lowered;
  Visibility visibility = Visibility::package;
  std::string owner_namespace;
};
struct ImportBinding final {
  std::string alias;
  std::string target;
  bool qualified_only = false;
  bool reexport = false;
};
struct NamespaceInfo final {
  std::map<std::string, SymbolInfo> symbols;
  std::vector<ImportBinding> imports;
};
using NamespaceTable = std::map<std::string, NamespaceInfo>;

bool has_modifier(const SyntaxNode& node, std::string_view wanted) {
  std::size_t begin = 0;
  while (begin <= node.modifier.size()) {
    const auto end = node.modifier.find(';', begin);
    if (node.modifier.substr(begin, end == std::string::npos ? std::string::npos : end - begin) == wanted) return true;
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return false;
}

std::string modifier_argument(const SyntaxNode& node, std::string_view name) {
  const std::string prefix = std::string(name) + "(";
  std::size_t begin = 0;
  while (begin <= node.modifier.size()) {
    const auto end = node.modifier.find(';', begin);
    const auto item = node.modifier.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    if (item.starts_with(prefix) && item.ends_with(')'))
      return item.substr(prefix.size(), item.size() - prefix.size() - 1);
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return {};
}

Visibility declaration_visibility(const SyntaxNode& node) {
  if (has_modifier(node, "public")) return Visibility::public_;
  if (has_modifier(node, "private")) return Visibility::private_;
  return Visibility::package;
}

std::string package_identity(std::string_view ns) {
  if (!ns.starts_with("__raz_pkg_")) return {};
  const auto module = ns.find("__mod_");
  const auto entry = ns.find("__entry_");
  auto end = std::min(module == std::string_view::npos ? ns.size() : module,
                      entry == std::string_view::npos ? ns.size() : entry);
  return std::string(ns.substr(0, end));
}

bool symbol_accessible(std::string_view requester, const SymbolInfo& symbol) {
  if (requester == symbol.owner_namespace) return true;
  if (symbol.visibility == Visibility::public_) return true;
  if (symbol.visibility == Visibility::private_) return false;
  return package_identity(requester) == package_identity(symbol.owner_namespace);
}

std::string sanitize(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const unsigned char c : value) out.push_back(std::isalnum(c) || c == '_' ? static_cast<char>(c) : '_');
  return out.empty() ? std::string("root") : out;
}

std::string join_namespace(std::string_view parent, std::string_view child) {
  if (parent.empty()) return std::string(child);
  if (child.empty()) return std::string(parent);
  return std::string(parent) + "::" + std::string(child);
}

std::string declaration_name(const SyntaxNode& node) {
  switch (node.kind) {
    case SyntaxKind::function_declaration:
    case SyntaxKind::struct_declaration:
    case SyntaxKind::enum_declaration:
    case SyntaxKind::trait_declaration: {
      auto end = node.label.find_first_of("< :=");
      if (node.kind == SyntaxKind::function_declaration) {
        const auto arrow = node.label.find(" -> ");
        if (arrow != std::string::npos) end = std::min(end, arrow);
      }
      return node.label.substr(0, end);
    }
    case SyntaxKind::const_declaration: {
      const auto space = node.label.find_last_of(' ');
      return space == std::string::npos ? node.label : node.label.substr(space + 1);
    }
    default: return {};
  }
}

std::string namespace_symbol(std::string_view ns, std::string_view source_name) {
  // Preserve the long-standing standalone/root ABI. Namespace identity is only
  // encoded when a declaration actually belongs to a namespace.
  if (ns.empty()) return std::string(source_name);
  if (source_name == "main" && ns.find("__entry_") != std::string_view::npos) return "main";
  return "__raz_ns_" + sanitize(ns) + "__" + std::string(source_name);
}

std::pair<std::string,std::string> import_binding(const SyntaxNode& node) {
  // Project composition encodes an alias mapping as
  // `import source::path::__raz_pkg_<pkg>__mod_<module>;`. Ordinary imports
  // map to themselves. `as` changes only the source-facing alias.
  std::string alias = modifier_argument(node, "alias");
  std::string target = node.label;
  const auto split = node.label.rfind("::");
  if (split != std::string::npos) {
    const auto tail = node.label.substr(split + 2);
    if (tail.starts_with("__raz_pkg_")) {
      target = tail;
      if (alias.empty()) alias = node.label.substr(0, split);
      return {alias, target};
    }
  }
  if (alias.empty()) alias = node.label;
  return {alias, target};
}

void collect(const SyntaxNode& node, const std::string& current, NamespaceTable& table,
             DiagnosticEngine& diagnostics) {
  if (node.kind == SyntaxKind::namespace_declaration) {
    const auto next = has_modifier(node, "file") ? node.label : join_namespace(current, node.label);
    table[next];
    for (const auto& child : node.children) collect(child, next, table, diagnostics);
    return;
  }
  if (node.kind == SyntaxKind::import_declaration) {
    auto [alias,target] = import_binding(node);
    auto& imports = table[current].imports;
    const bool qualified_only = !modifier_argument(node, "alias").empty();
    const bool reexport = has_modifier(node, "public");
    if (std::none_of(imports.begin(), imports.end(), [&](const auto& item){
          return item.alias == alias && item.target == target && item.qualified_only == qualified_only && item.reexport == reexport;
        }))
      imports.push_back({std::move(alias), std::move(target), qualified_only, reexport});
    return;
  }
  const auto name = declaration_name(node);
  if (!name.empty()) {
    // Extern declarations intentionally retain their ABI names and may be repeated
    // by independently composed modules.
    if (!(node.kind == SyntaxKind::function_declaration && has_modifier(node, "extern"))) {
      auto& symbols = table[current].symbols;
      if (symbols.contains(name)) {
        diagnostics.error("D2210", node.range, "duplicate declaration '" + name + "' in namespace '" + current + "'");
      } else {
        symbols.emplace(name, SymbolInfo{namespace_symbol(current, name), declaration_visibility(node), current});
      }
    }
  }
}

const SymbolInfo* find_direct(const NamespaceTable& table, std::string_view ns, std::string_view name) {
  const auto scope = table.find(std::string(ns));
  if (scope == table.end()) return nullptr;
  const auto symbol = scope->second.symbols.find(std::string(name));
  return symbol == scope->second.symbols.end() ? nullptr : &symbol->second;
}

const SymbolInfo* find_exported(const NamespaceTable& table, std::string_view ns, std::string_view name,
                                std::string_view requester, std::set<std::string>& visited) {
  if (!visited.insert(std::string(ns)).second) return nullptr;
  if (const auto* direct = find_direct(table, ns, name)) {
    if (symbol_accessible(requester, *direct)) return direct;
  }
  const auto scope = table.find(std::string(ns));
  if (scope == table.end()) return nullptr;
  const SymbolInfo* match = nullptr;
  for (const auto& binding : scope->second.imports) {
    if (!binding.reexport || binding.qualified_only) continue;
    if (const auto* found = find_exported(table, binding.target, name, requester, visited)) {
      if (match != nullptr && match->lowered != found->lowered) return nullptr;
      match = found;
    }
  }
  return match;
}

const std::string* resolve(const NamespaceTable& table, std::string_view current, std::string_view source,
                           SourceRange range, DiagnosticEngine& diagnostics) {
  const auto separator = source.rfind("::");
  if (separator != std::string_view::npos) {
    const auto prefix = source.substr(0, separator);
    const auto name = source.substr(separator + 2);
    if (const auto* direct = find_direct(table, prefix, name); direct != nullptr && symbol_accessible(current, *direct))
      return &direct->lowered;
    const auto scope = table.find(std::string(current));
    const SymbolInfo* match = nullptr;
    if (scope != table.end()) {
      for (const auto& binding : scope->second.imports) {
        if (binding.alias != prefix) continue;
        std::set<std::string> visited;
        if (const auto* found = find_exported(table, binding.target, name, current, visited)) {
          if (match != nullptr && match->lowered != found->lowered) {
            diagnostics.error("D2211", range, "ambiguous import alias '" + std::string(prefix) + "'");
            return nullptr;
          }
          match = found;
        }
      }
    }
    return match == nullptr ? nullptr : &match->lowered;
  }
  if (const auto* local = find_direct(table, current, source); local != nullptr) return &local->lowered;
  const auto scope = table.find(std::string(current));
  if (scope == table.end()) return nullptr;
  const SymbolInfo* match = nullptr;
  std::string matched_namespace;
  for (const auto& binding : scope->second.imports) {
    if (binding.qualified_only) continue;
    std::set<std::string> visited;
    if (const auto* found = find_exported(table, binding.target, source, current, visited)) {
      if (match != nullptr && match->lowered != found->lowered) {
        diagnostics.error("D2211", range, "ambiguous imported name '" + std::string(source) + "'");
        return nullptr;
      }
      match = found;
      matched_namespace = binding.target;
    }
  }
  return match == nullptr ? nullptr : &match->lowered;
}

bool identifier_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identifier_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::string rewrite_text(std::string_view text, const std::string& current, const NamespaceTable& table,
                         const std::unordered_set<std::string>& lexical, SourceRange range,
                         DiagnosticEngine& diagnostics) {
  std::string out;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    if (!identifier_start(text[cursor])) { out.push_back(text[cursor++]); continue; }
    const auto begin = cursor;
    while (cursor < text.size() && identifier_char(text[cursor])) ++cursor;
    while (cursor + 2 < text.size() && text[cursor] == ':' && text[cursor+1] == ':' && identifier_start(text[cursor+2])) {
      cursor += 2;
      while (cursor < text.size() && identifier_char(text[cursor])) ++cursor;
    }
    const auto token = std::string(text.substr(begin, cursor - begin));
    if (token.find("::") == std::string::npos && lexical.contains(token)) { out += token; continue; }
    if (const auto* replacement = resolve(table, current, token, range, diagnostics)) out += *replacement;
    else out += token;
  }
  return out;
}

std::pair<std::string,std::string> split_typed_name(std::string_view label) {
  const auto space = label.find_last_of(' ');
  if (space == std::string_view::npos) return {std::string(label), {}};
  return {std::string(label.substr(0, space)), std::string(label.substr(space + 1))};
}

void collect_locals(const SyntaxNode& node, std::unordered_set<std::string>& names) {
  if (node.kind == SyntaxKind::parameter || node.kind == SyntaxKind::variable_declaration) {
    const auto [type,name] = split_typed_name(node.label); (void)type;
    if (!name.empty()) names.insert(name);
  }
  if (node.kind == SyntaxKind::for_statement && !node.label.empty()) names.insert(node.label);
  for (const auto& child : node.children) collect_locals(child, names);
}

bool scoped_member_path(const SyntaxNode& node, std::string& out) {
  if (node.kind == SyntaxKind::name_expression) {
    out = node.label;
    return true;
  }
  if (node.kind != SyntaxKind::member_expression || node.modifier != "scoped" || node.children.size() != 1) return false;
  std::string prefix;
  if (!scoped_member_path(node.children.front(), prefix)) return false;
  out = prefix + "::" + node.label;
  return true;
}

void rewrite_node(SyntaxNode& node, const std::string& current, const NamespaceTable& table,
                  std::unordered_set<std::string>& lexical, DiagnosticEngine& diagnostics);

void rewrite_function(SyntaxNode& node, const std::string& current, const NamespaceTable& table,
                      std::unordered_set<std::string> lexical, DiagnosticEngine& diagnostics,
                      bool top_level) {
  const auto arrow = node.label.find(" -> ");
  auto header = arrow == std::string::npos ? node.label : node.label.substr(0, arrow);
  auto return_type = arrow == std::string::npos ? std::string{} : node.label.substr(arrow + 4);
  if (top_level && !has_modifier(node, "extern")) {
    const auto generic = header.find('<');
    const auto name = header.substr(0, generic);
    if (const auto* replacement = find_direct(table, current, name))
      header = replacement->lowered + (generic == std::string::npos ? std::string{} : header.substr(generic));
  }
  if (!return_type.empty()) return_type = rewrite_text(return_type, current, table, lexical, node.range, diagnostics);
  node.label = header + (return_type.empty() ? std::string{} : " -> " + return_type);
  collect_locals(node, lexical);
  for (auto& child : node.children) rewrite_node(child, current, table, lexical, diagnostics);
}

void rewrite_node(SyntaxNode& node, const std::string& current, const NamespaceTable& table,
                  std::unordered_set<std::string>& lexical, DiagnosticEngine& diagnostics) {
  switch (node.kind) {
    case SyntaxKind::member_expression: {
      if (node.modifier == "scoped") {
        std::string path;
        if (scoped_member_path(node, path)) {
          if (const auto* replacement = resolve(table, current, path, node.range, diagnostics)) {
            node.kind = SyntaxKind::name_expression;
            node.label = *replacement;
            node.modifier.clear();
            node.children.clear();
            return;
          }
        }
      }
      for (auto& child : node.children) rewrite_node(child, current, table, lexical, diagnostics);
      return;
    }
    case SyntaxKind::function_declaration:
      rewrite_function(node, current, table, lexical, diagnostics, false); return;
    case SyntaxKind::parameter:
    case SyntaxKind::field_declaration:
    case SyntaxKind::variable_declaration: {
      auto [type,name] = split_typed_name(node.label);
      type = rewrite_text(type, current, table, lexical, node.range, diagnostics);
      node.label = type + (name.empty() ? std::string{} : " " + name);
      for (auto& child : node.children) rewrite_node(child, current, table, lexical, diagnostics);
      if (node.kind == SyntaxKind::variable_declaration && !name.empty()) lexical.insert(name);
      return;
    }
    case SyntaxKind::struct_expression: {
      // Named struct literals carry their type in the expression label. Apply
      // the same namespace/package resolution as ordinary type/name
      // expressions so `Point { ... }` continues to refer to the renamed
      // declaration after namespace flattening.
      const auto generic = node.label.find('<');
      const auto base = node.label.substr(0, generic);
      const auto suffix = generic == std::string::npos
          ? std::string{}
          : rewrite_text(node.label.substr(generic), current, table, lexical, node.range, diagnostics);
      if (const auto* replacement = resolve(table, current, base, node.range, diagnostics))
        node.label = *replacement + suffix;
      else if (generic != std::string::npos)
        node.label = base + suffix;
      for (auto& child : node.children) rewrite_node(child, current, table, lexical, diagnostics);
      return;
    }
    case SyntaxKind::name_expression: {
      const auto generic = node.label.find('<');
      const auto base = node.label.substr(0, generic);
      const auto suffix = generic == std::string::npos
          ? std::string{}
          : rewrite_text(node.label.substr(generic), current, table, lexical, node.range, diagnostics);
      if (!(base.find("::") == std::string::npos && lexical.contains(base))) {
        if (const auto* replacement = resolve(table, current, base, node.range, diagnostics))
          node.label = *replacement + suffix;
        else if (generic != std::string::npos)
          // Compiler intrinsics such as size_of<T>() and align_of<T>() do not
          // have namespace symbols of their own, but their generic type
          // arguments still participate in module/package name resolution.
          node.label = base + suffix;
      }
      return;
    }
    case SyntaxKind::associated_type_declaration: {
      const auto equal = node.label.find('=');
      if (equal != std::string::npos) {
        node.label = node.label.substr(0, equal + 1) +
            rewrite_text(node.label.substr(equal + 1), current, table, lexical, node.range, diagnostics);
      }
      break;
    }
    case SyntaxKind::associated_const_declaration: {
      const auto colon = node.label.find(':');
      const auto equal = node.label.find('=');
      if (colon != std::string::npos && equal != std::string::npos && colon < equal) {
        node.label = node.label.substr(0, colon + 1) +
            rewrite_text(node.label.substr(colon + 1, equal - colon - 1), current, table, lexical, node.range, diagnostics) +
            node.label.substr(equal);
      }
      break;
    }
    case SyntaxKind::cast_expression:
      node.label = rewrite_text(node.label, current, table, lexical, node.range, diagnostics); break;
    case SyntaxKind::for_statement:
      for (auto& child : node.children) rewrite_node(child, current, table, lexical, diagnostics);
      if (!node.label.empty()) lexical.insert(node.label);
      return;
    default: break;
  }
  for (auto& child : node.children) rewrite_node(child, current, table, lexical, diagnostics);
}

void rewrite_declaration(SyntaxNode& node, const std::string& current, const NamespaceTable& table,
                         DiagnosticEngine& diagnostics) {
  std::unordered_set<std::string> lexical;
  switch (node.kind) {
    case SyntaxKind::function_declaration:
      rewrite_function(node, current, table, lexical, diagnostics, true); return;
    case SyntaxKind::struct_declaration:
    case SyntaxKind::enum_declaration:
    case SyntaxKind::trait_declaration: {
      const auto name = declaration_name(node);
      if (const auto* replacement = find_direct(table, current, name)) node.label.replace(0, name.size(), replacement->lowered);
      // Generic bounds, supertraits and aliases may mention module-level types/traits.
      const auto prefix = node.label.find_first_of("<:=");
      if (prefix != std::string::npos) {
        const auto head = node.label.substr(0, prefix);
        node.label = head + rewrite_text(node.label.substr(prefix), current, table, lexical, node.range, diagnostics);
      }
      for (auto& child : node.children) {
        if (child.kind == SyntaxKind::function_declaration) rewrite_function(child, current, table, lexical, diagnostics, false);
        else if (child.kind == SyntaxKind::enum_variant) {
          const auto open = child.label.find('(');
          if (open != std::string::npos) child.label = child.label.substr(0, open + 1) + rewrite_text(child.label.substr(open + 1), current, table, lexical, child.range, diagnostics);
        } else rewrite_node(child, current, table, lexical, diagnostics);
      }
      return;
    }
    case SyntaxKind::const_declaration: {
      auto [type,name] = split_typed_name(node.label);
      type = rewrite_text(type, current, table, lexical, node.range, diagnostics);
      if (const auto* replacement = find_direct(table, current, name)) name = replacement->lowered;
      node.label = type + " " + name;
      for (auto& child : node.children) rewrite_node(child, current, table, lexical, diagnostics);
      return;
    }
    case SyntaxKind::impl_declaration:
      node.label = rewrite_text(node.label, current, table, lexical, node.range, diagnostics);
      for (auto& child : node.children) {
        if (child.kind == SyntaxKind::function_declaration) rewrite_function(child, current, table, lexical, diagnostics, false);
        else rewrite_node(child, current, table, lexical, diagnostics);
      }
      return;
    default:
      rewrite_node(node, current, table, lexical, diagnostics); return;
  }
}

void flatten(const SyntaxNode& node, const std::string& current, const NamespaceTable& table,
             DiagnosticEngine& diagnostics, std::vector<SyntaxNode>& out,
             std::set<std::pair<std::string,std::string>>& externs) {
  if (node.kind == SyntaxKind::namespace_declaration) {
    const auto next = has_modifier(node, "file") ? node.label : join_namespace(current, node.label);
    for (const auto& child : node.children) flatten(child, next, table, diagnostics, out, externs);
    return;
  }
  if (node.kind == SyntaxKind::import_declaration) return;
  SyntaxNode copy = node;
  rewrite_declaration(copy, current, table, diagnostics);
  if (copy.kind == SyntaxKind::function_declaration && has_modifier(copy, "extern")) {
    const auto key = std::pair{copy.label, copy.modifier};
    if (!externs.insert(key).second) return;
  }
  out.push_back(std::move(copy));
}

} // namespace

SyntaxTree lower_namespaces(const SyntaxTree& tree, DiagnosticEngine& diagnostics) {
  NamespaceTable table;
  table[""];
  for (const auto& child : tree.root().children) collect(child, "", table, diagnostics);
  SyntaxNode root = tree.root();
  root.children.clear();
  std::set<std::pair<std::string,std::string>> externs;
  for (const auto& child : tree.root().children) flatten(child, "", table, diagnostics, root.children, externs);
  return SyntaxTree(std::move(root));
}

} // namespace raz::compiler
