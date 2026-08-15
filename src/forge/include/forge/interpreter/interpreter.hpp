// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "forge/diagnostics/diagnostic.hpp"
#include "forge/ir/module.hpp"

namespace forge::interpreter {

struct MemoryObject {
    std::vector<std::uint8_t> bytes;
    std::uint8_t* external_data{};
    std::size_t external_size{};
    std::unordered_map<std::size_t, std::string> function_slots;

    [[nodiscard]] std::uint8_t* data() noexcept { return external_data ? external_data : bytes.data(); }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return external_data ? external_data : bytes.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return external_data ? external_size : bytes.size(); }
};

struct Pointer {
    std::shared_ptr<MemoryObject> object;
    std::size_t offset{};
    bool read_only{};
};

class Value {
public:
    enum class Kind { void_, integer, floating, pointer, function };

    static Value void_value();
    static Value integer(ir::Type type, std::uint64_t bits);
    static Value floating(ir::Type type, double value);
    static Value pointer(Pointer pointer);
    static Value host_pointer(void* address, std::size_t size, bool read_only = false);
    static Value function(std::string name);

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] ir::Type type() const noexcept { return type_; }
    [[nodiscard]] std::uint64_t bits() const noexcept { return bits_; }
    [[nodiscard]] std::int64_t signed_value() const noexcept;
    [[nodiscard]] double floating_value() const noexcept;
    [[nodiscard]] const Pointer& as_pointer() const { return pointer_; }
    [[nodiscard]] void* host_address() const noexcept;
    [[nodiscard]] std::size_t remaining_bytes() const noexcept;
    [[nodiscard]] bool is_aligned(std::size_t alignment) const noexcept;
    [[nodiscard]] const std::string& function_name() const noexcept { return function_; }

private:
    Kind kind_{Kind::void_};
    ir::Type type_{ir::TypeKind::void_};
    std::uint64_t bits_{};
    Pointer pointer_{};
    std::string function_;
};

using ExternalFunction = std::function<std::optional<Value>(std::span<const Value>, Diagnostics&)>;
using ExternalMap = std::unordered_map<std::string, ExternalFunction>;
struct ExternalGlobal { void* address{}; std::size_t size{}; bool read_only{}; };
using ExternalGlobalMap = std::unordered_map<std::string, ExternalGlobal>;

struct Options {
    std::size_t max_steps{1'000'000};
    std::size_t max_call_depth{1024};
};

struct Result {
    std::optional<Value> value;
    Diagnostics diagnostics;
    std::size_t steps{};
};

[[nodiscard]] Result execute(const ir::Module& module,
                             std::string_view function,
                             std::span<const Value> arguments = {},
                             const ExternalMap& externals = {},
                             Options options = {});
[[nodiscard]] Result execute(const ir::Module& module,
                             std::string_view function,
                             std::span<const Value> arguments,
                             const ExternalMap& externals,
                             const ExternalGlobalMap& external_globals,
                             Options options = {});

} // namespace forge::interpreter
