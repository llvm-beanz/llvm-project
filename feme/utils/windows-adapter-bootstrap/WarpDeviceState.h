//===- WarpDeviceState.h - Owned WARP device state ----------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_UTILS_WINDOWS_ADAPTER_BOOTSTRAP_WARPDEVICESTATE_H
#define FEME_UTILS_WINDOWS_ADAPTER_BOOTSTRAP_WARPDEVICESTATE_H

#include <windows.h>
#include <winternl.h>

#include <d3d12umddi.h>

#include <atomic>

namespace feme {
namespace probe {

/// Existing WARP KMD virtual-context buffer output ABI.
struct WarpPrivateContextBuffer {
  UINT64 *Buffer;
  UINT64 *BufferSize;
};
static_assert(sizeof(WarpPrivateContextBuffer) == 2 * sizeof(UINT64 *));

struct WarpUserModeContext {
  UINT64 KernelContextHandle = 0;
  void *CommandBuffer = nullptr;
  UINT32 CommandBufferSize = 0;
};

/// Live CPU descriptor metadata admitted by the pageable-object callbacks.
struct WarpDescriptorHeapRegistration {
  WarpDescriptorHeapRegistration *Next = nullptr;
  void *Handle = nullptr;
};

/// Runtime-owned private device storage and its retained KMD objects.
struct WarpDeviceState {
  const D3DDDI_DEVICECALLBACKS *KTCallbacks = nullptr;
  const D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 *UMCallbacks = nullptr;
  HANDLE RTDevice = nullptr;
  const D3D12DDI_HRTTABLE *CommandListTables = nullptr;
  UINT32 NodeOrdinal = 0;
  UINT32 EngineAffinity = 0;
  bool VirtualGpu = false;
  D3DKMT_HANDLE PagingQueue = 0;
  D3DKMT_HANDLE PagingSyncObject = 0;
  volatile UINT64 *PagingFenceValue = nullptr;
  HANDLE Context = nullptr;
  UINT64 CommandBufferAddress = 0;
  UINT64 CommandBufferSize = 0;
  WarpUserModeContext UserModeContext;
  D3DKMT_HANDLE DmaAllocation = 0;
  void *DmaCpuAddress = nullptr;
  bool DmaLocked = false;
  D3DKMT_HANDLE DmaFence = 0;
  volatile UINT64 *DmaFenceValue = nullptr;
  std::atomic<SIZE_T> LiveHeaps{0};
  std::atomic<SIZE_T> LiveResources{0};
  SRWLOCK ResourceLock = SRWLOCK_INIT;
  void *ResourceList = nullptr;
  SRWLOCK DescriptorLock = SRWLOCK_INIT;
  WarpDescriptorHeapRegistration *DescriptorList = nullptr;
  std::atomic<SIZE_T> LiveRootSignatures{0};
  std::atomic<SIZE_T> LiveNullShaders{0};
  std::atomic<SIZE_T> LiveCompiledShaders{0};
  std::atomic<SIZE_T> LiveImmutableStates{0};
  std::atomic<SIZE_T> LivePipelines{0};
  std::atomic<SIZE_T> LiveCommandPools{0};
  std::atomic<SIZE_T> LiveCommandQueues{0};
  std::atomic<SIZE_T> LiveCommandRecorders{0};
  std::atomic<SIZE_T> LiveCommandLists{0};
  std::atomic<SIZE_T> LiveFences{0};
  std::atomic<HRESULT> HeapCleanupResult{S_OK};
};

static_assert(alignof(WarpDeviceState) <= MEMORY_ALLOCATION_ALIGNMENT);

} // namespace probe
} // namespace feme

#endif
