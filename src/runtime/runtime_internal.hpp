// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32)
// Windows SDK headers define min/max macros unless NOMINMAX is visible before
// the first transitive Windows include. OpenSSL can include Windows headers, so
// this must precede both the C++/OpenSSL headers and our direct Windows includes.
#ifndef NOMINMAX
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#endif

#include <bit>
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <charconv>
#include <cmath>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <new>
#include <string>
#include <thread>
#include <condition_variable>
#include <deque>
#include <memory>
#include <limits>
#include <queue>
#include <random>
#include <vector>
#include <unordered_map>
#include <system_error>

#if defined(RAZ_HAVE_OPENSSL)
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509_vfy.h>
#include <openssl/evp.h>
#endif

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <direct.h>
#include <io.h>
// Be defensive against third-party/SDK headers that define these despite
// NOMINMAX. Standard-library calls below must never be macro-expanded.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <poll.h>
#if defined(__linux__)
#include <sched.h>
#endif
#if defined(__linux__)
#include <sys/random.h>
#endif
#include <unistd.h>
#include <sys/wait.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#endif
#endif

namespace raz::runtime_detail {
struct RazDirectoryIterator {
  std::filesystem::directory_iterator current{};
  std::filesystem::directory_iterator end{};
  std::error_code error{};
};

inline thread_local std::int64_t raz_last_error_code = 0;

inline void raz_clear_last_error() { raz_last_error_code = 0; }
inline void raz_set_last_error(std::int64_t code) { raz_last_error_code = code == 0 ? -1 : code; }
inline void raz_set_errno_error() { raz_set_last_error(errno == 0 ? -1 : static_cast<std::int64_t>(errno)); }
inline void raz_set_socket_error() {
#if defined(_WIN32)
  raz_set_last_error(static_cast<std::int64_t>(WSAGetLastError()));
#else
  raz_set_errno_error();
#endif
}

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
struct WinsockState {
  WinsockState() { WSADATA data{}; ok = WSAStartup(MAKEWORD(2, 2), &data) == 0; }
  ~WinsockState() { if (ok) WSACleanup(); }
  bool ok{false};
};
inline WinsockState& winsock() { static WinsockState state; return state; }
inline void close_socket(Socket socket) { if (socket != invalid_socket) closesocket(socket); }
#else
using Socket = int;
constexpr Socket invalid_socket = -1;
inline void close_socket(Socket socket) { if (socket != invalid_socket) ::close(socket); }
#endif

inline std::int64_t copy_text(const std::string& text, char* output, std::int64_t capacity) {
  const auto required = static_cast<std::int64_t>(text.size());
  if (output == nullptr || capacity <= 0) return required;
  const auto count = required < capacity - 1 ? required : capacity - 1;
  if (count > 0) std::memcpy(output, text.data(), static_cast<std::size_t>(count));
  output[count] = '\0';
  return required;
}

inline std::vector<std::string> load_process_arguments() {
  std::vector<std::string> arguments;
#if defined(_WIN32)
  const wchar_t* cursor = GetCommandLineW();
  if (cursor == nullptr) return arguments;
  while (*cursor != L'\0') {
    while (*cursor == L' ' || *cursor == L'\t') ++cursor;
    if (*cursor == L'\0') break;
    std::wstring wide;
    bool quoted = false;
    while (*cursor != L'\0' && (quoted || (*cursor != L' ' && *cursor != L'\t'))) {
      std::size_t backslashes = 0;
      while (*cursor == L'\\') {
        ++backslashes;
        ++cursor;
      }
      if (*cursor == L'"') {
        wide.append(backslashes / 2, L'\\');
        if ((backslashes & 1U) != 0U) {
          wide.push_back(L'"');
        } else {
          quoted = !quoted;
        }
        ++cursor;
        continue;
      }
      wide.append(backslashes, L'\\');
      if (*cursor != L'\0') wide.push_back(*cursor++);
    }
    if (wide.empty()) {
      arguments.emplace_back();
    } else {
      const int required = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
      if (required <= 0) {
        arguments.emplace_back();
      } else {
        std::string utf8(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), required, nullptr, nullptr);
        arguments.push_back(std::move(utf8));
      }
    }
  }
#elif defined(__APPLE__)
  const int count = *_NSGetArgc();
  auto** values = *_NSGetArgv();
  if (values == nullptr || count <= 0) return arguments;
  arguments.reserve(static_cast<std::size_t>(count));
  for (int index = 0; index < count; ++index) arguments.emplace_back(values[index] == nullptr ? "" : values[index]);
#elif defined(__linux__)
  std::ifstream input("/proc/self/cmdline", std::ios::binary);
  if (!input) return arguments;
  std::string current;
  char byte = 0;
  while (input.get(byte)) {
    if (byte == '\0') {
      arguments.push_back(std::move(current));
      current.clear();
    } else {
      current.push_back(byte);
    }
  }

  if (!current.empty()) arguments.push_back(std::move(current));
#endif
  return arguments;
}

inline const std::vector<std::string>& process_arguments() {
  static const std::vector<std::string> arguments = load_process_arguments();
  return arguments;
}

struct RazTask final {
  std::thread worker;
  std::mutex mutex;
  std::condition_variable ready;
  bool complete = false;
  std::int64_t result = 0;
};
using RazTaskEntry = std::int64_t (*)(void*);
using RazFutureContinuation = void (*)(void*, std::int64_t, std::int64_t);
extern "C" void* raz_rt_future_create();
extern "C" std::int64_t raz_rt_future_complete_i64(void* handle, std::int64_t value);
extern "C" std::int64_t raz_rt_future_status(void* handle);
extern "C" std::int64_t raz_rt_future_cancel(void* handle);
extern "C" std::int64_t raz_rt_future_then_i64(void* handle, std::uintptr_t continuation_address, void* context);
extern "C" std::int64_t raz_rt_future_result_i64(void* handle, std::int64_t* output);
extern "C" void raz_rt_future_destroy(void* handle);
extern "C" std::int32_t raz_rt_callable_invoke_erased(void*, std::uint64_t, const void*, std::uint64_t, void*, std::uint64_t);
extern "C" void raz_rt_callable_destroy_erased(void*);
extern "C" std::uint64_t raz_rt_callable_signature_id(const void*);
extern "C" std::uint64_t raz_rt_callable_argument_size(const void*);
extern "C" std::uint64_t raz_rt_callable_result_size(const void*);

struct RazFutureContinuationEntry final {
  std::uint64_t id = 0;
  RazFutureContinuation callback = nullptr;
  void* context = nullptr;
};

struct RazFuture final {
  std::atomic<std::int64_t> references{1};
  std::mutex mutex;
  std::condition_variable ready;
  bool complete = false;
  bool cancelled = false;
  std::int64_t result = 0;
  std::vector<RazFutureContinuationEntry> continuations;
  std::uint64_t next_continuation_id = 1;
};

struct RazCondition final { std::condition_variable condition; };
struct RazRwLock final { std::shared_mutex mutex; };
bool finish_future(RazFuture* future, std::int64_t value, bool cancelled);

inline void retain_future(RazFuture* future) {
  if (future != nullptr) future->references.fetch_add(1, std::memory_order_relaxed);
}

inline void release_future(RazFuture* future) {
  if (future == nullptr || future->references.fetch_sub(1, std::memory_order_acq_rel) != 1) return;
  {
    std::unique_lock lock(future->mutex);
    future->ready.wait(lock, [future] { return future->complete || future->cancelled; });
  }
  delete future;
}

inline std::uint64_t register_future_continuation(RazFuture* future, RazFutureContinuation continuation, void* context) {
  if (future == nullptr || continuation == nullptr) return 0;
  std::int64_t value = 0;
  std::int64_t status = 0;
  {
    std::lock_guard lock(future->mutex);
    if (!future->complete && !future->cancelled) {
      const auto id = future->next_continuation_id++;
      future->continuations.push_back(RazFutureContinuationEntry{id, continuation, context});
      return id;
    }
    value = future->result;
    status = future->cancelled ? -1 : 1;
  }

  continuation(context, value, status);
  return 0;
}

inline bool remove_future_continuation(RazFuture* future, std::uint64_t id, void** context) {
  if (future == nullptr || id == 0) return false;
  std::lock_guard lock(future->mutex);
  const auto iterator = std::find_if(future->continuations.begin(), future->continuations.end(),
                                     [id](const RazFutureContinuationEntry& entry) { return entry.id == id; });
  if (iterator == future->continuations.end()) return false;
  if (context != nullptr) *context = iterator->context;
  future->continuations.erase(iterator);
  return true;
}

inline bool finish_future(RazFuture* future, std::int64_t value, bool cancelled) {
  std::vector<RazFutureContinuationEntry> continuations;
  {
    std::lock_guard lock(future->mutex);
    if (future->complete || future->cancelled) return false;
    future->result = value;
    future->cancelled = cancelled;
    future->complete = !cancelled;
    continuations.swap(future->continuations);
  }
  future->ready.notify_all();
  const std::int64_t status = cancelled ? -1 : 1;
  for (const auto& continuation : continuations) {
    if (continuation.callback != nullptr) continuation.callback(continuation.context, value, status);
  }
  return true;
}

using RazAsyncSlotCleanup = void (*)(std::int64_t);
using RazAsyncProjectedSlotCleanup = void (*)(std::int64_t, const std::uint64_t*, std::int64_t);

enum class RazAsyncSlotLifecycle : std::uint8_t { empty = 0, initialized = 1, projected_armed = 2, projected_transferred = 3 };
enum class RazAsyncFrameLifecycle : std::uint8_t {
  active = 0,
  completed = 1,
  cancelled = 2,
  destroying = 3,
  completing = 4,
  cancelling = 5,
};
enum class RazAsyncTerminalIntent : std::uint8_t { none = 0, complete = 1, cancel = 2 };

struct RazAsyncSlot final {
  std::int64_t value = 0;
  RazAsyncSlotCleanup cleanup = nullptr;
  RazAsyncProjectedSlotCleanup projected_cleanup = nullptr;
  std::vector<std::uint64_t> projection_words;
  std::uint32_t projection_count = 0;
  std::uint64_t projection_version = 0;
  bool initialized = false;
  bool owns_bytes = false;
  std::size_t byte_alignment = alignof(std::max_align_t);
  RazAsyncSlotLifecycle lifecycle = RazAsyncSlotLifecycle::empty;
};

struct RazAsyncFrame;
struct RazAsyncAwaitContext final {
  RazAsyncFrame* frame = nullptr;
  RazFuture* future = nullptr;
  std::uint32_t resume_state = 0;
  std::uint64_t generation = 0;
};

struct RazAsyncFrame final {
  std::atomic<std::uint32_t> references{1};
  std::atomic<std::uint32_t> state{0};
  std::atomic<std::int64_t> result{0};
  std::atomic<bool> cancelled{false};
  std::atomic<RazAsyncFrameLifecycle> lifecycle{RazAsyncFrameLifecycle::active};
  std::recursive_mutex poll_mutex;
  bool poll_running = false;
  bool poll_requested = false;
  std::atomic<RazAsyncTerminalIntent> terminal_intent{RazAsyncTerminalIntent::none};
  std::mutex await_mutex;
  RazFuture* awaited = nullptr;
  std::int64_t awaited_result = 0;
  std::int64_t awaited_status = 0;
  std::uint32_t awaited_resume_state = 0;
  std::uint64_t await_generation = 0;
  std::uint64_t await_continuation_id = 0;
  RazAsyncAwaitContext* await_context = nullptr;
  bool await_registered = false;
  std::atomic<std::uintptr_t> poll_callback{0};
  std::vector<RazAsyncSlot> slots;
  RazFuture* completion = nullptr;

  explicit RazAsyncFrame(std::size_t slot_count) : slots(slot_count) {
    completion = static_cast<RazFuture*>(raz_rt_future_create());
  }

  ~RazAsyncFrame() {
    lifecycle.store(RazAsyncFrameLifecycle::destroying, std::memory_order_release);
    for (auto slot = slots.rbegin(); slot != slots.rend(); ++slot) {
      if (slot->initialized && slot->projected_cleanup != nullptr) slot->projected_cleanup(slot->value, slot->projection_words.data(), static_cast<std::int64_t>(slot->projection_words.size()));
      else if (slot->initialized && slot->cleanup != nullptr) slot->cleanup(slot->value);
      if (slot->initialized && slot->owns_bytes && slot->value != 0) {
        ::operator delete(reinterpret_cast<void*>(slot->value), std::align_val_t(slot->byte_alignment));
      }
      slot->initialized = false;
      slot->cleanup = nullptr;
      slot->projected_cleanup = nullptr;
      slot->projection_words.clear();
      slot->projection_count = 0;
      slot->projection_version = 0;
      slot->owns_bytes = false;
      slot->lifecycle = RazAsyncSlotLifecycle::empty;
    }
    if (awaited != nullptr) release_future(awaited);
    if (completion != nullptr) {
      raz_rt_future_cancel(completion);
      release_future(completion);
    }
  }
};

inline void retain_async_frame(RazAsyncFrame* frame) {
  if (frame != nullptr) frame->references.fetch_add(1, std::memory_order_relaxed);
}

inline void release_async_frame(RazAsyncFrame* frame) {
  if (frame != nullptr && frame->references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete frame;
}

struct RazAsyncFrameRetainGuard final {
  RazAsyncFrame* frame = nullptr;
  explicit RazAsyncFrameRetainGuard(RazAsyncFrame* value) : frame(value) { retain_async_frame(frame); }
  ~RazAsyncFrameRetainGuard() { release_async_frame(frame); }
  RazAsyncFrameRetainGuard(const RazAsyncFrameRetainGuard&) = delete;
  RazAsyncFrameRetainGuard& operator=(const RazAsyncFrameRetainGuard&) = delete;
};

inline bool async_frame_accepts_slot_mutation(const RazAsyncFrame* frame) {
  return frame != nullptr && frame->lifecycle.load(std::memory_order_acquire) == RazAsyncFrameLifecycle::active;
}

inline void invalidate_async_await(RazAsyncFrame* frame) {
  if (frame == nullptr) return;
  RazFuture* awaited = nullptr;
  RazAsyncAwaitContext* context = nullptr;
  std::uint64_t continuation_id = 0;
  {
    std::lock_guard lock(frame->await_mutex);
    ++frame->await_generation;
    frame->await_registered = false;
    awaited = frame->awaited;
    context = frame->await_context;
    continuation_id = frame->await_continuation_id;
    frame->awaited = nullptr;
    frame->awaited_resume_state = 0;
    frame->await_context = nullptr;
    frame->await_continuation_id = 0;
  }
  // If the continuation is still queued, detach it and release the frame retain
  // immediately. If completion already claimed the continuation, its callback
  // owns the context and will release the retain after generation validation.
  void* removed_context = nullptr;
  if (awaited != nullptr && continuation_id != 0 &&
      remove_future_continuation(awaited, continuation_id, &removed_context)) {
    auto* removed = static_cast<RazAsyncAwaitContext*>(removed_context);
    delete removed;
    release_async_frame(frame);
  } else {
    (void)context;
  }

  if (awaited != nullptr) release_future(awaited);
}

inline void request_async_poll(RazAsyncFrame* frame, std::unique_lock<std::recursive_mutex>& poll_lock) {
  if (frame == nullptr) return;
  // The caller holds poll_mutex. Nested continuations only enqueue another pass;
  // the outermost poll invocation drains requests iteratively so synchronous
  // completion chains cannot recurse without bound.
  frame->poll_requested = true;
  if (frame->poll_running) return;
  frame->poll_running = true;
  struct PollDrainGuard final {
    RazAsyncFrame* frame;
    ~PollDrainGuard() {
      frame->poll_running = false;
      frame->poll_requested = false;
    }
  } guard{frame};
  constexpr std::uint32_t poll_quantum = 64;
  std::uint32_t passes = 0;
  while (frame->poll_requested &&
         frame->terminal_intent.load(std::memory_order_acquire) == RazAsyncTerminalIntent::none &&
         frame->lifecycle.load(std::memory_order_acquire) == RazAsyncFrameLifecycle::active) {
    frame->poll_requested = false;
    const auto callback_address = frame->poll_callback.load(std::memory_order_acquire);
    if (callback_address != 0) {
      const auto callback = reinterpret_cast<std::int32_t (*)(void*)>(callback_address);
      (void)callback(frame);
    }
    ++passes;
    if (passes >= poll_quantum && frame->poll_requested &&
        frame->terminal_intent.load(std::memory_order_acquire) == RazAsyncTerminalIntent::none) {
      // Give competing completion/cancellation requesters a bounded opportunity
      // to acquire the terminal gate even when a poller continuously schedules
      // synchronous wakeups. Re-check lifecycle and intent after reacquiring.
      passes = 0;
      poll_lock.unlock();
      std::this_thread::yield();
      poll_lock.lock();
    }
  }
}

inline void async_await_continuation(void* raw_context, std::int64_t value, std::int64_t status) {
  std::unique_ptr<RazAsyncAwaitContext> context(static_cast<RazAsyncAwaitContext*>(raw_context));
  auto* frame = context == nullptr ? nullptr : context->frame;
  if (frame == nullptr) return;
  {
    // Poll callbacks and terminal frame transitions share one recursive gate. This
    // closes the race where cancellation wins after generation validation but
    // before the callback starts, while still allowing a poller to complete its
    // own frame reentrantly.
    std::unique_lock poll_lock(frame->poll_mutex);
    bool resume = false;
    {
      std::lock_guard await_lock(frame->await_mutex);
      const bool current = frame->lifecycle.load(std::memory_order_acquire) == RazAsyncFrameLifecycle::active &&
          frame->await_registered && frame->awaited == context->future &&
          frame->await_generation == context->generation &&
          frame->awaited_resume_state == context->resume_state;
      if (current) {
        frame->awaited_result = value;
        frame->awaited_status = status;
        frame->await_registered = false;
        frame->await_continuation_id = 0;
        frame->await_context = nullptr;
        // The accepted callback consumes the frame's retained awaited-future
        // reference immediately. Keeping it until the next await or frame
        // destruction unnecessarily retains completed future graphs.
        frame->awaited = nullptr;
        frame->awaited_resume_state = 0;
        if (status < 0) frame->cancelled.store(true, std::memory_order_release);
        frame->state.store(context->resume_state, std::memory_order_release);
        resume = true;
      }
    }
    if (resume) {
      release_future(context->future);
      request_async_poll(frame, poll_lock);
    }
  }

  release_async_frame(frame);
}

inline std::string view_text(const char* text, std::int64_t length) {
  if (text == nullptr || length <= 0) return {};
  return std::string(text, static_cast<std::size_t>(length));
}

enum class RazCallableKind : std::int32_t { shared = 0, mutable_call = 1, once = 2 };

using RazErasedInvoke = std::int32_t (*)(void*, const void*, std::uint64_t, void*, std::uint64_t);
using RazErasedClone = void* (*)(const void*);
using RazErasedDrop = void (*)(void*);

struct RazErasedCallable {
  std::atomic<std::uint64_t> strong{1};
  std::atomic<std::uint64_t> weak{1};
  RazCallableKind kind = RazCallableKind::shared;
  std::atomic<bool> consumed{false};
  std::mutex invocation_mutex;
  void* environment = nullptr;
  RazErasedInvoke invoke = nullptr;
  RazErasedClone clone_environment = nullptr;
  RazErasedDrop drop_environment = nullptr;
  std::uint64_t signature_id = 0;
  std::uint64_t argument_size = 0;
  std::uint64_t result_size = 0;
};

struct RazErasedCallableWeak { RazErasedCallable* callable = nullptr; };

inline bool try_retain_erased_callable(RazErasedCallable* callable) {
  if (callable == nullptr) return false;
  auto current = callable->strong.load(std::memory_order_acquire);
  while (current != 0) {
    if (callable->strong.compare_exchange_weak(current, current + 1,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) return true;
  }
  return false;
}

inline void release_erased_callable_weak(RazErasedCallable* callable) {
  if (callable != nullptr && callable->weak.fetch_sub(1, std::memory_order_acq_rel) == 1) delete callable;
}

inline void release_erased_callable(RazErasedCallable* callable) {
  if (callable == nullptr || callable->strong.fetch_sub(1, std::memory_order_acq_rel) != 1) return;
  {
    std::lock_guard lock(callable->invocation_mutex);
    if (callable->environment != nullptr && callable->drop_environment != nullptr) callable->drop_environment(callable->environment);
    callable->environment = nullptr;
    callable->invoke = nullptr;
  }

  release_erased_callable_weak(callable);
}

struct RazErasedTraitMethod {
  RazErasedInvoke invoke = nullptr;
  std::uint64_t signature_id = 0;
  std::uint64_t argument_size = 0;
  std::uint64_t result_size = 0;
};

struct RazErasedTraitObject {
  std::atomic<std::uint64_t> strong{1};
  std::atomic<std::uint64_t> weak{1};
  void* data = nullptr;
  std::uint64_t type_id = 0;
  std::uint64_t trait_id = 0;
  RazErasedDrop drop_data = nullptr;
  std::vector<RazErasedTraitMethod> methods;
};

struct RazErasedTraitObjectWeak { RazErasedTraitObject* object = nullptr; };

inline bool try_retain_erased_trait(RazErasedTraitObject* object) {
  if (object == nullptr) return false;
  auto current = object->strong.load(std::memory_order_acquire);
  while (current != 0) {
    if (object->strong.compare_exchange_weak(current, current + 1,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire)) return true;
  }
  return false;
}

inline void release_erased_trait_weak(RazErasedTraitObject* object) {
  if (object != nullptr && object->weak.fetch_sub(1, std::memory_order_acq_rel) == 1) delete object;
}

inline void release_erased_trait(RazErasedTraitObject* object) {
  if (object == nullptr || object->strong.fetch_sub(1, std::memory_order_acq_rel) != 1) return;
  if (object->data != nullptr && object->drop_data != nullptr) object->drop_data(object->data);
  object->data = nullptr;
  object->methods.clear();
  release_erased_trait_weak(object);
}

#if defined(RAZ_HAVE_OPENSSL)
struct RazTlsSession {
  SSL* ssl{};
  std::string hostname;
};

struct RazTlsCacheEntry {
  std::string hostname;
  SSL_SESSION* session{};
};

struct RazTlsSessionCache {
  std::mutex mutex;
  std::array<RazTlsCacheEntry, 8> entries{};
  std::size_t next{};

  ~RazTlsSessionCache() {
    for (auto& entry : entries) {
      if (entry.session != nullptr) SSL_SESSION_free(entry.session);
    }
  }
};

inline RazTlsSessionCache& raz_tls_session_cache() {
  static RazTlsSessionCache cache;
  return cache;
}

inline SSL_SESSION* raz_tls_cached_session(const std::string& hostname) {
  auto& cache = raz_tls_session_cache();
  std::lock_guard<std::mutex> lock(cache.mutex);
  for (auto& entry : cache.entries) {
    if (entry.session != nullptr && entry.hostname == hostname) {
      if (SSL_SESSION_up_ref(entry.session) == 1) return entry.session;
      return nullptr;
    }
  }
  return nullptr;
}

inline void raz_tls_cache_session(const std::string& hostname, SSL* ssl) {
  if (hostname.empty() || ssl == nullptr || SSL_is_init_finished(ssl) != 1) return;
  SSL_SESSION* captured = SSL_get1_session(ssl);
  if (captured == nullptr) return;

  auto& cache = raz_tls_session_cache();
  std::lock_guard<std::mutex> lock(cache.mutex);
  for (auto& entry : cache.entries) {
    if (entry.session != nullptr && entry.hostname == hostname) {
      SSL_SESSION_free(entry.session);
      entry.session = captured;
      return;
    }
  }

  auto& entry = cache.entries[cache.next & (cache.entries.size() - 1)];
  cache.next += 1;
  if (entry.session != nullptr) SSL_SESSION_free(entry.session);
  entry.hostname = hostname;
  entry.session = captured;
}

#if defined(_WIN32)
inline bool raz_tls_load_windows_roots(SSL_CTX* context) {
  if (context == nullptr) return false;
  HCERTSTORE roots = CertOpenSystemStoreA(static_cast<HCRYPTPROV_LEGACY>(0), "ROOT");
  if (roots == nullptr) return false;

  X509_STORE* store = SSL_CTX_get_cert_store(context);
  PCCERT_CONTEXT certificate = nullptr;
  std::size_t imported = 0;
  while ((certificate = CertEnumCertificatesInStore(roots, certificate)) != nullptr) {
    const unsigned char* cursor = certificate->pbCertEncoded;
    X509* x509 = d2i_X509(nullptr, &cursor, static_cast<long>(certificate->cbCertEncoded));
    if (x509 == nullptr) {
      ERR_clear_error();
      continue;
    }
    if (X509_STORE_add_cert(store, x509) == 1) imported += 1;
    // Duplicate roots are expected when OpenSSL's configured CA bundle overlaps
    // the Windows store. They are harmless and must not poison later TLS errors.
    ERR_clear_error();
    X509_free(x509);
  }
  CertCloseStore(roots, 0);
  return imported != 0;
}
#endif

inline SSL_CTX* raz_tls_client_context() {
  static std::once_flag once;
  static SSL_CTX* context = nullptr;
  std::call_once(once, [] {
    context = SSL_CTX_new(TLS_client_method());
    if (context == nullptr) return;
    SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);
    SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION);
    // Client sessions are retained explicitly in a tiny bounded host cache.
    // OpenSSL keeps ticket/session internals opaque; Raz owns reuse policy at
    // the connection layer without serializing cryptographic state.
    SSL_CTX_set_session_cache_mode(context, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);
    bool trust_available = SSL_CTX_set_default_verify_paths(context) == 1;
#if defined(_WIN32)
    // OpenSSL does not automatically consume the Windows system ROOT store.
    // Import it so HTTPS works on a normal Windows installation without asking
    // users to set SSL_CERT_FILE/SSL_CERT_DIR manually.
    trust_available = raz_tls_load_windows_roots(context) || trust_available;
#endif
    if (!trust_available) {
      SSL_CTX_free(context);
      context = nullptr;
    }
  });
  return context;
}

inline void raz_tls_set_error() {
  const auto value = ERR_get_error();
  raz_set_last_error(value == 0 ? -1 : static_cast<std::int64_t>(value));
}

inline std::int64_t raz_tls_result(SSL* ssl, int result) {
  if (result == 1) return 1;
  const int error = SSL_get_error(ssl, result);
  if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) return 0;
  if (error == SSL_ERROR_ZERO_RETURN) return 2;
  raz_tls_set_error();
  return -1;
}
#endif

}
