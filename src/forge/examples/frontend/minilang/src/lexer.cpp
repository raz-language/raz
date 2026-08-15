// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "minilang/lexer.hpp"
#include <cctype>
#include <charconv>
#include <unordered_map>

namespace minilang {

const char* token_name(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::end: return "end of file";
    case TokenKind::identifier: return "identifier";
    case TokenKind::integer: return "integer";
    case TokenKind::keyword_fn: return "fn";
    case TokenKind::keyword_let: return "let";
    case TokenKind::keyword_if: return "if";
    case TokenKind::keyword_else: return "else";
    case TokenKind::keyword_return: return "return";
    case TokenKind::left_paren: return "(";
    case TokenKind::right_paren: return ")";
    case TokenKind::left_brace: return "{";
    case TokenKind::right_brace: return "}";
    case TokenKind::comma: return ",";
    case TokenKind::semicolon: return ";";
    case TokenKind::equal: return "=";
    case TokenKind::plus: return "+";
    case TokenKind::minus: return "-";
    case TokenKind::star: return "*";
    case TokenKind::slash: return "/";
    case TokenKind::equal_equal: return "==";
    case TokenKind::bang_equal: return "!=";
    case TokenKind::less: return "<";
    case TokenKind::less_equal: return "<=";
    case TokenKind::greater: return ">";
    case TokenKind::greater_equal: return ">=";
    }
    return "token";
}

LexResult lex(std::string_view source, std::string file_name) {
    LexResult result;
    std::size_t index = 0;
    std::uint32_t line = 1;
    std::uint32_t column = 1;

    auto advance = [&]() -> char {
        const char ch = source[index++];
        if (ch == '\n') { ++line; column = 1; }
        else { ++column; }
        return ch;
    };
    auto span = [&](std::uint32_t start_line, std::uint32_t start_column) {
        return SourceSpan{file_name, start_line, start_column, line, column};
    };
    auto push = [&](TokenKind kind, std::string text, std::uint32_t start_line, std::uint32_t start_column) {
        result.tokens.push_back(Token{kind, std::move(text), 0, span(start_line, start_column)});
    };

    const std::unordered_map<std::string, TokenKind> keywords{
        {"fn", TokenKind::keyword_fn}, {"let", TokenKind::keyword_let},
        {"if", TokenKind::keyword_if}, {"else", TokenKind::keyword_else},
        {"return", TokenKind::keyword_return},
    };

    while (index < source.size()) {
        const char ch = source[index];
        if (std::isspace(static_cast<unsigned char>(ch))) { advance(); continue; }
        if (ch == '/' && index + 1 < source.size() && source[index + 1] == '/') {
            while (index < source.size() && source[index] != '\n') advance();
            continue;
        }

        const auto start_line = line;
        const auto start_column = column;
        if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
            std::string text;
            while (index < source.size()) {
                const char current = source[index];
                if (!std::isalnum(static_cast<unsigned char>(current)) && current != '_') break;
                text.push_back(advance());
            }
            const auto it = keywords.find(text);
            push(it == keywords.end() ? TokenKind::identifier : it->second,
                 std::move(text), start_line, start_column);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            std::string text;
            while (index < source.size() && std::isdigit(static_cast<unsigned char>(source[index]))) {
                text.push_back(advance());
            }
            std::int64_t value{};
            const auto conversion = std::from_chars(text.data(), text.data() + text.size(), value);
            if (conversion.ec != std::errc{}) {
                result.diagnostics.push_back(file_name + ":" + std::to_string(start_line) + ":" +
                                             std::to_string(start_column) + ": invalid integer literal");
            }
            auto token = Token{TokenKind::integer, std::move(text), value, span(start_line, start_column)};
            result.tokens.push_back(std::move(token));
            continue;
        }

        auto pair = [&](char expected, TokenKind both, TokenKind one) {
            advance();
            if (index < source.size() && source[index] == expected) {
                advance();
                push(both, std::string{ch, expected}, start_line, start_column);
            } else {
                push(one, std::string(1, ch), start_line, start_column);
            }
        };
        switch (ch) {
        case '(': advance(); push(TokenKind::left_paren, "(", start_line, start_column); break;
        case ')': advance(); push(TokenKind::right_paren, ")", start_line, start_column); break;
        case '{': advance(); push(TokenKind::left_brace, "{", start_line, start_column); break;
        case '}': advance(); push(TokenKind::right_brace, "}", start_line, start_column); break;
        case ',': advance(); push(TokenKind::comma, ",", start_line, start_column); break;
        case ';': advance(); push(TokenKind::semicolon, ";", start_line, start_column); break;
        case '+': advance(); push(TokenKind::plus, "+", start_line, start_column); break;
        case '-': advance(); push(TokenKind::minus, "-", start_line, start_column); break;
        case '*': advance(); push(TokenKind::star, "*", start_line, start_column); break;
        case '/': advance(); push(TokenKind::slash, "/", start_line, start_column); break;
        case '=': pair('=', TokenKind::equal_equal, TokenKind::equal); break;
        case '!':
            advance();
            if (index < source.size() && source[index] == '=') {
                advance(); push(TokenKind::bang_equal, "!=", start_line, start_column);
            } else {
                result.diagnostics.push_back(file_name + ":" + std::to_string(start_line) + ":" +
                                             std::to_string(start_column) + ": expected '=' after '!'");
            }
            break;
        case '<': pair('=', TokenKind::less_equal, TokenKind::less); break;
        case '>': pair('=', TokenKind::greater_equal, TokenKind::greater); break;
        default:
            result.diagnostics.push_back(file_name + ":" + std::to_string(start_line) + ":" +
                                         std::to_string(start_column) + ": unexpected character '" + ch + "'");
            advance();
            break;
        }
    }
    result.tokens.push_back(Token{TokenKind::end, "", 0, {file_name, line, column, line, column}});
    return result;
}

} // namespace minilang
