// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

extern "C" {
void* raz_rt_async_frame_create(std::int64_t);
std::int32_t raz_rt_async_state_load(const void*);
void raz_rt_async_state_store(void*, std::int32_t);
std::int64_t raz_rt_async_frame_slot_count(const void*);
std::int64_t raz_rt_async_slot_load(const void*, std::int64_t);
void raz_rt_async_slot_store(void*, std::int64_t, std::int64_t);
void raz_rt_async_slot_store_owned(void*, std::int64_t, std::int64_t, std::uintptr_t);
void* raz_rt_async_slot_allocate_owned_bytes(void*, std::int64_t, std::int64_t, std::int64_t, std::uintptr_t);
void* raz_rt_async_slot_allocate_projected_bytes(void*, std::int64_t, std::int64_t, std::int64_t, std::uintptr_t, std::uint64_t, std::int64_t);
std::int64_t raz_rt_async_slot_projection_count(const void*, std::int64_t);
std::uint64_t raz_rt_async_slot_projection_version(const void*, std::int64_t);
void raz_rt_async_slot_projection_set(void*, std::int64_t, std::int64_t, bool);
void raz_rt_async_slot_projection_range_set(void*, std::int64_t, std::int64_t, std::int64_t, bool);
std::int64_t raz_rt_async_slot_projection_word_count(const void*, std::int64_t);
std::uint64_t raz_rt_async_slot_projection_word(const void*, std::int64_t, std::int64_t);
std::int64_t raz_rt_async_slot_projection_copy_words(const void*, std::int64_t, std::uint64_t*, std::int64_t);
std::int64_t raz_rt_async_slot_projection_run_count(const void*, std::int64_t);
std::int64_t raz_rt_async_slot_projection_copy_runs(const void*, std::int64_t, std::int64_t*, std::int64_t*, std::uint8_t*, std::int64_t);
std::int64_t raz_rt_async_slot_projection_batch_set(void*, std::int64_t, const std::int64_t*, const std::int64_t*, const std::uint8_t*, std::int64_t);
std::uint64_t raz_rt_async_slot_projection_mask(const void*, std::int64_t);
void raz_rt_async_slot_store_owned_bytes(void*, std::int64_t, const void*, std::int64_t, std::int64_t, std::uintptr_t);
std::int64_t raz_rt_async_slot_take(void*, std::int64_t);
void* raz_rt_async_slot_transfer_projected(void*, std::int64_t);
std::int32_t raz_rt_async_slot_lifecycle(const void*, std::int64_t);
void raz_rt_async_slot_disarm(void*, std::int64_t);
void* raz_rt_async_slot_address(void*, std::int64_t);
std::int64_t raz_rt_async_result_load(const void*);
void raz_rt_async_result_store(void*, std::int64_t);
std::int64_t raz_rt_async_cancel_requested(const void*);
void raz_rt_async_frame_cancel(void*);
void* raz_rt_async_frame_future(void*);
std::int32_t raz_rt_async_frame_lifecycle(const void*);
std::int32_t raz_rt_async_frame_terminal_intent(const void*);
void raz_rt_async_frame_set_poller(void*, void*);
std::int32_t raz_rt_async_await_poll(void*, void*, std::int32_t);
std::int64_t raz_rt_async_await_result(void*);
std::int32_t raz_rt_async_frame_has_pending_await(const void*);
std::uint64_t raz_rt_async_frame_await_generation(const void*);
void raz_rt_async_frame_destroy(void*);
std::int64_t raz_rt_abi_pointer_size();
std::int64_t raz_rt_abi_pointer_alignment();
std::int64_t raz_rt_abi_bool_size();
std::int64_t raz_rt_abi_bool_alignment();
std::int64_t raz_rt_abi_i8_size();
std::int64_t raz_rt_abi_i8_alignment();
std::int64_t raz_rt_abi_i16_size();
std::int64_t raz_rt_abi_i16_alignment();
std::int64_t raz_rt_abi_i32_size();
std::int64_t raz_rt_abi_i32_alignment();
std::int64_t raz_rt_abi_i64_size();
std::int64_t raz_rt_abi_i64_alignment();
std::int64_t raz_rt_abi_f32_size();
std::int64_t raz_rt_abi_f32_alignment();
std::int64_t raz_rt_abi_f64_size();
std::int64_t raz_rt_abi_f64_alignment();
std::int64_t raz_rt_abi_size_t_size();
std::int64_t raz_rt_abi_size_t_alignment();
std::int64_t raz_rt_abi_little_endian();
std::int64_t raz_rt_time_unix_millis();
std::int64_t raz_rt_time_monotonic_nanos();
std::int64_t raz_rt_hardware_threads();
void* raz_rt_alloc(std::int64_t);
void* raz_rt_realloc(void*, std::int64_t);
void raz_rt_dealloc(void*);
void raz_rt_fill(void*, std::int64_t, std::int64_t);
std::int64_t raz_rt_atomic_add_i64(std::int64_t*, std::int64_t);
std::int64_t raz_rt_atomic_compare_exchange_i64(std::int64_t*, std::int64_t, std::int64_t);
std::int64_t raz_rt_atomic_load_i64_ordered(const std::int64_t*, std::int64_t);
void raz_rt_atomic_store_i64_ordered(std::int64_t*, std::int64_t, std::int64_t);
std::int64_t raz_rt_atomic_add_i64_ordered(std::int64_t*, std::int64_t, std::int64_t);
void raz_rt_memory_fence_ordered(std::int64_t);
void* raz_rt_mutex_create();
void raz_rt_mutex_lock(void*);
void raz_rt_mutex_unlock(void*);
void raz_rt_mutex_destroy(void*);
void* raz_rt_condition_create();
void raz_rt_condition_destroy(void*);
void raz_rt_condition_notify_one(void*);
void raz_rt_condition_notify_all(void*);
void raz_rt_condition_wait(void*, void*);
std::int64_t raz_rt_condition_wait_millis(void*, void*, std::int64_t);
void* raz_rt_rwlock_create();
void raz_rt_rwlock_destroy(void*);
void raz_rt_rwlock_read_lock(void*);
std::int64_t raz_rt_rwlock_try_read(void*);
void raz_rt_rwlock_read_unlock(void*);
void raz_rt_rwlock_write_lock(void*);
std::int64_t raz_rt_rwlock_try_write(void*);
void raz_rt_rwlock_write_unlock(void*);
std::int64_t raz_rt_current_dir(char*, std::int64_t);
std::int64_t raz_rt_create_dir_one(const char*, std::int64_t);
std::int64_t raz_rt_remove_one(const char*, std::int64_t);
std::int64_t raz_rt_path_exists(const char*, std::int64_t);
std::int64_t raz_rt_file_size(const char*, std::int64_t);
std::int64_t raz_rt_dns_resolve_ipv4(const char*, std::int64_t, char*, std::int64_t);
struct RazResolvedAddressRecordForTest {
  std::int64_t family;
  std::uint64_t high;
  std::uint64_t low;
  std::int64_t scope_id;
};
std::int64_t raz_rt_dns_resolve_all(const char*, std::int64_t, RazResolvedAddressRecordForTest*, std::int64_t);
std::int64_t raz_rt_socket_family(std::int64_t);
std::int64_t raz_rt_tcp_listen_host(const char*, std::int64_t, std::int64_t, std::int64_t);
std::int64_t raz_rt_udp_bind_host(const char*, std::int64_t, std::int64_t);
std::int64_t raz_rt_socket_local_port(std::int64_t);
std::int64_t raz_rt_tcp_listen(std::int64_t, std::int64_t);
std::int64_t raz_rt_socket_send(std::int64_t, const void*, std::int64_t);
std::int64_t raz_rt_socket_receive(std::int64_t, void*, std::int64_t);
void raz_rt_socket_close(std::int64_t);
std::int64_t raz_rt_volatile_load_i64(std::uintptr_t);
void raz_rt_volatile_store_i64(std::uintptr_t, std::int64_t);
void raz_rt_memory_fence();
std::int64_t raz_rt_cpu_has_sse2();
std::int64_t raz_rt_cpu_has_avx2();
std::int64_t raz_rt_cpu_has_neon();
std::int64_t raz_rt_cache_line_size();
void* raz_rt_task_spawn_i64(std::uintptr_t, void*);
std::int64_t raz_rt_task_is_ready(void*);
std::int64_t raz_rt_task_wait_millis(void*, std::int64_t);
std::int64_t raz_rt_task_join_i64(void*);
void raz_rt_yield_now();
void* raz_rt_future_create();
std::int64_t raz_rt_future_complete_i64(void*, std::int64_t);
std::int64_t raz_rt_future_cancel(void*);
std::int64_t raz_rt_future_status(void*);
std::int64_t raz_rt_future_wait_millis(void*, std::int64_t);
std::int64_t raz_rt_future_result_i64(void*, std::int64_t*);
std::int64_t raz_rt_future_then_i64(void*, std::uintptr_t, void*);
std::int64_t raz_rt_future_continuation_count(const void*);
void raz_rt_future_destroy(void*);

std::int64_t raz_rt_udp_bind(std::int64_t);
std::int64_t raz_rt_udp_connect(const char*, std::int64_t, std::int64_t);
std::int64_t raz_rt_socket_local_port(std::int64_t);
std::int64_t raz_rt_socket_peer_port(std::int64_t);
std::int64_t raz_rt_socket_local_address(std::int64_t, char*, std::int64_t);
std::int64_t raz_rt_socket_peer_address(std::int64_t, char*, std::int64_t);
std::int64_t raz_rt_socket_set_nodelay(std::int64_t, std::int64_t);
std::int64_t raz_rt_socket_set_keepalive(std::int64_t, std::int64_t);
std::int64_t raz_rt_socket_set_reuse_address(std::int64_t, std::int64_t);
std::int64_t raz_rt_socket_set_buffer_sizes(std::int64_t, std::int64_t, std::int64_t);
std::int64_t raz_rt_socket_set_timeout_millis(std::int64_t, std::int64_t, std::int64_t);
std::int64_t raz_rt_socket_set_nonblocking(std::int64_t, std::int64_t);
std::int64_t raz_rt_socket_poll(std::int64_t, std::int64_t, std::int64_t);
std::int64_t raz_rt_socket_shutdown(std::int64_t, std::int64_t);
std::int64_t raz_rt_socket_send(std::int64_t, const void*, std::int64_t);
std::int64_t raz_rt_socket_receive(std::int64_t, void*, std::int64_t);
void raz_rt_socket_close(std::int64_t);

void raz_rt_i64x4_add(const std::int64_t*, const std::int64_t*, std::int64_t*);
void raz_rt_i64x4_sub(const std::int64_t*, const std::int64_t*, std::int64_t*);
void raz_rt_i64x4_mul(const std::int64_t*, const std::int64_t*, std::int64_t*);
void raz_rt_i64x4_min(const std::int64_t*, const std::int64_t*, std::int64_t*);
void raz_rt_i64x4_max(const std::int64_t*, const std::int64_t*, std::int64_t*);
void raz_rt_i64x4_equal(const std::int64_t*, const std::int64_t*, std::int64_t*);
std::int64_t raz_rt_i64x4_reduce_add(const std::int64_t*);
void raz_rt_f64x2_add(const double*, const double*, double*);
void raz_rt_f64x2_sub(const double*, const double*, double*);
void raz_rt_f64x2_mul(const double*, const double*, double*);
void raz_rt_f64x2_div(const double*, const double*, double*);
void raz_rt_f64x2_min(const double*, const double*, double*);
void raz_rt_f64x2_max(const double*, const double*, double*);
double raz_rt_f64x2_reduce_add(const double*);
void* raz_rt_callable_weak_create(void*);
void* raz_rt_callable_weak_upgrade(void*);
std::int32_t raz_rt_callable_weak_expired(const void*);
void raz_rt_callable_weak_destroy(void*);
void* raz_rt_trait_object_weak_create(void*);
void* raz_rt_trait_object_weak_upgrade(void*);
std::int32_t raz_rt_trait_object_weak_expired(const void*);
void raz_rt_trait_object_weak_destroy(void*);
std::uint64_t raz_rt_trait_object_trait_id(const void*);
std::int32_t raz_rt_trait_object_is_trait(const void*, std::uint64_t);
std::uint64_t raz_rt_trait_object_type_id(const void*);
void* raz_rt_trait_object_downcast_data(void*, std::uint64_t);
void* raz_rt_callable_create_erased(void*, void*, void*, void*, std::int32_t, std::uint64_t, std::uint64_t, std::uint64_t);
std::int32_t raz_rt_callable_invoke_erased(void*, std::uint64_t, const void*, std::uint64_t, void*, std::uint64_t);
void* raz_rt_callable_clone_erased(void*);
void raz_rt_callable_destroy_erased(void*);
std::uint64_t raz_rt_callable_signature_id(const void*);
std::uint64_t raz_rt_callable_argument_size(const void*);
std::uint64_t raz_rt_callable_result_size(const void*);
struct RazErasedTraitMethodTest { void* invoke; std::uint64_t signature_id; std::uint64_t argument_size; std::uint64_t result_size; };
void* raz_rt_trait_object_create_erased(void*, std::uint64_t, std::uint64_t, void*, const void*, std::uint64_t);
std::int32_t raz_rt_trait_object_invoke_erased(void*, std::uint64_t, std::uint64_t, const void*, std::uint64_t, void*, std::uint64_t);
void* raz_rt_trait_object_clone_erased(void*);
void raz_rt_trait_object_destroy_erased(void*);
std::uint64_t raz_rt_trait_object_method_signature_id(const void*, std::uint64_t);
}

namespace {
int failures = 0;
struct WidePair { std::int64_t left; std::int64_t right; };
struct PairScaleArgs { WidePair value; std::int64_t factor; };
struct PairEnvironment { WidePair bias; };
std::int32_t erased_pair_callable(void* environment, const void* args, std::uint64_t arg_size, void* result, std::uint64_t result_size) {
  if (environment == nullptr || args == nullptr || result == nullptr || arg_size != sizeof(WidePair) || result_size != sizeof(WidePair)) return -1;
  const auto& bias = *static_cast<const PairEnvironment*>(environment);
  const auto& value = *static_cast<const WidePair*>(args);
  *static_cast<WidePair*>(result) = {value.left + bias.bias.left, value.right + bias.bias.right};
  return 0;
}

std::int32_t erased_pair_scale(void* data, const void* args, std::uint64_t arg_size, void* result, std::uint64_t result_size) {
  if (data == nullptr || args == nullptr || result == nullptr || arg_size != sizeof(PairScaleArgs) || result_size != sizeof(WidePair)) return -1;
  const auto& base = *static_cast<const WidePair*>(data);
  const auto& input = *static_cast<const PairScaleArgs*>(args);
  *static_cast<WidePair*>(result) = {(base.left + input.value.left) * input.factor, (base.right + input.value.right) * input.factor};
  return 0;
}

std::int64_t task_entry(void* context) {
  const auto value = context == nullptr ? 0 : *static_cast<std::int64_t*>(context);
  return value + 2;
}

struct FutureContinuationState {
  std::atomic<std::int64_t> calls{0};
  std::atomic<std::int64_t> value{0};
  std::atomic<std::int64_t> status{0};
};
void future_continuation(void* context, std::int64_t value, std::int64_t status) {
  auto* state = static_cast<FutureContinuationState*>(context);
  state->value.store(value);
  state->status.store(status);
  state->calls.fetch_add(1);
}
std::atomic<std::int64_t> async_repoll_count{0};
std::atomic<bool> async_blocking_poller_entered{false};
std::atomic<bool> async_blocking_poller_release{false};
std::atomic<bool> async_cancel_returned{false};
std::atomic<std::int64_t> async_reentrant_poll_calls{0};
std::atomic<std::int64_t> async_reentrant_poll_depth{0};
std::atomic<std::int64_t> async_reentrant_poll_max_depth{0};
std::atomic<std::int32_t> async_reentrant_register_status{-99};
std::atomic<std::int64_t> async_reentrant_complete_status{-99};
void* async_reentrant_future = nullptr;
std::atomic<std::int64_t> async_fair_poll_calls{0};
constexpr std::int64_t async_fair_poll_limit = 10000;
std::atomic<std::int64_t> async_terminal_destroy_callbacks{0};
std::atomic<std::int64_t> async_terminal_destroy_status{0};
std::atomic<std::int64_t> async_terminal_destroy_cleanups{0};
std::vector<std::int64_t> async_cleanup_order;
void async_owned_slot_cleanup(std::int64_t value) { async_cleanup_order.push_back(value); }
void async_terminal_destroy_cleanup(std::int64_t) {
  async_terminal_destroy_cleanups.fetch_add(1, std::memory_order_acq_rel);
}

void async_terminal_destroy_continuation(void* context, std::int64_t, std::int64_t status) {
  async_terminal_destroy_status.store(status, std::memory_order_release);
  async_terminal_destroy_callbacks.fetch_add(1, std::memory_order_acq_rel);
  raz_rt_async_frame_destroy(context);
}

struct AsyncOwnedBytesProbe final { std::int64_t first; std::int64_t second; };
void async_owned_bytes_cleanup(std::int64_t value) {
  const auto* probe = reinterpret_cast<const AsyncOwnedBytesProbe*>(value);
  async_cleanup_order.push_back(probe == nullptr ? -1 : probe->first + probe->second);
}

void async_projected_bytes_cleanup(std::int64_t value, const std::uint64_t* words, std::int64_t word_count) {
  const auto* probe = reinterpret_cast<const AsyncOwnedBytesProbe*>(value);
  if (probe == nullptr || words == nullptr || word_count <= 0) { async_cleanup_order.push_back(-1); return; }
  if ((words[0] & 1u) != 0) async_cleanup_order.push_back(probe->first);
  if ((words[0] & 2u) != 0) async_cleanup_order.push_back(probe->second);
}

void async_large_projected_cleanup(std::int64_t value, const std::uint64_t* words, std::int64_t word_count) {
  const auto* payload = reinterpret_cast<const std::int64_t*>(value);
  if (payload == nullptr || words == nullptr || word_count < 3) { async_cleanup_order.push_back(-2); return; }
  const bool bit0 = (words[0] & 1u) != 0;
  const bool bit70 = (words[1] & (std::uint64_t{1} << 6)) != 0;
  const bool bit129 = (words[2] & (std::uint64_t{1} << 1)) != 0;
  async_cleanup_order.push_back((bit0 ? 1 : 0) + (bit70 ? 10 : 0) + (bit129 ? 100 : 0) + *payload);
}

std::int32_t async_test_poller(void* frame) {
  ++async_repoll_count;
  return raz_rt_async_state_load(frame);
}

std::int32_t async_blocking_completing_poller(void* frame) {
  async_blocking_poller_entered.store(true, std::memory_order_release);
  while (!async_blocking_poller_release.load(std::memory_order_acquire)) std::this_thread::yield();
  raz_rt_async_result_store(frame, 444);
  return 1;
}

std::int32_t async_fair_self_waking_poller(void* frame) {
  const auto call = async_fair_poll_calls.fetch_add(1, std::memory_order_acq_rel) + 1;
  if (call < async_fair_poll_limit) {
    void* future = raz_rt_future_create();
    (void)raz_rt_async_await_poll(frame, future, 29);
    (void)raz_rt_future_complete_i64(future, call);
    raz_rt_future_destroy(future);
    std::this_thread::yield();
  }

  return raz_rt_async_state_load(frame);
}

std::int32_t async_reentrant_poller(void* frame) {
  const auto depth = async_reentrant_poll_depth.fetch_add(1, std::memory_order_acq_rel) + 1;
  auto maximum = async_reentrant_poll_max_depth.load(std::memory_order_acquire);
  while (depth > maximum &&
         !async_reentrant_poll_max_depth.compare_exchange_weak(maximum, depth, std::memory_order_acq_rel)) {}
  const auto call = async_reentrant_poll_calls.fetch_add(1, std::memory_order_acq_rel) + 1;
  if (call == 1 && async_reentrant_future != nullptr) {
    async_reentrant_register_status.store(
        raz_rt_async_await_poll(frame, async_reentrant_future, 21), std::memory_order_release);
    async_reentrant_complete_status.store(
        raz_rt_future_complete_i64(async_reentrant_future, 66), std::memory_order_release);
  }
  async_reentrant_poll_depth.fetch_sub(1, std::memory_order_acq_rel);
  return raz_rt_async_state_load(frame);
}

void expect(bool condition, const char* message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main() {
  constexpr std::uint64_t pair_signature = 0x706169725f616269ull;
  PairEnvironment pair_environment{{10, 20}};
  void* erased_callable = raz_rt_callable_create_erased(&pair_environment, reinterpret_cast<void*>(&erased_pair_callable), nullptr, nullptr, 0,
                                                          pair_signature, sizeof(WidePair), sizeof(WidePair));
  expect(erased_callable != nullptr, "erased callable creation");
  expect(raz_rt_callable_signature_id(erased_callable) == pair_signature, "erased callable signature identity");
  expect(raz_rt_callable_argument_size(erased_callable) == sizeof(WidePair), "erased callable argument frame size");
  expect(raz_rt_callable_result_size(erased_callable) == sizeof(WidePair), "erased callable result frame size");
  WidePair pair_argument{3, 4};
  WidePair pair_result{};
  expect(raz_rt_callable_invoke_erased(erased_callable, pair_signature, &pair_argument, sizeof(pair_argument), &pair_result, sizeof(pair_result)) == 0 &&
         pair_result.left == 13 && pair_result.right == 24, "erased callable aggregate dispatch");
  expect(raz_rt_callable_invoke_erased(erased_callable, pair_signature + 1, &pair_argument, sizeof(pair_argument), &pair_result, sizeof(pair_result)) == -1,
         "erased callable rejects mismatched signature");
  void* erased_callable_weak = raz_rt_callable_weak_create(erased_callable);
  expect(erased_callable_weak != nullptr, "erased callable weak handle creation");
  expect(raz_rt_callable_weak_expired(erased_callable_weak) == 0, "erased callable weak handle initially live");
  void* erased_callable_upgraded = raz_rt_callable_weak_upgrade(erased_callable_weak);
  expect(erased_callable_upgraded == erased_callable, "erased callable weak upgrade");
  raz_rt_callable_destroy_erased(erased_callable_upgraded);
  void* erased_callable_clone = raz_rt_callable_clone_erased(erased_callable);
  expect(erased_callable_clone == erased_callable, "erased callable shared clone");
  raz_rt_callable_destroy_erased(erased_callable);
  raz_rt_callable_destroy_erased(erased_callable_clone);
  expect(raz_rt_callable_weak_expired(erased_callable_weak) == 1, "erased callable weak expires after final strong release");
  expect(raz_rt_callable_weak_upgrade(erased_callable_weak) == nullptr, "expired erased callable weak cannot upgrade");
  raz_rt_callable_weak_destroy(erased_callable_weak);

  WidePair trait_pair{2, 5};
  RazErasedTraitMethodTest erased_methods[]{{reinterpret_cast<void*>(&erased_pair_scale), pair_signature, sizeof(PairScaleArgs), sizeof(WidePair)}};
  void* erased_trait = raz_rt_trait_object_create_erased(&trait_pair, 0x70616972ull, 0x7363616c65ull, nullptr, erased_methods, 1);
  expect(erased_trait != nullptr, "erased trait object creation");
  expect(raz_rt_trait_object_method_signature_id(erased_trait, 0) == pair_signature, "erased trait method signature identity");
  PairScaleArgs scale_args{{1, 3}, 4};
  pair_result = {};
  expect(raz_rt_trait_object_invoke_erased(erased_trait, 0, pair_signature, &scale_args, sizeof(scale_args), &pair_result, sizeof(pair_result)) == 0 &&
         pair_result.left == 12 && pair_result.right == 32, "erased trait aggregate dispatch");
  expect(raz_rt_trait_object_invoke_erased(erased_trait, 0, pair_signature, &scale_args, sizeof(scale_args) - 1, &pair_result, sizeof(pair_result)) == -1,
         "erased trait rejects malformed argument frame");
  constexpr std::uint64_t erased_trait_type_id = 0x70616972ull;
  constexpr std::uint64_t erased_trait_id = 0x7363616c65ull;
  expect(raz_rt_trait_object_trait_id(erased_trait) == erased_trait_id, "erased trait identity");
  expect(raz_rt_trait_object_is_trait(erased_trait, erased_trait_id) == 1, "erased trait matching identity query");
  expect(raz_rt_trait_object_is_trait(erased_trait, erased_trait_id + 1) == 0, "erased trait mismatched identity query");
  expect(raz_rt_trait_object_type_id(erased_trait) == erased_trait_type_id, "erased trait concrete type identity");
  expect(raz_rt_trait_object_downcast_data(erased_trait, erased_trait_type_id) == &trait_pair, "erased trait matching downcast");
  expect(raz_rt_trait_object_downcast_data(erased_trait, erased_trait_type_id + 1) == nullptr, "erased trait mismatched downcast rejection");
  void* erased_trait_weak = raz_rt_trait_object_weak_create(erased_trait);
  expect(erased_trait_weak != nullptr, "erased trait weak handle creation");
  expect(raz_rt_trait_object_weak_expired(erased_trait_weak) == 0, "erased trait weak initially live");
  void* erased_trait_upgraded = raz_rt_trait_object_weak_upgrade(erased_trait_weak);
  expect(erased_trait_upgraded == erased_trait, "erased trait weak upgrade");
  raz_rt_trait_object_destroy_erased(erased_trait_upgraded);
  void* erased_trait_clone = raz_rt_trait_object_clone_erased(erased_trait);
  expect(erased_trait_clone == erased_trait, "erased trait object shared clone");
  raz_rt_trait_object_destroy_erased(erased_trait);
  raz_rt_trait_object_destroy_erased(erased_trait_clone);
  expect(raz_rt_trait_object_weak_expired(erased_trait_weak) == 1, "erased trait weak expires after final strong release");
  expect(raz_rt_trait_object_weak_upgrade(erased_trait_weak) == nullptr, "expired erased trait weak cannot upgrade");
  raz_rt_trait_object_weak_destroy(erased_trait_weak);

  void* async_frame = raz_rt_async_frame_create(3);
  expect(async_frame != nullptr, "async frame allocation");
  expect(raz_rt_async_frame_slot_count(async_frame) == 3, "async frame slot count");
  expect(raz_rt_async_state_load(async_frame) == 0, "async frame initial state");
  raz_rt_async_state_store(async_frame, 7);
  expect(raz_rt_async_state_load(async_frame) == 7, "async frame state roundtrip");
  raz_rt_async_slot_store(async_frame, 0, 42);
  raz_rt_async_slot_store(async_frame, 2, -9);
  expect(raz_rt_async_slot_load(async_frame, 0) == 42, "async frame slot zero roundtrip");
  expect(raz_rt_async_slot_load(async_frame, 2) == -9, "async frame slot two roundtrip");
  expect(raz_rt_async_slot_load(async_frame, 3) == 0, "async frame out-of-range load");
  auto* async_slot_address = static_cast<std::int64_t*>(raz_rt_async_slot_address(async_frame, 1));
  expect(async_slot_address != nullptr, "async frame slot address");
  *async_slot_address = 77;
  expect(raz_rt_async_slot_load(async_frame, 1) == 77, "async frame slot address roundtrip");
  expect(raz_rt_async_slot_address(async_frame, 3) == nullptr, "async frame invalid slot address");

  async_cleanup_order.clear();
  auto* owned_bytes = static_cast<AsyncOwnedBytesProbe*>(raz_rt_async_slot_allocate_owned_bytes(
      async_frame, 1, sizeof(AsyncOwnedBytesProbe), alignof(AsyncOwnedBytesProbe),
      reinterpret_cast<std::uintptr_t>(&async_owned_bytes_cleanup)));
  expect(owned_bytes != nullptr, "async owned byte slot allocation");
  owned_bytes->first = 19;
  owned_bytes->second = 23;
  const AsyncOwnedBytesProbe replacement{20, 22};
  raz_rt_async_slot_store_owned_bytes(async_frame, 1, &replacement, sizeof(replacement), alignof(AsyncOwnedBytesProbe),
                                       reinterpret_cast<std::uintptr_t>(&async_owned_bytes_cleanup));
  expect(async_cleanup_order.size() == 1 && async_cleanup_order[0] == 42,
         "async owned byte overwrite recursively cleans previous storage");
  raz_rt_async_slot_disarm(async_frame, 1);

  async_cleanup_order.clear();
  auto* projected_bytes = static_cast<AsyncOwnedBytesProbe*>(raz_rt_async_slot_allocate_projected_bytes(
      async_frame, 1, sizeof(AsyncOwnedBytesProbe), alignof(AsyncOwnedBytesProbe),
      reinterpret_cast<std::uintptr_t>(&async_projected_bytes_cleanup), 3, 2));
  expect(projected_bytes != nullptr, "async projected byte slot allocation");
  projected_bytes->first = 31;
  projected_bytes->second = 37;
  expect(raz_rt_async_slot_projection_count(async_frame, 1) == 2, "async projected slot projection count");
  expect(raz_rt_async_slot_projection_mask(async_frame, 1) == 3, "async projected slot initial mask");
  raz_rt_async_slot_projection_set(async_frame, 1, 2, false);
  expect(raz_rt_async_slot_projection_mask(async_frame, 1) == 3, "async projected slot rejects out-of-range projection");
  raz_rt_async_slot_projection_set(async_frame, 1, 0, false);
  expect(raz_rt_async_slot_projection_mask(async_frame, 1) == 2, "async projected slot clears moved field");
  raz_rt_async_slot_store(async_frame, 1, 0);
  expect(async_cleanup_order.size() == 1 && async_cleanup_order[0] == 37,
         "async projected cleanup skips moved field");

  async_cleanup_order.clear();
  auto* disarmed_projected = static_cast<AsyncOwnedBytesProbe*>(raz_rt_async_slot_allocate_projected_bytes(
      async_frame, 1, sizeof(AsyncOwnedBytesProbe), alignof(AsyncOwnedBytesProbe),
      reinterpret_cast<std::uintptr_t>(&async_projected_bytes_cleanup), 3, 2));
  expect(disarmed_projected != nullptr, "async projected disarm allocation");
  disarmed_projected->first = 41;
  disarmed_projected->second = 43;
  raz_rt_async_slot_disarm(async_frame, 1);
  expect(raz_rt_async_slot_projection_count(async_frame, 1) == 0, "async projected disarm clears projection metadata");
  raz_rt_async_slot_store(async_frame, 1, 0);
  expect(async_cleanup_order.empty(), "async projected disarm suppresses cleanup");

  async_cleanup_order.clear();
  auto* transferred_projected = static_cast<AsyncOwnedBytesProbe*>(raz_rt_async_slot_allocate_projected_bytes(
      async_frame, 1, sizeof(AsyncOwnedBytesProbe), alignof(AsyncOwnedBytesProbe),
      reinterpret_cast<std::uintptr_t>(&async_projected_bytes_cleanup), 3, 2));
  expect(transferred_projected != nullptr, "async projected transfer allocation");
  expect(raz_rt_async_slot_lifecycle(async_frame, 1) == 2,
         "async projected slot lifecycle is armed before transfer");
  transferred_projected->first = 47;
  transferred_projected->second = 53;
  expect(raz_rt_async_slot_transfer_projected(async_frame, 1) == transferred_projected,
         "async projected slot transfers storage atomically");
  expect(raz_rt_async_slot_projection_count(async_frame, 1) == 0,
         "async projected transfer clears projection metadata");
  expect(raz_rt_async_slot_lifecycle(async_frame, 1) == 3,
         "async projected slot lifecycle records transferred ownership");
  raz_rt_async_slot_projection_set(async_frame, 1, 0, true);
  expect(raz_rt_async_slot_projection_count(async_frame, 1) == 0 &&
             raz_rt_async_slot_lifecycle(async_frame, 1) == 3,
         "async projected slot rejects stale projection mutation after transfer");
  expect(raz_rt_async_slot_transfer_projected(async_frame, 1) == nullptr,
         "async projected slot cannot transfer ownership twice");
  raz_rt_async_slot_store(async_frame, 1, 0);
  expect(async_cleanup_order.empty(), "async projected transfer suppresses cancellation cleanup");
  expect(raz_rt_async_slot_lifecycle(async_frame, 1) == 1,
         "async projected slot lifecycle resets on replacement");

  async_cleanup_order.clear();
  auto* large_projected = static_cast<std::int64_t*>(raz_rt_async_slot_allocate_projected_bytes(
      async_frame, 1, sizeof(std::int64_t), alignof(std::int64_t),
      reinterpret_cast<std::uintptr_t>(&async_large_projected_cleanup), ~std::uint64_t{0}, 130));
  expect(large_projected != nullptr, "async projected slot supports more than 64 projections");
  *large_projected = 1000;
  expect(raz_rt_async_slot_projection_word_count(async_frame, 1) == 3,
         "async projected diagnostics report all bitmap words");
  expect(raz_rt_async_slot_projection_version(async_frame, 1) == 0,
         "async projected slot starts with a stable zero version");
  raz_rt_async_slot_projection_range_set(async_frame, 1, 60, 20, false);
  expect(raz_rt_async_slot_projection_version(async_frame, 1) == 1,
         "async projection range advances the version once");
  expect((raz_rt_async_slot_projection_word(async_frame, 1, 0) & (std::uint64_t{1} << 59)) != 0 &&
             (raz_rt_async_slot_projection_word(async_frame, 1, 0) & (std::uint64_t{1} << 60)) == 0 &&
             (raz_rt_async_slot_projection_word(async_frame, 1, 1) & (std::uint64_t{1} << 15)) == 0 &&
             (raz_rt_async_slot_projection_word(async_frame, 1, 1) & (std::uint64_t{1} << 16)) != 0,
         "async projection range update crosses bitmap words exactly");
  raz_rt_async_slot_projection_range_set(async_frame, 1, 70, 1, true);
  expect(raz_rt_async_slot_projection_version(async_frame, 1) == 2,
         "async projection restoration advances the version");
  std::uint64_t diagnostic_words[3] = {};
  expect(raz_rt_async_slot_projection_copy_words(async_frame, 1, diagnostic_words, 3) == 3 &&
             diagnostic_words[0] == raz_rt_async_slot_projection_word(async_frame, 1, 0) &&
             diagnostic_words[1] == raz_rt_async_slot_projection_word(async_frame, 1, 1) &&
             diagnostic_words[2] == raz_rt_async_slot_projection_word(async_frame, 1, 2),
         "async projected diagnostics serialize the complete bitmap");
  const std::int64_t batch_first[] = {0, 64, 128};
  const std::int64_t batch_count[] = {4, 4, 2};
  const std::uint8_t batch_state[] = {0, 0, 0};
  expect(raz_rt_async_slot_projection_batch_set(async_frame, 1, batch_first, batch_count, batch_state, 3) == 6 &&
             raz_rt_async_slot_projection_version(async_frame, 1) == 3,
         "async projection batch reports changed projections and advances one version");
  expect(raz_rt_async_slot_projection_batch_set(async_frame, 1, batch_first, batch_count, batch_state, 3) == 0 &&
             raz_rt_async_slot_projection_version(async_frame, 1) == 3,
         "async projection batch suppresses redundant publications");
  const std::int64_t invalid_first[] = {4, 130};
  const std::int64_t invalid_count[] = {2, 1};
  const std::uint8_t invalid_state[] = {0, 1};
  const auto before_invalid = raz_rt_async_slot_projection_word(async_frame, 1, 0);
  expect(raz_rt_async_slot_projection_batch_set(async_frame, 1, invalid_first, invalid_count, invalid_state, 2) == 0 &&
             raz_rt_async_slot_projection_word(async_frame, 1, 0) == before_invalid &&
             raz_rt_async_slot_projection_version(async_frame, 1) == 3,
         "async projection batch rejects malformed updates without partial mutation");
  const auto run_count = raz_rt_async_slot_projection_run_count(async_frame, 1);
  std::int64_t run_first[16] = {};
  std::int64_t run_counts[16] = {};
  std::uint8_t run_states[16] = {};
  expect(run_count > 0 && run_count <= 16 &&
             raz_rt_async_slot_projection_copy_runs(async_frame, 1, run_first, run_counts, run_states, 16) == run_count &&
             run_first[0] == 0 && run_counts[0] == 4 && run_states[0] == 0,
         "async projected diagnostics expose compact initialized-state runs");
  raz_rt_async_slot_projection_set(async_frame, 1, 70, false);
  raz_rt_async_slot_store(async_frame, 1, 0);
  expect(async_cleanup_order.size() == 1 && async_cleanup_order[0] == 1000,
         "async multiword projection cleanup observes batched high-bit transitions");

  async_cleanup_order.clear();
  raz_rt_async_slot_store_owned(async_frame, 0, 10, reinterpret_cast<std::uintptr_t>(&async_owned_slot_cleanup));
  raz_rt_async_slot_store_owned(async_frame, 1, 20, reinterpret_cast<std::uintptr_t>(&async_owned_slot_cleanup));
  raz_rt_async_slot_store_owned(async_frame, 2, 30, reinterpret_cast<std::uintptr_t>(&async_owned_slot_cleanup));
  expect(raz_rt_async_slot_take(async_frame, 1) == 20, "async owned slot take transfers value");
  raz_rt_async_slot_disarm(async_frame, 2);
  raz_rt_async_slot_store_owned(async_frame, 0, 11, reinterpret_cast<std::uintptr_t>(&async_owned_slot_cleanup));
  expect(async_cleanup_order.size() == 1 && async_cleanup_order[0] == 10,
         "async owned slot overwrite cleans previous value");
  expect(raz_rt_async_cancel_requested(async_frame) == 0, "async frame initially active");
  void* async_completion = raz_rt_async_frame_future(async_frame);
  expect(async_completion != nullptr, "async frame completion future");
  expect(raz_rt_async_frame_lifecycle(async_frame) == 0, "async frame starts active");
  raz_rt_async_result_store(async_frame, 1234);
  expect(raz_rt_async_result_load(async_frame) == 1234, "async frame result roundtrip");
  expect(raz_rt_async_frame_lifecycle(async_frame) == 1 &&
             raz_rt_async_frame_terminal_intent(async_frame) == 1,
         "async frame records completion intent and lifecycle");
  raz_rt_async_slot_store(async_frame, 0, 999);
  expect(raz_rt_async_slot_load(async_frame, 0) == 11,
         "completed async frame rejects stale slot mutation");
  raz_rt_async_result_store(async_frame, 5678);
  expect(raz_rt_async_result_load(async_frame) == 1234,
         "completed async frame rejects duplicate completion");
  std::int64_t async_result = 0;
  expect(raz_rt_future_result_i64(async_completion, &async_result) == 1 && async_result == 1234,
         "async frame completion future result");
  raz_rt_future_destroy(async_completion);

  async_terminal_destroy_callbacks.store(0, std::memory_order_release);
  async_terminal_destroy_status.store(0, std::memory_order_release);
  async_terminal_destroy_cleanups.store(0, std::memory_order_release);
  void* callback_destroyed_frame = raz_rt_async_frame_create(1);
  raz_rt_async_slot_store_owned(callback_destroyed_frame, 0, 91,
                                 reinterpret_cast<std::uintptr_t>(&async_terminal_destroy_cleanup));
  void* callback_destroyed_future = raz_rt_async_frame_future(callback_destroyed_frame);
  expect(callback_destroyed_future != nullptr &&
             raz_rt_future_then_i64(callback_destroyed_future,
                                     reinterpret_cast<std::uintptr_t>(&async_terminal_destroy_continuation),
                                     callback_destroyed_frame) == 1,
         "async completion registers frame-destroying continuation");
  raz_rt_async_result_store(callback_destroyed_frame, 909);
  std::int64_t callback_destroyed_result = 0;
  expect(async_terminal_destroy_callbacks.load(std::memory_order_acquire) == 1 &&
             async_terminal_destroy_status.load(std::memory_order_acquire) == 1 &&
             async_terminal_destroy_cleanups.load(std::memory_order_acquire) == 1 &&
             raz_rt_future_result_i64(callback_destroyed_future, &callback_destroyed_result) == 1 &&
             callback_destroyed_result == 909,
         "completion continuation may release final public frame reference during terminal publication");
  raz_rt_future_destroy(callback_destroyed_future);

  async_terminal_destroy_callbacks.store(0, std::memory_order_release);
  async_terminal_destroy_status.store(0, std::memory_order_release);
  async_terminal_destroy_cleanups.store(0, std::memory_order_release);
  void* callback_cancelled_frame = raz_rt_async_frame_create(1);
  raz_rt_async_slot_store_owned(callback_cancelled_frame, 0, 92,
                                 reinterpret_cast<std::uintptr_t>(&async_terminal_destroy_cleanup));
  void* callback_cancelled_future = raz_rt_async_frame_future(callback_cancelled_frame);
  expect(callback_cancelled_future != nullptr &&
             raz_rt_future_then_i64(callback_cancelled_future,
                                     reinterpret_cast<std::uintptr_t>(&async_terminal_destroy_continuation),
                                     callback_cancelled_frame) == 1,
         "async cancellation registers frame-destroying continuation");
  raz_rt_async_frame_cancel(callback_cancelled_frame);
  std::int64_t callback_cancelled_result = 0;
  expect(async_terminal_destroy_callbacks.load(std::memory_order_acquire) == 1 &&
             async_terminal_destroy_status.load(std::memory_order_acquire) == -1 &&
             async_terminal_destroy_cleanups.load(std::memory_order_acquire) == 1 &&
             raz_rt_future_result_i64(callback_cancelled_future, &callback_cancelled_result) == -1,
         "cancellation continuation may release final public frame reference during terminal publication");
  raz_rt_future_destroy(callback_cancelled_future);

  void* awaited_frame = raz_rt_async_frame_create(1);
  raz_rt_async_frame_set_poller(awaited_frame, reinterpret_cast<void*>(&async_test_poller));
  void* awaited_future = raz_rt_future_create();
  expect(raz_rt_async_await_poll(awaited_frame, awaited_future, 5) == 0, "async await initially pending");
  expect(raz_rt_async_frame_has_pending_await(awaited_frame) == 1,
         "async frame reports active pending await");
  expect(raz_rt_async_await_poll(awaited_frame, awaited_future, 5) == 0, "async await duplicate poll remains pending");
  expect(raz_rt_future_complete_i64(awaited_future, 88) == 1, "async awaited future completion");
  expect(raz_rt_async_frame_has_pending_await(awaited_frame) == 0,
         "accepted await callback releases pending registration immediately");
  expect(raz_rt_async_state_load(awaited_frame) == 5, "async await continuation resumes target state");
  expect(async_repoll_count.load() == 1, "async await continuation automatically re-polls frame");
  expect(raz_rt_async_await_poll(awaited_frame, awaited_future, 5) == 1, "async await ready poll");
  expect(raz_rt_async_await_result(awaited_frame) == 88, "async await result propagation");
  raz_rt_future_destroy(awaited_future);
  raz_rt_async_frame_destroy(awaited_frame);

  async_repoll_count.store(0);
  void* replaced_frame = raz_rt_async_frame_create(0);
  raz_rt_async_frame_set_poller(replaced_frame, reinterpret_cast<void*>(&async_test_poller));
  void* stale_future = raz_rt_future_create();
  void* current_future = raz_rt_future_create();
  expect(raz_rt_async_await_poll(replaced_frame, stale_future, 3) == 0,
         "async frame registers first pending await");
  expect(raz_rt_async_await_poll(replaced_frame, current_future, 7) == 0 &&
             raz_rt_future_continuation_count(stale_future) == 0 &&
             raz_rt_future_continuation_count(current_future) == 1,
         "async frame detaches replaced pending await registration");
  expect(raz_rt_future_complete_i64(stale_future, 11) == 1 &&
             async_repoll_count.load() == 0 && raz_rt_async_state_load(replaced_frame) == 0,
         "stale await completion cannot resume replaced registration");
  expect(raz_rt_future_complete_i64(current_future, 22) == 1 &&
             async_repoll_count.load() == 1 && raz_rt_async_state_load(replaced_frame) == 7 &&
             raz_rt_async_await_result(replaced_frame) == 22,
         "current await generation resumes exact continuation state");
  raz_rt_future_destroy(stale_future);
  raz_rt_future_destroy(current_future);
  raz_rt_async_frame_destroy(replaced_frame);

  async_repoll_count.store(0);
  void* ready_replacement_frame = raz_rt_async_frame_create(0);
  raz_rt_async_frame_set_poller(ready_replacement_frame, reinterpret_cast<void*>(&async_test_poller));
  void* obsolete_pending_future = raz_rt_future_create();
  void* already_ready_future = raz_rt_future_create();
  expect(raz_rt_async_await_poll(ready_replacement_frame, obsolete_pending_future, 31) == 0,
         "ready replacement frame registers obsolete pending await");
  const auto generation_before_ready = raz_rt_async_frame_await_generation(ready_replacement_frame);
  expect(raz_rt_future_complete_i64(already_ready_future, 77) == 1 &&
             raz_rt_async_await_poll(ready_replacement_frame, already_ready_future, 37) == 1 &&
             raz_rt_async_state_load(ready_replacement_frame) == 37 &&
             raz_rt_async_await_result(ready_replacement_frame) == 77 &&
             raz_rt_async_frame_has_pending_await(ready_replacement_frame) == 0 &&
             raz_rt_async_frame_await_generation(ready_replacement_frame) > generation_before_ready &&
             raz_rt_future_continuation_count(obsolete_pending_future) == 0,
         "already-ready await atomically detaches older pending registration");
  expect(raz_rt_future_complete_i64(obsolete_pending_future, 99) == 1 &&
             async_repoll_count.load(std::memory_order_acquire) == 0 &&
             raz_rt_async_state_load(ready_replacement_frame) == 37 &&
             raz_rt_async_await_result(ready_replacement_frame) == 77,
         "obsolete pending completion cannot overwrite ready replacement state");
  raz_rt_future_destroy(obsolete_pending_future);
  raz_rt_future_destroy(already_ready_future);
  raz_rt_async_frame_destroy(ready_replacement_frame);

  async_repoll_count.store(0);
  void* terminal_await_frame = raz_rt_async_frame_create(0);
  raz_rt_async_frame_set_poller(terminal_await_frame, reinterpret_cast<void*>(&async_test_poller));
  void* terminal_await_future = raz_rt_future_create();
  expect(raz_rt_async_await_poll(terminal_await_frame, terminal_await_future, 9) == 0,
         "terminal frame registers pending await");
  raz_rt_async_frame_cancel(terminal_await_frame);
  expect(raz_rt_future_continuation_count(terminal_await_future) == 0,
         "frame cancellation detaches pending await continuation immediately");
  expect(raz_rt_future_complete_i64(terminal_await_future, 33) == 1 &&
             async_repoll_count.load() == 0 && raz_rt_async_state_load(terminal_await_frame) == 0,
         "cancelled frame suppresses late await continuation");
  raz_rt_future_destroy(terminal_await_future);
  raz_rt_async_frame_destroy(terminal_await_frame);

  async_blocking_poller_entered.store(false, std::memory_order_release);
  async_blocking_poller_release.store(false, std::memory_order_release);
  async_cancel_returned.store(false, std::memory_order_release);
  void* serialized_frame = raz_rt_async_frame_create(0);
  raz_rt_async_frame_set_poller(serialized_frame, reinterpret_cast<void*>(&async_blocking_completing_poller));
  void* serialized_future = raz_rt_future_create();
  expect(raz_rt_async_await_poll(serialized_frame, serialized_future, 13) == 0,
         "serialized poll frame registers pending await");
  std::thread completion_thread([&] { (void)raz_rt_future_complete_i64(serialized_future, 55); });
  while (!async_blocking_poller_entered.load(std::memory_order_acquire)) std::this_thread::yield();
  std::thread cancellation_thread([&] {
    raz_rt_async_frame_cancel(serialized_frame);
    async_cancel_returned.store(true, std::memory_order_release);
  });
  while (raz_rt_async_frame_terminal_intent(serialized_frame) != 2) std::this_thread::yield();
  expect(!async_cancel_returned.load(std::memory_order_acquire),
         "claimed terminal intent waits for active poll callback");
  async_blocking_poller_release.store(true, std::memory_order_release);
  completion_thread.join();
  cancellation_thread.join();
  expect(raz_rt_async_frame_lifecycle(serialized_frame) == 2 &&
             raz_rt_async_frame_terminal_intent(serialized_frame) == 2 &&
             raz_rt_async_result_load(serialized_frame) == 0,
         "first terminal request deterministically wins poll completion race");
  raz_rt_future_destroy(serialized_future);
  raz_rt_async_frame_destroy(serialized_frame);

  async_reentrant_poll_calls.store(0, std::memory_order_release);
  async_reentrant_poll_depth.store(0, std::memory_order_release);
  async_reentrant_poll_max_depth.store(0, std::memory_order_release);
  async_reentrant_register_status.store(-99, std::memory_order_release);
  async_reentrant_complete_status.store(-99, std::memory_order_release);
  void* reentrant_frame = raz_rt_async_frame_create(0);
  void* reentrant_initial_future = raz_rt_future_create();
  async_reentrant_future = raz_rt_future_create();
  raz_rt_async_frame_set_poller(reentrant_frame, reinterpret_cast<void*>(&async_reentrant_poller));
  expect(raz_rt_async_await_poll(reentrant_frame, reentrant_initial_future, 17) == 0,
         "reentrant frame registers initial await");
  expect(raz_rt_future_complete_i64(reentrant_initial_future, 44) == 1 &&
             async_reentrant_poll_calls.load(std::memory_order_acquire) == 2 &&
             async_reentrant_poll_max_depth.load(std::memory_order_acquire) == 1 &&
             async_reentrant_register_status.load(std::memory_order_acquire) == 0 &&
             async_reentrant_complete_status.load(std::memory_order_acquire) == 1 &&
             raz_rt_async_state_load(reentrant_frame) == 21 &&
             raz_rt_async_await_result(reentrant_frame) == 66,
         "nested synchronous wakeups drain iteratively without recursive polling");
  void* completed_reentrant_future = async_reentrant_future;
  async_reentrant_future = nullptr;
  raz_rt_future_destroy(reentrant_initial_future);
  raz_rt_future_destroy(completed_reentrant_future);
  raz_rt_async_frame_destroy(reentrant_frame);

  async_fair_poll_calls.store(0, std::memory_order_release);
  void* fair_frame = raz_rt_async_frame_create(0);
  void* fair_initial_future = raz_rt_future_create();
  raz_rt_async_frame_set_poller(fair_frame, reinterpret_cast<void*>(&async_fair_self_waking_poller));
  expect(raz_rt_async_await_poll(fair_frame, fair_initial_future, 29) == 0,
         "fair poll frame registers initial await");
  std::thread fair_completion_thread([&] { (void)raz_rt_future_complete_i64(fair_initial_future, 1); });
  while (async_fair_poll_calls.load(std::memory_order_acquire) < 100) std::this_thread::yield();
  std::thread fair_cancellation_thread([&] { raz_rt_async_frame_cancel(fair_frame); });
  fair_completion_thread.join();
  fair_cancellation_thread.join();
  expect(raz_rt_async_frame_lifecycle(fair_frame) == 2 &&
             async_fair_poll_calls.load(std::memory_order_acquire) < async_fair_poll_limit,
         "terminal intent preempts an indefinitely self-waking poll drain");
  raz_rt_future_destroy(fair_initial_future);
  raz_rt_async_frame_destroy(fair_frame);

  void* retained_frame = raz_rt_async_frame_create(0);
  void* retained_future = raz_rt_future_create();
  expect(raz_rt_async_await_poll(retained_frame, retained_future, 3) == 0, "async await retains pending frame");
  raz_rt_async_frame_destroy(retained_frame);
  expect(raz_rt_future_complete_i64(retained_future, 12) == 1, "async continuation survives public frame destruction");
  raz_rt_future_destroy(retained_future);

  raz_rt_async_frame_cancel(async_frame);
  expect(raz_rt_async_cancel_requested(async_frame) == 0 && raz_rt_async_frame_lifecycle(async_frame) == 1,
         "completed async frame rejects late cancellation");
  raz_rt_async_frame_destroy(async_frame);
  expect(async_cleanup_order.size() == 2 && async_cleanup_order[1] == 11,
         "async frame skips disarmed slots and destroys remaining owned slots once");

  void* cancelled_frame = raz_rt_async_frame_create(1);
  expect(cancelled_frame != nullptr && raz_rt_async_frame_lifecycle(cancelled_frame) == 0,
         "async cancellation frame starts active");
  raz_rt_async_slot_store(cancelled_frame, 0, 77);
  raz_rt_async_frame_cancel(cancelled_frame);
  expect(raz_rt_async_cancel_requested(cancelled_frame) == 1 &&
             raz_rt_async_frame_lifecycle(cancelled_frame) == 2 &&
             raz_rt_async_frame_terminal_intent(cancelled_frame) == 2,
         "async frame records cancellation intent and lifecycle");
  raz_rt_async_slot_store(cancelled_frame, 0, 88);
  expect(raz_rt_async_slot_load(cancelled_frame, 0) == 77,
         "cancelled async frame rejects stale slot mutation");
  raz_rt_async_result_store(cancelled_frame, 99);
  expect(raz_rt_async_result_load(cancelled_frame) == 0,
         "cancelled async frame rejects completion");
  raz_rt_async_frame_destroy(cancelled_frame);
  expect(raz_rt_abi_pointer_size() == static_cast<std::int64_t>(sizeof(void*)), "ABI pointer size");
  expect(raz_rt_abi_pointer_alignment() == static_cast<std::int64_t>(alignof(void*)), "ABI pointer alignment");
  expect(raz_rt_abi_bool_size() == static_cast<std::int64_t>(sizeof(bool)) && raz_rt_abi_bool_alignment() == static_cast<std::int64_t>(alignof(bool)), "ABI bool layout");
  expect(raz_rt_abi_i8_size() == static_cast<std::int64_t>(sizeof(std::int8_t)) && raz_rt_abi_i8_alignment() == static_cast<std::int64_t>(alignof(std::int8_t)), "ABI i8 layout");
  expect(raz_rt_abi_i16_size() == static_cast<std::int64_t>(sizeof(std::int16_t)) && raz_rt_abi_i16_alignment() == static_cast<std::int64_t>(alignof(std::int16_t)), "ABI i16 layout");
  expect(raz_rt_abi_i32_size() == static_cast<std::int64_t>(sizeof(std::int32_t)) && raz_rt_abi_i32_alignment() == static_cast<std::int64_t>(alignof(std::int32_t)), "ABI i32 layout");
  expect(raz_rt_abi_i64_size() == 8 && raz_rt_abi_i64_alignment() == static_cast<std::int64_t>(alignof(std::int64_t)), "ABI i64 layout");
  expect(raz_rt_abi_f32_size() == static_cast<std::int64_t>(sizeof(float)) && raz_rt_abi_f32_alignment() == static_cast<std::int64_t>(alignof(float)), "ABI f32 layout");
  expect(raz_rt_abi_f64_size() == 8 && raz_rt_abi_f64_alignment() == static_cast<std::int64_t>(alignof(double)), "ABI f64 layout");
  expect(raz_rt_abi_size_t_size() == static_cast<std::int64_t>(sizeof(std::size_t)) && raz_rt_abi_size_t_alignment() == static_cast<std::int64_t>(alignof(std::size_t)), "ABI size_t layout");
  expect(raz_rt_abi_little_endian() == 0 || raz_rt_abi_little_endian() == 1, "ABI endian query");
  expect(raz_rt_time_unix_millis() > 0, "wall clock");
  expect(raz_rt_time_monotonic_nanos() > 0, "monotonic clock");
  expect(raz_rt_hardware_threads() >= 1, "hardware threads");

  void* memory = raz_rt_alloc(16);
  expect(memory != nullptr, "allocation");
  raz_rt_fill(memory, 0x5a, 16);
  memory = raz_rt_realloc(memory, 32);
  expect(memory != nullptr && static_cast<unsigned char*>(memory)[0] == 0x5a, "reallocation preserves data");
  raz_rt_dealloc(memory);

  std::int64_t atomic = 4;
  expect(raz_rt_atomic_add_i64(&atomic, 3) == 4 && atomic == 7, "atomic add");
  expect(raz_rt_atomic_compare_exchange_i64(&atomic, 7, 11) == 1 && atomic == 11, "atomic compare exchange");
  raz_rt_atomic_store_i64_ordered(&atomic, 20, 2);
  expect(raz_rt_atomic_load_i64_ordered(&atomic, 1) == 20, "ordered atomic load/store");
  expect(raz_rt_atomic_add_i64_ordered(&atomic, 2, 3) == 20 && atomic == 22, "ordered atomic add");
  raz_rt_memory_fence_ordered(4);

  void* mutex = raz_rt_mutex_create();
  expect(mutex != nullptr, "mutex create");
  raz_rt_mutex_lock(mutex);
  raz_rt_mutex_unlock(mutex);

  void* condition = raz_rt_condition_create();
  expect(condition != nullptr, "condition create");
  raz_rt_mutex_lock(mutex);
  expect(raz_rt_condition_wait_millis(condition, mutex, 1) == 0, "condition timeout");
  raz_rt_mutex_unlock(mutex);
  raz_rt_condition_notify_one(condition);
  raz_rt_condition_notify_all(condition);
  raz_rt_condition_destroy(condition);
  raz_rt_mutex_destroy(mutex);

  void* rwlock = raz_rt_rwlock_create();
  expect(rwlock != nullptr, "rwlock create");
  raz_rt_rwlock_read_lock(rwlock);
  raz_rt_rwlock_read_unlock(rwlock);
  expect(raz_rt_rwlock_try_write(rwlock) == 1, "rwlock try write");
  raz_rt_rwlock_write_unlock(rwlock);
  expect(raz_rt_rwlock_try_read(rwlock) == 1, "rwlock try read");
  raz_rt_rwlock_read_unlock(rwlock);
  raz_rt_rwlock_destroy(rwlock);

  std::array<char, 4096> cwd{};
  expect(raz_rt_current_dir(cwd.data(), static_cast<std::int64_t>(cwd.size())) > 0, "current directory");

  const auto root = std::filesystem::temp_directory_path() / "raz-runtime-filesystem-test";
  const auto root_text = root.string();
  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  expect(raz_rt_create_dir_one(root_text.data(), static_cast<std::int64_t>(root_text.size())) >= 0, "create one directory");
  expect(raz_rt_path_exists(root_text.data(), static_cast<std::int64_t>(root_text.size())) == 1, "path exists");
  const auto file = root / "data.bin";
  const auto file_text = file.string();
  const std::string payload = "raz-runtime";
  { std::ofstream out(file, std::ios::binary); out << payload; }
  expect(raz_rt_file_size(file_text.data(), static_cast<std::int64_t>(file_text.size())) == static_cast<std::int64_t>(payload.size()), "file size");
  expect(raz_rt_remove_one(file_text.data(), static_cast<std::int64_t>(file_text.size())) == 1, "remove one file");
  expect(raz_rt_remove_one(root_text.data(), static_cast<std::int64_t>(root_text.size())) == 1, "remove one directory");

  std::array<char, 64> address{};
  expect(raz_rt_dns_resolve_ipv4("localhost", 9, address.data(), static_cast<std::int64_t>(address.size())) > 0, "DNS resolution");
  const auto listener = raz_rt_tcp_listen(0, 4);
  expect(listener >= 0, "TCP listener");
  if (listener >= 0) raz_rt_socket_close(listener);

  const auto resolved_count = raz_rt_dns_resolve_all("localhost", 9, nullptr, 0);
  expect(resolved_count > 0, "AF_UNSPEC DNS sizing");
  if (resolved_count > 0) {
    std::vector<RazResolvedAddressRecordForTest> resolved(static_cast<std::size_t>(resolved_count));
    const auto fetched = raz_rt_dns_resolve_all("localhost", 9, resolved.data(), resolved_count);
    expect(fetched > 0 && fetched <= resolved_count, "AF_UNSPEC DNS fetch");
    bool valid_family = true;
    for (std::int64_t index = 0; index < std::min(fetched, resolved_count); ++index) {
      valid_family = valid_family && (resolved[static_cast<std::size_t>(index)].family == 4 || resolved[static_cast<std::size_t>(index)].family == 6);
    }

    expect(valid_family, "AF_UNSPEC DNS family records");
  }
  const auto any_listener = raz_rt_tcp_listen_host(nullptr, 0, 0, 8);
  expect(any_listener >= 0, "dual-stack-capable TCP listener");
  if (any_listener >= 0) {
    expect(raz_rt_socket_local_port(any_listener) > 0, "host listener ephemeral port");
    const auto family = raz_rt_socket_family(any_listener);
    expect(family == 4 || family == 6, "host listener family");
    raz_rt_socket_close(any_listener);
  }
  const auto any_udp = raz_rt_udp_bind_host(nullptr, 0, 0);
  expect(any_udp >= 0, "dual-stack-capable UDP bind");
  if (any_udp >= 0) {
    expect(raz_rt_socket_local_port(any_udp) > 0, "host UDP ephemeral port");
    const auto family = raz_rt_socket_family(any_udp);
    expect(family == 4 || family == 6, "host UDP family");
    raz_rt_socket_close(any_udp);
  }

  std::int64_t volatile_value = 17;
  const auto volatile_address = reinterpret_cast<std::uintptr_t>(&volatile_value);
  expect(raz_rt_volatile_load_i64(volatile_address) == 17, "volatile load");
  raz_rt_volatile_store_i64(volatile_address, 29);
  raz_rt_memory_fence();
  expect(volatile_value == 29, "volatile store and fence");
  expect(raz_rt_cpu_has_sse2() == 0 || raz_rt_cpu_has_sse2() == 1, "SSE2 feature query");
  expect(raz_rt_cpu_has_avx2() == 0 || raz_rt_cpu_has_avx2() == 1, "AVX2 feature query");
  expect(raz_rt_cpu_has_neon() == 0 || raz_rt_cpu_has_neon() == 1, "NEON feature query");
  expect(raz_rt_cache_line_size() >= 16, "cache-line size");


  std::int64_t task_input = 40;
  void* task = raz_rt_task_spawn_i64(reinterpret_cast<std::uintptr_t>(&task_entry), &task_input);
  expect(task != nullptr, "task spawn");
  if (task != nullptr) {
    raz_rt_yield_now();
    const auto ready = raz_rt_task_is_ready(task);
    expect(ready == 0 || ready == 1, "task readiness");
    expect(raz_rt_task_wait_millis(task, 1000) == 1, "task timed wait");
    expect(raz_rt_task_join_i64(task) == 42, "task join result");
  }

  void* manual_future = raz_rt_future_create();
  expect(manual_future != nullptr, "future create");
  if (manual_future != nullptr) {
    FutureContinuationState continuation{};
    expect(raz_rt_future_status(manual_future) == 0, "future initially pending");
    expect(raz_rt_future_then_i64(manual_future, reinterpret_cast<std::uintptr_t>(&future_continuation), &continuation) == 1,
           "future continuation registration");
    expect(raz_rt_future_wait_millis(manual_future, 1) == 0, "future wait timeout");
    expect(raz_rt_future_complete_i64(manual_future, 77) == 1, "future first completion");
    expect(raz_rt_future_complete_i64(manual_future, 88) == 0, "future rejects second completion");
    expect(raz_rt_future_wait_millis(manual_future, 10) == 1, "future completed wait");
    std::int64_t value = 0;
    expect(raz_rt_future_result_i64(manual_future, &value) == 1 && value == 77, "future result");
    expect(continuation.calls.load() == 1 && continuation.value.load() == 77 && continuation.status.load() == 1,
           "future continuation runs once");
    FutureContinuationState late{};
    expect(raz_rt_future_then_i64(manual_future, reinterpret_cast<std::uintptr_t>(&future_continuation), &late) == 1,
           "late continuation registration");
    expect(late.calls.load() == 1 && late.value.load() == 77, "late continuation runs immediately");
    raz_rt_future_destroy(manual_future);
  }

  void* cancelled_future = raz_rt_future_create();
  expect(cancelled_future != nullptr, "cancelled future create");
  if (cancelled_future != nullptr) {
    FutureContinuationState continuation{};
    raz_rt_future_then_i64(cancelled_future, reinterpret_cast<std::uintptr_t>(&future_continuation), &continuation);
    expect(raz_rt_future_cancel(cancelled_future) == 1, "future cancellation");
    expect(raz_rt_future_status(cancelled_future) == -1, "future cancelled status");
    expect(raz_rt_future_wait_millis(cancelled_future, 10) == -1, "future cancelled wait");
    expect(continuation.calls.load() == 1 && continuation.status.load() == -1, "cancel continuation status");
    raz_rt_future_destroy(cancelled_future);
  }

  const std::int64_t udp_server = raz_rt_udp_bind(0);
  expect(udp_server >= 0, "UDP loopback bind");
  if (udp_server >= 0) {
    const std::int64_t udp_port = raz_rt_socket_local_port(udp_server);
    expect(udp_port > 0 && udp_port <= 65535, "UDP local port discovery");
    const std::string loopback = "127.0.0.1";
    const std::int64_t udp_client = raz_rt_udp_connect(loopback.data(), static_cast<std::int64_t>(loopback.size()), udp_port);
    expect(udp_client >= 0, "UDP connected endpoint");
    if (udp_client >= 0) {
      std::array<char, 64> local_address{};
      std::array<char, 64> peer_address{};
      expect(raz_rt_socket_local_address(udp_client, local_address.data(), static_cast<std::int64_t>(local_address.size())) > 0,
             "socket local address introspection");
      expect(raz_rt_socket_peer_address(udp_client, peer_address.data(), static_cast<std::int64_t>(peer_address.size())) > 0,
             "socket peer address introspection");
      expect(raz_rt_socket_peer_port(udp_client) == udp_port, "socket peer port introspection");
      expect(raz_rt_socket_set_keepalive(udp_client, 1) == 1, "socket keepalive option");
      expect(raz_rt_socket_set_reuse_address(udp_client, 1) == 1, "socket reuse-address option");
      expect(raz_rt_socket_set_buffer_sizes(udp_client, 32768, 32768) == 1, "socket buffer sizing");
      expect(raz_rt_socket_set_timeout_millis(udp_client, 250, 250) == 1, "socket timeout options");
      const std::string datagram = "raz-udp-loopback";
      expect(raz_rt_socket_send(udp_client, datagram.data(), static_cast<std::int64_t>(datagram.size())) ==
             static_cast<std::int64_t>(datagram.size()), "UDP datagram send");
      std::array<char, 64> datagram_buffer{};
      const auto received = raz_rt_socket_receive(udp_server, datagram_buffer.data(), static_cast<std::int64_t>(datagram_buffer.size()));
      expect(received == static_cast<std::int64_t>(datagram.size()) &&
             std::memcmp(datagram_buffer.data(), datagram.data(), datagram.size()) == 0, "UDP datagram receive");
      expect(raz_rt_socket_set_nonblocking(udp_client, 1) == 1 &&
             raz_rt_socket_set_nonblocking(udp_client, 0) == 1, "socket nonblocking control");
      raz_rt_socket_close(udp_client);
    }

    raz_rt_socket_close(udp_server);
  }

#if !defined(_WIN32)
  int shutdown_pair[2]{-1, -1};
  expect(::socketpair(AF_UNIX, SOCK_STREAM, 0, shutdown_pair) == 0, "socket shutdown pair create");
  if (shutdown_pair[0] >= 0 && shutdown_pair[1] >= 0) {
    expect(raz_rt_socket_shutdown(shutdown_pair[0], 1) == 1, "socket write shutdown");
    ::close(shutdown_pair[0]);
    ::close(shutdown_pair[1]);
  }
#endif


  const std::array<std::int64_t, 4> lhs{{1, 2, 3, 4}};
  const std::array<std::int64_t, 4> rhs{{5, 6, 7, 8}};
  std::array<std::int64_t, 4> lanes{};
  raz_rt_i64x4_add(lhs.data(), rhs.data(), lanes.data());
  expect(lanes == std::array<std::int64_t, 4>{{6, 8, 10, 12}}, "i64x4 add");
  raz_rt_i64x4_sub(rhs.data(), lhs.data(), lanes.data());
  expect(lanes == std::array<std::int64_t, 4>{{4, 4, 4, 4}}, "i64x4 subtract");
  raz_rt_i64x4_mul(lhs.data(), rhs.data(), lanes.data());
  expect(lanes == std::array<std::int64_t, 4>{{5, 12, 21, 32}}, "i64x4 multiply");
  raz_rt_i64x4_min(lhs.data(), rhs.data(), lanes.data());
  expect(lanes == lhs, "i64x4 minimum");
  raz_rt_i64x4_max(lhs.data(), rhs.data(), lanes.data());
  expect(lanes == rhs, "i64x4 maximum");
  raz_rt_i64x4_equal(lhs.data(), lhs.data(), lanes.data());
  expect(lanes == std::array<std::int64_t, 4>{{-1, -1, -1, -1}}, "i64x4 equality mask");
  expect(raz_rt_i64x4_reduce_add(lhs.data()) == 10, "i64x4 reduction");
  const std::array<double, 2> flhs{{1.5, 2.0}};
  const std::array<double, 2> frhs{{2.0, 4.0}};
  std::array<double, 2> flanes{};
  raz_rt_f64x2_add(flhs.data(), frhs.data(), flanes.data());
  expect(flanes[0] == 3.5 && flanes[1] == 6.0, "f64x2 add");
  raz_rt_f64x2_sub(frhs.data(), flhs.data(), flanes.data());
  expect(flanes[0] == 0.5 && flanes[1] == 2.0, "f64x2 subtract");
  raz_rt_f64x2_mul(flhs.data(), frhs.data(), flanes.data());
  expect(flanes[0] == 3.0 && flanes[1] == 8.0, "f64x2 multiply");
  raz_rt_f64x2_div(frhs.data(), flhs.data(), flanes.data());
  expect(flanes[0] > 1.333 && flanes[0] < 1.334 && flanes[1] == 2.0, "f64x2 divide");
  raz_rt_f64x2_min(flhs.data(), frhs.data(), flanes.data());
  expect(flanes[0] == 1.5 && flanes[1] == 2.0, "f64x2 minimum");
  raz_rt_f64x2_max(flhs.data(), frhs.data(), flanes.data());
  expect(flanes[0] == 2.0 && flanes[1] == 4.0, "f64x2 maximum");
  expect(raz_rt_f64x2_reduce_add(flhs.data()) == 3.5, "f64x2 reduction");

  if (failures == 0) std::cout << "Systems runtime capabilities passed\n";
  return failures == 0 ? 0 : 1;
}
