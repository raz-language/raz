// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/source/source_location.hpp"
#include "compiler/syntax/syntax_kind.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace raz::compiler {

class SourceManager;

struct SyntaxNode final {
  SyntaxKind kind = SyntaxKind::error_node;
  SourceRange range{};
  std::string label;
  std::string modifier;
  std::vector<SyntaxNode> children;
};

class SyntaxTree final {
 public:
  explicit SyntaxTree(SyntaxNode root);
  [[nodiscard]] const SyntaxNode& root() const noexcept;
  void dump(std::ostream& stream, const SourceManager& sources) const;

 private:
  SyntaxNode root_;
};

}  // namespace raz::compiler
