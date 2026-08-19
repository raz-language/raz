// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "oblink/diagnostic.hpp"

namespace oblink {

struct LinkOptions {
  std::filesystem::path output;
  // Empty means infer the Windows startup root. If a CRT startup provider is
  // present, ObLink uses mainCRTStartup/wmainCRTStartup/WinMainCRTStartup;
  // otherwise a freestanding image falls back to the user main symbol.
  std::string entry;
  bool infer_crt_startup{true};
  std::uint64_t image_base{0x0000000140000000ULL};
  std::uint32_t file_alignment{0x200};
  std::uint32_t section_alignment{0x1000};
  std::uint16_t subsystem{3}; // IMAGE_SUBSYSTEM_WINDOWS_CUI
  std::uint64_t stack_reserve{0x100000};
  std::uint64_t stack_commit{0x1000};
  std::uint64_t heap_reserve{0x100000};
  std::uint64_t heap_commit{0x1000};
  bool deterministic{true};
  // Reports which archive member satisfied which symbol, and the final section
  // layout. Linking large C++ archives is otherwise opaque when a member is
  // pulled for a symbol nobody expected to be live.
  bool verbose{false};
  // When set, a link map naming every image section and every external symbol
  // address is written here. Diagnosing a bad relocation without one means
  // disassembling the output and guessing at what should have been there.
  std::filesystem::path map_output;

  // Search state is part of the linker rather than delegated to a compiler
  // driver.  This lets ObLink consume /DEFAULTLIB directives emitted by
  // clang-cl/MSVC objects and resolve the corresponding COFF/import archives.
  std::vector<std::filesystem::path> library_paths;
  std::vector<std::string> libraries;
};

struct LinkResult {
  Diagnostics diagnostics;
  std::filesystem::path output;
  std::size_t input_count{};
  std::size_t output_bytes{};
  // Populated only when LinkOptions::verbose is set.
  std::vector<std::string> trace;
  [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

[[nodiscard]] LinkResult link(const std::vector<std::filesystem::path>& inputs,
                              const LinkOptions& options);

} // namespace oblink
