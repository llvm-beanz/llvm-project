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
  bool FillTableAttempted = false;
  std::uint32_t FillTableType = 0;
  std::uint64_t FillTableSize = 0;
  std::int32_t FillTableResult = 0;
  bool PagingQueueDestroyAttempted = false;
  bool PagingQueueDestroyReturned = false;
  std::int32_t PagingQueueDestroyResult = 0;
  bool DestroyAttempted = false;
  bool DestroyReturned = false;
  std::int32_t DestroyResult = 0;
  std::uint32_t ExceptionCode = 0;
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
