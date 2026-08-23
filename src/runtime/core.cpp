// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "runtime_internal.hpp"

using namespace raz::runtime_detail;

namespace {
constexpr std::array<std::uint32_t, 256> raz_make_crc32_ieee_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t index = 0; index < 256; ++index) {
    std::uint32_t value = index;
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = 0U - (value & 1U);
      value = (value >> 1U) ^ (UINT32_C(0xedb88320) & mask);
    }
    table[index] = value;
  }
  return table;
}

constexpr auto raz_crc32_ieee_table = raz_make_crc32_ieee_table();

// Stage-1 aggregate storage for the LLVM backend. Aggregates lower to a handle
// into a flat array of i64 cells, and the backend inttoptr's that handle and
// dereferences it directly ("the arena handle is the payload address" --
// compiler/src/backend/llvm/codegen.rz), so the handle must BE the address of
// the cells. The 24-byte header therefore sits behind the handle and matches
// the compiler's own arena ABI: magic at -24, cell count at -16.
constexpr std::int64_t raz_stage1_arena_magic = 4923358263036431937LL;
constexpr std::int64_t raz_stage1_arena_header = 24;

std::int64_t* raz_stage1_arena_cells(std::int64_t handle) {
  if (handle == 0) return nullptr;
  auto* cells = reinterpret_cast<std::int64_t*>(static_cast<std::uintptr_t>(handle));
  const auto* header = reinterpret_cast<const std::int64_t*>(
      reinterpret_cast<const char*>(cells) - raz_stage1_arena_header);
  return header[0] == raz_stage1_arena_magic ? cells : nullptr;
}

std::int64_t raz_stage1_arena_count(std::int64_t handle) {
  if (handle == 0) return 0;
  const auto* header = reinterpret_cast<const std::int64_t*>(
      reinterpret_cast<const char*>(static_cast<std::uintptr_t>(handle)) - raz_stage1_arena_header);
  return header[0] == raz_stage1_arena_magic ? header[1] : 0;
}

std::int64_t raz_stage1_arena_element_bytes(std::int64_t handle) {
  if (handle == 0) return 0;
  const auto* header = reinterpret_cast<const std::int64_t*>(
      reinterpret_cast<const char*>(static_cast<std::uintptr_t>(handle)) - raz_stage1_arena_header);
  if (header[0] != raz_stage1_arena_magic) return 0;
  const auto width = header[2];
  return width == 1 || width == 2 || width == 4 || width == 8 ? width : 8;
}
}

extern "C" {

void raz_rt_abort() {
  std::abort();
}

std::int64_t raz_rt_cstr_len(const char* value) {
  if (value == nullptr) return 0;
  return static_cast<std::int64_t>(std::strlen(value));
}

std::uintptr_t raz_rt_cstr_ptr(const char* value) {
  return reinterpret_cast<std::uintptr_t>(value);
}

std::int64_t raz_rt_cstr_equal(const char* left, std::int64_t left_length, const char* right) {
  if (left_length < 0 || right == nullptr) return 0;
  const auto right_length = static_cast<std::int64_t>(std::strlen(right));
  if (left_length != right_length) return 0;
  if (left_length == 0) return 1;
  return left != nullptr && std::memcmp(left, right, static_cast<std::size_t>(left_length)) == 0 ? 1 : 0;
}

std::int64_t raz_rt_cstr_find(const char* data, std::int64_t length, const char* needle) {
  if (length < 0 || (length > 0 && data == nullptr) || needle == nullptr) return -1;
  const auto needle_length = static_cast<std::int64_t>(std::strlen(needle));
  if (needle_length == 0) return 0;
  if (needle_length > length) return -1;
  for (std::int64_t i = 0; i <= length - needle_length; ++i) {
    if (std::memcmp(data + i, needle, static_cast<std::size_t>(needle_length)) == 0) return i;
  }
  return -1;
}

std::int64_t raz_rt_cstr_rfind(const char* data, std::int64_t length, const char* needle) {
  if (length < 0 || (length > 0 && data == nullptr) || needle == nullptr) return -1;
  const auto needle_length = static_cast<std::int64_t>(std::strlen(needle));
  if (needle_length == 0) return length;
  if (needle_length > length) return -1;
  for (std::int64_t i = length - needle_length; i >= 0; --i) {
    if (std::memcmp(data + i, needle, static_cast<std::size_t>(needle_length)) == 0) return i;
  }
  return -1;
}

std::int64_t raz_rt_f64_format(double value, void* output, std::int64_t capacity) {
  if (output == nullptr || capacity <= 0) return -1;
  auto* begin = static_cast<char*>(output);
  auto* end = begin + capacity;
  const auto result = std::to_chars(begin, end, value, std::chars_format::general);
  if (result.ec != std::errc{}) return -1;
  return static_cast<std::int64_t>(result.ptr - begin);
}
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

std::int64_t raz_rt_page_size() {
#if defined(_WIN32)
  SYSTEM_INFO info{};
  GetSystemInfo(&info);
  return static_cast<std::int64_t>(info.dwPageSize);
#else
  const long value = ::sysconf(_SC_PAGESIZE);
  return value > 0 ? static_cast<std::int64_t>(value) : 4096;
#endif
}

std::int64_t raz_rt_current_thread_set_affinity(std::int64_t cpu) {
  if (cpu < 0) return 0;
#if defined(_WIN32)
  constexpr auto bits = static_cast<std::int64_t>(sizeof(DWORD_PTR) * CHAR_BIT);
  if (cpu >= bits) return 0;
  const DWORD_PTR mask = static_cast<DWORD_PTR>(1) << static_cast<unsigned>(cpu);
  return SetThreadAffinityMask(GetCurrentThread(), mask) != 0 ? 1 : 0;
#elif defined(__linux__)
  if (cpu >= CPU_SETSIZE) return 0;
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(static_cast<int>(cpu), &set);
  return ::sched_setaffinity(0, sizeof(set), &set) == 0 ? 1 : 0;
#else
  (void)cpu;
  return 0;
#endif
}

void* raz_rt_page_alloc(std::int64_t size) {
  if (size <= 0) return nullptr;
#if defined(_WIN32)
  return VirtualAlloc(nullptr, static_cast<SIZE_T>(size), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
  void* result = ::mmap(nullptr, static_cast<std::size_t>(size), PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return result == MAP_FAILED ? nullptr : result;
#endif
}

std::int64_t raz_rt_page_free(void* address, std::int64_t size) {
  if (address == nullptr || size <= 0) return 0;
#if defined(_WIN32)
  return VirtualFree(address, 0, MEM_RELEASE) != 0 ? 1 : 0;
#else
  return ::munmap(address, static_cast<std::size_t>(size)) == 0 ? 1 : 0;
#endif
}

std::int64_t raz_rt_page_protect(void* address, std::int64_t size, std::int64_t protection) {
  if (address == nullptr || size <= 0 || protection < 0 || protection > 4) return 0;
#if defined(_WIN32)
  DWORD mode = PAGE_NOACCESS;
  if (protection == 1) mode = PAGE_READONLY;
  else if (protection == 2) mode = PAGE_READWRITE;
  else if (protection == 3) mode = PAGE_EXECUTE_READ;
  else if (protection == 4) mode = PAGE_EXECUTE_READWRITE;
  DWORD previous = 0;
  return VirtualProtect(address, static_cast<SIZE_T>(size), mode, &previous) != 0 ? 1 : 0;
#else
  int mode = PROT_NONE;
  if (protection == 1) mode = PROT_READ;
  else if (protection == 2) mode = PROT_READ | PROT_WRITE;
  else if (protection == 3) mode = PROT_READ | PROT_EXEC;
  else if (protection == 4) mode = PROT_READ | PROT_WRITE | PROT_EXEC;
  return ::mprotect(address, static_cast<std::size_t>(size), mode) == 0 ? 1 : 0;
#endif
}

std::int64_t raz_rt_page_lock(void* address, std::int64_t size) {
  if (address == nullptr || size <= 0) return 0;
#if defined(_WIN32)
  return VirtualLock(address, static_cast<SIZE_T>(size)) != 0 ? 1 : 0;
#else
  return ::mlock(address, static_cast<std::size_t>(size)) == 0 ? 1 : 0;
#endif
}

std::int64_t raz_rt_page_unlock(void* address, std::int64_t size) {
  if (address == nullptr || size <= 0) return 0;
#if defined(_WIN32)
  return VirtualUnlock(address, static_cast<SIZE_T>(size)) != 0 ? 1 : 0;
#else
  return ::munlock(address, static_cast<std::size_t>(size)) == 0 ? 1 : 0;
#endif
}

double raz_rt_f64_floor(double value) { return std::floor(value); }
double raz_rt_f64_ceil(double value) { return std::ceil(value); }
double raz_rt_f64_round(double value) { return std::round(value); }
double raz_rt_f64_trunc(double value) { return std::trunc(value); }
double raz_rt_f64_sqrt(double value) { return std::sqrt(value); }
double raz_rt_f64_cbrt(double value) { return std::cbrt(value); }
double raz_rt_f64_pow(double base, double exponent) { return std::pow(base, exponent); }
double raz_rt_f64_exp(double value) { return std::exp(value); }
double raz_rt_f64_log(double value) { return std::log(value); }
double raz_rt_f64_log2(double value) { return std::log2(value); }
double raz_rt_f64_log10(double value) { return std::log10(value); }
double raz_rt_f64_sin(double value) { return std::sin(value); }
double raz_rt_f64_cos(double value) { return std::cos(value); }
double raz_rt_f64_tan(double value) { return std::tan(value); }
double raz_rt_f64_asin(double value) { return std::asin(value); }
double raz_rt_f64_acos(double value) { return std::acos(value); }
double raz_rt_f64_atan(double value) { return std::atan(value); }
double raz_rt_f64_atan2(double y, double x) { return std::atan2(y, x); }
std::int64_t raz_rt_f64_is_finite(double value) { return std::isfinite(value) ? 1 : 0; }
std::int64_t raz_rt_f64_is_nan(double value) { return std::isnan(value) ? 1 : 0; }
std::int64_t raz_rt_f64_is_infinite(double value) { return std::isinf(value) ? 1 : 0; }

void* raz_rt_alloc(std::int64_t size) { return size <= 0 ? nullptr : std::malloc(static_cast<std::size_t>(size)); }
void* raz_rt_alloc_aligned(std::int64_t size, std::int64_t alignment) {
  if (size <= 0 || alignment <= 0 || alignment > 4096 ||
      (alignment & (alignment - 1)) != 0) return nullptr;
  const auto normalized = static_cast<std::size_t>(std::max<std::int64_t>(alignment, alignof(std::max_align_t)));
  // malloc already satisfies max_align_t. Keeping ordinary alignments on the
  // malloc/realloc path lets Raz vectors grow in place when the host allocator
  // can extend the allocation, which avoids an allocate/copy/free cycle on one
  // of the hottest standard-library paths.
  if (normalized <= alignof(std::max_align_t)) {
    return std::malloc(static_cast<std::size_t>(size));
  }
  return ::operator new(static_cast<std::size_t>(size), std::align_val_t(normalized), std::nothrow);
}

void raz_rt_dealloc_aligned(void* pointer, std::int64_t alignment) {
  if (pointer == nullptr) return;
  if (alignment <= 0 || alignment > 4096 || (alignment & (alignment - 1)) != 0) return;
  const auto normalized = static_cast<std::size_t>(std::max<std::int64_t>(alignment, alignof(std::max_align_t)));
  if (normalized <= alignof(std::max_align_t)) {
    std::free(pointer);
    return;
  }
  ::operator delete(pointer, std::align_val_t(normalized));
}

#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
static void* raz_realloc_overaligned(void* pointer, std::int64_t old_size, std::int64_t new_size,
                                     std::size_t alignment) {
  void* replacement = ::operator new(static_cast<std::size_t>(new_size), std::align_val_t(alignment), std::nothrow);
  if (replacement == nullptr) return nullptr;
  if (old_size > 0) {
    const auto copy_size = static_cast<std::size_t>(std::min(old_size, new_size));
    std::memcpy(replacement, pointer, copy_size);
  }
  ::operator delete(pointer, std::align_val_t(alignment));
  return replacement;
}

void* raz_rt_realloc_aligned(void* pointer, std::int64_t old_size, std::int64_t new_size,
                             std::int64_t alignment) {
  if (old_size < 0 || alignment <= 0 || alignment > 4096 ||
      (alignment & (alignment - 1)) != 0) return nullptr;
  if (new_size <= 0) {
    raz_rt_dealloc_aligned(pointer, alignment);
    return nullptr;
  }

  const auto normalized = static_cast<std::size_t>(std::max<std::int64_t>(alignment, alignof(std::max_align_t)));
  if (pointer == nullptr) return raz_rt_alloc_aligned(new_size, alignment);
  if (normalized <= alignof(std::max_align_t)) {
    return std::realloc(pointer, static_cast<std::size_t>(new_size));
  }

  return raz_realloc_overaligned(pointer, old_size, new_size, normalized);
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

std::int64_t raz_rt_atomic_exchange_i64_ordered(std::int64_t* value, std::int64_t desired, std::int64_t order) {
  return value == nullptr ? 0 : std::atomic_ref<std::int64_t>(*value).exchange(desired, raz_memory_order(order));
}

std::int64_t raz_rt_atomic_compare_exchange_i64_ordered(std::int64_t* value, std::int64_t expected,
                                                         std::int64_t desired, std::int64_t success_order,
                                                         std::int64_t failure_order) {
  if (value == nullptr) return 0;
  auto success = raz_memory_order(success_order);
  auto failure = raz_memory_order(failure_order);
  // C++ does not permit release/acq_rel on the failure edge, and the failure
  // order may not be stronger than the success edge. Clamp invalid requests
  // instead of forcing all lock-free Raz code back to seq_cst.
  if (success == std::memory_order_relaxed || success == std::memory_order_release) {
    failure = std::memory_order_relaxed;
  } else if (success == std::memory_order_acquire || success == std::memory_order_acq_rel) {
    if (failure != std::memory_order_relaxed && failure != std::memory_order_acquire) {
      failure = std::memory_order_acquire;
    }
  } else {
    if (failure == std::memory_order_release) failure = std::memory_order_relaxed;
    if (failure == std::memory_order_acq_rel) failure = std::memory_order_acquire;
  }
  return std::atomic_ref<std::int64_t>(*value).compare_exchange_strong(expected, desired, success, failure) ? 1 : 0;
}

void raz_rt_memory_fence_ordered(std::int64_t order) {
  std::atomic_thread_fence(raz_memory_order(order));
}

void raz_rt_cpu_relax() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  YieldProcessor();
#elif (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
  __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
  __asm__ __volatile__("yield" ::: "memory");
#else
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
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

std::int64_t raz_rt_load_scalar(std::uintptr_t address, std::int64_t bytes, std::int64_t signed_value) {
  if (address == 0) return 0;
  if (bytes == 1) {
    std::uint8_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return signed_value != 0 ? static_cast<std::int64_t>(static_cast<std::int8_t>(value))
                             : static_cast<std::int64_t>(value);
  }
  if (bytes == 2) {
    std::uint16_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return signed_value != 0 ? static_cast<std::int64_t>(static_cast<std::int16_t>(value))
                             : static_cast<std::int64_t>(value);
  }
  if (bytes == 4) {
    std::uint32_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return signed_value != 0 ? static_cast<std::int64_t>(static_cast<std::int32_t>(value))
                             : static_cast<std::int64_t>(value);
  }
  if (bytes == 8) {
    std::int64_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
  }
  return 0;
}

std::int64_t raz_rt_store_scalar(std::uintptr_t address, std::int64_t bytes, std::int64_t value) {
  if (address == 0) return 0;
  if (bytes == 1) {
    const auto narrowed = static_cast<std::uint8_t>(value);
    std::memcpy(reinterpret_cast<void*>(address), &narrowed, sizeof(narrowed));
    return 1;
  }
  if (bytes == 2) {
    const auto narrowed = static_cast<std::uint16_t>(value);
    std::memcpy(reinterpret_cast<void*>(address), &narrowed, sizeof(narrowed));
    return 1;
  }
  if (bytes == 4) {
    const auto narrowed = static_cast<std::uint32_t>(value);
    std::memcpy(reinterpret_cast<void*>(address), &narrowed, sizeof(narrowed));
    return 1;
  }
  if (bytes == 8) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
    return 1;
  }
  return 0;
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

std::uint32_t raz_rt_crc32_ieee_update(std::uint32_t state, std::uintptr_t data, std::int64_t size) {
  if (size <= 0 || data == 0) return state;
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(data);
  std::int64_t index = 0;

  // One native crossing updates the complete buffer. The eight-way unroll
  // gives the compiler enough independent address work to overlap table loads
  // while preserving the exact incremental IEEE CRC-32 state contract.
  while (index + 8 <= size) {
    state = (state >> 8U) ^ raz_crc32_ieee_table[(state ^ bytes[index + 0]) & 0xffU];
    state = (state >> 8U) ^ raz_crc32_ieee_table[(state ^ bytes[index + 1]) & 0xffU];
    state = (state >> 8U) ^ raz_crc32_ieee_table[(state ^ bytes[index + 2]) & 0xffU];
    state = (state >> 8U) ^ raz_crc32_ieee_table[(state ^ bytes[index + 3]) & 0xffU];
    state = (state >> 8U) ^ raz_crc32_ieee_table[(state ^ bytes[index + 4]) & 0xffU];
    state = (state >> 8U) ^ raz_crc32_ieee_table[(state ^ bytes[index + 5]) & 0xffU];
    state = (state >> 8U) ^ raz_crc32_ieee_table[(state ^ bytes[index + 6]) & 0xffU];
    state = (state >> 8U) ^ raz_crc32_ieee_table[(state ^ bytes[index + 7]) & 0xffU];
    index += 8;
  }
  while (index < size) {
    state = (state >> 8U) ^ raz_crc32_ieee_table[(state ^ bytes[index]) & 0xffU];
    ++index;
  }
  return state;
}

std::int64_t raz_rt_hash_bytes(std::uintptr_t data, std::int64_t size) {
  if (size < 0 || (size > 0 && data == 0)) return 0;

  const auto avalanche = [](std::uint64_t value) noexcept {
    value ^= value >> 30U;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27U;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31U;
    return value;
  };

  const auto* bytes = reinterpret_cast<const std::uint8_t*>(data);
  std::uint64_t hash = avalanche(UINT64_C(0xa0761d6478bd642f) ^ static_cast<std::uint64_t>(size));
  std::int64_t offset = 0;

  while (offset + 8 <= size) {
    std::uint64_t word = 0;
    std::memcpy(&word, bytes + offset, sizeof(word));
    hash ^= avalanche(word + UINT64_C(0xe7037ed1a0b428db) + static_cast<std::uint64_t>(offset));
    hash = std::rotl(hash, 27) * UINT64_C(0x9e3779b185ebca87) + UINT64_C(0x165667b19e3779f9);
    offset += 8;
  }

  if (offset < size) {
    std::uint64_t tail = 0;
    std::memcpy(&tail, bytes + offset, static_cast<std::size_t>(size - offset));
    hash ^= avalanche(tail ^ (static_cast<std::uint64_t>(size - offset) << 56U));
  }
  hash = avalanche(hash ^ static_cast<std::uint64_t>(size));
  return static_cast<std::int64_t>(hash & UINT64_C(0x7fffffffffffffff));
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

std::int64_t raz_rt_stage1_arena_create(std::int64_t count) {
  if (count <= 0 || count > (INT64_MAX - raz_stage1_arena_header) / 8) return 0;
  const std::int64_t bytes = raz_stage1_arena_header + count * 8;
  void* allocation = raz_rt_alloc_aligned(bytes, 8);
  if (allocation == nullptr) return 0;
  std::memset(allocation, 0, static_cast<std::size_t>(bytes));
  auto* header = static_cast<std::int64_t*>(allocation);
  header[0] = raz_stage1_arena_magic;
  header[1] = count;
  header[2] = 8;
  return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(
      static_cast<char*>(allocation) + raz_stage1_arena_header));
}

std::int64_t raz_rt_stage1_array_create(std::int64_t count, std::int64_t element_bytes) {
  if (count <= 0 || (element_bytes != 1 && element_bytes != 2 && element_bytes != 4 && element_bytes != 8)) return 0;
  if (count > (INT64_MAX - raz_stage1_arena_header) / element_bytes) return 0;
  const std::int64_t bytes = raz_stage1_arena_header + count * element_bytes;
  void* allocation = raz_rt_alloc_aligned(bytes, 8);
  if (allocation == nullptr) return 0;
  std::memset(allocation, 0, static_cast<std::size_t>(bytes));
  auto* header = static_cast<std::int64_t*>(allocation);
  header[0] = raz_stage1_arena_magic;
  header[1] = count;
  header[2] = element_bytes;
  return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(
      static_cast<char*>(allocation) + raz_stage1_arena_header));
}

std::int64_t raz_rt_stage1_arena_get(std::int64_t handle, std::int64_t index) {
  if (raz_stage1_arena_cells(handle) == nullptr || index < 0 || index >= raz_stage1_arena_count(handle)) return 0;
  const auto width = raz_stage1_arena_element_bytes(handle);
  const auto* base = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(handle));
  const auto* p = base + static_cast<std::size_t>(index * width);
  if (width == 1) return *p;
  if (width == 2) { std::uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
  if (width == 4) { std::uint32_t v = 0; std::memcpy(&v, p, 4); return v; }
  std::int64_t v = 0; std::memcpy(&v, p, 8); return v;
}

void raz_rt_stage1_arena_set(std::int64_t handle, std::int64_t index, std::int64_t value) {
  if (raz_stage1_arena_cells(handle) == nullptr || index < 0 || index >= raz_stage1_arena_count(handle)) return;
  const auto width = raz_stage1_arena_element_bytes(handle);
  auto* base = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(handle));
  auto* p = base + static_cast<std::size_t>(index * width);
  if (width == 1) { *p = static_cast<std::uint8_t>(value); return; }
  if (width == 2) { const auto v = static_cast<std::uint16_t>(value); std::memcpy(p, &v, 2); return; }
  if (width == 4) { const auto v = static_cast<std::uint32_t>(value); std::memcpy(p, &v, 4); return; }
  std::memcpy(p, &value, 8);
}

void raz_rt_stage1_arena_destroy(std::int64_t handle) {
  if (raz_stage1_arena_cells(handle) == nullptr) return;
  auto* allocation = reinterpret_cast<char*>(static_cast<std::uintptr_t>(handle)) - raz_stage1_arena_header;
  auto* header = reinterpret_cast<std::int64_t*>(allocation);
  header[0] = 0;
  header[1] = 0;
  header[2] = 0;
  raz_rt_dealloc_aligned(allocation, 8);
}

// Legacy textual Forge uses stage-1 frame storage when a function cannot yet
// stay on the structured-native path. References are direct addresses into the
// arena payload, so this compatibility ABI needs no compiler-private reference
// table and no per-reference allocation.
double raz_rt_stage1_arena_get_f64(std::int64_t handle, std::int64_t index) {
  std::int64_t* cells = raz_stage1_arena_cells(handle);
  if (cells == nullptr || index < 0 || index >= raz_stage1_arena_count(handle)) return 0.0;
  double value = 0.0;
  std::memcpy(&value, &cells[index], sizeof(value));
  return value;
}

void raz_rt_stage1_arena_set_f64(std::int64_t handle, std::int64_t index, double value) {
  std::int64_t* cells = raz_stage1_arena_cells(handle);
  if (cells == nullptr || index < 0 || index >= raz_stage1_arena_count(handle)) return;
  std::memcpy(&cells[index], &value, sizeof(value));
}

std::int64_t raz_rt_stage1_ref_create(std::int64_t frame, std::int64_t slot) {
  std::int64_t* cells = raz_stage1_arena_cells(frame);
  if (cells == nullptr || slot < 0 || slot >= raz_stage1_arena_count(frame)) return 0;
  return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(&cells[slot]));
}

std::int64_t raz_rt_stage1_array_ref_create(std::int64_t frame, std::int64_t slot) {
  if (raz_stage1_arena_cells(frame) == nullptr || slot < 0 || slot >= raz_stage1_arena_count(frame)) return 0;
  const auto width = raz_stage1_arena_element_bytes(frame);
  auto* base = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(frame));
  return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(
      base + static_cast<std::size_t>(slot * width)));
}

std::int64_t raz_rt_stage1_ref_create_address(void* address) {
  return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(address));
}

std::int64_t raz_rt_stage1_ref_get(std::int64_t reference) {
  if (reference == 0) return 0;
  return *reinterpret_cast<const std::int64_t*>(static_cast<std::uintptr_t>(reference));
}

void raz_rt_stage1_ref_set(std::int64_t reference, std::int64_t value) {
  if (reference == 0) return;
  *reinterpret_cast<std::int64_t*>(static_cast<std::uintptr_t>(reference)) = value;
}

double raz_rt_stage1_ref_get_f64(std::int64_t reference) {
  if (reference == 0) return 0.0;
  double value = 0.0;
  std::memcpy(&value, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(reference)), sizeof(value));
  return value;
}

void raz_rt_stage1_ref_set_f64(std::int64_t reference, double value) {
  if (reference == 0) return;
  std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(reference)), &value, sizeof(value));
}


}
