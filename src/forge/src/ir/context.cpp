// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/context.hpp"

namespace forge::ir {
Module& Context::create_module(std::string name) {
    modules_.push_back(std::make_unique<Module>(std::move(name)));
    return *modules_.back();
}
}
