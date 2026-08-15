// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/lexer.hpp"
#include <cctype>

namespace forge::ir {
LexResult lex(std::string_view source) {
    LexResult out;
    std::size_t i = 0;
    auto push = [&](TokenKind kind, std::size_t begin, std::size_t end) {
        out.tokens.push_back({kind, std::string(source.substr(begin, end-begin)), {begin,end}});
    };
    while (i < source.size()) {
        if (std::isspace(static_cast<unsigned char>(source[i]))) { ++i; continue; }
        if (source[i]=='/' && i+1<source.size() && source[i+1]=='/') {
            i += 2; while (i<source.size() && source[i]!='\n') ++i; continue;
        }
        const auto begin=i;
        if (source[i]=='-' && i+1<source.size() && source[i+1]=='>') { i+=2; push(TokenKind::arrow,begin,i); continue; }
        if (source[i]=='"') {
            ++i;
            bool escaped = false;
            while (i < source.size()) {
                if (!escaped && source[i] == '"') { ++i; break; }
                if (!escaped && source[i] == '\\') escaped = true;
                else escaped = false;
                ++i;
            }
            if (i > source.size() || source[i - 1] != '"')
                out.diagnostics.push_back({DiagnosticSeverity::error,"unterminated string literal",{begin,i}});
            push(TokenKind::string, begin, i); continue;
        }
        if (source[i]=='@' || source[i]=='%') {
            const bool value=source[i]=='%'; ++i;
            int generic_depth = 0;
            while (i < source.size()) {
                const unsigned char c = static_cast<unsigned char>(source[i]);
                if (std::isalnum(c) || source[i]=='_' || source[i]=='.') { ++i; continue; }
                // Forge printers retain concrete generic arguments in symbol
                // names (for example @Vector<i64>_push). The lexer must accept
                // that same spelling so project builds can round-trip emitted
                // FIR through parse/verify/codegen. Commas are symbol content
                // only while inside generic brackets; outside they remain IR
                // operand separators. Reference/raw-pointer markers can occur
                // in generic type arguments as well.
                if (source[i]=='<') { ++generic_depth; ++i; continue; }
                if (source[i]=='>' && generic_depth > 0) { --generic_depth; ++i; continue; }
                // Raz type identities can also contain slice/array suffixes
                // (for example @i64[] or @Vector<i64[]>). Brackets are part of
                // the symbol spelling after a sigil, never operand separators.
                if (source[i]=='[' || source[i]==']') { ++i; continue; }
                if (generic_depth > 0 && (source[i]==',' || source[i]=='&' || source[i]=='*')) { ++i; continue; }
                break;
            }
            if (i==begin+1) out.diagnostics.push_back({DiagnosticSeverity::error,"expected name after sigil",{begin,i}});
            push(value?TokenKind::value:TokenKind::symbol,begin,i); continue;
        }
        if (std::isalpha(static_cast<unsigned char>(source[i])) || source[i]=='_') {
            ++i; while (i<source.size() && (std::isalnum(static_cast<unsigned char>(source[i]))||source[i]=='_'||source[i]=='.')) ++i;
            push(TokenKind::identifier,begin,i); continue;
        }
        if (std::isdigit(static_cast<unsigned char>(source[i])) || (source[i]=='-'&&i+1<source.size()&&std::isdigit(static_cast<unsigned char>(source[i+1])))) {
            const bool negative = source[i] == '-';
            if (negative) ++i;
            // Raz's FIR printer can preserve hexadecimal integer spellings,
            // including integer-valued constants later converted to floating
            // point. Keep the literal as one token so FIR is round-trippable.
            if (i + 1 < source.size() && source[i] == '0' && (source[i + 1] == 'x' || source[i + 1] == 'X')) {
                i += 2;
                while (i < source.size() && std::isxdigit(static_cast<unsigned char>(source[i]))) ++i;
                push(TokenKind::integer, begin, i); continue;
            }
            ++i;
            while (i<source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) ++i;
            if (i < source.size() && source[i] == '.') {
                ++i; while (i<source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) ++i;
            }
            if (i < source.size() && (source[i] == 'e' || source[i] == 'E')) {
                ++i; if (i < source.size() && (source[i] == '+' || source[i] == '-')) ++i;
                while (i<source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) ++i;
            }
            push(TokenKind::integer,begin,i); continue;
        }
        if (std::string_view("{}[]():,=").find(source[i]) != std::string_view::npos) { ++i; push(TokenKind::punctuation,begin,i); continue; }
        out.diagnostics.push_back({DiagnosticSeverity::error,"unexpected character",{begin,begin+1}}); ++i;
    }
    out.tokens.push_back({TokenKind::end,"",{source.size(),source.size()}});
    return out;
}
}
