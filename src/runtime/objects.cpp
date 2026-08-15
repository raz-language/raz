// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "runtime_internal.hpp"

using namespace raz::runtime_detail;

extern "C" {
void* raz_rt_callable_weak_create(void* handle) {
  auto* callable = static_cast<RazErasedCallable*>(handle);
  if (callable == nullptr || callable->strong.load(std::memory_order_acquire) == 0) return nullptr;
  try {
    auto* weak = new RazErasedCallableWeak{callable};
    callable->weak.fetch_add(1, std::memory_order_relaxed);
    return weak;
  } catch (...) {
    return nullptr;
  }
}

void* raz_rt_callable_weak_upgrade(void* handle) {
  auto* weak = static_cast<RazErasedCallableWeak*>(handle);
  if (weak == nullptr || !try_retain_erased_callable(weak->callable)) return nullptr;
  return weak->callable;
}

std::int32_t raz_rt_callable_weak_expired(const void* handle) {
  const auto* weak = static_cast<const RazErasedCallableWeak*>(handle);
  return weak == nullptr || weak->callable == nullptr ||
         weak->callable->strong.load(std::memory_order_acquire) == 0 ? 1 : 0;
}

void raz_rt_callable_weak_destroy(void* handle) {
  auto* weak = static_cast<RazErasedCallableWeak*>(handle);
  if (weak == nullptr) return;
  release_erased_callable_weak(weak->callable);
  delete weak;
}

void* raz_rt_trait_object_weak_create(void* object) {
  auto* value = static_cast<RazErasedTraitObject*>(object);
  if (value == nullptr || value->strong.load(std::memory_order_acquire) == 0) return nullptr;
  try {
    auto* weak = new RazErasedTraitObjectWeak{value};
    value->weak.fetch_add(1, std::memory_order_relaxed);
    return weak;
  } catch (...) {
    return nullptr;
  }
}

void* raz_rt_trait_object_weak_upgrade(void* handle) {
  auto* weak = static_cast<RazErasedTraitObjectWeak*>(handle);
  if (weak == nullptr || !try_retain_erased_trait(weak->object)) return nullptr;
  return weak->object;
}

std::int32_t raz_rt_trait_object_weak_expired(const void* handle) {
  const auto* weak = static_cast<const RazErasedTraitObjectWeak*>(handle);
  return weak == nullptr || weak->object == nullptr ||
         weak->object->strong.load(std::memory_order_acquire) == 0 ? 1 : 0;
}

void raz_rt_trait_object_weak_destroy(void* handle) {
  auto* weak = static_cast<RazErasedTraitObjectWeak*>(handle);
  if (weak == nullptr) return;
  release_erased_trait_weak(weak->object);
  delete weak;
}

std::uint64_t raz_rt_trait_object_trait_id(const void* object) {
  const auto* value = static_cast<const RazErasedTraitObject*>(object);
  return value == nullptr || value->strong.load(std::memory_order_acquire) == 0 ? 0 : value->trait_id;
}

std::int32_t raz_rt_trait_object_is_trait(const void* object, std::uint64_t expected_trait_id) {
  const auto* value = static_cast<const RazErasedTraitObject*>(object);
  return value != nullptr && value->strong.load(std::memory_order_acquire) != 0 &&
         value->trait_id == expected_trait_id ? 1 : 0;
}

std::uint64_t raz_rt_trait_object_type_id(const void* object) {
  const auto* value = static_cast<const RazErasedTraitObject*>(object);
  return value == nullptr || value->strong.load(std::memory_order_acquire) == 0 ? 0 : value->type_id;
}

void* raz_rt_trait_object_downcast_data(void* object, std::uint64_t expected_type_id) {
  auto* value = static_cast<RazErasedTraitObject*>(object);
  if (value == nullptr || value->strong.load(std::memory_order_acquire) == 0 ||
      value->type_id != expected_type_id) return nullptr;
  return value->data;
}


}
