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

#ifdef FEME_BOOTSTRAP_WARP_COMPILER
#include "WarpShaderCompilerABI.h"
#endif

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <new>
#include <type_traits>
#include <vector>

namespace {
std::atomic<const char *> LastUnsupported{nullptr};
std::atomic<UINT> PublishedTables[4]{};
std::atomic<UINT> ExecutableShaderQueries{0};
std::atomic<UINT> UnsupportedCalls{0};
std::atomic<UINT> CompiledShadersCreated{0};
std::atomic<UINT> CompiledShadersDestroyed{0};
std::atomic<UINT> PipelinesCreated{0};
std::atomic<UINT> PipelinesDestroyed{0};

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

template <typename Descriptor, typename Handle> struct ImmutableState {
  feme::probe::WarpDeviceState *Owner;
  Descriptor Desc;

  static HRESULT validate(const Descriptor *Args) {
    if (!Args)
      return E_INVALIDARG;
    if (Args->LibraryReference.hLibrary.pDrvPrivate ||
        Args->LibraryReference.PipelineIndex)
      return E_NOTIMPL;
    return S_OK;
  }

  static SIZE_T APIENTRY size(D3D12DDI_HDEVICE Device, const Descriptor *Args) {
    HRESULT Result = validate(Args);
    if (FAILED(Result)) {
      reportError(Device, Result);
      return 0;
    }
    return sizeof(ImmutableState);
  }

  static void APIENTRY create(D3D12DDI_HDEVICE Device, const Descriptor *Args,
                              Handle Object) {
    HRESULT Result = validate(Args);
    auto *Owner =
        static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
    if (SUCCEEDED(Result) && (!Owner || !Object.pDrvPrivate))
      Result = E_INVALIDARG;
    if (FAILED(Result)) {
      reportError(Device, Result);
      return;
    }
    new (Object.pDrvPrivate) ImmutableState{Owner, *Args};
    ++Owner->LiveImmutableStates;
  }

  static void APIENTRY destroy(D3D12DDI_HDEVICE Device, Handle Object) {
    auto *Owner =
        static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
    auto *State = static_cast<ImmutableState *>(Object.pDrvPrivate);
    if (!Owner || !State || State->Owner != Owner) {
      reportError(Device, E_INVALIDARG);
      return;
    }
    State->~ImmutableState();
    --Owner->LiveImmutableStates;
  }
};

using BlendState =
    ImmutableState<D3D12DDI_BLEND_DESC_0010, D3D12DDI_HBLENDSTATE>;
using DepthState = ImmutableState<D3D12DDI_DEPTH_STENCIL_DESC_0025,
                                  D3D12DDI_HDEPTHSTENCILSTATE>;
using RasterState =
    ImmutableState<D3D12DDI_RASTERIZER_DESC_0010, D3D12DDI_HRASTERIZERSTATE>;

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

struct ShaderObject {
  feme::probe::WarpDeviceState *Owner;
#ifdef FEME_BOOTSTRAP_WARP_COMPILER
  void *Compiled = nullptr;
  FeMeWarpCompiledShaderInfo Info{};
#endif
};

HRESULT validateShader(const D3D12DDIARG_CREATE_SHADER_0026 *Args) {
  if (!Args)
    return E_INVALIDARG;
#ifdef FEME_BOOTSTRAP_WARP_COMPILER
  if (Args->pShaderCode && !Args->LibraryReference.hLibrary.pDrvPrivate &&
      !Args->LibraryReference.PipelineIndex && !Args->Flags) {
    std::fprintf(stderr, "FeMe WARP raw shader: version=0x%08x tokens=%u\n",
                 Args->pShaderCode[0], Args->pShaderCode[1]);
    // The public tokenized-bytecode contract bounds the buffer by word 1.
    if (Args->pShaderCode[1] < 2 || Args->pShaderCode[1] > 4 * 1024 * 1024)
      return E_INVALIDARG;
    return S_OK;
  }
#endif
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
  HRESULT Result = validateShader(Args);
  if (Args && Args->pShaderCode)
    ++ExecutableShaderQueries;
  if (FAILED(Result)) {
    if (Args && Args->pShaderCode) {
      LastUnsupported.store("pfnCalcPrivateShaderSize");
    }
    reportError(Device, Result);
    return 0;
  }
  return sizeof(ShaderObject);
}

template <UINT Stage>
void APIENTRY createShader(D3D12DDI_HDEVICE Device,
                           const D3D12DDIARG_CREATE_SHADER_0026 *Args,
                           D3D12DDI_HSHADER Shader) {
  HRESULT Result = validateShader(Args);
  if (FAILED(Result)) {
    reportError(Device, Result);
    return;
  }
  if (Args->pShaderCode && (Args->pShaderCode[0] >> 16) != Stage) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (!Owner || !Shader.pDrvPrivate) {
    reportError(Device, E_INVALIDARG);
    return;
  }
#ifdef FEME_BOOTSTRAP_WARP_COMPILER
  if (Args->pShaderCode) {
    void *Compiled = nullptr;
    FeMeWarpCompiledShaderInfo Info;
    Result = FeMeWarpCompileRawDXBC(Args->pShaderCode, Args->pShaderCode[1],
                                    &Compiled, &Info);
    if (FAILED(Result)) {
      LastUnsupported.store("pfnCreateShader: FeMe compilation failed");
      reportError(Device, Result);
      return;
    }
    new (Shader.pDrvPrivate) ShaderObject{Owner, Compiled, Info};
    ++Owner->LiveCompiledShaders;
    ++CompiledShadersCreated;
    return;
  }
#endif
  new (Shader.pDrvPrivate) ShaderObject{Owner};
  ++Owner->LiveNullShaders;
}

void APIENTRY destroyShader(D3D12DDI_HDEVICE Device, D3D12DDI_HSHADER Shader) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  auto *State = static_cast<ShaderObject *>(Shader.pDrvPrivate);
  if (!Owner || !State || State->Owner != Owner) {
    reportError(Device, E_INVALIDARG);
    return;
  }
#ifdef FEME_BOOTSTRAP_WARP_COMPILER
  if (State->Compiled) {
    FeMeWarpDestroyCompiledShader(State->Compiled);
    --Owner->LiveCompiledShaders;
    ++CompiledShadersDestroyed;
  } else
#endif
    --Owner->LiveNullShaders;
  State->~ShaderObject();
}

#ifdef FEME_BOOTSTRAP_WARP_COMPILER
struct CommandPool {
  feme::probe::WarpDeviceState *Owner;
  std::vector<UINT8> Bytes;
  UINT64 Generation = 0;
  UINT Recorders = 0;
};

SIZE_T APIENTRY commandPoolSize(
    D3D12DDI_HDEVICE Device, const D3D12DDIARG_CREATE_COMMAND_POOL_0040 *Args) {
  if (!Args || Args->PoolFlags != D3D12DDI_COMMAND_POOL_FLAG_NONE) {
    reportError(Device, E_INVALIDARG);
    return 0;
  }
  return sizeof(CommandPool);
}

HRESULT APIENTRY createCommandPool(
    D3D12DDI_HDEVICE Device, const D3D12DDIARG_CREATE_COMMAND_POOL_0040 *Args,
    D3D12DDI_HCOMMANDPOOL_0040 Pool) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (!Owner || !Pool.pDrvPrivate || !Args ||
      Args->PoolFlags != D3D12DDI_COMMAND_POOL_FLAG_NONE)
    return E_INVALIDARG;
  new (Pool.pDrvPrivate) CommandPool{Owner, {}};
  ++Owner->LiveCommandPools;
  return S_OK;
}

void APIENTRY destroyCommandPool(D3D12DDI_HDEVICE Device,
                                 D3D12DDI_HCOMMANDPOOL_0040 Pool) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  auto *State = static_cast<CommandPool *>(Pool.pDrvPrivate);
  if (!Owner || !State || State->Owner != Owner || State->Recorders) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  State->~CommandPool();
  --Owner->LiveCommandPools;
}

void APIENTRY resetCommandPool(D3D12DDI_HDEVICE Device,
                               D3D12DDI_HCOMMANDPOOL_0040 Pool) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  auto *State = static_cast<CommandPool *>(Pool.pDrvPrivate);
  if (!Owner || !State || State->Owner != Owner) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  State->Bytes.clear();
  ++State->Generation;
}

struct DescriptorSlot {
  D3D12DDI_DESCRIPTOR_HEAP_TYPE Type;
  D3D12DDI_HRESOURCE Resource;
  UINT64 ByteOffset;
  UINT64 ByteSize;
};

UINT APIENTRY descriptorSize(D3D12DDI_HDEVICE Device,
                             D3D12DDI_DESCRIPTOR_HEAP_TYPE Type) {
  if (Type < D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
      Type >= D3D12DDI_DESCRIPTOR_HEAP_TYPE_NUM_TYPES) {
    reportError(Device, E_INVALIDARG);
    return 0;
  }
  return sizeof(DescriptorSlot);
}

struct PipelineObject {
  feme::probe::WarpDeviceState *Owner;
  void *Compiled;
  bool Compute;
};

HRESULT pipelineDesc(D3D12DDI_HDEVICE Device,
                     const D3D12DDIARG_CREATE_PIPELINE_STATE_0033 *Args,
                     FeMeWarpVertexPipelineDesc &Desc, ShaderObject *&Vertex,
                     bool &Compute) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (!Owner || !Args || Args->NodeMask != 1)
    return E_INVALIDARG;
  std::fprintf(stderr,
               "FeMe WARP pipeline: targets=%u depth=%u samples=%u quality=%u "
               "topology=%u views=%u\n",
               Args->NumRenderTargets, Args->DSVFormat, Args->SampleDesc.Count,
               Args->SampleDesc.Quality, Args->PrimitiveTopologyType,
               Args->ViewInstancingDesc.ViewInstanceCount);
  if (Args->LibraryReference.hLibrary.pDrvPrivate ||
      Args->LibraryReference.PipelineIndex || Args->NumRenderTargets ||
      Args->DSVFormat != DXGI_FORMAT_UNKNOWN || Args->SampleDesc.Count != 1 ||
      Args->SampleDesc.Quality || Args->ViewInstancingDesc.ViewInstanceCount ||
      Args->hElementLayout.pDrvPrivate)
    return E_NOTIMPL;
  auto *Root =
      static_cast<EmptyRootSignature *>(Args->hRootSignature.pDrvPrivate);
  if (!Root || Root->Owner != Owner)
    return E_INVALIDARG;
  auto *ComputeShader =
      static_cast<ShaderObject *>(Args->hComputeShader.pDrvPrivate);
  if (ComputeShader && ComputeShader->Compiled) {
    if (ComputeShader->Owner != Owner || ComputeShader->Info.Stage != 5)
      return E_INVALIDARG;
    for (auto Shader :
         {Args->hVertexShader, Args->hPixelShader, Args->hDomainShader,
          Args->hHullShader, Args->hGeometryShader}) {
      auto *State = static_cast<ShaderObject *>(Shader.pDrvPrivate);
      if (State && State->Owner != Owner)
        return E_INVALIDARG;
      if (State && State->Compiled)
        return E_INVALIDARG;
    }
    Vertex = ComputeShader;
    Compute = true;
    return S_OK;
  }
  for (auto Shader :
       {Args->hComputeShader, Args->hPixelShader, Args->hDomainShader,
        Args->hHullShader, Args->hGeometryShader}) {
    auto *State = static_cast<ShaderObject *>(Shader.pDrvPrivate);
    if (State && State->Owner != Owner)
      return E_INVALIDARG;
    if (State && State->Compiled)
      return E_NOTIMPL;
  }
  Vertex = static_cast<ShaderObject *>(Args->hVertexShader.pDrvPrivate);
  auto *Raster = static_cast<RasterState *>(Args->hRasterizerState.pDrvPrivate);
  auto *Depth = static_cast<DepthState *>(Args->hDepthStencilState.pDrvPrivate);
  auto *Blend = static_cast<BlendState *>(Args->hBlendState.pDrvPrivate);
  if (!Vertex || Vertex->Owner != Owner || !Vertex->Compiled ||
      Vertex->Info.Stage != 0 || !Root || Root->Owner != Owner || !Raster ||
      Raster->Owner != Owner || !Depth || Depth->Owner != Owner || !Blend ||
      Blend->Owner != Owner)
    return E_INVALIDARG;
  if (Depth->Desc.DepthEnable || Depth->Desc.StencilEnable ||
      Depth->Desc.DepthBoundsTestEnable || Blend->Desc.AlphaToCoverageEnable ||
      Raster->Desc.DepthBias || Raster->Desc.DepthBiasClamp ||
      Raster->Desc.SlopeScaledDepthBias || Raster->Desc.ForcedSampleCount ||
      Raster->Desc.ConservativeRasterizationMode !=
          D3D12DDI_CONSERVATIVE_RASTERIZATION_MODE_OFF ||
      Raster->Desc.AntialiasedLineEnable)
    return E_NOTIMPL;
  switch (Args->PrimitiveTopologyType) {
  case D3D12DDI_PRIMITIVE_TOPOLOGY_TYPE_POINT:
    Desc.Topology = 0;
    break;
  case D3D12DDI_PRIMITIVE_TOPOLOGY_TYPE_LINE:
    Desc.Topology = 1;
    break;
  case D3D12DDI_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
    Desc.Topology = 2;
    break;
  default:
    return E_NOTIMPL;
  }
  if (Raster->Desc.CullMode < D3D12DDI_CULL_MODE_NONE ||
      Raster->Desc.CullMode > D3D12DDI_CULL_MODE_BACK)
    return E_INVALIDARG;
  switch (Raster->Desc.CullMode) {
  case D3D12DDI_CULL_MODE_NONE:
    Desc.Cull = 0;
    break;
  case D3D12DDI_CULL_MODE_FRONT:
    Desc.Cull = 1;
    break;
  case D3D12DDI_CULL_MODE_BACK:
    Desc.Cull = 2;
    break;
  }
  if (Raster->Desc.FillMode != D3D12DDI_FILL_MODE_WIREFRAME &&
      Raster->Desc.FillMode != D3D12DDI_FILL_MODE_SOLID)
    return E_INVALIDARG;
  Desc.Wireframe = Raster->Desc.FillMode == D3D12DDI_FILL_MODE_WIREFRAME;
  Desc.FrontCounterClockwise = Raster->Desc.FrontCounterClockwise != FALSE;
  Desc.DepthClamp = Raster->Desc.DepthClipEnable == FALSE;
  Desc.SampleMask = Args->SampleMask;
  return S_OK;
}

SIZE_T APIENTRY
pipelineSize(D3D12DDI_HDEVICE Device,
             const D3D12DDIARG_CREATE_PIPELINE_STATE_0033 *Args) {
  FeMeWarpVertexPipelineDesc Desc;
  ShaderObject *Vertex = nullptr;
  bool Compute = false;
  HRESULT Result = pipelineDesc(Device, Args, Desc, Vertex, Compute);
  if (FAILED(Result)) {
    LastUnsupported.store("pfnCalcPrivatePipelineStateSize");
    reportError(Device, Result);
    return 0;
  }
  return sizeof(PipelineObject);
}

HRESULT APIENTRY createPipeline(
    D3D12DDI_HDEVICE Device, const D3D12DDIARG_CREATE_PIPELINE_STATE_0033 *Args,
    D3D12DDI_HPIPELINESTATE Pipeline, D3D12DDI_HRTPIPELINESTATE) {
  if (!Pipeline.pDrvPrivate)
    return E_INVALIDARG;
  FeMeWarpVertexPipelineDesc Desc;
  ShaderObject *Vertex = nullptr;
  bool Compute = false;
  HRESULT Result = pipelineDesc(Device, Args, Desc, Vertex, Compute);
  if (FAILED(Result))
    return Result;
  void *Compiled = nullptr;
  Result = Compute ? FeMeWarpCreateComputePipeline(Vertex->Compiled, &Compiled)
                   : FeMeWarpCreateVertexPipeline(Vertex->Compiled, &Desc,
                                                  &Compiled);
  if (FAILED(Result))
    return Result;
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  new (Pipeline.pDrvPrivate) PipelineObject{Owner, Compiled, Compute};
  ++Owner->LivePipelines;
  ++PipelinesCreated;
  return S_OK;
}

void APIENTRY destroyPipeline(D3D12DDI_HDEVICE Device,
                              D3D12DDI_HPIPELINESTATE Pipeline) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  auto *State = static_cast<PipelineObject *>(Pipeline.pDrvPrivate);
  if (!Owner || !State || State->Owner != Owner) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  if (State->Compute)
    FeMeWarpDestroyComputePipeline(State->Compiled);
  else
    FeMeWarpDestroyVertexPipeline(State->Compiled);
  State->~PipelineObject();
  --Owner->LivePipelines;
  ++PipelinesDestroyed;
}
#endif

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
    Table.pfnCalcPrivateBlendStateSize = BlendState::size;
    Table.pfnCreateBlendState = BlendState::create;
    Table.pfnDestroyBlendState = BlendState::destroy;
    Table.pfnCalcPrivateDepthStencilStateSize = DepthState::size;
    Table.pfnCreateDepthStencilState = DepthState::create;
    Table.pfnDestroyDepthStencilState = DepthState::destroy;
    Table.pfnCalcPrivateRasterizerStateSize = RasterState::size;
    Table.pfnCreateRasterizerState = RasterState::create;
    Table.pfnDestroyRasterizerState = RasterState::destroy;
    Table.pfnCalcPrivateRootSignatureSize = rootSize;
    Table.pfnCreateRootSignature = createRoot;
    Table.pfnDestroyRootSignature = destroyRoot;
    Table.pfnCalcPrivateShaderSize = shaderSize;
    Table.pfnCalcPrivateTessellationShaderSize = shaderSize;
    Table.pfnCreateVertexShader = createShader<1>;
    Table.pfnCreatePixelShader = createShader<0>;
    Table.pfnCreateGeometryShader = createShader<2>;
    Table.pfnCreateComputeShader = createShader<5>;
    Table.pfnCreateHullShader = createShader<3>;
    Table.pfnCreateDomainShader = createShader<4>;
    Table.pfnDestroyShader = destroyShader;
#ifdef FEME_BOOTSTRAP_WARP_COMPILER
    Table.pfnCalcPrivateCommandPoolSize = commandPoolSize;
    Table.pfnCreateCommandPool = createCommandPool;
    Table.pfnDestroyCommandPool = destroyCommandPool;
    Table.pfnResetCommandPool = resetCommandPool;
    Table.pfnGetDescriptorSizeInBytes = descriptorSize;
    Table.pfnCalcPrivatePipelineStateSize = pipelineSize;
    Table.pfnCreatePipelineState = createPipeline;
    Table.pfnDestroyPipelineState = destroyPipeline;
#endif
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
            ExecutableShaderQueries.load(), UnsupportedCalls.load(),
            CompiledShadersCreated.load(),  CompiledShadersDestroyed.load(),
            PipelinesCreated.load(),        PipelinesDestroyed.load()};
}
