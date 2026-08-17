//===- Icd.h - Common Vulkan ICD object model ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Common building blocks for the FeMe Vulkan ICD's object model (see "Object
// Model" in feme/docs/FeMeVulkanDesign.md): the loader-dispatch header every
// dispatchable handle must start with, and an allocator wrapper so every
// object honors the application's `VkAllocationCallbacks` when supplied.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_ICD_H
#define FEME_LIB_VULKAN_ICD_H

#include <vulkan/vk_icd.h>
#include <vulkan/vulkan_core.h>

#include <cstdlib>
#include <utility>

namespace feme::vulkan {

/// Base for a Vulkan *dispatchable* object (`VkInstance`, `VkPhysicalDevice`,
/// `VkDevice`, `VkQueue`, `VkCommandBuffer`). The loader requires its own
/// dispatch-table pointer to be the very first member of any such object,
/// initialized with `ICD_LOADER_MAGIC` ("Loader Integration" in
/// feme/docs/FeMeVulkanDesign.md); every ICD object class derives from this
/// first so that layout holds regardless of the derived class's own fields.
struct DispatchableBase {
  VK_LOADER_DATA LoaderData;

  DispatchableBase() { set_loader_magic_value(&LoaderData); }
};

/// Wraps an optional `VkAllocationCallbacks`, allocating and freeing through
/// it when supplied and falling back to the global allocator otherwise, per
/// "Object Model": "All objects use the application's `VkAllocationCallbacks`
/// when supplied."
class Allocator {
public:
  explicit Allocator(const VkAllocationCallbacks *Callbacks)
      : HasCallbacks(Callbacks != nullptr) {
    if (HasCallbacks)
      Callbacks_ = *Callbacks;
  }

  void *allocate(size_t Size, size_t Alignment,
                 VkSystemAllocationScope Scope) const {
    if (HasCallbacks)
      return Callbacks_.pfnAllocation(Callbacks_.pUserData, Size, Alignment,
                                      Scope);
    // `::operator new` only guarantees alignment up to
    // `__STDCPP_DEFAULT_NEW_ALIGNMENT__`; Vulkan objects never request more
    // than that without callbacks, so plain `malloc` (naturally aligned for
    // any fundamental type) is sufficient here.
    (void)Alignment;
    return std::malloc(Size);
  }

  void free(void *Ptr) const {
    if (!Ptr)
      return;
    if (HasCallbacks)
      Callbacks_.pfnFree(Callbacks_.pUserData, Ptr);
    else
      std::free(Ptr);
  }

  /// Allocates and default-constructs a `T`, matching this allocator's
  /// scope. Returns null if the underlying allocation fails, matching
  /// `vkAllocationFunction`'s own null-on-failure contract.
  template <typename T, typename... Args>
  T *create(VkSystemAllocationScope Scope, Args &&...ArgPack) const {
    void *Mem = allocate(sizeof(T), alignof(T), Scope);
    if (!Mem)
      return nullptr;
    return new (Mem) T(std::forward<Args>(ArgPack)...);
  }

  /// Destroys and frees an object previously returned by `create`.
  template <typename T> void destroy(T *Obj) const {
    if (!Obj)
      return;
    Obj->~T();
    free(Obj);
  }

  const VkAllocationCallbacks *callbacks() const {
    return HasCallbacks ? &Callbacks_ : nullptr;
  }

private:
  VkAllocationCallbacks Callbacks_{};
  bool HasCallbacks;
};

/// Casts a dispatchable Vulkan handle to its underlying ICD object type.
/// Handles are opaque pointers to incomplete `*_T` structs by design; every
/// FeMe ICD object begins with `DispatchableBase` so this cast is well
/// defined as long as \p Handle was produced by this ICD.
template <typename ObjectT, typename HandleT> ObjectT *fromHandle(HandleT H) {
  return reinterpret_cast<ObjectT *>(H);
}

template <typename HandleT, typename ObjectT> HandleT toHandle(ObjectT *Obj) {
  return reinterpret_cast<HandleT>(Obj);
}

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_ICD_H
