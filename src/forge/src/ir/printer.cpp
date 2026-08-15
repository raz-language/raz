// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/printer.hpp"
#include <sstream>
#include <cctype>
#include <iomanip>

namespace forge::ir {
namespace {
void print_params(std::ostringstream& out, const std::vector<ValueDecl>& params) {
    out << '(';
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i) out << ", ";
        out << params[i].name << ": ";
        if (params[i].borrow_mode == BorrowMode::immutable) out << "borrow ";
        else if (params[i].borrow_mode == BorrowMode::mutable_) out << "borrow mut ";
        else if (params[i].owned) out << "owned ";
        if (!params[i].function_signature_name.empty()) out << "callback @" << params[i].function_signature_name;
        else if (params[i].aggregate_kind == AggregateRefKind::structure) out << "struct @" << params[i].aggregate_name;
        else if (params[i].aggregate_kind == AggregateRefKind::array) out << "array @" << params[i].aggregate_name;
        else out << params[i].type.str();
    }
    out << ')';
}

void print_bytes(std::ostringstream& out, const std::vector<std::uint8_t>& bytes) {
    out << '"';
    for (const auto byte : bytes) {
        switch (byte) {
        case 0: out << "\\0"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        default:
            if (std::isprint(static_cast<unsigned char>(byte))) out << static_cast<char>(byte);
            else out << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte) << std::dec;
            break;
        }
    }
    out << '"';
}

void print_symbol_prefix(std::ostringstream& out, SymbolLinkage linkage, SymbolVisibility visibility) {
    if (linkage == SymbolLinkage::internal) out << "internal ";
    else if (linkage == SymbolLinkage::weak) out << "weak ";
    if (visibility == SymbolVisibility::hidden) out << "hidden ";
}

void print_calling_convention(std::ostringstream& out, CallingConvention convention) {
    switch (convention) {
    case CallingConvention::platform: break;
    case CallingConvention::c: out << "c "; break;
    case CallingConvention::system_v: out << "systemv "; break;
    case CallingConvention::windows_x64: out << "win64 "; break;
    case CallingConvention::fast: out << "fast "; break;
    }
}

void print_return_type(std::ostringstream& out, const Function& function) {
    if (function.return_borrow_mode == BorrowMode::immutable) out << "borrow ";
    else if (function.return_borrow_mode == BorrowMode::mutable_) out << "borrow mut ";
    else if (function.return_owned) out << "owned ";
    if (function.return_aggregate_kind == AggregateRefKind::structure) out << "struct @" << function.return_aggregate_name;
    else if (function.return_aggregate_kind == AggregateRefKind::array) out << "array @" << function.return_aggregate_name;
    else out << function.return_type.str();
    if (function.return_borrow_mode != BorrowMode::none) out << " from " << function.return_borrow_parameter;
}

void print_args(std::ostringstream& out, const std::vector<std::string>& args) {
    if (args.empty()) return;
    out << '(';
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) out << ", ";
        out << args[i];
    }
    out << ')';
}
}

std::string print_module(const Module& module) {
    std::ostringstream out; out<<"module @"<<module.name()<<" {\n";
    for (const auto& structure : module.structs()) {
        out << "  " << (structure.move_only ? "moveonly " : "") << "struct @" << structure.name << " { ";
        for (std::size_t i = 0; i < structure.fields.size(); ++i) {
            if (i) out << ", ";
            out << structure.fields[i].name << ": ";
            if (structure.fields[i].aggregate_kind == AggregateRefKind::structure)
                out << "struct @" << structure.fields[i].aggregate_name;
            else if (structure.fields[i].aggregate_kind == AggregateRefKind::array)
                out << "array @" << structure.fields[i].aggregate_name;
            else
                out << structure.fields[i].type.str();
        }
        out << " }\n";
    }

    for (const auto& array : module.arrays()) {
        out << "  " << (array.move_only ? "moveonly " : "") << "array @" << array.name << " = ";
        if (array.element_aggregate_kind == AggregateRefKind::structure) out << "struct @" << array.element_aggregate_name;
        else if (array.element_aggregate_kind == AggregateRefKind::array) out << "array @" << array.element_aggregate_name;
        else out << array.element_type.str();
        if (array.element_aggregate_kind != AggregateRefKind::scalar) out << " ";
        out << "[" << array.element_count << "]\n";
    }

    if ((!module.structs().empty() || !module.arrays().empty()) && (!module.globals().empty() || !module.functions().empty())) out << "\n";
    for (const auto& global : module.globals()) {
        out << "  ";
        if (global.is_external) out << "extern ";
        if (global.is_thread_local) out << "thread_local ";
        print_symbol_prefix(out, global.linkage, global.visibility);
        out << (global.is_constant ? "constant" : "global") << " @" << global.name
            << ": ";
        if (!global.function_signature_name.empty()) { out << "callback @" << global.function_signature_name; if (global.element_count != 1) out << '[' << global.element_count << ']'; }
        else if (global.aggregate_kind == AggregateRefKind::structure) out << "struct @" << global.aggregate_name;
        else if (global.aggregate_kind == AggregateRefKind::array) out << "array @" << global.aggregate_name;
        else { out << global.type.str(); if (global.element_count != 1) out << '[' << global.element_count << ']'; }
        if (global.alignment) out << " align " << global.alignment;
        if (!global.is_external) {
            out << " = ";
            if (global.zero_initialized) out << "zero";
            else if (!global.bytes.empty() || global.element_count != 1 || global.is_named_aggregate()) print_bytes(out, global.bytes);
            else out << global.initializer;
        }
        out << "\n";
    }

    if (!module.globals().empty() && !module.functions().empty()) out << "\n";
    for(const auto& fn:module.functions()) {
        out << "  ";
        if (fn.is_external) out << "extern ";
        if (fn.is_signature) out << "signature ";
        if (fn.variadic) out << "variadic ";
        print_symbol_prefix(out, fn.linkage, fn.visibility);
        print_calling_convention(out, fn.calling_convention);
        if (!fn.target_feature.empty()) out << fn.target_feature << ' ';
        if (!fn.is_signature) out << "func ";
        out << "@" << fn.name;
        print_params(out, fn.parameters);
        out << " -> ";
        print_return_type(out, fn);
        if (fn.is_signature || fn.is_external) { out << "\n"; continue; }
        out << " {\n";
        for(const auto& block:fn.blocks) { out<<"  "<<block.name; if(!block.parameters.empty()) print_params(out,block.parameters); out<<":\n";
            for(const auto& op:block.operations) { out<<"    "; if(!op.result.empty())out<<op.result<<" = "; out<<op.opcode;
                if(op.opcode=="jump") { out<<' '<<op.successors[0]; print_args(out,op.successor_arguments[0]); }
                else if(op.opcode=="branch") { out<<' '<<op.operands[0]<<", "<<op.successors[0]; print_args(out,op.successor_arguments[0]); out<<", "<<op.successors[1]; print_args(out,op.successor_arguments[1]); }
                else if(op.opcode=="return") { if(!op.operands.empty()) out<<' '<<op.operands[0]; }
                else if(op.opcode=="func.address" || op.opcode=="callback.address") { out<<' '<<op.type.str()<<' '<<op.operands[0]; if(op.operands.size()>1) out<<" as "<<op.operands[1]; }
                else if(op.opcode=="call" || op.opcode=="call.indirect") { out<<' '<<op.type.str()<<' '<<op.operands[0]; std::size_t first=1; if(op.opcode=="call.indirect" && op.operands.size()>1 && op.operands[1].starts_with("@")){ out<<" as "<<op.operands[1]; first=2; } out<<'('; for(std::size_t i=first;i<op.operands.size();++i){ if(i>first)out<<", "; out<<op.operands[i]; } out<<')'; }
                else if(op.opcode!="unreachable") { out<<' '<<op.type.str(); for(const auto& x:op.operands) out<<' '<<x; if(op.alignment) out<<" align "<<op.alignment; }
                out<<'\n';
            }
        } out<<"  }\n";
    } out<<"}\n"; return out.str();
}
}
