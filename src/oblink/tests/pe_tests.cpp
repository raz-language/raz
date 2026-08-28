// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "oblink/pe.hpp"

namespace {

std::uint16_t read_u16(const std::vector<std::byte>& bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes.at(offset))) |
         static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes.at(offset + 1U)) << 8U);
}

std::uint32_t read_u32(const std::vector<std::byte>& bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4U; ++i)
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes.at(offset + i))) << (8U * i);
  return value;
}

int fail(const char* message) {
  std::cerr << "oblink PE tests: FAIL: " << message << '\n';
  return 1;
}

} // namespace

int main() {
  using namespace oblink::pe;

  SectionImage text;
  text.name = ".text";
  text.rva = 0x1000;
  text.virtual_size = 1;
  text.characteristics = 0x60000020U;
  text.data = {std::byte{0xC3}};

  ImageOptions options;
  options.image_base = 0x140000000ULL;
  options.entry_rva = 0x1000;
  options.file_alignment = 0x200;
  options.section_alignment = 0x1000;
  options.subsystem = 3;
  options.deterministic = true;

  const std::vector<SectionImage> sections{text};
  const auto first = build_pe32_plus(sections, options);
  const auto second = build_pe32_plus(sections, options);
  if (first != second) return fail("deterministic inputs produced different images");
  if (first.size() < 0x200U) return fail("image is unexpectedly small");
  if (read_u16(first, 0) != 0x5A4DU) return fail("DOS MZ signature is missing");
  const auto pe_offset = read_u32(first, 0x3CU);
  if (read_u32(first, pe_offset) != 0x00004550U) return fail("PE signature is missing");
  if (read_u16(first, pe_offset + 4U) != 0x8664U) return fail("machine is not AMD64");
  if (read_u16(first, pe_offset + 6U) != 1U) return fail("section count is not one");

  bool empty_rejected = false;
  try {
    (void)build_pe32_plus({}, options);
  } catch (const std::runtime_error&) {
    empty_rejected = true;
  }
  if (!empty_rejected) return fail("empty images were accepted");

  bool zero_alignment_rejected = false;
  try {
    auto invalid = options;
    invalid.file_alignment = 0;
    (void)build_pe32_plus(sections, invalid);
  } catch (const std::runtime_error&) {
    zero_alignment_rejected = true;
  }
  if (!zero_alignment_rejected) return fail("zero file alignment was accepted");

  std::cout << "oblink PE tests: PASS\n";
  return 0;
}
