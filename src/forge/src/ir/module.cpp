// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/type.hpp"
#include <stdexcept>

namespace forge::ir {
std::string Type::str() const {
    switch (kind_) {
        case TypeKind::void_: return "void";
        case TypeKind::i1: return "i1";
        case TypeKind::i8: return "i8";
        case TypeKind::i16: return "i16";
        case TypeKind::i32: return "i32";
        case TypeKind::i64: return "i64";
        case TypeKind::f32: return "f32";
        case TypeKind::f64: return "f64";
        case TypeKind::ptr: return "ptr";
    }
    return "void";
}

Type Type::parse(std::string_view text) {
    if (text == "void") return Type(TypeKind::void_);
    if (text == "i1") return Type(TypeKind::i1);
    if (text == "i8") return Type(TypeKind::i8);
    if (text == "i16") return Type(TypeKind::i16);
    if (text == "i32") return Type(TypeKind::i32);
    if (text == "i64") return Type(TypeKind::i64);
    if (text == "f32") return Type(TypeKind::f32);
    if (text == "f64") return Type(TypeKind::f64);
    if (text == "ptr") return Type(TypeKind::ptr);
    throw std::invalid_argument("unknown Forge IR type");
}
}
