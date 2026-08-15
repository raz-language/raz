// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <memory>
#include <string>
#include <vector>
#include "forge/ir/module.hpp"

namespace forge::ir {

class Context {
public:
    Module& create_module(std::string name);
    [[nodiscard]] const std::vector<std::unique_ptr<Module>>& modules() const noexcept { return modules_; }
private:
    std::vector<std::unique_ptr<Module>> modules_;
};

} // namespace forge::ir
