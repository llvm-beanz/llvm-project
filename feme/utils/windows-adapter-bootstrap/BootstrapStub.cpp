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
// device creation deliberately returns E_NOTIMPL. By default no supported DDI
// versions are advertised. W0(g)'s audit variant defines
// FEME_BOOTSTRAP_PINNED_VERSION (one public D3D12DDI_SUPPORTED_00NN value) to
// observe what the stock runtime asks for once negotiation can succeed. The
// two GetCaps queries that W0(g) observed happen unconditionally before
// CreateDevice (D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1 and the older
// D3D12DDICAPS_TYPE_3DPIPELINESUPPORT) are answered truthfully and minimally:
// the former simply echoes back the runtime's own requested feature level
// (its pData struct carries it as an input field), and the latter reports the
// lowest defined D3D12DDI_3DPIPELINELEVEL. Neither answer fabricates driver
// capability beyond what the runtime already told us or the lowest possible
// truthful claim; every other cap and createDevice itself still deliberately
// fail, so this remains audit work, not a device contract.
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

// W0(g)'s audit evidence: which public D3D12DDICAPS_TYPE values GetCaps is
// queried with, and whether CreateDevice's publicly documented thunk-service
// pointers (pKTCallbacks/p12UMCallbacks) are populated. Logging this does not
// implement any capability or device contract.
constexpr UINT32 MaxCapsTypeLog = 32;
LONG GCapsTypeLog[MaxCapsTypeLog] = {};
LONG GCapsTypeLogCount = 0; // Total GetCaps calls observed, unclamped.

struct CreateDeviceAudit {
  LONG Captured = 0;
  UINT Interface = 0;
  UINT Version = 0;
  LONG HasKTCallbacks = 0;
  LONG HasUMCallbacks = 0;
};
CreateDeviceAudit GCreateDeviceAudit;

SIZE_T APIENTRY
calcPrivateDeviceSize(D3D12DDI_HADAPTER, CONST D3D12DDIARG_CALCPRIVATEDEVICESIZE *) {
  InterlockedIncrement(&GCalls.CalcPrivateDeviceSize);
  // A nonzero, deliberately recognizable size; nothing allocates against it.
  return 64;
}

HRESULT APIENTRY createDevice(D3D12DDI_HADAPTER,
                              CONST D3D12DDIARG_CREATEDEVICE_0003 *Args) {
  InterlockedIncrement(&GCalls.CreateDevice);
  if (Args && !InterlockedCompareExchange(&GCreateDeviceAudit.Captured, 1, 0)) {
    GCreateDeviceAudit.Interface = Args->Interface;
    GCreateDeviceAudit.Version = Args->Version;
    GCreateDeviceAudit.HasKTCallbacks = Args->pKTCallbacks != nullptr;
    GCreateDeviceAudit.HasUMCallbacks = Args->p12UMCallbacks != nullptr;
  }
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
                                       UINT64 *Versions) {
  InterlockedIncrement(&GCalls.GetSupportedVersions);
#ifndef FEME_BOOTSTRAP_PINNED_VERSION
  (void)Versions;
#endif
#ifdef FEME_BOOTSTRAP_PINNED_VERSION
  // W0(g)'s audit pins exactly one publicly documented interface version so
  // the stock runtime can proceed past negotiation; this still implements no
  // real capability or device contract, and no private version is guessed.
  if (Versions && *EntryCount >= 1)
    Versions[0] = FEME_BOOTSTRAP_PINNED_VERSION;
  *EntryCount = 1;
#else
  // No interface version is actually supported by this experiment.
  *EntryCount = 0;
#endif
  return S_OK;
}

HRESULT APIENTRY getCaps(D3D12DDI_HADAPTER, CONST D3D12DDIARG_GETCAPS *Args) {
  InterlockedIncrement(&GCalls.GetCaps);
  LONG Index = InterlockedIncrement(&GCapsTypeLogCount) - 1;
  if (Args && Index >= 0 && static_cast<UINT32>(Index) < MaxCapsTypeLog)
    GCapsTypeLog[Index] = static_cast<LONG>(Args->Type);

  if (!Args)
    return E_NOTIMPL;

  // W0(g): answer the two caps the stock runtime is observed to query before
  // CreateDevice, truthfully and minimally. Every other cap still fails.
  if (Args->Type == D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1 &&
      Args->pData && Args->DataSize >= sizeof(D3D12DDI_3DPIPELINESUPPORT1_DATA_0081)) {
    auto *Data =
        static_cast<D3D12DDI_3DPIPELINESUPPORT1_DATA_0081 *>(Args->pData);
    // The runtime supplies its own requested level as input; echoing it back
    // as the maximum driver-supported level is a true statement, not a
    // fabricated capability claim.
    Data->MaximumDriverSupportedFeatureLevel =
        Data->HighestRuntimeSupportedFeatureLevel;
    return S_OK;
  }
  if (Args->Type == D3D12DDICAPS_TYPE_3DPIPELINESUPPORT && Args->pData &&
      Args->DataSize >= sizeof(D3D12DDI_3DPIPELINELEVEL)) {
    // Report the lowest defined pipeline level: a truthful floor, not a
    // fabricated claim of broader support.
    *static_cast<D3D12DDI_3DPIPELINELEVEL *>(Args->pData) =
        D3D12DDI_3DPIPELINELEVEL_1_0_GENERIC;
    return S_OK;
  }

  // No capability data is fabricated for any other cap; this remains an
  // audit, not a device.
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

// The fixed capacity backing FeMeBootstrapGetAudit's CapsTypes buffer; callers
// should size their buffer to this value rather than hardcoding MaxCapsTypeLog.
extern "C" __declspec(dllexport) UINT32 APIENTRY
FeMeBootstrapGetAuditCapacity() {
  return MaxCapsTypeLog;
}

extern "C" __declspec(dllexport) void APIENTRY
FeMeBootstrapGetAudit(LONG *CapsTypeCount,
                      LONG CapsTypes[/* FeMeBootstrapGetAuditCapacity() */],
                      UINT *CreateDeviceInterface, UINT *CreateDeviceVersion,
                      LONG *CreateDeviceHasKTCallbacks,
                      LONG *CreateDeviceHasUMCallbacks) {
  LONG Count = InterlockedCompareExchange(&GCapsTypeLogCount, 0, 0);
  *CapsTypeCount = Count;
  UINT32 Fill = Count <= 0 ? 0
                : static_cast<UINT32>(Count) < MaxCapsTypeLog
                      ? static_cast<UINT32>(Count)
                      : MaxCapsTypeLog;
  for (UINT32 I = 0; I < Fill; ++I)
    CapsTypes[I] = GCapsTypeLog[I];
  *CreateDeviceInterface = GCreateDeviceAudit.Interface;
  *CreateDeviceVersion = GCreateDeviceAudit.Version;
  *CreateDeviceHasKTCallbacks =
      InterlockedCompareExchange(&GCreateDeviceAudit.HasKTCallbacks, 0, 0);
  *CreateDeviceHasUMCallbacks =
      InterlockedCompareExchange(&GCreateDeviceAudit.HasUMCallbacks, 0, 0);
}
