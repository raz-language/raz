// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/session/session.hpp"

#include <utility>

namespace raz::compiler {

Session::Session(SessionOptions options) : options_(std::move(options)) {}

const SessionOptions& Session::options() const noexcept { return options_; }

}  // namespace raz::compiler
