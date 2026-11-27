//===- WarpTablesTests.cpp - Diagnostic table contracts
//--------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "WarpTables.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
HRESULT LastError = S_OK;
UINT ErrorCalls = 0;
void APIENTRY setError(D3D12DDI_HRTDEVICE, HRESULT Result) {
  LastError = Result;
  ++ErrorCalls;
}
} // namespace

int main() {
  D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 Callbacks{};
  Callbacks.pfnSetErrorCb = setError;
  feme::probe::WarpDeviceState State;
  State.UMCallbacks = &Callbacks;
  State.RTDevice = &State;
  D3D12DDI_HDEVICE Device{};
  Device.pDrvPrivate = &State;
  D3D12DDI_DEVICE_FUNCS_CORE_0072 Core{};
  bool Valid =
      feme::probe::fillWarpPrototypeTable(D3D12DDI_TABLE_TYPE_DEVICE_CORE,
                                          &Core, sizeof(Core), 0) == S_OK;
  constexpr UINT CoreEntries = 0
#define WARP_CORE_DDI(Name) +1
#define WARP_LIST_DDI(Name)
#include "WarpTableEntries.def"
#undef WARP_CORE_DDI
#undef WARP_LIST_DDI
      ;
  static_assert(sizeof(Core) == CoreEntries * sizeof(void *));
#define WARP_CORE_DDI(Name) Valid &= Core.Name != nullptr;
#define WARP_LIST_DDI(Name)
#include "WarpTableEntries.def"
#undef WARP_CORE_DDI
#undef WARP_LIST_DDI

  UINT Node = 99;
  Core.pfnQueryNodeMap(Device, 1, &Node);
  Valid &= Node == 0 && ErrorCalls == 0;
  Core.pfnQueryNodeMap(Device, 2, &Node);
  Valid &= Node == 0 && ErrorCalls == 1 && LastError == E_INVALIDARG;
  Core.pfnQueryNodeMap(Device, 1, nullptr);
  Valid &= ErrorCalls == 2 && LastError == E_INVALIDARG;
  Valid &= Core.pfnGetImplicitPhysicalAdapterMask(Device) == 0;

  // Rejection is observable even for private-size and void callbacks.
  D3D12DDIARG_CREATECOMMANDQUEUE_0050 QueueArgs{};
  Valid &= Core.pfnCalcPrivateCommandQueueSize(Device, &QueueArgs) == 0 &&
           ErrorCalls == 3 && LastError == E_NOTIMPL;
  unsigned char Private[64];
  std::memset(Private, 0x5a, sizeof(Private));
  D3D12DDI_HCOMMANDQUEUE Queue{};
  Queue.pDrvPrivate = Private;
  Valid &=
      Core.pfnCreateCommandQueue(Device, &QueueArgs, Queue, {}) == E_NOTIMPL &&
      ErrorCalls == 4;
  Core.pfnDestroyCommandQueue(Device, Queue);
  Valid &= ErrorCalls == 5 && LastError == E_NOTIMPL;
  for (unsigned char Byte : Private)
    Valid &= Byte == 0x5a;

  UINT Support = 0xffffffff;
  UINT Quality = 0xffffffff;
  Core.pfnCheckFormatSupport(Device, DXGI_FORMAT_R8G8B8A8_UNORM, &Support);
  Core.pfnCheckMultisampleQualityLevels(
      Device, DXGI_FORMAT_R8G8B8A8_UNORM, 4,
      static_cast<D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAGS>(0), &Quality);
  Valid &= Support == 0 && Quality == 0 && ErrorCalls == 5;
  Core.pfnCheckFormatSupport(Device, DXGI_FORMAT_UNKNOWN, nullptr);
  Valid &= ErrorCalls == 6 && LastError == E_INVALIDARG;

  D3D12DDI_ROOT_SIGNATURE_0013 RootDesc{};
  D3D12DDIARG_CREATE_ROOT_SIGNATURE_0013 RootArgs{};
  RootArgs.Version = D3D12DDI_ROOT_SIGNATURE_VERSION_1_1;
  RootArgs.pRootSignature_1_1 = &RootDesc;
  RootArgs.NodeMask = 1;
  alignas(MEMORY_ALLOCATION_ALIGNMENT) unsigned char RootStorage[64];
  D3D12DDI_HROOTSIGNATURE Root{};
  Root.pDrvPrivate = RootStorage;
  SIZE_T RootSize = Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs);
  Valid &= RootSize > 0 && RootSize <= sizeof(RootStorage);
  Valid &= Core.pfnCreateRootSignature(Device, &RootArgs, Root) == S_OK &&
           State.LiveRootSignatures == 1;
  feme::probe::WarpDeviceState OtherState;
  D3D12DDI_HDEVICE OtherDevice{};
  OtherDevice.pDrvPrivate = &OtherState;
  Core.pfnDestroyRootSignature(OtherDevice, Root);
  Valid &= State.LiveRootSignatures == 1;
  Core.pfnDestroyRootSignature(Device, Root);
  Valid &= State.LiveRootSignatures == 0;
  std::memset(RootStorage, 0x5a, sizeof(RootStorage));
  RootDesc.NumParameters = 1;
  Valid &= Core.pfnCreateRootSignature(Device, &RootArgs, Root) == E_NOTIMPL &&
           State.LiveRootSignatures == 0;
  for (unsigned char Byte : RootStorage)
    Valid &= Byte == 0x5a;

  D3D12DDIARG_CREATE_SHADER_0026 ShaderArgs{};
  D3D12DDI_HSHADER Shader{};
  alignas(MEMORY_ALLOCATION_ALIGNMENT) unsigned char ShaderStorage[64];
  Shader.pDrvPrivate = ShaderStorage;
  SIZE_T ShaderSize = Core.pfnCalcPrivateShaderSize(Device, &ShaderArgs);
  Valid &= ShaderSize > 0 && ShaderSize <= sizeof(ShaderStorage);
  Core.pfnCreateVertexShader(Device, &ShaderArgs, Shader);
  Valid &= State.LiveNullShaders == 1;
  Core.pfnDestroyShader(Device, Shader);
  Valid &= State.LiveNullShaders == 0;
  std::memset(ShaderStorage, 0x5a, sizeof(ShaderStorage));
  const UINT Executable[] = {0, 2};
  ShaderArgs.pShaderCode = Executable;
  UINT PreviousErrors = ErrorCalls;
  Valid &= Core.pfnCalcPrivateShaderSize(Device, &ShaderArgs) == 0 &&
           ErrorCalls == PreviousErrors + 1 && LastError == E_NOTIMPL;
  Core.pfnCreateComputeShader(Device, &ShaderArgs, Shader);
  Valid &= State.LiveNullShaders == 0 && ErrorCalls == PreviousErrors + 2 &&
           LastError == E_NOTIMPL;
  for (unsigned char Byte : ShaderStorage)
    Valid &= Byte == 0x5a;
  ShaderArgs.pShaderCode = nullptr;
  ShaderArgs.LibraryReference.hLibrary.pDrvPrivate = Private;
  Valid &= Core.pfnCalcPrivateShaderSize(Device, &ShaderArgs) == 0 &&
           LastError == E_NOTIMPL;
  D3D12DDI_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER_0054 Identifier{};
  Valid &=
      Core.pfnCheckDriverMatchingIdentifier(
          Device, static_cast<D3D12DDI_SERIALIZED_DATA_TYPE>(0), &Identifier) ==
          D3D12DDI_DRIVER_MATCHING_IDENTIFIER_UNSUPPORTED_TYPE &&
      LastError == E_NOTIMPL;

  auto CheckImmutable = [&](auto Args, auto Size, auto Create, auto Destroy,
                            auto Handle) {
    alignas(MEMORY_ALLOCATION_ALIGNMENT) unsigned char Storage[512];
    Handle.pDrvPrivate = Storage;
    SIZE_T Required = Size(Device, &Args);
    if (!Required || Required > sizeof(Storage))
      return false;
    Create(Device, &Args, Handle);
    bool Good = State.LiveImmutableStates == 1;
    Destroy(OtherDevice, Handle);
    Good &= State.LiveImmutableStates == 1;
    Destroy(Device, Handle);
    Good &= State.LiveImmutableStates == 0;
    std::memset(Storage, 0x5a, sizeof(Storage));
    Args.LibraryReference.hLibrary.pDrvPrivate = Private;
    UINT Before = ErrorCalls;
    Good &= Size(Device, &Args) == 0 && ErrorCalls == Before + 1 &&
            LastError == E_NOTIMPL;
    Create(Device, &Args, Handle);
    Good &= State.LiveImmutableStates == 0 && ErrorCalls == Before + 2;
    for (unsigned char Byte : Storage)
      Good &= Byte == 0x5a;
    Good &= Size(Device, nullptr) == 0 && LastError == E_INVALIDARG;
    return Good;
  };
  Valid &= CheckImmutable(D3D12DDI_BLEND_DESC_0010{},
                          Core.pfnCalcPrivateBlendStateSize,
                          Core.pfnCreateBlendState, Core.pfnDestroyBlendState,
                          D3D12DDI_HBLENDSTATE{});
  Valid &= CheckImmutable(
      D3D12DDI_DEPTH_STENCIL_DESC_0025{},
      Core.pfnCalcPrivateDepthStencilStateSize, Core.pfnCreateDepthStencilState,
      Core.pfnDestroyDepthStencilState, D3D12DDI_HDEPTHSTENCILSTATE{});
  Valid &= CheckImmutable(
      D3D12DDI_RASTERIZER_DESC_0010{}, Core.pfnCalcPrivateRasterizerStateSize,
      Core.pfnCreateRasterizerState, Core.pfnDestroyRasterizerState,
      D3D12DDI_HRASTERIZERSTATE{});

  const auto Original = Core;
  for (SIZE_T Size : {sizeof(Core) - 1, sizeof(Core) + 1}) {
    Valid &=
        feme::probe::fillWarpPrototypeTable(D3D12DDI_TABLE_TYPE_DEVICE_CORE,
                                            &Core, Size, 0) == E_INVALIDARG &&
        std::memcmp(&Core, &Original, sizeof(Core)) == 0;
  }
  Valid &= feme::probe::fillWarpPrototypeTable(D3D12DDI_TABLE_TYPE_DEVICE_CORE,
                                               nullptr, sizeof(Core),
                                               0) == E_INVALIDARG;
  Valid &= feme::probe::fillWarpPrototypeTable(D3D12DDI_TABLE_TYPE_DEVICE_CORE,
                                               &Core, sizeof(Core),
                                               1) == E_INVALIDARG &&
           std::memcmp(&Core, &Original, sizeof(Core)) == 0;

  D3D12DDI_COMMAND_LIST_FUNCS_3D_0062 List{};
  Valid &=
      feme::probe::fillWarpPrototypeTable(D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D,
                                          &List, sizeof(List), 0) == S_OK;
  constexpr UINT ListEntries = 0
#define WARP_CORE_DDI(Name)
#define WARP_LIST_DDI(Name) +1
#include "WarpTableEntries.def"
#undef WARP_CORE_DDI
#undef WARP_LIST_DDI
      ;
  static_assert(sizeof(List) == ListEntries * sizeof(void *));
#define WARP_CORE_DDI(Name)
#define WARP_LIST_DDI(Name) Valid &= List.Name != nullptr;
#include "WarpTableEntries.def"
#undef WARP_CORE_DDI
#undef WARP_LIST_DDI
  const auto OriginalList = List;
  Valid &= feme::probe::fillWarpPrototypeTable(
               D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D, &List, sizeof(List), 2) ==
               E_INVALIDARG &&
           std::memcmp(&List, &OriginalList, sizeof(List)) == 0;

  D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 QueueTable{};
  Valid &= feme::probe::fillWarpPrototypeTable(
               D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D, &QueueTable,
               sizeof(QueueTable), 0) == S_OK &&
           QueueTable.pfnExecuteCommandLists &&
           QueueTable.pfnUpdateTileMappings && QueueTable.pfnCopyTileMappings &&
           QueueTable.pfnSignalFence && QueueTable.pfnWaitForFence &&
           !QueueTable.pfnUnused && !QueueTable.pfnUnused2;
  D3D12DDI_EXTENDED_FEATURES_FUNCS_0021 Extended{};
  Valid &= feme::probe::fillWarpPrototypeTable(
               D3D12DDI_TABLE_TYPE_0020_EXTENDED_FEATURES, &Extended,
               sizeof(Extended), 0) == S_OK;
  UINT Count = 99;
  D3D12DDI_FEATURE_0020 Feature = D3D12DDI_FEATURE_0020_VIDEO;
  Valid &= Extended.pfnGetSupportedExtendedFeatures(Device, &Count, &Feature) ==
               S_OK &&
           Count == 0 && Feature == D3D12DDI_FEATURE_0020_VIDEO;
  Valid &= Extended.pfnGetSupportedExtendedFeatures(Device, nullptr, nullptr) ==
           E_INVALIDARG;
  Count = 99;
  Valid &= Extended.pfnGetSupportedExtendedFeatureVersions(
               Device, Feature, &Count, nullptr) == E_NOTIMPL &&
           Count == 0;
  Valid &= Extended.pfnEnableExtendedFeature(Device, Feature, 0) == E_NOTIMPL;
  std::printf("WARP diagnostic table contracts: %s\n", Valid ? "PASS" : "FAIL");
  return Valid ? 0 : 1;
}
