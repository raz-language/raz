// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/parser.hpp"
#include "forge/ir/lexer.hpp"
#include "forge/platform/data_layout.hpp"
#include <charconv>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <unordered_set>
#include <stdexcept>
#include <string>

namespace forge::ir {
namespace {
class Parser {
public:
    Parser(std::vector<Token> tokens, Diagnostics diagnostics) : tokens_(std::move(tokens)), diagnostics_(std::move(diagnostics)) {}
    ParseResult run() {
        Module module;
        try {
            expect("module"); module = Module(strip(expect_kind(TokenKind::symbol,"module name").text)); expect("{");
            while (!at("}") && !ended()) {
                if (at("struct") || (at("moveonly") && peek(1).text == "struct")) module.structs().push_back(parse_struct());
                else if (at("array") || (at("moveonly") && peek(1).text == "array")) module.arrays().push_back(parse_array());
                else if (is_global_start()) module.globals().push_back(parse_global(module));
                else module.functions().push_back(parse_function());
            }
            expect("}");

            // Compatibility Raz FIR may carry a quoted literal as a
            // pointer-sized scalar const. Canonical Forge IR stores literals as
            // private NUL-terminated globals, so intern and rewrite them here.
            std::uint64_t string_index = 0;
            for (auto& function : module.functions()) {
                for (auto& block : function.blocks) {
                    for (auto& operation : block.operations) {
                        if (operation.opcode != "const" || operation.operands.size() != 1U ||
                            operation.operands[0].size() < 2U || operation.operands[0].front() != '"' ||
                            operation.operands[0].back() != '"')
                            continue;
                        if (operation.type != Type(TypeKind::i64) && operation.type != Type(TypeKind::ptr))
                            fail("string constant requires pointer-sized i64 or ptr result");

                        Token literal{TokenKind::string, operation.operands[0], {}};
                        auto bytes = decode_string(literal);
                        bytes.push_back(0);
                        Global global;
                        std::string module_tag = module.name();
                        for (char& ch : module_tag) {
                            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                                  (ch >= '0' && ch <= '9') || ch == '_')) ch = '_';
                        }
                        global.name = "__forge_string_literal_" + module_tag + "_" + std::to_string(string_index++);
                        global.type = Type(TypeKind::i8);
                        global.is_constant = true;
                        global.linkage = SymbolLinkage::internal;
                        global.alignment = 1;
                        global.element_count = static_cast<std::uint32_t>(bytes.size());
                        global.bytes = std::move(bytes);
                        module.globals().push_back(std::move(global));
                        operation.opcode = "global.address";
                        operation.operands = {"@" + module.globals().back().name};
                    }
                }
            }
        } catch (const std::runtime_error&) {}
        ParseResult result; result.diagnostics=std::move(diagnostics_);
        if (result.diagnostics.empty()) result.module=std::move(module);
        return result;
    }
private:
    const Token& peek(std::size_t n=0) const { return tokens_[pos_+n]; }
    bool ended() const { return peek().kind==TokenKind::end; }
    bool at(std::string_view text) const { return peek().text==text; }
    bool take(std::string_view text) { if (at(text)) { ++pos_; return true; } return false; }
    bool is_symbol_prefix(std::string_view text) const {
        return text == "extern" || text == "thread_local" || text == "internal" || text == "weak" || text == "hidden";
    }
    bool is_global_start() const {
        std::size_t lookahead = 0;
        while (is_symbol_prefix(peek(lookahead).text)) ++lookahead;
        return peek(lookahead).text == "global" || peek(lookahead).text == "constant";
    }
    Token expect(std::string_view text) { if (!take(text)) fail("expected '"+std::string(text)+"'"); return tokens_[pos_-1]; }
    Token expect_kind(TokenKind kind, std::string_view what) { if (peek().kind!=kind) fail("expected "+std::string(what)); return tokens_[pos_++]; }
    [[noreturn]] void fail(std::string message) { diagnostics_.push_back({DiagnosticSeverity::error,std::move(message),peek().range}); throw std::runtime_error("parse"); }
    static std::string strip(const std::string& s) { return s.empty()?s:s.substr(1); }
    Type parse_type() { auto t=expect_kind(TokenKind::identifier,"type"); try { return Type::parse(t.text); } catch (...) { fail("unknown type '"+t.text+"'"); } }
    ValueDecl parse_value_decl(std::string_view what) {
        ValueDecl declaration;
        declaration.name = expect_kind(TokenKind::value, what).text;
        expect(":");
        if (take("borrow")) {
            declaration.borrow_mode = take("mut") ? BorrowMode::mutable_ : BorrowMode::immutable;
        } else {
            declaration.owned = take("owned");
        }
        if (take("callback")) {
            declaration.function_signature_name = strip(expect_kind(TokenKind::symbol, "callback signature").text);
            declaration.type = Type(TypeKind::ptr);
            if (declaration.owned || declaration.borrow_mode != BorrowMode::none) fail("callback parameter cannot be owned or borrowed");
        } else if (take("struct")) {
            declaration.aggregate_kind = AggregateRefKind::structure;
            declaration.aggregate_name = strip(expect_kind(TokenKind::symbol, "structure type").text);
            declaration.type = Type(TypeKind::ptr);
        } else if (take("array")) {
            declaration.aggregate_kind = AggregateRefKind::array;
            declaration.aggregate_name = strip(expect_kind(TokenKind::symbol, "array type").text);
            declaration.type = Type(TypeKind::ptr);
        } else {
            declaration.type = parse_type();
            if (declaration.owned) fail("owned parameter requires a named aggregate type");
            if (declaration.borrow_mode != BorrowMode::none) fail("borrowed parameter requires a named aggregate type");
        }
        return declaration;
    }
    std::vector<ValueDecl> parse_parameters() {
        std::vector<ValueDecl> params; expect("(");
        while (!at(")")) {
            params.push_back(parse_value_decl("parameter")); if (!take(",")) break;
        }
        expect(")"); return params;
    }
    std::uint32_t parse_u32_token(std::string_view what) {
        const auto token = expect_kind(TokenKind::integer, what);
        std::uint32_t value{};
        const auto [end, error] = std::from_chars(token.text.data(), token.text.data() + token.text.size(), value);
        if (error != std::errc{} || end != token.text.data() + token.text.size()) fail("invalid " + std::string(what));
        return value;
    }
    std::vector<std::uint8_t> decode_string(const Token& token) {
        std::vector<std::uint8_t> bytes;
        for (std::size_t i = 1; i + 1 < token.text.size(); ++i) {
            unsigned char ch = static_cast<unsigned char>(token.text[i]);
            if (ch != '\\') { bytes.push_back(ch); continue; }
            if (++i + 1 >= token.text.size()) fail("invalid string escape");
            switch (token.text[i]) {
            case '0': bytes.push_back(0); break;
            case 'n': bytes.push_back('\n'); break;
            case 'r': bytes.push_back('\r'); break;
            case 't': bytes.push_back('\t'); break;
            case '\\': bytes.push_back('\\'); break;
            case '"': bytes.push_back('"'); break;
            case 'x': {
                if (i + 2 >= token.text.size() - 1) fail("incomplete hexadecimal string escape");
                auto hex = [](char value) -> int {
                    if (value >= '0' && value <= '9') return value - '0';
                    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
                    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
                    return -1;
                };
                const int high = hex(token.text[i + 1]);
                const int low = hex(token.text[i + 2]);
                if (high < 0 || low < 0) fail("invalid hexadecimal string escape");
                bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
                i += 2;
                break;
            }
            default: fail("unsupported string escape");
            }
        }
        return bytes;
    }
    StructDecl parse_struct() {
        const bool move_only = take("moveonly");
        expect("struct");
        StructDecl declaration;
        declaration.move_only = move_only;
        declaration.name = strip(expect_kind(TokenKind::symbol, "structure name").text);
        expect("{");
        while (!at("}")) {
            StructField field;
            field.name = expect_kind(TokenKind::identifier, "field name").text;
            expect(":");
            if (take("struct")) {
                field.aggregate_kind = AggregateRefKind::structure;
                field.aggregate_name = strip(expect_kind(TokenKind::symbol, "structure reference").text);
                field.type = Type(TypeKind::void_);
            } else if (take("array")) {
                field.aggregate_kind = AggregateRefKind::array;
                field.aggregate_name = strip(expect_kind(TokenKind::symbol, "array reference").text);
                field.type = Type(TypeKind::void_);
            } else {
                field.type = parse_type();
            }
            declaration.fields.push_back(std::move(field));
            if (!take(",")) break;
        }
        expect("}");
        return declaration;
    }
    ArrayDecl parse_array() {
        const bool move_only = take("moveonly");
        expect("array");
        ArrayDecl declaration;
        declaration.move_only = move_only;
        declaration.name = strip(expect_kind(TokenKind::symbol, "array name").text);
        expect("=");
        if (take("struct")) {
            declaration.element_aggregate_kind = AggregateRefKind::structure;
            declaration.element_aggregate_name = strip(expect_kind(TokenKind::symbol, "structure reference").text);
            declaration.element_type = Type(TypeKind::ptr);
        } else if (take("array")) {
            declaration.element_aggregate_kind = AggregateRefKind::array;
            declaration.element_aggregate_name = strip(expect_kind(TokenKind::symbol, "array reference").text);
            declaration.element_type = Type(TypeKind::ptr);
        } else {
            declaration.element_type = parse_type();
        }
        expect("[");
        declaration.element_count = parse_u32_token("array element count");
        expect("]");
        return declaration;
    }
    const StructDecl* find_struct(const Module& module, const std::string& name) const {
        const auto found = std::find_if(module.structs().begin(), module.structs().end(), [&](const StructDecl& item) { return item.name == name; });
        return found == module.structs().end() ? nullptr : &*found;
    }
    const ArrayDecl* find_array(const Module& module, const std::string& name) const {
        const auto found = std::find_if(module.arrays().begin(), module.arrays().end(), [&](const ArrayDecl& item) { return item.name == name; });
        return found == module.arrays().end() ? nullptr : &*found;
    }
    std::uint64_t parse_integer_bits(Type type) {
        const auto token = expect_kind(TokenKind::integer, "integer initializer");
        std::uint32_t bits{};
        switch (type.kind()) {
        case TypeKind::i1: bits = 1; break;
        case TypeKind::i8: bits = 8; break;
        case TypeKind::i16: bits = 16; break;
        case TypeKind::i32: bits = 32; break;
        case TypeKind::i64: bits = 64; break;
        default: fail("aggregate scalar initializer requires an integer type");
        }
        try {
            if (!token.text.empty() && token.text.front() == '-') {
                const auto value = std::stoll(token.text);
                if (bits < 64) {
                    const auto minimum = -(std::int64_t{1} << (bits - 1));
                    if (value < minimum) fail("integer initializer is below the field's signed range");
                }
                return static_cast<std::uint64_t>(value);
            }
            const auto value = std::stoull(token.text);
            if (bits < 64 && value >= (std::uint64_t{1} << bits)) fail("integer initializer exceeds the field width");
            return value;
        } catch (const std::invalid_argument&) { fail("invalid integer initializer"); }
          catch (const std::out_of_range&) { fail("integer initializer is out of range"); }
    }
    void parse_operation_attributes(Operation& operation) {
        while (take("attr")) {
            const auto name = expect_kind(TokenKind::identifier, "operation attribute name").text;
            const auto value_token = expect_kind(TokenKind::string, "operation attribute value");
            const auto value_bytes = decode_string(value_token);
            operation.attributes.push_back({name, std::string(value_bytes.begin(), value_bytes.end())});
        }
    }
    void write_scalar(std::vector<std::uint8_t>& bytes, std::size_t offset, Type type, std::uint64_t value) {
        const auto layout = target::DataLayout::host();
        const auto size = layout.size_of(type);
        if (!size || offset > bytes.size() || *size > bytes.size() - offset) fail("aggregate initializer exceeds its storage");
        for (std::size_t index = 0; index < *size; ++index) {
            const auto shift_index = layout.endianness == target::Endianness::little ? index : (*size - index - 1);
            bytes[offset + index] = static_cast<std::uint8_t>((value >> (shift_index * 8)) & 0xffU);
        }
    }
    std::vector<std::uint8_t> parse_array_initializer(const Module& module, const ArrayDecl& declaration) {
        const auto layout = target::DataLayout::host().array_layout(module, declaration);
        if (!layout) fail("invalid layout for array @" + declaration.name);
        std::vector<std::uint8_t> bytes(layout->size, 0);
        expect("[");
        for (std::uint32_t index = 0; index < declaration.element_count; ++index) {
            if (at("]")) fail("array initializer has too few elements");
            const auto offset = static_cast<std::size_t>(index) * layout->stride;
            if (declaration.element_aggregate_kind == AggregateRefKind::structure) {
                const auto* nested = find_struct(module, declaration.element_aggregate_name);
                if (!nested) fail("unknown nested structure @" + declaration.element_aggregate_name);
                const auto nested_bytes = parse_struct_initializer(module, *nested);
                if (nested_bytes.size() > layout->stride) fail("nested structure initializer exceeds array stride");
                std::copy(nested_bytes.begin(), nested_bytes.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
            } else if (declaration.element_aggregate_kind == AggregateRefKind::array) {
                const auto* nested = find_array(module, declaration.element_aggregate_name);
                if (!nested) fail("unknown nested array @" + declaration.element_aggregate_name);
                const auto nested_bytes = parse_array_initializer(module, *nested);
                if (nested_bytes.size() > layout->stride) fail("nested array initializer exceeds array stride");
                std::copy(nested_bytes.begin(), nested_bytes.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
            } else {
                write_scalar(bytes, offset, declaration.element_type, parse_integer_bits(declaration.element_type));
            }
            if (index + 1 < declaration.element_count) expect(",");
        }
        if (!at("]")) fail("array initializer has too many elements");
        expect("]");
        return bytes;
    }
    std::vector<std::uint8_t> parse_struct_initializer(const Module& module, const StructDecl& declaration) {
        const auto layout = target::DataLayout::host().struct_layout(module, declaration);
        if (!layout) fail("invalid or recursive layout for structure @" + declaration.name);
        std::vector<std::uint8_t> bytes(layout->size, 0);
        std::unordered_set<std::string> initialized;
        expect("{");
        while (!at("}")) {
            const auto field_name = expect_kind(TokenKind::identifier, "structure field name").text;
            if (!initialized.insert(field_name).second) fail("duplicate initializer for field '" + field_name + "'");
            const auto found = std::find_if(declaration.fields.begin(), declaration.fields.end(), [&](const StructField& field) { return field.name == field_name; });
            if (found == declaration.fields.end()) fail("unknown field '" + field_name + "' in structure @" + declaration.name);
            const auto field_index = static_cast<std::size_t>(std::distance(declaration.fields.begin(), found));
            expect(":");
            std::vector<std::uint8_t> field_bytes;
            if (found->aggregate_kind == AggregateRefKind::structure) {
                const auto* nested = find_struct(module, found->aggregate_name);
                if (!nested) fail("unknown nested structure @" + found->aggregate_name);
                field_bytes = parse_struct_initializer(module, *nested);
            } else if (found->aggregate_kind == AggregateRefKind::array) {
                const auto* nested = find_array(module, found->aggregate_name);
                if (!nested) fail("unknown nested array @" + found->aggregate_name);
                field_bytes = parse_array_initializer(module, *nested);
            } else {
                write_scalar(bytes, layout->fields[field_index].offset, found->type, parse_integer_bits(found->type));
            }
            if (!field_bytes.empty()) {
                const auto offset = layout->fields[field_index].offset;
                if (field_bytes.size() != layout->fields[field_index].size) fail("nested aggregate initializer size mismatch");
                std::copy(field_bytes.begin(), field_bytes.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
            }
            if (!take(",")) break;
        }
        expect("}");
        if (initialized.size() != declaration.fields.size()) fail("structure initializer must provide every field exactly once");
        return bytes;
    }
    Global parse_global(const Module& module) {
        Global global;
        bool parsing_prefixes = true;
        while (parsing_prefixes) {
            if (take("extern")) global.is_external = true;
            else if (take("thread_local")) global.is_thread_local = true;
            else if (take("internal")) global.linkage = SymbolLinkage::internal;
            else if (take("weak")) global.linkage = SymbolLinkage::weak;
            else if (take("hidden")) global.visibility = SymbolVisibility::hidden;
            else parsing_prefixes = false;
        }
        global.is_constant = take("constant");
        if (!global.is_constant) expect("global");
        global.name = strip(expect_kind(TokenKind::symbol, "global name").text);
        expect(":");
        if (take("callback")) {
            const auto signature_token = expect_kind(TokenKind::symbol, "callback signature").text;
            const auto bracket = signature_token.find('[');
            if (bracket != std::string::npos) {
                if (signature_token.back() != ']' || bracket + 1 >= signature_token.size() - 1)
                    fail("malformed callback element count");
                global.function_signature_name = strip(signature_token.substr(0, bracket));
                try {
                    global.element_count = static_cast<std::uint32_t>(std::stoul(signature_token.substr(bracket + 1, signature_token.size() - bracket - 2)));
                } catch (...) { fail("invalid callback element count"); }
            } else {
                global.function_signature_name = strip(signature_token);
                if (take("[")) {
                    global.element_count = parse_u32_token("callback element count");
                    expect("]");
                }
            }
            global.type = Type(TypeKind::ptr);
        } else if (take("struct")) {
            global.aggregate_kind = AggregateRefKind::structure;
            global.aggregate_name = strip(expect_kind(TokenKind::symbol, "structure name").text);
            global.type = Type(TypeKind::i8);
        } else if (take("array")) {
            global.aggregate_kind = AggregateRefKind::array;
            global.aggregate_name = strip(expect_kind(TokenKind::symbol, "array name").text);
            global.type = Type(TypeKind::i8);
        } else {
            global.type = parse_type();
            if (take("[")) {
                global.element_count = parse_u32_token("array element count");
                expect("]");
            }
        }
        if (take("align")) global.alignment = parse_u32_token("global alignment");
        if (!global.is_external) {
            expect("=");
            if (take("zero")) global.zero_initialized = true;
            else if (global.aggregate_kind == AggregateRefKind::structure && at("{")) {
                const auto* declaration = find_struct(module, global.aggregate_name);
                if (!declaration) fail("typed initializer references unknown structure @" + global.aggregate_name);
                global.bytes = parse_struct_initializer(module, *declaration);
            } else if (global.aggregate_kind == AggregateRefKind::array && at("[")) {
                const auto* declaration = find_array(module, global.aggregate_name);
                if (!declaration) fail("typed initializer references unknown array @" + global.aggregate_name);
                global.bytes = parse_array_initializer(module, *declaration);
            } else if (peek().kind == TokenKind::string) global.bytes = decode_string(tokens_[pos_++]);
            else global.initializer = expect_kind(TokenKind::integer, "integer initializer").text;
        }
        return global;
    }
    Function parse_function() {
        Function fn;
        bool signature = false;
        bool parsing_prefixes = true;
        while (parsing_prefixes) {
            if (take("extern")) fn.is_external = true;
            else if (take("signature")) { fn.is_signature = true; signature = true; }
            else if (take("variadic")) fn.variadic = true;
            else if (take("internal")) fn.linkage = SymbolLinkage::internal;
            else if (take("weak")) fn.linkage = SymbolLinkage::weak;
            else if (take("hidden")) fn.visibility = SymbolVisibility::hidden;
            else if (take("c")) fn.calling_convention = CallingConvention::c;
            else if (take("systemv")) fn.calling_convention = CallingConvention::system_v;
            else if (take("win64")) fn.calling_convention = CallingConvention::windows_x64;
            else if (take("fast")) fn.calling_convention = CallingConvention::fast;
            else if (take("sse2")) fn.target_feature = "sse2";
            else if (take("avx2")) fn.target_feature = "avx2";
            else if (take("neon")) fn.target_feature = "neon";
            else parsing_prefixes = false;
        }
        if (!signature) expect("func");
        fn.name = strip(expect_kind(TokenKind::symbol, signature ? "signature name" : "function name").text);
        fn.parameters = parse_parameters();
        expect("->");
        if (take("borrow")) fn.return_borrow_mode = take("mut") ? BorrowMode::mutable_ : BorrowMode::immutable;
        else fn.return_owned = take("owned");
        if (take("struct")) {
            fn.return_aggregate_kind = AggregateRefKind::structure;
            fn.return_aggregate_name = strip(expect_kind(TokenKind::symbol, "structure return type").text);
            fn.return_type = Type(TypeKind::ptr);
        } else if (take("array")) {
            fn.return_aggregate_kind = AggregateRefKind::array;
            fn.return_aggregate_name = strip(expect_kind(TokenKind::symbol, "array return type").text);
            fn.return_type = Type(TypeKind::ptr);
        } else {
            if (fn.return_owned) fail("owned return requires struct or array type");
            if (fn.return_borrow_mode != BorrowMode::none) fail("borrowed return requires struct or array type");
            fn.return_type = parse_type();
        }
        if (fn.return_borrow_mode != BorrowMode::none) {
            expect("from");
            fn.return_borrow_parameter = static_cast<std::int32_t>(parse_u32_token("borrowed return parameter index"));
        }
        if (fn.is_external || signature) return fn;
        expect("{");
        while (!at("}") && !ended()) fn.blocks.push_back(parse_block());
        expect("}");
        return fn;
    }
    Block parse_block() {
        Block b; b.name=expect_kind(TokenKind::identifier,"block name").text;
        if (at("(")) b.parameters = parse_parameters();
        expect(":");
        while (!at("}") && !(peek().kind==TokenKind::identifier && (peek(1).text==":" || peek(1).text=="("))) b.operations.push_back(parse_operation());
        return b;
    }
    std::vector<std::string> parse_args() {
        std::vector<std::string> args; expect("("); while (!at(")")) { args.push_back(expect_kind(TokenKind::value,"SSA value").text); if (!take(",")) break; } expect(")"); return args;
    }
    Operation parse_operation() {
        Operation op;
        if (peek().kind==TokenKind::value && peek(1).text=="=") { op.result=peek().text; pos_+=2; }
        op.opcode=expect_kind(TokenKind::identifier,"opcode").text;
        if (op.opcode=="return") { if (peek().kind==TokenKind::value) op.operands.push_back(tokens_[pos_++].text); parse_operation_attributes(op); return op; }
        if (op.opcode=="unreachable") { parse_operation_attributes(op); return op; }
        if (op.opcode=="jump") { op.successors.push_back(expect_kind(TokenKind::identifier,"destination block").text); op.successor_arguments.push_back(at("(")?parse_args():std::vector<std::string>{}); parse_operation_attributes(op); return op; }
        if (op.opcode=="branch") {
            op.operands.push_back(expect_kind(TokenKind::value,"condition").text); expect(",");
            for (int i=0;i<2;++i) { op.successors.push_back(expect_kind(TokenKind::identifier,"destination block").text); op.successor_arguments.push_back(at("(")?parse_args():std::vector<std::string>{}); if (i==0) expect(","); }
            parse_operation_attributes(op);
            return op;
        }
        op.type=parse_type();
        if (op.opcode == "call" || op.opcode == "call.indirect") {
            if (op.opcode == "call") op.operands.push_back(expect_kind(TokenKind::symbol, "callee").text);
            else {
                op.operands.push_back(expect_kind(TokenKind::value, "function pointer").text);
                if (take("as")) op.operands.push_back(expect_kind(TokenKind::symbol, "indirect call signature").text);
            }
            expect("(");
            while (!at(")")) {
                op.operands.push_back(expect_kind(TokenKind::value, "call argument").text);
                if (!take(",")) break;
            }
            expect(")");
            parse_operation_attributes(op);
            return op;
        }
        std::size_t arity = 0;
        if (op.opcode == "func.address" || op.opcode == "callback.address") {
            op.operands.push_back(expect_kind(TokenKind::symbol, "function address target").text);
            if (take("as")) op.operands.push_back(expect_kind(TokenKind::symbol, "function address signature").text);
            parse_operation_attributes(op);
            return op;
        }
        if (op.opcode == "const" || op.opcode == "load" || op.opcode == "neg" || op.opcode == "not" || op.opcode == "copy" || op.opcode == "stack.alloc" ||
            op.opcode == "truncate" || op.opcode == "zero_extend" || op.opcode == "sign_extend" || op.opcode == "bitcast" ||
            op.opcode == "int_to_float.signed" || op.opcode == "int_to_float.unsigned" ||
            op.opcode == "float_to_int.signed" || op.opcode == "float_to_int.unsigned" ||
            op.opcode == "float_extend" || op.opcode == "float_truncate" || op.opcode == "global.address" || op.opcode == "tls.address" ||
            op.opcode == "sizeof.struct" || op.opcode == "alignof.struct" ||
            op.opcode == "sizeof.array" || op.opcode == "alignof.array" ||
            op.opcode == "stack.alloc.struct" || op.opcode == "stack.alloc.array") arity = 1;
        else if (op.opcode == "add" || op.opcode == "sub" || op.opcode == "mul" || op.opcode == "div" ||
                 op.opcode == "div.signed" || op.opcode == "div.unsigned" ||
                 op.opcode == "rem.signed" || op.opcode == "rem.unsigned" ||
                 op.opcode == "and" || op.opcode == "or" || op.opcode == "xor" ||
                 op.opcode == "shl" || op.opcode == "shr.signed" || op.opcode == "shr.unsigned" ||
                 (op.opcode == "ptr.offset" || op.opcode == "field.address") || op.opcode.starts_with("cmp.")) arity = 2;
        else if (op.opcode == "select" || op.opcode == "struct.field.address" || op.opcode == "struct.field.name.address" || op.opcode == "array.element.address" || op.opcode == "callback.element.address" || op.opcode == "memory.copy" || op.opcode == "memory.set") arity = 3;
        else if (op.opcode == "store" || op.opcode == "aggregate.zero.struct" || op.opcode == "aggregate.zero.array" || op.opcode == "aggregate.move.struct" || op.opcode == "aggregate.move.array" || op.opcode == "aggregate.borrow.struct" || op.opcode == "aggregate.borrow.array" || op.opcode == "aggregate.borrow.mut.struct" || op.opcode == "aggregate.borrow.mut.array" || op.opcode == "aggregate.attach.struct" || op.opcode == "aggregate.attach.array" || op.opcode == "aggregate.borrow.end.struct" || op.opcode == "aggregate.borrow.end.array" || op.opcode == "aggregate.end.struct" || op.opcode == "aggregate.end.array") arity = 2;
        else if (op.opcode == "aggregate.copy.struct" || op.opcode == "aggregate.copy.array") arity = 3;
        else fail("unknown opcode '" + op.opcode + "'");
        for (std::size_t i = 0; i < arity; ++i) {
            const bool string_constant = op.opcode == "const" && peek().kind == TokenKind::string;
            if (peek().kind != TokenKind::value && peek().kind != TokenKind::integer && peek().kind != TokenKind::symbol &&
                peek().kind != TokenKind::identifier && !string_constant)
                fail("expected operand for '" + op.opcode + "'");
            op.operands.push_back(tokens_[pos_++].text);
            if (i + 1 < arity) take(",");
        }
        if (take("align")) {
            const auto text = expect_kind(TokenKind::integer, "alignment").text;
            try {
                const auto value = std::stoull(text);
                if (value > 0xffffffffULL) fail("alignment is too large");
                else op.alignment = static_cast<std::uint32_t>(value);
            } catch (...) { fail("invalid alignment"); }
        }
        parse_operation_attributes(op);
        return op;
    }
    std::vector<Token> tokens_; Diagnostics diagnostics_; std::size_t pos_{};
};
}

ParseResult parse_module(std::string_view source) { auto l=lex(source); return Parser(std::move(l.tokens),std::move(l.diagnostics)).run(); }
}
