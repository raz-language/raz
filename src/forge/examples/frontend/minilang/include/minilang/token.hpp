// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstdint>
#include <string>
#include "minilang/source.hpp"

namespace minilang {

enum class TokenKind {
    end,
    identifier,
    integer,
    keyword_fn,
    keyword_let,
    keyword_if,
    keyword_else,
    keyword_return,
    left_paren,
    right_paren,
    left_brace,
    right_brace,
    comma,
    semicolon,
    equal,
    plus,
    minus,
    star,
    slash,
    equal_equal,
    bang_equal,
    less,
    less_equal,
    greater,
    greater_equal,
};

struct Token {
    TokenKind kind{TokenKind::end};
    std::string text;
    std::int64_t integer{};
    SourceSpan span;
};

[[nodiscard]] const char* token_name(TokenKind kind) noexcept;

} // namespace minilang
