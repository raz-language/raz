// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "minilang/codegen.hpp"
#include <forge/ir/builder.hpp>
#include <forge/ir/opcode.hpp>
#include <forge/ir/verifier.hpp>
#include <unordered_map>
#include <unordered_set>

namespace minilang {
namespace {

class Lowerer {
public:
    explicit Lowerer(LowerResult& result) : result_(result) {
        result_.module = &result_.context.create_module("minilang");
        builder_ = std::make_unique<forge::ir::IRBuilder>(result_.context, *result_.module);
        builder_->set_module_metadata("frontend.language", "MiniLang");
        builder_->set_module_metadata("frontend.example", "true");
    }

    void lower(const Program& program) {
        for (const auto& function : program.functions) {
            if (!functions_.insert(function.name).second) {
                diagnose(function.span, "duplicate function '" + function.name + "'");
                continue;
            }
            function_arity_[function.name] = function.parameters.size();
            std::vector<forge::ir::ValueDecl> parameters;
            for (const auto& parameter : function.parameters) {
                parameters.push_back({"%" + parameter, forge::ir::i64_type()});
            }
            const auto handle = builder_->create_function_handle(
                function.name, forge::ir::i64_type(), std::move(parameters));
            (void)handle;
        }
        for (const auto& function : program.functions) lower_function(function);
        for (const auto& diagnostic : forge::ir::verify_module(*result_.module)) {
            result_.diagnostics.push_back(diagnostic.message);
        }
    }

private:
    void source(const SourceSpan& span) {
        builder_->set_source_range(span.file, span.line, span.column, span.end_line, span.end_column);
    }
    void diagnose(const SourceSpan& span, const std::string& message) {
        result_.diagnostics.push_back(span.file + ":" + std::to_string(span.line) + ":" +
                                      std::to_string(span.column) + ": " + message);
    }

    void lower_function(const Function& function) {
        const auto handle = builder_->find_function(function.name);
        auto entry = builder_->create_block_handle(handle, "entry");
        builder_->position_at_end(entry);
        current_block_ = entry;
        locals_.clear();
        for (const auto& parameter : function.parameters) locals_[parameter] = "%" + parameter;
        terminated_ = false;
        for (const auto& statement : function.body) {
            if (terminated_) {
                diagnose(statement->span, "unreachable statement after terminator");
                break;
            }
            lower_statement(*statement, handle);
        }
        if (!terminated_) {
            diagnose(function.span, "function '" + function.name + "' does not return on every path");
            source(function.span);
            builder_->create_unreachable();
            terminated_ = true;
        }
    }

    void lower_statement(const Statement& statement, forge::ir::FunctionHandle function) {
        source(statement.span);
        if (const auto* let = std::get_if<LetStmt>(&statement.value)) {
            if (locals_.contains(let->name)) {
                diagnose(statement.span, "duplicate local '" + let->name + "'");
                return;
            }
            const auto value = lower_expression(*let->initializer);
            if (!value.empty()) locals_[let->name] = value;
            return;
        }
        if (const auto* ret = std::get_if<ReturnStmt>(&statement.value)) {
            const auto value = lower_expression(*ret->value);
            if (!value.empty()) {
                source(statement.span);
                builder_->create_return(value);
                terminated_ = true;
            }
            return;
        }
        const auto& conditional = std::get<IfStmt>(statement.value);
        const auto condition = lower_condition(*conditional.condition);
        if (condition.empty()) return;

        const auto block_id = next_block_++;
        const auto parent_block = current_block_;
        const auto then_block = builder_->create_block_handle(function, "if.then." + std::to_string(block_id));
        const auto else_block = builder_->create_block_handle(function, "if.else." + std::to_string(block_id));
        builder_->position_at_end(parent_block);
        current_block_ = parent_block;
        source(statement.span);
        builder_->create_branch(condition,
                                builder_->resolve(then_block).name,
                                builder_->resolve(else_block).name);

        const auto incoming = locals_;
        builder_->position_at_end(then_block);
        current_block_ = then_block;
        locals_ = incoming;
        terminated_ = false;
        for (const auto& nested : conditional.then_body) {
            if (terminated_) break;
            lower_statement(*nested, function);
        }
        const bool then_returns = terminated_;
        if (!then_returns) {
            diagnose(statement.span, "this example requires each if branch to terminate with return");
            builder_->create_unreachable();
        }

        builder_->position_at_end(else_block);
        current_block_ = else_block;
        locals_ = incoming;
        terminated_ = false;
        for (const auto& nested : conditional.else_body) {
            if (terminated_) break;
            lower_statement(*nested, function);
        }
        const bool else_returns = terminated_;
        if (!else_returns) {
            diagnose(statement.span, "this example requires an else branch ending in return");
            builder_->create_unreachable();
        }
        terminated_ = then_returns && else_returns;
    }

    std::string lower_condition(const Expr& expression) {
        const auto value = lower_expression(expression);
        if (value.empty()) return {};
        if (expression_is_comparison(expression)) return value;
        source(expression.span);
        const auto zero = builder_->create_constant(forge::ir::i64_type(), "0");
        return builder_->create_compare(forge::ir::Opcode::compare_not_equal,
                                        forge::ir::i64_type(), value, zero);
    }

    static bool expression_is_comparison(const Expr& expression) {
        const auto* binary = std::get_if<BinaryExpr>(&expression.value);
        if (!binary) return false;
        return binary->op == "==" || binary->op == "!=" || binary->op == "<" ||
               binary->op == "<=" || binary->op == ">" || binary->op == ">=";
    }

    std::string lower_expression(const Expr& expression) {
        if (const auto* integer = std::get_if<IntegerExpr>(&expression.value)) {
            annotate(expression);
            return builder_->create_constant(forge::ir::i64_type(), std::to_string(integer->value));
        }
        if (const auto* name = std::get_if<NameExpr>(&expression.value)) {
            const auto it = locals_.find(name->name);
            if (it == locals_.end()) {
                diagnose(expression.span, "unknown name '" + name->name + "'");
                return {};
            }
            return it->second;
        }
        if (const auto* unary = std::get_if<UnaryExpr>(&expression.value)) {
            const auto operand = lower_expression(*unary->operand);
            if (operand.empty()) return {};
            const auto zero = builder_->create_constant(forge::ir::i64_type(), "0");
            annotate(expression);
            return builder_->create_binary(forge::ir::Opcode::subtract, forge::ir::i64_type(), zero, operand);
        }
        if (const auto* call = std::get_if<CallExpr>(&expression.value)) {
            if (!functions_.contains(call->callee)) {
                diagnose(expression.span, "unknown function '" + call->callee + "'");
                return {};
            }
            const auto arity = function_arity_.find(call->callee);
            if (arity != function_arity_.end() && arity->second != call->arguments.size()) {
                diagnose(expression.span, "function '" + call->callee + "' expects " +
                         std::to_string(arity->second) + " arguments but received " +
                         std::to_string(call->arguments.size()));
                return {};
            }
            std::vector<std::string> arguments;
            for (const auto& argument : call->arguments) {
                auto value = lower_expression(*argument);
                if (value.empty()) return {};
                arguments.push_back(std::move(value));
            }
            annotate(expression);
            return builder_->create_call(forge::ir::i64_type(), call->callee, std::move(arguments));
        }

        const auto& binary = std::get<BinaryExpr>(expression.value);
        auto left = lower_expression(*binary.left);
        auto right = lower_expression(*binary.right);
        if (left.empty() || right.empty()) return {};
        annotate(expression);
        if (binary.op == "+") return builder_->create_binary(forge::ir::Opcode::add, forge::ir::i64_type(), left, right);
        if (binary.op == "-") return builder_->create_binary(forge::ir::Opcode::subtract, forge::ir::i64_type(), left, right);
        if (binary.op == "*") return builder_->create_binary(forge::ir::Opcode::multiply, forge::ir::i64_type(), left, right);
        if (binary.op == "/") return builder_->create_binary(forge::ir::Opcode::divide_signed, forge::ir::i64_type(), left, right);
        if (binary.op == "==") return builder_->create_compare(forge::ir::Opcode::compare_equal, forge::ir::i64_type(), left, right);
        if (binary.op == "!=") return builder_->create_compare(forge::ir::Opcode::compare_not_equal, forge::ir::i64_type(), left, right);
        if (binary.op == "<") return builder_->create_compare(forge::ir::Opcode::compare_less_signed, forge::ir::i64_type(), left, right);
        if (binary.op == "<=") return builder_->create_compare(forge::ir::Opcode::compare_less_equal_signed, forge::ir::i64_type(), left, right);
        if (binary.op == ">") return builder_->create_compare(forge::ir::Opcode::compare_less_signed, forge::ir::i64_type(), right, left);
        if (binary.op == ">=") return builder_->create_compare(forge::ir::Opcode::compare_less_equal_signed, forge::ir::i64_type(), right, left);
        diagnose(expression.span, "unsupported operator '" + binary.op + "'");
        return {};
    }


    void annotate(const Expr& expression) {
        source(expression.span);
        builder_->set_next_attribute("frontend.ast.kind", expression_kind(expression));
    }

    static std::string expression_kind(const Expr& expression) {
        if (std::holds_alternative<IntegerExpr>(expression.value)) return "integer";
        if (std::holds_alternative<NameExpr>(expression.value)) return "name";
        if (std::holds_alternative<UnaryExpr>(expression.value)) return "unary";
        if (std::holds_alternative<BinaryExpr>(expression.value)) return "binary";
        return "call";
    }

    LowerResult& result_;
    std::unique_ptr<forge::ir::IRBuilder> builder_;
    std::unordered_set<std::string> functions_;
    std::unordered_map<std::string, std::size_t> function_arity_;
    std::unordered_map<std::string, std::string> locals_;
    std::uint64_t next_block_{};
    forge::ir::BlockHandle current_block_{};
    bool terminated_{};
};

} // namespace

LowerResult lower_to_forge(const Program& program) {
    LowerResult result;
    Lowerer lowerer(result);
    lowerer.lower(program);
    return result;
}

} // namespace minilang
