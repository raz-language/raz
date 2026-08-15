// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <forge/ir/context.hpp>
#include <forge/version.hpp>

#include <iostream>

int main() {
    forge::ir::Context context;
    (void)context;
    std::cout << FORGE_VERSION_STRING << '\n';
    return 0;
}
