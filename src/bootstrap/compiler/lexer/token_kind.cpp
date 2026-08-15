// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/lexer/token_kind.hpp"

#include <array>
#include <utility>

namespace raz::compiler {

std::string_view token_kind_name(TokenKind kind) noexcept {
#define RAZ_TOKEN_CASE(name) case TokenKind::name: return #name
  switch (kind) {
    RAZ_TOKEN_CASE(invalid); RAZ_TOKEN_CASE(end_of_file); RAZ_TOKEN_CASE(identifier); RAZ_TOKEN_CASE(lifetime_identifier);
    RAZ_TOKEN_CASE(integer_literal); RAZ_TOKEN_CASE(float_literal);
    RAZ_TOKEN_CASE(string_literal); RAZ_TOKEN_CASE(character_literal);
    RAZ_TOKEN_CASE(kw_as); RAZ_TOKEN_CASE(kw_async); RAZ_TOKEN_CASE(kw_await);
    RAZ_TOKEN_CASE(kw_break); RAZ_TOKEN_CASE(kw_case); RAZ_TOKEN_CASE(kw_comptime);
    RAZ_TOKEN_CASE(kw_const); RAZ_TOKEN_CASE(kw_continue); RAZ_TOKEN_CASE(kw_defer); RAZ_TOKEN_CASE(kw_dyn);
    RAZ_TOKEN_CASE(kw_else); RAZ_TOKEN_CASE(kw_enum); RAZ_TOKEN_CASE(kw_extern);
    RAZ_TOKEN_CASE(kw_false); RAZ_TOKEN_CASE(kw_for); RAZ_TOKEN_CASE(kw_fn);
    RAZ_TOKEN_CASE(kw_if); RAZ_TOKEN_CASE(kw_impl); RAZ_TOKEN_CASE(kw_import);
    RAZ_TOKEN_CASE(kw_in); RAZ_TOKEN_CASE(kw_match); RAZ_TOKEN_CASE(kw_move);
    RAZ_TOKEN_CASE(kw_mut); RAZ_TOKEN_CASE(kw_namespace); RAZ_TOKEN_CASE(kw_null);
    RAZ_TOKEN_CASE(kw_private); RAZ_TOKEN_CASE(kw_public); RAZ_TOKEN_CASE(kw_ref); RAZ_TOKEN_CASE(kw_return);
    RAZ_TOKEN_CASE(kw_spawn); RAZ_TOKEN_CASE(kw_static); RAZ_TOKEN_CASE(kw_struct);
    RAZ_TOKEN_CASE(kw_trait); RAZ_TOKEN_CASE(kw_true); RAZ_TOKEN_CASE(kw_type);
    RAZ_TOKEN_CASE(kw_union); RAZ_TOKEN_CASE(kw_unsafe); RAZ_TOKEN_CASE(kw_while);
    RAZ_TOKEN_CASE(left_paren); RAZ_TOKEN_CASE(right_paren); RAZ_TOKEN_CASE(left_brace);
    RAZ_TOKEN_CASE(right_brace); RAZ_TOKEN_CASE(left_bracket); RAZ_TOKEN_CASE(right_bracket);
    RAZ_TOKEN_CASE(comma); RAZ_TOKEN_CASE(dot); RAZ_TOKEN_CASE(semicolon);
    RAZ_TOKEN_CASE(colon); RAZ_TOKEN_CASE(colon_colon); RAZ_TOKEN_CASE(question);
    RAZ_TOKEN_CASE(at); RAZ_TOKEN_CASE(hash); RAZ_TOKEN_CASE(arrow);
    RAZ_TOKEN_CASE(fat_arrow); RAZ_TOKEN_CASE(plus); RAZ_TOKEN_CASE(plus_equal);
    RAZ_TOKEN_CASE(plus_plus); RAZ_TOKEN_CASE(minus); RAZ_TOKEN_CASE(minus_equal);
    RAZ_TOKEN_CASE(minus_minus); RAZ_TOKEN_CASE(star); RAZ_TOKEN_CASE(star_equal);
    RAZ_TOKEN_CASE(slash); RAZ_TOKEN_CASE(slash_equal); RAZ_TOKEN_CASE(percent);
    RAZ_TOKEN_CASE(percent_equal); RAZ_TOKEN_CASE(ampersand); RAZ_TOKEN_CASE(ampersand_equal);
    RAZ_TOKEN_CASE(ampersand_ampersand); RAZ_TOKEN_CASE(pipe); RAZ_TOKEN_CASE(pipe_equal);
    RAZ_TOKEN_CASE(pipe_pipe); RAZ_TOKEN_CASE(caret); RAZ_TOKEN_CASE(caret_equal);
    RAZ_TOKEN_CASE(tilde); RAZ_TOKEN_CASE(bang); RAZ_TOKEN_CASE(bang_equal);
    RAZ_TOKEN_CASE(equal); RAZ_TOKEN_CASE(equal_equal); RAZ_TOKEN_CASE(less);
    RAZ_TOKEN_CASE(less_equal); RAZ_TOKEN_CASE(less_less); RAZ_TOKEN_CASE(less_less_equal);
    RAZ_TOKEN_CASE(greater); RAZ_TOKEN_CASE(greater_equal); RAZ_TOKEN_CASE(greater_greater);
    RAZ_TOKEN_CASE(greater_greater_equal); RAZ_TOKEN_CASE(dot_dot);
    RAZ_TOKEN_CASE(dot_dot_equal); RAZ_TOKEN_CASE(ellipsis);
  }
#undef RAZ_TOKEN_CASE
  return "unknown";
}

TokenKind keyword_kind(std::string_view text) noexcept {
  using Pair = std::pair<std::string_view, TokenKind>;
  static constexpr std::array<Pair, 41> keywords{{
      {"as", TokenKind::kw_as}, {"async", TokenKind::kw_async},
      {"await", TokenKind::kw_await}, {"break", TokenKind::kw_break},
      {"case", TokenKind::kw_case}, {"comptime", TokenKind::kw_comptime},
      {"const", TokenKind::kw_const}, {"continue", TokenKind::kw_continue},
      {"defer", TokenKind::kw_defer}, {"dyn", TokenKind::kw_dyn}, {"else", TokenKind::kw_else},
      {"enum", TokenKind::kw_enum}, {"extern", TokenKind::kw_extern},
      {"false", TokenKind::kw_false}, {"fn", TokenKind::kw_fn},
      {"for", TokenKind::kw_for}, {"if", TokenKind::kw_if},
      {"impl", TokenKind::kw_impl}, {"import", TokenKind::kw_import},
      {"in", TokenKind::kw_in}, {"match", TokenKind::kw_match},
      {"move", TokenKind::kw_move}, {"mut", TokenKind::kw_mut},
      {"namespace", TokenKind::kw_namespace}, {"null", TokenKind::kw_null},
      {"private", TokenKind::kw_private}, {"public", TokenKind::kw_public},
      {"ref", TokenKind::kw_ref},
      {"return", TokenKind::kw_return}, {"spawn", TokenKind::kw_spawn},
      {"static", TokenKind::kw_static}, {"struct", TokenKind::kw_struct},
      {"trait", TokenKind::kw_trait}, {"true", TokenKind::kw_true},
      {"type", TokenKind::kw_type}, {"union", TokenKind::kw_union},
      {"unsafe", TokenKind::kw_unsafe}, {"while", TokenKind::kw_while},
      {"pub", TokenKind::kw_public}, {"priv", TokenKind::kw_private},
      {"function", TokenKind::kw_fn},
  }};
  for (const auto& [spelling, kind] : keywords) {
    if (text == spelling) return kind;
  }
  return TokenKind::identifier;
}

}  // namespace raz::compiler
