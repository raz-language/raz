// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/lexer/lexer.hpp"

#include "compiler/diagnostics/diagnostic_engine.hpp"
#include "compiler/source/source_manager.hpp"

#include <cctype>
#include <string>

namespace raz::compiler {
namespace {

bool is_identifier_start(unsigned char c) {
  return std::isalpha(c) != 0 || c == '_' || c >= 0x80;
}

bool is_identifier_continue(unsigned char c) {
  return std::isalnum(c) != 0 || c == '_' || c >= 0x80;
}

bool is_digit_for_base(char c, unsigned base) {
  if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0') < base;
  if (c >= 'a' && c <= 'f') return base > 10 && static_cast<unsigned>(c - 'a' + 10) < base;
  if (c >= 'A' && c <= 'F') return base > 10 && static_cast<unsigned>(c - 'A' + 10) < base;
  return false;
}

}  // namespace

Lexer::Lexer(const SourceManager& sources, DiagnosticEngine& diagnostics,
             SourceFileId file)
    : diagnostics_(diagnostics), file_(file), text_(sources.text(file)) {}

bool Lexer::at_end() const noexcept { return offset_ >= text_.size(); }
char Lexer::peek(std::uint32_t distance) const noexcept {
  const auto position = static_cast<std::size_t>(offset_) + distance;
  return position < text_.size() ? text_[position] : '\0';
}

char Lexer::advance() noexcept { return at_end() ? '\0' : text_[offset_++]; }
bool Lexer::consume(char expected) noexcept {
  if (peek() != expected) return false;
  ++offset_;
  return true;
}

Token Lexer::make_token(TokenKind kind, std::uint32_t begin) const noexcept {
  return Token{kind, {{file_, begin}, {file_, offset_}}};
}

void Lexer::report(std::string code, std::uint32_t begin, std::uint32_t end,
                   std::string message) {
  diagnostics_.error(std::move(code), {{file_, begin}, {file_, end}}, std::move(message));
}

void Lexer::skip_line_comment() {
  while (!at_end() && peek() != '\n') advance();
}

void Lexer::skip_block_comment() {
  const auto begin = offset_ - 2;
  unsigned depth = 1;
  while (!at_end() && depth != 0) {
    if (peek() == '/' && peek(1) == '*') {
      advance(); advance(); ++depth;
    } else if (peek() == '*' && peek(1) == '/') {
      advance(); advance(); --depth;
    } else {
      advance();
    }
  }

  if (depth != 0) {
    report("D0001", begin, offset_, "unterminated block comment");
  }
}

void Lexer::skip_trivia() {
  for (;;) {
    while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') advance();
    if (peek() == '/' && peek(1) == '/') {
      advance(); advance(); skip_line_comment(); continue;
    }
    if (peek() == '/' && peek(1) == '*') {
      advance(); advance(); skip_block_comment(); continue;
    }
    break;
  }
}

Token Lexer::lex_identifier_or_keyword() {
  const auto begin = offset_;
  advance();
  while (is_identifier_continue(static_cast<unsigned char>(peek()))) advance();
  const auto spelling = text_.substr(begin, offset_ - begin);
  return make_token(keyword_kind(spelling), begin);
}

Token Lexer::lex_number() {
  const auto begin = offset_;
  unsigned base = 10;
  if (peek() == '0') {
    if (peek(1) == 'x' || peek(1) == 'X') { base = 16; advance(); advance(); }
    else if (peek(1) == 'b' || peek(1) == 'B') { base = 2; advance(); advance(); }
    else if (peek(1) == 'o' || peek(1) == 'O') { base = 8; advance(); advance(); }
  }
  bool saw_digit = false;
  while (is_digit_for_base(peek(), base) || peek() == '_') {
    saw_digit = saw_digit || peek() != '_';
    advance();
  }

  if (!saw_digit && base != 10) {
    report("D0002", begin, offset_, "numeric base prefix requires at least one digit");
  }

  bool floating = false;
  if (base == 10 && peek() == '.' && peek(1) != '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
    floating = true; advance();
    while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') advance();
  }

  if (base == 10 && (peek() == 'e' || peek() == 'E')) {
    floating = true; advance();
    if (peek() == '+' || peek() == '-') advance();
    const auto exponent_begin = offset_;
    while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') advance();
    if (offset_ == exponent_begin) {
      report("D0003", begin, offset_, "floating-point exponent requires digits");
    }
  }

  while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
  return make_token(floating ? TokenKind::float_literal : TokenKind::integer_literal, begin);
}

Token Lexer::lex_string() {
  const auto begin = offset_;
  advance();
  bool terminated = false;
  while (!at_end()) {
    const char c = advance();
    if (c == '"') { terminated = true; break; }
    if (c == '\\' && !at_end()) { advance(); continue; }
    if (c == '\n') break;
  }

  if (!terminated) report("D0004", begin, offset_, "unterminated string literal");
  return make_token(TokenKind::string_literal, begin);
}

Token Lexer::lex_character() {
  const auto begin = offset_;
  advance();
  if (at_end() || peek() == '\n') {
    report("D0005", begin, offset_, "unterminated character literal");
    return make_token(TokenKind::character_literal, begin);
  }

  if (peek() == '\\') { advance(); if (!at_end()) advance(); }
  else advance();
  if (!consume('\'')) {
    while (!at_end() && peek() != '\n' && peek() != '\'') advance();
    (void)consume('\'');
    report("D0006", begin, offset_, "character literal must contain exactly one character");
  }

  return make_token(TokenKind::character_literal, begin);
}

Token Lexer::lex_lifetime() {
  const auto begin = offset_;
  advance();
  if (at_end() || !is_identifier_start(static_cast<unsigned char>(peek()))) {
    report("D0008", begin, offset_, "expected lifetime name after apostrophe");
    return make_token(TokenKind::invalid, begin);
  }

  advance();
  while (!at_end() && is_identifier_continue(static_cast<unsigned char>(peek()))) advance();
  return make_token(TokenKind::lifetime_identifier, begin);
}

Token Lexer::next() {
  skip_trivia();
  const auto begin = offset_;
  if (at_end()) return make_token(TokenKind::end_of_file, begin);
  const auto c = static_cast<unsigned char>(peek());
  if (is_identifier_start(c)) return lex_identifier_or_keyword();
  if (std::isdigit(c)) return lex_number();
  if (peek() == '"') return lex_string();
  if (peek() == '\'') {
    const bool simple_character = offset_ + 2 < text_.size() && text_[offset_ + 2] == '\'';
    const bool escaped_character = offset_ + 3 < text_.size() && text_[offset_ + 1] == '\\' && text_[offset_ + 3] == '\'';
    if (simple_character || escaped_character) return lex_character();
    return lex_lifetime();
  }

#define ONE(ch, kind) case ch: advance(); return make_token(TokenKind::kind, begin)
  switch (peek()) {
    ONE('(', left_paren); ONE(')', right_paren); ONE('{', left_brace); ONE('}', right_brace);
    ONE('[', left_bracket); ONE(']', right_bracket); ONE(',', comma); ONE(';', semicolon);
    ONE('?', question); ONE('@', at); ONE('#', hash); ONE('~', tilde);
    case ':': advance(); return make_token(consume(':') ? TokenKind::colon_colon : TokenKind::colon, begin);
    case '.':
      advance();
      if (consume('.')) {
        if (consume('.')) return make_token(TokenKind::ellipsis, begin);
        if (consume('=')) return make_token(TokenKind::dot_dot_equal, begin);
        return make_token(TokenKind::dot_dot, begin);
      }
      return make_token(TokenKind::dot, begin);
    case '+': advance(); if (consume('=')) return make_token(TokenKind::plus_equal, begin); if (consume('+')) return make_token(TokenKind::plus_plus, begin); return make_token(TokenKind::plus, begin);
    case '-': advance(); if (consume('>')) return make_token(TokenKind::arrow, begin); if (consume('=')) return make_token(TokenKind::minus_equal, begin); if (consume('-')) return make_token(TokenKind::minus_minus, begin); return make_token(TokenKind::minus, begin);
    case '*': advance(); return make_token(consume('=') ? TokenKind::star_equal : TokenKind::star, begin);
    case '/': advance(); return make_token(consume('=') ? TokenKind::slash_equal : TokenKind::slash, begin);
    case '%': advance(); return make_token(consume('=') ? TokenKind::percent_equal : TokenKind::percent, begin);
    case '&': advance(); if (consume('&')) return make_token(TokenKind::ampersand_ampersand, begin); return make_token(consume('=') ? TokenKind::ampersand_equal : TokenKind::ampersand, begin);
    case '|': advance(); if (consume('|')) return make_token(TokenKind::pipe_pipe, begin); return make_token(consume('=') ? TokenKind::pipe_equal : TokenKind::pipe, begin);
    case '^': advance(); return make_token(consume('=') ? TokenKind::caret_equal : TokenKind::caret, begin);
    case '!': advance(); return make_token(consume('=') ? TokenKind::bang_equal : TokenKind::bang, begin);
    case '=': advance(); if (consume('>')) return make_token(TokenKind::fat_arrow, begin); return make_token(consume('=') ? TokenKind::equal_equal : TokenKind::equal, begin);
    case '<': advance(); if (consume('<')) return make_token(consume('=') ? TokenKind::less_less_equal : TokenKind::less_less, begin); return make_token(consume('=') ? TokenKind::less_equal : TokenKind::less, begin);
    case '>': advance(); if (consume('>')) return make_token(consume('=') ? TokenKind::greater_greater_equal : TokenKind::greater_greater, begin); return make_token(consume('=') ? TokenKind::greater_equal : TokenKind::greater, begin);
    default: break;
  }
#undef ONE
  advance();
  report("D0007", begin, offset_, "unexpected character in source file");
  return make_token(TokenKind::invalid, begin);
}

std::vector<Token> Lexer::lex_all() {
  std::vector<Token> tokens;
  do { tokens.push_back(next()); } while (!tokens.back().is(TokenKind::end_of_file));
  return tokens;
}

}  // namespace raz::compiler
