// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "runtime_internal.hpp"

using namespace raz::runtime_detail;

extern "C" {
void* raz_rt_async_frame_create(std::int64_t slot_count) {
  if (slot_count < 0) return nullptr;
  try {
    return new RazAsyncFrame(static_cast<std::size_t>(slot_count));
  } catch (...) {
    return nullptr;
  }
}

std::int32_t raz_rt_async_state_load(const void* handle) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  return frame == nullptr ? -1 : static_cast<std::int32_t>(frame->state.load(std::memory_order_acquire));
}

void raz_rt_async_state_store(void* handle, std::int32_t state) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (async_frame_accepts_slot_mutation(frame) && state >= 0)
    frame->state.store(static_cast<std::uint32_t>(state), std::memory_order_release);
}

std::int64_t raz_rt_async_frame_slot_count(const void* handle) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  return frame == nullptr ? 0 : static_cast<std::int64_t>(frame->slots.size());
}

std::int64_t raz_rt_async_slot_load(const void* handle, std::int64_t slot) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  if (frame == nullptr || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return 0;
  return frame->slots[static_cast<std::size_t>(slot)].value;
}

void* raz_rt_async_slot_load_ptr(const void* handle, std::int64_t slot) {
  return reinterpret_cast<void*>(raz_rt_async_slot_load(handle, slot));
}

void* raz_rt_async_slot_transfer_projected(void* handle, std::int64_t slot) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (!async_frame_accepts_slot_mutation(frame) || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return nullptr;
  auto& source = frame->slots[static_cast<std::size_t>(slot)];
  if (!source.initialized || !source.owns_bytes || source.projected_cleanup == nullptr ||
      source.lifecycle != RazAsyncSlotLifecycle::projected_armed) return nullptr;
  source.cleanup = nullptr;
  source.projected_cleanup = nullptr;
  source.projection_words.clear();
  source.projection_count = 0;
  source.projection_version = 0;
  source.lifecycle = RazAsyncSlotLifecycle::projected_transferred;
  return reinterpret_cast<void*>(source.value);
}

std::int32_t raz_rt_async_slot_lifecycle(const void* handle, std::int64_t slot) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  if (frame == nullptr || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return 0;
  return static_cast<std::int32_t>(frame->slots[static_cast<std::size_t>(slot)].lifecycle);
}

static void clear_async_slot(RazAsyncSlot& target) {
  if (target.initialized && target.projected_cleanup != nullptr) target.projected_cleanup(target.value, target.projection_words.data(), static_cast<std::int64_t>(target.projection_words.size()));
  else if (target.initialized && target.cleanup != nullptr) target.cleanup(target.value);
  if (target.initialized && target.owns_bytes && target.value != 0) {
    ::operator delete(reinterpret_cast<void*>(target.value), std::align_val_t(target.byte_alignment));
  }
  target.value = 0;
  target.cleanup = nullptr;
  target.projected_cleanup = nullptr;
  target.projection_words.clear();
  target.projection_count = 0;
  target.projection_version = 0;
  target.initialized = false;
  target.owns_bytes = false;
  target.byte_alignment = alignof(std::max_align_t);
  target.lifecycle = RazAsyncSlotLifecycle::empty;
}

void raz_rt_async_slot_store(void* handle, std::int64_t slot, std::int64_t value) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (!async_frame_accepts_slot_mutation(frame) || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return;
  auto& target = frame->slots[static_cast<std::size_t>(slot)];
  clear_async_slot(target);
  target.value = value;
  target.cleanup = nullptr;
  target.projected_cleanup = nullptr;
  target.projection_words.clear();
  target.projection_count = 0;
  target.projection_version = 0;
  target.initialized = true;
  target.lifecycle = RazAsyncSlotLifecycle::initialized;
}

void raz_rt_async_slot_store_owned(void* handle, std::int64_t slot, std::int64_t value,
                                    std::uintptr_t cleanup_address) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (!async_frame_accepts_slot_mutation(frame) || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return;
  auto& target = frame->slots[static_cast<std::size_t>(slot)];
  clear_async_slot(target);
  target.value = value;
  target.cleanup = reinterpret_cast<RazAsyncSlotCleanup>(cleanup_address);
  target.projected_cleanup = nullptr;
  target.projection_words.clear();
  target.projection_count = 0;
  target.projection_version = 0;
  target.initialized = true;
  target.lifecycle = RazAsyncSlotLifecycle::initialized;
}

void* raz_rt_async_slot_allocate_owned_bytes(void* handle, std::int64_t slot,
                                                std::int64_t size, std::int64_t alignment,
                                                std::uintptr_t cleanup_address) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (!async_frame_accepts_slot_mutation(frame) || slot < 0 || size <= 0 || alignment <= 0 ||
      static_cast<std::size_t>(slot) >= frame->slots.size()) return nullptr;
  auto& target = frame->slots[static_cast<std::size_t>(slot)];
  clear_async_slot(target);
  const auto byte_alignment = static_cast<std::size_t>(alignment);
  void* storage = ::operator new(static_cast<std::size_t>(size), std::align_val_t(byte_alignment));
  std::memset(storage, 0, static_cast<std::size_t>(size));
  target.value = reinterpret_cast<std::int64_t>(storage);
  target.cleanup = reinterpret_cast<RazAsyncSlotCleanup>(cleanup_address);
  target.initialized = true;
  target.owns_bytes = true;
  target.byte_alignment = byte_alignment;
  target.lifecycle = RazAsyncSlotLifecycle::initialized;
  return storage;
}

void* raz_rt_async_slot_allocate_projected_bytes(void* handle, std::int64_t slot,
                                                    std::int64_t size, std::int64_t alignment,
                                                    std::uintptr_t cleanup_address,
                                                    std::uint64_t projection_mask,
                                                    std::int64_t projection_count) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (!async_frame_accepts_slot_mutation(frame) || slot < 0 || size <= 0 || alignment <= 0 || projection_count <= 0 ||
      static_cast<std::size_t>(slot) >= frame->slots.size()) return nullptr;
  auto& target = frame->slots[static_cast<std::size_t>(slot)];
  clear_async_slot(target);
  const auto byte_alignment = static_cast<std::size_t>(alignment);
  void* storage = ::operator new(static_cast<std::size_t>(size), std::align_val_t(byte_alignment));
  std::memset(storage, 0, static_cast<std::size_t>(size));
  target.value = reinterpret_cast<std::int64_t>(storage);
  target.cleanup = nullptr;
  target.projected_cleanup = reinterpret_cast<RazAsyncProjectedSlotCleanup>(cleanup_address);
  target.projection_count = static_cast<std::uint32_t>(projection_count);
  target.projection_version = 0;
  const auto word_count = static_cast<std::size_t>((projection_count + 63) / 64);
  target.projection_words.assign(word_count, projection_mask);
  if (!target.projection_words.empty() && projection_count % 64 != 0) {
    target.projection_words.back() &= (std::uint64_t{1} << (projection_count % 64)) - 1;
  }
  target.initialized = true;
  target.owns_bytes = true;
  target.byte_alignment = byte_alignment;
  target.lifecycle = RazAsyncSlotLifecycle::projected_armed;
  return storage;
}

namespace {
std::int64_t update_async_projection_range(RazAsyncSlot& target,
                                           std::int64_t first_projection,
                                           std::int64_t projection_count,
                                           bool initialized) {
  if (target.lifecycle != RazAsyncSlotLifecycle::projected_armed ||
      first_projection < 0 || projection_count <= 0 || first_projection >= target.projection_count) return 0;
  const auto bounded_end = std::min<std::int64_t>(
      static_cast<std::int64_t>(target.projection_count), first_projection + projection_count);
  auto cursor = first_projection;
  std::int64_t changed = 0;
  while (cursor < bounded_end) {
    const auto word_index = static_cast<std::size_t>(cursor / 64);
    if (word_index >= target.projection_words.size()) return changed;
    const auto bit_index = static_cast<unsigned>(cursor % 64);
    const auto remaining_in_word = static_cast<std::int64_t>(64 - bit_index);
    const auto span = std::min(remaining_in_word, bounded_end - cursor);
    const auto low_mask = span == 64 ? ~std::uint64_t{0} : ((std::uint64_t{1} << span) - 1);
    const auto mask = low_mask << bit_index;
    const auto before = target.projection_words[word_index];
    const auto after = initialized ? (before | mask) : (before & ~mask);
    changed += static_cast<std::int64_t>(std::popcount((before ^ after) & mask));
    target.projection_words[word_index] = after;
    cursor += span;
  }
  return changed;
}
}

void raz_rt_async_slot_projection_range_set(void* handle, std::int64_t slot,
                                             std::int64_t first_projection,
                                             std::int64_t projection_count,
                                             bool initialized) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (!async_frame_accepts_slot_mutation(frame) || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return;
  auto& target = frame->slots[static_cast<std::size_t>(slot)];
  if (update_async_projection_range(target, first_projection, projection_count, initialized) != 0)
    ++target.projection_version;
}

void raz_rt_async_slot_projection_set(void* handle, std::int64_t slot,
                                       std::int64_t projection, bool initialized) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (!async_frame_accepts_slot_mutation(frame) || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return;
  auto& target = frame->slots[static_cast<std::size_t>(slot)];
  if (update_async_projection_range(target, projection, 1, initialized) != 0)
    ++target.projection_version;
}

std::uint64_t raz_rt_async_slot_projection_version(const void* handle, std::int64_t slot) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  if (frame == nullptr || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return 0;
  return frame->slots[static_cast<std::size_t>(slot)].projection_version;
}

std::int64_t raz_rt_async_slot_projection_count(const void* handle, std::int64_t slot) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  if (frame == nullptr || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return 0;
  return static_cast<std::int64_t>(frame->slots[static_cast<std::size_t>(slot)].projection_count);
}

std::int64_t raz_rt_async_slot_projection_word_count(const void* handle, std::int64_t slot) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  if (frame == nullptr || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return 0;
  return static_cast<std::int64_t>(frame->slots[static_cast<std::size_t>(slot)].projection_words.size());
}

std::uint64_t raz_rt_async_slot_projection_word(const void* handle, std::int64_t slot,
                                                  std::int64_t word_index) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  if (frame == nullptr || slot < 0 || word_index < 0 ||
      static_cast<std::size_t>(slot) >= frame->slots.size()) return 0;
  const auto& words = frame->slots[static_cast<std::size_t>(slot)].projection_words;
  return static_cast<std::size_t>(word_index) < words.size() ? words[static_cast<std::size_t>(word_index)] : 0;
}

std::int64_t raz_rt_async_slot_projection_copy_words(const void* handle, std::int64_t slot,
                                                       std::uint64_t* destination,
                                                       std::int64_t capacity) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  if (frame == nullptr || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return 0;
  const auto& words = frame->slots[static_cast<std::size_t>(slot)].projection_words;
  const auto required = static_cast<std::int64_t>(words.size());
  if (destination == nullptr || capacity <= 0) return required;
  const auto copied = std::min(required, capacity);
  std::copy_n(words.begin(), static_cast<std::size_t>(copied), destination);
  return required;
}

std::int64_t raz_rt_async_slot_projection_run_count(const void* handle, std::int64_t slot) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  if (frame == nullptr || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return 0;
  const auto& target = frame->slots[static_cast<std::size_t>(slot)];
  if (target.projection_count == 0 || target.projection_words.empty()) return 0;
  std::int64_t runs = 1;
  bool previous = (target.projection_words.front() & 1) != 0;
  for (std::uint32_t projection = 1; projection < target.projection_count; ++projection) {
    const auto word = static_cast<std::size_t>(projection / 64);
    const auto bit = std::uint64_t{1} << static_cast<unsigned>(projection % 64);
    const bool current = (target.projection_words[word] & bit) != 0;
    if (current != previous) { ++runs; previous = current; }
  }
  return runs;
}

std::int64_t raz_rt_async_slot_projection_copy_runs(const void* handle, std::int64_t slot,
                                                      std::int64_t* first_projections,
                                                      std::int64_t* projection_counts,
                                                      std::uint8_t* initialized,
                                                      std::int64_t capacity) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  if (frame == nullptr || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return 0;
  const auto& target = frame->slots[static_cast<std::size_t>(slot)];
  if (target.projection_count == 0 || target.projection_words.empty()) return 0;
  const auto required = raz_rt_async_slot_projection_run_count(handle, slot);
  if (first_projections == nullptr || projection_counts == nullptr || initialized == nullptr || capacity <= 0)
    return required;
  std::int64_t emitted = 0;
  std::int64_t run_first = 0;
  bool run_state = (target.projection_words.front() & 1) != 0;
  for (std::uint32_t projection = 1; projection <= target.projection_count; ++projection) {
    const bool boundary = projection == target.projection_count;
    bool current = run_state;
    if (!boundary) {
      const auto word = static_cast<std::size_t>(projection / 64);
      const auto bit = std::uint64_t{1} << static_cast<unsigned>(projection % 64);
      current = (target.projection_words[word] & bit) != 0;
    }
    if (boundary || current != run_state) {
      if (emitted < capacity) {
        first_projections[emitted] = run_first;
        projection_counts[emitted] = static_cast<std::int64_t>(projection) - run_first;
        initialized[emitted] = run_state ? 1 : 0;
      }
      ++emitted;
      run_first = projection;
      run_state = current;
    }
  }
  return required;
}

std::int64_t raz_rt_async_slot_projection_batch_set(void* handle, std::int64_t slot,
                                                      const std::int64_t* first_projections,
                                                      const std::int64_t* projection_counts,
                                                      const std::uint8_t* initialized,
                                                      std::int64_t update_count) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (!async_frame_accepts_slot_mutation(frame) || slot < 0 || update_count < 0 ||
      static_cast<std::size_t>(slot) >= frame->slots.size()) return 0;
  if (update_count == 0) return 0;
  if (first_projections == nullptr || projection_counts == nullptr || initialized == nullptr) return 0;
  auto& target = frame->slots[static_cast<std::size_t>(slot)];
  for (std::int64_t index = 0; index < update_count; ++index) {
    const auto first = first_projections[index];
    const auto count = projection_counts[index];
    if (first < 0 || count <= 0 || first >= target.projection_count ||
        count > static_cast<std::int64_t>(target.projection_count) - first) return 0;
  }
  std::int64_t changed = 0;
  for (std::int64_t index = 0; index < update_count; ++index)
    changed += update_async_projection_range(target, first_projections[index],
                                             projection_counts[index], initialized[index] != 0);
  if (changed != 0) ++target.projection_version;
  return changed;
}

std::uint64_t raz_rt_async_slot_projection_mask(const void* handle, std::int64_t slot) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  if (frame == nullptr || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return 0;
  const auto& words = frame->slots[static_cast<std::size_t>(slot)].projection_words;
  return words.empty() ? 0 : words.front();
}

void raz_rt_async_slot_store_owned_bytes(void* handle, std::int64_t slot, const void* source,
                                          std::int64_t size, std::int64_t alignment,
                                          std::uintptr_t cleanup_address) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (!async_frame_accepts_slot_mutation(frame) || source == nullptr || slot < 0 || size <= 0 || alignment <= 0 ||
      static_cast<std::size_t>(slot) >= frame->slots.size()) return;
  auto& target = frame->slots[static_cast<std::size_t>(slot)];
  clear_async_slot(target);
  const auto byte_alignment = static_cast<std::size_t>(alignment);
  void* storage = ::operator new(static_cast<std::size_t>(size), std::align_val_t(byte_alignment));
  std::memcpy(storage, source, static_cast<std::size_t>(size));
  target.value = reinterpret_cast<std::int64_t>(storage);
  target.cleanup = reinterpret_cast<RazAsyncSlotCleanup>(cleanup_address);
  target.initialized = true;
  target.owns_bytes = true;
  target.byte_alignment = byte_alignment;
  target.lifecycle = RazAsyncSlotLifecycle::initialized;
}

std::int64_t raz_rt_async_slot_take(void* handle, std::int64_t slot) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (!async_frame_accepts_slot_mutation(frame) || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return 0;
  auto& source = frame->slots[static_cast<std::size_t>(slot)];
  const auto value = source.value;
  source.value = 0;
  source.cleanup = nullptr;
  source.projected_cleanup = nullptr;
  source.projection_words.clear();
  source.projection_count = 0;
  source.projection_version = 0;
  source.initialized = false;
  source.owns_bytes = false;
  source.byte_alignment = alignof(std::max_align_t);
  source.lifecycle = RazAsyncSlotLifecycle::empty;
  return value;
}

void raz_rt_async_slot_disarm(void* handle, std::int64_t slot) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (!async_frame_accepts_slot_mutation(frame) || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return;
  auto& source = frame->slots[static_cast<std::size_t>(slot)];
  source.cleanup = nullptr;
  source.projected_cleanup = nullptr;
  source.projection_words.clear();
  source.projection_count = 0;
  source.projection_version = 0;
  if (source.owns_bytes && source.initialized) source.lifecycle = RazAsyncSlotLifecycle::projected_transferred;
}

void* raz_rt_async_slot_address(void* handle, std::int64_t slot) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (!async_frame_accepts_slot_mutation(frame) || slot < 0 || static_cast<std::size_t>(slot) >= frame->slots.size()) return nullptr;
  return &frame->slots[static_cast<std::size_t>(slot)].value;
}

std::int64_t raz_rt_async_result_load(const void* handle) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  return frame == nullptr ? 0 : frame->result.load(std::memory_order_acquire);
}

void raz_rt_async_result_store(void* handle, std::int64_t value) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (frame == nullptr) return;
  // Atomically claim completion intent before waiting for the poll gate. The
  // first terminal requester wins independently of mutex scheduling, and a
  // synchronous wakeup chain yields between passes as soon as intent exists.
  auto expected_intent = RazAsyncTerminalIntent::none;
  if (!frame->terminal_intent.compare_exchange_strong(expected_intent, RazAsyncTerminalIntent::complete,
                                                       std::memory_order_acq_rel, std::memory_order_acquire)) return;
  std::lock_guard poll_lock(frame->poll_mutex);
  auto expected = RazAsyncFrameLifecycle::active;
  if (!frame->lifecycle.compare_exchange_strong(expected, RazAsyncFrameLifecycle::completing,
                                                 std::memory_order_acq_rel, std::memory_order_acquire)) return;
  // Completion-future continuations run synchronously and may release the last
  // external frame reference. Keep terminal publication alive until the final
  // lifecycle state has been stored.
  RazAsyncFrameRetainGuard publication_guard(frame);
  frame->result.store(value, std::memory_order_release);
  invalidate_async_await(frame);
  if (frame->completion != nullptr) raz_rt_future_complete_i64(frame->completion, value);
  // Publish the terminal lifecycle last. An acquire observation of `completed`
  // therefore guarantees that the result and completion future are settled.
  frame->lifecycle.store(RazAsyncFrameLifecycle::completed, std::memory_order_release);
}

std::int64_t raz_rt_async_cancel_requested(const void* handle) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  if (frame == nullptr) return 1;
  if (frame->cancelled.load(std::memory_order_acquire)) return 1;
  return frame->completion != nullptr && raz_rt_future_status(frame->completion) < 0 ? 1 : 0;
}

void raz_rt_async_frame_cancel(void* handle) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (frame == nullptr) return;
  // Atomically claim cancellation intent before blocking on an active poll
  // callback. Competing completion requests lose immediately, while the poll
  // trampoline yields between passes as soon as this intent is visible.
  auto expected_intent = RazAsyncTerminalIntent::none;
  if (!frame->terminal_intent.compare_exchange_strong(expected_intent, RazAsyncTerminalIntent::cancel,
                                                       std::memory_order_acq_rel, std::memory_order_acquire)) return;
  std::lock_guard poll_lock(frame->poll_mutex);
  auto expected = RazAsyncFrameLifecycle::active;
  if (!frame->lifecycle.compare_exchange_strong(expected, RazAsyncFrameLifecycle::cancelling,
                                                 std::memory_order_acq_rel, std::memory_order_acquire)) return;
  // Cancellation-future continuations are synchronous as well. Retain the
  // frame through the complete terminal commit so a callback may destroy its
  // public handle without invalidating this transition.
  RazAsyncFrameRetainGuard publication_guard(frame);
  frame->cancelled.store(true, std::memory_order_release);
  invalidate_async_await(frame);
  if (frame->completion != nullptr) raz_rt_future_cancel(frame->completion);
  // Publish the terminal lifecycle last so observers never see `cancelled`
  // while the completion future is still pending.
  frame->lifecycle.store(RazAsyncFrameLifecycle::cancelled, std::memory_order_release);
}

std::int32_t raz_rt_async_frame_lifecycle(const void* handle) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  return frame == nullptr ? static_cast<std::int32_t>(RazAsyncFrameLifecycle::destroying)
                          : static_cast<std::int32_t>(frame->lifecycle.load(std::memory_order_acquire));
}

std::int32_t raz_rt_async_frame_terminal_intent(const void* handle) {
  const auto* frame = static_cast<const RazAsyncFrame*>(handle);
  return frame == nullptr ? static_cast<std::int32_t>(RazAsyncTerminalIntent::none)
                          : static_cast<std::int32_t>(frame->terminal_intent.load(std::memory_order_acquire));
}

void* raz_rt_async_frame_future(void* handle) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (frame == nullptr || frame->completion == nullptr) return nullptr;
  retain_future(frame->completion);
  return frame->completion;
}

void raz_rt_async_frame_set_poller(void* handle, void* callback) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (frame == nullptr) return;
  std::lock_guard poll_lock(frame->poll_mutex);
  if (async_frame_accepts_slot_mutation(frame))
    frame->poll_callback.store(reinterpret_cast<std::uintptr_t>(callback), std::memory_order_release);
}

std::int64_t raz_rt_async_frame_future_i64(void* handle) {
  return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(raz_rt_async_frame_future(handle)));
}

std::int32_t raz_rt_async_await_poll(void* frame_handle, void* future_handle, std::int32_t resume_state) {
  auto* frame = static_cast<RazAsyncFrame*>(frame_handle);
  auto* future = static_cast<RazFuture*>(future_handle);
  if (frame == nullptr || future == nullptr || resume_state < 0) return -1;
  std::lock_guard poll_lock(frame->poll_mutex);
  if (!async_frame_accepts_slot_mutation(frame)) return -1;
  std::int64_t value = 0;
  const auto status = raz_rt_future_result_i64(future, &value);
  if (status != 0) {
    // A ready await supersedes and physically detaches any older pending
    // callback before publishing its result.
    invalidate_async_await(frame);
    {
      std::lock_guard lock(frame->await_mutex);
      frame->awaited_result = value;
      frame->awaited_status = status;
      if (status < 0) frame->cancelled.store(true, std::memory_order_release);
      frame->state.store(static_cast<std::uint32_t>(resume_state), std::memory_order_release);
    }
    return static_cast<std::int32_t>(status);
  }
  {
    std::lock_guard lock(frame->await_mutex);
    if (frame->lifecycle.load(std::memory_order_acquire) != RazAsyncFrameLifecycle::active) return -1;
    if (frame->await_registered && frame->awaited == future &&
        frame->awaited_resume_state == static_cast<std::uint32_t>(resume_state)) return 0;
  }

  invalidate_async_await(frame);
  auto context = std::make_unique<RazAsyncAwaitContext>();
  context->frame = frame;
  context->future = future;
  context->resume_state = static_cast<std::uint32_t>(resume_state);
  auto* raw_context = context.get();
  {
    std::lock_guard lock(frame->await_mutex);
    if (frame->lifecycle.load(std::memory_order_acquire) != RazAsyncFrameLifecycle::active) return -1;
    frame->awaited = future;
    retain_future(future);
    frame->awaited_resume_state = context->resume_state;
    frame->awaited_status = 0;
    frame->await_registered = true;
    frame->await_context = raw_context;
    context->generation = ++frame->await_generation;
    retain_async_frame(frame);
  }
  const auto registration_generation = context->generation;
  context.release();
  const auto continuation_id = register_future_continuation(
      future, &async_await_continuation, raw_context);
  if (continuation_id == 0) {
    // Ready completion invoked the callback synchronously and consumed context.
    return 0;
  }
  {
    std::lock_guard lock(frame->await_mutex);
    if (frame->await_registered && frame->await_context == raw_context &&
        frame->await_generation == registration_generation) {
      frame->await_continuation_id = continuation_id;
      return 0;
    }
  }
  // The callback won the race after queueing but before token publication. It
  // owns the context; no additional cleanup is required here.
  return 0;
}

std::int64_t raz_rt_async_await_result(void* handle) {
  auto* frame = static_cast<RazAsyncFrame*>(handle);
  if (frame == nullptr) return 0;
  std::lock_guard lock(frame->await_mutex);
  return frame->awaited_result;
}

std::int32_t raz_rt_async_frame_has_pending_await(const void* handle) {
  auto* frame = const_cast<RazAsyncFrame*>(static_cast<const RazAsyncFrame*>(handle));
  if (frame == nullptr) return 0;
  std::lock_guard lock(frame->await_mutex);
  return frame->await_registered && frame->awaited != nullptr ? 1 : 0;
}

std::uint64_t raz_rt_async_frame_await_generation(const void* handle) {
  auto* frame = const_cast<RazAsyncFrame*>(static_cast<const RazAsyncFrame*>(handle));
  if (frame == nullptr) return 0;
  std::lock_guard lock(frame->await_mutex);
  return frame->await_generation;
}

void raz_rt_async_frame_destroy(void* handle) {
  release_async_frame(static_cast<RazAsyncFrame*>(handle));
}


}
