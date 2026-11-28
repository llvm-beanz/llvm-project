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

#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
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
std::atomic<UINT> DispatchesExecuted{0};
std::atomic<UINT> CopiesExecuted{0};

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

#ifdef FEME_BOOTSTRAP_WARP_COMPILER
void reportObjectError(D3D12DDI_HCOMMANDQUEUE Queue, HRESULT Result);
void reportObjectError(D3D12DDI_HCOMMANDLIST List, HRESULT Result);
#endif

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
#ifdef FEME_BOOTSTRAP_WARP_COMPILER
    else if constexpr (std::is_same_v<Handle, D3D12DDI_HCOMMANDQUEUE> ||
                       std::is_same_v<Handle, D3D12DDI_HCOMMANDLIST>)
      reportObjectError(Owner, E_NOTIMPL);
#endif
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

constexpr UINT MaxBufferRootParameters = 32;
struct BufferRootParameter {
  UINT Namespace = 0;
  UINT Space = 0;
  UINT Register = 0;
};

struct RootSignature {
  feme::probe::WarpDeviceState *Owner;
  D3D12DDI_ROOT_SIGNATURE_FLAGS Flags;
  UINT Count = 0;
  std::array<BufferRootParameter, MaxBufferRootParameters> Parameters{};
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

HRESULT validateRoot(const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0013 *Args) {
  if (!Args || !Args->pRootSignature_1_1 || Args->NodeMask != 1)
    return E_INVALIDARG;
  if (Args->Version != D3D12DDI_ROOT_SIGNATURE_VERSION_1_1 ||
      Args->pRootSignature_1_1->NumStaticSamplers)
    return E_NOTIMPL;
  const auto &Root = *Args->pRootSignature_1_1;
#ifndef FEME_BOOTSTRAP_WARP_COMPILER
  if (Root.NumParameters)
    return E_NOTIMPL;
#else
  if (Root.NumParameters > MaxBufferRootParameters ||
      (Root.NumParameters && !Root.pRootParameters))
    return E_INVALIDARG;
  for (UINT I = 0; I < Root.NumParameters; ++I) {
    const auto &Parameter = Root.pRootParameters[I];
    if ((Parameter.ParameterType != D3D12DDI_ROOT_PARAMETER_TYPE_SRV &&
         Parameter.ParameterType != D3D12DDI_ROOT_PARAMETER_TYPE_UAV) ||
        Parameter.ShaderVisibility != D3D12DDI_SHADER_VISIBILITY_ALL)
      return E_NOTIMPL;
    auto Flags = Parameter.Descriptor.Flags;
    if (Flags != D3D12DDI_ROOT_DESCRIPTOR_FLAG_0013_NONE &&
        Flags != D3D12DDI_ROOT_DESCRIPTOR_FLAG_0013_DATA_VOLATILE &&
        Flags !=
            D3D12DDI_ROOT_DESCRIPTOR_FLAG_0013_DATA_STATIC_WHILE_SET_AT_EXECUTE &&
        Flags != D3D12DDI_ROOT_DESCRIPTOR_FLAG_0013_DATA_STATIC)
      return E_INVALIDARG;
    for (UINT J = 0; J < I; ++J) {
      const auto &Other = Root.pRootParameters[J];
      if (Parameter.ParameterType == Other.ParameterType &&
          Parameter.Descriptor.RegisterSpace ==
              Other.Descriptor.RegisterSpace &&
          Parameter.Descriptor.ShaderRegister ==
              Other.Descriptor.ShaderRegister)
        return E_INVALIDARG;
    }
  }
#endif
  return S_OK;
}

SIZE_T APIENTRY rootSize(D3D12DDI_HDEVICE Device,
                         const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0013 *Args) {
  HRESULT Result = validateRoot(Args);
  if (FAILED(Result)) {
    reportError(Device, Result);
    return 0;
  }
  return sizeof(RootSignature);
}

HRESULT APIENTRY createRoot(D3D12DDI_HDEVICE Device,
                            const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0013 *Args,
                            D3D12DDI_HROOTSIGNATURE Root) {
  HRESULT Result = validateRoot(Args);
  if (FAILED(Result))
    return Result;
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (!Owner || !Root.pDrvPrivate)
    return E_INVALIDARG;
  auto *State = new (Root.pDrvPrivate)
      RootSignature{Owner, Args->pRootSignature_1_1->Flags};
  State->Count = Args->pRootSignature_1_1->NumParameters;
  for (UINT I = 0; I < State->Count; ++I) {
    const auto &Parameter = Args->pRootSignature_1_1->pRootParameters[I];
    State->Parameters[I] = {
        Parameter.ParameterType == D3D12DDI_ROOT_PARAMETER_TYPE_SRV ? 1u : 2u,
        Parameter.Descriptor.RegisterSpace,
        Parameter.Descriptor.ShaderRegister};
  }
  ++Owner->LiveRootSignatures;
  return S_OK;
}

void APIENTRY destroyRoot(D3D12DDI_HDEVICE Device,
                          D3D12DDI_HROOTSIGNATURE Root) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  auto *State = static_cast<RootSignature *>(Root.pDrvPrivate);
  if (!Owner || !State || State->Owner != Owner) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  State->~RootSignature();
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
    auto Compile = ((Args->pShaderCode[0] >> 4) & 0xf) == 6
                       ? FeMeWarpCompileDXILProgram
                       : FeMeWarpCompileRawDXBC;
    Result = Compile(Args->pShaderCode, Args->pShaderCode[1], &Compiled, &Info);
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
struct PoolEpoch {
  std::atomic<UINT> References{1};
  UINT64 Generation = 0;
  UINT Recording = 0;
  bool Alive = true;
};

void releaseEpoch(PoolEpoch *Epoch) {
  if (Epoch && --Epoch->References == 0)
    delete Epoch;
}

struct CommandPool {
  feme::probe::WarpDeviceState *Owner;
  std::vector<UINT8> Bytes;
  PoolEpoch *Epoch;
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
  auto *Epoch = new (std::nothrow) PoolEpoch;
  if (!Epoch)
    return E_OUTOFMEMORY;
  new (Pool.pDrvPrivate) CommandPool{Owner, {}, Epoch};
  ++Owner->LiveCommandPools;
  return S_OK;
}

void APIENTRY destroyCommandPool(D3D12DDI_HDEVICE Device,
                                 D3D12DDI_HCOMMANDPOOL_0040 Pool) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  auto *State = static_cast<CommandPool *>(Pool.pDrvPrivate);
  if (!Owner || !State || State->Owner != Owner || State->Epoch->Recording) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  State->Epoch->Alive = false;
  releaseEpoch(State->Epoch);
  State->~CommandPool();
  --Owner->LiveCommandPools;
}

void APIENTRY resetCommandPool(D3D12DDI_HDEVICE Device,
                               D3D12DDI_HCOMMANDPOOL_0040 Pool) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  auto *State = static_cast<CommandPool *>(Pool.pDrvPrivate);
  if (!Owner || !State || State->Owner != Owner || State->Epoch->Recording) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  State->Bytes.clear();
  ++State->Epoch->Generation;
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
  auto *Root = static_cast<RootSignature *>(Args->hRootSignature.pDrvPrivate);
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

struct FenceObject {
  feme::probe::WarpDeviceState *Owner;
  D3D12DDI_FENCE Placement;
};

HRESULT validateFence(const D3D12DDIARG_CREATE_FENCE *Args) {
  if (!Args || Args->FenceCount != 1 || !Args->Fences)
    return E_INVALIDARG;
  const auto &Fence = Args->Fences[0];
  if ((Fence.FenceValue.BaseAddress & 7) ||
      (Fence.FenceMonitoredValue.BaseAddress & 7) ||
      (Fence.Flags != D3D12DDI_FENCE_FLAG_NONE &&
       Fence.Flags != D3D12DDI_FENCE_FLAG_BOTTOM_OF_PIPE))
    return E_INVALIDARG;
  return S_OK;
}

SIZE_T APIENTRY fenceSize(D3D12DDI_HDEVICE Device,
                          const D3D12DDIARG_CREATE_FENCE *Args) {
  HRESULT Result = validateFence(Args);
  if (FAILED(Result)) {
    reportError(Device, Result);
    return 0;
  }
  return sizeof(FenceObject);
}

HRESULT APIENTRY createFence(D3D12DDI_HDEVICE Device, D3D12DDI_HFENCE Fence,
                             const D3D12DDIARG_CREATE_FENCE *Args) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (!Owner || !Fence.pDrvPrivate)
    return E_INVALIDARG;
  HRESULT Result = validateFence(Args);
  if (FAILED(Result))
    return Result;
  // The runtime owns the monitored synchronization object and its kernel
  // Signal/Wait operations; this object retains only the UMD placements.
  new (Fence.pDrvPrivate) FenceObject{Owner, Args->Fences[0]};
  ++Owner->LiveFences;
  return S_OK;
}

void APIENTRY destroyFence(D3D12DDI_HDEVICE Device, D3D12DDI_HFENCE Fence) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  auto *State = static_cast<FenceObject *>(Fence.pDrvPrivate);
  if (!Owner || !State || State->Owner != Owner) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  State->~FenceObject();
  --Owner->LiveFences;
}

struct PendingSubmission;
struct CommandQueue {
  feme::probe::WarpDeviceState *Owner;
  D3D12DDI_COMMAND_QUEUE_FLAGS Flags;
  SRWLOCK Lock = SRWLOCK_INIT;
  D3D12DDI_HRTCOMMANDQUEUE Runtime{};
  HANDLE Context = nullptr;
  UINT64 Buffer = 0;
  UINT64 BufferSize = 0;
  D3DKMT_HANDLE ReadyFence = 0;
  D3DKMT_HANDLE DoneFence = 0;
  volatile UINT64 *ReadyValue = nullptr;
  volatile UINT64 *DoneValue = nullptr;
  HANDLE ReadyEvent = nullptr;
  HANDLE Worker = nullptr;
  CONDITION_VARIABLE WorkAvailable = CONDITION_VARIABLE_INIT;
  PendingSubmission *First = nullptr;
  PendingSubmission *Last = nullptr;
  UINT64 Submitted = 0;
  bool Stopping = false;
  std::atomic<HRESULT> Failure{S_OK};
};

DWORD WINAPI runQueue(void *Argument);

struct CommandRecorder {
  feme::probe::WarpDeviceState *Owner;
  D3D12DDI_COMMAND_QUEUE_FLAGS Flags;
  PoolEpoch *Target = nullptr;
  bool InUse = false;
};

struct RecordedCommand {
  enum class Kind { Dispatch, Copy, Barrier } Type = Kind::Dispatch;
  RecordedCommand *Next = nullptr;
  void *Pipeline = nullptr;
  FeMeWarpDispatchDesc Dispatch;
  std::array<FeMeWarpBufferBinding, MaxBufferRootParameters> Buffers{};
  D3D12DDIARG_BUFFER_PLACEMENT Destination{};
  D3D12DDIARG_BUFFER_PLACEMENT Source{};
  UINT64 Bytes = 0;
  D3D12DDI_HRESOURCE BarrierResource{};

  ~RecordedCommand() {
    if (Pipeline)
      FeMeWarpDestroyComputePipeline(Pipeline);
  }
};

void clearCommands(RecordedCommand *&First, RecordedCommand *&Last) {
  while (First) {
    auto *Next = First->Next;
    delete First;
    First = Next;
  }
  Last = nullptr;
}

void appendCommand(RecordedCommand *&First, RecordedCommand *&Last,
                   RecordedCommand *Command) {
  if (Last)
    Last->Next = Command;
  else
    First = Command;
  Last = Command;
}

struct PendingSubmission {
  PendingSubmission *Next = nullptr;
  UINT64 Value = 0;
  RecordedCommand *First = nullptr;
  RecordedCommand *Last = nullptr;
  ~PendingSubmission() { clearCommands(First, Last); }
};

struct CommandList {
  feme::probe::WarpDeviceState *Owner;
  D3D12DDI_COMMAND_QUEUE_FLAGS Flags;
  CommandRecorder *Recorder = nullptr;
  PoolEpoch *Epoch = nullptr;
  UINT64 Generation = 0;
  bool Open = false;
  void *Pipeline = nullptr;
  const void *RootIdentity = nullptr;
  UINT RootCount = 0;
  std::array<BufferRootParameter, MaxBufferRootParameters> RootParameters{};
  std::array<UINT64, MaxBufferRootParameters> RootAddresses{};
  UINT64 Marker = 0;
  RecordedCommand *First = nullptr;
  RecordedCommand *Last = nullptr;

  void clear() {
    clearCommands(First, Last);
    if (Pipeline)
      FeMeWarpDestroyComputePipeline(Pipeline);
    Pipeline = nullptr;
  }

  void append(RecordedCommand *Command) { appendCommand(First, Last, Command); }
};

void reportObjectError(D3D12DDI_HCOMMANDQUEUE Queue, HRESULT Result) {
  auto *State = static_cast<CommandQueue *>(Queue.pDrvPrivate);
  reportError({State ? State->Owner : nullptr}, Result);
}

void reportObjectError(D3D12DDI_HCOMMANDLIST List, HRESULT Result) {
  auto *State = static_cast<CommandList *>(List.pDrvPrivate);
  reportError({State ? State->Owner : nullptr}, Result);
}

bool validQueueFlags(D3D12DDI_COMMAND_QUEUE_FLAGS Flags) {
  return Flags == (D3D12DDI_COMMAND_QUEUE_FLAG_3D |
                   D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE |
                   D3D12DDI_COMMAND_QUEUE_FLAG_COPY) ||
         Flags == (D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE |
                   D3D12DDI_COMMAND_QUEUE_FLAG_COPY) ||
         Flags == D3D12DDI_COMMAND_QUEUE_FLAG_COPY;
}

HRESULT validateQueue(const D3D12DDIARG_CREATECOMMANDQUEUE_0050 *Args) {
  if (!Args || Args->NodeMask != 1 || !validQueueFlags(Args->QueueFlags))
    return E_INVALIDARG;
  if (Args->QueueCreationFlags != D3D12DDI_COMMAND_QUEUE_CREATION_FLAG_NONE ||
      Args->SchedulingGroup.pDrvPrivate)
    return E_NOTIMPL;
  return S_OK;
}

SIZE_T APIENTRY queueSize(D3D12DDI_HDEVICE Device,
                          const D3D12DDIARG_CREATECOMMANDQUEUE_0050 *Args) {
  HRESULT Result = validateQueue(Args);
  if (FAILED(Result)) {
    reportError(Device, Result);
    return 0;
  }
  return sizeof(CommandQueue);
}

HRESULT checkedCallbackResult(HRESULT Result) {
  return Result == S_OK || FAILED(Result) ? Result : E_UNEXPECTED;
}

HRESULT lastWindowsError() {
  DWORD Error = GetLastError();
  return Error ? HRESULT_FROM_WIN32(Error) : E_UNEXPECTED;
}

HRESULT createQueueFence(CommandQueue *State, D3DKMT_HANDLE &Handle,
                         volatile UINT64 *&Value) {
  D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 Create{};
  Create.Info.Type = D3DDDI_MONITORED_FENCE;
  HRESULT Result = checkedCallbackResult(
      State->Owner->KTCallbacks->pfnCreateSynchronizationObject2Cb(
          State->Owner->RTDevice, &Create));
  Handle = Create.hSyncObject;
  Value = static_cast<volatile UINT64 *>(
      Create.Info.MonitoredFence.FenceValueCPUVirtualAddress);
  if (FAILED(Result))
    return Result;
  return Handle && Value && !(reinterpret_cast<UINT64>(Value) & 7)
             ? S_OK
             : E_UNEXPECTED;
}

HRESULT releaseQueueResources(CommandQueue *State) {
  auto *Owner = State->Owner;
  if (State->Context) {
    D3DDDICB_DESTROYCONTEXT Destroy{State->Context};
    HRESULT Result = checkedCallbackResult(
        Owner->UMCallbacks->pfnDestroyContextCb(State->Runtime, &Destroy));
    if (FAILED(Result))
      return Result;
    State->Context = nullptr;
  }
  for (auto *Fence : {&State->ReadyFence, &State->DoneFence}) {
    if (!*Fence)
      continue;
    D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT Destroy{};
    Destroy.hSyncObject = *Fence;
    HRESULT Result = checkedCallbackResult(
        Owner->KTCallbacks->pfnDestroySynchronizationObjectCb(Owner->RTDevice,
                                                              &Destroy));
    if (FAILED(Result))
      return Result;
    *Fence = 0;
  }
  State->ReadyValue = State->DoneValue = nullptr;
  if (State->ReadyEvent) {
    if (!CloseHandle(State->ReadyEvent))
      return lastWindowsError();
    State->ReadyEvent = nullptr;
  }
  return S_OK;
}

HRESULT APIENTRY createQueue(D3D12DDI_HDEVICE Device,
                             const D3D12DDIARG_CREATECOMMANDQUEUE_0050 *Args,
                             D3D12DDI_HCOMMANDQUEUE Queue,
                             D3D12DDI_HRTCOMMANDQUEUE Runtime) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (!Owner || !Queue.pDrvPrivate || !Runtime.handle)
    return E_INVALIDARG;
  if (!Owner->UMCallbacks || !Owner->UMCallbacks->pfnCreateContextVirtualCb ||
      !Owner->UMCallbacks->pfnDestroyContextCb || !Owner->KTCallbacks ||
      !Owner->KTCallbacks->pfnCreateSynchronizationObject2Cb ||
      !Owner->KTCallbacks->pfnDestroySynchronizationObjectCb ||
      !Owner->KTCallbacks->pfnSignalSynchronizationObjectFromGpuCb ||
      !Owner->KTCallbacks->pfnWaitForSynchronizationObjectFromGpuCb ||
      !Owner->KTCallbacks->pfnSignalSynchronizationObjectFromCpuCb ||
      !Owner->KTCallbacks->pfnWaitForSynchronizationObjectFromCpuCb)
    return E_NOINTERFACE;
  HRESULT Result = validateQueue(Args);
  if (FAILED(Result))
    return Result;
  auto *State = new (Queue.pDrvPrivate) CommandQueue{Owner, Args->QueueFlags};
  State->Runtime = Runtime;
  State->BufferSize = sizeof(State->Buffer);
  feme::probe::WarpPrivateContextBuffer Private{&State->Buffer,
                                                &State->BufferSize};
  D3DDDICB_CREATECONTEXTVIRTUAL Context{};
  // WARP's virtual GPU uses the software node; the physical path queries
  // its preferred node during device initialization.
  Context.NodeOrdinal = Owner->VirtualGpu ? 3 : Owner->NodeOrdinal;
  Context.EngineAffinity = Owner->EngineAffinity ? Owner->EngineAffinity : 1;
  Context.pPrivateDriverData = &Private;
  Context.PrivateDriverDataSize = sizeof(Private);
  Result = Owner->UMCallbacks->pfnCreateContextVirtualCb(Runtime, &Context);
  State->Context = Context.hContext;
  Result = checkedCallbackResult(Result);
  if (SUCCEEDED(Result) && (!State->Context || !State->BufferSize))
    Result = E_UNEXPECTED;
  if (SUCCEEDED(Result))
    Result = createQueueFence(State, State->ReadyFence, State->ReadyValue);
  if (SUCCEEDED(Result))
    Result = createQueueFence(State, State->DoneFence, State->DoneValue);
  if (SUCCEEDED(Result)) {
    State->ReadyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!State->ReadyEvent)
      Result = lastWindowsError();
  }
  if (SUCCEEDED(Result)) {
    State->Worker = CreateThread(nullptr, 0, runQueue, State, 0, nullptr);
    if (!State->Worker)
      Result = lastWindowsError();
  }
  if (FAILED(Result)) {
    std::fprintf(stderr, "FeMe queue creation failed: 0x%08lx\n",
                 static_cast<unsigned long>(Result));
    HRESULT Cleanup = releaseQueueResources(State);
    if (FAILED(Cleanup)) {
      Owner->HeapCleanupResult = Cleanup;
      reportError(Device, Cleanup);
    }
    State->~CommandQueue();
    return Result;
  }
  ++Owner->LiveCommandQueues;
  return S_OK;
}

void APIENTRY destroyQueue(D3D12DDI_HDEVICE Device,
                           D3D12DDI_HCOMMANDQUEUE Queue) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  auto *State = static_cast<CommandQueue *>(Queue.pDrvPrivate);
  if (!Owner || !State || State->Owner != Owner) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  AcquireSRWLockExclusive(&State->Lock);
  State->Stopping = true;
  WakeConditionVariable(&State->WorkAvailable);
  ReleaseSRWLockExclusive(&State->Lock);
  if (WaitForSingleObject(State->Worker, INFINITE) != WAIT_OBJECT_0) {
    reportError(Device, lastWindowsError());
    return;
  }
  if (!CloseHandle(State->Worker)) {
    reportError(Device, lastWindowsError());
    return;
  }
  State->Worker = nullptr;
  HRESULT Result = releaseQueueResources(State);
  if (FAILED(Result)) {
    Owner->HeapCleanupResult = Result;
    reportError(Device, Result);
    return;
  }
  State->~CommandQueue();
  --Owner->LiveCommandQueues;
}

SIZE_T APIENTRY
recorderSize(D3D12DDI_HDEVICE Device,
             const D3D12DDIARG_CREATE_COMMAND_RECORDER_0040 *Args) {
  if (!Args || !validQueueFlags(Args->QueueFlags) ||
      Args->RecorderFlags != D3D12DDI_COMMAND_RECORDER_FLAG_NONE) {
    reportError(Device, E_INVALIDARG);
    return 0;
  }
  return sizeof(CommandRecorder);
}

HRESULT APIENTRY
createRecorder(D3D12DDI_HDEVICE Device,
               const D3D12DDIARG_CREATE_COMMAND_RECORDER_0040 *Args,
               D3D12DDI_HCOMMANDRECORDER_0040 Recorder) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (!Owner || !Recorder.pDrvPrivate || !Args ||
      !validQueueFlags(Args->QueueFlags) ||
      Args->RecorderFlags != D3D12DDI_COMMAND_RECORDER_FLAG_NONE)
    return E_INVALIDARG;
  new (Recorder.pDrvPrivate) CommandRecorder{Owner, Args->QueueFlags};
  ++Owner->LiveCommandRecorders;
  return S_OK;
}

void APIENTRY destroyRecorder(D3D12DDI_HDEVICE Device,
                              D3D12DDI_HCOMMANDRECORDER_0040 Recorder) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  auto *State = static_cast<CommandRecorder *>(Recorder.pDrvPrivate);
  if (!Owner || !State || State->Owner != Owner || State->InUse) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  releaseEpoch(State->Target);
  State->~CommandRecorder();
  --Owner->LiveCommandRecorders;
}

void APIENTRY recorderTarget(D3D12DDI_HDEVICE Device,
                             D3D12DDI_HCOMMANDRECORDER_0040 Recorder,
                             D3D12DDI_HCOMMANDPOOL_0040 Pool) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  auto *State = static_cast<CommandRecorder *>(Recorder.pDrvPrivate);
  auto *Target = static_cast<CommandPool *>(Pool.pDrvPrivate);
  if (!Owner || !State || State->Owner != Owner || State->InUse ||
      (Target && Target->Owner != Owner)) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  releaseEpoch(State->Target);
  State->Target = Target ? Target->Epoch : nullptr;
  if (State->Target)
    ++State->Target->References;
}

SIZE_T APIENTRY listSize(D3D12DDI_HDEVICE Device,
                         const D3D12DDIARG_CREATE_COMMAND_LIST_0040 *Args) {
  // The runtime does not initialize creation-only flags in size queries.
  if (!Args || Args->NodeMask != 1 ||
      Args->Type != D3D12DDI_COMMAND_LIST_TYPE_DIRECT ||
      !validQueueFlags(Args->QueueFlags)) {
    reportError(Device, E_INVALIDARG);
    return 0;
  }
  return sizeof(CommandList);
}

HRESULT APIENTRY createList(D3D12DDI_HDEVICE Device,
                            const D3D12DDIARG_CREATE_COMMAND_LIST_0040 *Args,
                            D3D12DDI_HCOMMANDLIST List,
                            D3D12DDI_HRTCOMMANDLIST Runtime) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (!Owner || !List.pDrvPrivate)
    return E_INVALIDARG;
  if (!listSize(Device, Args))
    return E_INVALIDARG;
  if (Args->CommandListFlags &
      ~(D3D12DDI_COMMAND_LIST_FLAG_ENABLE_MARKERS |
        D3D12DDI_COMMAND_LIST_FLAG_0010_ENABLE_FULLPIPELINE_MARKERS))
    return E_INVALIDARG;
  if (!Runtime.handle || !Owner->UMCallbacks ||
      !Owner->UMCallbacks->pfnSetCommandListDDITableCb ||
      !Owner->CommandListTables || !Owner->CommandListTables[Args->Type].handle)
    return E_NOINTERFACE;
  new (List.pDrvPrivate) CommandList{Owner, Args->QueueFlags};
  ++Owner->LiveCommandLists;
  Owner->UMCallbacks->pfnSetCommandListDDITableCb(
      Runtime, Owner->CommandListTables[Args->Type]);
  return S_OK;
}

void closeListState(CommandList *State) {
  State->Open = false;
  if (State->Recorder) {
    State->Recorder->InUse = false;
    State->Recorder = nullptr;
    --State->Epoch->Recording;
  }
}

void APIENTRY destroyList(D3D12DDI_HDEVICE Device, D3D12DDI_HCOMMANDLIST List) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  auto *State = static_cast<CommandList *>(List.pDrvPrivate);
  if (!Owner || !State || State->Owner != Owner) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  closeListState(State);
  State->clear();
  releaseEpoch(State->Epoch);
  State->~CommandList();
  --Owner->LiveCommandLists;
}

void APIENTRY resetList(D3D12DDI_HCOMMANDLIST List,
                        const D3D12DDIARG_RESETCOMMANDLIST_0040 *Args) {
  auto *State = static_cast<CommandList *>(List.pDrvPrivate);
  auto *Recorder = Args ? static_cast<CommandRecorder *>(
                              Args->hDrvCommandRecorder.pDrvPrivate)
                        : nullptr;
  if (!State || State->Open || !Recorder || Recorder->Owner != State->Owner ||
      (Recorder->Flags & State->Flags) != State->Flags || Recorder->InUse ||
      !Recorder->Target || !Recorder->Target->Alive ||
      (Args->CommandListFlags &
       ~(D3D12DDI_COMMAND_LIST_FLAG_ENABLE_MARKERS |
         D3D12DDI_COMMAND_LIST_FLAG_0010_ENABLE_FULLPIPELINE_MARKERS))) {
    reportObjectError(List, E_INVALIDARG);
    return;
  }
  State->clear();
  releaseEpoch(State->Epoch);
  State->Epoch = Recorder->Target;
  ++State->Epoch->References;
  State->Generation = State->Epoch->Generation;
  State->Recorder = Recorder;
  Recorder->InUse = true;
  ++State->Epoch->Recording;
  State->Open = true;
  State->RootIdentity = nullptr;
  State->RootCount = 0;
  State->RootAddresses.fill(0);
}

void APIENTRY closeList(D3D12DDI_HCOMMANDLIST List) {
  auto *State = static_cast<CommandList *>(List.pDrvPrivate);
  if (!State || !State->Open) {
    reportObjectError(List, E_INVALIDARG);
    return;
  }
  closeListState(State);
}

CommandList *recordingList(D3D12DDI_HCOMMANDLIST List) {
  auto *State = static_cast<CommandList *>(List.pDrvPrivate);
  if (!State || !State->Open) {
    reportObjectError(List, E_INVALIDARG);
    return nullptr;
  }
  return State;
}

void APIENTRY setPipeline(D3D12DDI_HCOMMANDLIST List,
                          D3D12DDI_HPIPELINESTATE Pipeline) {
  auto *State = recordingList(List);
  if (!State)
    return;
  auto *Object = static_cast<PipelineObject *>(Pipeline.pDrvPrivate);
  if (!Object || Object->Owner != State->Owner ||
      !(State->Flags & D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE) ||
      (!Object->Compute && !(State->Flags & D3D12DDI_COMMAND_QUEUE_FLAG_3D))) {
    reportObjectError(List, E_INVALIDARG);
    return;
  }
  if (!Object->Compute) {
    if (State->Pipeline)
      FeMeWarpDestroyComputePipeline(State->Pipeline);
    State->Pipeline = nullptr;
    return;
  }
  void *Retained = nullptr;
  HRESULT Result = FeMeWarpRetainComputePipeline(Object->Compiled, &Retained);
  if (FAILED(Result)) {
    reportObjectError(List, Result);
    return;
  }
  if (State->Pipeline)
    FeMeWarpDestroyComputePipeline(State->Pipeline);
  State->Pipeline = Retained;
}

void APIENTRY setComputeRoot(D3D12DDI_HCOMMANDLIST List,
                             D3D12DDI_HROOTSIGNATURE Root) {
  auto *State = recordingList(List);
  if (!State)
    return;
  auto *Signature = static_cast<RootSignature *>(Root.pDrvPrivate);
  if (Signature && Signature->Owner != State->Owner) {
    reportObjectError(List, E_INVALIDARG);
    return;
  }
  if (State->RootIdentity == Signature)
    return;
  State->RootIdentity = Signature;
  State->RootCount = Signature ? Signature->Count : 0;
  if (Signature)
    State->RootParameters = Signature->Parameters;
  State->RootAddresses.fill(0);
}

void setRootBuffer(D3D12DDI_HCOMMANDLIST List, UINT Index, UINT64 Address,
                   UINT Namespace) {
  auto *State = recordingList(List);
  if (!State)
    return;
  void *Data = nullptr;
  UINT64 Size = 0;
  HRESULT Result = feme::probe::getWarpBufferAddress(
      State->Owner, Address, Data, Size, Namespace == 2);
  if (Index >= State->RootCount || Address % 16 ||
      State->RootParameters[Index].Namespace != Namespace || FAILED(Result)) {
    reportObjectError(List, E_INVALIDARG);
    return;
  }
  State->RootAddresses[Index] = Address;
}

void APIENTRY setRootUAV(D3D12DDI_HCOMMANDLIST List, UINT Index,
                         D3D12DDI_GPU_VIRTUAL_ADDRESS Address) {
  setRootBuffer(List, Index, Address, 2);
}

void APIENTRY setRootSRV(D3D12DDI_HCOMMANDLIST List, UINT Index,
                         D3D12DDI_GPU_VIRTUAL_ADDRESS Address) {
  setRootBuffer(List, Index, Address, 1);
}

void APIENTRY dispatch(D3D12DDI_HCOMMANDLIST List, UINT X, UINT Y, UINT Z) {
  auto *State = recordingList(List);
  if (!State)
    return;
  if (!State->Pipeline || X > 65535 || Y > 65535 || Z > 65535) {
    reportObjectError(List, E_INVALIDARG);
    return;
  }
  auto Command =
      std::unique_ptr<RecordedCommand>(new (std::nothrow) RecordedCommand);
  if (!Command) {
    reportObjectError(List, E_OUTOFMEMORY);
    return;
  }
  HRESULT Result =
      FeMeWarpRetainComputePipeline(State->Pipeline, &Command->Pipeline);
  if (FAILED(Result)) {
    reportObjectError(List, Result);
    return;
  }
  Command->Dispatch.GroupCount[0] = X;
  Command->Dispatch.GroupCount[1] = Y;
  Command->Dispatch.GroupCount[2] = Z;
  Command->Dispatch.BufferCount = State->RootCount;
  Command->Dispatch.Buffers = Command->Buffers.data();
  for (UINT I = 0; I < State->RootCount; ++I) {
    auto &Buffer = Command->Buffers[I];
    Result = feme::probe::getWarpBufferAddress(
        State->Owner, State->RootAddresses[I], Buffer.Data, Buffer.SizeInBytes,
        State->RootParameters[I].Namespace == 2);
    if (FAILED(Result)) {
      reportObjectError(List, Result);
      return;
    }
    Buffer.Namespace = State->RootParameters[I].Namespace;
    Buffer.Space = State->RootParameters[I].Space;
    Buffer.Register = State->RootParameters[I].Register;
  }
  State->append(Command.release());
}

void APIENTRY copyBuffer(D3D12DDI_HCOMMANDLIST List,
                         D3D12DDIARG_BUFFER_PLACEMENT Destination,
                         D3D12DDIARG_BUFFER_PLACEMENT Source, UINT64 Bytes) {
  auto *State = recordingList(List);
  if (!State)
    return;
  void *Data = nullptr;
  HRESULT Result = feme::probe::getWarpBufferRange(
      State->Owner, Destination.BaseAddress.UMD.hResource,
      Destination.BaseAddress.UMD.Offset, Bytes, Data);
  if (SUCCEEDED(Result))
    Result = feme::probe::getWarpBufferRange(
        State->Owner, Source.BaseAddress.UMD.hResource,
        Source.BaseAddress.UMD.Offset, Bytes, Data);
  if (FAILED(Result)) {
    reportObjectError(List, Result);
    return;
  }
  auto *Command = new (std::nothrow) RecordedCommand;
  if (!Command) {
    reportObjectError(List, E_OUTOFMEMORY);
    return;
  }
  Command->Type = RecordedCommand::Kind::Copy;
  Command->Destination = Destination;
  Command->Source = Source;
  Command->Bytes = Bytes;
  State->append(Command);
}

void APIENTRY
resourceBarrier(D3D12DDI_HCOMMANDLIST List, UINT Count,
                const D3D12DDIARG_RESOURCE_BARRIER_0022 *Barriers) {
  auto *State = recordingList(List);
  if (!State)
    return;
  if (Count && !Barriers) {
    reportObjectError(List, E_INVALIDARG);
    return;
  }
  for (UINT I = 0; I < Count; ++I) {
    const auto &Barrier = Barriers[I];
    D3D12DDI_HRESOURCE Resource{};
    if (Barrier.Flags != D3D12DDI_RESOURCE_BARRIER_FLAG_NONE) {
      reportObjectError(List, E_NOTIMPL);
      return;
    }
    if (Barrier.Type == D3D12DDI_RESOURCE_BARRIER_TYPE_TRANSITION) {
      if (!Barrier.Transition.hResource.pDrvPrivate ||
          (Barrier.Transition.Subresource != 0 &&
           Barrier.Transition.Subresource !=
               std::numeric_limits<UINT>::max())) {
        reportObjectError(List, E_INVALIDARG);
        return;
      }
      Resource = Barrier.Transition.hResource;
    } else if (Barrier.Type == D3D12DDI_RESOURCE_BARRIER_TYPE_UAV)
      Resource = Barrier.UAV.hResource;
    else {
      reportObjectError(List, E_NOTIMPL);
      return;
    }
    void *Data = nullptr;
    if (Resource.pDrvPrivate) {
      HRESULT Result =
          feme::probe::getWarpBufferRange(State->Owner, Resource, 0, 0, Data);
      if (FAILED(Result)) {
        reportObjectError(List, Result);
        return;
      }
    }
    auto *Command = new (std::nothrow) RecordedCommand;
    if (!Command) {
      reportObjectError(List, E_OUTOFMEMORY);
      return;
    }
    Command->Type = RecordedCommand::Kind::Barrier;
    Command->BarrierResource = Resource;
    State->append(Command);
  }
}

void APIENTRY setMarker(D3D12DDI_HCOMMANDLIST List, UINT64 Marker) {
  if (auto *State = recordingList(List))
    State->Marker = Marker;
}

void APIENTRY clearRootArguments(D3D12DDI_HCOMMANDLIST List) {
  if (auto *State = recordingList(List))
    State->RootAddresses.fill(0);
}

// Direct-list initialization clears graphics bindings even for compute-only
// execution. Accept only their neutral state; graphics work remains
// unsupported.
void validateNeutralState(D3D12DDI_HCOMMANDLIST List, bool Neutral) {
  if (recordingList(List) && !Neutral)
    reportObjectError(List, E_NOTIMPL);
}

void APIENTRY setTopology(D3D12DDI_HCOMMANDLIST List,
                          D3D12DDI_PRIMITIVE_TOPOLOGY Topology) {
  validateNeutralState(List, Topology == D3D12DDI_PRIMITIVE_TOPOLOGY_UNDEFINED);
}

void APIENTRY setDescriptorHeaps(D3D12DDI_HCOMMANDLIST List, UINT Count,
                                 D3D12DDI_HDESCRIPTORHEAP *) {
  validateNeutralState(List, Count == 0);
}

void APIENTRY setViewports(D3D12DDI_HCOMMANDLIST List, UINT Count,
                           const D3D12DDI_VIEWPORT *) {
  validateNeutralState(List, Count == 0);
}

void APIENTRY setScissors(D3D12DDI_HCOMMANDLIST List, UINT Count,
                          const D3D12DDI_RECT *) {
  validateNeutralState(List, Count == 0);
}

void APIENTRY setBlend(D3D12DDI_HCOMMANDLIST List, const FLOAT Factor[4]) {
  validateNeutralState(List, Factor && Factor[0] == 1 && Factor[1] == 1 &&
                                 Factor[2] == 1 && Factor[3] == 1);
}

void APIENTRY setStencil(D3D12DDI_HCOMMANDLIST List, UINT Reference) {
  validateNeutralState(List, Reference == 0);
}

void APIENTRY setIndexBuffer(D3D12DDI_HCOMMANDLIST List,
                             const D3D12DDI_INDEX_BUFFER_VIEW *View) {
  validateNeutralState(List,
                       !View || (!View->BufferLocation && !View->SizeInBytes &&
                                 View->Format == DXGI_FORMAT_UNKNOWN));
}

void APIENTRY setVertexBuffers(D3D12DDI_HCOMMANDLIST List, UINT Start,
                               UINT Count,
                               const D3D12DDI_VERTEX_BUFFER_VIEW *Views) {
  bool Neutral = Start <= 32 && Count <= 32 - Start;
  for (UINT I = 0; Neutral && Views && I < Count; ++I)
    Neutral = !Views[I].BufferLocation && !Views[I].SizeInBytes &&
              !Views[I].StrideInBytes;
  validateNeutralState(List, Neutral);
}

void APIENTRY setStreamOutput(D3D12DDI_HCOMMANDLIST List, UINT Start,
                              UINT Count,
                              const D3D12DDI_STREAM_OUTPUT_BUFFER_VIEW *Views) {
  bool Neutral = Start <= 4 && Count <= 4 - Start;
  for (UINT I = 0; Neutral && Views && I < Count; ++I)
    Neutral = !Views[I].BufferLocation && !Views[I].SizeInBytes &&
              !Views[I].BufferFilledSizeLocation;
  validateNeutralState(List, Neutral);
}

void APIENTRY setRenderTargets(D3D12DDI_HCOMMANDLIST List, UINT Count,
                               const D3D12DDI_CPU_DESCRIPTOR_HANDLE *, BOOL,
                               const D3D12DDI_CPU_DESCRIPTOR_HANDLE *Depth) {
  validateNeutralState(List, !Count && !Depth);
}

void APIENTRY setPredication(D3D12DDI_HCOMMANDLIST List,
                             D3D12DDI_HRESOURCE Resource, UINT64 Offset,
                             D3D12DDI_PREDICATION_OP Operation) {
  validateNeutralState(List,
                       !Resource.pDrvPrivate && !Offset &&
                           Operation == D3D12DDI_PREDICATION_OP_EQUAL_ZERO);
}

HRESULT executeSubmission(CommandQueue *State, PendingSubmission *Submission) {
  HRESULT Result = S_OK;
  for (auto *Command = Submission->First; Command && SUCCEEDED(Result);
       Command = Command->Next) {
    feme::probe::WarpBufferAccess Access(State->Owner);
    if (Command->Type == RecordedCommand::Kind::Barrier) {
      void *Data = nullptr;
      if (Command->BarrierResource.pDrvPrivate)
        Result = Access.range(Command->BarrierResource, 0, 0, Data);
      if (SUCCEEDED(Result))
        std::atomic_thread_fence(std::memory_order_seq_cst);
      continue;
    }
    if (Command->Type == RecordedCommand::Kind::Dispatch) {
      for (UINT J = 0; J < Command->Dispatch.BufferCount && SUCCEEDED(Result);
           ++J) {
        const auto &Buffer = Command->Buffers[J];
        void *Data = nullptr;
        UINT64 Size = 0;
        Result = Access.address(reinterpret_cast<UINT64>(Buffer.Data), Data,
                                Size, Buffer.Namespace == 2);
        if (SUCCEEDED(Result) && Size < Buffer.SizeInBytes)
          Result = E_INVALIDARG;
      }
      if (SUCCEEDED(Result))
        Result = FeMeWarpDispatchCompute(Command->Pipeline, &Command->Dispatch);
      if (SUCCEEDED(Result))
        ++DispatchesExecuted;
      continue;
    }
    void *Destination = nullptr, *Source = nullptr;
    Result = Access.range(Command->Destination.BaseAddress.UMD.hResource,
                          Command->Destination.BaseAddress.UMD.Offset,
                          Command->Bytes, Destination);
    if (SUCCEEDED(Result))
      Result = Access.range(Command->Source.BaseAddress.UMD.hResource,
                            Command->Source.BaseAddress.UMD.Offset,
                            Command->Bytes, Source);
    if (SUCCEEDED(Result)) {
      std::memmove(Destination, Source, static_cast<SIZE_T>(Command->Bytes));
      ++CopiesExecuted;
    }
  }
  return Result;
}

void failQueue(CommandQueue *State, HRESULT Result) {
  HRESULT Expected = S_OK;
  if (State->Failure.compare_exchange_strong(Expected, Result)) {
    std::fprintf(stderr, "FeMe queue execution failed: 0x%08lx\n",
                 static_cast<unsigned long>(Result));
    reportError({State->Owner}, Result);
  }
}

DWORD WINAPI runQueue(void *Argument) {
  auto *State = static_cast<CommandQueue *>(Argument);
  for (;;) {
    AcquireSRWLockExclusive(&State->Lock);
    while (!State->First && !State->Stopping) {
      if (!SleepConditionVariableSRW(&State->WorkAvailable, &State->Lock,
                                     INFINITE, 0)) {
        HRESULT Result = lastWindowsError();
        State->Stopping = true;
        ReleaseSRWLockExclusive(&State->Lock);
        failQueue(State, Result);
        return 0;
      }
    }
    if (!State->First) {
      ReleaseSRWLockExclusive(&State->Lock);
      return 0;
    }
    std::unique_ptr<PendingSubmission> Submission(State->First);
    State->First = Submission->Next;
    if (!State->First)
      State->Last = nullptr;
    ReleaseSRWLockExclusive(&State->Lock);

    auto *Owner = State->Owner;
    HRESULT Result = State->Failure.load();
    if (SUCCEEDED(Result)) {
      D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait{};
      Wait.ObjectCount = 1;
      Wait.ObjectHandleArray = &State->ReadyFence;
      Wait.FenceValueArray = &Submission->Value;
      Wait.hAsyncEvent = State->ReadyEvent;
      Result = checkedCallbackResult(
          Owner->KTCallbacks->pfnWaitForSynchronizationObjectFromCpuCb(
              Owner->RTDevice, &Wait));
    }
    if (SUCCEEDED(Result) &&
        WaitForSingleObject(State->ReadyEvent, INFINITE) != WAIT_OBJECT_0)
      Result = lastWindowsError();
    if (SUCCEEDED(Result)) {
      UINT64 Ready = *State->ReadyValue;
      if (Ready < Submission->Value ||
          Ready == std::numeric_limits<UINT64>::max())
        Result = DXGI_ERROR_DEVICE_REMOVED;
    }
    if (SUCCEEDED(Result))
      Result = executeSubmission(State, Submission.get());
    if (FAILED(Result))
      failQueue(State, Result);

    std::atomic_thread_fence(std::memory_order_seq_cst);
    D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMCPU Signal{};
    Signal.ObjectCount = 1;
    Signal.ObjectHandleArray = &State->DoneFence;
    Signal.FenceValueArray = &Submission->Value;
    Result = checkedCallbackResult(
        Owner->KTCallbacks->pfnSignalSynchronizationObjectFromCpuCb(
            Owner->RTDevice, &Signal));
    if (FAILED(Result))
      failQueue(State, Result);
  }
}

HRESULT snapshotSubmission(UINT Count, const D3D12DDI_HCOMMANDLIST *Lists,
                           PendingSubmission &Submission) {
  for (UINT I = 0; I < Count; ++I)
    for (auto *Command =
             static_cast<CommandList *>(Lists[I].pDrvPrivate)->First;
         Command; Command = Command->Next) {
      auto *Copy = new (std::nothrow) RecordedCommand(*Command);
      if (!Copy)
        return E_OUTOFMEMORY;
      Copy->Next = nullptr;
      Copy->Pipeline = nullptr;
      Copy->Dispatch.Buffers = Copy->Buffers.data();
      appendCommand(Submission.First, Submission.Last, Copy);
      if (Command->Pipeline) {
        HRESULT Result =
            FeMeWarpRetainComputePipeline(Command->Pipeline, &Copy->Pipeline);
        if (FAILED(Result))
          return Result;
      }
    }
  return S_OK;
}

void APIENTRY executeLists(D3D12DDI_HCOMMANDQUEUE Queue, UINT Count,
                           const D3D12DDI_HCOMMANDLIST *Lists) {
  auto *State = static_cast<CommandQueue *>(Queue.pDrvPrivate);
  if (!State || (Count && !Lists)) {
    reportObjectError(Queue, E_INVALIDARG);
    return;
  }
  if (!Count)
    return;
  for (UINT I = 0; I < Count; ++I) {
    auto *List = static_cast<CommandList *>(Lists[I].pDrvPrivate);
    if (!List || List->Owner != State->Owner || List->Open ||
        List->Flags != State->Flags || !List->Epoch || !List->Epoch->Alive ||
        List->Generation != List->Epoch->Generation) {
      reportObjectError(Queue, E_INVALIDARG);
      return;
    }
  }
  std::unique_ptr<PendingSubmission> Submission(new (std::nothrow)
                                                    PendingSubmission);
  if (!Submission) {
    reportObjectError(Queue, E_OUTOFMEMORY);
    return;
  }
  HRESULT Result = snapshotSubmission(Count, Lists, *Submission);
  if (FAILED(Result)) {
    reportObjectError(Queue, Result);
    return;
  }
  AcquireSRWLockExclusive(&State->Lock);
  Result = State->Failure.load();
  if (SUCCEEDED(Result) &&
      (State->Stopping ||
       State->Submitted >= std::numeric_limits<UINT64>::max() - 1))
    Result = E_UNEXPECTED;
  Submission->Value = State->Submitted + 1;
  auto *Owner = State->Owner;
  if (SUCCEEDED(Result)) {
    // Ready is queued after any runtime kernel waits; Done keeps subsequent
    // runtime signals behind completion of the owned CPU commands.
    D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU Signal{};
    Signal.hContext = State->Context;
    Signal.ObjectCount = 1;
    Signal.ObjectHandleArray = &State->ReadyFence;
    Signal.MonitoredFenceValueArray = &Submission->Value;
    Result = checkedCallbackResult(
        Owner->KTCallbacks->pfnSignalSynchronizationObjectFromGpuCb(
            Owner->RTDevice, &Signal));
  }
  if (SUCCEEDED(Result)) {
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU Wait{};
    Wait.hContext = State->Context;
    Wait.ObjectCount = 1;
    Wait.ObjectHandleArray = &State->DoneFence;
    Wait.MonitoredFenceValueArray = &Submission->Value;
    Result = checkedCallbackResult(
        Owner->KTCallbacks->pfnWaitForSynchronizationObjectFromGpuCb(
            Owner->RTDevice, &Wait));
  }
  if (SUCCEEDED(Result)) {
    State->Submitted = Submission->Value;
    if (State->Last)
      State->Last->Next = Submission.get();
    else
      State->First = Submission.get();
    State->Last = Submission.release();
    WakeConditionVariable(&State->WorkAvailable);
  }
  ReleaseSRWLockExclusive(&State->Lock);
  if (FAILED(Result))
    failQueue(State, Result);
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
    Table.pfnCalcPrivateCommandQueueSize = queueSize;
    Table.pfnCreateCommandQueue = createQueue;
    Table.pfnDestroyCommandQueue = destroyQueue;
    Table.pfnCalcPrivateCommandRecorderSize = recorderSize;
    Table.pfnCreateCommandRecorder = createRecorder;
    Table.pfnDestroyCommandRecorder = destroyRecorder;
    Table.pfnCommandRecorderSetCommandPoolAsTarget = recorderTarget;
    Table.pfnCalcPrivateCommandListSize = listSize;
    Table.pfnCreateCommandList = createList;
    Table.pfnDestroyCommandList = destroyList;
    Table.pfnCalcPrivateFenceSize = fenceSize;
    Table.pfnCreateFence = createFence;
    Table.pfnDestroyFence = destroyFence;
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
#ifdef FEME_BOOTSTRAP_WARP_COMPILER
    Table.pfnExecuteCommandLists = executeLists;
#endif
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
#ifdef FEME_BOOTSTRAP_WARP_COMPILER
    Table.pfnCloseCommandList = closeList;
    Table.pfnResetCommandList = resetList;
    Table.pfnSetPipelineState = setPipeline;
    Table.pfnSetComputeRootSignature = setComputeRoot;
    Table.pfnSetComputeRootUnorderedAccessView = setRootUAV;
    Table.pfnSetComputeRootShaderResourceView = setRootSRV;
    Table.pfnDispatch = dispatch;
    Table.pfnCopyBufferRegion = copyBuffer;
    Table.pfnResourceBarrier = resourceBarrier;
    Table.pfnSetMarker = setMarker;
    Table.pfnClearRootArguments = clearRootArguments;
    Table.pfnIaSetTopology = setTopology;
    Table.pfnSetDescriptorHeaps = setDescriptorHeaps;
    Table.pfnRsSetViewports = setViewports;
    Table.pfnRsSetScissorRects = setScissors;
    Table.pfnOmSetBlendFactor = setBlend;
    Table.pfnOmSetStencilRef = setStencil;
    Table.pfnIASetIndexBuffer = setIndexBuffer;
    Table.pfnIASetVertexBuffers = setVertexBuffers;
    Table.pfnSOSetTargets = setStreamOutput;
    Table.pfnOMSetRenderTargets = setRenderTargets;
    Table.pfnSetPredication = setPredication;
#endif
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

extern "C" __declspec(dllexport) void APIENTRY
FeMeWarpGetCommandAudit(UINT *Dispatches, UINT *Copies) {
  if (!Dispatches || !Copies) {
    OutputDebugStringA("FeMe WARP command audit requires output pointers.\n");
    return;
  }
  *Dispatches = DispatchesExecuted.load();
  *Copies = CopiesExecuted.load();
}
