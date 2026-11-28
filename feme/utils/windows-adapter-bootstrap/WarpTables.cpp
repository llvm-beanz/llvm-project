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
#include "feme/Image/CopyWalk.h"
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

#ifdef FEME_BOOTSTRAP_WARP_COMPILER
static_assert(FeMeWarpDefaultComponentMapping ==
              D3D12DDI_DEFAULT_SHADER_4_COMPONENT_MAPPING);
using feme::probe::warpFormatElementSize;

bool typedBufferUAVSupported(DXGI_FORMAT Format) {
  return warpFormatElementSize(Format) != 0;
}
#endif

void APIENTRY checkFormat(D3D12DDI_HDEVICE Device, DXGI_FORMAT Format,
                          UINT *Support) {
  if (!Support) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  *Support = 0;
#ifdef FEME_BOOTSTRAP_WARP_COMPILER
  UINT ElementSize = warpFormatElementSize(Format);
  if (ElementSize)
    *Support = D3D12DDI_FORMAT_SUPPORT_BUFFER;
  if (typedBufferUAVSupported(Format))
    *Support |=
        D3D12DDI_FORMAT_SUPPORT_UAV_READS | D3D12DDI_FORMAT_SUPPORT_UAV_WRITES;
#else
  (void)Format;
#endif
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

constexpr UINT MaxRootParameters = 64;
constexpr UINT MaxRootDwords = 64;
struct BufferRootParameter {
  UINT Namespace = 0;
  UINT Space = 0;
  UINT Register = 0;
  UINT ConstantOffset = 0;
  UINT ConstantCount = 0;
  UINT RangeBase = 0;
  UINT RangeCount = 0;
  D3D12DDI_SHADER_VISIBILITY Visibility = D3D12DDI_SHADER_VISIBILITY_ALL;
};

struct BufferTableRange {
  UINT Namespace = 0;
  UINT Space = 0;
  UINT Register = 0;
  UINT Offset = 0;
  UINT Count = 0;
};

UINT rangeNamespace(D3D12DDI_DESCRIPTOR_RANGE_TYPE Type) {
  switch (Type) {
  case D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV:
    return 1;
  case D3D12DDI_DESCRIPTOR_RANGE_TYPE_UAV:
    return 2;
  case D3D12DDI_DESCRIPTOR_RANGE_TYPE_CBV:
    return 3;
  case D3D12DDI_DESCRIPTOR_RANGE_TYPE_SAMPLER:
    return 0;
  }
  return 0;
}

BufferRootParameter
getRootParameter(const D3D12DDI_ROOT_PARAMETER_0013 &Parameter) {
  if (Parameter.ParameterType ==
      D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
    BufferRootParameter Result;
    Result.RangeCount = Parameter.DescriptorTable.NumDescriptorRanges;
    Result.Visibility = Parameter.ShaderVisibility;
    return Result;
  }
  bool Constants =
      Parameter.ParameterType == D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  return {Parameter.ParameterType == D3D12DDI_ROOT_PARAMETER_TYPE_SRV   ? 1u
          : Parameter.ParameterType == D3D12DDI_ROOT_PARAMETER_TYPE_UAV ? 2u
                                                                        : 3u,
          Constants ? Parameter.Constants.RegisterSpace
                    : Parameter.Descriptor.RegisterSpace,
          Constants ? Parameter.Constants.ShaderRegister
                    : Parameter.Descriptor.ShaderRegister,
          0,
          Constants ? Parameter.Constants.Num32BitValues : 0,
          0,
          0,
          Parameter.ShaderVisibility};
}

struct RootSignature {
  feme::probe::WarpDeviceState *Owner;
  D3D12DDI_ROOT_SIGNATURE_FLAGS Flags;
  UINT Count = 0;
  std::array<BufferRootParameter, MaxRootParameters> Parameters{};
  UINT RangeCount = 0;
  std::array<BufferTableRange, MaxRootParameters> Ranges{};
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
  if (Root.NumParameters > MaxRootParameters ||
      (Root.NumParameters && !Root.pRootParameters))
    return E_INVALIDARG;
  UINT Cost = 0, BindingCount = 0;
  std::array<BufferRootParameter, MaxRootParameters> Bindings{};
  auto AddBinding = [&](BufferRootParameter Binding) {
    if (BindingCount == Bindings.size())
      return false;
    for (UINT I = 0; I < BindingCount; ++I)
      if (Binding.Namespace == Bindings[I].Namespace &&
          Binding.Space == Bindings[I].Space &&
          Binding.Register == Bindings[I].Register &&
          (Binding.Visibility == Bindings[I].Visibility ||
           Binding.Visibility == D3D12DDI_SHADER_VISIBILITY_ALL ||
           Bindings[I].Visibility == D3D12DDI_SHADER_VISIBILITY_ALL))
        return false;
    Bindings[BindingCount++] = Binding;
    return true;
  };
  for (UINT I = 0; I < Root.NumParameters; ++I) {
    const auto &Parameter = Root.pRootParameters[I];
    if ((Parameter.ParameterType != D3D12DDI_ROOT_PARAMETER_TYPE_SRV &&
         Parameter.ParameterType != D3D12DDI_ROOT_PARAMETER_TYPE_UAV &&
         Parameter.ParameterType != D3D12DDI_ROOT_PARAMETER_TYPE_CBV &&
         Parameter.ParameterType !=
             D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE &&
         Parameter.ParameterType !=
             D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) ||
        static_cast<UINT>(Parameter.ShaderVisibility) >
            D3D12DDI_SHADER_VISIBILITY_PIXEL)
      return E_NOTIMPL;
    bool Constants =
        Parameter.ParameterType == D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    bool Table = Parameter.ParameterType ==
                 D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    const auto Binding = getRootParameter(Parameter);
    UINT ParameterCost = Table ? 1 : Constants ? Binding.ConstantCount : 2;
    if (!ParameterCost || ParameterCost > MaxRootDwords - Cost)
      return E_INVALIDARG;
    Cost += ParameterCost;
    if (Table) {
      const auto &Ranges = Parameter.DescriptorTable;
      if (!Ranges.NumDescriptorRanges || !Ranges.pDescriptorRanges ||
          Ranges.NumDescriptorRanges > MaxRootParameters)
        return E_INVALIDARG;
      UINT64 Cursor = 0;
      for (UINT J = 0; J < Ranges.NumDescriptorRanges; ++J) {
        const auto &Range = Ranges.pDescriptorRanges[J];
        UINT Namespace = rangeNamespace(Range.RangeType);
        if (!Namespace || Range.NumDescriptors > MaxRootParameters)
          return E_NOTIMPL;
        if (!Range.NumDescriptors ||
            Range.BaseShaderRegister >
                std::numeric_limits<UINT>::max() - (Range.NumDescriptors - 1))
          return E_INVALIDARG;
        constexpr UINT DataMask =
            D3D12DDI_DESCRIPTOR_RANGE_FLAG_0013_DATA_VOLATILE |
            D3D12DDI_DESCRIPTOR_RANGE_FLAG_0013_DATA_STATIC_WHILE_SET_AT_EXECUTE |
            D3D12DDI_DESCRIPTOR_RANGE_FLAG_0013_DATA_STATIC;
        constexpr UINT Volatile =
            D3D12DDI_DESCRIPTOR_RANGE_FLAG_0013_DESCRIPTORS_VOLATILE;
        constexpr UINT KeepingBounds =
            D3D12DDI_DESCRIPTOR_RANGE_FLAG_0052_DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS;
        UINT Flags = Range.Flags;
        UINT DataFlags = Flags & DataMask;
        if ((Flags & ~(DataMask | Volatile | KeepingBounds)) ||
            (DataFlags && (DataFlags & (DataFlags - 1))) ||
            ((Flags & Volatile) && (Flags & KeepingBounds)))
          return E_INVALIDARG;
        UINT64 Offset = Range.OffsetInDescriptorsFromTableStart ==
                                std::numeric_limits<UINT>::max()
                            ? Cursor
                            : Range.OffsetInDescriptorsFromTableStart;
        if (Offset > std::numeric_limits<UINT>::max() - Range.NumDescriptors)
          return E_INVALIDARG;
        for (UINT K = 0; K < Range.NumDescriptors; ++K) {
          auto Element = Binding;
          Element.Namespace = Namespace;
          Element.Space = Range.RegisterSpace;
          Element.Register = Range.BaseShaderRegister + K;
          if (!AddBinding(Element))
            return E_INVALIDARG;
        }
        Cursor = Offset + Range.NumDescriptors;
      }
      continue;
    }
    auto Flags = Constants ? D3D12DDI_ROOT_DESCRIPTOR_FLAG_0013_NONE
                           : Parameter.Descriptor.Flags;
    if (Flags != D3D12DDI_ROOT_DESCRIPTOR_FLAG_0013_NONE &&
        Flags != D3D12DDI_ROOT_DESCRIPTOR_FLAG_0013_DATA_VOLATILE &&
        Flags !=
            D3D12DDI_ROOT_DESCRIPTOR_FLAG_0013_DATA_STATIC_WHILE_SET_AT_EXECUTE &&
        Flags != D3D12DDI_ROOT_DESCRIPTOR_FLAG_0013_DATA_STATIC)
      return E_INVALIDARG;
    if (!AddBinding(Binding))
      return E_INVALIDARG;
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
  UINT ConstantOffset = 0;
  for (UINT I = 0; I < State->Count; ++I) {
    const auto &Parameter = Args->pRootSignature_1_1->pRootParameters[I];
    State->Parameters[I] = getRootParameter(Parameter);
    if (State->Parameters[I].RangeCount) {
      State->Parameters[I].RangeBase = State->RangeCount;
      UINT Cursor = 0;
      for (UINT J = 0; J < Parameter.DescriptorTable.NumDescriptorRanges; ++J) {
        const auto &Range = Parameter.DescriptorTable.pDescriptorRanges[J];
        UINT Offset = Range.OffsetInDescriptorsFromTableStart ==
                              std::numeric_limits<UINT>::max()
                          ? Cursor
                          : Range.OffsetInDescriptorsFromTableStart;
        State->Ranges[State->RangeCount++] = {
            rangeNamespace(Range.RangeType), Range.RegisterSpace,
            Range.BaseShaderRegister, Offset, Range.NumDescriptors};
        Cursor = Offset + Range.NumDescriptors;
      }
    }
    if (State->Parameters[I].ConstantCount) {
      State->Parameters[I].ConstantOffset = ConstantOffset;
      ConstantOffset += Parameter.Constants.Num32BitValues;
    }
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
  UINT Namespace = 0;
  UINT Stride = 0;
  DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
  D3D12DDI_HRESOURCE Resource{};
  UINT64 ByteOffset = 0;
  UINT64 ByteSize = 0;
  UINT64 Address = 0;
  D3D12DDI_HRESOURCE CounterResource{};
  UINT64 CounterOffset = 0;
  UINT Shader4ComponentMapping = FeMeWarpDefaultComponentMapping;
};

struct DescriptorHeap {
  DescriptorHeap *Next;
  feme::probe::WarpDeviceState *Owner;
  D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 Desc;
  std::unique_ptr<DescriptorSlot[]> Slots;
  feme::probe::WarpDescriptorHeapRegistration Registration{};
};

SRWLOCK DescriptorLock = SRWLOCK_INIT;
DescriptorHeap *DescriptorHeaps = nullptr;

class DescriptorAccess {
public:
  DescriptorAccess() { AcquireSRWLockExclusive(&DescriptorLock); }
  ~DescriptorAccess() { ReleaseSRWLockExclusive(&DescriptorLock); }
};

DescriptorHeap **findDescriptorHeap(D3D12DDI_HDESCRIPTORHEAP Heap) {
  auto **Entry = &DescriptorHeaps;
  while (*Entry && *Entry != Heap.pDrvPrivate)
    Entry = &(*Entry)->Next;
  return Entry;
}

DescriptorSlot *descriptorTableSlot(DescriptorHeap *Heap, UINT64 Address,
                                    UINT Offset) {
  UINT64 Base = reinterpret_cast<UINT64>(Heap->Slots.get());
  if (!(Heap->Desc.Flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) ||
      Address < Base || (Address - Base) % sizeof(DescriptorSlot))
    return nullptr;
  UINT64 Index = (Address - Base) / sizeof(DescriptorSlot);
  if (Index >= Heap->Desc.NumDescriptors ||
      Offset >= Heap->Desc.NumDescriptors - Index)
    return nullptr;
  return Heap->Slots.get() + Index + Offset;
}

DescriptorSlot *descriptorRange(feme::probe::WarpDeviceState *Owner,
                                UINT64 Address, UINT Count,
                                bool CopySource = false) {
  for (auto *Heap = DescriptorHeaps; Heap; Heap = Heap->Next) {
    UINT64 Base = reinterpret_cast<UINT64>(Heap->Slots.get());
    if (Heap->Owner != Owner || Address < Base ||
        (Address - Base) % sizeof(DescriptorSlot) ||
        !(Heap->Desc.Flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_CPU_VISIBLE) ||
        (CopySource &&
         (Heap->Desc.Flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)))
      continue;
    UINT64 Index = (Address - Base) / sizeof(DescriptorSlot);
    if (Index <= Heap->Desc.NumDescriptors &&
        Count <= Heap->Desc.NumDescriptors - Index)
      return Heap->Slots.get() + Index;
  }
  return nullptr;
}

HRESULT
validateDescriptorHeap(const D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 *Args) {
  if (!Args || Args->NodeMask != 1 || !Args->NumDescriptors ||
      Args->NumDescriptors > 1000000 ||
      (Args->Flags & ~(D3D12DDI_DESCRIPTOR_HEAP_FLAG_CPU_VISIBLE |
                       D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)))
    return E_INVALIDARG;
  return Args->Type == D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? S_OK
                                                                 : E_NOTIMPL;
}

SIZE_T APIENTRY
descriptorHeapSize(D3D12DDI_HDEVICE Device,
                   const D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 *Args) {
  HRESULT Result = validateDescriptorHeap(Args);
  if (FAILED(Result)) {
    if (Args)
      std::fprintf(stderr,
                   "FeMe descriptor heap rejected: type=%u count=%u flags=%u "
                   "nodes=%u result=0x%08lx\n",
                   Args->Type, Args->NumDescriptors, Args->Flags,
                   Args->NodeMask, static_cast<unsigned long>(Result));
    reportError(Device, Result);
    return 0;
  }
  return sizeof(DescriptorHeap);
}

HRESULT APIENTRY
createDescriptorHeap(D3D12DDI_HDEVICE Device,
                     const D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 *Args,
                     D3D12DDI_HDESCRIPTORHEAP Heap) {
  HRESULT Result = validateDescriptorHeap(Args);
  if (FAILED(Result))
    return Result;
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (!Owner || !Heap.pDrvPrivate)
    return E_INVALIDARG;
  DescriptorAccess Access;
  if (*findDescriptorHeap(Heap))
    return E_INVALIDARG;
  std::unique_ptr<DescriptorSlot[]> Slots(
      new (std::nothrow) DescriptorSlot[Args->NumDescriptors]);
  if (!Slots)
    return E_OUTOFMEMORY;
  auto *State = new (Heap.pDrvPrivate)
      DescriptorHeap{DescriptorHeaps, Owner, *Args, std::move(Slots)};
  DescriptorHeaps = State;
  AcquireSRWLockExclusive(&Owner->DescriptorLock);
  State->Registration = {Owner->DescriptorList, Heap.pDrvPrivate};
  Owner->DescriptorList = &State->Registration;
  ReleaseSRWLockExclusive(&Owner->DescriptorLock);
  ++Owner->LiveImmutableStates;
  return S_OK;
}

void APIENTRY destroyDescriptorHeap(D3D12DDI_HDEVICE Device,
                                    D3D12DDI_HDESCRIPTORHEAP Heap) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  DescriptorAccess Access;
  auto **Entry = findDescriptorHeap(Heap);
  if (!Owner || !*Entry || (*Entry)->Owner != Owner) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  auto *State = *Entry;
  *Entry = State->Next;
  AcquireSRWLockExclusive(&Owner->DescriptorLock);
  auto **Registration = &Owner->DescriptorList;
  while (*Registration != &State->Registration)
    Registration = &(*Registration)->Next;
  *Registration = State->Registration.Next;
  ReleaseSRWLockExclusive(&Owner->DescriptorLock);
  State->~DescriptorHeap();
  --Owner->LiveImmutableStates;
}

D3D12DDI_CPU_DESCRIPTOR_HANDLE APIENTRY
descriptorCPUStart(D3D12DDI_HDEVICE Device, D3D12DDI_HDESCRIPTORHEAP Heap) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  DescriptorAccess Access;
  auto *State = *findDescriptorHeap(Heap);
  if (!Owner || !State || State->Owner != Owner) {
    reportError(Device, E_INVALIDARG);
    return {};
  }
  if (!(State->Desc.Flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_CPU_VISIBLE))
    return {};
  return {reinterpret_cast<SIZE_T>(State->Slots.get())};
}

D3D12DDI_GPU_DESCRIPTOR_HANDLE APIENTRY
descriptorGPUStart(D3D12DDI_HDEVICE Device, D3D12DDI_HDESCRIPTORHEAP Heap) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  DescriptorAccess Access;
  auto *State = *findDescriptorHeap(Heap);
  if (!Owner || !State || State->Owner != Owner) {
    reportError(Device, E_INVALIDARG);
    return {};
  }
  if (!(State->Desc.Flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
    return {};
  return {reinterpret_cast<UINT64>(State->Slots.get())};
}

void APIENTRY createCBV(D3D12DDI_HDEVICE Device,
                        const D3D12DDI_CONSTANT_BUFFER_VIEW_DESC *Desc,
                        D3D12DDI_CPU_DESCRIPTOR_HANDLE Destination) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  DescriptorSlot Value;
  Value.Namespace = 3;
  if (Desc) {
    if (!Desc->SizeInBytes || Desc->SizeInBytes > 65536 ||
        Desc->SizeInBytes % 256 || Desc->BufferLocation % 256 ||
        Desc->Padding) {
      reportError(Device, E_INVALIDARG);
      return;
    }
    Value.Address = Desc->BufferLocation;
    Value.ByteSize = Desc->SizeInBytes;
    if (Value.Address) {
      void *Data = nullptr;
      UINT64 Extent = 0;
      HRESULT Result =
          feme::probe::getWarpBufferAddress(Owner, Value.Address, Data, Extent);
      if (FAILED(Result) || Value.ByteSize > Extent) {
        reportError(Device, E_INVALIDARG);
        return;
      }
    }
  }
  DescriptorAccess Access;
  auto *Slot = descriptorRange(Owner, Destination.ptr, 1);
  if (!Owner || !Slot) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  *Slot = Value;
}

HRESULT makeBufferView(feme::probe::WarpDeviceState *Owner,
                       D3D12DDI_HRESOURCE Resource, DXGI_FORMAT Format,
                       UINT64 First, UINT Count, UINT Stride, UINT Flags,
                       UINT Namespace, DescriptorSlot &Value) {
  if (!Count || Flags > 1 ||
      (Flags && (Format != DXGI_FORMAT_R32_TYPELESS || Stride)) ||
      (Stride &&
       (Format != DXGI_FORMAT_UNKNOWN || Stride % 4 || Stride > 2048 || Flags)))
    return E_INVALIDARG;
  UINT ElementSize = Stride  ? Stride
                     : Flags ? 4
                             : warpFormatElementSize(Format);
  if (!Flags && !Stride) {
    if (!ElementSize || (Namespace == 2 && !typedBufferUAVSupported(Format)))
      return E_NOTIMPL;
    Value.Format = Format;
  }
  if (First > std::numeric_limits<UINT64>::max() / ElementSize)
    return E_INVALIDARG;
  Value.Namespace = Namespace;
  Value.Stride = Stride;
  Value.Resource = Resource;
  Value.ByteOffset = First * ElementSize;
  Value.ByteSize = UINT64(Count) * ElementSize;
  if (Value.ByteSize > std::numeric_limits<UINT64>::max() - Value.ByteOffset)
    return E_INVALIDARG;
  if (!Resource.pDrvPrivate)
    return S_OK;
  void *Data = nullptr;
  return feme::probe::getWarpBufferRange(Owner, Resource, Value.ByteOffset,
                                         Value.ByteSize, Data, Namespace == 2);
}

void APIENTRY
createSRV(D3D12DDI_HDEVICE Device,
          const D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002 *Desc,
          D3D12DDI_CPU_DESCRIPTOR_HANDLE Destination) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (!Owner || !Desc) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  if (Desc->ResourceDimension != D3D12DDI_RD_BUFFER) {
    reportError(Device, E_NOTIMPL);
    return;
  }
  if (!FeMeWarpValidComponentMapping(Desc->Shader4ComponentMapping)) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  DescriptorSlot Value;
  HRESULT Result = makeBufferView(
      Owner, Desc->hDrvResource, Desc->Format, Desc->Buffer.FirstElement,
      Desc->Buffer.NumElements, Desc->Buffer.StructureByteStride,
      Desc->Buffer.Flags, 1, Value);
  if (FAILED(Result)) {
    reportError(Device, Result);
    return;
  }
  if (Desc->Shader4ComponentMapping != FeMeWarpDefaultComponentMapping &&
      Value.Format == DXGI_FORMAT_UNKNOWN) {
    reportError(Device, E_NOTIMPL);
    return;
  }
  Value.Shader4ComponentMapping = Desc->Shader4ComponentMapping;
  DescriptorAccess Access;
  auto *Slot = descriptorRange(Owner, Destination.ptr, 1);
  if (!Slot) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  *Slot = Value;
}

void APIENTRY
createUAV(D3D12DDI_HDEVICE Device,
          const D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002 *Desc,
          D3D12DDI_CPU_DESCRIPTOR_HANDLE Destination) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (!Owner || !Desc) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  if (Desc->ResourceDimension != D3D12DDI_RD_BUFFER) {
    reportError(Device, E_NOTIMPL);
    return;
  }
  DescriptorSlot Value;
  HRESULT Result = makeBufferView(
      Owner, Desc->hDrvResource, Desc->Format, Desc->Buffer.FirstElement,
      Desc->Buffer.NumElements, Desc->Buffer.StructureByteStride,
      Desc->Buffer.Flags, 2, Value);
  if (FAILED(Result)) {
    reportError(Device, Result);
    return;
  }
  if (Desc->Buffer.hDrvCounterResource.pDrvPrivate) {
    if (!Desc->hDrvResource.pDrvPrivate || !Value.Stride ||
        Desc->Buffer.CounterOffsetInBytes % 4096) {
      reportError(Device, E_INVALIDARG);
      return;
    }
    void *Counter = nullptr;
    Result = feme::probe::getWarpBufferRange(
        Owner, Desc->Buffer.hDrvCounterResource,
        Desc->Buffer.CounterOffsetInBytes, sizeof(UINT), Counter, true);
    if (FAILED(Result)) {
      reportError(Device, Result);
      return;
    }
    Value.CounterResource = Desc->Buffer.hDrvCounterResource;
    Value.CounterOffset = Desc->Buffer.CounterOffsetInBytes;
  } else if (Desc->Buffer.CounterOffsetInBytes) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  DescriptorAccess Access;
  auto *Slot = descriptorRange(Owner, Destination.ptr, 1);
  if (!Slot) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  *Slot = Value;
}

void APIENTRY
copyDescriptors(D3D12DDI_HDEVICE Device, UINT DestinationCount,
                const D3D12DDI_CPU_DESCRIPTOR_HANDLE *Destinations,
                const UINT *DestinationSizes, UINT SourceCount,
                const D3D12DDI_CPU_DESCRIPTOR_HANDLE *Sources,
                const UINT *SourceSizes, D3D12DDI_DESCRIPTOR_HEAP_TYPE Type) {
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (!Owner || Type != D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
      (DestinationCount && !Destinations) || (SourceCount && !Sources)) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  DescriptorAccess Access;
  UINT64 DestinationTotal = 0, SourceTotal = 0;
  for (UINT I = 0; I < DestinationCount; ++I) {
    UINT Count = DestinationSizes ? DestinationSizes[I] : 1;
    if (!descriptorRange(Owner, Destinations[I].ptr, Count)) {
      reportError(Device, E_INVALIDARG);
      return;
    }
    DestinationTotal += Count;
  }
  for (UINT I = 0; I < SourceCount; ++I) {
    UINT Count = SourceSizes ? SourceSizes[I] : 1;
    if (!descriptorRange(Owner, Sources[I].ptr, Count, true)) {
      reportError(Device, E_INVALIDARG);
      return;
    }
    SourceTotal += Count;
  }
  if (DestinationTotal != SourceTotal ||
      SourceTotal >
          std::numeric_limits<SIZE_T>::max() / sizeof(DescriptorSlot)) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  if (!SourceTotal)
    return;
  std::unique_ptr<DescriptorSlot[]> Values(
      new (std::nothrow) DescriptorSlot[static_cast<SIZE_T>(SourceTotal)]);
  if (!Values) {
    reportError(Device, E_OUTOFMEMORY);
    return;
  }
  SIZE_T Offset = 0;
  for (UINT I = 0; I < SourceCount; ++I) {
    UINT Count = SourceSizes ? SourceSizes[I] : 1;
    std::memcpy(Values.get() + Offset,
                descriptorRange(Owner, Sources[I].ptr, Count, true),
                SIZE_T(Count) * sizeof(DescriptorSlot));
    Offset += Count;
  }
  Offset = 0;
  for (UINT I = 0; I < DestinationCount; ++I) {
    UINT Count = DestinationSizes ? DestinationSizes[I] : 1;
    std::memcpy(descriptorRange(Owner, Destinations[I].ptr, Count),
                Values.get() + Offset, SIZE_T(Count) * sizeof(DescriptorSlot));
    Offset += Count;
  }
}

void APIENTRY copyDescriptorsSimple(D3D12DDI_HDEVICE Device, UINT Count,
                                    D3D12DDI_CPU_DESCRIPTOR_HANDLE Destination,
                                    D3D12DDI_CPU_DESCRIPTOR_HANDLE Source,
                                    D3D12DDI_DESCRIPTOR_HEAP_TYPE Type) {
  copyDescriptors(Device, 1, &Destination, &Count, 1, &Source, &Count, Type);
}

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

struct PredicationState {
  D3D12DDI_HRESOURCE Resource{};
  UINT64 Offset = 0;
  D3D12DDI_PREDICATION_OP Operation = D3D12DDI_PREDICATION_OP_EQUAL_ZERO;
};

struct RecordedCommand {
  enum class Kind {
    Dispatch,
    IndirectDispatch,
    Copy,
    Barrier
  } Type = Kind::Dispatch;
  RecordedCommand *Next = nullptr;
  void *Pipeline = nullptr;
  FeMeWarpDispatchDesc Dispatch;
  std::array<FeMeWarpBufferBinding, MaxRootParameters> Buffers{};
  std::array<UINT, MaxRootDwords> RootConstants{};
  std::array<bool, MaxRootParameters> InlineBuffers{};
  std::array<UINT, MaxRootParameters> InlineOffsets{};
  D3D12DDI_HDESCRIPTORHEAP DescriptorHeap{};
  std::array<UINT64, MaxRootParameters> TableDescriptors{};
  D3D12DDIARG_BUFFER_PLACEMENT Destination{};
  D3D12DDIARG_BUFFER_PLACEMENT Source{};
  UINT64 Bytes = 0;
  UINT CopyRows = 1;
  UINT CopySlices = 1;
  UINT SourceRowPitch = 0;
  UINT DestinationRowPitch = 0;
  UINT SourceSlicePitch = 0;
  UINT DestinationSlicePitch = 0;
  D3D12DDI_HRESOURCE BarrierResource{};
  D3D12DDIARG_BUFFER_PLACEMENT ArgumentBuffer{};
  D3D12DDIARG_BUFFER_PLACEMENT CountBuffer{};
  UINT MaxCommandCount = 0;
  UINT ByteStride = 0;
  PredicationState Predicate{};

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
  std::array<BufferRootParameter, MaxRootParameters> RootParameters{};
  std::array<UINT64, MaxRootParameters> RootAddresses{};
  std::array<UINT, MaxRootDwords> RootConstants{};
  std::array<BufferTableRange, MaxRootParameters> RootRanges{};
  std::array<UINT64, MaxRootParameters> RootTables{};
  D3D12DDI_HDESCRIPTORHEAP DescriptorHeap{};
  PredicationState Predicate{};
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

struct CommandSignature {
  CommandSignature *Next;
  feme::probe::WarpDeviceState *Owner;
  UINT ByteStride;
};

SRWLOCK CommandSignatureLock = SRWLOCK_INIT;
CommandSignature *CommandSignatures = nullptr;

class CommandSignatureAccess {
public:
  CommandSignatureAccess() { AcquireSRWLockExclusive(&CommandSignatureLock); }
  ~CommandSignatureAccess() { ReleaseSRWLockExclusive(&CommandSignatureLock); }
};

CommandSignature **findCommandSignature(D3D12DDI_HCOMMANDSIGNATURE Handle) {
  auto **Entry = &CommandSignatures;
  while (*Entry && *Entry != Handle.pDrvPrivate)
    Entry = &(*Entry)->Next;
  return Entry;
}

HRESULT validateCommandSignature(
    const D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 *Args) {
  if (!Args || Args->NodeMask != 1 || !Args->NumArgumentDescs ||
      !Args->pArgumentDescs || Args->ByteStride < 3 * sizeof(UINT) ||
      Args->ByteStride % sizeof(UINT))
    return E_INVALIDARG;
  if (Args->NumArgumentDescs != 1 ||
      Args->pArgumentDescs[0].Type != D3D12DDI_INDIRECT_ARGUMENT_TYPE_DISPATCH)
    return E_NOTIMPL;
  return Args->hRootSignature.pDrvPrivate ? E_INVALIDARG : S_OK;
}

SIZE_T APIENTRY
commandSignatureSize(D3D12DDI_HDEVICE Device,
                     const D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 *Args) {
  HRESULT Result = validateCommandSignature(Args);
  if (FAILED(Result)) {
    reportError(Device, Result);
    return 0;
  }
  return sizeof(CommandSignature);
}

HRESULT APIENTRY
createCommandSignature(D3D12DDI_HDEVICE Device,
                       const D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 *Args,
                       D3D12DDI_HCOMMANDSIGNATURE Handle) {
  HRESULT Result = validateCommandSignature(Args);
  if (FAILED(Result))
    return Result;
  auto *Owner = static_cast<feme::probe::WarpDeviceState *>(Device.pDrvPrivate);
  if (!Owner || !Handle.pDrvPrivate)
    return E_INVALIDARG;
  CommandSignatureAccess Access;
  if (*findCommandSignature(Handle))
    return E_INVALIDARG;
  auto *Signature = new (Handle.pDrvPrivate)
      CommandSignature{CommandSignatures, Owner, Args->ByteStride};
  CommandSignatures = Signature;
  ++Owner->LiveImmutableStates;
  return S_OK;
}

void APIENTRY destroyCommandSignature(D3D12DDI_HDEVICE Device,
                                      D3D12DDI_HCOMMANDSIGNATURE Handle) {
  CommandSignatureAccess Access;
  auto **Entry = findCommandSignature(Handle);
  if (!*Entry || (*Entry)->Owner != Device.pDrvPrivate) {
    reportError(Device, E_INVALIDARG);
    return;
  }
  auto *Signature = *Entry;
  *Entry = Signature->Next;
  --Signature->Owner->LiveImmutableStates;
  Signature->~CommandSignature();
}

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
  State->RootConstants.fill(0);
  State->RootTables.fill(0);
  State->DescriptorHeap = {};
  State->Predicate = {};
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
  if (Signature) {
    State->RootParameters = Signature->Parameters;
    State->RootRanges = Signature->Ranges;
  }
  State->RootAddresses.fill(0);
  State->RootConstants.fill(0);
  State->RootTables.fill(0);
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
  if (Index >= State->RootCount || Address % (Namespace == 3 ? 256 : 16) ||
      State->RootParameters[Index].Namespace != Namespace ||
      State->RootParameters[Index].ConstantCount ||
      State->RootParameters[Index].RangeCount || FAILED(Result)) {
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

void APIENTRY setRootCBV(D3D12DDI_HCOMMANDLIST List, UINT Index,
                         D3D12DDI_GPU_VIRTUAL_ADDRESS Address) {
  setRootBuffer(List, Index, Address, 3);
}

void APIENTRY setRootConstants(D3D12DDI_HCOMMANDLIST List, UINT Index,
                               UINT Count, const void *Data, UINT Offset) {
  auto *State = recordingList(List);
  if (!State)
    return;
  if (Index >= State->RootCount ||
      !State->RootParameters[Index].ConstantCount || (Count && !Data)) {
    reportObjectError(List, E_INVALIDARG);
    return;
  }
  const auto &Parameter = State->RootParameters[Index];
  if (Offset > Parameter.ConstantCount ||
      Count > Parameter.ConstantCount - Offset) {
    reportObjectError(List, E_INVALIDARG);
    return;
  }
  if (Count)
    std::memcpy(State->RootConstants.data() + Parameter.ConstantOffset + Offset,
                Data, Count * sizeof(UINT));
}

void APIENTRY setRootConstant(D3D12DDI_HCOMMANDLIST List, UINT Index, UINT Data,
                              UINT Offset) {
  setRootConstants(List, Index, 1, &Data, Offset);
}

void APIENTRY setRootTable(D3D12DDI_HCOMMANDLIST List, UINT Index,
                           D3D12DDI_GPU_DESCRIPTOR_HANDLE Address) {
  auto *State = recordingList(List);
  if (!State)
    return;
  DescriptorAccess Access;
  auto *Heap = *findDescriptorHeap(State->DescriptorHeap);
  if (Index >= State->RootCount || !State->RootParameters[Index].RangeCount ||
      !Heap || Heap->Owner != State->Owner ||
      !descriptorTableSlot(Heap, Address.ptr, 0)) {
    reportObjectError(List, E_INVALIDARG);
    return;
  }
  State->RootTables[Index] = Address.ptr;
}

HRESULT prepareDispatch(CommandList *State,
                        std::unique_ptr<RecordedCommand> &Command) {
  if (!State->Pipeline)
    return E_INVALIDARG;
  Command.reset(new (std::nothrow) RecordedCommand);
  if (!Command) {
    return E_OUTOFMEMORY;
  }
  HRESULT Result =
      FeMeWarpRetainComputePipeline(State->Pipeline, &Command->Pipeline);
  if (FAILED(Result))
    return Result;
  Command->Dispatch.Buffers = Command->Buffers.data();
  Command->RootConstants = State->RootConstants;
  Command->DescriptorHeap = State->DescriptorHeap;
  Command->Predicate = State->Predicate;
  for (UINT I = 0; I < State->RootCount; ++I) {
    const auto &Parameter = State->RootParameters[I];
    if (Parameter.Visibility != D3D12DDI_SHADER_VISIBILITY_ALL)
      continue;
    if (Parameter.RangeCount) {
      if (!State->RootTables[I])
        continue;
      DescriptorAccess Access;
      auto *Heap = *findDescriptorHeap(State->DescriptorHeap);
      if (!Heap || Heap->Owner != State->Owner)
        return E_INVALIDARG;
      for (UINT J = 0; J < Parameter.RangeCount; ++J) {
        const auto &Range = State->RootRanges[Parameter.RangeBase + J];
        for (UINT K = 0; K < Range.Count; ++K) {
          auto *Slot =
              descriptorTableSlot(Heap, State->RootTables[I], Range.Offset + K);
          if (!Slot)
            return E_INVALIDARG;
          UINT Binding = Command->Dispatch.BufferCount++;
          Command->TableDescriptors[Binding] = reinterpret_cast<UINT64>(Slot);
          auto &Buffer = Command->Buffers[Binding];
          Buffer.Namespace = Range.Namespace;
          Buffer.Space = Range.Space;
          Buffer.Register = Range.Register + K;
        }
      }
      continue;
    }
    if (!Parameter.ConstantCount && !State->RootAddresses[I])
      continue;
    UINT Binding = Command->Dispatch.BufferCount++;
    auto &Buffer = Command->Buffers[Binding];
    if (Parameter.ConstantCount) {
      Command->InlineBuffers[Binding] = true;
      Command->InlineOffsets[Binding] = Parameter.ConstantOffset;
      Buffer.Data = Command->RootConstants.data() + Parameter.ConstantOffset;
      Buffer.SizeInBytes = UINT64(Parameter.ConstantCount) * sizeof(UINT);
    } else {
      Result = feme::probe::getWarpBufferAddress(
          State->Owner, State->RootAddresses[I], Buffer.Data,
          Buffer.SizeInBytes, Parameter.Namespace == 2);
      if (FAILED(Result))
        return Result;
    }
    Buffer.Namespace = Parameter.Namespace;
    Buffer.Space = Parameter.Space;
    Buffer.Register = Parameter.Register;
  }
  return S_OK;
}

void APIENTRY dispatch(D3D12DDI_HCOMMANDLIST List, UINT X, UINT Y, UINT Z) {
  auto *State = recordingList(List);
  if (!State)
    return;
  std::unique_ptr<RecordedCommand> Command;
  HRESULT Result = X > 65535 || Y > 65535 || Z > 65535
                       ? E_INVALIDARG
                       : prepareDispatch(State, Command);
  if (FAILED(Result)) {
    reportObjectError(List, Result);
    return;
  }
  Command->Dispatch.GroupCount[0] = X;
  Command->Dispatch.GroupCount[1] = Y;
  Command->Dispatch.GroupCount[2] = Z;
  State->append(Command.release());
}

void APIENTRY executeIndirect(D3D12DDI_HCOMMANDLIST List,
                              D3D12DDI_HCOMMANDSIGNATURE Signature,
                              UINT MaxCommandCount,
                              D3D12DDIARG_BUFFER_PLACEMENT ArgumentBuffer,
                              D3D12DDIARG_BUFFER_PLACEMENT CountBuffer) {
  auto *State = recordingList(List);
  if (!State)
    return;
  UINT ByteStride = 0;
  {
    CommandSignatureAccess Access;
    auto *Object = *findCommandSignature(Signature);
    if (Object && Object->Owner == State->Owner)
      ByteStride = Object->ByteStride;
  }
  if (!ByteStride || ArgumentBuffer.BaseAddress.UMD.Offset % sizeof(UINT) ||
      CountBuffer.BaseAddress.UMD.Offset % sizeof(UINT) ||
      (!CountBuffer.BaseAddress.UMD.hResource.pDrvPrivate &&
       CountBuffer.BaseAddress.UMD.Offset)) {
    reportObjectError(List, E_INVALIDARG);
    return;
  }
  void *Data = nullptr;
  HRESULT Result = feme::probe::getWarpBufferRange(
      State->Owner, ArgumentBuffer.BaseAddress.UMD.hResource,
      ArgumentBuffer.BaseAddress.UMD.Offset,
      UINT64(MaxCommandCount) * ByteStride, Data);
  if (SUCCEEDED(Result) && CountBuffer.BaseAddress.UMD.hResource.pDrvPrivate)
    Result = feme::probe::getWarpBufferRange(
        State->Owner, CountBuffer.BaseAddress.UMD.hResource,
        CountBuffer.BaseAddress.UMD.Offset, sizeof(UINT), Data);
  std::unique_ptr<RecordedCommand> Command;
  if (SUCCEEDED(Result))
    Result = prepareDispatch(State, Command);
  if (FAILED(Result)) {
    reportObjectError(List, Result);
    return;
  }
  Command->Type = RecordedCommand::Kind::IndirectDispatch;
  Command->ArgumentBuffer = ArgumentBuffer;
  Command->CountBuffer = CountBuffer;
  Command->MaxCommandCount = MaxCommandCount;
  Command->ByteStride = ByteStride;
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
  Command->Predicate = State->Predicate;
  State->append(Command);
}

struct PitchedCopyLayout {
  DXGI_FORMAT Format;
  UINT Width, Height, Depth, RowPitch, SlicePitch;
};

HRESULT normalizeCopyLayout(D3D12DDIARG_PLACED_RESOURCE Placed,
                            PitchedCopyLayout &Layout,
                            feme::probe::WarpBufferAccess &Access,
                            D3D12DDIARG_BUFFER_PLACEMENT &Placement) {
  D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT Selected{};
  if (Placed.Layout == D3D12DDI_RL_SELECT_SUBRESOURCE) {
    // This placement carries a subresource index rather than a byte offset.
    if (Placement.BaseAddress.UMD.Offset > std::numeric_limits<UINT>::max())
      return E_INVALIDARG;
    D3D12DDI_SUBRESOURCE_INFO Info{};
    HRESULT Result = Access.subresource(
        Placement.BaseAddress.UMD.hResource,
        static_cast<UINT>(Placement.BaseAddress.UMD.Offset), Info, &Selected);
    if (FAILED(Result))
      return Result;
    Placement.BaseAddress.UMD.Offset = Info.Offset;
    Placed = {D3D12DDI_RL_PLACED_PHYSICAL_SUBRESOURCE_PITCHED, &Selected};
  }
  if (!Placed.pLayout)
    return E_INVALIDARG;
  if (Placed.Layout == D3D12DDI_RL_PLACED_PHYSICAL_SUBRESOURCE_PITCHED) {
    const auto &Input =
        *static_cast<const D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT *>(
            Placed.pLayout);
    Layout = {Input.Format,        Input.PhysicalWidth, Input.PhysicalHeight,
              Input.PhysicalDepth, Input.Pitch,         Input.SlicePitch};
  } else if (Placed.Layout == D3D12DDI_RL_PLACED_VIRTUAL_SUBRESOURCE_PITCHED) {
    const auto &Input =
        *static_cast<const D3D12DDIARG_VIRTUAL_SUBRESOURCE_PITCHED_LAYOUT *>(
            Placed.pLayout);
    if (Input.VirtualWidth != Input.PhysicalWidth ||
        Input.VirtualHeight != Input.PhysicalHeight ||
        Input.VirtualDepth != Input.PhysicalDepth)
      return E_NOTIMPL;
    Layout = {Input.Format,       Input.VirtualWidth, Input.VirtualHeight,
              Input.VirtualDepth, Input.Pitch,        Input.SlicePitch};
  } else
    return E_NOTIMPL;
  UINT ElementSize = warpFormatElementSize(Layout.Format);
  if (!ElementSize)
    return E_NOTIMPL;
  if (!Layout.Width || !Layout.Height || !Layout.Depth ||
      Layout.Width > static_cast<UINT>(std::numeric_limits<LONG>::max()) ||
      Layout.Height > static_cast<UINT>(std::numeric_limits<LONG>::max()) ||
      Layout.Depth > static_cast<UINT>(std::numeric_limits<LONG>::max()) ||
      UINT64(Layout.Width) * ElementSize > Layout.RowPitch ||
      UINT64(Layout.Height) * Layout.RowPitch > Layout.SlicePitch)
    return E_INVALIDARG;
  return S_OK;
}

void APIENTRY copyTextureRegion(D3D12DDI_HCOMMANDLIST List,
                                const D3D12DDIARG_BUFFER_PLACEMENT *Destination,
                                D3D12DDIARG_PLACED_RESOURCE DestinationLayout,
                                UINT X, UINT Y, UINT Z,
                                const D3D12DDIARG_BUFFER_PLACEMENT *Source,
                                D3D12DDIARG_PLACED_RESOURCE SourceLayout,
                                const D3D12DDI_BOX *Box) {
  auto *State = recordingList(List);
  if (!State)
    return;
  feme::probe::WarpBufferAccess Access(State->Owner);
  D3D12DDIARG_BUFFER_PLACEMENT SrcPlacement =
      Source ? *Source : D3D12DDIARG_BUFFER_PLACEMENT{};
  D3D12DDIARG_BUFFER_PLACEMENT DstPlacement =
      Destination ? *Destination : D3D12DDIARG_BUFFER_PLACEMENT{};
  PitchedCopyLayout Src{}, Dst{};
  HRESULT Result =
      Destination && Source
          ? normalizeCopyLayout(SourceLayout, Src, Access, SrcPlacement)
          : E_INVALIDARG;
  if (SUCCEEDED(Result))
    Result = normalizeCopyLayout(DestinationLayout, Dst, Access, DstPlacement);
  if (FAILED(Result)) {
    reportObjectError(List, Result);
    return;
  }
  if (Src.Format != Dst.Format) {
    reportObjectError(List, E_NOTIMPL);
    return;
  }
  D3D12DDI_BOX Region = Box ? *Box
                            : D3D12DDI_BOX{0,
                                           0,
                                           0,
                                           static_cast<LONG>(Src.Width),
                                           static_cast<LONG>(Src.Height),
                                           static_cast<LONG>(Src.Depth)};
  if (Region.Left < 0 || Region.Top < 0 || Region.Front < 0 ||
      Region.Left > Region.Right || Region.Top > Region.Bottom ||
      Region.Front > Region.Back ||
      static_cast<UINT>(Region.Right) > Src.Width ||
      static_cast<UINT>(Region.Bottom) > Src.Height ||
      static_cast<UINT>(Region.Back) > Src.Depth || X > Dst.Width ||
      Y > Dst.Height || Z > Dst.Depth ||
      static_cast<UINT>(Region.Right - Region.Left) > Dst.Width - X ||
      static_cast<UINT>(Region.Bottom - Region.Top) > Dst.Height - Y ||
      static_cast<UINT>(Region.Back - Region.Front) > Dst.Depth - Z) {
    reportObjectError(List, E_INVALIDARG);
    return;
  }
  auto Walk = feme::image::computeImageCopyRegionWalk(
      1, 1, 1, 1, warpFormatElementSize(Src.Format), 1, true, true,
      static_cast<UINT>(Region.Right - Region.Left),
      static_cast<UINT>(Region.Bottom - Region.Top),
      static_cast<UINT>(Region.Back - Region.Front),
      static_cast<UINT>(Region.Left), static_cast<UINT>(Region.Top), X, Y, 1);
  void *Data = nullptr;
  Result = Access.resourceRange(SrcPlacement.BaseAddress.UMD.hResource,
                                SrcPlacement.BaseAddress.UMD.Offset,
                                UINT64(Src.Depth - 1) * Src.SlicePitch +
                                    UINT64(Src.Height - 1) * Src.RowPitch +
                                    UINT64(Src.Width) *
                                        warpFormatElementSize(Src.Format),
                                Data);
  if (SUCCEEDED(Result))
    Result = Access.resourceRange(DstPlacement.BaseAddress.UMD.hResource,
                                  DstPlacement.BaseAddress.UMD.Offset,
                                  UINT64(Dst.Depth - 1) * Dst.SlicePitch +
                                      UINT64(Dst.Height - 1) * Dst.RowPitch +
                                      UINT64(Dst.Width) *
                                          warpFormatElementSize(Dst.Format),
                                  Data);
  if (FAILED(Result)) {
    reportObjectError(List, Result);
    return;
  }
  if (!Walk.WidthUnits || !Walk.HeightUnits || !Walk.SliceCount)
    return;
  UINT64 SrcOffset =
      UINT64(Region.Front) * Src.SlicePitch +
      UINT64(Walk.SrcOffsetYUnits) * Src.RowPitch +
      UINT64(Walk.SrcOffsetXUnits) * warpFormatElementSize(Src.Format);
  UINT64 DstOffset =
      UINT64(Z) * Dst.SlicePitch + UINT64(Walk.DstOffsetYUnits) * Dst.RowPitch +
      UINT64(Walk.DstOffsetXUnits) * warpFormatElementSize(Dst.Format);
  UINT64 SrcBytes = UINT64(Walk.SliceCount - 1) * Src.SlicePitch +
                    UINT64(Walk.HeightUnits - 1) * Src.RowPitch + Walk.RowBytes;
  UINT64 DstBytes = UINT64(Walk.SliceCount - 1) * Dst.SlicePitch +
                    UINT64(Walk.HeightUnits - 1) * Dst.RowPitch + Walk.RowBytes;
  if (SrcOffset > std::numeric_limits<UINT64>::max() -
                      SrcPlacement.BaseAddress.UMD.Offset ||
      DstOffset > std::numeric_limits<UINT64>::max() -
                      DstPlacement.BaseAddress.UMD.Offset)
    Result = E_INVALIDARG;
  else {
    SrcOffset += SrcPlacement.BaseAddress.UMD.Offset;
    DstOffset += DstPlacement.BaseAddress.UMD.Offset;
    Result = Access.resourceRange(SrcPlacement.BaseAddress.UMD.hResource,
                                  SrcOffset, SrcBytes, Data);
    if (SUCCEEDED(Result))
      Result = Access.resourceRange(DstPlacement.BaseAddress.UMD.hResource,
                                    DstOffset, DstBytes, Data);
  }
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
  Command->Source = SrcPlacement;
  Command->Destination = DstPlacement;
  Command->Source.BaseAddress.UMD.Offset = SrcOffset;
  Command->Destination.BaseAddress.UMD.Offset = DstOffset;
  Command->Bytes = Walk.RowBytes;
  Command->CopyRows = Walk.HeightUnits;
  Command->CopySlices = Walk.SliceCount;
  Command->SourceRowPitch = Src.RowPitch;
  Command->DestinationRowPitch = Dst.RowPitch;
  Command->SourceSlicePitch = Src.SlicePitch;
  Command->DestinationSlicePitch = Dst.SlicePitch;
  Command->Predicate = State->Predicate;
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
    bool Aliasing =
        Barrier.Type == D3D12DDI_RESOURCE_BARRIER_TYPE_0022_RANGED &&
        (Barrier.Flags & D3D12DDI_RESOURCE_BARRIER_FLAG_0022_ALIASING);
    if (!Aliasing && Barrier.Flags != D3D12DDI_RESOURCE_BARRIER_FLAG_NONE) {
      reportObjectError(List, E_NOTIMPL);
      return;
    }
    if (Barrier.Type == D3D12DDI_RESOURCE_BARRIER_TYPE_TRANSITION) {
      if (!Barrier.Transition.hResource.pDrvPrivate) {
        reportObjectError(List, E_INVALIDARG);
        return;
      }
      Resource = Barrier.Transition.hResource;
      feme::probe::WarpBufferAccess Access(State->Owner);
      D3D12DDI_SUBRESOURCE_INFO Info{};
      HRESULT Result = Access.subresource(
          Resource,
          Barrier.Transition.Subresource == std::numeric_limits<UINT>::max()
              ? 0
              : Barrier.Transition.Subresource,
          Info);
      if (FAILED(Result)) {
        reportObjectError(List, Result);
        return;
      }
    } else if (Barrier.Type == D3D12DDI_RESOURCE_BARRIER_TYPE_UAV)
      Resource = Barrier.UAV.hResource;
    else if (Aliasing) {
      auto Flags =
          Barrier.Flags & ~D3D12DDI_RESOURCE_BARRIER_FLAG_0022_ALIASING;
      if ((Flags != D3D12DDI_RESOURCE_BARRIER_FLAG_NONE &&
           Flags != D3D12DDI_RESOURCE_BARRIER_FLAG_BEGIN_ONLY &&
           Flags != D3D12DDI_RESOURCE_BARRIER_FLAG_END_ONLY) ||
          (Barrier.Ranged.Subresource != 0 &&
           Barrier.Ranged.Subresource != std::numeric_limits<UINT>::max()) ||
          Barrier.Ranged.Range.Begin ||
          Barrier.Ranged.Range.End != std::numeric_limits<UINT64>::max()) {
        reportObjectError(List, E_INVALIDARG);
        return;
      }
      Resource = Barrier.Ranged.hResource;
    } else {
      reportObjectError(List, E_NOTIMPL);
      return;
    }
    void *Data = nullptr;
    if (Resource.pDrvPrivate) {
      feme::probe::WarpBufferAccess Access(State->Owner);
      HRESULT Result = Access.resourceRange(Resource, 0, 0, Data);
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
  if (auto *State = recordingList(List)) {
    State->RootAddresses.fill(0);
    State->RootConstants.fill(0);
    State->RootTables.fill(0);
  }
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
                                 D3D12DDI_HDESCRIPTORHEAP *Heaps) {
  auto *State = recordingList(List);
  if (!State)
    return;
  if (Count > 1 || (Count && !Heaps)) {
    reportObjectError(List, E_INVALIDARG);
    return;
  }
  D3D12DDI_HDESCRIPTORHEAP Selected{};
  if (Count) {
    DescriptorAccess Access;
    auto *Heap = *findDescriptorHeap(Heaps[0]);
    if (!Heap || Heap->Owner != State->Owner ||
        !(Heap->Desc.Flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)) {
      reportObjectError(List, E_INVALIDARG);
      return;
    }
    Selected = Heaps[0];
  }
  if (State->DescriptorHeap.pDrvPrivate != Selected.pDrvPrivate)
    State->RootTables.fill(0);
  State->DescriptorHeap = Selected;
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
  auto *State = recordingList(List);
  if (!State)
    return;
  if (!(State->Flags & D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE) ||
      (Operation != D3D12DDI_PREDICATION_OP_EQUAL_ZERO &&
       Operation != D3D12DDI_PREDICATION_OP_NOT_EQUAL_ZERO)) {
    reportObjectError(List, E_INVALIDARG);
    return;
  }
  if (!Resource.pDrvPrivate) {
    State->Predicate = {};
    return;
  }
  void *Data = nullptr;
  HRESULT Result =
      Offset % sizeof(UINT64)
          ? E_INVALIDARG
          : feme::probe::getWarpBufferRange(State->Owner, Resource, Offset,
                                            sizeof(UINT64), Data);
  if (FAILED(Result)) {
    reportObjectError(List, Result);
    return;
  }
  State->Predicate = {Resource, Offset, Operation};
}

HRESULT resolveDispatchBindings(CommandQueue *State, RecordedCommand *Command,
                                feme::probe::WarpBufferAccess &Buffers) {
  DescriptorAccess Descriptors;
  for (UINT J = 0; J < Command->Dispatch.BufferCount; ++J) {
    auto &Buffer = Command->Buffers[J];
    if (Command->InlineBuffers[J])
      continue;
    if (Command->TableDescriptors[J]) {
      auto *Heap = *findDescriptorHeap(Command->DescriptorHeap);
      if (!Heap || Heap->Owner != State->Owner)
        return E_INVALIDARG;
      auto *Slot = descriptorTableSlot(Heap, Command->TableDescriptors[J], 0);
      if (!Slot || Slot->Namespace != Buffer.Namespace)
        return E_INVALIDARG;
      Buffer.Data = nullptr;
      Buffer.SizeInBytes = 0;
      Buffer.CounterData = nullptr;
      Buffer.CounterSizeInBytes = 0;
      Buffer.Kind = Slot->Format != DXGI_FORMAT_UNKNOWN ? 2
                    : Slot->Stride                      ? 1
                                                        : 0;
      Buffer.Stride = Slot->Stride;
      Buffer.Format = Slot->Format;
      Buffer.Shader4ComponentMapping = Slot->Shader4ComponentMapping;
      if (Slot->Namespace == 3) {
        if (!Slot->Address)
          continue;
        UINT64 Extent = 0;
        HRESULT Result = Buffers.address(Slot->Address, Buffer.Data, Extent);
        if (FAILED(Result) || Slot->ByteSize > Extent)
          return E_INVALIDARG;
      } else {
        if (!Slot->Resource.pDrvPrivate)
          continue;
        HRESULT Result =
            Buffers.range(Slot->Resource, Slot->ByteOffset, Slot->ByteSize,
                          Buffer.Data, Slot->Namespace == 2);
        if (FAILED(Result))
          return Result;
      }
      Buffer.SizeInBytes = Slot->ByteSize;
      if (Slot->CounterResource.pDrvPrivate) {
        HRESULT Result =
            Buffers.range(Slot->CounterResource, Slot->CounterOffset,
                          sizeof(UINT), Buffer.CounterData, true);
        if (FAILED(Result))
          return Result;
        Buffer.CounterSizeInBytes = sizeof(UINT);
      }
      continue;
    }
    HRESULT Result =
        Buffers.address(reinterpret_cast<UINT64>(Buffer.Data), Buffer.Data,
                        Buffer.SizeInBytes, Buffer.Namespace == 2);
    if (FAILED(Result))
      return Result;
  }
  return S_OK;
}

HRESULT executeSubmission(CommandQueue *State, PendingSubmission *Submission) {
  HRESULT Result = S_OK;
  for (auto *Command = Submission->First; Command && SUCCEEDED(Result);
       Command = Command->Next) {
    feme::probe::WarpBufferAccess Access(State->Owner);
    if (Command->Type == RecordedCommand::Kind::Barrier) {
      void *Data = nullptr;
      if (Command->BarrierResource.pDrvPrivate)
        Result = Access.resourceRange(Command->BarrierResource, 0, 0, Data);
      if (SUCCEEDED(Result))
        std::atomic_thread_fence(std::memory_order_seq_cst);
      continue;
    }
    if (Command->Predicate.Resource.pDrvPrivate) {
      void *Data = nullptr;
      Result = Access.range(Command->Predicate.Resource,
                            Command->Predicate.Offset, sizeof(UINT64), Data);
      if (FAILED(Result))
        continue;
      UINT64 Value = 0;
      std::memcpy(&Value, Data, sizeof(Value));
      bool Skip =
          Command->Predicate.Operation == D3D12DDI_PREDICATION_OP_EQUAL_ZERO
              ? Value == 0
              : Value != 0;
      if (Skip)
        continue;
    }
    if (Command->Type == RecordedCommand::Kind::Dispatch ||
        Command->Type == RecordedCommand::Kind::IndirectDispatch) {
      Result = resolveDispatchBindings(State, Command, Access);
      UINT Count = 1;
      void *Arguments = nullptr;
      if (SUCCEEDED(Result) &&
          Command->Type == RecordedCommand::Kind::IndirectDispatch) {
        Count = Command->MaxCommandCount;
        Result = Access.range(Command->ArgumentBuffer.BaseAddress.UMD.hResource,
                              Command->ArgumentBuffer.BaseAddress.UMD.Offset,
                              UINT64(Count) * Command->ByteStride, Arguments);
        if (SUCCEEDED(Result) &&
            Command->CountBuffer.BaseAddress.UMD.hResource.pDrvPrivate) {
          void *Data = nullptr;
          Result = Access.range(Command->CountBuffer.BaseAddress.UMD.hResource,
                                Command->CountBuffer.BaseAddress.UMD.Offset,
                                sizeof(UINT), Data);
          if (SUCCEEDED(Result)) {
            UINT Requested = 0;
            std::memcpy(&Requested, Data, sizeof(Requested));
            if (Requested < Count)
              Count = Requested;
          }
        }
      }
      for (UINT I = 0; I < Count && SUCCEEDED(Result); ++I) {
        if (Arguments) {
          std::memcpy(Command->Dispatch.GroupCount,
                      static_cast<const char *>(Arguments) +
                          UINT64(I) * Command->ByteStride,
                      sizeof(Command->Dispatch.GroupCount));
          for (UINT Dimension : Command->Dispatch.GroupCount)
            if (Dimension > 65535)
              Result = E_INVALIDARG;
        }
        if (SUCCEEDED(Result))
          Result =
              FeMeWarpDispatchCompute(Command->Pipeline, &Command->Dispatch);
        if (SUCCEEDED(Result))
          ++DispatchesExecuted;
      }
      continue;
    }
    void *Destination = nullptr, *Source = nullptr;
    Result = Access.resourceRange(
        Command->Destination.BaseAddress.UMD.hResource,
        Command->Destination.BaseAddress.UMD.Offset,
        UINT64(Command->CopySlices - 1) * Command->DestinationSlicePitch +
            UINT64(Command->CopyRows - 1) * Command->DestinationRowPitch +
            Command->Bytes,
        Destination);
    if (SUCCEEDED(Result))
      Result = Access.resourceRange(
          Command->Source.BaseAddress.UMD.hResource,
          Command->Source.BaseAddress.UMD.Offset,
          UINT64(Command->CopySlices - 1) * Command->SourceSlicePitch +
              UINT64(Command->CopyRows - 1) * Command->SourceRowPitch +
              Command->Bytes,
          Source);
    if (SUCCEEDED(Result)) {
      for (UINT Slice = 0; Slice < Command->CopySlices; ++Slice)
        for (UINT Row = 0; Row < Command->CopyRows; ++Row)
          std::memmove(static_cast<unsigned char *>(Destination) +
                           UINT64(Slice) * Command->DestinationSlicePitch +
                           UINT64(Row) * Command->DestinationRowPitch,
                       static_cast<const unsigned char *>(Source) +
                           UINT64(Slice) * Command->SourceSlicePitch +
                           UINT64(Row) * Command->SourceRowPitch,
                       static_cast<SIZE_T>(Command->Bytes));
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
      for (UINT J = 0; J < Copy->Dispatch.BufferCount; ++J)
        if (Copy->InlineBuffers[J])
          Copy->Buffers[J].Data =
              Copy->RootConstants.data() + Copy->InlineOffsets[J];
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
    Table.pfnCalcPrivateDescriptorHeapSize = descriptorHeapSize;
    Table.pfnCreateDescriptorHeap = createDescriptorHeap;
    Table.pfnDestroyDescriptorHeap = destroyDescriptorHeap;
    Table.pfnGetCPUDescriptorHandleForHeapStart = descriptorCPUStart;
    Table.pfnGetGPUDescriptorHandleForHeapStart = descriptorGPUStart;
    Table.pfnCreateConstantBufferView = createCBV;
    Table.pfnCreateShaderResourceView = createSRV;
    Table.pfnCreateUnorderedAccessView = createUAV;
    Table.pfnCopyDescriptors = copyDescriptors;
    Table.pfnCopyDescriptorsSimple = copyDescriptorsSimple;
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
    Table.pfnCalcPrivateCommandSignatureSize = commandSignatureSize;
    Table.pfnCreateCommandSignature = createCommandSignature;
    Table.pfnDestroyCommandSignature = destroyCommandSignature;
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
    Table.pfnSetComputeRootConstantBufferView = setRootCBV;
    Table.pfnSetComputeRoot32BitConstant = setRootConstant;
    Table.pfnSetComputeRoot32BitConstants = setRootConstants;
    Table.pfnSetComputeRootDescriptorTable = setRootTable;
    Table.pfnDispatch = dispatch;
    Table.pfnExecuteIndirect = executeIndirect;
    Table.pfnCopyBufferRegion = copyBuffer;
    Table.pfnCopyTextureRegion = copyTextureRegion;
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
