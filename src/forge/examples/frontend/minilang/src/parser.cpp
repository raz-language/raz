// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "minilang/parser.hpp"
#include <cstdlib>
#include <sstream>

namespace minilang {
namespace {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    ParseResult run() {
        Program program;
        while (!at(TokenKind::end)) {
            auto function = parse_function();
            if (!function) synchronize();
            else program.functions.push_back(std::move(*function));
        }
        return ParseResult{diagnostics_.empty() ? std::optional<Program>{std::move(program)} : std::nullopt,
                           std::move(diagnostics_)};
    }

private:
    [[nodiscard]] const Token& current() const { return tokens_[position_]; }
    [[nodiscard]] bool at(TokenKind kind) const { return current().kind == kind; }
    bool match(TokenKind kind) { if (!at(kind)) return false; ++position_; return true; }
    const Token* consume(TokenKind kind, const char* message) {
        if (at(kind)) return &tokens_[position_++];
        error(current(), message);
        return nullptr;
    }
    void error(const Token& token, const std::string& message) {
        diagnostics_.push_back(token.span.file + ":" + std::to_string(token.span.line) + ":" +
                               std::to_string(token.span.column) + ": " + message +
                               " (found " + token_name(token.kind) + ")");
    }
    void synchronize() {
        while (!at(TokenKind::end) && !at(TokenKind::keyword_fn)) ++position_;
    }

    std::optional<Function> parse_function() {
        const Token* start = consume(TokenKind::keyword_fn, "expected 'fn'");
        const Token* name = consume(TokenKind::identifier, "expected function name");
        if (!start || !name || !consume(TokenKind::left_paren, "expected '('") ) return std::nullopt;
        std::vector<std::string> parameters;
        if (!at(TokenKind::right_paren)) {
            do {
                const Token* parameter = consume(TokenKind::identifier, "expected parameter name");
                if (!parameter) return std::nullopt;
                parameters.push_back(parameter->text);
            } while (match(TokenKind::comma));
        }
        if (!consume(TokenKind::right_paren, "expected ')' after parameters")) return std::nullopt;
        auto body = parse_block();
        if (!body) return std::nullopt;
        Function function{name->text, std::move(parameters), std::move(*body), start->span};
        return function;
    }

    std::optional<std::vector<StatementPtr>> parse_block() {
        if (!consume(TokenKind::left_brace, "expected '{'")) return std::nullopt;
        std::vector<StatementPtr> body;
        while (!at(TokenKind::right_brace) && !at(TokenKind::end)) {
            auto statement = parse_statement();
            if (!statement) return std::nullopt;
            body.push_back(std::move(statement));
        }
        if (!consume(TokenKind::right_brace, "expected '}'")) return std::nullopt;
        return body;
    }

    StatementPtr parse_statement() {
        if (match(TokenKind::keyword_let)) {
            const Token start = tokens_[position_ - 1];
            const Token* name = consume(TokenKind::identifier, "expected variable name");
            if (!name || !consume(TokenKind::equal, "expected '=' after variable name")) return {};
            auto initializer = parse_expression();
            if (!initializer || !consume(TokenKind::semicolon, "expected ';' after declaration")) return {};
            auto statement = std::make_unique<Statement>();
            statement->span = start.span;
            statement->value = LetStmt{name->text, std::move(initializer)};
            return statement;
        }
        if (match(TokenKind::keyword_return)) {
            const Token start = tokens_[position_ - 1];
            auto value = parse_expression();
            if (!value || !consume(TokenKind::semicolon, "expected ';' after return")) return {};
            auto statement = std::make_unique<Statement>();
            statement->span = start.span;
            statement->value = ReturnStmt{std::move(value)};
            return statement;
        }
        if (match(TokenKind::keyword_if)) {
            const Token start = tokens_[position_ - 1];
            auto condition = parse_expression();
            auto then_body = parse_block();
            if (!condition || !then_body) return {};
            std::vector<StatementPtr> else_body;
            if (match(TokenKind::keyword_else)) {
                auto parsed_else = parse_block();
                if (!parsed_else) return {};
                else_body = std::move(*parsed_else);
            }
            auto statement = std::make_unique<Statement>();
            statement->span = start.span;
            statement->value = IfStmt{std::move(condition), std::move(*then_body), std::move(else_body)};
            return statement;
        }
        error(current(), "expected statement");
        return {};
    }

    ExprPtr parse_expression() { return parse_equality(); }
    ExprPtr parse_equality() {
        auto expression = parse_comparison();
        while (at(TokenKind::equal_equal) || at(TokenKind::bang_equal)) {
            Token op = tokens_[position_++];
            auto right = parse_comparison();
            expression = binary(std::move(expression), std::move(op), std::move(right));
        }
        return expression;
    }
    ExprPtr parse_comparison() {
        auto expression = parse_term();
        while (at(TokenKind::less) || at(TokenKind::less_equal) || at(TokenKind::greater) || at(TokenKind::greater_equal)) {
            Token op = tokens_[position_++];
            auto right = parse_term();
            expression = binary(std::move(expression), std::move(op), std::move(right));
        }
        return expression;
    }
    ExprPtr parse_term() {
        auto expression = parse_factor();
        while (at(TokenKind::plus) || at(TokenKind::minus)) {
            Token op = tokens_[position_++];
            auto right = parse_factor();
            expression = binary(std::move(expression), std::move(op), std::move(right));
        }
        return expression;
    }
    ExprPtr parse_factor() {
        auto expression = parse_unary();
        while (at(TokenKind::star) || at(TokenKind::slash)) {
            Token op = tokens_[position_++];
            auto right = parse_unary();
            expression = binary(std::move(expression), std::move(op), std::move(right));
        }
        return expression;
    }
    ExprPtr parse_unary() {
        if (match(TokenKind::minus)) {
            Token op = tokens_[position_ - 1];
            auto operand = parse_unary();
            if (!operand) return {};
            auto expression = std::make_unique<Expr>();
            expression->span = op.span;
            expression->value = UnaryExpr{op.text, std::move(operand)};
            return expression;
        }
        return parse_primary();
    }
    ExprPtr parse_primary() {
        if (match(TokenKind::integer)) {
            const Token token = tokens_[position_ - 1];
            auto expression = std::make_unique<Expr>();
            expression->span = token.span;
            expression->value = IntegerExpr{token.integer};
            return expression;
        }
        if (match(TokenKind::identifier)) {
            const Token token = tokens_[position_ - 1];
            if (match(TokenKind::left_paren)) {
                std::vector<ExprPtr> arguments;
                if (!at(TokenKind::right_paren)) {
                    do {
                        auto argument = parse_expression();
                        if (!argument) return {};
                        arguments.push_back(std::move(argument));
                    } while (match(TokenKind::comma));
                }
                if (!consume(TokenKind::right_paren, "expected ')' after arguments")) return {};
                auto expression = std::make_unique<Expr>();
                expression->span = token.span;
                expression->value = CallExpr{token.text, std::move(arguments)};
                return expression;
            }
            auto expression = std::make_unique<Expr>();
            expression->span = token.span;
            expression->value = NameExpr{token.text};
            return expression;
        }
        if (match(TokenKind::left_paren)) {
            auto expression = parse_expression();
            if (!consume(TokenKind::right_paren, "expected ')'")) return {};
            return expression;
        }
        error(current(), "expected expression");
        return {};
    }

    ExprPtr binary(ExprPtr left, Token op, ExprPtr right) {
        if (!left || !right) return {};
        auto expression = std::make_unique<Expr>();
        expression->span = op.span;
        expression->value = BinaryExpr{op.text, std::move(left), std::move(right)};
        return expression;
    }

    std::vector<Token> tokens_;
    std::size_t position_{};
    std::vector<std::string> diagnostics_;
};

} // namespace

ParseResult parse(std::vector<Token> tokens) { return Parser(std::move(tokens)).run(); }

} // namespace minilang
