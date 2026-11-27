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
// The default/bootstrap variants deliberately return E_NOTIMPL; the W0(g)
// audit variant additionally initializes WARP KMD objects but still does not
// implement a real device, resource, or command contract. By default no
// supported DDI versions are advertised. W0(g)'s audit variant defines
// FEME_BOOTSTRAP_PINNED_VERSION (one public D3D12DDI_SUPPORTED_00NN value) to
// observe what the stock runtime asks for once negotiation can succeed. The
// pre-device GetCaps queries observed by W0(g) receive minimal responses:
// D3D12_OPTIONS reports only baseline tiers, optional shader caps are clear,
// and the pipeline query is capped at feature level 11_0. These negotiation
// responses are only probes; the shim implements no graphics/resource work.
//
// The W0(g) callback-table audit checks individual function pointers without
// invoking them. The separate WARP-device-context variant does invoke the
// core-layer allocation callback for its private DMA buffer and matching
// teardown; it still does not create application resources.
// FEME_BOOTSTRAP_PROBE_CALLBACK additionally invokes exactly one pure
// user-mode, non-allocation callback (pfnSetErrorCb, the documented
// mechanism for a driver to report a device error to the runtime) with a
// truthful E_NOTIMPL, wrapped in SEH (__try/__except) so an unexpected fault
// is observed rather than silently tearing down the whole process.
//
// FEME_BOOTSTRAP_PROBE_PAGING_QUEUE is a separate opt-in experiment using
// the public KT create/destroy paging-queue callbacks and hRTDevice handle.
// It supplies no private driver data and submits no memory or GPU work.
// A successful round trip still ends in E_NOTIMPL, not a working device;
// callback failures and exceptions are preserved in the probe telemetry.
//
// FEME_BOOTSTRAP_PROBE_GPU_VA separately reserves/frees 64 KiB of unmapped
// GPU address space through the public KT callbacks. This is not allocation
// or CPU memory. A rejected reservation is reported without attempting free.
//
//===----------------------------------------------------------------------===//

#include "BootstrapAudit.h"
#include "WarpDeviceState.h"

#include <windows.h>
#include <winternl.h>

#include <d3d10umddi.h>
#include <d3d12umddi.h>
#include <dxgi.h>

#include <cstring>
#include <limits>
#include <new>

#if defined(FEME_BOOTSTRAP_PROBE_CALLBACK) &&                                  \
    defined(FEME_BOOTSTRAP_PROBE_PAGING_QUEUE)
#error "Run diagnostic-error and paging-queue probes separately."
#endif

#if defined(FEME_BOOTSTRAP_PROBE_GPU_VA) &&                                    \
    (defined(FEME_BOOTSTRAP_PROBE_CALLBACK) ||                                 \
     defined(FEME_BOOTSTRAP_PROBE_PAGING_QUEUE))
#error "Run GPU VA reservation separately from the other callback probes."
#endif

#if defined(FEME_BOOTSTRAP_PROBE_MONITORED_FENCE) &&                           \
    (defined(FEME_BOOTSTRAP_PROBE_CALLBACK) ||                                 \
     defined(FEME_BOOTSTRAP_PROBE_PAGING_QUEUE) ||                             \
     defined(FEME_BOOTSTRAP_PROBE_GPU_VA))
#error "Run monitored-fence probing separately from the other callback probes."
#endif

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
HANDLE GWarpRuntimeAdapter = nullptr;

// W0(g)'s audit evidence: which public D3D12DDICAPS_TYPE values GetCaps is
// queried with, and whether CreateDevice's publicly documented thunk-service
// pointers (pKTCallbacks/p12UMCallbacks) are populated. Logging this does not
// implement any capability or device contract.
constexpr UINT32 MaxCapsTypeLog = 32;
LONG GCapsTypeLog[MaxCapsTypeLog] = {};
UINT32 GCapsDataSizeLog[MaxCapsTypeLog] = {};
LONG GCapsResultLog[MaxCapsTypeLog] = {};
LONG GCapsTypeLogCount = 0; // Total GetCaps calls observed, unclamped.

struct CreateDeviceAudit {
  LONG Captured = 0;
  UINT Interface = 0;
  UINT Version = 0;
  LONG HasKTCallbacks = 0;
  LONG HasUMCallbacks = 0;
  // W0(g) follow-up: individual pointer null-checks, read-only, one level
  // deeper than the outer table pointers above.
  LONG HasKTAllocateCb = 0;
  LONG HasKTDeallocateCb = 0;
  LONG HasUMSetErrorCb = 0;
};
CreateDeviceAudit GCreateDeviceAudit;
feme::probe::BootstrapContractAudit GContractAudit;
feme::probe::PagingQueueProbe GPagingQueueProbe;
feme::probe::GpuVirtualAddressProbe GGpuVirtualAddressProbe;
feme::probe::MonitoredFenceProbe GMonitoredFenceProbe;
feme::probe::WarpPrivateAdapterInfoProbe GWarpPrivateAdapterInfoProbe;
feme::probe::WarpContextProbe GWarpContextProbe;
feme::probe::WarpDeviceContextProbe GWarpDeviceContextProbe;

#ifdef FEME_BOOTSTRAP_PROBE_WARP_PRIVATE_INFO
struct WarpPrivateAdapterInfo {
  UINT32 Version;
  UINT32 WddmVersion;
  UINT32 DriverCaps;
};
static_assert(sizeof(WarpPrivateAdapterInfo) == 3 * sizeof(UINT32));
#endif

#if defined(FEME_BOOTSTRAP_PROBE_WARP_CONTEXT) ||                              \
    defined(FEME_BOOTSTRAP_PROBE_WARP_DEVICE_CONTEXT)
struct WarpPrivateContextBuffer {
  UINT64 *Buffer;
  UINT64 *BufferSize;
};
static_assert(sizeof(WarpPrivateContextBuffer) == 2 * sizeof(UINT64 *));
#endif

#ifdef FEME_BOOTSTRAP_PROBE_WARP_DEVICE_CONTEXT
struct WarpQueryVersionNumber {
  UINT32 EscapeId;
  UINT32 Version;
};

static_assert(sizeof(WarpQueryVersionNumber) == 2 * sizeof(UINT32));
constexpr UINT32 WarpEscapeQueryVersionNumber = 3;
constexpr UINT32 WarpBasicRenderVersion = 2;
constexpr UINT32 WarpDriverCapsFullWddmDevice = 1u << 3;
constexpr UINT32 WarpDriverCapsSupportsVirtualGpu = 1u << 4;

struct WarpPreferredGpuNode {
  UINT32 NodeType;
  UINT32 NodeOrdinal;
  UINT32 EngineAffinity;
};

struct WarpQueryPreferredGpuNode {
  UINT32 EscapeId;
  WarpPreferredGpuNode PreferredNode;
};

static_assert(sizeof(WarpQueryPreferredGpuNode) == 4 * sizeof(UINT32));
constexpr UINT32 WarpEscapeQueryPreferredGpuNode = 1;

struct WarpInitContextVirtual2 {
  UINT32 EscapeId;
  union {
    void *UMContext;
    UINT64 KernelContextHandle;
  };
  void *CommandBuffer;
  UINT32 CommandBufferSize;
  UINT64 DmaBufferAddress;
  UINT32 DmaBufferSize;
};

static_assert(offsetof(WarpInitContextVirtual2, UMContext) == 8);
static_assert(offsetof(WarpInitContextVirtual2, CommandBuffer) == 16);
static_assert(offsetof(WarpInitContextVirtual2, DmaBufferAddress) == 32);
static_assert(offsetof(WarpInitContextVirtual2, DmaBufferSize) == 40);
static_assert(sizeof(WarpInitContextVirtual2) == 48);
constexpr UINT32 WarpEscapeInitContextVirtual2 = 7;

struct WarpKmdAllocationInfo {
  D3D10DDIRESOURCE_TYPE ResourceDimension;
  UINT32 Width;
  UINT32 Height;
  UINT32 Depth;
  DXGI_FORMAT Format;
  UINT32 Pitch;
  UINT32 TotalSize;
  UINT32 ArraySize;
  UINT32 MipLevels;
  UINT32 Usage;
  UINT32 BindFlags;
  DXGI_SAMPLE_DESC SampleDesc;
  D3DDDI_RATIONAL RefreshRate;
  D3DDDI_ROTATION Rotation;
  D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
  union {
    UINT32 VirtualGpuAlignment;
    HANDLE FlipStart;
    UINT64 FlipStartAlignment;
  };
  union {
    struct {
      UINT32 VirtualGpuSegmentId;
      UINT32 VirtualGpuPrivateData;
    };
    HANDLE FlipFinished;
    UINT64 FlipFinishedAlignment;
  };
  UINT32 Flags;
};

static_assert(sizeof(WarpKmdAllocationInfo) == 96);
static_assert(offsetof(WarpKmdAllocationInfo, Flags) == 88);
constexpr UINT32 WarpAllocationFlagInAperture = 1u << 7;

struct WarpInitDeviceEscape {
  UINT32 EscapeId;
  union {
    void *UMDevice;
    UINT64 UMDeviceAlignment;
  };
  union {
    void *CommandBuffer;
    UINT64 CommandBufferAlignment;
  };
  UINT32 CommandBufferSize;
  INT32 WddmV2Device;
  INT32 FullWddmDevice;
  union {
    void *DisplaySet;
    UINT64 DisplaySetAlignment;
  };
  UINT32 Flags;
};

static_assert(offsetof(WarpInitDeviceEscape, UMDevice) == 8);
static_assert(offsetof(WarpInitDeviceEscape, CommandBuffer) == 16);
static_assert(offsetof(WarpInitDeviceEscape, DisplaySet) == 40);
static_assert(offsetof(WarpInitDeviceEscape, Flags) == 48);
static_assert(sizeof(WarpInitDeviceEscape) == 56);
constexpr UINT32 WarpEscapeInitDevice = 0;
#endif

#ifdef FEME_BOOTSTRAP_PROBE_WARP_CONTEXT
HRESULT probeWarpContext(const D3D12DDIARG_CREATEDEVICE_0003 *Args) {
  auto &Probe = GWarpContextProbe;
  Probe.CreateAttempted = true;
  if (!Args || !Args->hRTDevice.handle)
    return E_INVALIDARG;
  if (!Args->pKTCallbacks || !Args->pKTCallbacks->pfnCreateContextVirtualCb ||
      !Args->pKTCallbacks->pfnDestroyContextCb)
    return E_NOINTERFACE;

  Probe.CommandBufferSize = sizeof(Probe.CommandBufferAddress);
  WarpPrivateContextBuffer PrivateData{};
  PrivateData.Buffer = &Probe.CommandBufferAddress;
  PrivateData.BufferSize = &Probe.CommandBufferSize;

  D3DDDICB_CREATECONTEXTVIRTUAL CreateContext{};
  CreateContext.pPrivateDriverData = &PrivateData;
  CreateContext.PrivateDriverDataSize = sizeof(PrivateData);
  DWORD ExceptionCode = 0;
  __try {
    Probe.CreateResult = Args->pKTCallbacks->pfnCreateContextVirtualCb(
        Args->hRTDevice.handle, &CreateContext);
    Probe.CreateReturned = true;
  } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    Probe.ExceptionCode = ExceptionCode;
    return E_FAIL;
  }
  if (Probe.CreateResult != S_OK)
    return FAILED(Probe.CreateResult) ? Probe.CreateResult : E_FAIL;

  Probe.HasContext = CreateContext.hContext != nullptr;
  Probe.PrivateDataPointersIntact =
      PrivateData.Buffer == &Probe.CommandBufferAddress &&
      PrivateData.BufferSize == &Probe.CommandBufferSize &&
      CreateContext.pPrivateDriverData == &PrivateData;
  if (!Probe.HasContext)
    return E_UNEXPECTED;

  HRESULT Result =
      Probe.PrivateDataPointersIntact && Probe.CommandBufferSize != 0
          ? S_OK
          : E_UNEXPECTED;
  D3DDDICB_DESTROYCONTEXT DestroyContext{};
  DestroyContext.hContext = CreateContext.hContext;
  Probe.DestroyAttempted = true;
  __try {
    Probe.DestroyResult = Args->pKTCallbacks->pfnDestroyContextCb(
        Args->hRTDevice.handle, &DestroyContext);
    Probe.DestroyReturned = true;
  } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    Probe.ExceptionCode = ExceptionCode;
    return E_FAIL;
  }
  if (Probe.DestroyResult != S_OK)
    return FAILED(Probe.DestroyResult) ? Probe.DestroyResult : E_FAIL;
  return Result;
}
#endif

#ifdef FEME_BOOTSTRAP_PROBE_WARP_DEVICE_CONTEXT
using feme::probe::WarpDeviceState;

void destroyWarpDeviceContext(D3D12DDI_HDEVICE Device);

HRESULT allocateWarpDmaBuffer(WarpDeviceState *State) {
  auto &Probe = GWarpDeviceContextProbe;
  if (!State->UMCallbacks || !State->UMCallbacks->pfnAllocateCb ||
      !State->KTCallbacks->pfnLockCb)
    return E_NOINTERFACE;
  if (!Probe.DmaBufferSize)
    return E_INVALIDARG;

  WarpKmdAllocationInfo PrivateInfo{};
  PrivateInfo.ResourceDimension = D3D10DDIRESOURCE_BUFFER;
  PrivateInfo.Width = Probe.DmaBufferSize;
  PrivateInfo.Height = 1;
  PrivateInfo.Depth = 1;
  PrivateInfo.Format = DXGI_FORMAT_UNKNOWN;
  PrivateInfo.Pitch = Probe.DmaBufferSize;
  PrivateInfo.TotalSize = Probe.DmaBufferSize;
  PrivateInfo.ArraySize = 1;
  PrivateInfo.MipLevels = 1;
  PrivateInfo.Flags = WarpAllocationFlagInAperture;

  D3D12DDI_ALLOCATION_INFO_0022 AllocationInfo{};
  AllocationInfo.pPrivateDriverData = &PrivateInfo;
  AllocationInfo.PrivateDriverDataSize = sizeof(PrivateInfo);
  D3D12DDICB_ALLOCATE_0022 Allocate{};
  Allocate.NumAllocations = 1;
  Allocate.pAllocationInfo = &AllocationInfo;

  D3D12DDI_HRTDEVICE RTDevice{};
  RTDevice.handle = State->RTDevice;
  Probe.DmaAllocationAttempted = true;
  bool CallbackReturned = false;
  DWORD ExceptionCode = 0;
  __try {
    Probe.DmaAllocationResult =
        State->UMCallbacks->pfnAllocateCb(RTDevice, &Allocate);
    CallbackReturned = true;
  } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    Probe.ExceptionCode = ExceptionCode;
  }
  Probe.DmaAllocationReturned = CallbackReturned;
  State->DmaAllocation = AllocationInfo.hAllocation;
  Probe.DmaAllocationHandle = State->DmaAllocation;
  Probe.DmaAllocationCreated = State->DmaAllocation != 0;
  if (!CallbackReturned)
    return E_FAIL;
  if (FAILED(Probe.DmaAllocationResult))
    return Probe.DmaAllocationResult;
  if (!State->DmaAllocation)
    return E_UNEXPECTED;

  D3DDDICB_LOCK Lock{};
  Lock.hAllocation = State->DmaAllocation;
  Lock.Flags.IgnoreSync = 1;
  Lock.Flags.DonotWait = 1;
  Probe.DmaLockAttempted = true;
  CallbackReturned = false;
  __try {
    Probe.DmaLockResult = State->KTCallbacks->pfnLockCb(State->RTDevice, &Lock);
    CallbackReturned = true;
  } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    Probe.ExceptionCode = ExceptionCode;
  }
  Probe.DmaLockReturned = CallbackReturned;
  State->DmaCpuAddress = Lock.pData;
  State->DmaLocked = CallbackReturned && SUCCEEDED(Probe.DmaLockResult);
  Probe.DmaCpuAddress = reinterpret_cast<ULONG_PTR>(State->DmaCpuAddress);
  if (!CallbackReturned)
    return E_FAIL;
  if (FAILED(Probe.DmaLockResult))
    return Probe.DmaLockResult;
  return State->DmaCpuAddress ? S_OK : E_UNEXPECTED;
}

HRESULT createWarpDmaFence(WarpDeviceState *State) {
  auto &Probe = GWarpDeviceContextProbe;
  if (!State->KTCallbacks->pfnCreateSynchronizationObject2Cb ||
      !State->KTCallbacks->pfnDestroySynchronizationObjectCb)
    return E_NOINTERFACE;

  D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 Create{};
  Create.Info.Type = D3DDDI_MONITORED_FENCE;
  Create.Info.MonitoredFence.InitialFenceValue = 0;
  Probe.DmaFenceCreateAttempted = true;
  bool CallbackReturned = false;
  DWORD ExceptionCode = 0;
  __try {
    Probe.DmaFenceCreateResult =
        State->KTCallbacks->pfnCreateSynchronizationObject2Cb(State->RTDevice,
                                                              &Create);
    CallbackReturned = true;
  } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    Probe.ExceptionCode = ExceptionCode;
  }
  Probe.DmaFenceCreateReturned = CallbackReturned;
  State->DmaFence = Create.hSyncObject;
  State->DmaFenceValue = static_cast<volatile UINT64 *>(
      Create.Info.MonitoredFence.FenceValueCPUVirtualAddress);
  Probe.DmaFenceHandle = State->DmaFence;
  Probe.DmaFenceCreated =
      State->DmaFence != 0 && State->DmaFenceValue != nullptr;
  if (!CallbackReturned)
    return E_FAIL;
  if (FAILED(Probe.DmaFenceCreateResult))
    return Probe.DmaFenceCreateResult;
  return Probe.DmaFenceCreated ? S_OK : E_UNEXPECTED;
}

HRESULT initializeWarpKmdDevice(WarpDeviceState *State, HANDLE RTAdapter) {
  auto &Probe = GWarpDeviceContextProbe;
  WarpInitDeviceEscape InitDevice{};
  InitDevice.EscapeId = WarpEscapeInitDevice;
  InitDevice.UMDevice = State;
  InitDevice.CommandBuffer = State->UserModeContext.CommandBuffer;
  InitDevice.CommandBufferSize = State->UserModeContext.CommandBufferSize;
  InitDevice.Flags = 1u | (IsDebuggerPresent() ? 2u : 0u);

  D3DDDICB_ESCAPE Escape{};
  Escape.hDevice = State->RTDevice;
  Escape.pPrivateDriverData = &InitDevice;
  Escape.PrivateDriverDataSize = sizeof(InitDevice);
  Probe.InitDeviceEscapeAttempted = true;
  bool CallbackReturned = false;
  DWORD ExceptionCode = 0;
  __try {
    Probe.InitDeviceEscapeResult =
        State->KTCallbacks->pfnEscapeCb(RTAdapter, &Escape);
    CallbackReturned = true;
  } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    Probe.ExceptionCode = ExceptionCode;
  }
  Probe.InitDeviceEscapeReturned = CallbackReturned;
  if (!CallbackReturned)
    return E_FAIL;
  return FAILED(Probe.InitDeviceEscapeResult) ? Probe.InitDeviceEscapeResult
                                              : S_OK;
}

HRESULT createWarpDeviceContext(const D3D12DDIARG_CREATEDEVICE_0003 *Args,
                                HANDLE RTAdapter) {
  auto &Probe = GWarpDeviceContextProbe;
  Probe.CreateAttempted = true;
  if (!Args || !Args->hRTDevice.handle || !Args->hDrvDevice.pDrvPrivate)
    return E_INVALIDARG;
  if (!RTAdapter || !Args->pKTCallbacks || !Args->pKTCallbacks->pfnEscapeCb ||
      !Args->pKTCallbacks->pfnCreatePagingQueueCb ||
      !Args->pKTCallbacks->pfnDestroyPagingQueueCb ||
      !Args->pKTCallbacks->pfnCreateContextVirtualCb ||
      !Args->pKTCallbacks->pfnDestroyContextCb)
    return E_NOINTERFACE;

  auto *State = ::new (Args->hDrvDevice.pDrvPrivate) WarpDeviceState{};
  State->KTCallbacks = Args->pKTCallbacks;
  State->RTDevice = Args->hRTDevice.handle;
  UINT64 DdiVersion =
      (static_cast<UINT64>(Args->Interface) << 32) | Args->Version;
  if (DdiVersion >= D3D12DDI_SUPPORTED_0022 && Args->p12UMCallbacks) {
    State->UMCallbacks =
        reinterpret_cast<const D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 *>(
            Args->p12UMCallbacks);
  }
  if (!State->UMCallbacks || !State->UMCallbacks->pfnAllocateCb ||
      !State->UMCallbacks->pfnDeallocateCb)
    return E_NOINTERFACE;
  DWORD ExceptionCode = 0;
  UINT32 DriverCaps = GWarpPrivateAdapterInfoProbe.DriverCaps;
  if (!(DriverCaps & WarpDriverCapsFullWddmDevice)) {
    WarpQueryVersionNumber VersionQuery{WarpEscapeQueryVersionNumber,
                                        WarpBasicRenderVersion};
    D3DDDICB_ESCAPE VersionEscape{};
    VersionEscape.hDevice = State->RTDevice;
    VersionEscape.pPrivateDriverData = &VersionQuery;
    VersionEscape.PrivateDriverDataSize = sizeof(VersionQuery);
    Probe.VersionNumberEscapeAttempted = true;
    __try {
      Probe.VersionNumberEscapeResult =
          State->KTCallbacks->pfnEscapeCb(RTAdapter, &VersionEscape);
      Probe.VersionNumberEscapeReturned = true;
    } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
      Probe.ExceptionCode = ExceptionCode;
      return E_FAIL;
    }
    Probe.BasicRenderVersion = VersionQuery.Version;
    if (Probe.VersionNumberEscapeResult != S_OK)
      return FAILED(Probe.VersionNumberEscapeResult)
                 ? Probe.VersionNumberEscapeResult
                 : E_FAIL;
    if (Probe.BasicRenderVersion != WarpBasicRenderVersion)
      return E_OUTOFMEMORY;
  }

  if (!(DriverCaps & WarpDriverCapsSupportsVirtualGpu)) {
    WarpQueryPreferredGpuNode PreferredNode{};
    PreferredNode.EscapeId = WarpEscapeQueryPreferredGpuNode;
    D3DDDICB_ESCAPE Escape{};
    Escape.hDevice = State->RTDevice;
    Escape.pPrivateDriverData = &PreferredNode;
    Escape.PrivateDriverDataSize = sizeof(PreferredNode);
    Probe.PreferredNodeEscapeAttempted = true;
    __try {
      Probe.PreferredNodeEscapeResult =
          State->KTCallbacks->pfnEscapeCb(RTAdapter, &Escape);
      Probe.PreferredNodeEscapeReturned = true;
    } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
      Probe.ExceptionCode = ExceptionCode;
      return E_FAIL;
    }
    if (Probe.PreferredNodeEscapeResult != S_OK)
      return FAILED(Probe.PreferredNodeEscapeResult)
                 ? Probe.PreferredNodeEscapeResult
                 : E_FAIL;
    Probe.PreferredNodeOrdinal = PreferredNode.PreferredNode.NodeOrdinal;
    Probe.PreferredEngineAffinity = PreferredNode.PreferredNode.EngineAffinity;
    State->NodeOrdinal = Probe.PreferredNodeOrdinal;
    State->EngineAffinity = Probe.PreferredEngineAffinity;
    if (State->EngineAffinity != 1)
      State->EngineAffinity = 1;
  }

  D3DDDICB_CREATEPAGINGQUEUE PagingQueue{};
  Probe.PagingQueueCreateAttempted = true;
  bool PagingQueueCallbackReturned = false;
  __try {
    Probe.PagingQueueCreateResult = State->KTCallbacks->pfnCreatePagingQueueCb(
        State->RTDevice, &PagingQueue);
    PagingQueueCallbackReturned = true;
  } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    Probe.ExceptionCode = ExceptionCode;
  }
  State->PagingQueue = PagingQueue.hPagingQueue;
  State->PagingSyncObject = PagingQueue.hSyncObject;
  State->PagingFenceValue =
      static_cast<volatile UINT64 *>(PagingQueue.FenceValueCPUVirtualAddress);
  Probe.PagingQueueCreated = State->PagingQueue != 0;
  Probe.PagingQueueHasSyncObject = State->PagingSyncObject != 0;
  Probe.PagingQueueHasFenceMapping = State->PagingFenceValue != nullptr;
  if (!PagingQueueCallbackReturned || Probe.PagingQueueCreateResult != S_OK ||
      !Probe.PagingQueueCreated || !Probe.PagingQueueHasSyncObject ||
      !Probe.PagingQueueHasFenceMapping) {
    if (State->PagingQueue) {
      D3D12DDI_HDEVICE Device{};
      Device.pDrvPrivate = State;
      destroyWarpDeviceContext(Device);
    }
    if (!PagingQueueCallbackReturned)
      return E_FAIL;
    return FAILED(Probe.PagingQueueCreateResult) ? Probe.PagingQueueCreateResult
                                                 : E_UNEXPECTED;
  }

  State->CommandBufferSize = sizeof(State->CommandBufferAddress);

  WarpPrivateContextBuffer PrivateData{};
  PrivateData.Buffer = &State->CommandBufferAddress;
  PrivateData.BufferSize = &State->CommandBufferSize;
  D3DDDICB_CREATECONTEXTVIRTUAL CreateContext{};
  CreateContext.NodeOrdinal = State->NodeOrdinal;
  CreateContext.EngineAffinity = State->EngineAffinity;
  CreateContext.pPrivateDriverData = &PrivateData;
  CreateContext.PrivateDriverDataSize = sizeof(PrivateData);

  bool ContextCallbackReturned = false;
  __try {
    Probe.CreateContextResult = State->KTCallbacks->pfnCreateContextVirtualCb(
        State->RTDevice, &CreateContext);
    ContextCallbackReturned = true;
  } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    Probe.ExceptionCode = ExceptionCode;
  }
  State->Context = CreateContext.hContext;
  Probe.ContextCreated = State->Context != nullptr;
  if (!ContextCallbackReturned || Probe.CreateContextResult != S_OK) {
    D3D12DDI_HDEVICE Device{};
    Device.pDrvPrivate = State;
    destroyWarpDeviceContext(Device);
    if (!ContextCallbackReturned)
      return E_FAIL;
    return FAILED(Probe.CreateContextResult) ? Probe.CreateContextResult
                                             : E_FAIL;
  }

  Probe.PrivateDataPointersIntact =
      PrivateData.Buffer == &State->CommandBufferAddress &&
      PrivateData.BufferSize == &State->CommandBufferSize &&
      CreateContext.pPrivateDriverData == &PrivateData;
  if (!Probe.ContextCreated || !Probe.PrivateDataPointersIntact ||
      !State->CommandBufferSize) {
    D3D12DDI_HDEVICE Device{};
    Device.pDrvPrivate = State;
    destroyWarpDeviceContext(Device);
    return E_UNEXPECTED;
  }

  Probe.CommandBufferAddress = State->CommandBufferAddress;
  Probe.CommandBufferSize = State->CommandBufferSize;
  if (!State->CommandBufferAddress) {
    D3D12DDI_HDEVICE Device{};
    Device.pDrvPrivate = State;
    destroyWarpDeviceContext(Device);
    return E_UNEXPECTED;
  }
  State->UserModeContext.CommandBuffer = reinterpret_cast<void *>(
      static_cast<ULONG_PTR>(State->CommandBufferAddress));
  State->UserModeContext.CommandBufferSize =
      static_cast<UINT32>(State->CommandBufferSize);
  WarpInitContextVirtual2 InitContext{};
  InitContext.EscapeId = WarpEscapeInitContextVirtual2;
  InitContext.UMContext = &State->UserModeContext;
  InitContext.CommandBuffer = State->UserModeContext.CommandBuffer;
  InitContext.CommandBufferSize = State->UserModeContext.CommandBufferSize;
  D3DDDICB_ESCAPE InitContextEscape{};
  InitContextEscape.hDevice = State->RTDevice;
  InitContextEscape.hContext = State->Context;
  InitContextEscape.pPrivateDriverData = &InitContext;
  InitContextEscape.PrivateDriverDataSize = sizeof(InitContext);
  Probe.InitContextEscapeAttempted = true;
  bool InitContextEscapeReturned = false;
  __try {
    Probe.InitContextEscapeResult =
        State->KTCallbacks->pfnEscapeCb(RTAdapter, &InitContextEscape);
    InitContextEscapeReturned = true;
  } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    Probe.ExceptionCode = ExceptionCode;
  }
  Probe.InitContextEscapeReturned = InitContextEscapeReturned;
  Probe.KernelContextHandle = InitContext.KernelContextHandle;
  Probe.DmaBufferAddress = InitContext.DmaBufferAddress;
  Probe.DmaBufferSize = InitContext.DmaBufferSize;
  State->UserModeContext.KernelContextHandle = InitContext.KernelContextHandle;
  if (!InitContextEscapeReturned) {
    D3D12DDI_HDEVICE Device{};
    Device.pDrvPrivate = State;
    destroyWarpDeviceContext(Device);
    return E_FAIL;
  }
  if (FAILED(Probe.InitContextEscapeResult) || !Probe.DmaBufferSize) {
    D3D12DDI_HDEVICE Device{};
    Device.pDrvPrivate = State;
    destroyWarpDeviceContext(Device);
    return FAILED(Probe.InitContextEscapeResult) ? Probe.InitContextEscapeResult
                                                 : E_UNEXPECTED;
  }
  HRESULT Result = allocateWarpDmaBuffer(State);
  if (FAILED(Result)) {
    D3D12DDI_HDEVICE Device{};
    Device.pDrvPrivate = State;
    destroyWarpDeviceContext(Device);
    return Result;
  }
  Result = createWarpDmaFence(State);
  if (FAILED(Result)) {
    D3D12DDI_HDEVICE Device{};
    Device.pDrvPrivate = State;
    destroyWarpDeviceContext(Device);
    return Result;
  }
  Result = initializeWarpKmdDevice(State, RTAdapter);
  if (FAILED(Result)) {
    D3D12DDI_HDEVICE Device{};
    Device.pDrvPrivate = State;
    destroyWarpDeviceContext(Device);
    return Result;
  }
  return S_OK;
}

SIZE_T warpDevicePrivateSize() {
  constexpr SIZE_T Alignment = MEMORY_ALLOCATION_ALIGNMENT;
  return (sizeof(WarpDeviceState) + Alignment - 1) & ~(Alignment - 1);
}

void destroyWarpDeviceContext(D3D12DDI_HDEVICE Device) {
  auto &Probe = GWarpDeviceContextProbe;
  auto *State = static_cast<WarpDeviceState *>(Device.pDrvPrivate);
  if (!State || !State->KTCallbacks)
    return;

  DWORD ExceptionCode = 0;
  if (State->DmaFence &&
      State->KTCallbacks->pfnDestroySynchronizationObjectCb) {
    D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT DestroyFence{};
    DestroyFence.hSyncObject = State->DmaFence;
    Probe.DmaFenceDestroyAttempted = true;
    __try {
      Probe.DmaFenceDestroyResult =
          State->KTCallbacks->pfnDestroySynchronizationObjectCb(State->RTDevice,
                                                                &DestroyFence);
      Probe.DmaFenceDestroyReturned = true;
    } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
      Probe.ExceptionCode = ExceptionCode;
    }
    State->DmaFence = 0;
    State->DmaFenceValue = nullptr;
  }
  if (State->DmaLocked && State->KTCallbacks->pfnUnlockCb) {
    D3DDDICB_UNLOCK Unlock{};
    Unlock.NumAllocations = 1;
    Unlock.phAllocations = &State->DmaAllocation;
    Probe.DmaUnlockAttempted = true;
    __try {
      Probe.DmaUnlockResult =
          State->KTCallbacks->pfnUnlockCb(State->RTDevice, &Unlock);
      Probe.DmaUnlockReturned = true;
    } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
      Probe.ExceptionCode = ExceptionCode;
    }
    State->DmaLocked = false;
  }
  if (State->DmaAllocation && State->UMCallbacks &&
      State->UMCallbacks->pfnDeallocateCb) {
    D3D12DDICB_DEALLOCATE_0022 Deallocate{};
    Deallocate.NumAllocations = 1;
    Deallocate.HandleList = &State->DmaAllocation;
    Deallocate.Flags = D3D12DDI_DEALLOCATE_FLAGS_0022_ASSUME_NOT_IN_USE |
                       D3D12DDI_DEALLOCATE_FLAGS_0022_SYNCHRONOUS_DESTROY;
    D3D12DDI_HRTDEVICE RTDevice{};
    RTDevice.handle = State->RTDevice;
    Probe.DmaDeallocateAttempted = true;
    __try {
      Probe.DmaDeallocateResult =
          State->UMCallbacks->pfnDeallocateCb(RTDevice, &Deallocate);
      Probe.DmaDeallocateReturned = true;
    } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
      Probe.ExceptionCode = ExceptionCode;
    }
    State->DmaAllocation = 0;
    State->DmaCpuAddress = nullptr;
  }
  if (State->Context && State->KTCallbacks->pfnDestroyContextCb) {
    D3DDDICB_DESTROYCONTEXT DestroyContext{};
    DestroyContext.hContext = State->Context;
    Probe.DestroyAttempted = true;
    __try {
      Probe.DestroyResult = State->KTCallbacks->pfnDestroyContextCb(
          State->RTDevice, &DestroyContext);
      Probe.DestroyReturned = true;
    } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
      Probe.ExceptionCode = ExceptionCode;
    }
    State->Context = nullptr;
  }
  if (State->PagingQueue && State->KTCallbacks->pfnDestroyPagingQueueCb) {
    D3DDDI_DESTROYPAGINGQUEUE DestroyQueue{};
    DestroyQueue.hPagingQueue = State->PagingQueue;
    Probe.PagingQueueDestroyAttempted = true;
    __try {
      Probe.PagingQueueDestroyResult =
          State->KTCallbacks->pfnDestroyPagingQueueCb(State->RTDevice,
                                                      &DestroyQueue);
      Probe.PagingQueueDestroyReturned = true;
    } __except (ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
      Probe.ExceptionCode = ExceptionCode;
    }
    State->PagingQueue = 0;
  }
}
#endif

#ifdef FEME_BOOTSTRAP_PROBE_MONITORED_FENCE
HRESULT exerciseMonitoredFence(const D3D12DDIARG_CREATEDEVICE_0003 *Args,
                               D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 &Create,
                               HANDLE Event) {
  auto &Probe = GMonitoredFenceProbe;
  const auto *Fence = static_cast<volatile const UINT64 *>(
      Create.Info.MonitoredFence.FenceValueCPUVirtualAddress);
  Probe.InitialValue = *Fence;
  if (Probe.InitialValue != 0)
    return E_FAIL;

  UINT64 Target = feme::probe::MonitoredFenceTarget;
  D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait{};
  Wait.ObjectCount = 1;
  Wait.ObjectHandleArray = &Create.hSyncObject;
  Wait.FenceValueArray = &Target;
  Wait.hAsyncEvent = Event;
  Probe.WaitAttempted = true;
  Probe.WaitResult =
      Args->pKTCallbacks->pfnWaitForSynchronizationObjectFromCpuCb(
          Args->hRTDevice.handle, &Wait);
  Probe.WaitReturned = true;
  if (Probe.WaitResult != S_OK)
    return FAILED(Probe.WaitResult) ? Probe.WaitResult : E_FAIL;
  DWORD Before = WaitForSingleObject(Event, 0);
  Probe.EventCompleted = Before == WAIT_OBJECT_0;
  Probe.WaitWasPending = Before == WAIT_TIMEOUT;
  if (!Probe.WaitWasPending) {
    if (Before == WAIT_FAILED)
      Probe.Win32Error = GetLastError();
    return E_FAIL;
  }

  D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMCPU Signal{};
  Signal.ObjectCount = 1;
  Signal.ObjectHandleArray = &Create.hSyncObject;
  Signal.FenceValueArray = &Target;
  Probe.SignalAttempted = true;
  Probe.SignalResult =
      Args->pKTCallbacks->pfnSignalSynchronizationObjectFromCpuCb(
          Args->hRTDevice.handle, &Signal);
  Probe.SignalReturned = true;
  if (Probe.SignalResult != S_OK)
    return FAILED(Probe.SignalResult) ? Probe.SignalResult : E_FAIL;
  DWORD After = WaitForSingleObject(Event, 10000);
  Probe.EventCompleted = After == WAIT_OBJECT_0;
  if (!Probe.EventCompleted) {
    Probe.Win32Error = After == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
    return HRESULT_FROM_WIN32(Probe.Win32Error);
  }
  Probe.CompletedValue = *Fence;
  return Probe.CompletedValue == Target ? S_OK : E_FAIL;
}

HRESULT probeMonitoredFence(const D3D12DDIARG_CREATEDEVICE_0003 *Args) {
  auto &Probe = GMonitoredFenceProbe;
  Probe.Enabled = true;
  if (!Args || !Args->hRTDevice.handle)
    return E_INVALIDARG;
  if (!Args->pKTCallbacks ||
      !Args->pKTCallbacks->pfnCreateSynchronizationObject2Cb ||
      !Args->pKTCallbacks->pfnDestroySynchronizationObjectCb ||
      !Args->pKTCallbacks->pfnWaitForSynchronizationObjectFromCpuCb ||
      !Args->pKTCallbacks->pfnSignalSynchronizationObjectFromCpuCb)
    return E_NOINTERFACE;

  D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 Create{};
  Create.Info.Type = D3DDDI_MONITORED_FENCE;
  Create.Info.MonitoredFence.EngineAffinity = 1;
  Probe.CreateAttempted = true;
  DWORD Code = 0;
  __try {
    Probe.CreateResult = Args->pKTCallbacks->pfnCreateSynchronizationObject2Cb(
        Args->hRTDevice.handle, &Create);
    Probe.CreateReturned = true;
  } __except (Code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    Probe.ExceptionCode = Code;
    return E_FAIL;
  }
  if (Probe.CreateResult != S_OK)
    return FAILED(Probe.CreateResult) ? Probe.CreateResult : E_FAIL;

  Probe.HasSyncObject = Create.hSyncObject != 0;
  auto Address = reinterpret_cast<std::uintptr_t>(
      Create.Info.MonitoredFence.FenceValueCPUVirtualAddress);
  Probe.HasFenceMapping = Address && Address % alignof(UINT64) == 0;
  if (!Probe.HasSyncObject)
    return E_FAIL;
  HRESULT Result = E_FAIL;
  if (Probe.HasFenceMapping) {
    HANDLE Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!Event) {
      Probe.Win32Error = GetLastError();
      Result = HRESULT_FROM_WIN32(Probe.Win32Error);
    } else {
      __try {
        Result = exerciseMonitoredFence(Args, Create, Event);
      } __except (Code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        Probe.ExceptionCode = Code;
        Result = E_FAIL;
      }
      if (Probe.WaitAttempted && !Probe.EventCompleted) {
        // Keep a potentially registered async event alive until process exit.
        Probe.EventRetained = true;
      } else if (!CloseHandle(Event)) {
        Probe.Win32Error = GetLastError();
        Result = HRESULT_FROM_WIN32(Probe.Win32Error);
      }
    }
  }

  D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT Destroy{};
  Destroy.hSyncObject = Create.hSyncObject;
  Probe.DestroyAttempted = true;
  __try {
    Probe.DestroyResult = Args->pKTCallbacks->pfnDestroySynchronizationObjectCb(
        Args->hRTDevice.handle, &Destroy);
    Probe.DestroyReturned = true;
  } __except (Code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    Probe.ExceptionCode = Code;
    return E_FAIL;
  }
  if (Probe.DestroyResult != S_OK)
    return FAILED(Probe.DestroyResult) ? Probe.DestroyResult : E_FAIL;
  return Result;
}
#endif

#ifdef FEME_BOOTSTRAP_PROBE_GPU_VA
HRESULT probeGpuVirtualAddress(const D3D12DDIARG_CREATEDEVICE_0003 *Args) {
  using namespace feme::probe;
  auto &Probe = GGpuVirtualAddressProbe;
  Probe.Enabled = true;
  if (!Args || !Args->hRTDevice.handle)
    return E_INVALIDARG;
  if (!Args->pKTCallbacks ||
      !Args->pKTCallbacks->pfnReserveGpuVirtualAddressCb ||
      !Args->pKTCallbacks->pfnFreeGpuVirtualAddressCb)
    return E_NOINTERFACE;

  // A zero hAdapter lets the runtime supply its own kernel adapter handle.
  // This reserves address space only: no allocation, mapping or CPU access.
  D3DDDI_RESERVEGPUVIRTUALADDRESS Reserve{};
  Reserve.Size = GpuVaReservationBytes;
  Probe.ReserveAttempted = true;
  DWORD Code = 0;
  __try {
    Probe.ReserveResult = Args->pKTCallbacks->pfnReserveGpuVirtualAddressCb(
        Args->hRTDevice.handle, &Reserve);
    Probe.ReserveReturned = true;
  } __except (Code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    Probe.ExceptionCode = Code;
    return E_FAIL;
  }
  Probe.Address = Reserve.VirtualAddress;
  if (Probe.ReserveResult != S_OK)
    return FAILED(Probe.ReserveResult) ? Probe.ReserveResult : E_FAIL;

  Probe.HasValidRange =
      Probe.Address && Probe.Address % GpuVaReservationBytes == 0 &&
      Probe.Address <=
          std::numeric_limits<std::uint64_t>::max() - GpuVaReservationBytes;
  if (!Probe.Address)
    return E_FAIL;

  // Release every nonzero successful reservation, even a malformed output.
  D3DDDICB_FREEGPUVIRTUALADDRESS Free{};
  Free.BaseAddress = Probe.Address;
  Free.Size = GpuVaReservationBytes;
  Probe.FreeAttempted = true;
  __try {
    Probe.FreeResult = Args->pKTCallbacks->pfnFreeGpuVirtualAddressCb(
        Args->hRTDevice.handle, &Free);
    Probe.FreeReturned = true;
  } __except (Code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    Probe.ExceptionCode = Code;
    return E_FAIL;
  }
  if (Probe.FreeResult != S_OK)
    return FAILED(Probe.FreeResult) ? Probe.FreeResult : E_FAIL;
  return Probe.HasValidRange ? S_OK : E_FAIL;
}
#endif

#ifdef FEME_BOOTSTRAP_PROBE_PAGING_QUEUE
HRESULT probePagingQueue(const D3D12DDIARG_CREATEDEVICE_0003 *Args) {
  GPagingQueueProbe.Enabled = true;
  if (!Args || !Args->hRTDevice.handle)
    return E_INVALIDARG;
  if (!Args->pKTCallbacks || !Args->pKTCallbacks->pfnCreatePagingQueueCb ||
      !Args->pKTCallbacks->pfnDestroyPagingQueueCb)
    return E_NOINTERFACE;

  D3DDDICB_CREATEPAGINGQUEUE Queue{};
  Queue.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
  Queue.PhysicalAdapterIndex = 0;
  GPagingQueueProbe.CreateAttempted = true;
  DWORD Code = 0;
  __try {
    GPagingQueueProbe.CreateResult = Args->pKTCallbacks->pfnCreatePagingQueueCb(
        Args->hRTDevice.handle, &Queue);
    GPagingQueueProbe.CreateReturned = true;
  } __except (Code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    GPagingQueueProbe.ExceptionCode = Code;
    return E_FAIL;
  }
  if (GPagingQueueProbe.CreateResult != S_OK)
    return FAILED(GPagingQueueProbe.CreateResult)
               ? GPagingQueueProbe.CreateResult
               : E_FAIL;

  GPagingQueueProbe.HasPagingQueue = Queue.hPagingQueue != 0;
  GPagingQueueProbe.HasSyncObject = Queue.hSyncObject != 0;
  GPagingQueueProbe.HasFenceMapping =
      Queue.FenceValueCPUVirtualAddress != nullptr;
  if (!Queue.hPagingQueue)
    return E_FAIL;

  // Destroy every successfully created queue, even if its other outputs were
  // incomplete. Destruction also releases its sync object and fence mapping.
  D3DDDI_DESTROYPAGINGQUEUE DestroyArgs{};
  DestroyArgs.hPagingQueue = Queue.hPagingQueue;
  GPagingQueueProbe.DestroyAttempted = true;
  __try {
    GPagingQueueProbe.DestroyResult =
        Args->pKTCallbacks->pfnDestroyPagingQueueCb(Args->hRTDevice.handle,
                                                    &DestroyArgs);
    GPagingQueueProbe.DestroyReturned = true;
  } __except (Code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    GPagingQueueProbe.ExceptionCode = Code;
    return E_FAIL;
  }
  if (GPagingQueueProbe.DestroyResult != S_OK)
    return FAILED(GPagingQueueProbe.DestroyResult)
               ? GPagingQueueProbe.DestroyResult
               : E_FAIL;
  return GPagingQueueProbe.HasSyncObject && GPagingQueueProbe.HasFenceMapping
             ? S_OK
             : E_FAIL;
}
#endif

#ifdef FEME_BOOTSTRAP_PROBE_CALLBACK
// W0(g) follow-up: evidence from actually invoking pfnSetErrorCb once. This
// struct has no non-POD members so the function that uses it below can mix
// SEH with no object-unwinding conflict.
struct CallbackProbe {
  LONG Attempted = 0;
  LONG Completed = 0;
  LONG ExceptionCaught = 0;
  DWORD ExceptionCode = 0;
};
CallbackProbe GCallbackProbe;

// No local C++ objects requiring unwinding live in this function, so
// __try/__except (SEH) can wrap the call and observe -- not merely assume --
// whether invoking a real user-mode-only thunk callback the stock runtime
// supplied is safe, rather than letting an unexpected fault silently tear
// down the whole harness process.
void probeSetErrorCb(D3D12DDI_HRTDEVICE RTDevice,
                     PFND3D12DDI_SETERROR_CB Callback) {
  InterlockedExchange(&GCallbackProbe.Attempted, 1);
  DWORD Code = 0;
  __try {
    // A truthful diagnostic report, not a fabricated success: this
    // experiment genuinely has not implemented a device. No allocation or
    // resource callback is attempted.
    Callback(RTDevice, E_NOTIMPL);
    InterlockedExchange(&GCallbackProbe.Completed, 1);
  } __except (Code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    InterlockedExchange(&GCallbackProbe.ExceptionCaught, 1);
    GCallbackProbe.ExceptionCode = Code;
  }
}
#endif

SIZE_T
APIENTRY calcPrivateDeviceSize(D3D12DDI_HADAPTER,
                               CONST D3D12DDIARG_CALCPRIVATEDEVICESIZE *) {
  InterlockedIncrement(&GCalls.CalcPrivateDeviceSize);
#ifdef FEME_BOOTSTRAP_PROBE_WARP_DEVICE_CONTEXT
  return warpDevicePrivateSize();
#else
  // A nonzero, deliberately recognizable size; nothing allocates against it.
  return 64;
#endif
}

HRESULT APIENTRY createDevice(D3D12DDI_HADAPTER,
                              CONST D3D12DDIARG_CREATEDEVICE_0003 *Args) {
  InterlockedIncrement(&GCalls.CreateDevice);
  if (Args && !InterlockedCompareExchange(&GCreateDeviceAudit.Captured, 1, 0)) {
    GCreateDeviceAudit.Interface = Args->Interface;
    GCreateDeviceAudit.Version = Args->Version;
    GCreateDeviceAudit.HasKTCallbacks = Args->pKTCallbacks != nullptr;
    GCreateDeviceAudit.HasUMCallbacks = Args->p12UMCallbacks != nullptr;
    // Pointer observations are separate from the opt-in invocation probes.
    if (Args->pKTCallbacks) {
      GCreateDeviceAudit.HasKTAllocateCb =
          Args->pKTCallbacks->pfnAllocateCb != nullptr;
      GCreateDeviceAudit.HasKTDeallocateCb =
          Args->pKTCallbacks->pfnDeallocateCb != nullptr;
      GContractAudit.HasKTMakeResidentCb =
          Args->pKTCallbacks->pfnMakeResidentCb != nullptr;
      GContractAudit.HasKTEvictCb = Args->pKTCallbacks->pfnEvictCb != nullptr;
      GContractAudit.HasKTCreatePagingQueueCb =
          Args->pKTCallbacks->pfnCreatePagingQueueCb != nullptr;
      GContractAudit.HasKTDestroyPagingQueueCb =
          Args->pKTCallbacks->pfnDestroyPagingQueueCb != nullptr;
      GContractAudit.HasKTMapGpuVirtualAddressCb =
          Args->pKTCallbacks->pfnMapGpuVirtualAddressCb != nullptr;
      GContractAudit.HasKTDeallocate2Cb =
          Args->pKTCallbacks->pfnDeallocate2Cb != nullptr;
    }
    if (Args->p12UMCallbacks) {
      GCreateDeviceAudit.HasUMSetErrorCb =
          Args->p12UMCallbacks->pfnSetErrorCb != nullptr;
      GContractAudit.HasUMCreatePagingQueueCb =
          Args->p12UMCallbacks->pfnCreatePagingQueueCb != nullptr;
      GContractAudit.HasUMDestroyPagingQueueCb =
          Args->p12UMCallbacks->pfnDestroyPagingQueueCb != nullptr;
      GContractAudit.HasUMMakeResidentCb =
          Args->p12UMCallbacks->pfnMakeResidentCb != nullptr;
      GContractAudit.HasUMEvictCb = Args->p12UMCallbacks->pfnEvictCb != nullptr;
      // The public 0022 contract extends the 0003 callback prefix with
      // allocation wrappers; older negotiated versions cannot expose them.
      UINT64 Version =
          (static_cast<UINT64>(Args->Interface) << 32) | Args->Version;
      if (Version >= D3D12DDI_SUPPORTED_0022) {
        const auto *Callbacks =
            reinterpret_cast<const D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 *>(
                Args->p12UMCallbacks);
        GContractAudit.HasUMAllocateCb = Callbacks->pfnAllocateCb != nullptr;
        GContractAudit.HasUMDeallocateCb =
            Callbacks->pfnDeallocateCb != nullptr;
      }
    }
  }
#ifdef FEME_BOOTSTRAP_PROBE_CALLBACK
  if (Args && Args->p12UMCallbacks && Args->p12UMCallbacks->pfnSetErrorCb)
    probeSetErrorCb(Args->hRTDevice, Args->p12UMCallbacks->pfnSetErrorCb);
#endif
#ifdef FEME_BOOTSTRAP_PROBE_PAGING_QUEUE
  HRESULT PagingResult = probePagingQueue(Args);
  if (FAILED(PagingResult))
    return PagingResult;
#endif
#ifdef FEME_BOOTSTRAP_PROBE_GPU_VA
  HRESULT GpuVaResult = probeGpuVirtualAddress(Args);
  if (FAILED(GpuVaResult))
    return GpuVaResult;
#endif
#ifdef FEME_BOOTSTRAP_PROBE_MONITORED_FENCE
  HRESULT FenceResult = probeMonitoredFence(Args);
  if (FAILED(FenceResult))
    return FenceResult;
#endif
#ifdef FEME_BOOTSTRAP_PROBE_WARP_CONTEXT
  HRESULT WarpContextResult = probeWarpContext(Args);
  if (FAILED(WarpContextResult))
    return WarpContextResult;
#endif
#ifdef FEME_BOOTSTRAP_PROBE_WARP_DEVICE_CONTEXT
  HRESULT DeviceContextResult =
      createWarpDeviceContext(Args, GWarpRuntimeAdapter);
  if (FAILED(DeviceContextResult))
    return DeviceContextResult;
  return S_OK;
#else
  // A real device table is out of scope for this experiment; fail loudly and
  // distinctly so a caller cannot mistake this for the generic rejection
  // windows-adapter-probe's RejectionStub already measures.
  return E_NOTIMPL;
#endif
}

HRESULT APIENTRY closeAdapter(D3D12DDI_HADAPTER) {
  InterlockedIncrement(&GCalls.CloseAdapter);
#ifdef FEME_BOOTSTRAP_PROBE_WARP_DEVICE_CONTEXT
  GWarpRuntimeAdapter = nullptr;
#endif
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
  auto RecordResult = [Index, Args](HRESULT Result) {
    if (Index >= 0 && static_cast<UINT32>(Index) < MaxCapsTypeLog) {
      GCapsDataSizeLog[Index] = Args ? Args->DataSize : 0;
      GCapsResultLog[Index] = Result;
    }
    return Result;
  };

  if (!Args)
    return RecordResult(E_NOTIMPL);

  // Keep the observed pre-device capability queries to baseline values so the
  // probe can determine whether the runtime proceeds to the next DDI stage.
  if (Args->Type == D3D12DDICAPS_TYPE_D3D12_OPTIONS && Args->pData &&
      Args->DataSize >= sizeof(D3D12DDI_D3D12_OPTIONS_DATA_0003)) {
    std::memset(Args->pData, 0, Args->DataSize);
    auto *Options =
        static_cast<D3D12DDI_D3D12_OPTIONS_DATA_0003 *>(Args->pData);
    Options->ResourceBindingTier = D3D12DDI_RESOURCE_BINDING_TIER_1;
    Options->ConservativeRasterizationTier =
        D3D12DDI_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED;
    Options->TiledResourcesTier = D3D12DDI_TILED_RESOURCES_TIER_NOT_SUPPORTED;
    Options->CrossNodeSharingTier =
        D3D12DDI_CROSS_NODE_SHARING_TIER_NOT_SUPPORTED;
    Options->ResourceHeapTier = D3D12DDI_RESOURCE_HEAP_TIER_1;
    return RecordResult(S_OK);
  }
  if (Args->Type == D3D12DDICAPS_TYPE_SHADER) {
    if (!Args->pData)
      return RecordResult(E_INVALIDARG);
    switch (Args->DataSize) {
    case sizeof(D3D12DDI_SHADER_CAPS_0003):
    case sizeof(D3D12DDI_SHADER_CAPS_0012):
    case sizeof(D3D12DDI_SHADER_CAPS_0015):
    case sizeof(D3D12DDI_SHADER_CAPS_0042):
    case sizeof(D3D12DDI_SHADER_CAPS_0082):
    case sizeof(D3D12DDI_SHADER_CAPS_0084):
      break;
    default:
      return RecordResult(E_INVALIDARG);
    }
    std::memset(Args->pData, 0, Args->DataSize);
    if (Args->DataSize >= sizeof(D3D12DDI_SHADER_CAPS_0012)) {
      // The runtime validates lane limits even with WaveOps disabled.
      auto *Shader = static_cast<D3D12DDI_SHADER_CAPS_0012 *>(Args->pData);
      Shader->WaveLaneCountMin = 4;
      Shader->WaveLaneCountMax = 4;
      Shader->TotalLaneCount = 4;
    }
    return RecordResult(S_OK);
  }
  if (Args->Type == D3D12DDICAPS_TYPE_0011_SHADER_MODELS) {
    if (!Args->pData ||
        Args->DataSize != sizeof(D3D12DDI_D3D12_SHADER_MODELS_DATA_0011))
      return RecordResult(E_INVALIDARG);
    auto *Models =
        static_cast<D3D12DDI_D3D12_SHADER_MODELS_DATA_0011 *>(Args->pData);
    if (!Models->pNumShaderModelsSupported)
      return RecordResult(E_INVALIDARG);
    UINT Capacity =
        Models->pShaderModelsSupported ? *Models->pNumShaderModelsSupported : 0;
    *Models->pNumShaderModelsSupported = 1;
    if (Models->pShaderModelsSupported) {
      if (!Capacity)
        return RecordResult(HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
      // This is the audit's input profile, not a shader-execution claim.
      Models->pShaderModelsSupported[0] =
          D3D12DDI_SHADER_MODEL_5_1_RELEASE_0011;
    }
    return RecordResult(S_OK);
  }
  if (Args->Type == D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1 && Args->pData &&
      Args->DataSize >= sizeof(D3D12DDI_3DPIPELINESUPPORT1_DATA_0081)) {
    auto *Data =
        static_cast<D3D12DDI_3DPIPELINESUPPORT1_DATA_0081 *>(Args->pData);
    Data->MaximumDriverSupportedFeatureLevel =
        Data->HighestRuntimeSupportedFeatureLevel <
                D3D12DDI_3DPIPELINELEVEL_11_0
            ? Data->HighestRuntimeSupportedFeatureLevel
            : D3D12DDI_3DPIPELINELEVEL_11_0;
    return RecordResult(S_OK);
  }
  if (Args->Type == D3D12DDICAPS_TYPE_3DPIPELINESUPPORT && Args->pData &&
      Args->DataSize >= sizeof(D3D12DDI_3DPIPELINELEVEL)) {
    // Keep negotiation at the requested 11_0 floor; this probe does not claim
    // support for higher feature levels or a complete device implementation.
    *static_cast<D3D12DDI_3DPIPELINELEVEL *>(Args->pData) =
        D3D12DDI_3DPIPELINELEVEL_11_0;
    return RecordResult(S_OK);
  }

  // No capability data is fabricated for any other cap; this remains an
  // audit, not a device.
  return RecordResult(E_NOTIMPL);
}

HRESULT APIENTRY getOptionalDDITables(D3D12DDI_HADAPTER, UINT32 *EntryCount,
                                      D3D12DDI_TABLE_REQUEST *) {
  InterlockedIncrement(&GCalls.GetOptionalDDITables);
  *EntryCount = 0;
  return S_OK;
}

HRESULT APIENTRY fillDDITable(D3D12DDI_HADAPTER, D3D12DDI_TABLE_TYPE TableType,
                              VOID *Table, SIZE_T TableSize, UINT Interface,
                              D3D12DDI_HRTTABLE) {
  InterlockedIncrement(&GCalls.FillDDITable);
#ifdef FEME_BOOTSTRAP_PROBE_WARP_DEVICE_CONTEXT
  (void)Table;
  (void)Interface;
  GWarpDeviceContextProbe.FillTableAttempted = true;
  GWarpDeviceContextProbe.FillTableType = TableType;
  GWarpDeviceContextProbe.FillTableSize = TableSize;
  GWarpDeviceContextProbe.FillTableResult = E_NOTIMPL;
#else
  (void)TableType;
  (void)Table;
  (void)TableSize;
  (void)Interface;
#endif
  return E_NOTIMPL;
}

VOID APIENTRY destroyDevice(D3D12DDI_HDEVICE Device) {
  InterlockedIncrement(&GCalls.DestroyDevice);
#ifdef FEME_BOOTSTRAP_PROBE_WARP_DEVICE_CONTEXT
  destroyWarpDeviceContext(Device);
#else
  (void)Device;
#endif
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
#ifdef FEME_BOOTSTRAP_PROBE_WARP_DEVICE_CONTEXT
  if (!Args->hRTAdapter.handle)
    return E_INVALIDARG;
  GWarpRuntimeAdapter = Args->hRTAdapter.handle;
#endif
#ifdef FEME_BOOTSTRAP_PROBE_WARP_PRIVATE_INFO
  if (!Args->hRTAdapter.handle ||
      !Args->pAdapterCallbacks->pfnQueryAdapterInfoCb)
    return E_NOINTERFACE;
  WarpPrivateAdapterInfo PrivateInfo{};
  D3DDDICB_QUERYADAPTERINFO QueryInfo{};
  QueryInfo.pPrivateDriverData = &PrivateInfo;
  QueryInfo.PrivateDriverDataSize = sizeof(PrivateInfo);
  GWarpPrivateAdapterInfoProbe.Attempted = true;
  GWarpPrivateAdapterInfoProbe.Result =
      Args->pAdapterCallbacks->pfnQueryAdapterInfoCb(Args->hRTAdapter.handle,
                                                     &QueryInfo);
  if (SUCCEEDED(GWarpPrivateAdapterInfoProbe.Result)) {
    GWarpPrivateAdapterInfoProbe.Version = PrivateInfo.Version;
    GWarpPrivateAdapterInfoProbe.WddmVersion = PrivateInfo.WddmVersion;
    GWarpPrivateAdapterInfoProbe.DriverCaps = PrivateInfo.DriverCaps;
  } else {
    return GWarpPrivateAdapterInfoProbe.Result;
  }
#endif
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

extern "C" __declspec(dllexport) void APIENTRY FeMeBootstrapGetCalls(
    LONG *OpenAdapter12Count, LONG *CalcPrivateDeviceSizeCount,
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
FeMeBootstrapGetContractAudit(feme::probe::BootstrapContractAudit *Audit) {
  *Audit = GContractAudit;
#ifdef FEME_BOOTSTRAP_PINNED_VERSION
  Audit->PinnedVersion = FEME_BOOTSTRAP_PINNED_VERSION;
#endif
}

extern "C" __declspec(dllexport) void APIENTRY
FeMeBootstrapGetPagingQueueProbe(feme::probe::PagingQueueProbe *Probe) {
  *Probe = GPagingQueueProbe;
}

extern "C" __declspec(dllexport) void APIENTRY
FeMeBootstrapGetGpuVirtualAddressProbe(
    feme::probe::GpuVirtualAddressProbe *Probe) {
  *Probe = GGpuVirtualAddressProbe;
}

extern "C" __declspec(dllexport) void APIENTRY
FeMeBootstrapGetMonitoredFenceProbe(feme::probe::MonitoredFenceProbe *Probe) {
  *Probe = GMonitoredFenceProbe;
}

extern "C" __declspec(dllexport) void APIENTRY
FeMeBootstrapGetWarpPrivateAdapterInfoProbe(
    feme::probe::WarpPrivateAdapterInfoProbe *Probe) {
  *Probe = GWarpPrivateAdapterInfoProbe;
}

extern "C" __declspec(dllexport) void APIENTRY
FeMeBootstrapGetWarpContextProbe(feme::probe::WarpContextProbe *Probe) {
  *Probe = GWarpContextProbe;
}

extern "C" __declspec(dllexport) void APIENTRY
FeMeBootstrapGetWarpDeviceContextProbe(
    feme::probe::WarpDeviceContextProbe *Probe) {
  *Probe = GWarpDeviceContextProbe;
}

extern "C" __declspec(dllexport) void APIENTRY FeMeBootstrapGetAudit(
    LONG *CapsTypeCount, LONG CapsTypes[/* FeMeBootstrapGetAuditCapacity() */],
    UINT32 CapsDataSizes[/* FeMeBootstrapGetAuditCapacity() */],
    LONG CapsResults[/* FeMeBootstrapGetAuditCapacity() */],
    UINT *CreateDeviceInterface, UINT *CreateDeviceVersion,
    LONG *CreateDeviceHasKTCallbacks, LONG *CreateDeviceHasUMCallbacks,
    LONG *CreateDeviceHasKTAllocateCb, LONG *CreateDeviceHasKTDeallocateCb,
    LONG *CreateDeviceHasUMSetErrorCb) {
  LONG Count = InterlockedCompareExchange(&GCapsTypeLogCount, 0, 0);
  *CapsTypeCount = Count;
  UINT32 Fill = Count <= 0 ? 0
                : static_cast<UINT32>(Count) < MaxCapsTypeLog
                    ? static_cast<UINT32>(Count)
                    : MaxCapsTypeLog;
  for (UINT32 I = 0; I < Fill; ++I)
    CapsTypes[I] = GCapsTypeLog[I];
  for (UINT32 I = 0; I < Fill; ++I) {
    CapsDataSizes[I] = GCapsDataSizeLog[I];
    CapsResults[I] = GCapsResultLog[I];
  }
  *CreateDeviceInterface = GCreateDeviceAudit.Interface;
  *CreateDeviceVersion = GCreateDeviceAudit.Version;
  *CreateDeviceHasKTCallbacks =
      InterlockedCompareExchange(&GCreateDeviceAudit.HasKTCallbacks, 0, 0);
  *CreateDeviceHasUMCallbacks =
      InterlockedCompareExchange(&GCreateDeviceAudit.HasUMCallbacks, 0, 0);
  *CreateDeviceHasKTAllocateCb =
      InterlockedCompareExchange(&GCreateDeviceAudit.HasKTAllocateCb, 0, 0);
  *CreateDeviceHasKTDeallocateCb =
      InterlockedCompareExchange(&GCreateDeviceAudit.HasKTDeallocateCb, 0, 0);
  *CreateDeviceHasUMSetErrorCb =
      InterlockedCompareExchange(&GCreateDeviceAudit.HasUMSetErrorCb, 0, 0);
}

// W0(g) follow-up: evidence from actually invoking pfnSetErrorCb once (only
// populated when built with FEME_BOOTSTRAP_PROBE_CALLBACK; otherwise this
// truthfully reports that no invocation was attempted).
extern "C" __declspec(dllexport) void APIENTRY
FeMeBootstrapGetCallbackProbe(LONG *Attempted, LONG *Completed,
                              LONG *ExceptionCaught, DWORD *ExceptionCode) {
#ifdef FEME_BOOTSTRAP_PROBE_CALLBACK
  *Attempted = InterlockedCompareExchange(&GCallbackProbe.Attempted, 0, 0);
  *Completed = InterlockedCompareExchange(&GCallbackProbe.Completed, 0, 0);
  *ExceptionCaught =
      InterlockedCompareExchange(&GCallbackProbe.ExceptionCaught, 0, 0);
  *ExceptionCode = GCallbackProbe.ExceptionCode;
#else
  *Attempted = 0;
  *Completed = 0;
  *ExceptionCaught = 0;
  *ExceptionCode = 0;
#endif
}
