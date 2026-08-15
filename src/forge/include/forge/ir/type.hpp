// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace forge::ir {

enum class TypeKind : std::uint8_t { void_, i1, i8, i16, i32, i64, f32, f64, ptr };

class Type {
public:
    constexpr explicit Type(TypeKind kind = TypeKind::void_) noexcept : kind_(kind) {}
    [[nodiscard]] constexpr TypeKind kind() const noexcept { return kind_; }
    [[nodiscard]] constexpr bool is_integer() const noexcept {
        return kind_ >= TypeKind::i1 && kind_ <= TypeKind::i64;
    }
    [[nodiscard]] constexpr bool is_float() const noexcept {
        return kind_ == TypeKind::f32 || kind_ == TypeKind::f64;
    }
    [[nodiscard]] constexpr bool is_numeric() const noexcept { return is_integer() || is_float(); }
    [[nodiscard]] std::string str() const;
    [[nodiscard]] static Type parse(std::string_view text);
    friend constexpr bool operator==(Type, Type) = default;
private:
    TypeKind kind_;
};

[[nodiscard]] constexpr Type void_type() noexcept { return Type(TypeKind::void_); }
[[nodiscard]] constexpr Type i1_type() noexcept { return Type(TypeKind::i1); }
[[nodiscard]] constexpr Type i8_type() noexcept { return Type(TypeKind::i8); }
[[nodiscard]] constexpr Type i16_type() noexcept { return Type(TypeKind::i16); }
[[nodiscard]] constexpr Type i32_type() noexcept { return Type(TypeKind::i32); }
[[nodiscard]] constexpr Type i64_type() noexcept { return Type(TypeKind::i64); }
[[nodiscard]] constexpr Type f32_type() noexcept { return Type(TypeKind::f32); }
[[nodiscard]] constexpr Type f64_type() noexcept { return Type(TypeKind::f64); }
[[nodiscard]] constexpr Type ptr_type() noexcept { return Type(TypeKind::ptr); }

} // namespace forge::ir
