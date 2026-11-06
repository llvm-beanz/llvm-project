//===- RejectionStub.cpp - Software adapter loading diagnostic
//--------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <windows.h>
#include <winternl.h>

#include <d3d10umddi.h>
#include <d3d12umddi.h>
#include <dxgi.h>

static LONG Calls10;
static LONG Calls10_2;
static LONG Calls12;

// This intentionally cannot create a device. It measures loader reachability,
// not a valid driver contract, adapter identity, or FeMe execution.
extern "C" __declspec(dllexport) HRESULT APIENTRY
OpenAdapter10(D3D10DDIARG_OPENADAPTER *) {
  InterlockedIncrement(&Calls10);
  return DXGI_ERROR_UNSUPPORTED;
}

extern "C" __declspec(dllexport) HRESULT APIENTRY
OpenAdapter10_2(D3D10DDIARG_OPENADAPTER *) {
  InterlockedIncrement(&Calls10_2);
  return DXGI_ERROR_UNSUPPORTED;
}

extern "C" __declspec(dllexport) HRESULT APIENTRY
OpenAdapter12(D3D12DDIARG_OPENADAPTER *) {
  InterlockedIncrement(&Calls12);
  return DXGI_ERROR_UNSUPPORTED;
}

extern "C" __declspec(dllexport) void APIENTRY
FeMeProbeGetCalls(LONG *Count10, LONG *Count10_2, LONG *Count12) {
  *Count10 = InterlockedCompareExchange(&Calls10, 0, 0);
  *Count10_2 = InterlockedCompareExchange(&Calls10_2, 0, 0);
  *Count12 = InterlockedCompareExchange(&Calls12, 0, 0);
}
