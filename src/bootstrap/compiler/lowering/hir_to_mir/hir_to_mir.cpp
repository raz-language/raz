// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <limits>
#include "compiler/lowering/hir_to_mir/hir_to_mir.hpp"

#include "compiler/diagnostics/diagnostic_engine.hpp"
#include "compiler/semantic/type.hpp"
#include "compiler/syntax/syntax_kind.hpp"

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace raz::compiler {

// MIR lowering is grouped into helper machinery and module orchestration.
#include "compiler/lowering/hir_to_mir/detail/lowering_helpers.hpp"
#include "compiler/lowering/hir_to_mir/detail/lower_module.hpp"

}  // namespace raz::compiler
