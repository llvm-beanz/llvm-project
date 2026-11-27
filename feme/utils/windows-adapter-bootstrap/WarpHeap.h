//===- WarpHeap.h - Owned buffer heap DDIs -----------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_UTILS_WINDOWS_ADAPTER_BOOTSTRAP_WARPHEAP_H
#define FEME_UTILS_WINDOWS_ADAPTER_BOOTSTRAP_WARPHEAP_H

#include "WarpDeviceState.h"

namespace feme {
namespace probe {

/// Populate the committed-buffer heap entries, not a complete device table.
/// These callbacks use the existing WARP KMD allocation ABI. Shared-resource
/// opening, protected heaps and placed resources are not supported.
/// Publication through FillDDITable remains
/// gated on the complete device contract.
void populateWarpHeapDDIs(D3D12DDI_DEVICE_FUNCS_CORE_0072 &Table);

} // namespace probe
} // namespace feme

#endif
