//===- WarpTables.cpp - Fail-closed W0 diagnostic tables
//-------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "WarpTables.h"

#include "BootstrapAudit.h"
#include "WarpHeap.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>

namespace {
std::atomic<const char *> LastUnsupported{nullptr};
std::atomic<UINT> PublishedTables[4]{};
std::atomic<UINT> ExecutableShaderQueries{0};
std::atomic<UINT> UnsupportedCalls{0};

void reportError(D3D12DDI_HDEVICE Device, HRESULT Result) {
  auto *State = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (State && State->UMCallbacks && State->UMCallbacks->pfnSetErrorCb &&
      State->RTDevice) {
    D3D12DDI_HRTDEVICE RTDevice{};
    RTDevice.handle = State->RTDevice;
    State->UMCallbacks->pfnSetErrorCb(RTDevice, Result);
  } else {
    OutputDebugStringA(
        "FeMe WARP DDI error without a runtime error callback.\n");
  }
}

// Preserve every public callback's calling convention and return type.
// Unsupported operations must fail, including void and private-size DDIs.
template <typename Function, typename Entry> struct UnsupportedDDI;
template <typename Entry, typename Result, typename Handle, typename... Args>
struct UnsupportedDDI<Result(APIENTRY *)(Handle, Args...), Entry> {
  static Result APIENTRY call(Handle Owner, Args...) {
    LastUnsupported.store(Entry::Name);
    ++UnsupportedCalls;
    std::fprintf(stderr, "FeMe WARP unavailable DDI: %s\n", Entry::Name);
    if constexpr (std::is_same_v<Handle, D3D12DDI_HDEVICE>)
      reportError(Owner, E_NOTIMPL);
    // Non-device callbacks belong to objects whose creation is rejected.
    // They cannot have a legitimate owner until that feature is implemented.
    if constexpr (std::is_same_v<Result, HRESULT>)
      return E_NOTIMPL;
    else if constexpr (std::is_same_v<
                           Result, D3D12DDI_DRIVER_MATCHING_IDENTIFIER_STATUS>)
      return D3D12DDI_DRIVER_MATCHING_IDENTIFIER_UNSUPPORTED_TYPE;
    else if constexpr (!std::is_void_v<Result>)
      return {};
  }
};

#define WARP_CORE_DDI(Slot)                                                    \
  struct Slot##Entry {                                                         \
    static constexpr const char *Name = #Slot;                                 \
  };
#define WARP_LIST_DDI(Name)
#include "WarpTableEntries.def"
#undef WARP_CORE_DDI
#undef WARP_LIST_DDI

// Use separate tags because core and command-list tables share entry names.
#define WARP_CORE_DDI(Name)
#define WARP_LIST_DDI(Slot)                                                    \
  struct Slot##ListEntry {                                                     \
    static constexpr const char *Name = #Slot;                                 \
  };
#include "WarpTableEntries.def"
#undef WARP_CORE_DDI
#undef WARP_LIST_DDI

#define QUEUE_ENTRY(Slot)                                                      \
  struct Slot##QueueEntry {                                                    \
    static constexpr const char *Name = #Slot;                                 \
  };
QUEUE_ENTRY(pfnExecuteCommandLists)
QUEUE_ENTRY(pfnUpdateTileMappings)
QUEUE_ENTRY(pfnCopyTileMappings)
QUEUE_ENTRY(pfnSignalFence)
QUEUE_ENTRY(pfnWaitForFence)
#undef QUEUE_ENTRY

void APIENTRY queryNodeMap(D3D12DDI_HDEVICE Device, UINT Count, UINT *Map) {
  if (!Map || Count != 1) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  Map[0] = 0;
}

UINT APIENTRY implicitAdapterMask(D3D12DDI_HDEVICE) { return 0; }

void APIENTRY checkFormat(D3D12DDI_HDEVICE Device, DXGI_FORMAT, UINT *Support) {
  if (!Support) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  // The buffer-only diagnostic profile advertises no texture/view formats.
  *Support = 0;
}

void APIENTRY checkMultisample(D3D12DDI_HDEVICE Device, DXGI_FORMAT, UINT,
                               D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAGS,
                               UINT *QualityLevels) {
  if (!QualityLevels) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  *QualityLevels = 0;
}

struct EmptyRootSignature {
  feme::probe::WarpDeviceState *Owner;
  D3D12DDI_ROOT_SIGNATURE_FLAGS Flags;
};

HRESULT validateEmptyRoot(const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0013 *Args) {
  if (!Args || !Args->pRootSignature_1_1 || Args->NodeMask != 1)
    return E_INVALIDARG;
  if (Args->Version != D3D12DDI_ROOT_SIGNATURE_VERSION_1_1 ||
      Args->pRootSignature_1_1->NumParameters ||
      Args->pRootSignature_1_1->NumStaticSamplers)
    return E_NOTIMPL;
  return S_OK;
}

SIZE_T APIENTRY rootSize(D3D12DDI_HDEVICE Device,
                         const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0013 *Args) {
  HRESULT Result = validateEmptyRoot(Args);
  if (FAILED(Result)) {
    reportError(Device, Result);
    return 0;
  }
  return sizeof(EmptyRootSignature);
}

HRESULT APIENTRY createRoot(D3D12DDI_HDEVICE Device,
                            const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0013 *Args,
                            D3D12DDI_HROOTSIGNATURE Root) {
  HRESULT Result = validateEmptyRoot(Args);
  if (FAILED(Result))
    return Result;
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (!Owner || !Root.pDrvPrivate)
    return E_INVALIDARG;
  new (Root.pDrvPrivate)
      EmptyRootSignature{Owner, Args->pRootSignature_1_1->Flags};
  ++Owner->LiveRootSignatures;
  return S_OK;
}

void APIENTRY destroyRoot(D3D12DDI_HDEVICE Device,
                          D3D12DDI_HROOTSIGNATURE Root) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  auto *State = static_cast<EmptyRootSignature *>(Root.pDrvPrivate);
  if (!Owner || !State || State->Owner != Owner) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  State->~EmptyRootSignature();
  --Owner->LiveRootSignatures;
}

struct NullShader {
  feme::probe::WarpDeviceState *Owner;
};

HRESULT validateNullShader(const D3D12DDIARG_CREATE_SHADER_0026 *Args) {
  if (!Args)
    return E_INVALIDARG;
  if (Args->pShaderCode || Args->LibraryReference.hLibrary.pDrvPrivate ||
      Args->LibraryReference.PipelineIndex || Args->Flags ||
      Args->IOSignatures.Standard) {
    std::fprintf(stderr,
                 "FeMe WARP only implements null-stage shader objects.\n");
    return E_NOTIMPL;
  }
  return S_OK;
}

SIZE_T APIENTRY shaderSize(D3D12DDI_HDEVICE Device,
                           const D3D12DDIARG_CREATE_SHADER_0026 *Args) {
  HRESULT Result = validateNullShader(Args);
  if (FAILED(Result)) {
    if (Args && Args->pShaderCode) {
      ++ExecutableShaderQueries;
      LastUnsupported.store("pfnCalcPrivateShaderSize");
    }
    reportError(Device, Result);
    return 0;
  }
  return sizeof(NullShader);
}

void APIENTRY createNullShader(D3D12DDI_HDEVICE Device,
                               const D3D12DDIARG_CREATE_SHADER_0026 *Args,
                               D3D12DDI_HSHADER Shader) {
  HRESULT Result = validateNullShader(Args);
  if (FAILED(Result)) {
    reportError(Device, Result);
    return;
  }
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (!Owner || !Shader.pDrvPrivate) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  new (Shader.pDrvPrivate) NullShader{Owner};
  ++Owner->LiveNullShaders;
}

void APIENTRY destroyShader(D3D12DDI_HDEVICE Device, D3D12DDI_HSHADER Shader) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  auto *State = static_cast<NullShader *>(Shader.pDrvPrivate);
  if (!Owner || !State || State->Owner != Owner) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  State->~NullShader();
  --Owner->LiveNullShaders;
}

HRESULT APIENTRY extendedFeatures(D3D12DDI_HDEVICE, UINT32 *Count,
                                  D3D12DDI_FEATURE_0020 *) {
  if (!Count)
    return E_INVALIDARG;
  *Count = 0;
  return S_OK;
}

HRESULT APIENTRY extendedVersions(D3D12DDI_HDEVICE, D3D12DDI_FEATURE_0020,
                                  UINT32 *Count, UINT32 *) {
  if (!Count)
    return E_INVALIDARG;
  *Count = 0;
  return E_NOTIMPL;
}

HRESULT APIENTRY enableExtendedFeature(D3D12DDI_HDEVICE, D3D12DDI_FEATURE_0020,
                                       UINT32) {
  return E_NOTIMPL;
}

HRESULT APIENTRY extendedCallbacks(D3D12DDI_HDEVICE, D3D12DDI_TABLE_TYPE,
                                   const void *, SIZE_T) {
  return E_NOTIMPL;
}

template <typename Table>
HRESULT publish(void *Destination, SIZE_T Size, const Table &Source,
                UINT AuditIndex) {
  if (!Destination || Size != sizeof(Table))
    return E_INVALIDARG;
  std::memcpy(Destination, &Source, sizeof(Source));
  ++PublishedTables[AuditIndex];
  return S_OK;
}
} // namespace

HRESULT feme::probe::fillWarpPrototypeTable(D3D12DDI_TABLE_TYPE Type,
                                            void *Destination, SIZE_T Size,
                                            UINT TableIndex) {
  if (Type != D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D && TableIndex)
    return E_INVALIDARG;
  switch (Type) {
  case D3D12DDI_TABLE_TYPE_DEVICE_CORE: {
    D3D12DDI_DEVICE_FUNCS_CORE_0072 Table{};
#define WARP_CORE_DDI(Name)                                                    \
  Table.Name = UnsupportedDDI<decltype(Table.Name), Name##Entry>::call;
#define WARP_LIST_DDI(Name)
#include "WarpTableEntries.def"
#undef WARP_CORE_DDI
#undef WARP_LIST_DDI
    populateWarpHeapDDIs(Table);
    Table.pfnQueryNodeMap = queryNodeMap;
    Table.pfnGetImplicitPhysicalAdapterMask = implicitAdapterMask;
    Table.pfnCheckFormatSupport = checkFormat;
    Table.pfnCheckMultisampleQualityLevels = checkMultisample;
    Table.pfnCalcPrivateRootSignatureSize = rootSize;
    Table.pfnCreateRootSignature = createRoot;
    Table.pfnDestroyRootSignature = destroyRoot;
    Table.pfnCalcPrivateShaderSize = shaderSize;
    Table.pfnCalcPrivateTessellationShaderSize = shaderSize;
    Table.pfnCreateVertexShader = createNullShader;
    Table.pfnCreatePixelShader = createNullShader;
    Table.pfnCreateGeometryShader = createNullShader;
    Table.pfnCreateComputeShader = createNullShader;
    Table.pfnCreateHullShader = createNullShader;
    Table.pfnCreateDomainShader = createNullShader;
    Table.pfnDestroyShader = destroyShader;
    return publish(Destination, Size, Table, 0);
  }
  case D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D: {
    D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 Table{};
    Table.pfnExecuteCommandLists =
        UnsupportedDDI<decltype(Table.pfnExecuteCommandLists),
                       pfnExecuteCommandListsQueueEntry>::call;
    Table.pfnUpdateTileMappings =
        UnsupportedDDI<decltype(Table.pfnUpdateTileMappings),
                       pfnUpdateTileMappingsQueueEntry>::call;
    Table.pfnCopyTileMappings =
        UnsupportedDDI<decltype(Table.pfnCopyTileMappings),
                       pfnCopyTileMappingsQueueEntry>::call;
    Table.pfnSignalFence = UnsupportedDDI<decltype(Table.pfnSignalFence),
                                          pfnSignalFenceQueueEntry>::call;
    Table.pfnWaitForFence = UnsupportedDDI<decltype(Table.pfnWaitForFence),
                                           pfnWaitForFenceQueueEntry>::call;
    return publish(Destination, Size, Table, 1);
  }
  case D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D: {
    if (TableIndex >= 2)
      return E_INVALIDARG;
    D3D12DDI_COMMAND_LIST_FUNCS_3D_0062 Table{};
#define WARP_CORE_DDI(Name)
#define WARP_LIST_DDI(Name)                                                    \
  Table.Name = UnsupportedDDI<decltype(Table.Name), Name##ListEntry>::call;
#include "WarpTableEntries.def"
#undef WARP_CORE_DDI
#undef WARP_LIST_DDI
    return publish(Destination, Size, Table, 2);
  }
  case D3D12DDI_TABLE_TYPE_0020_EXTENDED_FEATURES: {
    D3D12DDI_EXTENDED_FEATURES_FUNCS_0021 Table{
        extendedFeatures, extendedVersions, enableExtendedFeature,
        extendedCallbacks};
    return publish(Destination, Size, Table, 3);
  }
  default:
    return E_NOTIMPL;
  }
}

extern "C" __declspec(dllexport) const char *APIENTRY
FeMeWarpGetLastUnsupportedDDI() {
  return LastUnsupported.load();
}

extern "C" __declspec(dllexport) void APIENTRY
FeMeWarpGetTableAudit(feme::probe::WarpPrototypeTableAudit *Audit) {
  if (!Audit) {
    OutputDebugStringA("FeMe WARP table audit requires an output pointer.\n");
    return;
  }
  *Audit = {PublishedTables[0].load(),      PublishedTables[1].load(),
            PublishedTables[2].load(),      PublishedTables[3].load(),
            ExecutableShaderQueries.load(), UnsupportedCalls.load()};
}
