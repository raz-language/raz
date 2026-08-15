// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "runtime_internal.hpp"

using namespace raz::runtime_detail;

extern "C" {
std::int64_t raz_rt_abi_pointer_size() { return static_cast<std::int64_t>(sizeof(void*)); }
std::int64_t raz_rt_abi_pointer_alignment() { return static_cast<std::int64_t>(alignof(void*)); }
std::int64_t raz_rt_abi_bool_size() { return static_cast<std::int64_t>(sizeof(bool)); }
std::int64_t raz_rt_abi_bool_alignment() { return static_cast<std::int64_t>(alignof(bool)); }
std::int64_t raz_rt_abi_i8_size() { return static_cast<std::int64_t>(sizeof(std::int8_t)); }
std::int64_t raz_rt_abi_i8_alignment() { return static_cast<std::int64_t>(alignof(std::int8_t)); }
std::int64_t raz_rt_abi_i16_size() { return static_cast<std::int64_t>(sizeof(std::int16_t)); }
std::int64_t raz_rt_abi_i16_alignment() { return static_cast<std::int64_t>(alignof(std::int16_t)); }
std::int64_t raz_rt_abi_i32_size() { return static_cast<std::int64_t>(sizeof(std::int32_t)); }
std::int64_t raz_rt_abi_i32_alignment() { return static_cast<std::int64_t>(alignof(std::int32_t)); }
std::int64_t raz_rt_abi_i64_size() { return static_cast<std::int64_t>(sizeof(std::int64_t)); }
std::int64_t raz_rt_abi_i64_alignment() { return static_cast<std::int64_t>(alignof(std::int64_t)); }
std::int64_t raz_rt_abi_f32_size() { return static_cast<std::int64_t>(sizeof(float)); }
std::int64_t raz_rt_abi_f32_alignment() { return static_cast<std::int64_t>(alignof(float)); }
std::int64_t raz_rt_abi_f64_size() { return static_cast<std::int64_t>(sizeof(double)); }
std::int64_t raz_rt_abi_f64_alignment() { return static_cast<std::int64_t>(alignof(double)); }
std::int64_t raz_rt_abi_size_t_size() { return static_cast<std::int64_t>(sizeof(std::size_t)); }
std::int64_t raz_rt_abi_size_t_alignment() { return static_cast<std::int64_t>(alignof(std::size_t)); }
std::int64_t raz_rt_abi_little_endian() {
  const std::uint16_t value = 1;
  return *reinterpret_cast<const std::uint8_t*>(&value) == 1 ? 1 : 0;
}

std::int64_t raz_rt_random_fill(void* output, std::int64_t size) {
  if (size < 0 || (size > 0 && output == nullptr)) {
    raz_set_last_error(EINVAL);
    return 0;
  }

  if (size == 0) {
    raz_clear_last_error();
    return 1;
  }
#if defined(_WIN32)
  auto* bytes = static_cast<unsigned char*>(output);
  std::int64_t remaining = size;
  while (remaining > 0) {
    const auto chunk = static_cast<ULONG>(std::min<std::int64_t>(remaining, static_cast<std::int64_t>(std::numeric_limits<ULONG>::max())));
    const NTSTATUS status = BCryptGenRandom(nullptr, bytes, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
      raz_set_last_error(static_cast<std::int64_t>(status));
      return 0;
    }
    bytes += chunk;
    remaining -= chunk;
  }
#elif defined(__APPLE__)
  arc4random_buf(output, static_cast<std::size_t>(size));
#elif defined(__linux__)
  auto* bytes = static_cast<unsigned char*>(output);
  std::int64_t remaining = size;
  while (remaining > 0) {
    const auto count = ::getrandom(bytes, static_cast<std::size_t>(remaining), 0);
    if (count < 0) {
      if (errno == EINTR) continue;
      raz_set_errno_error();
      return 0;
    }
    if (count == 0) {
      raz_set_last_error(EIO);
      return 0;
    }
    bytes += count;
    remaining -= static_cast<std::int64_t>(count);
  }
#else
  std::random_device source;
  auto* bytes = static_cast<unsigned char*>(output);
  for (std::int64_t index = 0; index < size; ++index) {
    bytes[index] = static_cast<unsigned char>(source());
  }
#endif
  raz_clear_last_error();
  return 1;
}

std::uint64_t raz_rt_random_seed() {
  std::uint64_t value = 0;
  if (raz_rt_random_fill(&value, static_cast<std::int64_t>(sizeof(value))) != 1) return 0;
  return value == 0 ? 1 : value;
}

std::int64_t raz_rt_time_unix_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

std::int64_t raz_rt_time_monotonic_nanos() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

void raz_rt_sleep_millis(std::int64_t millis) {
  if (millis > 0) std::this_thread::sleep_for(std::chrono::milliseconds(millis));
}

std::int64_t raz_rt_process_id() {
#if defined(_WIN32)
  return static_cast<std::int64_t>(GetCurrentProcessId());
#else
  return static_cast<std::int64_t>(::getpid());
#endif
}

std::int64_t raz_rt_hardware_threads() {
  const auto count = std::thread::hardware_concurrency();
  return count == 0 ? 1 : static_cast<std::int64_t>(count);
}

void* raz_rt_alloc(std::int64_t size) { return size <= 0 ? nullptr : std::malloc(static_cast<std::size_t>(size)); }
void* raz_rt_alloc_aligned(std::int64_t size, std::int64_t alignment) {
  if (size <= 0 || alignment <= 0 || alignment > 4096 ||
      (alignment & (alignment - 1)) != 0) return nullptr;
  const auto normalized = static_cast<std::size_t>(std::max<std::int64_t>(alignment, alignof(std::max_align_t)));
  return ::operator new(static_cast<std::size_t>(size), std::align_val_t(normalized), std::nothrow);
}

void raz_rt_dealloc_aligned(void* pointer, std::int64_t alignment) {
  if (pointer == nullptr) return;
  if (alignment <= 0 || alignment > 4096 || (alignment & (alignment - 1)) != 0) return;
  const auto normalized = static_cast<std::size_t>(std::max<std::int64_t>(alignment, alignof(std::max_align_t)));
  ::operator delete(pointer, std::align_val_t(normalized));
}

void* raz_rt_realloc(void* pointer, std::int64_t size) {
  if (size <= 0) { std::free(pointer); return nullptr; }
  return std::realloc(pointer, static_cast<std::size_t>(size));
}

void raz_rt_dealloc(void* pointer) { std::free(pointer); }
void raz_rt_memcpy(void* destination, const void* source, std::int64_t size) {
  if (destination == nullptr || source == nullptr || size <= 0) return;
  std::memcpy(destination, source, static_cast<std::size_t>(size));
}
// Bootstrap arena ABI: compact i64 storage used by generated compiler stages. The
// public handle still points at element zero; a private header stores bounds so hot
// scalar reads and writes avoid the global allocation registry. The registry remains
// responsible for lifetime and reference validation.


void __forge_memmove(void* destination, const void* source, std::int64_t size) {
  if (destination != nullptr && source != nullptr && size > 0) std::memmove(destination, source, static_cast<std::size_t>(size));
}

void __forge_memset(void* destination, std::int64_t value, std::int64_t size) {
  if (destination != nullptr && size > 0) std::memset(destination, static_cast<int>(value), static_cast<std::size_t>(size));
}

void raz_rt_copy(void* destination, const void* source, std::int64_t size) {
  if (destination != nullptr && source != nullptr && size > 0) std::memcpy(destination, source, static_cast<std::size_t>(size));
}

void raz_rt_move(void* destination, const void* source, std::int64_t size) {
  if (destination != nullptr && source != nullptr && size > 0) std::memmove(destination, source, static_cast<std::size_t>(size));
}

void raz_rt_fill(void* destination, std::int64_t value, std::int64_t size) {
  if (destination != nullptr && size > 0) std::memset(destination, static_cast<int>(value), static_cast<std::size_t>(size));
}

std::int64_t raz_rt_atomic_load_i64(const std::int64_t* value) {
  return value == nullptr ? 0 : std::atomic_ref<const std::int64_t>(*value).load(std::memory_order_seq_cst);
}

void raz_rt_atomic_store_i64(std::int64_t* value, std::int64_t desired) {
  if (value != nullptr) std::atomic_ref<std::int64_t>(*value).store(desired, std::memory_order_seq_cst);
}

std::int64_t raz_rt_atomic_add_i64(std::int64_t* value, std::int64_t delta) {
  return value == nullptr ? 0 : std::atomic_ref<std::int64_t>(*value).fetch_add(delta, std::memory_order_seq_cst);
}

std::int64_t raz_rt_atomic_exchange_i64(std::int64_t* value, std::int64_t desired) {
  return value == nullptr ? 0 : std::atomic_ref<std::int64_t>(*value).exchange(desired, std::memory_order_seq_cst);
}

std::int64_t raz_rt_atomic_compare_exchange_i64(std::int64_t* value, std::int64_t expected, std::int64_t desired) {
  if (value == nullptr) return 0;
  return std::atomic_ref<std::int64_t>(*value).compare_exchange_strong(expected, desired, std::memory_order_seq_cst) ? 1 : 0;
}

namespace {
std::memory_order raz_memory_order(std::int64_t order) {
  switch (order) {
    case 0: return std::memory_order_relaxed;
    case 1: return std::memory_order_acquire;
    case 2: return std::memory_order_release;
    case 3: return std::memory_order_acq_rel;
    default: return std::memory_order_seq_cst;
  }
}
}

std::int64_t raz_rt_atomic_load_i64_ordered(const std::int64_t* value, std::int64_t order) {
  if (value == nullptr) return 0;
  auto memory_order = raz_memory_order(order);
  if (memory_order == std::memory_order_release || memory_order == std::memory_order_acq_rel) memory_order = std::memory_order_acquire;
  return std::atomic_ref<const std::int64_t>(*value).load(memory_order);
}

void raz_rt_atomic_store_i64_ordered(std::int64_t* value, std::int64_t desired, std::int64_t order) {
  if (value == nullptr) return;
  auto memory_order = raz_memory_order(order);
  if (memory_order == std::memory_order_acquire || memory_order == std::memory_order_acq_rel) memory_order = std::memory_order_release;
  std::atomic_ref<std::int64_t>(*value).store(desired, memory_order);
}

std::int64_t raz_rt_atomic_add_i64_ordered(std::int64_t* value, std::int64_t delta, std::int64_t order) {
  return value == nullptr ? 0 : std::atomic_ref<std::int64_t>(*value).fetch_add(delta, raz_memory_order(order));
}

void raz_rt_memory_fence_ordered(std::int64_t order) {
  std::atomic_thread_fence(raz_memory_order(order));
}

std::int64_t raz_rt_load_u8(std::uintptr_t address) {
  if (address == 0) return -1;
  return static_cast<std::int64_t>(*reinterpret_cast<const std::uint8_t*>(address));
}

std::int64_t raz_rt_store_u8(std::uintptr_t address, std::int64_t value) {
  if (address == 0) return 0;
  *reinterpret_cast<std::uint8_t*>(address) = static_cast<std::uint8_t>(value & 0xff);
  return 1;
}

std::int64_t raz_rt_bytes_equal(std::uintptr_t left, std::uintptr_t right, std::int64_t size) {
  if (size < 0 || (size > 0 && (left == 0 || right == 0))) return 0;
  if (size == 0) return 1;
  return std::memcmp(reinterpret_cast<const void*>(left), reinterpret_cast<const void*>(right),
                     static_cast<std::size_t>(size)) == 0 ? 1 : 0;
}

std::int64_t raz_rt_bytes_compare(std::uintptr_t left, std::uintptr_t right, std::int64_t size) {
  if (size < 0 || (size > 0 && (left == 0 || right == 0))) return 0;
  if (size == 0) return 0;
  const int result = std::memcmp(reinterpret_cast<const void*>(left), reinterpret_cast<const void*>(right),
                                 static_cast<std::size_t>(size));
  return result < 0 ? -1 : (result > 0 ? 1 : 0);
}

std::int64_t raz_rt_bytes_find(std::uintptr_t data, std::int64_t size, std::int64_t value) {
  if (size < 0 || (size > 0 && data == 0)) return -1;
  if (size == 0) return -1;
  const auto* base = reinterpret_cast<const std::uint8_t*>(data);
  const void* found = std::memchr(base, static_cast<unsigned char>(value & 0xff), static_cast<std::size_t>(size));
  if (found == nullptr) return -1;
  return static_cast<std::int64_t>(static_cast<const std::uint8_t*>(found) - base);
}

std::int64_t raz_rt_bytes_rfind(std::uintptr_t data, std::int64_t size, std::int64_t value) {
  if (size < 0 || (size > 0 && data == 0)) return -1;
  const auto needle = static_cast<std::uint8_t>(value & 0xff);
  const auto* base = reinterpret_cast<const std::uint8_t*>(data);
  // Reverse scans are intentionally native so compilers can vectorize/unroll
  // them and Raz callers avoid one ABI call per byte.
  for (std::int64_t index = size; index > 0; --index) {
    if (base[index - 1] == needle) return index - 1;
  }
  return -1;
}

void* raz_rt_mutex_create() { return new (std::nothrow) std::mutex(); }
void raz_rt_mutex_destroy(void* mutex) { delete static_cast<std::mutex*>(mutex); }
void raz_rt_mutex_lock(void* mutex) { if (mutex != nullptr) static_cast<std::mutex*>(mutex)->lock(); }
std::int64_t raz_rt_mutex_try_lock(void* mutex) { return mutex != nullptr && static_cast<std::mutex*>(mutex)->try_lock() ? 1 : 0; }
void raz_rt_mutex_unlock(void* mutex) { if (mutex != nullptr) static_cast<std::mutex*>(mutex)->unlock(); }

void* raz_rt_condition_create() { return new (std::nothrow) RazCondition(); }
void raz_rt_condition_destroy(void* condition) { delete static_cast<RazCondition*>(condition); }
void raz_rt_condition_notify_one(void* condition) { if (condition != nullptr) static_cast<RazCondition*>(condition)->condition.notify_one(); }
void raz_rt_condition_notify_all(void* condition) { if (condition != nullptr) static_cast<RazCondition*>(condition)->condition.notify_all(); }
void raz_rt_condition_wait(void* condition, void* mutex) {
  if (condition == nullptr || mutex == nullptr) return;
  std::unique_lock<std::mutex> lock(*static_cast<std::mutex*>(mutex), std::adopt_lock);
  static_cast<RazCondition*>(condition)->condition.wait(lock);
  lock.release();
}

std::int64_t raz_rt_condition_wait_millis(void* condition, void* mutex, std::int64_t timeout_millis) {
  if (condition == nullptr || mutex == nullptr || timeout_millis < 0) return 0;
  std::unique_lock<std::mutex> lock(*static_cast<std::mutex*>(mutex), std::adopt_lock);
  const auto status = static_cast<RazCondition*>(condition)->condition.wait_for(lock, std::chrono::milliseconds(timeout_millis));
  lock.release();
  return status == std::cv_status::no_timeout ? 1 : 0;
}

void* raz_rt_rwlock_create() { return new (std::nothrow) RazRwLock(); }
void raz_rt_rwlock_destroy(void* lock) { delete static_cast<RazRwLock*>(lock); }
void raz_rt_rwlock_read_lock(void* lock) { if (lock != nullptr) static_cast<RazRwLock*>(lock)->mutex.lock_shared(); }
std::int64_t raz_rt_rwlock_try_read(void* lock) { return lock != nullptr && static_cast<RazRwLock*>(lock)->mutex.try_lock_shared() ? 1 : 0; }
void raz_rt_rwlock_read_unlock(void* lock) { if (lock != nullptr) static_cast<RazRwLock*>(lock)->mutex.unlock_shared(); }
void raz_rt_rwlock_write_lock(void* lock) { if (lock != nullptr) static_cast<RazRwLock*>(lock)->mutex.lock(); }
std::int64_t raz_rt_rwlock_try_write(void* lock) { return lock != nullptr && static_cast<RazRwLock*>(lock)->mutex.try_lock() ? 1 : 0; }
void raz_rt_rwlock_write_unlock(void* lock) { if (lock != nullptr) static_cast<RazRwLock*>(lock)->mutex.unlock(); }


}
