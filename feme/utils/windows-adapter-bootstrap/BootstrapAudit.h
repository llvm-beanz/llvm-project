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

} // namespace probe
} // namespace feme

#endif
