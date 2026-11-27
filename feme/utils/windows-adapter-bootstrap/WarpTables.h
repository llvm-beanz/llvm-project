//===- WarpTables.h - W0 diagnostic device tables -----------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_UTILS_WINDOWS_ADAPTER_BOOTSTRAP_WARPTABLES_H
#define FEME_UTILS_WINDOWS_ADAPTER_BOOTSTRAP_WARPTABLES_H

#include "WarpDeviceState.h"

namespace feme {
namespace probe {

/// Fill an exact 0072 diagnostic table. Buffer DDIs are implemented;
/// unavailable operations report errors instead of exposing null callable
/// entries. This prototype is not a graphics-capable adapter or an execution
/// implementation.
HRESULT fillWarpPrototypeTable(D3D12DDI_TABLE_TYPE Type, void *Table,
                               SIZE_T Size, UINT TableIndex);

} // namespace probe
} // namespace feme

#endif
