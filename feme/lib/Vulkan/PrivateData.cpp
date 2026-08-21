//===- PrivateData.cpp - VkPrivateDataSlot object model implementation ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PrivateData.h"
#include "Icd.h"

using namespace feme::vulkan;

namespace feme::vulkan {

VKAPI_ATTR VkResult VKAPI_CALL
vkCreatePrivateDataSlot(VkDevice, const VkPrivateDataSlotCreateInfo *,
                        const VkAllocationCallbacks *pAllocator,
                        VkPrivateDataSlot *pPrivateDataSlot) {
  Allocator Alloc(pAllocator);
  PrivateDataSlot *Obj =
      Alloc.create<PrivateDataSlot>(VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pPrivateDataSlot = toHandle<VkPrivateDataSlot>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyPrivateDataSlot(VkDevice, VkPrivateDataSlot privateDataSlot,
                         const VkAllocationCallbacks *pAllocator) {
  if (!privateDataSlot)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<PrivateDataSlot>(privateDataSlot));
}

VKAPI_ATTR VkResult VKAPI_CALL
vkSetPrivateData(VkDevice, VkObjectType objectType, uint64_t objectHandle,
                 VkPrivateDataSlot privateDataSlot, uint64_t data) {
  fromHandle<PrivateDataSlot>(privateDataSlot)
      ->set(objectType, objectHandle, data);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkGetPrivateData(VkDevice, VkObjectType objectType, uint64_t objectHandle,
                 VkPrivateDataSlot privateDataSlot, uint64_t *pData) {
  *pData =
      fromHandle<PrivateDataSlot>(privateDataSlot)->get(objectType, objectHandle);
}

} // namespace feme::vulkan
