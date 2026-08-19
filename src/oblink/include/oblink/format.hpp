// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

// Constants shared by the object reader and the image writer, named after the
// Microsoft PE/COFF specification.
namespace oblink {

inline constexpr std::uint16_t coff_machine_amd64 = 0x8664;

// AMD64 relocation types.
inline constexpr std::uint16_t reloc_amd64_absolute = 0x0000;
inline constexpr std::uint16_t reloc_amd64_addr64 = 0x0001;
inline constexpr std::uint16_t reloc_amd64_addr32 = 0x0002;
inline constexpr std::uint16_t reloc_amd64_addr32nb = 0x0003;
inline constexpr std::uint16_t reloc_amd64_rel32 = 0x0004;
inline constexpr std::uint16_t reloc_amd64_rel32_1 = 0x0005;
inline constexpr std::uint16_t reloc_amd64_rel32_2 = 0x0006;
inline constexpr std::uint16_t reloc_amd64_rel32_3 = 0x0007;
inline constexpr std::uint16_t reloc_amd64_rel32_4 = 0x0008;
inline constexpr std::uint16_t reloc_amd64_rel32_5 = 0x0009;
inline constexpr std::uint16_t reloc_amd64_section = 0x000A;
inline constexpr std::uint16_t reloc_amd64_secrel = 0x000B;

// Symbol storage classes.
inline constexpr std::uint8_t storage_class_external = 2;
inline constexpr std::uint8_t storage_class_static = 3;
inline constexpr std::uint8_t storage_class_label = 6;
inline constexpr std::uint8_t storage_class_file = 103;
inline constexpr std::uint8_t storage_class_section = 104;
inline constexpr std::uint8_t storage_class_weak_external = 105;

// COMDAT selection values from a section definition auxiliary record.
inline constexpr std::uint8_t comdat_none = 0;
inline constexpr std::uint8_t comdat_no_duplicates = 1;
inline constexpr std::uint8_t comdat_any = 2;
inline constexpr std::uint8_t comdat_same_size = 3;
inline constexpr std::uint8_t comdat_exact_match = 4;
inline constexpr std::uint8_t comdat_associative = 5;
inline constexpr std::uint8_t comdat_largest = 6;

// Weak external characteristics.
inline constexpr std::uint32_t weak_search_no_library = 1;
inline constexpr std::uint32_t weak_search_library = 2;
inline constexpr std::uint32_t weak_search_alias = 3;

// Section characteristics.
inline constexpr std::uint32_t section_code = 0x00000020;
inline constexpr std::uint32_t section_initialized_data = 0x00000040;
inline constexpr std::uint32_t section_uninitialized_data = 0x00000080;
inline constexpr std::uint32_t section_link_info = 0x00000200;
inline constexpr std::uint32_t section_link_remove = 0x00000800;
inline constexpr std::uint32_t section_align_mask = 0x00F00000;
inline constexpr std::uint32_t section_memory_discardable = 0x02000000;
inline constexpr std::uint32_t section_memory_execute = 0x20000000;
inline constexpr std::uint32_t section_memory_read = 0x40000000;
inline constexpr std::uint32_t section_memory_write = 0x80000000;

// Image subsystems.
inline constexpr std::uint16_t subsystem_windows_gui = 2;
inline constexpr std::uint16_t subsystem_console = 3;

// Data directory slots.
inline constexpr std::size_t directory_import = 1;
inline constexpr std::size_t directory_exception = 3;
inline constexpr std::size_t directory_base_relocation = 5;
inline constexpr std::size_t directory_tls = 9;
inline constexpr std::size_t directory_import_address_table = 12;
inline constexpr std::size_t directory_count = 16;

} // namespace oblink
