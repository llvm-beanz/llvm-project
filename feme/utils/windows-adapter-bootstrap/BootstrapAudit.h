//===- BootstrapAudit.h - Public DDI negotiation audit data -------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_UTILS_WINDOWS_ADAPTER_BOOTSTRAP_BOOTSTRAPAUDIT_H
#define FEME_UTILS_WINDOWS_ADAPTER_BOOTSTRAP_BOOTSTRAPAUDIT_H

#include <cstdint>

namespace feme {
namespace probe {

/// Read-only observations, not proof that any kernel callback is callable.
struct BootstrapContractAudit {
  std::uint64_t PinnedVersion = 0;
  bool HasKTMakeResidentCb = false;
  bool HasKTEvictCb = false;
  bool HasKTCreatePagingQueueCb = false;
  bool HasKTDestroyPagingQueueCb = false;
  bool HasKTMapGpuVirtualAddressCb = false;
  bool HasKTDeallocate2Cb = false;
  bool HasUMCreatePagingQueueCb = false;
  bool HasUMDestroyPagingQueueCb = false;
  bool HasUMMakeResidentCb = false;
  bool HasUMEvictCb = false;
  bool HasUMAllocateCb = false;
  bool HasUMDeallocateCb = false;
};

/// Result of querying WARP's private adapter information through the runtime.
struct WarpPrivateAdapterInfoProbe {
  bool Attempted = false;
  std::int32_t Result = 0;
  std::uint32_t Version = 0;
  std::uint32_t WddmVersion = 0;
  std::uint32_t DriverCaps = 0;
};

/// Results of creating and destroying a WARP context through the runtime.
struct WarpContextProbe {
  bool CreateAttempted = false;
  bool CreateReturned = false;
  std::int32_t CreateResult = 0;
  bool HasContext = false;
  std::uint64_t CommandBufferAddress = 0;
  std::uint64_t CommandBufferSize = 0;
  bool PrivateDataPointersIntact = false;
  bool DestroyAttempted = false;
  bool DestroyReturned = false;
  std::int32_t DestroyResult = 0;
  std::uint32_t ExceptionCode = 0;
};

/// Results of a device attempt that retains its kernel context through
/// teardown.
struct WarpDeviceContextProbe {
  bool VersionNumberEscapeAttempted = false;
  bool VersionNumberEscapeReturned = false;
  std::int32_t VersionNumberEscapeResult = 0;
  std::uint32_t BasicRenderVersion = 0;
  bool PreferredNodeEscapeAttempted = false;
  bool PreferredNodeEscapeReturned = false;
  std::int32_t PreferredNodeEscapeResult = 0;
  std::uint32_t PreferredNodeOrdinal = 0;
  std::uint32_t PreferredEngineAffinity = 0;
  bool PagingQueueCreateAttempted = false;
  bool PagingQueueCreated = false;
  std::int32_t PagingQueueCreateResult = 0;
  bool PagingQueueHasSyncObject = false;
  bool PagingQueueHasFenceMapping = false;
  bool CreateAttempted = false;
  bool ContextCreated = false;
  std::int32_t CreateContextResult = 0;
  std::uint64_t CommandBufferAddress = 0;
  std::uint64_t CommandBufferSize = 0;
  bool PrivateDataPointersIntact = false;
  bool InitContextEscapeAttempted = false;
  bool InitContextEscapeReturned = false;
  std::int32_t InitContextEscapeResult = 0;
  std::uint64_t KernelContextHandle = 0;
  std::uint64_t DmaBufferAddress = 0;
  std::uint32_t DmaBufferSize = 0;
  bool DmaAllocationAttempted = false;
  bool DmaAllocationReturned = false;
  bool DmaAllocationCreated = false;
  std::int32_t DmaAllocationResult = 0;
  std::uint32_t DmaAllocationHandle = 0;
  bool DmaLockAttempted = false;
  bool DmaLockReturned = false;
  std::int32_t DmaLockResult = 0;
  std::uint64_t DmaCpuAddress = 0;
  bool DmaFenceCreateAttempted = false;
  bool DmaFenceCreateReturned = false;
  bool DmaFenceCreated = false;
  std::int32_t DmaFenceCreateResult = 0;
  std::uint32_t DmaFenceHandle = 0;
  bool InitDeviceEscapeAttempted = false;
  bool InitDeviceEscapeReturned = false;
  std::int32_t InitDeviceEscapeResult = 0;
  bool DmaFenceDestroyAttempted = false;
  bool DmaFenceDestroyReturned = false;
  std::int32_t DmaFenceDestroyResult = 0;
  bool DmaUnlockAttempted = false;
  bool DmaUnlockReturned = false;
  std::int32_t DmaUnlockResult = 0;
  bool DmaDeallocateAttempted = false;
  bool DmaDeallocateReturned = false;
  std::int32_t DmaDeallocateResult = 0;
  bool FillTableAttempted = false;
  std::uint32_t FillTableType = 0;
  std::uint64_t FillTableSize = 0;
  std::int32_t FillTableResult = 0;
  bool HeapAttempted = false;
  std::int32_t HeapCreateResult = 0;
  std::int32_t HeapMapResult = 0;
  std::int32_t HeapDestroyResult = 0;
  std::uint32_t HeapAllocationHandle = 0;
  std::uint64_t HeapCpuAddress = 0;
  bool HeapDataRoundTrip = false;
  bool HeapDestroyed = false;
  std::uint64_t LiveRootSignaturesAtDestroy = 0;
  std::uint64_t LiveNullShadersAtDestroy = 0;
  std::uint64_t LiveCompiledShadersAtDestroy = 0;
  std::uint64_t LiveImmutableStatesAtDestroy = 0;
  std::uint64_t LivePipelinesAtDestroy = 0;
  std::uint64_t LiveCommandPoolsAtDestroy = 0;
  std::uint64_t LiveCommandQueuesAtDestroy = 0;
  std::uint64_t LiveCommandRecordersAtDestroy = 0;
  std::uint64_t LiveCommandListsAtDestroy = 0;
  std::uint64_t LiveHeapsAtDestroy = 0;
  std::uint64_t LiveResourcesAtDestroy = 0;
  std::uint64_t LiveFencesAtDestroy = 0;
  bool PagingQueueDestroyAttempted = false;
  bool PagingQueueDestroyReturned = false;
  std::int32_t PagingQueueDestroyResult = 0;
  bool DestroyAttempted = false;
  bool DestroyReturned = false;
  std::int32_t DestroyResult = 0;
  std::uint32_t ExceptionCode = 0;
};

/// Publication and fail-closed shader evidence for the W0 diagnostic tables.
struct WarpPrototypeTableAudit {
  std::uint32_t CoreTables = 0;
  std::uint32_t QueueTables = 0;
  std::uint32_t CommandListTables = 0;
  std::uint32_t ExtendedTables = 0;
  std::uint32_t ExecutableShaderQueries = 0;
  std::uint32_t UnsupportedCalls = 0;
  std::uint32_t CompiledShadersCreated = 0;
  std::uint32_t CompiledShadersDestroyed = 0;
  std::uint32_t PipelinesCreated = 0;
  std::uint32_t PipelinesDestroyed = 0;
};

/// Only application resources with a real HRTRESOURCE contribute here.
struct WarpApplicationHeapAudit {
  std::uint32_t Created = 0;
  std::uint32_t Destroyed = 0;
  std::uint32_t Maps = 0;
  std::uint32_t Unmaps = 0;
  std::uint32_t MakeResidentCallbacks = 0;
  std::uint32_t EvictCallbacks = 0;
  bool HasRuntimePagingQueue = false;
};

/// Results of the opt-in kernel paging-queue create/destroy experiment.
struct PagingQueueProbe {
  bool Enabled = false;
  bool CreateAttempted = false;
  bool CreateReturned = false;
  std::int32_t CreateResult = 0;
  bool HasPagingQueue = false;
  bool HasSyncObject = false;
  bool HasFenceMapping = false;
  bool DestroyAttempted = false;
  bool DestroyReturned = false;
  std::int32_t DestroyResult = 0;
  std::uint32_t ExceptionCode = 0;
};

constexpr std::uint64_t GpuVaReservationBytes = 65536;

/// Results of reserving and freeing an unmapped runtime-owned GPU VA range.
struct GpuVirtualAddressProbe {
  bool Enabled = false;
  bool ReserveAttempted = false;
  bool ReserveReturned = false;
  std::int32_t ReserveResult = 0;
  std::uint64_t Address = 0;
  bool HasValidRange = false;
  bool FreeAttempted = false;
  bool FreeReturned = false;
  std::int32_t FreeResult = 0;
  std::uint32_t ExceptionCode = 0;
};

constexpr std::uint64_t MonitoredFenceTarget = 7;

/// Observations from an owned-runtime CPU monitored-fence round trip.
struct MonitoredFenceProbe {
  bool Enabled = false;
  bool CreateAttempted = false;
  bool CreateReturned = false;
  std::int32_t CreateResult = 0;
  bool HasSyncObject = false;
  bool HasFenceMapping = false;
  std::uint64_t InitialValue = 0;
  bool WaitAttempted = false;
  bool WaitReturned = false;
  std::int32_t WaitResult = 0;
  bool WaitWasPending = false;
  bool SignalAttempted = false;
  bool SignalReturned = false;
  std::int32_t SignalResult = 0;
  bool EventCompleted = false;
  bool EventRetained = false;
  std::uint64_t CompletedValue = 0;
  bool DestroyAttempted = false;
  bool DestroyReturned = false;
  std::int32_t DestroyResult = 0;
  std::uint32_t ExceptionCode = 0;
  std::uint32_t Win32Error = 0;
};

} // namespace probe
} // namespace feme

#endif
