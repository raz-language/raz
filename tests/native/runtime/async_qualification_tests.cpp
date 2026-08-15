// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

extern "C" {
void* raz_rt_future_create();
std::int64_t raz_rt_future_complete_i64(void*, std::int64_t);
std::int64_t raz_rt_future_cancel(void*);
std::int64_t raz_rt_future_wait_millis(void*, std::int64_t);
std::int64_t raz_rt_future_result_i64(void*, std::int64_t*);
void raz_rt_future_destroy(void*);
}

namespace {
std::atomic<int> failures{0};
void expect(bool ok, const char* message) {
  if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main() {
  // The native future is intentionally only the compiler-facing async kernel.
  // Higher-level map/zip/race/timeout composition is implemented and qualified
  // in Raz's std::thread::future / scheduler layers.
  for (int iteration = 0; iteration < 2000; ++iteration) {
    void* future = raz_rt_future_create();
    expect(future != nullptr, "future kernel allocation");
    std::atomic<std::int64_t> wins{0};
    std::thread complete([&] { wins.fetch_add(raz_rt_future_complete_i64(future, 42)); });
    std::thread cancel([&] { wins.fetch_add(raz_rt_future_cancel(future)); });
    complete.join();
    cancel.join();
    expect(wins.load() == 1, "future terminal transition has exactly one winner");
    expect(raz_rt_future_wait_millis(future, 100) != 0, "future is terminal after race");
    std::int64_t value = 0;
    const auto status = raz_rt_future_result_i64(future, &value);
    expect(status == -1 || (status == 1 && value == 42), "future terminal result is coherent");
    raz_rt_future_destroy(future);
  }
  return failures.load() == 0 ? 0 : 1;
}
