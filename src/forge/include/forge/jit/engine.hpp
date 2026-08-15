// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "forge/codegen/x86_64/encoder.hpp"
#include "forge/diagnostics/diagnostic.hpp"
#include "forge/machine/module.hpp"

namespace forge::jit {

struct LoadResult;

class Engine {
public:
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;
    ~Engine();

    [[nodiscard]] void* lookup(std::string_view name) const noexcept;
    [[nodiscard]] void* lookup_global(std::string_view name) const noexcept;
    [[nodiscard]] std::size_t code_size() const noexcept { return code_size_; }
    [[nodiscard]] bool is_global_read_only(std::string_view name) const noexcept;

private:
    friend struct LoadResult;
    friend LoadResult load(const machine::Module&, codegen::x86_64::Abi,
                           const codegen::x86_64::ExternalResolver&,
                           const codegen::x86_64::ExternalResolver&);
    Engine() = default;

    void* memory_{};
    std::size_t allocation_size_{};
    std::size_t code_size_{};
    std::vector<std::pair<std::string, std::size_t>> entries_;
    std::vector<codegen::x86_64::EncodedGlobal> globals_;
    std::size_t read_only_data_offset_{};
    std::size_t writable_data_offset_{};
};

struct LoadResult {
    std::unique_ptr<Engine> engine;
    Diagnostics diagnostics;
    [[nodiscard]] bool ok() const noexcept { return engine != nullptr && diagnostics.empty(); }
};

[[nodiscard]] LoadResult load(
    const machine::Module& module,
    codegen::x86_64::Abi abi,
    const codegen::x86_64::ExternalResolver& resolver = {},
    const codegen::x86_64::ExternalResolver& global_resolver = {});

} // namespace forge::jit
