// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <iostream>
#include <string>

extern "C" {
std::int64_t raz_rt_stage1_arena_create(std::int64_t count);
void raz_rt_stage1_arena_destroy(std::int64_t handle);
std::int64_t raz_rt_stage1_arena_get(std::int64_t handle, std::int64_t index);
double raz_rt_stage1_arena_get_f64(std::int64_t handle, std::int64_t index);
void raz_rt_stage1_arena_set_f64(std::int64_t handle, std::int64_t index, double value);
std::int64_t raz_rt_stage1_process_argc();
std::int64_t raz_rt_stage1_process_arg(std::int64_t index, std::int64_t output_handle, std::int64_t capacity);
}

int main(int argc, char** argv) {
  if (raz_rt_stage1_process_argc() != argc) {
    std::cerr << "runtime argc does not match process argc\n";
    return 1;
  }

  for (int index = 0; index < argc; ++index) {
    const std::string expected = argv[index] == nullptr ? "" : argv[index];
    const auto capacity = static_cast<std::int64_t>(expected.size() + 8);
    const auto arena = raz_rt_stage1_arena_create(capacity);
    if (arena == 0) {
      std::cerr << "failed to allocate argument arena\n";
      return 2;
    }
    const auto length = raz_rt_stage1_process_arg(index, arena, capacity);
    if (length != static_cast<std::int64_t>(expected.size())) {
      std::cerr << "argument length mismatch at index " << index << "\n";
      raz_rt_stage1_arena_destroy(arena);
      return 3;
    }
    std::string actual;
    actual.reserve(static_cast<std::size_t>(length));
    for (std::int64_t byte = 0; byte < length; ++byte) {
      actual.push_back(static_cast<char>(raz_rt_stage1_arena_get(arena, byte)));
    }

    raz_rt_stage1_arena_destroy(arena);
    if (actual != expected) {
      std::cerr << "argument mismatch at index " << index << ": '" << actual << "' != '" << expected << "'\n";
      return 4;
    }
  }

  const auto scratch = raz_rt_stage1_arena_create(8);
  if (scratch == 0) return 5;
  const auto invalid = raz_rt_stage1_process_arg(argc, scratch, 8);
  if (invalid != -1) {
    raz_rt_stage1_arena_destroy(scratch);
    std::cerr << "out-of-range argument lookup was not rejected\n";
    return 6;
  }

  raz_rt_stage1_arena_set_f64(scratch, 3, 1.5);
  if (raz_rt_stage1_arena_get_f64(scratch, 3) != 1.5) {
    raz_rt_stage1_arena_destroy(scratch);
    std::cerr << "f64 bootstrap arena round-trip failed\n";
    return 7;
  }

  raz_rt_stage1_arena_set_f64(scratch, 4, -2.25);
  if (raz_rt_stage1_arena_get_f64(scratch, 4) != -2.25) {
    raz_rt_stage1_arena_destroy(scratch);
    std::cerr << "negative f64 bootstrap arena round-trip failed\n";
    return 8;
  }

  raz_rt_stage1_arena_destroy(scratch);
  return 0;
}
