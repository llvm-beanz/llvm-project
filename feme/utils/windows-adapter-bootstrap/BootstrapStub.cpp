//===- BootstrapStub.cpp - Direct D3D12 UMD DDI table responder ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This minimal OpenAdapter12 fills a public D3D12DDI_ADAPTERFUNCS table.
// DirectHarness.cpp invokes it by hand, independently of Windows routing.
// windows-warp-interposition also builds it as a local d3d10warp.dll and
// measures stock D3D12's adapter negotiation through Microsoft's WARP identity.
// Neither experiment implements a real device, resource, or command contract:
// no supported DDI versions are advertised, and capabilities/device creation
// deliberately return E_NOTIMPL.
//
//===----------------------------------------------------------------------===//

#include <windows.h>
#include <winternl.h>

#include <d3d10umddi.h>
#include <d3d12umddi.h>
#include <dxgi.h>

namespace {
struct Counters {
  LONG OpenAdapter12 = 0;
  LONG CalcPrivateDeviceSize = 0;
  LONG GetSupportedVersions = 0;
  LONG GetCaps = 0;
  LONG GetOptionalDDITables = 0;
  LONG FillDDITable = 0;
  LONG CreateDevice = 0;
  LONG CloseAdapter = 0;
  LONG DestroyDevice = 0;
};
Counters GCalls;

SIZE_T APIENTRY
calcPrivateDeviceSize(D3D12DDI_HADAPTER, CONST D3D12DDIARG_CALCPRIVATEDEVICESIZE *) {
  InterlockedIncrement(&GCalls.CalcPrivateDeviceSize);
  // A nonzero, deliberately recognizable size; nothing allocates against it.
  return 64;
}

HRESULT APIENTRY createDevice(D3D12DDI_HADAPTER, CONST D3D12DDIARG_CREATEDEVICE_0003 *) {
  InterlockedIncrement(&GCalls.CreateDevice);
  // A real device table is out of scope for this experiment; fail loudly and
  // distinctly so a caller cannot mistake this for the generic rejection
  // windows-adapter-probe's RejectionStub already measures.
  return E_NOTIMPL;
}

HRESULT APIENTRY closeAdapter(D3D12DDI_HADAPTER) {
  InterlockedIncrement(&GCalls.CloseAdapter);
  return S_OK;
}

HRESULT APIENTRY getSupportedVersions(D3D12DDI_HADAPTER, UINT32 *EntryCount,
                                       UINT64 *) {
  InterlockedIncrement(&GCalls.GetSupportedVersions);
  // No interface version is actually supported by this experiment.
  *EntryCount = 0;
  return S_OK;
}

HRESULT APIENTRY getCaps(D3D12DDI_HADAPTER, CONST D3D12DDIARG_GETCAPS *) {
  InterlockedIncrement(&GCalls.GetCaps);
  return E_NOTIMPL;
}

HRESULT APIENTRY getOptionalDDITables(D3D12DDI_HADAPTER, UINT32 *EntryCount,
                                      D3D12DDI_TABLE_REQUEST *) {
  InterlockedIncrement(&GCalls.GetOptionalDDITables);
  *EntryCount = 0;
  return S_OK;
}

HRESULT APIENTRY fillDDITable(D3D12DDI_HADAPTER, D3D12DDI_TABLE_TYPE, VOID *,
                               SIZE_T, UINT, D3D12DDI_HRTTABLE) {
  InterlockedIncrement(&GCalls.FillDDITable);
  return E_NOTIMPL;
}

VOID APIENTRY destroyDevice(D3D12DDI_HDEVICE) {
  InterlockedIncrement(&GCalls.DestroyDevice);
}
} // namespace

extern "C" __declspec(dllexport) HRESULT APIENTRY
OpenAdapter10(D3D10DDIARG_OPENADAPTER *) {
  return DXGI_ERROR_UNSUPPORTED;
}

extern "C" __declspec(dllexport) HRESULT APIENTRY
OpenAdapter10_2(D3D10DDIARG_OPENADAPTER *) {
  return DXGI_ERROR_UNSUPPORTED;
}

extern "C" __declspec(dllexport) HRESULT APIENTRY
OpenAdapter12(D3D12DDIARG_OPENADAPTER *Args) {
  if (!Args || !Args->pAdapterCallbacks || !Args->pAdapterFuncs)
    return E_INVALIDARG;
  InterlockedIncrement(&GCalls.OpenAdapter12);
  Args->pAdapterFuncs->pfnCalcPrivateDeviceSize = calcPrivateDeviceSize;
  Args->pAdapterFuncs->pfnCreateDevice = createDevice;
  Args->pAdapterFuncs->pfnCloseAdapter = closeAdapter;
  Args->pAdapterFuncs->pfnGetSupportedVersions = getSupportedVersions;
  Args->pAdapterFuncs->pfnGetCaps = getCaps;
  Args->pAdapterFuncs->pfnGetOptionalDDITables = getOptionalDDITables;
  Args->pAdapterFuncs->pfnFillDDITable = fillDDITable;
  Args->pAdapterFuncs->pfnDestroyDevice = destroyDevice;
  Args->hAdapter.pDrvPrivate = &GCalls;
  return S_OK;
}

extern "C" __declspec(dllexport) void APIENTRY
FeMeBootstrapGetCalls(LONG *OpenAdapter12Count, LONG *CalcPrivateDeviceSizeCount,
                       LONG *GetSupportedVersionsCount, LONG *GetCapsCount,
                       LONG *GetOptionalDDITablesCount, LONG *FillDDITableCount,
                       LONG *CreateDeviceCount, LONG *CloseAdapterCount,
                       LONG *DestroyDeviceCount) {
  *OpenAdapter12Count = InterlockedCompareExchange(&GCalls.OpenAdapter12, 0, 0);
  *CalcPrivateDeviceSizeCount =
      InterlockedCompareExchange(&GCalls.CalcPrivateDeviceSize, 0, 0);
  *GetSupportedVersionsCount =
      InterlockedCompareExchange(&GCalls.GetSupportedVersions, 0, 0);
  *GetCapsCount = InterlockedCompareExchange(&GCalls.GetCaps, 0, 0);
  *GetOptionalDDITablesCount =
      InterlockedCompareExchange(&GCalls.GetOptionalDDITables, 0, 0);
  *FillDDITableCount = InterlockedCompareExchange(&GCalls.FillDDITable, 0, 0);
  *CreateDeviceCount = InterlockedCompareExchange(&GCalls.CreateDevice, 0, 0);
  *CloseAdapterCount = InterlockedCompareExchange(&GCalls.CloseAdapter, 0, 0);
  *DestroyDeviceCount = InterlockedCompareExchange(&GCalls.DestroyDevice, 0, 0);
}
