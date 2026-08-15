// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "minilang/source.hpp"

namespace minilang {

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct IntegerExpr { std::int64_t value{}; };
struct NameExpr { std::string name; };
struct UnaryExpr { std::string op; ExprPtr operand; };
struct BinaryExpr { std::string op; ExprPtr left; ExprPtr right; };
struct CallExpr { std::string callee; std::vector<ExprPtr> arguments; };

struct Expr {
    SourceSpan span;
    std::variant<IntegerExpr, NameExpr, UnaryExpr, BinaryExpr, CallExpr> value;
};

struct Statement;
using StatementPtr = std::unique_ptr<Statement>;

struct LetStmt { std::string name; ExprPtr initializer; };
struct ReturnStmt { ExprPtr value; };
struct IfStmt {
    ExprPtr condition;
    std::vector<StatementPtr> then_body;
    std::vector<StatementPtr> else_body;
};

struct Statement {
    SourceSpan span;
    std::variant<LetStmt, ReturnStmt, IfStmt> value;
};

struct Function {
    std::string name;
    std::vector<std::string> parameters;
    std::vector<StatementPtr> body;
    SourceSpan span;
};

struct Program {
    std::vector<Function> functions;
};

} // namespace minilang
