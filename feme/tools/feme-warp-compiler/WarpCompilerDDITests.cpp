//===- WarpCompilerDDITests.cpp - Native shader DDI ownership tests
//---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "..\..\utils\windows-adapter-bootstrap\WarpTables.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
std::atomic<HRESULT> LastError{S_OK};
std::atomic<UINT64> FenceValues[2]{};
alignas(8) UINT64 MappedFenceValues[2]{};
UINT NextFence = 0;
UINT LiveFences = 0;
UINT LiveContexts = 0;
HRESULT FenceCreationResult = S_OK;
std::atomic<HRESULT> CPUWaitResult{S_OK};
bool CancelCPUWait = false;
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
  ++LiveContexts;
  return S_OK;
}

HRESULT APIENTRY destroyQueueContext(D3D12DDI_HRTCOMMANDQUEUE Queue,
                                     const D3DDDICB_DESTROYCONTEXT *Args) {
  if (Args->hContext != Queue.handle || !LiveContexts)
    return E_INVALIDARG;
  --LiveContexts;
  return S_OK;
}

HRESULT APIENTRY createSync(HANDLE Device,
                            D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 *Args) {
  if (!Device || Args->Info.Type != D3DDDI_MONITORED_FENCE || NextFence == 2)
    return E_INVALIDARG;
  if (NextFence == 1 && FAILED(FenceCreationResult))
    return FenceCreationResult;
  Args->Info.MonitoredFence.FenceValueCPUVirtualAddress =
      &MappedFenceValues[NextFence];
  MappedFenceValues[NextFence] = 0;
  Args->hSyncObject = ++NextFence;
  ++LiveFences;
  return NextFence == 2 ? FenceCreationResult : S_OK;
}

HRESULT APIENTRY
destroySync(HANDLE Device, const D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT *Args) {
  if (!Device || !Args->hSyncObject || Args->hSyncObject > 2 || !LiveFences)
    return E_INVALIDARG;
  --LiveFences;
  return S_OK;
}

HRESULT APIENTRY signalGPU(
    HANDLE Device, const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU *Args) {
  if (Args->hContext != Device || Args->ObjectCount != 1 ||
      Args->ObjectHandleArray[0] != 1)
    return E_INVALIDARG;
  FenceValues[0] = Args->MonitoredFenceValueArray[0];
  InterlockedExchange64(
      reinterpret_cast<volatile LONG64 *>(&MappedFenceValues[0]),
      static_cast<LONG64>(Args->MonitoredFenceValueArray[0]));
  return S_OK;
}

HRESULT APIENTRY waitGPU(
    HANDLE Device, const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU *Args) {
  return Args->hContext == Device && Args->ObjectCount == 1 &&
                 Args->ObjectHandleArray[0] == 2
             ? S_OK
             : E_INVALIDARG;
}

HRESULT APIENTRY signalCPU(
    HANDLE Device, const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *Args) {
  if (!Device || Args->ObjectCount != 1 || Args->ObjectHandleArray[0] != 2)
    return E_INVALIDARG;
  FenceValues[1] = Args->FenceValueArray[0];
  InterlockedExchange64(
      reinterpret_cast<volatile LONG64 *>(&MappedFenceValues[1]),
      static_cast<LONG64>(Args->FenceValueArray[0]));
  return S_OK;
}

HRESULT APIENTRY waitCPU(
    HANDLE Device, const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *Args) {
  if (!Device || Args->ObjectCount != 1 || Args->ObjectHandleArray[0] != 1 ||
      FenceValues[0] < Args->FenceValueArray[0] || !Args->hAsyncEvent)
    return E_INVALIDARG;
  if (FAILED(CPUWaitResult.load()))
    return CPUWaitResult.load();
  if (CancelCPUWait)
    InterlockedExchange64(
        reinterpret_cast<volatile LONG64 *>(&MappedFenceValues[0]), 0);
  return SetEvent(Args->hAsyncEvent) ? S_OK
                                     : HRESULT_FROM_WIN32(GetLastError());
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

bool testQueueWorkerFailure(D3D12DDI_HDEVICE Device,
                            const D3D12DDI_DEVICE_FUNCS_CORE_0072 &Core,
                            bool Canceled = false) {
  NextFence = 0;
  FenceValues[0] = FenceValues[1] = 0;
  D3D12DDI_COMMAND_LIST_FUNCS_3D_0062 Lists{};
  D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 Queues{};
  if (feme::probe::fillWarpPrototypeTable(D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D,
                                          &Lists, sizeof(Lists), 0) != S_OK ||
      feme::probe::fillWarpPrototypeTable(D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D,
                                          &Queues, sizeof(Queues), 0) != S_OK)
    return false;
  D3D12DDIARG_CREATECOMMANDQUEUE_0050 QueueArgs{};
  QueueArgs.NodeMask = 1;
  QueueArgs.QueueFlags =
      D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE | D3D12DDI_COMMAND_QUEUE_FLAG_COPY;
  Storage QueueStorage(Core.pfnCalcPrivateCommandQueueSize(Device, &QueueArgs));
  D3D12DDI_HCOMMANDQUEUE Queue{QueueStorage.data()};
  if (Core.pfnCreateCommandQueue(Device, &QueueArgs, Queue,
                                 {Device.pDrvPrivate}) != S_OK)
    return false;
  D3D12DDIARG_CREATE_COMMAND_POOL_0040 PoolArgs{};
  Storage PoolStorage(Core.pfnCalcPrivateCommandPoolSize(Device, &PoolArgs));
  D3D12DDI_HCOMMANDPOOL_0040 Pool{PoolStorage.data()};
  if (Core.pfnCreateCommandPool(Device, &PoolArgs, Pool) != S_OK)
    return false;
  D3D12DDIARG_CREATE_COMMAND_RECORDER_0040 RecorderArgs{};
  RecorderArgs.QueueFlags = QueueArgs.QueueFlags;
  Storage RecorderStorage(
      Core.pfnCalcPrivateCommandRecorderSize(Device, &RecorderArgs));
  D3D12DDI_HCOMMANDRECORDER_0040 Recorder{RecorderStorage.data()};
  if (Core.pfnCreateCommandRecorder(Device, &RecorderArgs, Recorder) != S_OK)
    return false;
  Core.pfnCommandRecorderSetCommandPoolAsTarget(Device, Recorder, Pool);
  D3D12DDIARG_CREATE_COMMAND_LIST_0040 ListArgs{};
  ListArgs.NodeMask = 1;
  ListArgs.QueueFlags = QueueArgs.QueueFlags;
  Storage ListStorage(Core.pfnCalcPrivateCommandListSize(Device, &ListArgs));
  D3D12DDI_HCOMMANDLIST List{ListStorage.data()};
  if (Core.pfnCreateCommandList(Device, &ListArgs, List,
                                {Device.pDrvPrivate}) != S_OK)
    return false;
  D3D12DDIARG_RESETCOMMANDLIST_0040 Reset{};
  Reset.hDrvCommandRecorder = Recorder;
  Lists.pfnResetCommandList(List, &Reset);
  Lists.pfnCloseCommandList(List);
  CPUWaitResult = Canceled ? S_OK : E_FAIL;
  CancelCPUWait = Canceled;
  Queues.pfnExecuteCommandLists(Queue, 1, &List);
  Core.pfnDestroyCommandQueue(Device, Queue);
  CPUWaitResult = S_OK;
  CancelCPUWait = false;
  bool Good = LastError == (Canceled ? DXGI_ERROR_DEVICE_REMOVED : E_FAIL) &&
              FenceValues[1] == 1 && !LiveFences && !LiveContexts;
  Core.pfnDestroyCommandList(Device, List);
  Core.pfnDestroyCommandRecorder(Device, Recorder);
  Core.pfnDestroyCommandPool(Device, Pool);
  return Good;
}
} // namespace

bool testWarpCompilerDDIs(const std::vector<std::uint32_t> &Tokens) {
  NextFence = LiveFences = 0;
  LiveContexts = 0;
  FenceValues[0] = FenceValues[1] = 0;
  feme::probe::WarpDeviceState Owner, Other;
  D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 Callbacks{};
  Callbacks.pfnSetErrorCb = setError;
  Callbacks.pfnCreateContextVirtualCb = createQueueContext;
  Callbacks.pfnDestroyContextCb = destroyQueueContext;
  Callbacks.pfnSetCommandListDDITableCb = setListTable;
  Owner.UMCallbacks = Other.UMCallbacks = &Callbacks;
  Owner.RTDevice = &Owner;
  D3DDDI_DEVICECALLBACKS KTCallbacks{};
  KTCallbacks.pfnCreateSynchronizationObject2Cb = createSync;
  KTCallbacks.pfnDestroySynchronizationObjectCb = destroySync;
  KTCallbacks.pfnSignalSynchronizationObjectFromGpuCb = signalGPU;
  KTCallbacks.pfnWaitForSynchronizationObjectFromGpuCb = waitGPU;
  KTCallbacks.pfnSignalSynchronizationObjectFromCpuCb = signalCPU;
  KTCallbacks.pfnWaitForSynchronizationObjectFromCpuCb = waitCPU;
  Owner.KTCallbacks = &KTCallbacks;
  D3D12DDI_HRTTABLE RuntimeTables[2] = {{&Owner}, {}};
  Owner.CommandListTables = RuntimeTables;
  Other.RTDevice = &Other;
  D3D12DDI_HDEVICE Device{&Owner}, OtherDevice{&Other};
  D3D12DDI_DEVICE_FUNCS_CORE_0072 Core{};
  if (feme::probe::fillWarpPrototypeTable(D3D12DDI_TABLE_TYPE_DEVICE_CORE,
                                          &Core, sizeof(Core), 0) != S_OK)
    return false;
  D3D12DDI_FENCE FenceDesc{};
  FenceDesc.FenceValue.BaseAddress = 16;
  FenceDesc.Flags = D3D12DDI_FENCE_FLAG_BOTTOM_OF_PIPE;
  D3D12DDIARG_CREATE_FENCE FenceArgs{1, &FenceDesc};
  Storage FenceStorage(Core.pfnCalcPrivateFenceSize(Device, &FenceArgs));
  D3D12DDI_HFENCE Fence{FenceStorage.data()};
  if (Core.pfnCreateFence(Device, Fence, &FenceArgs) != S_OK ||
      Owner.LiveFences != 1)
    return false;
  Core.pfnDestroyFence(OtherDevice, Fence);
  if (LastError != E_INVALIDARG || Owner.LiveFences != 1)
    return false;
  Core.pfnDestroyFence(Device, Fence);
  FenceDesc.FenceValue.BaseAddress = 1;
  if (Owner.LiveFences ||
      Core.pfnCreateFence(Device, Fence, &FenceArgs) != E_INVALIDARG)
    return false;
  FenceDesc.FenceValue.BaseAddress = 16;
  FenceArgs.FenceCount = 2;
  if (Core.pfnCalcPrivateFenceSize(Device, &FenceArgs) ||
      LastError != E_INVALIDARG)
    return false;
  LastError = S_OK;
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
    FenceCreationResult = E_OUTOFMEMORY;
    Good &= Core.pfnCreateCommandQueue(Device, &QueueArgs, Queue, RTQueue) ==
                E_OUTOFMEMORY &&
            !LiveFences && !LiveContexts && !Owner.LiveCommandQueues;
    NextFence = 0;
    FenceCreationResult = S_FALSE;
    Good &= Core.pfnCreateCommandQueue(Device, &QueueArgs, Queue, RTQueue) ==
                E_UNEXPECTED &&
            !LiveFences && !LiveContexts && !Owner.LiveCommandQueues;
    NextFence = 0;
    FenceCreationResult = S_OK;
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
    D3D12DDI_ROOT_PARAMETER_0013 ConstantParameter{};
    ConstantParameter.ParameterType =
        D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    ConstantParameter.ShaderVisibility = D3D12DDI_SHADER_VISIBILITY_ALL;
    ConstantParameter.Constants.Num32BitValues = 5;
    D3D12DDI_ROOT_SIGNATURE_0013 ConstantDesc{};
    ConstantDesc.NumParameters = 1;
    ConstantDesc.pRootParameters = &ConstantParameter;
    auto ConstantArgs = RootArgs;
    ConstantArgs.pRootSignature_1_1 = &ConstantDesc;
    Storage ConstantStorage(
        Core.pfnCalcPrivateRootSignatureSize(Device, &ConstantArgs));
    D3D12DDI_HROOTSIGNATURE ConstantRoot{ConstantStorage.data()};
    Good &= Core.pfnCreateRootSignature(Device, &ConstantArgs, ConstantRoot) ==
            S_OK;
    Lists.pfnSetComputeRootSignature(List, ConstantRoot);
    Lists.pfnSetComputeRoot32BitConstant(List, 0, 23, 4);
    Good &= LastError == S_OK;
    Lists.pfnSetComputeRoot32BitConstants(List, 0, 0, nullptr, 5);
    Good &= LastError == S_OK;
    Lists.pfnSetComputeRoot32BitConstant(List, 0, 23, 5);
    Good &= LastError == E_INVALIDARG;
    LastError = S_OK;
    Lists.pfnSetComputeRoot32BitConstants(List, 0, 1, nullptr, 0);
    Good &= LastError == E_INVALIDARG;
    LastError = S_OK;
    UINT Values[2] = {7, 13};
    Lists.pfnSetComputeRoot32BitConstants(List, 0, 2, Values, 4);
    Good &= LastError == E_INVALIDARG;
    LastError = S_OK;
    Lists.pfnSetComputeRoot32BitConstant(List, 0, 23,
                                         std::numeric_limits<UINT>::max());
    Good &= LastError == E_INVALIDARG;
    LastError = S_OK;
    Lists.pfnSetComputeRoot32BitConstant(List, 1, 23, 0);
    Good &= LastError == E_INVALIDARG;
    LastError = S_OK;
    Lists.pfnSetComputeRootSignature(List, Root);
    Core.pfnDestroyRootSignature(Device, ConstantRoot);
    D3D12DDI_INDIRECT_ARGUMENT_DESC IndirectArgument{};
    IndirectArgument.Type = D3D12DDI_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 SignatureArgs{};
    SignatureArgs.NodeMask = 1;
    SignatureArgs.ByteStride = 16;
    SignatureArgs.NumArgumentDescs = 1;
    SignatureArgs.pArgumentDescs = &IndirectArgument;
    Storage SignatureStorage(
        Core.pfnCalcPrivateCommandSignatureSize(Device, &SignatureArgs));
    D3D12DDI_HCOMMANDSIGNATURE Signature{SignatureStorage.data()};
    Good &= Core.pfnCreateCommandSignature(Device, &SignatureArgs, Signature) ==
            S_OK;
    Good &= Core.pfnCreateCommandSignature(Device, &SignatureArgs, Signature) ==
            E_INVALIDARG;
    Core.pfnDestroyCommandSignature(OtherDevice, Signature);
    Good &= LastError == E_INVALIDARG;
    LastError = S_OK;
    Lists.pfnExecuteIndirect(List, Signature, 1, {}, {});
    Good &= LastError == E_INVALIDARG;
    LastError = S_OK;
    Core.pfnDestroyCommandSignature(Device, Signature);
    Lists.pfnExecuteIndirect(List, Signature, 1, {}, {});
    Good &= LastError == E_INVALIDARG;
    Core.pfnDestroyCommandSignature(Device, {&Other});
    Good &= LastError == E_INVALIDARG;
    SignatureArgs.ByteStride = 13;
    Good &= !Core.pfnCalcPrivateCommandSignatureSize(Device, &SignatureArgs) &&
            LastError == E_INVALIDARG;
    SignatureArgs.ByteStride = 16;
    SignatureArgs.hRootSignature = Root;
    Good &= !Core.pfnCalcPrivateCommandSignatureSize(Device, &SignatureArgs) &&
            LastError == E_INVALIDARG;
    SignatureArgs.hRootSignature = {};
    IndirectArgument.Type = D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW;
    Good &= !Core.pfnCalcPrivateCommandSignatureSize(Device, &SignatureArgs) &&
            LastError == E_NOTIMPL;
    LastError = S_OK;
    D3D12DDIARG_RESOURCE_BARRIER_0022 Alias{};
    Alias.Type = D3D12DDI_RESOURCE_BARRIER_TYPE_0022_RANGED;
    Alias.Flags = D3D12DDI_RESOURCE_BARRIER_FLAG_0022_ALIASING;
    Alias.Ranged.Subresource = std::numeric_limits<UINT>::max();
    Alias.Ranged.Range.End = std::numeric_limits<UINT64>::max();
    Lists.pfnResourceBarrier(List, 1, &Alias);
    Good &= LastError == S_OK;
    Alias.Ranged.hResource.pDrvPrivate = &Other;
    Lists.pfnResourceBarrier(List, 1, &Alias);
    Good &= LastError == E_INVALIDARG;
    LastError = S_OK;
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
    Good &= LiveFences == 0 && FenceValues[1] == 3;
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
  D3D12DDI_ROOT_PARAMETER_0013 Parameters[64] = {};
  for (auto &Parameter : Parameters) {
    Parameter.ParameterType = D3D12DDI_ROOT_PARAMETER_TYPE_UAV;
    Parameter.ShaderVisibility = D3D12DDI_SHADER_VISIBILITY_ALL;
    Parameter.Descriptor.ShaderRegister = 3;
  }
  RootDesc.NumParameters = 65;
  RootDesc.pRootParameters = Parameters;
  Good &= Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs) == 0;
  RootDesc.NumParameters = 2;
  Good &= Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs) == 0;
  Parameters[1].ParameterType = D3D12DDI_ROOT_PARAMETER_TYPE_SRV;
  Storage BufferRootStorage(
      Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs));
  D3D12DDI_HROOTSIGNATURE BufferRoot{BufferRootStorage.data()};
  Good &= Core.pfnCreateRootSignature(Device, &RootArgs, BufferRoot) == S_OK;
  Core.pfnDestroyRootSignature(OtherDevice, BufferRoot);
  Good &= LastError == E_INVALIDARG && Owner.LiveRootSignatures == 1;
  Core.pfnDestroyRootSignature(Device, BufferRoot);
  Good &= Owner.LiveRootSignatures == 0;
  Parameters[1].ParameterType = D3D12DDI_ROOT_PARAMETER_TYPE_CBV;
  Storage CBVRootStorage(
      Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs));
  D3D12DDI_HROOTSIGNATURE CBVRoot{CBVRootStorage.data()};
  Good &= Core.pfnCreateRootSignature(Device, &RootArgs, CBVRoot) == S_OK;
  Core.pfnDestroyRootSignature(Device, CBVRoot);
  Parameters[0].ParameterType = D3D12DDI_ROOT_PARAMETER_TYPE_CBV;
  Good &= Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs) == 0;
  Good &= Owner.LiveRootSignatures == 0;
  Parameters[1].ParameterType = D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  Parameters[1].Constants.Num32BitValues = 5;
  Good &= Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs) == 0;
  RootDesc.NumParameters = 64;
  for (UINT I = 0; I < 64; ++I) {
    Parameters[I].ParameterType = D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    Parameters[I].Constants.ShaderRegister = I;
    Parameters[I].Constants.Num32BitValues = 1;
  }
  Storage FullRootStorage(
      Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs));
  D3D12DDI_HROOTSIGNATURE FullRoot{FullRootStorage.data()};
  Good &= Core.pfnCreateRootSignature(Device, &RootArgs, FullRoot) == S_OK;
  Core.pfnDestroyRootSignature(Device, FullRoot);
  Parameters[0].Constants.Num32BitValues = 2;
  Good &= Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs) == 0;
  Parameters[0].Constants.Num32BitValues = 0;
  Good &= Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs) == 0;
  Good &= Owner.LiveRootSignatures == 0;
  D3D12DDI_DESCRIPTOR_RANGE_0013 TableRange{};
  TableRange.RangeType = D3D12DDI_DESCRIPTOR_RANGE_TYPE_CBV;
  TableRange.NumDescriptors = 1;
  TableRange.BaseShaderRegister = 3;
  TableRange.OffsetInDescriptorsFromTableStart =
      std::numeric_limits<UINT>::max();
  RootDesc.NumParameters = 1;
  Parameters[0] = {};
  Parameters[0].ParameterType = D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  Parameters[0].ShaderVisibility = D3D12DDI_SHADER_VISIBILITY_ALL;
  Parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  Parameters[0].DescriptorTable.pDescriptorRanges = &TableRange;
  Storage TableRootStorage(
      Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs));
  D3D12DDI_HROOTSIGNATURE TableRoot{TableRootStorage.data()};
  Good &= Core.pfnCreateRootSignature(Device, &RootArgs, TableRoot) == S_OK;
  Core.pfnDestroyRootSignature(Device, TableRoot);
  for (auto Type : {D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV,
                    D3D12DDI_DESCRIPTOR_RANGE_TYPE_UAV}) {
    TableRange.RangeType = Type;
    Good &= Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs) != 0;
    Good &= Core.pfnCreateRootSignature(Device, &RootArgs, TableRoot) == S_OK;
    Core.pfnDestroyRootSignature(Device, TableRoot);
  }
  TableRange.RangeType = D3D12DDI_DESCRIPTOR_RANGE_TYPE_CBV;
  TableRange.NumDescriptors = 2;
  Good &= Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs) == 0 &&
          LastError == E_NOTIMPL;
  TableRange.NumDescriptors = 1;
  TableRange.Flags =
      D3D12DDI_DESCRIPTOR_RANGE_FLAG_0013_DESCRIPTORS_VOLATILE |
      D3D12DDI_DESCRIPTOR_RANGE_FLAG_0052_DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS;
  Good &= Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs) == 0 &&
          LastError == E_INVALIDARG;
  Parameters[0].DescriptorTable.NumDescriptorRanges = 0;
  Good &= Core.pfnCalcPrivateRootSignatureSize(Device, &RootArgs) == 0 &&
          LastError == E_INVALIDARG;
  Good &= testQueueWorkerFailure(Device, Core);
  D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 HeapArgs{};
  HeapArgs.Type = D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  HeapArgs.NodeMask = 1;
  HeapArgs.NumDescriptors = 4;
  HeapArgs.Flags = D3D12DDI_DESCRIPTOR_HEAP_FLAG_CPU_VISIBLE;
  auto BadHeapArgs = HeapArgs;
  BadHeapArgs.NumDescriptors = 0;
  Good &= Core.pfnCalcPrivateDescriptorHeapSize(Device, &BadHeapArgs) == 0;
  BadHeapArgs.NumDescriptors = 1000001;
  Good &= Core.pfnCalcPrivateDescriptorHeapSize(Device, &BadHeapArgs) == 0;
  Storage CPUHeapStorage(
      Core.pfnCalcPrivateDescriptorHeapSize(Device, &HeapArgs));
  D3D12DDI_HDESCRIPTORHEAP CPUHeap{CPUHeapStorage.data()};
  Good &= Core.pfnCreateDescriptorHeap(Device, &HeapArgs, CPUHeap) == S_OK;
  Good &=
      Core.pfnCreateDescriptorHeap(Device, &HeapArgs, CPUHeap) == E_INVALIDARG;
  auto CPUBase = Core.pfnGetCPUDescriptorHandleForHeapStart(Device, CPUHeap);
  Good &= CPUBase.ptr &&
          !Core.pfnGetGPUDescriptorHandleForHeapStart(Device, CPUHeap).ptr;
  HeapArgs.Flags |= D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Storage GPUHeapStorage(
      Core.pfnCalcPrivateDescriptorHeapSize(Device, &HeapArgs));
  D3D12DDI_HDESCRIPTORHEAP GPUHeap{GPUHeapStorage.data()};
  Good &= Core.pfnCreateDescriptorHeap(Device, &HeapArgs, GPUHeap) == S_OK;
  auto GPUCPUBase = Core.pfnGetCPUDescriptorHandleForHeapStart(Device, GPUHeap);
  Good &= GPUCPUBase.ptr &&
          Core.pfnGetGPUDescriptorHandleForHeapStart(Device, GPUHeap).ptr;
  UINT Stride = Core.pfnGetDescriptorSizeInBytes(Device, HeapArgs.Type);
  D3D12DDI_CONSTANT_BUFFER_VIEW_DESC NullView{};
  NullView.SizeInBytes = 256;
  Core.pfnCreateConstantBufferView(Device, &NullView, CPUBase);
  NullView.SizeInBytes = 512;
  D3D12DDI_CPU_DESCRIPTOR_HANDLE Second{CPUBase.ptr + Stride};
  Core.pfnCreateConstantBufferView(Device, &NullView, Second);
  if (CPUBase.ptr && GPUCPUBase.ptr) {
    std::vector<unsigned char> Expected(2 * Stride);
    std::memcpy(Expected.data(), reinterpret_cast<void *>(CPUBase.ptr),
                Expected.size());
    Core.pfnCopyDescriptorsSimple(Device, 2, GPUCPUBase, CPUBase,
                                  HeapArgs.Type);
    Good &=
        std::memcmp(Expected.data(), reinterpret_cast<void *>(GPUCPUBase.ptr),
                    Expected.size()) == 0;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Destinations[] = {
        {GPUCPUBase.ptr + 2 * Stride}, {GPUCPUBase.ptr + 3 * Stride}};
    UINT Count = 2;
    Core.pfnCopyDescriptors(Device, 2, Destinations, nullptr, 1, &CPUBase,
                            &Count, HeapArgs.Type);
    Good &= std::memcmp(Expected.data(),
                        reinterpret_cast<void *>(Destinations[0].ptr),
                        Expected.size()) == 0;
    LastError = S_OK;
    Core.pfnCopyDescriptorsSimple(Device, 1, CPUBase, GPUCPUBase,
                                  HeapArgs.Type);
    Good &= LastError == E_INVALIDARG;
    LastError = S_OK;
    Core.pfnCopyDescriptorsSimple(Device, 5, GPUCPUBase, CPUBase,
                                  HeapArgs.Type);
    Good &= LastError == E_INVALIDARG;
    LastError = S_OK;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Misaligned{CPUBase.ptr + 1};
    Core.pfnCreateConstantBufferView(Device, &NullView, Misaligned);
    Good &= LastError == E_INVALIDARG;
    LastError = S_OK;
    Core.pfnCreateConstantBufferView(OtherDevice, &NullView, CPUBase);
    Good &= LastError == E_INVALIDARG;
    Good &= std::memcmp(Expected.data(), reinterpret_cast<void *>(CPUBase.ptr),
                        Expected.size()) == 0;
    D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002 SRV{};
    SRV.ResourceDimension = D3D12DDI_RD_BUFFER;
    SRV.Shader4ComponentMapping = D3D12DDI_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    SRV.Format = DXGI_FORMAT_R32_TYPELESS;
    SRV.Buffer.NumElements = 4;
    SRV.Buffer.Flags = D3D12DDI_BUFFER_SRV_FLAG_RAW;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Third{CPUBase.ptr + 2 * Stride};
    LastError = S_OK;
    Core.pfnCreateShaderResourceView(Device, &SRV, Third);
    Good &= LastError == S_OK;
    std::vector<unsigned char> Before(Stride);
    std::memcpy(Before.data(), reinterpret_cast<void *>(Third.ptr), Stride);
    SRV.Buffer.FirstElement = std::numeric_limits<UINT64>::max();
    Core.pfnCreateShaderResourceView(Device, &SRV, Third);
    Good &= LastError == E_INVALIDARG &&
            std::memcmp(Before.data(), reinterpret_cast<void *>(Third.ptr),
                        Stride) == 0;
    SRV.Buffer.FirstElement = std::numeric_limits<UINT64>::max() / 4;
    Core.pfnCreateShaderResourceView(Device, &SRV, Third);
    Good &= LastError == E_INVALIDARG &&
            std::memcmp(Before.data(), reinterpret_cast<void *>(Third.ptr),
                        Stride) == 0;
    SRV.Buffer.FirstElement = 0;
    SRV.Buffer.StructureByteStride = 8;
    SRV.Buffer.Flags = D3D12DDI_BUFFER_SRV_FLAG_NONE;
    SRV.Format = DXGI_FORMAT_UNKNOWN;
    LastError = S_OK;
    Core.pfnCreateShaderResourceView(Device, &SRV, Third);
    Good &= LastError == S_OK;
    SRV.Buffer.StructureByteStride = 6;
    Core.pfnCreateShaderResourceView(Device, &SRV, Third);
    Good &= LastError == E_INVALIDARG;
    D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002 UAV{};
    UAV.ResourceDimension = D3D12DDI_RD_BUFFER;
    UAV.Format = DXGI_FORMAT_R32_TYPELESS;
    UAV.Buffer.NumElements = 4;
    UAV.Buffer.Flags = D3D12DDI_BUFFER_UAV_FLAG_RAW;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Fourth{CPUBase.ptr + 3 * Stride};
    LastError = S_OK;
    Core.pfnCreateUnorderedAccessView(Device, &UAV, Fourth);
    Good &= LastError == S_OK;
    UAV.Buffer.hDrvCounterResource = {reinterpret_cast<void *>(1)};
    Core.pfnCreateUnorderedAccessView(Device, &UAV, Fourth);
    Good &= LastError == E_NOTIMPL;
    UAV.Buffer.hDrvCounterResource = {};
    LastError = S_OK;
    Core.pfnCreateUnorderedAccessView(OtherDevice, &UAV, Fourth);
    Good &= LastError == E_INVALIDARG;
    std::memcpy(Expected.data(), reinterpret_cast<void *>(Third.ptr),
                Expected.size());
    Core.pfnCopyDescriptorsSimple(Device, 2, GPUCPUBase, Third, HeapArgs.Type);
    Good &=
        std::memcmp(Expected.data(), reinterpret_cast<void *>(GPUCPUBase.ptr),
                    Expected.size()) == 0;
  } else
    Good = false;
  Core.pfnDestroyDescriptorHeap(OtherDevice, GPUHeap);
  Good &= LastError == E_INVALIDARG;
  Core.pfnDestroyDescriptorHeap(Device, GPUHeap);
  Core.pfnDestroyDescriptorHeap(Device, CPUHeap);
  LastError = S_OK;
  Good &= !Core.pfnGetCPUDescriptorHandleForHeapStart(Device, CPUHeap).ptr &&
          LastError == E_INVALIDARG;
  Good &= Owner.LiveImmutableStates == 0;
  Good &= testQueueWorkerFailure(Device, Core, true);
  std::puts(Good ? "compiled DDI ownership PASS"
                 : "compiled DDI ownership FAIL");
  return Good;
}
