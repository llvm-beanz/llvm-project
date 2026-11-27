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

HRESULT APIENTRY createQueueContext(D3D12DDI_HRTCOMMANDQUEUE Queue,
                                    D3DDDICB_CREATECONTEXTVIRTUAL *Args) {
  auto *Private = static_cast<feme::probe::WarpPrivateContextBuffer *>(
      Args->pPrivateDriverData);
  if (!Queue.handle || Args->PrivateDriverDataSize != sizeof(*Private))
    return E_INVALIDARG;
  *Private->Buffer = 0x10000;
  *Private->BufferSize = 4096;
  Args->hContext = Queue.handle;
  return S_OK;
}

HRESULT APIENTRY destroyQueueContext(D3D12DDI_HRTCOMMANDQUEUE Queue,
                                     const D3DDDICB_DESTROYCONTEXT *Args) {
  return Args->hContext == Queue.handle ? S_OK : E_INVALIDARG;
}

struct Storage {
  std::vector<std::max_align_t> Words;
  explicit Storage(SIZE_T Size)
      : Words((Size + sizeof(std::max_align_t) - 1) /
              sizeof(std::max_align_t)) {}
  void *data() { return Words.data(); }
};
void APIENTRY setListTable(D3D12DDI_HRTCOMMANDLIST List,
                           D3D12DDI_HRTTABLE Table) {
  if (List.handle != Table.handle)
    LastError = E_INVALIDARG;
  else
    LastError = S_OK;
}
} // namespace

bool testWarpCompilerDDIs(const std::vector<std::uint32_t> &Tokens) {
  feme::probe::WarpDeviceState Owner, Other;
  D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 Callbacks{};
  Callbacks.pfnSetErrorCb = setError;
  Callbacks.pfnCreateContextVirtualCb = createQueueContext;
  Callbacks.pfnDestroyContextCb = destroyQueueContext;
  Callbacks.pfnSetCommandListDDITableCb = setListTable;
  Owner.UMCallbacks = Other.UMCallbacks = &Callbacks;
  Owner.RTDevice = &Owner;
  D3D12DDI_HRTTABLE RuntimeTables[2] = {{&Owner}, {}};
  Owner.CommandListTables = RuntimeTables;
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
  if (Compute) {
    D3D12DDI_COMMAND_LIST_FUNCS_3D_0062 Lists{};
    D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 Queues{};
    Good &=
        feme::probe::fillWarpPrototypeTable(D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D,
                                            &Lists, sizeof(Lists), 0) == S_OK;
    Good &= feme::probe::fillWarpPrototypeTable(
                D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D, &Queues, sizeof(Queues),
                0) == S_OK;
    D3D12DDIARG_CREATE_COMMAND_POOL_0040 PoolArgs{};
    Storage PoolStorage(Core.pfnCalcPrivateCommandPoolSize(Device, &PoolArgs));
    D3D12DDI_HCOMMANDPOOL_0040 Pool{PoolStorage.data()};
    Good &= Core.pfnCreateCommandPool(Device, &PoolArgs, Pool) == S_OK;
    D3D12DDIARG_CREATECOMMANDQUEUE_0050 QueueArgs{};
    QueueArgs.NodeMask = 1;
    QueueArgs.QueueFlags =
        D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE | D3D12DDI_COMMAND_QUEUE_FLAG_COPY;
    Storage QueueStorage(
        Core.pfnCalcPrivateCommandQueueSize(Device, &QueueArgs));
    D3D12DDI_HCOMMANDQUEUE Queue{QueueStorage.data()};
    D3D12DDI_HRTCOMMANDQUEUE RTQueue{};
    RTQueue.handle = &Owner;
    Good &=
        Core.pfnCreateCommandQueue(Device, &QueueArgs, Queue, RTQueue) == S_OK;
    D3D12DDIARG_CREATE_COMMAND_RECORDER_0040 RecorderArgs{};
    RecorderArgs.QueueFlags =
        QueueArgs.QueueFlags | D3D12DDI_COMMAND_QUEUE_FLAG_3D;
    Storage RecorderStorage(
        Core.pfnCalcPrivateCommandRecorderSize(Device, &RecorderArgs));
    D3D12DDI_HCOMMANDRECORDER_0040 Recorder{RecorderStorage.data()};
    Good &=
        Core.pfnCreateCommandRecorder(Device, &RecorderArgs, Recorder) == S_OK;
    Core.pfnCommandRecorderSetCommandPoolAsTarget(Device, Recorder, Pool);
    D3D12DDIARG_CREATE_COMMAND_LIST_0040 ListArgs{};
    ListArgs.NodeMask = 1;
    ListArgs.QueueFlags = QueueArgs.QueueFlags;
    Storage ListStorage(Core.pfnCalcPrivateCommandListSize(Device, &ListArgs));
    D3D12DDI_HCOMMANDLIST List{ListStorage.data()};
    ListArgs.CommandListFlags =
        static_cast<D3D12DDI_COMMAND_LIST_FLAGS>(0x80000000);
    Good &= Core.pfnCalcPrivateCommandListSize(Device, &ListArgs) != 0;
    Good &= Core.pfnCreateCommandList(Device, &ListArgs, List, {&Owner}) ==
            E_INVALIDARG;
    ListArgs.CommandListFlags = D3D12DDI_COMMAND_LIST_FLAG_NONE;
    Good &=
        Core.pfnCreateCommandList(Device, &ListArgs, List, {}) == E_NOINTERFACE;
    LastError = E_FAIL;
    Good &=
        Core.pfnCreateCommandList(Device, &ListArgs, List, {&Owner}) == S_OK;
    Good &= LastError == S_OK;
    D3D12DDIARG_RESETCOMMANDLIST_0040 Reset{};
    Reset.hDrvCommandRecorder = Recorder;
    LastError = S_OK;
    Lists.pfnResetCommandList(List, &Reset);
    Lists.pfnIaSetTopology(List, D3D12DDI_PRIMITIVE_TOPOLOGY_UNDEFINED);
    Lists.pfnSetDescriptorHeaps(List, 0, nullptr);
    Lists.pfnRsSetViewports(List, 0, nullptr);
    Lists.pfnRsSetScissorRects(List, 0, nullptr);
    const FLOAT BlendFactor[4] = {1, 1, 1, 1};
    Lists.pfnOmSetBlendFactor(List, BlendFactor);
    Lists.pfnOmSetStencilRef(List, 0);
    Lists.pfnIASetIndexBuffer(List, nullptr);
    D3D12DDI_VERTEX_BUFFER_VIEW VertexViews[32] = {};
    Lists.pfnIASetVertexBuffers(List, 0, 32, VertexViews);
    D3D12DDI_STREAM_OUTPUT_BUFFER_VIEW StreamViews[4] = {};
    Lists.pfnSOSetTargets(List, 0, 4, StreamViews);
    Lists.pfnOMSetRenderTargets(List, 0, nullptr, TRUE, nullptr);
    Lists.pfnSetPredication(List, {}, 0, D3D12DDI_PREDICATION_OP_EQUAL_ZERO);
    Good &= LastError == S_OK;
    VertexViews[31].BufferLocation = 1;
    Lists.pfnIASetVertexBuffers(List, 0, 32, VertexViews);
    Good &= LastError == E_NOTIMPL;
    LastError = S_OK;
    Lists.pfnSetPipelineState(List, Pipeline);
    Lists.pfnDispatch(List, 2, 1, 1);
    Good &= LastError == S_OK;
    Core.pfnResetCommandPool(Device, Pool);
    Good &= LastError == E_INVALIDARG;
    LastError = S_OK;
    Queues.pfnExecuteCommandLists(Queue, 1, &List);
    Good &= LastError == E_INVALIDARG;
    LastError = S_OK;
    Lists.pfnCloseCommandList(List);
    Core.pfnDestroyPipelineState(Device, Pipeline);
    Queues.pfnExecuteCommandLists(Queue, 1, &List);
    Queues.pfnExecuteCommandLists(Queue, 1, &List);
    Good &= LastError == S_OK;
    Core.pfnResetCommandPool(Device, Pool);
    Queues.pfnExecuteCommandLists(Queue, 1, &List);
    Good &= LastError == E_INVALIDARG;
    LastError = S_OK;
    Lists.pfnResetCommandList(List, &Reset);
    Lists.pfnCloseCommandList(List);
    Queues.pfnExecuteCommandLists(Queue, 1, &List);
    Good &= LastError == S_OK;
    Core.pfnDestroyCommandQueue(OtherDevice, Queue);
    Good &= LastError == E_INVALIDARG && Owner.LiveCommandQueues == 1;
    LastError = S_OK;
    Core.pfnDestroyCommandPool(Device, Pool);
    Queues.pfnExecuteCommandLists(Queue, 1, &List);
    Good &= LastError == E_INVALIDARG;
    Core.pfnDestroyCommandList(Device, List);
    Core.pfnDestroyCommandRecorder(Device, Recorder);
    Core.pfnDestroyCommandQueue(Device, Queue);
    Good &= Owner.LiveCommandQueues == 0 && Owner.LiveCommandRecorders == 0 &&
            Owner.LiveCommandLists == 0 && Owner.LiveCommandPools == 0;
  } else
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
