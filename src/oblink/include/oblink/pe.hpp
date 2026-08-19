// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace oblink::pe {

struct SectionImage {
  std::string name;
  std::uint32_t rva{};
  std::uint32_t characteristics{};
  std::vector<std::byte> data;
  std::uint32_t virtual_size{};
};

struct ImageOptions {
  std::uint64_t image_base{};
  std::uint32_t entry_rva{};
  std::uint32_t file_alignment{};
  std::uint32_t section_alignment{};
  std::uint16_t subsystem{};
  std::uint64_t stack_reserve{0x100000};
  std::uint64_t stack_commit{0x1000};
  std::uint64_t heap_reserve{0x100000};
  std::uint64_t heap_commit{0x1000};
  bool deterministic{};

  // PE data directories are expressed as RVAs into already-laid-out sections.
  // The Windows AMD64 executable path currently publishes imports/IAT,
  // exceptions, base relocations, and TLS; optional directories remain zero.
  std::uint32_t import_rva{};
  std::uint32_t import_size{};
  std::uint32_t exception_rva{};
  std::uint32_t exception_size{};
  std::uint32_t basereloc_rva{};
  std::uint32_t basereloc_size{};
  std::uint32_t tls_rva{};
  std::uint32_t tls_size{};
  std::uint32_t iat_rva{};
  std::uint32_t iat_size{};
};

[[nodiscard]] std::vector<std::byte> build_pe32_plus(std::span<const SectionImage> sections,
                                                      const ImageOptions& options);

} // namespace oblink::pe
