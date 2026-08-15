// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/syntax/syntax_tree.hpp"

#include "compiler/source/source_manager.hpp"

#include <iomanip>
#include <ostream>

namespace raz::compiler {
namespace {

void dump_node(std::ostream& stream, const SourceManager& sources,
               const SyntaxNode& node, unsigned depth) {
  stream << std::string(depth * 2, ' ') << syntax_kind_name(node.kind);
  if (!node.label.empty()) stream << " " << std::quoted(node.label);
  if (node.range.valid()) {
    const auto location = sources.line_column(node.range.begin);
    stream << " @" << location.line << ':' << location.column;
  }
  stream << '\n';
  for (const auto& child : node.children) dump_node(stream, sources, child, depth + 1);
}

}  // namespace

SyntaxTree::SyntaxTree(SyntaxNode root) : root_(std::move(root)) {}
const SyntaxNode& SyntaxTree::root() const noexcept { return root_; }
void SyntaxTree::dump(std::ostream& stream, const SourceManager& sources) const {
  dump_node(stream, sources, root_, 0);
}

}  // namespace raz::compiler
