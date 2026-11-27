//===- WarpCompilerDDITests.cpp - Native shader DDI ownership tests
//---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "..\..\utils\windows-adapter-bootstrap\WarpTables.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
HRESULT LastError = S_OK;
void APIENTRY setError(D3D12DDI_HRTDEVICE, HRESULT Result) {
  LastError = Result;
}

struct Storage {
  std::vector<std::max_align_t> Words;
  explicit Storage(SIZE_T Size)
      : Words((Size + sizeof(std::max_align_t) - 1) /
              sizeof(std::max_align_t)) {}
  void *data() { return Words.data(); }
};
} // namespace

bool testWarpCompilerDDIs(const std::vector<std::uint32_t> &Tokens) {
  feme::probe::WarpDeviceState Owner, Other;
  D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 Callbacks{};
  Callbacks.pfnSetErrorCb = setError;
  Owner.UMCallbacks = Other.UMCallbacks = &Callbacks;
  Owner.RTDevice = &Owner;
  Other.RTDevice = &Other;
  D3D12DDI_HDEVICE Device{&Owner}, OtherDevice{&Other};
  D3D12DDI_DEVICE_FUNCS_CORE_0072 Core{};
  if (feme::probe::fillWarpPrototypeTable(D3D12DDI_TABLE_TYPE_DEVICE_CORE,
                                          &Core, sizeof(Core), 0) != S_OK)
    return false;
  D3D12DDI_ROOT_SIGNATURE_0013 RootDesc{};
  D3D12DDIARG_CREATE_ROOT_SIGNATURE_0013 RootArgs{};
  RootArgs.Version = D3D12DDI_ROOT_SIGNATURE_VERSION_1_1;
  RootArgs.pRootSignature_1_1 = &RootDesc;
  RootArgs.NodeMask = 1;
  Storage RootStorage(Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs));
  D3D12DDI_HROOTSIGNATURE Root{RootStorage.data()};
  if (Core.pfnCreateRootSignature(Device, &RootArgs, Root) != S_OK)
    return false;
  D3D12DDIARG_CREATE_SHADER_0026 ShaderArgs{};
  ShaderArgs.hRootSignature = Root;
  ShaderArgs.pShaderCode = Tokens.data();
  SIZE_T Size = Core.pfnCalcPrivateShaderSize(Device, &ShaderArgs);
  if (!Size)
    return false;
  Storage ShaderStorage(Size);
  D3D12DDI_HSHADER Shader{ShaderStorage.data()};
  bool Compute = (Tokens[0] >> 16) == 5;
  auto Create =
      Compute ? Core.pfnCreateComputeShader : Core.pfnCreateVertexShader;
  auto WrongStage =
      Compute ? Core.pfnCreateVertexShader : Core.pfnCreateComputeShader;
  std::memset(ShaderStorage.data(), 0x5a, Size);
  WrongStage(Device, &ShaderArgs, Shader);
  bool Good = LastError == E_INVALIDARG && Owner.LiveCompiledShaders == 0;
  for (SIZE_T I = 0; I < Size; ++I)
    Good &= static_cast<unsigned char *>(ShaderStorage.data())[I] == 0x5a;
  LastError = S_OK;
  Create(Device, &ShaderArgs, Shader);
  Good &= LastError == S_OK && Owner.LiveCompiledShaders == 1;
  if (!Good)
    return false;
  Core.pfnDestroyShader(OtherDevice, Shader);
  Good &= LastError == E_INVALIDARG && Owner.LiveCompiledShaders == 1;
  LastError = S_OK;

  D3D12DDI_BLEND_DESC_0010 Blend{};
  D3D12DDI_DEPTH_STENCIL_DESC_0025 Depth{};
  D3D12DDI_RASTERIZER_DESC_0010 Raster{};
  Raster.FillMode = D3D12DDI_FILL_MODE_SOLID;
  Raster.CullMode = D3D12DDI_CULL_MODE_NONE;
  Raster.DepthClipEnable = TRUE;
  Storage BlendStorage(Core.pfnCalcPrivateBlendStateSize(Device, &Blend));
  Storage DepthStorage(
      Core.pfnCalcPrivateDepthStencilStateSize(Device, &Depth));
  Storage RasterStorage(
      Core.pfnCalcPrivateRasterizerStateSize(Device, &Raster));
  D3D12DDI_HBLENDSTATE BlendHandle{BlendStorage.data()};
  D3D12DDI_HDEPTHSTENCILSTATE DepthHandle{DepthStorage.data()};
  D3D12DDI_HRASTERIZERSTATE RasterHandle{RasterStorage.data()};
  Core.pfnCreateBlendState(Device, &Blend, BlendHandle);
  Core.pfnCreateDepthStencilState(Device, &Depth, DepthHandle);
  Core.pfnCreateRasterizerState(Device, &Raster, RasterHandle);
  // Stack descriptors are not retained by the immutable DDI objects.
  Raster.CullMode = static_cast<D3D12DDI_CULL_MODE>(99);
  D3D12DDIARG_CREATE_PIPELINE_STATE_0033 PipelineArgs{};
  PipelineArgs.NodeMask = 1;
  PipelineArgs.SampleDesc.Count = 1;
  PipelineArgs.hRootSignature = Root;
  if (Compute)
    PipelineArgs.hComputeShader = Shader;
  else {
    PipelineArgs.hVertexShader = Shader;
    PipelineArgs.hBlendState = BlendHandle;
    PipelineArgs.hDepthStencilState = DepthHandle;
    PipelineArgs.hRasterizerState = RasterHandle;
    PipelineArgs.PrimitiveTopologyType = D3D12DDI_PRIMITIVE_TOPOLOGY_TYPE_POINT;
  }
  SIZE_T PipelineSize =
      Core.pfnCalcPrivatePipelineStateSize(Device, &PipelineArgs);
  if (!PipelineSize)
    return false;
  Storage PipelineStorage(PipelineSize);
  D3D12DDI_HPIPELINESTATE Pipeline{PipelineStorage.data()};
  Good &= Core.pfnCreatePipelineState(Device, &PipelineArgs, Pipeline, {}) ==
              S_OK &&
          Owner.LivePipelines == 1;
  if (!Good)
    return false;
  Core.pfnDestroyShader(Device, Shader);
  Core.pfnDestroyPipelineState(OtherDevice, Pipeline);
  Good &= LastError == E_INVALIDARG && Owner.LivePipelines == 1;
  Core.pfnDestroyPipelineState(Device, Pipeline);
  Core.pfnDestroyBlendState(Device, BlendHandle);
  Core.pfnDestroyDepthStencilState(Device, DepthHandle);
  Core.pfnDestroyRasterizerState(Device, RasterHandle);
  Core.pfnDestroyRootSignature(Device, Root);
  Good &= Owner.LivePipelines == 0 && Owner.LiveCompiledShaders == 0 &&
          Owner.LiveRootSignatures == 0 && Owner.LiveImmutableStates == 0;

  D3D12DDIARG_CREATE_COMMAND_POOL_0040 PoolArgs{};
  Storage PoolStorage(Core.pfnCalcPrivateCommandPoolSize(Device, &PoolArgs));
  D3D12DDI_HCOMMANDPOOL_0040 Pool{PoolStorage.data()};
  Good &= Core.pfnCreateCommandPool(Device, &PoolArgs, Pool) == S_OK;
  Core.pfnResetCommandPool(Device, Pool);
  Core.pfnDestroyCommandPool(OtherDevice, Pool);
  Good &= LastError == E_INVALIDARG && Owner.LiveCommandPools == 1;
  Core.pfnDestroyCommandPool(Device, Pool);
  Good &= Owner.LiveCommandPools == 0;
  std::puts(Good ? "compiled DDI ownership PASS"
                 : "compiled DDI ownership FAIL");
  return Good;
}
