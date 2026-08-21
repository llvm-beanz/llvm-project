//===- PrivateData.h - VkPrivateDataSlot object model ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The roadmap E10 `VkPrivateDataSlot` object model (`VK_EXT_private_data`,
// promoted to core `privateData` at Vulkan 1.3): an opaque per-(object
// handle) `uint64_t` map, self-contained and independent of every other
// object in Objects.h. `vkSetPrivateData`/`vkGetPrivateData` key on the raw
// `(VkObjectType, uint64_t)` handle pair the application already owns --
// this ICD never dereferences the handle itself, so a slot has no
// dependency on what kind of object (or even whether a *live* object) the
// handle actually names, matching the extension's own "no additional
// requirement that the object still exist" contract for `vkGetPrivateData`.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_PRIVATEDATA_H
#define FEME_LIB_VULKAN_PRIVATEDATA_H

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>

namespace feme::vulkan {

/// A `VkPrivateDataSlot`: a map from `(VkObjectType, object handle)` to a
/// single `uint64_t` value, per "Object Model" in
/// feme/docs/FeMeVulkanDesign.md.
class PrivateDataSlot {
public:
  /// `vkSetPrivateData`: associates \p Data with \p ObjectHandle,
  /// overwriting any value previously set for the same
  /// `(ObjectType, ObjectHandle)` pair.
  void set(VkObjectType ObjectType, uint64_t ObjectHandle, uint64_t Data) {
    Values[{ObjectType, ObjectHandle}] = Data;
  }

  /// `vkGetPrivateData`: returns the value last set for
  /// `(ObjectType, ObjectHandle)`, or 0 if none has ever been set, per the
  /// extension's own "0 if no value has been associated" default.
  uint64_t get(VkObjectType ObjectType, uint64_t ObjectHandle) const {
    auto It = Values.find({ObjectType, ObjectHandle});
    return It == Values.end() ? 0 : It->second;
  }

private:
  using Key = std::pair<VkObjectType, uint64_t>;

  struct KeyHash {
    size_t operator()(const Key &K) const {
      return std::hash<uint64_t>()(static_cast<uint64_t>(K.first)) ^
             (std::hash<uint64_t>()(K.second) << 1);
    }
  };

  std::unordered_map<Key, uint64_t, KeyHash> Values;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_PRIVATEDATA_H
