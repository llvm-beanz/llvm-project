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

} // namespace probe
} // namespace feme

#endif
