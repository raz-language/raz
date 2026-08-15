// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "runtime_internal.hpp"

using namespace raz::runtime_detail;

extern "C" {
std::int64_t raz_rt_volatile_load_i64(std::uintptr_t address) {
  if (address == 0) return 0;
  return *reinterpret_cast<volatile std::int64_t*>(address);
}

void raz_rt_volatile_store_i64(std::uintptr_t address, std::int64_t value) {
  if (address == 0) return;
  *reinterpret_cast<volatile std::int64_t*>(address) = value;
}

void raz_rt_memory_fence() { std::atomic_thread_fence(std::memory_order_seq_cst); }
std::int64_t raz_rt_cpu_has_sse2() {
#if defined(__x86_64__) || defined(_M_X64)
  return 1;
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
  return __builtin_cpu_supports("sse2") ? 1 : 0;
#else
  return 0;
#endif
}

std::int64_t raz_rt_cpu_has_avx2() {
#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
  return __builtin_cpu_supports("avx2") ? 1 : 0;
#elif defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
  int registers[4]{};
  __cpuidex(registers, 7, 0);
  return (registers[1] & (1 << 5)) != 0 ? 1 : 0;
#else
  return 0;
#endif
}

std::int64_t raz_rt_cpu_has_neon() {
#if defined(__aarch64__) || defined(_M_ARM64)
  return 1;
#else
  return 0;
#endif
}

std::int64_t raz_rt_cache_line_size() { return 64; }

void* raz_rt_task_spawn_callable_i64(void* callable) {
  if (callable == nullptr) return nullptr;
  auto* task = new (std::nothrow) RazTask();
  if (task == nullptr) { raz_rt_callable_destroy_erased(callable); return nullptr; }
  try {
    task->worker = std::thread([task, callable] {
      const auto signature = raz_rt_callable_signature_id(callable);
      const auto argument_size = raz_rt_callable_argument_size(callable);
      const auto result_size = raz_rt_callable_result_size(callable);
      alignas(8) std::uint64_t dummy_arguments = 0;
      std::int64_t value = 0;
      if (raz_rt_callable_invoke_erased(callable, signature, &dummy_arguments, argument_size, &value, result_size) != 0) value = 0;
      raz_rt_callable_destroy_erased(callable);
      {
        std::lock_guard lock(task->mutex);
        task->result = value;
        task->complete = true;
      }
      task->ready.notify_all();
    });
  } catch (...) {
    raz_rt_callable_destroy_erased(callable);
    delete task;
    return nullptr;
  }
  return task;
}

void* raz_rt_task_spawn_i64(std::uintptr_t entry_address, void* context) {
  if (entry_address == 0) return nullptr;
  auto* task = new (std::nothrow) RazTask();
  if (task == nullptr) return nullptr;
  const auto entry = reinterpret_cast<RazTaskEntry>(entry_address);
  try {
    task->worker = std::thread([task, entry, context] {
      const auto value = entry(context);
      {
        std::lock_guard lock(task->mutex);
        task->result = value;
        task->complete = true;
      }
      task->ready.notify_all();
    });
  } catch (...) {
    delete task;
    return nullptr;
  }
  return task;
}

std::int64_t raz_rt_task_is_ready(void* handle) {
  if (handle == nullptr) return 0;
  auto* task = static_cast<RazTask*>(handle);
  std::lock_guard lock(task->mutex);
  return task->complete ? 1 : 0;
}

std::int64_t raz_rt_task_wait_millis(void* handle, std::int64_t timeout_millis) {
  if (handle == nullptr || timeout_millis < 0) return 0;
  auto* task = static_cast<RazTask*>(handle);
  std::unique_lock lock(task->mutex);
  return task->ready.wait_for(lock, std::chrono::milliseconds(timeout_millis), [task] { return task->complete; }) ? 1 : 0;
}

std::int64_t raz_rt_task_join_i64(void* handle) {
  if (handle == nullptr) return 0;
  auto* task = static_cast<RazTask*>(handle);
  if (task->worker.joinable()) task->worker.join();
  const auto result = task->result;
  delete task;
  return result;
}

void raz_rt_yield_now() { std::this_thread::yield(); }

void* raz_rt_future_create() {
  return new (std::nothrow) RazFuture();
}

std::int64_t raz_rt_future_complete_i64(void* handle, std::int64_t value) {
  if (handle == nullptr) return 0;
  auto* future = static_cast<RazFuture*>(handle);
  return finish_future(future, value, false) ? 1 : 0;
}

std::int64_t raz_rt_future_cancel(void* handle) {
  if (handle == nullptr) return 0;
  return finish_future(static_cast<RazFuture*>(handle), 0, true) ? 1 : 0;
}

std::int64_t raz_rt_future_status(void* handle) {
  if (handle == nullptr) return -1;
  auto* future = static_cast<RazFuture*>(handle);
  std::lock_guard lock(future->mutex);
  if (future->cancelled) return -1;
  return future->complete ? 1 : 0;
}

std::int64_t raz_rt_future_wait_millis(void* handle, std::int64_t timeout_millis) {
  if (handle == nullptr || timeout_millis < 0) return 0;
  auto* future = static_cast<RazFuture*>(handle);
  std::unique_lock lock(future->mutex);
  if (!future->complete && !future->cancelled) {
    if (!future->ready.wait_for(lock, std::chrono::milliseconds(timeout_millis), [future] {
          return future->complete || future->cancelled;
        })) return 0;
  }
  return future->cancelled ? -1 : 1;
}

std::int64_t raz_rt_future_result_i64(void* handle, std::int64_t* output) {
  if (handle == nullptr) return -1;
  auto* future = static_cast<RazFuture*>(handle);
  std::lock_guard lock(future->mutex);
  if (future->cancelled) return -1;
  if (!future->complete) return 0;
  if (output != nullptr) *output = future->result;
  return 1;
}

std::int64_t raz_rt_future_then_i64(void* handle, std::uintptr_t continuation_address, void* context) {
  if (handle == nullptr || continuation_address == 0) return 0;
  auto* future = static_cast<RazFuture*>(handle);
  const auto continuation = reinterpret_cast<RazFutureContinuation>(continuation_address);
  (void)register_future_continuation(future, continuation, context);
  return 1;
}

std::int64_t raz_rt_future_continuation_count(const void* handle) {
  auto* future = const_cast<RazFuture*>(static_cast<const RazFuture*>(handle));
  if (future == nullptr) return 0;
  std::lock_guard lock(future->mutex);
  return static_cast<std::int64_t>(future->continuations.size());
}

void raz_rt_future_destroy(void* handle) {
  release_future(static_cast<RazFuture*>(handle));
}

void raz_rt_i64x4_add(const std::int64_t* lhs, const std::int64_t* rhs, std::int64_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] + rhs[lane];
}

void raz_rt_i64x4_sub(const std::int64_t* lhs, const std::int64_t* rhs, std::int64_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] - rhs[lane];
}

void raz_rt_i64x4_mul(const std::int64_t* lhs, const std::int64_t* rhs, std::int64_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] * rhs[lane];
}

void raz_rt_f64x2_add(const double* lhs, const double* rhs, double* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  output[0] = lhs[0] + rhs[0]; output[1] = lhs[1] + rhs[1];
}

void raz_rt_i64x4_min(const std::int64_t* lhs, const std::int64_t* rhs, std::int64_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] < rhs[lane] ? lhs[lane] : rhs[lane];
}

void raz_rt_i64x4_max(const std::int64_t* lhs, const std::int64_t* rhs, std::int64_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] > rhs[lane] ? lhs[lane] : rhs[lane];
}

void raz_rt_i64x4_equal(const std::int64_t* lhs, const std::int64_t* rhs, std::int64_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] == rhs[lane] ? -1 : 0;
}

std::int64_t raz_rt_i64x4_reduce_add(const std::int64_t* value) {
  if (value == nullptr) return 0;
  return value[0] + value[1] + value[2] + value[3];
}

void raz_rt_f64x2_sub(const double* lhs, const double* rhs, double* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  output[0] = lhs[0] - rhs[0]; output[1] = lhs[1] - rhs[1];
}

void raz_rt_f64x2_mul(const double* lhs, const double* rhs, double* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  output[0] = lhs[0] * rhs[0]; output[1] = lhs[1] * rhs[1];
}

void raz_rt_f64x2_div(const double* lhs, const double* rhs, double* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  output[0] = lhs[0] / rhs[0]; output[1] = lhs[1] / rhs[1];
}

void raz_rt_f64x2_min(const double* lhs, const double* rhs, double* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  output[0] = lhs[0] < rhs[0] ? lhs[0] : rhs[0]; output[1] = lhs[1] < rhs[1] ? lhs[1] : rhs[1];
}

void raz_rt_f64x2_max(const double* lhs, const double* rhs, double* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  output[0] = lhs[0] > rhs[0] ? lhs[0] : rhs[0]; output[1] = lhs[1] > rhs[1] ? lhs[1] : rhs[1];
}

double raz_rt_f64x2_reduce_add(const double* value) {
  return value == nullptr ? 0.0 : value[0] + value[1];
}

void raz_rt_i32x4_add(const std::int32_t* lhs, const std::int32_t* rhs, std::int32_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] + rhs[lane];
}

void raz_rt_i32x4_sub(const std::int32_t* lhs, const std::int32_t* rhs, std::int32_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] - rhs[lane];
}

void raz_rt_i32x4_mul(const std::int32_t* lhs, const std::int32_t* rhs, std::int32_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] * rhs[lane];
}

void raz_rt_i32x4_min(const std::int32_t* lhs, const std::int32_t* rhs, std::int32_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] < rhs[lane] ? lhs[lane] : rhs[lane];
}

void raz_rt_i32x4_max(const std::int32_t* lhs, const std::int32_t* rhs, std::int32_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] > rhs[lane] ? lhs[lane] : rhs[lane];
}

void raz_rt_i32x4_equal(const std::int32_t* lhs, const std::int32_t* rhs, std::int32_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] == rhs[lane] ? -1 : 0;
}

void raz_rt_i32x4_splat(std::int32_t value, std::int32_t* output) {
  if (output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = value;
}

void raz_rt_i32x4_neg(const std::int32_t* value, std::int32_t* output) {
  if (value == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = -value[lane];
}

std::int32_t raz_rt_i32x4_reduce_add(const std::int32_t* value) {
  return value == nullptr ? 0 : value[0] + value[1] + value[2] + value[3];
}

std::int32_t raz_rt_i32x4_all_true(const std::int32_t* value) {
  if (value == nullptr) return 0;
  return value[0] != 0 && value[1] != 0 && value[2] != 0 && value[3] != 0 ? 1 : 0;
}

std::int32_t raz_rt_i32x4_bitmask(const std::int32_t* value) {
  if (value == nullptr) return 0;
  std::int32_t mask = 0;
  for (std::size_t lane = 0; lane < 4; ++lane) {
    if (value[lane] < 0) mask |= static_cast<std::int32_t>(1U << lane);
  }
  return mask;
}

void raz_rt_f32x4_add(const float* lhs, const float* rhs, float* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] + rhs[lane];
}

void raz_rt_f32x4_sub(const float* lhs, const float* rhs, float* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] - rhs[lane];
}

void raz_rt_f32x4_mul(const float* lhs, const float* rhs, float* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] * rhs[lane];
}

void raz_rt_f32x4_div(const float* lhs, const float* rhs, float* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] / rhs[lane];
}

void raz_rt_f32x4_min(const float* lhs, const float* rhs, float* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] < rhs[lane] ? lhs[lane] : rhs[lane];
}

void raz_rt_f32x4_max(const float* lhs, const float* rhs, float* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] > rhs[lane] ? lhs[lane] : rhs[lane];
}

void raz_rt_f32x4_equal(const float* lhs, const float* rhs, std::int32_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] == rhs[lane] ? -1 : 0;
}

void raz_rt_f32x4_splat(float value, float* output) {
  if (output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = value;
}

void raz_rt_f32x4_abs(const float* value, float* output) {
  if (value == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = std::abs(value[lane]);
}

void raz_rt_f32x4_neg(const float* value, float* output) {
  if (value == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = -value[lane];
}

void raz_rt_f32x4_sqrt(const float* value, float* output) {
  if (value == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = std::sqrt(value[lane]);
}

float raz_rt_f32x4_reduce_add(const float* value) {
  return value == nullptr ? 0.0F : value[0] + value[1] + value[2] + value[3];
}

void raz_rt_i32x4_less(const std::int32_t* lhs, const std::int32_t* rhs, std::int32_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] < rhs[lane] ? -1 : 0;
}

void raz_rt_i32x4_greater(const std::int32_t* lhs, const std::int32_t* rhs, std::int32_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] > rhs[lane] ? -1 : 0;
}

void raz_rt_i32x4_and(const std::int32_t* lhs, const std::int32_t* rhs, std::int32_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] & rhs[lane];
}

void raz_rt_i32x4_or(const std::int32_t* lhs, const std::int32_t* rhs, std::int32_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] | rhs[lane];
}

void raz_rt_i32x4_xor(const std::int32_t* lhs, const std::int32_t* rhs, std::int32_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] ^ rhs[lane];
}

void raz_rt_i32x4_not(const std::int32_t* value, std::int32_t* output) {
  if (value == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = ~value[lane];
}

void raz_rt_f32x4_less(const float* lhs, const float* rhs, std::int32_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] < rhs[lane] ? -1 : 0;
}

void raz_rt_f32x4_greater(const float* lhs, const float* rhs, std::int32_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = lhs[lane] > rhs[lane] ? -1 : 0;
}

void raz_rt_f32x4_ceil(const float* value, float* output) {
  if (value == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = std::ceil(value[lane]);
}

void raz_rt_f32x4_floor(const float* value, float* output) {
  if (value == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = std::floor(value[lane]);
}

void raz_rt_f32x4_trunc(const float* value, float* output) {
  if (value == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = std::trunc(value[lane]);
}

void raz_rt_f32x4_nearest(const float* value, float* output) {
  if (value == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = std::nearbyint(value[lane]);
}

void raz_rt_f32x4_from_i32x4(const std::int32_t* value, float* output) {
  if (value == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = static_cast<float>(value[lane]);
}

void raz_rt_i32x4_from_f32x4_sat(const float* value, std::int32_t* output) {
  if (value == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 4; ++lane) {
    if (std::isnan(value[lane])) output[lane] = 0;
    else if (value[lane] >= static_cast<float>(std::numeric_limits<std::int32_t>::max())) output[lane] = std::numeric_limits<std::int32_t>::max();
    else if (value[lane] <= static_cast<float>(std::numeric_limits<std::int32_t>::min())) output[lane] = std::numeric_limits<std::int32_t>::min();
    else output[lane] = static_cast<std::int32_t>(value[lane]);
  }
}

void raz_rt_i8x16_swizzle(const std::uint8_t* value, const std::uint8_t* indices, std::uint8_t* output) {
  if (value == nullptr || indices == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 16; ++lane) output[lane] = indices[lane] < 16 ? value[indices[lane]] : 0;
}

void raz_rt_i8x16_splat(std::uint8_t value, std::uint8_t* output) {
  if (output == nullptr) return;
  for (std::size_t lane = 0; lane < 16; ++lane) output[lane] = value;
}

std::int32_t raz_rt_i8x16_bitmask(const std::uint8_t* value) {
  if (value == nullptr) return 0;
  std::int32_t mask = 0;
  for (std::size_t lane = 0; lane < 16; ++lane) {
    if ((value[lane] & 0x80U) != 0) mask |= static_cast<std::int32_t>(1U << lane);
  }
  return mask;
}

std::int32_t raz_rt_i8x16_any_true(const std::uint8_t* value) {
  if (value == nullptr) return 0;
  for (std::size_t lane = 0; lane < 16; ++lane) if (value[lane] != 0) return 1;
  return 0;
}

void raz_rt_i8x16_add(const std::int8_t* lhs, const std::int8_t* rhs, std::int8_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 16; ++lane) output[lane] = static_cast<std::int8_t>(static_cast<std::uint8_t>(lhs[lane]) + static_cast<std::uint8_t>(rhs[lane]));
}
void raz_rt_i8x16_sub(const std::int8_t* lhs, const std::int8_t* rhs, std::int8_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 16; ++lane) output[lane] = static_cast<std::int8_t>(static_cast<std::uint8_t>(lhs[lane]) - static_cast<std::uint8_t>(rhs[lane]));
}
void raz_rt_i8x16_min(const std::int8_t* lhs, const std::int8_t* rhs, std::int8_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 16; ++lane) output[lane] = lhs[lane] < rhs[lane] ? lhs[lane] : rhs[lane];
}
void raz_rt_i8x16_max(const std::int8_t* lhs, const std::int8_t* rhs, std::int8_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 16; ++lane) output[lane] = lhs[lane] > rhs[lane] ? lhs[lane] : rhs[lane];
}
void raz_rt_i8x16_equal(const std::int8_t* lhs, const std::int8_t* rhs, std::int8_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 16; ++lane) output[lane] = lhs[lane] == rhs[lane] ? static_cast<std::int8_t>(-1) : 0;
}
std::int32_t raz_rt_i8x16_all_true(const std::int8_t* value) {
  if (value == nullptr) return 0;
  for (std::size_t lane = 0; lane < 16; ++lane) if (value[lane] == 0) return 0;
  return 1;
}
void raz_rt_i8x16_shl(const std::int8_t* value, std::int32_t count, std::int8_t* output) {
  if (value == nullptr || output == nullptr) return;
  const auto shift = static_cast<unsigned>(count) & 7U;
  for (std::size_t lane = 0; lane < 16; ++lane) output[lane] = static_cast<std::int8_t>(static_cast<std::uint8_t>(value[lane]) << shift);
}
void raz_rt_i8x16_shr_s(const std::int8_t* value, std::int32_t count, std::int8_t* output) {
  if (value == nullptr || output == nullptr) return;
  const auto shift = static_cast<unsigned>(count) & 7U;
  for (std::size_t lane = 0; lane < 16; ++lane) output[lane] = static_cast<std::int8_t>(value[lane] >> shift);
}
void raz_rt_i8x16_shr_u(const std::uint8_t* value, std::int32_t count, std::uint8_t* output) {
  if (value == nullptr || output == nullptr) return;
  const auto shift = static_cast<unsigned>(count) & 7U;
  for (std::size_t lane = 0; lane < 16; ++lane) output[lane] = static_cast<std::uint8_t>(value[lane] >> shift);
}

void raz_rt_i16x8_add(const std::int16_t* lhs, const std::int16_t* rhs, std::int16_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 8; ++lane) output[lane] = static_cast<std::int16_t>(static_cast<std::uint16_t>(lhs[lane]) + static_cast<std::uint16_t>(rhs[lane]));
}
void raz_rt_i16x8_sub(const std::int16_t* lhs, const std::int16_t* rhs, std::int16_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 8; ++lane) output[lane] = static_cast<std::int16_t>(static_cast<std::uint16_t>(lhs[lane]) - static_cast<std::uint16_t>(rhs[lane]));
}
void raz_rt_i16x8_mul(const std::int16_t* lhs, const std::int16_t* rhs, std::int16_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 8; ++lane) output[lane] = static_cast<std::int16_t>(static_cast<std::uint16_t>(lhs[lane]) * static_cast<std::uint16_t>(rhs[lane]));
}
void raz_rt_i16x8_min(const std::int16_t* lhs, const std::int16_t* rhs, std::int16_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 8; ++lane) output[lane] = lhs[lane] < rhs[lane] ? lhs[lane] : rhs[lane];
}
void raz_rt_i16x8_max(const std::int16_t* lhs, const std::int16_t* rhs, std::int16_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 8; ++lane) output[lane] = lhs[lane] > rhs[lane] ? lhs[lane] : rhs[lane];
}
void raz_rt_i16x8_equal(const std::int16_t* lhs, const std::int16_t* rhs, std::int16_t* output) {
  if (lhs == nullptr || rhs == nullptr || output == nullptr) return;
  for (std::size_t lane = 0; lane < 8; ++lane) output[lane] = lhs[lane] == rhs[lane] ? static_cast<std::int16_t>(-1) : 0;
}
void raz_rt_i16x8_splat(std::int16_t value, std::int16_t* output) {
  if (output == nullptr) return;
  for (std::size_t lane = 0; lane < 8; ++lane) output[lane] = value;
}
std::int32_t raz_rt_i16x8_all_true(const std::int16_t* value) {
  if (value == nullptr) return 0;
  for (std::size_t lane = 0; lane < 8; ++lane) if (value[lane] == 0) return 0;
  return 1;
}
std::int32_t raz_rt_i16x8_bitmask(const std::int16_t* value) {
  if (value == nullptr) return 0;
  std::int32_t mask = 0;
  for (std::size_t lane = 0; lane < 8; ++lane) if (value[lane] < 0) mask |= static_cast<std::int32_t>(1U << lane);
  return mask;
}
void raz_rt_i16x8_shl(const std::int16_t* value, std::int32_t count, std::int16_t* output) {
  if (value == nullptr || output == nullptr) return;
  const auto shift = static_cast<unsigned>(count) & 15U;
  for (std::size_t lane = 0; lane < 8; ++lane) output[lane] = static_cast<std::int16_t>(static_cast<std::uint16_t>(value[lane]) << shift);
}
void raz_rt_i16x8_shr_s(const std::int16_t* value, std::int32_t count, std::int16_t* output) {
  if (value == nullptr || output == nullptr) return;
  const auto shift = static_cast<unsigned>(count) & 15U;
  for (std::size_t lane = 0; lane < 8; ++lane) output[lane] = static_cast<std::int16_t>(value[lane] >> shift);
}
void raz_rt_i16x8_shr_u(const std::uint16_t* value, std::int32_t count, std::uint16_t* output) {
  if (value == nullptr || output == nullptr) return;
  const auto shift = static_cast<unsigned>(count) & 15U;
  for (std::size_t lane = 0; lane < 8; ++lane) output[lane] = static_cast<std::uint16_t>(value[lane] >> shift);
}

void raz_rt_i32x4_shl(const std::int32_t* value, std::int32_t count, std::int32_t* output) {
  if (value == nullptr || output == nullptr) return;
  const auto shift = static_cast<unsigned>(count) & 31U;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = static_cast<std::int32_t>(static_cast<std::uint32_t>(value[lane]) << shift);
}
void raz_rt_i32x4_shr_s(const std::int32_t* value, std::int32_t count, std::int32_t* output) {
  if (value == nullptr || output == nullptr) return;
  const auto shift = static_cast<unsigned>(count) & 31U;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = value[lane] >> shift;
}
void raz_rt_i32x4_shr_u(const std::uint32_t* value, std::int32_t count, std::uint32_t* output) {
  if (value == nullptr || output == nullptr) return;
  const auto shift = static_cast<unsigned>(count) & 31U;
  for (std::size_t lane = 0; lane < 4; ++lane) output[lane] = value[lane] >> shift;
}

void* raz_rt_callable_create_erased(void* environment, void* invoke, void* clone_environment,
                                     void* drop_environment, std::int32_t kind,
                                     std::uint64_t signature_id, std::uint64_t argument_size,
                                     std::uint64_t result_size) {
  if (invoke == nullptr || kind < 0 || kind > 2) return nullptr;
  try {
    auto* callable = new RazErasedCallable;
    callable->environment = environment;
    callable->invoke = reinterpret_cast<RazErasedInvoke>(invoke);
    callable->clone_environment = reinterpret_cast<RazErasedClone>(clone_environment);
    callable->drop_environment = reinterpret_cast<RazErasedDrop>(drop_environment);
    callable->kind = static_cast<RazCallableKind>(kind);
    callable->signature_id = signature_id;
    callable->argument_size = argument_size;
    callable->result_size = result_size;
    return callable;
  } catch (...) { return nullptr; }
}

std::int32_t raz_rt_callable_invoke_erased(void* handle, std::uint64_t signature_id,
                                            const void* arguments, std::uint64_t argument_size,
                                            void* result, std::uint64_t result_size) {
  auto* callable = static_cast<RazErasedCallable*>(handle);
  if (callable == nullptr || callable->invoke == nullptr || callable->signature_id != signature_id ||
      callable->argument_size != argument_size || callable->result_size != result_size ||
      (argument_size != 0 && arguments == nullptr) || (result_size != 0 && result == nullptr)) return -1;
  std::unique_lock<std::mutex> lock(callable->invocation_mutex, std::defer_lock);
  if (callable->kind == RazCallableKind::mutable_call) lock.lock();
  if (callable->kind == RazCallableKind::once && callable->consumed.exchange(true, std::memory_order_acq_rel)) return -2;
  return callable->invoke(callable->environment, arguments, argument_size, result, result_size);
}

void* raz_rt_callable_clone_erased(void* handle) {
  auto* callable = static_cast<RazErasedCallable*>(handle);
  if (callable == nullptr || callable->kind == RazCallableKind::once) return nullptr;
  return try_retain_erased_callable(callable) ? callable : nullptr;
}

void raz_rt_callable_destroy_erased(void* handle) { release_erased_callable(static_cast<RazErasedCallable*>(handle)); }

// Low-level ownership bridge for Raz-owned schedulers. These three functions
// deliberately expose no queueing, scheduling, synchronization, or worker-pool
// policy: they only transfer an already-erased callable across an integer-sized
// storage boundary and dispatch/destroy it through the canonical callable ABI.
std::uintptr_t raz_rt_callable_into_raw(void* callable) {
  return reinterpret_cast<std::uintptr_t>(callable);
}

std::int64_t raz_rt_callable_invoke_once_i64_raw(std::uintptr_t raw) {
  auto* callable = reinterpret_cast<void*>(raw);
  if (callable == nullptr) return 0;
  const auto signature = raz_rt_callable_signature_id(callable);
  const auto argument_size = raz_rt_callable_argument_size(callable);
  const auto result_size = raz_rt_callable_result_size(callable);
  alignas(8) std::uint64_t dummy_arguments = 0;
  std::int64_t result = 0;
  const auto status = raz_rt_callable_invoke_erased(
      callable, signature, &dummy_arguments, argument_size, &result, result_size);
  raz_rt_callable_destroy_erased(callable);
  return status == 0 ? result : 0;
}

void raz_rt_callable_destroy_raw(std::uintptr_t raw) {
  if (raw != 0) raz_rt_callable_destroy_erased(reinterpret_cast<void*>(raw));
}

std::uint64_t raz_rt_callable_signature_id(const void* handle) { const auto* c=static_cast<const RazErasedCallable*>(handle); return c==nullptr?0:c->signature_id; }
std::uint64_t raz_rt_callable_argument_size(const void* handle) { const auto* c=static_cast<const RazErasedCallable*>(handle); return c==nullptr?0:c->argument_size; }
std::uint64_t raz_rt_callable_result_size(const void* handle) { const auto* c=static_cast<const RazErasedCallable*>(handle); return c==nullptr?0:c->result_size; }

void* raz_rt_trait_object_create_erased(void* data, std::uint64_t type_id, std::uint64_t trait_id,
                                         void* drop_data, const void* methods, std::uint64_t method_count) {
  if (data == nullptr || methods == nullptr || method_count == 0) return nullptr;
  try {
    auto* object = new RazErasedTraitObject;
    object->data = data; object->type_id = type_id; object->trait_id = trait_id;
    object->drop_data = reinterpret_cast<RazErasedDrop>(drop_data);
    const auto* source = static_cast<const RazErasedTraitMethod*>(methods);
    object->methods.assign(source, source + method_count);
    for (const auto& method : object->methods) if (method.invoke == nullptr) { delete object; return nullptr; }
    return object;
  } catch (...) { return nullptr; }
}

std::int32_t raz_rt_trait_object_invoke_erased(void* handle, std::uint64_t slot,
                                                std::uint64_t signature_id, const void* arguments,
                                                std::uint64_t argument_size, void* result,
                                                std::uint64_t result_size) {
  auto* object = static_cast<RazErasedTraitObject*>(handle);
  if (object == nullptr || slot >= object->methods.size()) return -1;
  const auto& method = object->methods[static_cast<std::size_t>(slot)];
  if (method.signature_id != signature_id || method.argument_size != argument_size ||
      method.result_size != result_size || (argument_size != 0 && arguments == nullptr) ||
      (result_size != 0 && result == nullptr)) return -1;
  return method.invoke(object->data, arguments, argument_size, result, result_size);
}

void* raz_rt_trait_object_clone_erased(void* handle) { auto* o=static_cast<RazErasedTraitObject*>(handle); return try_retain_erased_trait(o)?o:nullptr; }
void raz_rt_trait_object_destroy_erased(void* handle) { release_erased_trait(static_cast<RazErasedTraitObject*>(handle)); }
std::uint64_t raz_rt_trait_object_method_signature_id(const void* handle, std::uint64_t slot) {
  const auto* o=static_cast<const RazErasedTraitObject*>(handle); return o==nullptr||slot>=o->methods.size()?0:o->methods[static_cast<std::size_t>(slot)].signature_id;
}

}

extern "C" std::int64_t raz_rt_tool_available(const char* data, std::int64_t length) {
  if (data == nullptr || length <= 0) return 0;
  const std::string name(data, static_cast<std::size_t>(length));
#if defined(_WIN32)
  const DWORD required = SearchPathA(nullptr, name.c_str(), ".exe", 0, nullptr, nullptr);
  if (required != 0) return 1;
  return SearchPathA(nullptr, name.c_str(), nullptr, 0, nullptr, nullptr) != 0 ? 1 : 0;
#else
  if (name.find('/') != std::string::npos) return ::access(name.c_str(), X_OK) == 0 ? 1 : 0;
  const char* raw_path = std::getenv("PATH");
  if (raw_path == nullptr) return 0;
  std::string path(raw_path);
  std::size_t start = 0;
  while (start <= path.size()) {
    const auto end = path.find(':', start);
    const auto count = end == std::string::npos ? path.size() - start : end - start;
    std::filesystem::path candidate = count == 0 ? std::filesystem::path(name) : std::filesystem::path(path.substr(start, count)) / name;
    if (::access(candidate.c_str(), X_OK) == 0) return 1;
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return 0;
#endif
}

extern "C" std::int64_t raz_rt_host_platform() {
#if defined(_WIN32)
  return 1;
#elif defined(__APPLE__)
  return 3;
#elif defined(__linux__)
  return 2;
#else
  return 0;
#endif
}

extern "C" std::int64_t raz_rt_ed25519_keygen(unsigned char* private_key, std::int64_t private_capacity,
                                                  unsigned char* public_key, std::int64_t public_capacity) {
#if defined(RAZ_HAVE_OPENSSL)
  if (private_key == nullptr || public_key == nullptr || private_capacity < 32 || public_capacity < 32) return -1;
  EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
  if (context == nullptr) return -1;
  EVP_PKEY* key = nullptr;
  if (EVP_PKEY_keygen_init(context) != 1 || EVP_PKEY_keygen(context, &key) != 1 || key == nullptr) {
    EVP_PKEY_CTX_free(context); return -1;
  }
  std::size_t private_length = 32;
  std::size_t public_length = 32;
  const bool ok = EVP_PKEY_get_raw_private_key(key, private_key, &private_length) == 1 && private_length == 32 &&
                  EVP_PKEY_get_raw_public_key(key, public_key, &public_length) == 1 && public_length == 32;
  EVP_PKEY_free(key);
  EVP_PKEY_CTX_free(context);
  return ok ? 32 : -1;
#else
  (void)private_key; (void)private_capacity; (void)public_key; (void)public_capacity; return -1;
#endif
}

extern "C" std::int64_t raz_rt_ed25519_public(const unsigned char* private_key, std::int64_t private_length,
                                                  unsigned char* public_key, std::int64_t public_capacity) {
#if defined(RAZ_HAVE_OPENSSL)
  if (private_key == nullptr || public_key == nullptr || private_length != 32 || public_capacity < 32) return -1;
  EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, private_key, 32);
  if (key == nullptr) return -1;
  std::size_t length = 32;
  const bool ok = EVP_PKEY_get_raw_public_key(key, public_key, &length) == 1 && length == 32;
  EVP_PKEY_free(key);
  return ok ? 32 : -1;
#else
  (void)private_key; (void)private_length; (void)public_key; (void)public_capacity; return -1;
#endif
}

extern "C" std::int64_t raz_rt_ed25519_sign(const unsigned char* private_key, std::int64_t private_length,
                                                const unsigned char* message, std::int64_t message_length,
                                                unsigned char* signature, std::int64_t signature_capacity) {
#if defined(RAZ_HAVE_OPENSSL)
  if (private_key == nullptr || signature == nullptr || private_length != 32 || message_length < 0 ||
      signature_capacity < 64 || (message_length > 0 && message == nullptr)) return -1;
  EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, private_key, 32);
  if (key == nullptr) return -1;
  EVP_MD_CTX* context = EVP_MD_CTX_new();
  if (context == nullptr) { EVP_PKEY_free(key); return -1; }
  std::size_t length = 64;
  const bool ok = EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key) == 1 &&
                  EVP_DigestSign(context, signature, &length, message, static_cast<std::size_t>(message_length)) == 1 &&
                  length == 64;
  EVP_MD_CTX_free(context);
  EVP_PKEY_free(key);
  return ok ? 64 : -1;
#else
  (void)private_key; (void)private_length; (void)message; (void)message_length; (void)signature; (void)signature_capacity; return -1;
#endif
}

extern "C" std::int64_t raz_rt_ed25519_verify(const unsigned char* public_key, std::int64_t public_length,
                                                  const unsigned char* message, std::int64_t message_length,
                                                  const unsigned char* signature, std::int64_t signature_length) {
#if defined(RAZ_HAVE_OPENSSL)
  if (public_key == nullptr || signature == nullptr || public_length != 32 || signature_length != 64 ||
      message_length < 0 || (message_length > 0 && message == nullptr)) return -1;
  EVP_PKEY* key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, public_key, 32);
  if (key == nullptr) return -1;
  EVP_MD_CTX* context = EVP_MD_CTX_new();
  if (context == nullptr) { EVP_PKEY_free(key); return -1; }
  const int result = EVP_DigestVerifyInit(context, nullptr, nullptr, nullptr, key) == 1
      ? EVP_DigestVerify(context, signature, 64, message, static_cast<std::size_t>(message_length)) : -1;
  EVP_MD_CTX_free(context);
  EVP_PKEY_free(key);
  return result == 1 ? 1 : (result == 0 ? 0 : -1);
#else
  (void)public_key; (void)public_length; (void)message; (void)message_length; (void)signature; (void)signature_length; return -1;
#endif
}
