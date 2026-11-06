//===- Intercept.cpp - Deliberately rejecting WARP DDI interposition
//--------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <windows.h>
#include <winternl.h>

#include <d3d12umddi.h>
#include <dxgi.h>

static LONG OpenCalls;

// The linker aliases this public DDI signature to OpenAdapter12.
// Reference bootstrap forwarding is explicitly not an independent FeMe driver.
extern "C" HRESULT APIENTRY
FeMeInterceptOpenAdapter12(D3D12DDIARG_OPENADAPTER *) {
  InterlockedIncrement(&OpenCalls);
  return DXGI_ERROR_UNSUPPORTED;
}

extern "C" __declspec(dllexport) void APIENTRY
FeMeProbeGetCalls(LONG *Count10, LONG *Count10_2, LONG *Count12) {
  *Count10 = 0;
  *Count10_2 = 0;
  *Count12 = InterlockedCompareExchange(&OpenCalls, 0, 0);
}
