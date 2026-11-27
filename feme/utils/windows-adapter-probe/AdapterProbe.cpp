//===- AdapterProbe.cpp - Stock Windows software adapter boundary probe
//----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../windows-adapter-bootstrap/BootstrapAudit.h"
#include "BufferCopyValidation.h"
#include "ComputeShaderValidation.h"
#include "PartialWaveValidation.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdio>
#include <cwchar>
#include <vector>

using Microsoft::WRL::ComPtr;

#ifdef FEME_PROBE_AGILITY_VERSION
extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion =
    FEME_PROBE_AGILITY_VERSION;
__declspec(dllexport) extern const char *D3D12SDKPath = ".\\AgilitySDK\\";
}
#endif

static HRESULT report(const wchar_t *Operation, HRESULT Result) {
  std::wprintf(L"%ls: 0x%08lx\n", Operation,
               static_cast<unsigned long>(Result));
  return Result;
}

static void reportModule(const wchar_t *Name) {
  wchar_t Path[32768];
  HMODULE Module = GetModuleHandleW(Name);
  if (!Module) {
    std::wprintf(L"Module %ls: not loaded\n", Name);
    return;
  }
  DWORD Length = GetModuleFileNameW(Module, Path, ARRAYSIZE(Path));
  if (!Length || Length == ARRAYSIZE(Path)) {
    std::wprintf(L"GetModuleFileNameW(%ls): error %lu\n", Name, GetLastError());
    return;
  }
  std::wprintf(L"Module %ls: %ls\n", Name, Path);
}

static HRESULT checkWarpIdentity(IDXGIAdapter1 *Adapter) {
  DXGI_ADAPTER_DESC1 Desc{};
  HRESULT Result = report(L"WARP GetDesc1", Adapter->GetDesc1(&Desc));
  if (FAILED(Result))
    return Result;
  if (Desc.VendorId != 0x1414 || Desc.DeviceId != 0x008c ||
      !(Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
    return report(L"Microsoft WARP identity mismatch", E_FAIL);
  return S_OK;
}

static HRESULT signalAndWait(ID3D12Device *Device, ID3D12CommandQueue *Queue,
                             ID3D12CommandList *Commands, bool &Submitted) {
  Submitted = false;
  ComPtr<ID3D12Fence> Fence;
  HRESULT Result =
      report(L"CreateFence", Device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                 IID_PPV_ARGS(&Fence)));
  if (FAILED(Result))
    return Result;
  HANDLE Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!Event)
    return report(L"CreateEventW", HRESULT_FROM_WIN32(GetLastError()));
  Result =
      report(L"SetEventOnCompletion", Fence->SetEventOnCompletion(1, Event));
  if (SUCCEEDED(Result) && Commands) {
    Queue->ExecuteCommandLists(1, &Commands);
    Submitted = true;
  }
  if (SUCCEEDED(Result))
    Result = report(L"Queue::Signal", Queue->Signal(Fence.Get(), 1));
  if (SUCCEEDED(Result)) {
    DWORD Wait = WaitForSingleObject(Event, 10000);
    if (Wait != WAIT_OBJECT_0)
      Result = report(L"Fence wait", Wait == WAIT_FAILED
                                         ? HRESULT_FROM_WIN32(GetLastError())
                                         : HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    else if (Fence->GetCompletedValue() != 1)
      Result = report(L"Fence completed value mismatch", E_FAIL);
  }
  if (Submitted && FAILED(Result)) {
    // Keep the fence and its registered event alive until process exit if
    // completion could not be established. The caller retains the work objects.
    Fence.Detach();
    return Result;
  }
  if (!CloseHandle(Event))
    return report(L"CloseHandle", HRESULT_FROM_WIN32(GetLastError()));
  return Result;
}

static D3D12_RESOURCE_DESC bufferDesc(UINT64 Bytes) {
  D3D12_RESOURCE_DESC Desc{};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = Bytes;
  Desc.Height = 1;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return Desc;
}

static HRESULT createCpuBuffer(ID3D12Device *Device, D3D12_HEAP_TYPE Type,
                               UINT64 Bytes, D3D12_RESOURCE_STATES State,
                               ID3D12Resource **Resource) {
  D3D12_HEAP_PROPERTIES Properties{};
  Properties.Type = Type;
  Properties.CreationNodeMask = Properties.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC Desc = bufferDesc(Bytes);
  return report(
      L"CreateCommittedResource(CPU buffer)",
      Device->CreateCommittedResource(&Properties, D3D12_HEAP_FLAG_NONE, &Desc,
                                      State, nullptr, IID_PPV_ARGS(Resource)));
}

static HRESULT exerciseBufferCopy(ID3D12Device *Device,
                                  ID3D12CommandQueue *Queue) {
  using namespace feme::probe;
  ComPtr<ID3D12Heap> Heap;
  D3D12_HEAP_DESC HeapDesc{};
  HeapDesc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  HeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  HeapDesc.Properties.CreationNodeMask = HeapDesc.Properties.VisibleNodeMask =
      1;
  HeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  HRESULT Result = report(L"CreateHeap(default buffer)",
                          Device->CreateHeap(&HeapDesc, IID_PPV_ARGS(&Heap)));
  if (FAILED(Result))
    return Result;
  ComPtr<ID3D12Resource> Buffer, Upload, Readback;
  D3D12_RESOURCE_DESC Desc = bufferDesc(BufferBytes);
  Result = report(L"CreatePlacedResource(buffer)",
                  Device->CreatePlacedResource(Heap.Get(), 0, &Desc,
                                               D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr, IID_PPV_ARGS(&Buffer)));
  if (FAILED(Result))
    return Result;
  Result =
      createCpuBuffer(Device, D3D12_HEAP_TYPE_UPLOAD, 2 * BufferBytes,
                      D3D12_RESOURCE_STATE_GENERIC_READ, Upload.GetAddressOf());
  if (FAILED(Result))
    return Result;
  Result =
      createCpuBuffer(Device, D3D12_HEAP_TYPE_READBACK, BufferBytes,
                      D3D12_RESOURCE_STATE_COPY_DEST, Readback.GetAddressOf());
  if (FAILED(Result))
    return Result;
  void *Mapped = nullptr;
  D3D12_RANGE NoAccess{0, 0};
  Result = report(L"Map(upload)", Upload->Map(0, &NoAccess, &Mapped));
  if (FAILED(Result))
    return Result;
  if (!Mapped) {
    Upload->Unmap(0, &NoAccess);
    return report(L"Missing upload mapping", E_FAIL);
  }
  fillCopyUpload(static_cast<std::uint8_t *>(Mapped));
  D3D12_RANGE UploadWritten{0, 2 * BufferBytes};
  Upload->Unmap(0, &UploadWritten);

  ComPtr<ID3D12CommandAllocator> Allocator;
  Result = report(L"CreateCommandAllocator(copy)",
                  Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,
                                                 IID_PPV_ARGS(&Allocator)));
  if (FAILED(Result))
    return Result;
  ComPtr<ID3D12GraphicsCommandList> Commands;
  Result = report(L"CreateCommandList(copy)",
                  Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY,
                                            Allocator.Get(), nullptr,
                                            IID_PPV_ARGS(&Commands)));
  if (FAILED(Result))
    return Result;
  Commands->CopyBufferRegion(Buffer.Get(), 0, Upload.Get(), 0,
                             CopyTargetOffset);
  Commands->CopyBufferRegion(Buffer.Get(), CopyTargetOffset, Upload.Get(),
                             BufferBytes + CopySourceOffset, CopyBytes);
  Commands->CopyBufferRegion(Buffer.Get(), CopyTailOffset, Upload.Get(),
                             CopyTailOffset, BufferBytes - CopyTailOffset);
  D3D12_RESOURCE_BARRIER Barrier{};
  Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  Barrier.Transition.pResource = Buffer.Get();
  Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  Commands->ResourceBarrier(1, &Barrier);
  Commands->CopyBufferRegion(Readback.Get(), 0, Buffer.Get(), 0, BufferBytes);
  Result = report(L"Close(copy list)", Commands->Close());
  if (FAILED(Result))
    return Result;
  bool Submitted = false;
  Result = signalAndWait(Device, Queue, Commands.Get(), Submitted);
  if (FAILED(Result)) {
    if (Submitted) {
      std::wprintf(
          L"Retaining copy resources, queue, device and synchronization "
          L"objects until process exit after completion/teardown failure.\n");
      Device->AddRef();
      Queue->AddRef();
      Heap.Detach();
      Buffer.Detach();
      Upload.Detach();
      Readback.Detach();
      Allocator.Detach();
      Commands.Detach();
    }
    return Result;
  }
  D3D12_RANGE ReadRange{0, BufferBytes};
  Mapped = nullptr;
  Result = report(L"Map(readback)", Readback->Map(0, &ReadRange, &Mapped));
  if (FAILED(Result))
    return Result;
  bool Valid = validateBufferCopy(static_cast<const std::uint8_t *>(Mapped),
                                  BufferBytes);
  Readback->Unmap(0, &NoAccess);
  if (!Valid)
    return report(L"Buffer copy validation", E_FAIL);
  std::wprintf(
      L"Buffer copy verified: bytes=%zu prefix=%zu patch=%zu tail=%zu; "
      L"source-offset=%zu target-offset=%zu\n",
      BufferBytes, CopyTargetOffset, CopyBytes, BufferBytes - CopyTailOffset,
      CopySourceOffset, CopyTargetOffset);
  return S_OK;
}

static HRESULT readShader(const wchar_t *Path,
                          std::vector<std::uint8_t> &Code) {
  wchar_t FullPath[32768];
  DWORD Length = GetFullPathNameW(Path, ARRAYSIZE(FullPath), FullPath, nullptr);
  if (!Length || Length >= ARRAYSIZE(FullPath))
    return report(L"GetFullPathNameW(shader)",
                  Length ? HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW)
                         : HRESULT_FROM_WIN32(GetLastError()));
  HANDLE File = CreateFileW(FullPath, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (File == INVALID_HANDLE_VALUE)
    return report(L"CreateFileW(shader)", HRESULT_FROM_WIN32(GetLastError()));
  LARGE_INTEGER Size{};
  HRESULT Result = S_OK;
  if (!GetFileSizeEx(File, &Size))
    Result =
        report(L"GetFileSizeEx(shader)", HRESULT_FROM_WIN32(GetLastError()));
  else if (Size.QuadPart <= 0 || Size.QuadPart > 16 * 1024 * 1024) {
    std::wprintf(L"Shader size outside (0, 16777216]: %lld\n", Size.QuadPart);
    Result = report(L"Invalid shader size", E_INVALIDARG);
  } else {
    Code.resize(static_cast<std::size_t>(Size.QuadPart));
    DWORD BytesRead = 0;
    if (!ReadFile(File, Code.data(), static_cast<DWORD>(Code.size()),
                  &BytesRead, nullptr))
      Result = report(L"ReadFile(shader)", HRESULT_FROM_WIN32(GetLastError()));
    else if (BytesRead != Code.size())
      Result = report(L"Incomplete shader read",
                      HRESULT_FROM_WIN32(ERROR_HANDLE_EOF));
    else
      std::wprintf(L"Shader input: %ls; bytes=%zu\n", FullPath, Code.size());
  }
  if (!CloseHandle(File)) {
    HRESULT CloseResult =
        report(L"CloseHandle(shader)", HRESULT_FROM_WIN32(GetLastError()));
    if (SUCCEEDED(Result))
      Result = CloseResult;
  }
  return Result;
}

enum class ComputeWorkload { Linear, Grouped, Partial };

static HRESULT exerciseComputeShader(ID3D12Device *Device,
                                     ID3D12CommandQueue *Queue,
                                     const wchar_t *Path, UINT Space,
                                     ComputeWorkload Workload) {
  using namespace feme::probe;
  bool Grouped = Workload == ComputeWorkload::Grouped;
  bool Partial = Workload == ComputeWorkload::Partial;
  ComputeDispatch Dispatch =
      Partial ? PartialDispatch : computeDispatch(Grouped);
  WavePolicy Policy{};
  HRESULT Result;
  if (Partial) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 Options{};
    Result = report(L"CheckFeatureSupport(D3D12_OPTIONS1)",
                    Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1,
                                                &Options, sizeof(Options)));
    if (FAILED(Result))
      return Result;
    Policy = {Options.WaveOps != FALSE, Options.WaveLaneCountMin,
              Options.WaveLaneCountMax};
    std::wprintf(L"Runtime wave policy: WaveOps=%u lanes=%u..%u\n",
                 unsigned(Policy.WaveOps), Policy.Min, Policy.Max);
    if (!Policy.WaveOps || !validWaveSize(Policy.Min) ||
        !validWaveSize(Policy.Max) || Policy.Min > Policy.Max)
      return report(L"Unsupported runtime wave policy", E_FAIL);
  }
  std::vector<std::uint8_t> Code;
  Result = readShader(Path, Code);
  if (FAILED(Result))
    return Result;
  D3D12_ROOT_PARAMETER Parameter{};
  Parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  Parameter.Descriptor.ShaderRegister = 3;
  Parameter.Descriptor.RegisterSpace = Space;
  Parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC SignatureDesc{};
  SignatureDesc.NumParameters = 1;
  SignatureDesc.pParameters = &Parameter;
  ComPtr<ID3DBlob> Serialized, Errors;
  Result = report(L"D3D12SerializeRootSignature",
                  D3D12SerializeRootSignature(&SignatureDesc,
                                              D3D_ROOT_SIGNATURE_VERSION_1,
                                              &Serialized, &Errors));
  if (FAILED(Result)) {
    if (Errors)
      std::fwrite(Errors->GetBufferPointer(), 1, Errors->GetBufferSize(),
                  stderr);
    return Result;
  }
  ComPtr<ID3D12RootSignature> Signature;
  Result = report(L"CreateRootSignature",
                  Device->CreateRootSignature(0, Serialized->GetBufferPointer(),
                                              Serialized->GetBufferSize(),
                                              IID_PPV_ARGS(&Signature)));
  if (FAILED(Result))
    return Result;
  D3D12_COMPUTE_PIPELINE_STATE_DESC PipelineDesc{};
  PipelineDesc.pRootSignature = Signature.Get();
  PipelineDesc.CS = {Code.data(), Code.size()};
  ComPtr<ID3D12PipelineState> Pipeline;
  Result = report(L"CreateComputePipelineState",
                  Device->CreateComputePipelineState(&PipelineDesc,
                                                     IID_PPV_ARGS(&Pipeline)));
  if (FAILED(Result))
    return Result;

  ComPtr<ID3D12Heap> Heap;
  D3D12_HEAP_DESC HeapDesc{};
  HeapDesc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  HeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  HeapDesc.Properties.CreationNodeMask = HeapDesc.Properties.VisibleNodeMask =
      1;
  HeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  Result = report(L"CreateHeap(compute buffer)",
                  Device->CreateHeap(&HeapDesc, IID_PPV_ARGS(&Heap)));
  if (FAILED(Result))
    return Result;
  ComPtr<ID3D12Resource> Buffer, Upload, Readback;
  D3D12_RESOURCE_DESC Desc = bufferDesc(BufferBytes);
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  Result = report(L"CreatePlacedResource(compute buffer)",
                  Device->CreatePlacedResource(Heap.Get(), 0, &Desc,
                                               D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr, IID_PPV_ARGS(&Buffer)));
  if (FAILED(Result))
    return Result;
  Result =
      createCpuBuffer(Device, D3D12_HEAP_TYPE_UPLOAD, BufferBytes,
                      D3D12_RESOURCE_STATE_GENERIC_READ, Upload.GetAddressOf());
  if (FAILED(Result))
    return Result;
  Result =
      createCpuBuffer(Device, D3D12_HEAP_TYPE_READBACK, BufferBytes,
                      D3D12_RESOURCE_STATE_COPY_DEST, Readback.GetAddressOf());
  if (FAILED(Result))
    return Result;
  void *Mapped = nullptr;
  D3D12_RANGE NoAccess{0, 0};
  Result = report(L"Map(compute upload)", Upload->Map(0, &NoAccess, &Mapped));
  if (FAILED(Result))
    return Result;
  if (!Mapped) {
    Upload->Unmap(0, &NoAccess);
    return report(L"Missing compute upload mapping", E_FAIL);
  }
  for (std::size_t I = 0; I < BufferBytes; ++I)
    static_cast<std::uint8_t *>(Mapped)[I] = baselineByte(I);
  D3D12_RANGE FullRange{0, BufferBytes};
  Upload->Unmap(0, &FullRange);

  ComPtr<ID3D12CommandAllocator> Allocator;
  Result =
      report(L"CreateCommandAllocator(compute)",
             Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                            IID_PPV_ARGS(&Allocator)));
  if (FAILED(Result))
    return Result;
  ComPtr<ID3D12GraphicsCommandList> Commands;
  Result = report(L"CreateCommandList(compute)",
                  Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                            Allocator.Get(), Pipeline.Get(),
                                            IID_PPV_ARGS(&Commands)));
  if (FAILED(Result))
    return Result;
  D3D12_RESOURCE_BARRIER Barrier{};
  Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  Barrier.Transition.pResource = Buffer.Get();
  Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  UINT Iterations = Partial ? 3 : 1;
  for (UINT Iteration = 0; Iteration < Iterations; ++Iteration) {
    if (Iteration != 0) {
      Result = report(L"Reset(compute allocator)", Allocator->Reset());
      if (FAILED(Result))
        return Result;
      Result = report(L"Reset(compute list)",
                      Commands->Reset(Allocator.Get(), Pipeline.Get()));
      if (FAILED(Result))
        return Result;
      // Buffers decay to COMMON after the previous batch completes.
      Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
      Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
      Commands->ResourceBarrier(1, &Barrier);
    }
    // Reinitialize every time: old output must not validate a missing dispatch.
    Commands->CopyBufferRegion(Buffer.Get(), 0, Upload.Get(), 0, BufferBytes);
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    Commands->ResourceBarrier(1, &Barrier);
    Commands->SetComputeRootSignature(Signature.Get());
    Commands->SetComputeRootUnorderedAccessView(0,
                                                Buffer->GetGPUVirtualAddress());
    Commands->Dispatch(Dispatch.X, Dispatch.Y, Dispatch.Z);
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    Commands->ResourceBarrier(1, &Barrier);
    Commands->CopyBufferRegion(Readback.Get(), 0, Buffer.Get(), 0, BufferBytes);
    Result = report(L"Close(compute list)", Commands->Close());
    if (FAILED(Result))
      return Result;
    bool Submitted = false;
    Result = signalAndWait(Device, Queue, Commands.Get(), Submitted);
    if (FAILED(Result)) {
      if (Submitted) {
        std::wprintf(L"Retaining compute resources until process exit after "
                     L"completion/teardown failure.\n");
        Device->AddRef();
        Queue->AddRef();
        Signature.Detach();
        Pipeline.Detach();
        Heap.Detach();
        Buffer.Detach();
        Upload.Detach();
        Readback.Detach();
        Allocator.Detach();
        Commands.Detach();
      }
      return Result;
    }
    Mapped = nullptr;
    Result =
        report(L"Map(compute readback)", Readback->Map(0, &FullRange, &Mapped));
    if (FAILED(Result))
      return Result;
    WaveObservation Observed;
    const auto *Data = static_cast<const std::uint8_t *>(Mapped);
    bool Valid = Partial
                     ? validatePartialWave(Data, BufferBytes, Policy, Observed)
                     : validateComputeShader(Data, BufferBytes, Grouped);
    Readback->Unmap(0, &NoAccess);
    if (!Valid)
      return report(L"Compute shader validation", E_FAIL);
    if (Partial)
      std::wprintf(
          L"Partial-wave iteration %u/%u verified: observed-lanes=%u..%u "
          L"waves=%zu partial-waves=%zu records=%zu guard-bytes=%zu\n",
          Iteration + 1, Iterations, Observed.Min, Observed.Max, Observed.Waves,
          Observed.PartialWaves, PartialRecords,
          BufferBytes - PartialOutputBytes);
  }
  std::wprintf(L"Compute shader verified: values=%zu bytes=%zu groups=%zu "
               L"threads=%zu; uav-register=3 space=%u guard-bytes=%zu\n",
               Dispatch.Values, BufferBytes,
               std::size_t(Dispatch.X) * Dispatch.Y * Dispatch.Z,
               Dispatch.Threads, Space,
               BufferBytes - Dispatch.Values * sizeof(std::uint32_t));
  if (Grouped)
    std::wprintf(L"Grouped compute verified: groups=2,2,2 threads=8,4,2.\n");
  if (Partial)
    std::wprintf(L"Partial-wave compute verified: groups=3,2,1 threads=5,3,2 "
                 L"iterations=3.\n");
  return S_OK;
}

static HRESULT
exerciseAdapter(IDXGIAdapter1 *Adapter, bool BufferCopy = false,
                const wchar_t *ShaderPath = nullptr, UINT ShaderSpace = 0,
                ComputeWorkload Workload = ComputeWorkload::Linear) {
  DXGI_ADAPTER_DESC1 Desc{};
  HRESULT Result = report(L"GetDesc1", Adapter->GetDesc1(&Desc));
  if (FAILED(Result))
    return Result;
  std::wprintf(L"Adapter: %ls; vendor=%04x device=%04x flags=%x "
               L"LUID=%08lx:%08lx\n",
               Desc.Description, Desc.VendorId, Desc.DeviceId, Desc.Flags,
               static_cast<unsigned long>(Desc.AdapterLuid.HighPart),
               Desc.AdapterLuid.LowPart);
  ComPtr<ID3D12Device> Device;
  Result = report(L"D3D12CreateDevice(11_0)",
                  D3D12CreateDevice(Adapter, D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&Device)));
  reportModule(L"d3d12.dll");
  reportModule(L"D3D12Core.dll");
  reportModule(L"d3d10warp.dll");
  if (FAILED(Result))
    return Result;
  LUID DeviceLuid = Device->GetAdapterLuid();
  if (DeviceLuid.HighPart != Desc.AdapterLuid.HighPart ||
      DeviceLuid.LowPart != Desc.AdapterLuid.LowPart)
    return report(L"Device/adapter LUID mismatch", E_FAIL);
  D3D12_COMMAND_QUEUE_DESC QueueDesc{};
  QueueDesc.Type = BufferCopy ? D3D12_COMMAND_LIST_TYPE_COPY
                              : D3D12_COMMAND_LIST_TYPE_COMPUTE;
  ComPtr<ID3D12CommandQueue> Queue;
  Result = report(L"CreateCommandQueue",
                  Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue)));
  if (FAILED(Result))
    return Result;
  if (BufferCopy)
    return exerciseBufferCopy(Device.Get(), Queue.Get());
  if (ShaderPath)
    return exerciseComputeShader(Device.Get(), Queue.Get(), ShaderPath,
                                 ShaderSpace, Workload);
  bool Submitted = false;
  return signalAndWait(Device.Get(), Queue.Get(), nullptr, Submitted);
}

int wmain(int Argc, wchar_t **Argv) {
  bool WarpBufferCopy =
      Argc == 2 && std::wcscmp(Argv[1], L"--warp-buffer-copy") == 0;
  bool SoftwareBufferCopy =
      Argc == 3 && std::wcscmp(Argv[1], L"--software-buffer-copy") == 0;
  bool BufferCopy = WarpBufferCopy || SoftwareBufferCopy;
  bool WarpCompute = (Argc == 4 || Argc == 5) &&
                     std::wcscmp(Argv[1], L"--warp-compute-shader") == 0;
  bool SoftwareCompute =
      (Argc == 5 || Argc == 6) &&
      std::wcscmp(Argv[1], L"--software-compute-shader") == 0;
  const wchar_t *Workload = WarpCompute && Argc == 5       ? Argv[4]
                            : SoftwareCompute && Argc == 6 ? Argv[5]
                                                           : nullptr;
  bool Grouped = Workload && std::wcscmp(Workload, L"grouped") == 0;
  bool Partial = Workload && std::wcscmp(Workload, L"partial") == 0;
  ComputeWorkload SelectedWorkload = Partial   ? ComputeWorkload::Partial
                                     : Grouped ? ComputeWorkload::Grouped
                                               : ComputeWorkload::Linear;
  bool ValidWorkload =
      !Workload || Grouped || Partial || std::wcscmp(Workload, L"linear") == 0;
  const wchar_t *ShaderPath = WarpCompute       ? Argv[2]
                              : SoftwareCompute ? Argv[3]
                                                : nullptr;
  const wchar_t *SpaceArg = WarpCompute       ? Argv[3]
                            : SoftwareCompute ? Argv[4]
                                              : nullptr;
  bool ValidSpace = !SpaceArg || std::wcscmp(SpaceArg, L"0") == 0 ||
                    std::wcscmp(SpaceArg, L"7") == 0;
  UINT ShaderSpace = SpaceArg && std::wcscmp(SpaceArg, L"7") == 0 ? 7 : 0;
  bool Stub = Argc == 3 && std::wcscmp(Argv[1], L"--stub") == 0;
  bool Software = (Argc == 3 && std::wcscmp(Argv[1], L"--software") == 0) ||
                  SoftwareBufferCopy || SoftwareCompute;
  bool WarpShim = Argc == 3 && std::wcscmp(Argv[1], L"--warp-shim") == 0;
  bool WarpBootstrap =
      Argc == 3 && std::wcscmp(Argv[1], L"--warp-bootstrap") == 0;
  bool WarpBootstrapAudit =
      (Argc == 3 || Argc == 4) &&
      std::wcscmp(Argv[1], L"--warp-bootstrap-audit") == 0;
  bool ExpectCreateDevice =
      WarpBootstrapAudit && Argc == 4 &&
      std::wcscmp(Argv[3], L"--expect-create-device") == 0;
  bool ExpectUnsupported = WarpBootstrapAudit && Argc == 4 &&
                           std::wcscmp(Argv[3], L"--expect-unsupported") == 0;
  bool ExpectCallback = WarpBootstrapAudit && Argc == 4 &&
                        std::wcscmp(Argv[3], L"--expect-callback") == 0;
  bool ExpectPagingQueue = WarpBootstrapAudit && Argc == 4 &&
                           std::wcscmp(Argv[3], L"--expect-paging-queue") == 0;
  bool ExpectGpuVa = WarpBootstrapAudit && Argc == 4 &&
                     std::wcscmp(Argv[3], L"--expect-gpu-va") == 0;
  bool ExpectGpuVaRejected =
      WarpBootstrapAudit && Argc == 4 &&
      std::wcscmp(Argv[3], L"--expect-gpu-va-rejected") == 0;
  bool ExpectMonitoredFence =
      WarpBootstrapAudit && Argc == 4 &&
      std::wcscmp(Argv[3], L"--expect-monitored-fence") == 0;
  bool ExpectWarpPrivateInfo =
      WarpBootstrapAudit && Argc == 4 &&
      std::wcscmp(Argv[3], L"--expect-warp-private-info") == 0;
  bool ExpectWarpContext = WarpBootstrapAudit && Argc == 4 &&
                           std::wcscmp(Argv[3], L"--expect-warp-context") == 0;
  bool ExpectWarpDeviceContext =
      WarpBootstrapAudit && Argc == 4 &&
      std::wcscmp(Argv[3], L"--expect-warp-device-context") == 0;
  ExpectWarpPrivateInfo |= ExpectWarpContext;
  ExpectCreateDevice |= ExpectCallback || ExpectPagingQueue || ExpectGpuVa ||
                        ExpectGpuVaRejected || ExpectMonitoredFence ||
                        ExpectWarpPrivateInfo || ExpectWarpContext ||
                        ExpectWarpDeviceContext;
  if ((Argc != 1 && !Stub && !Software && !WarpShim && !WarpBootstrap &&
       !WarpBootstrapAudit && !WarpBufferCopy && !WarpCompute) ||
      !ValidSpace || !ValidWorkload ||
      (WarpBootstrapAudit && Argc == 4 && !ExpectCreateDevice &&
       !ExpectUnsupported)) {
    std::fwprintf(
        stderr,
        L"Usage: %ls [--warp-buffer-copy | --software-buffer-copy DLL | "
        L"--warp-compute-shader SHADER {0|7} [linear|grouped|partial] | "
        L"--software-compute-shader DLL SHADER {0|7} [linear|grouped|partial] "
        L"| "
        L"--software DLL | --stub DLL | --warp-shim DLL | "
        L"--warp-bootstrap DLL | --warp-bootstrap-audit DLL "
        L"[--expect-create-device | --expect-unsupported | "
        L"--expect-callback | --expect-paging-queue | --expect-gpu-va | "
        L"--expect-gpu-va-rejected | --expect-monitored-fence | "
        L"--expect-warp-private-info | --expect-warp-context | "
        L"--expect-warp-device-context]]\n",
        Argv[0]);
    return 1;
  }
  if (ExpectCreateDevice || ExpectUnsupported)
    std::wprintf(L"Explicit bootstrap expectation: %ls\n", Argv[3]);
  ComPtr<IDXGIFactory4> Factory;
  if (FAILED(report(L"CreateDXGIFactory1",
                    CreateDXGIFactory1(IID_PPV_ARGS(&Factory)))))
    return 1;
  ComPtr<IDXGIAdapter1> Adapter;
  if (Argc == 1 || WarpBufferCopy || WarpCompute) {
    if (FAILED(report(L"EnumWarpAdapter",
                      Factory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter)))))
      return 1;
    if (FAILED(checkWarpIdentity(Adapter.Get())))
      return 1;
    HRESULT Result = exerciseAdapter(Adapter.Get(), BufferCopy, ShaderPath,
                                     ShaderSpace, SelectedWorkload);
    Adapter.Reset();
    Factory.Reset();
    std::wprintf(
        WarpCompute
            ? L"Reference WARP compute-shader probe finished; not an owned "
              L"FeMe device.\n"
        : BufferCopy
            ? L"Reference WARP buffer-copy probe finished; not FeMe coverage.\n"
            : L"Reference WARP objects released; not FeMe coverage.\n");
    return FAILED(Result) ? 1 : 0;
  }

  // An absolute path and restricted dependency search avoid substituting
  // binaries from the working directory into this diagnostic.
  wchar_t FullPath[32768];
  DWORD Length =
      GetFullPathNameW(Argv[2], ARRAYSIZE(FullPath), FullPath, nullptr);
  if (!Length || Length >= ARRAYSIZE(FullPath)) {
    report(L"GetFullPathNameW", Length
                                    ? HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW)
                                    : HRESULT_FROM_WIN32(GetLastError()));
    return 1;
  }
  std::wprintf(L"Requested software module: %ls\n", FullPath);
  HMODULE Module = LoadLibraryExW(FullPath, nullptr,
                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                      LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!Module) {
    report(L"LoadLibraryExW", HRESULT_FROM_WIN32(GetLastError()));
    return 1;
  }
  using GetCalls = void(APIENTRY *)(LONG *, LONG *, LONG *);
  using GetBootstrapCalls = void(APIENTRY *)(
      LONG *, LONG *, LONG *, LONG *, LONG *, LONG *, LONG *, LONG *, LONG *);
  using GetAuditCapacity = UINT32(APIENTRY *)();
  using GetAudit = void(APIENTRY *)(LONG *, LONG *, UINT *, UINT *, LONG *,
                                    LONG *, LONG *, LONG *, LONG *);
  using GetCallbackProbe = void(APIENTRY *)(LONG *, LONG *, LONG *, DWORD *);
  using GetContractAudit =
      void(APIENTRY *)(feme::probe::BootstrapContractAudit *);
  using GetPagingQueueProbe = void(APIENTRY *)(feme::probe::PagingQueueProbe *);
  using GetGpuVirtualAddressProbe =
      void(APIENTRY *)(feme::probe::GpuVirtualAddressProbe *);
  using GetMonitoredFenceProbe =
      void(APIENTRY *)(feme::probe::MonitoredFenceProbe *);
  using GetWarpPrivateAdapterInfoProbe =
      void(APIENTRY *)(feme::probe::WarpPrivateAdapterInfoProbe *);
  using GetWarpContextProbe = void(APIENTRY *)(feme::probe::WarpContextProbe *);
  using GetWarpDeviceContextProbe =
      void(APIENTRY *)(feme::probe::WarpDeviceContextProbe *);
  GetCalls ReadCalls = nullptr;
  GetBootstrapCalls ReadBootstrapCalls = nullptr;
  GetAuditCapacity ReadAuditCapacity = nullptr;
  GetAudit ReadAudit = nullptr;
  GetCallbackProbe ReadCallbackProbe = nullptr;
  GetContractAudit ReadContractAudit = nullptr;
  GetPagingQueueProbe ReadPagingQueueProbe = nullptr;
  GetGpuVirtualAddressProbe ReadGpuVirtualAddressProbe = nullptr;
  GetMonitoredFenceProbe ReadMonitoredFenceProbe = nullptr;
  GetWarpPrivateAdapterInfoProbe ReadWarpPrivateAdapterInfoProbe = nullptr;
  GetWarpContextProbe ReadWarpContextProbe = nullptr;
  GetWarpDeviceContextProbe ReadWarpDeviceContextProbe = nullptr;
  if (Stub || WarpShim) {
    ReadCalls =
        reinterpret_cast<GetCalls>(GetProcAddress(Module, "FeMeProbeGetCalls"));
    if (!ReadCalls) {
      report(L"GetProcAddress(FeMeProbeGetCalls)",
             HRESULT_FROM_WIN32(GetLastError()));
      FreeLibrary(Module);
      return 1;
    }
  }
  if (WarpBootstrap || WarpBootstrapAudit) {
    ReadBootstrapCalls = reinterpret_cast<GetBootstrapCalls>(
        GetProcAddress(Module, "FeMeBootstrapGetCalls"));
    if (!ReadBootstrapCalls) {
      report(L"GetProcAddress(FeMeBootstrapGetCalls)",
             HRESULT_FROM_WIN32(GetLastError()));
      FreeLibrary(Module);
      return 1;
    }
  }
  if (WarpBootstrapAudit) {
    ReadAuditCapacity = reinterpret_cast<GetAuditCapacity>(
        GetProcAddress(Module, "FeMeBootstrapGetAuditCapacity"));
    ReadAudit = reinterpret_cast<GetAudit>(
        GetProcAddress(Module, "FeMeBootstrapGetAudit"));
    if (!ReadAuditCapacity || !ReadAudit) {
      report(L"GetProcAddress(FeMeBootstrapGetAudit)",
             HRESULT_FROM_WIN32(GetLastError()));
      FreeLibrary(Module);
      return 1;
    }
    // W0(g) follow-up: optional, since older-shaped shims may not export it.
    ReadCallbackProbe = reinterpret_cast<GetCallbackProbe>(
        GetProcAddress(Module, "FeMeBootstrapGetCallbackProbe"));
    ReadContractAudit = reinterpret_cast<GetContractAudit>(
        GetProcAddress(Module, "FeMeBootstrapGetContractAudit"));
    ReadPagingQueueProbe = reinterpret_cast<GetPagingQueueProbe>(
        GetProcAddress(Module, "FeMeBootstrapGetPagingQueueProbe"));
    ReadGpuVirtualAddressProbe = reinterpret_cast<GetGpuVirtualAddressProbe>(
        GetProcAddress(Module, "FeMeBootstrapGetGpuVirtualAddressProbe"));
    ReadMonitoredFenceProbe = reinterpret_cast<GetMonitoredFenceProbe>(
        GetProcAddress(Module, "FeMeBootstrapGetMonitoredFenceProbe"));
    ReadWarpPrivateAdapterInfoProbe =
        reinterpret_cast<GetWarpPrivateAdapterInfoProbe>(GetProcAddress(
            Module, "FeMeBootstrapGetWarpPrivateAdapterInfoProbe"));
    ReadWarpContextProbe = reinterpret_cast<GetWarpContextProbe>(
        GetProcAddress(Module, "FeMeBootstrapGetWarpContextProbe"));
    ReadWarpDeviceContextProbe = reinterpret_cast<GetWarpDeviceContextProbe>(
        GetProcAddress(Module, "FeMeBootstrapGetWarpDeviceContextProbe"));
    if (ExpectMonitoredFence && !ReadMonitoredFenceProbe) {
      report(L"GetProcAddress(FeMeBootstrapGetMonitoredFenceProbe)",
             HRESULT_FROM_WIN32(GetLastError()));
      FreeLibrary(Module);
      return 1;
    }
    if (ExpectWarpPrivateInfo && !ReadWarpPrivateAdapterInfoProbe) {
      report(L"GetProcAddress(FeMeBootstrapGetWarpPrivateAdapterInfoProbe)",
             HRESULT_FROM_WIN32(GetLastError()));
      FreeLibrary(Module);
      return 1;
    }
    if (ExpectWarpContext && !ReadWarpContextProbe) {
      report(L"GetProcAddress(FeMeBootstrapGetWarpContextProbe)",
             HRESULT_FROM_WIN32(GetLastError()));
      FreeLibrary(Module);
      return 1;
    }
    if (ExpectWarpDeviceContext && !ReadWarpDeviceContextProbe) {
      report(L"GetProcAddress(FeMeBootstrapGetWarpDeviceContextProbe)",
             HRESULT_FROM_WIN32(GetLastError()));
      FreeLibrary(Module);
      return 1;
    }
    if ((ExpectGpuVa || ExpectGpuVaRejected) && !ReadGpuVirtualAddressProbe) {
      report(L"GetProcAddress(FeMeBootstrapGetGpuVirtualAddressProbe)",
             HRESULT_FROM_WIN32(GetLastError()));
      FreeLibrary(Module);
      return 1;
    }
    if (ExpectPagingQueue && !ReadPagingQueueProbe) {
      report(L"GetProcAddress(FeMeBootstrapGetPagingQueueProbe)",
             HRESULT_FROM_WIN32(GetLastError()));
      FreeLibrary(Module);
      return 1;
    }
    if ((ExpectCreateDevice || ExpectUnsupported) && !ReadContractAudit) {
      report(L"GetProcAddress(FeMeBootstrapGetContractAudit)",
             HRESULT_FROM_WIN32(GetLastError()));
      FreeLibrary(Module);
      return 1;
    }
    if (ExpectCallback && !ReadCallbackProbe) {
      report(L"GetProcAddress(FeMeBootstrapGetCallbackProbe)",
             HRESULT_FROM_WIN32(GetLastError()));
      FreeLibrary(Module);
      return 1;
    }
  }
  ComPtr<IDXGIAdapter> SoftwareAdapter;
  HRESULT Result =
      WarpShim || WarpBootstrap || WarpBootstrapAudit
          ? report(L"EnumWarpAdapter(shim)",
                   Factory->EnumWarpAdapter(IID_PPV_ARGS(&SoftwareAdapter)))
          : report(L"CreateSoftwareAdapter",
                   Factory->CreateSoftwareAdapter(Module, &SoftwareAdapter));
  bool Opened = false;
  if (SUCCEEDED(Result)) {
    Result =
        report(L"QueryInterface(IDXGIAdapter1)", SoftwareAdapter.As(&Adapter));
    if (SUCCEEDED(Result) && (WarpShim || WarpBootstrap || WarpBootstrapAudit ||
                              SoftwareBufferCopy || SoftwareCompute))
      Result = checkWarpIdentity(Adapter.Get());
    if (SUCCEEDED(Result)) {
      Opened = true;
      Result = exerciseAdapter(Adapter.Get(), BufferCopy, ShaderPath,
                               ShaderSpace, SelectedWorkload);
      if ((SoftwareBufferCopy || SoftwareCompute) && SUCCEEDED(Result) &&
          GetModuleHandleW(L"d3d10warp.dll") != Module)
        Result = report(L"Requested/selected WARP module mismatch", E_FAIL);
    }
  }
  if (WarpShim || WarpBootstrap || WarpBootstrapAudit)
    reportModule(L"feme_warp_reference.dll");
  Adapter.Reset();
  SoftwareAdapter.Reset();
  Factory.Reset();
  bool ExpectedDDICalls = true;
  if (ReadCalls) {
    LONG Count10, Count10_2, Count12;
    ReadCalls(&Count10, &Count10_2, &Count12);
    std::wprintf(L"Stub calls: OpenAdapter10=%ld OpenAdapter10_2=%ld "
                 L"OpenAdapter12=%ld\n",
                 Count10, Count10_2, Count12);
    ExpectedDDICalls = Count10 == 0 && Count10_2 == 0 &&
                       (WarpShim ? Count12 == 1 : Count12 == 0);
    if (!ExpectedDDICalls)
      report(L"Stub rejection stage changed", E_FAIL);
  }
  if (ReadBootstrapCalls) {
    LONG Open, Size, Versions, Caps, Optional, Fill, Create, Close, Destroy;
    ReadBootstrapCalls(&Open, &Size, &Versions, &Caps, &Optional, &Fill,
                       &Create, &Close, &Destroy);
    std::wprintf(L"Bootstrap calls: Open=%ld Size=%ld Versions=%ld Caps=%ld "
                 L"Optional=%ld Fill=%ld Create=%ld Close=%ld Destroy=%ld\n",
                 Open, Size, Versions, Caps, Optional, Fill, Create, Close,
                 Destroy);
    if (WarpBootstrapAudit) {
      // A pinned version is advertised, so the runtime may or may not reach
      // CreateDevice; that count is audit evidence here, not an assumption.
      // Destroy should still never fire, since createDevice deliberately
      // fails and no device handle is ever produced to destroy.
      ExpectedDDICalls =
          Open == 1 && Versions > 0 && Caps > 0 && Close == 1 &&
          (ExpectWarpDeviceContext ? Destroy == 1 : Destroy == 0);
      if (ExpectWarpDeviceContext) {
        ExpectedDDICalls &= Size == 1 && Create == 1 && Optional == 0 &&
                            Fill == 0 &&
                            Result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
      } else if (ExpectCreateDevice) {
        HRESULT ExpectedResult = ExpectCallback ? DXGI_ERROR_DEVICE_REMOVED
                                 : ExpectGpuVaRejected ? E_INVALIDARG
                                                       : E_NOTIMPL;
        ExpectedDDICalls &= Size == 1 && Create == 1 && Optional == 0 &&
                            Fill == 0 && Result == ExpectedResult;
      }
      if (ExpectUnsupported)
        ExpectedDDICalls &= Size == 0 && Create == 0 && Optional == 0 &&
                            Fill == 0 && Result == DXGI_ERROR_UNSUPPORTED;
    } else {
      ExpectedDDICalls = Open == 1 && Versions > 0 && Caps > 0 && Close == 1 &&
                         Size == 0 && Optional == 0 && Fill == 0 &&
                         Create == 0 && Destroy == 0;
    }
    if (GetModuleHandleW(L"feme_warp_reference.dll")) {
      report(L"Unexpected reference WARP dependency", E_FAIL);
      ExpectedDDICalls = false;
    }
    if ((ExpectCreateDevice || ExpectUnsupported) &&
        GetModuleHandleW(L"d3d10warp.dll") != Module) {
      report(L"Selected WARP module does not match the owned audit module",
             E_FAIL);
      ExpectedDDICalls = false;
    }
    if (!ExpectedDDICalls)
      report(L"Bootstrap negotiation/cleanup mismatch", E_FAIL);
  }
  if (ReadAudit) {
    UINT32 Capacity = ReadAuditCapacity();
    std::vector<LONG> CapsTypes(Capacity);
    LONG CapsTypeCount = 0;
    UINT CreateInterface = 0, CreateVersion = 0;
    LONG CreateHasKTCallbacks = 0, CreateHasUMCallbacks = 0;
    LONG CreateHasKTAllocateCb = 0, CreateHasKTDeallocateCb = 0,
         CreateHasUMSetErrorCb = 0;
    ReadAudit(&CapsTypeCount, CapsTypes.data(), &CreateInterface,
              &CreateVersion, &CreateHasKTCallbacks, &CreateHasUMCallbacks,
              &CreateHasKTAllocateCb, &CreateHasKTDeallocateCb,
              &CreateHasUMSetErrorCb);
    std::wprintf(L"GetCaps Type values queried (%ld total):", CapsTypeCount);
    UINT32 Logged = CapsTypeCount < 0
                        ? 0
                        : (static_cast<UINT32>(CapsTypeCount) < Capacity
                               ? static_cast<UINT32>(CapsTypeCount)
                               : Capacity);
    for (UINT32 I = 0; I < Logged; ++I)
      std::wprintf(L" %ld", CapsTypes[I]);
    std::wprintf(L"\n");
    std::wprintf(L"CreateDevice args: Interface=%u Version=%u "
                 L"pKTCallbacks=%ls p12UMCallbacks=%ls\n",
                 CreateInterface, CreateVersion,
                 CreateHasKTCallbacks ? L"non-null" : L"null",
                 CreateHasUMCallbacks ? L"non-null" : L"null");
    std::wprintf(L"CreateDevice inner callback pointers (read-only, never "
                 L"invoked unless noted): pKTCallbacks->pfnAllocateCb=%ls "
                 L"pKTCallbacks->pfnDeallocateCb=%ls "
                 L"p12UMCallbacks->pfnSetErrorCb=%ls\n",
                 CreateHasKTAllocateCb ? L"non-null" : L"null",
                 CreateHasKTDeallocateCb ? L"non-null" : L"null",
                 CreateHasUMSetErrorCb ? L"non-null" : L"null");
    if (ReadContractAudit) {
      feme::probe::BootstrapContractAudit Audit;
      ReadContractAudit(&Audit);
      std::uint64_t NegotiatedVersion =
          (static_cast<std::uint64_t>(CreateInterface) << 32) | CreateVersion;
      std::wprintf(L"Pinned DDI: 0x%016llx; CreateDevice DDI: 0x%016llx\n",
                   static_cast<unsigned long long>(Audit.PinnedVersion),
                   static_cast<unsigned long long>(NegotiatedVersion));
      std::wprintf(
          L"Modern kernel callback pointers (invocations reported separately): "
          L"MakeResident=%u Evict=%u CreatePagingQueue=%u "
          L"DestroyPagingQueue=%u "
          L"MapGpuVirtualAddress=%u Deallocate2=%u\n",
          unsigned(Audit.HasKTMakeResidentCb), unsigned(Audit.HasKTEvictCb),
          unsigned(Audit.HasKTCreatePagingQueueCb),
          unsigned(Audit.HasKTDestroyPagingQueueCb),
          unsigned(Audit.HasKTMapGpuVirtualAddressCb),
          unsigned(Audit.HasKTDeallocate2Cb));
      std::wprintf(
          L"Core-layer kernel-service pointers (read-only, never invoked): "
          L"CreatePagingQueue=%u DestroyPagingQueue=%u MakeResident=%u "
          L"Evict=%u "
          L"Allocate0022=%u Deallocate0022=%u\n",
          unsigned(Audit.HasUMCreatePagingQueueCb),
          unsigned(Audit.HasUMDestroyPagingQueueCb),
          unsigned(Audit.HasUMMakeResidentCb), unsigned(Audit.HasUMEvictCb),
          unsigned(Audit.HasUMAllocateCb), unsigned(Audit.HasUMDeallocateCb));
      if (ExpectCreateDevice &&
          (!Audit.PinnedVersion || NegotiatedVersion != Audit.PinnedVersion ||
           !CreateHasKTCallbacks || !CreateHasUMCallbacks ||
           !CreateHasUMSetErrorCb)) {
        report(L"Expected pinned CreateDevice DDI and runtime callbacks",
               E_FAIL);
        ExpectedDDICalls = false;
      }
#ifdef FEME_PROBE_EXPECT_CORE_LAYER_ALLOCATION_CALLBACKS
      if (ExpectCreateDevice) {
        bool ExpectedKTAllocationCallbacks =
            FEME_PROBE_EXPECT_KT_ALLOCATION_CALLBACKS != 0;
        bool ExpectedAllocationCallbacks =
            FEME_PROBE_EXPECT_CORE_LAYER_ALLOCATION_CALLBACKS != 0;
        if ((CreateHasKTAllocateCb != 0) != ExpectedKTAllocationCallbacks ||
            (CreateHasKTDeallocateCb != 0) != ExpectedKTAllocationCallbacks ||
            Audit.HasUMAllocateCb != ExpectedAllocationCallbacks ||
            Audit.HasUMDeallocateCb != ExpectedAllocationCallbacks) {
          report(L"Allocation callbacks do not match negotiated DDI", E_FAIL);
          ExpectedDDICalls = false;
        }
      }
#endif
      if (ExpectUnsupported && (!Audit.PinnedVersion || NegotiatedVersion ||
                                CreateHasKTCallbacks || CreateHasUMCallbacks)) {
        report(L"Expected rejection before CreateDevice", E_FAIL);
        ExpectedDDICalls = false;
      }
    }
    if (ReadCallbackProbe) {
      LONG Attempted = 0, Completed = 0, ExceptionCaught = 0;
      DWORD ExceptionCode = 0;
      ReadCallbackProbe(&Attempted, &Completed, &ExceptionCaught,
                        &ExceptionCode);
      std::wprintf(L"Callback probe (p12UMCallbacks->pfnSetErrorCb): "
                   L"Attempted=%ld Completed=%ld ExceptionCaught=%ld "
                   L"ExceptionCode=0x%08lx\n",
                   Attempted, Completed, ExceptionCaught,
                   static_cast<unsigned long>(ExceptionCode));
      // Only a consistency check: if the probe ran, it must have either
      // completed or been caught by SEH, never neither (which would mean
      // the harness process itself did not survive to report this).
      if (Attempted && !Completed && !ExceptionCaught) {
        report(L"Callback probe ran but reported neither outcome", E_FAIL);
        ExpectedDDICalls = false;
      }
      if (ExpectCallback && (Attempted != 1 || Completed != 1 ||
                             ExceptionCaught || ExceptionCode)) {
        report(L"Expected successful user-mode callback probe", E_FAIL);
        ExpectedDDICalls = false;
      }
    }
    if (ReadPagingQueueProbe) {
      feme::probe::PagingQueueProbe Probe;
      ReadPagingQueueProbe(&Probe);
      std::wprintf(
          L"Kernel paging queue probe: Enabled=%u "
          L"CreateAttempted=%u CreateReturned=%u CreateResult=0x%08lx "
          L"HasPagingQueue=%u HasSyncObject=%u HasFenceMapping=%u "
          L"DestroyAttempted=%u DestroyReturned=%u DestroyResult=0x%08lx "
          L"ExceptionCode=0x%08lx\n",
          unsigned(Probe.Enabled), unsigned(Probe.CreateAttempted),
          unsigned(Probe.CreateReturned),
          static_cast<unsigned long>(Probe.CreateResult),
          unsigned(Probe.HasPagingQueue), unsigned(Probe.HasSyncObject),
          unsigned(Probe.HasFenceMapping), unsigned(Probe.DestroyAttempted),
          unsigned(Probe.DestroyReturned),
          static_cast<unsigned long>(Probe.DestroyResult),
          static_cast<unsigned long>(Probe.ExceptionCode));
      if (ExpectPagingQueue &&
          (!Probe.Enabled || !Probe.CreateAttempted || !Probe.CreateReturned ||
           Probe.CreateResult != S_OK || !Probe.HasPagingQueue ||
           !Probe.HasSyncObject || !Probe.HasFenceMapping ||
           !Probe.DestroyAttempted || !Probe.DestroyReturned ||
           Probe.DestroyResult != S_OK || Probe.ExceptionCode)) {
        report(L"Expected successful kernel paging queue create/destroy",
               E_FAIL);
        ExpectedDDICalls = false;
      }
    }
  }
  if (ReadGpuVirtualAddressProbe) {
    feme::probe::GpuVirtualAddressProbe Probe;
    ReadGpuVirtualAddressProbe(&Probe);
    std::wprintf(
        L"Kernel GPU VA probe: Enabled=%u "
        L"ReserveAttempted=%u ReserveReturned=%u ReserveResult=0x%08lx "
        L"Address=0x%016llx Bytes=%llu HasValidRange=%u "
        L"FreeAttempted=%u FreeReturned=%u FreeResult=0x%08lx "
        L"ExceptionCode=0x%08lx\n",
        unsigned(Probe.Enabled), unsigned(Probe.ReserveAttempted),
        unsigned(Probe.ReserveReturned),
        static_cast<unsigned long>(Probe.ReserveResult),
        static_cast<unsigned long long>(Probe.Address),
        static_cast<unsigned long long>(feme::probe::GpuVaReservationBytes),
        unsigned(Probe.HasValidRange), unsigned(Probe.FreeAttempted),
        unsigned(Probe.FreeReturned),
        static_cast<unsigned long>(Probe.FreeResult),
        static_cast<unsigned long>(Probe.ExceptionCode));
    if (ExpectGpuVa &&
        (!Probe.Enabled || !Probe.ReserveAttempted || !Probe.ReserveReturned ||
         Probe.ReserveResult != S_OK || !Probe.HasValidRange ||
         !Probe.FreeAttempted || !Probe.FreeReturned ||
         Probe.FreeResult != S_OK || Probe.ExceptionCode)) {
      report(L"Expected successful kernel GPU VA reserve/free", E_FAIL);
      ExpectedDDICalls = false;
    }
    if (ExpectGpuVaRejected &&
        (!Probe.Enabled || !Probe.ReserveAttempted || !Probe.ReserveReturned ||
         Probe.ReserveResult != E_INVALIDARG || Probe.Address ||
         Probe.HasValidRange || Probe.FreeAttempted || Probe.FreeReturned ||
         Probe.FreeResult != S_OK || Probe.ExceptionCode)) {
      report(L"Expected kernel GPU VA reservation rejection (E_INVALIDARG)",
             E_FAIL);
      ExpectedDDICalls = false;
    }
  }
  if (ReadMonitoredFenceProbe) {
    feme::probe::MonitoredFenceProbe Probe;
    ReadMonitoredFenceProbe(&Probe);
    std::wprintf(
        L"Kernel monitored fence probe: Enabled=%u "
        L"CreateAttempted=%u CreateReturned=%u CreateResult=0x%08lx "
        L"HasSyncObject=%u HasFenceMapping=%u InitialValue=%llu "
        L"WaitAttempted=%u WaitReturned=%u WaitResult=0x%08lx "
        L"WaitWasPending=%u SignalAttempted=%u SignalReturned=%u "
        L"SignalResult=0x%08lx EventCompleted=%u EventRetained=%u "
        L"CompletedValue=%llu DestroyAttempted=%u DestroyReturned=%u "
        L"DestroyResult=0x%08lx ExceptionCode=0x%08lx Win32Error=%lu\n",
        unsigned(Probe.Enabled), unsigned(Probe.CreateAttempted),
        unsigned(Probe.CreateReturned),
        static_cast<unsigned long>(Probe.CreateResult),
        unsigned(Probe.HasSyncObject), unsigned(Probe.HasFenceMapping),
        static_cast<unsigned long long>(Probe.InitialValue),
        unsigned(Probe.WaitAttempted), unsigned(Probe.WaitReturned),
        static_cast<unsigned long>(Probe.WaitResult),
        unsigned(Probe.WaitWasPending), unsigned(Probe.SignalAttempted),
        unsigned(Probe.SignalReturned),
        static_cast<unsigned long>(Probe.SignalResult),
        unsigned(Probe.EventCompleted), unsigned(Probe.EventRetained),
        static_cast<unsigned long long>(Probe.CompletedValue),
        unsigned(Probe.DestroyAttempted), unsigned(Probe.DestroyReturned),
        static_cast<unsigned long>(Probe.DestroyResult),
        static_cast<unsigned long>(Probe.ExceptionCode),
        static_cast<unsigned long>(Probe.Win32Error));
    if (Probe.EventRetained)
      std::wprintf(L"Retaining a potentially registered monitored-fence event "
                   L"until process exit after failure.\n");
    if (ExpectMonitoredFence &&
        (!Probe.Enabled || !Probe.CreateAttempted || !Probe.CreateReturned ||
         Probe.CreateResult != S_OK || !Probe.HasSyncObject ||
         !Probe.HasFenceMapping || Probe.InitialValue != 0 ||
         !Probe.WaitAttempted || !Probe.WaitReturned ||
         Probe.WaitResult != S_OK || !Probe.WaitWasPending ||
         !Probe.SignalAttempted || !Probe.SignalReturned ||
         Probe.SignalResult != S_OK || !Probe.EventCompleted ||
         Probe.EventRetained ||
         Probe.CompletedValue != feme::probe::MonitoredFenceTarget ||
         !Probe.DestroyAttempted || !Probe.DestroyReturned ||
         Probe.DestroyResult != S_OK || Probe.ExceptionCode ||
         Probe.Win32Error)) {
      report(L"Expected successful kernel monitored-fence lifecycle", E_FAIL);
      ExpectedDDICalls = false;
    }
    if (ReadWarpPrivateAdapterInfoProbe) {
      feme::probe::WarpPrivateAdapterInfoProbe WarpInfoProbe;
      ReadWarpPrivateAdapterInfoProbe(&WarpInfoProbe);
      std::wprintf(L"WARP private adapter info query: Attempted=%u "
                   L"Result=0x%08lx Version=%u WddmVersion=%u "
                   L"DriverCaps=0x%08x\n",
                   unsigned(WarpInfoProbe.Attempted),
                   static_cast<unsigned long>(WarpInfoProbe.Result),
                   WarpInfoProbe.Version, WarpInfoProbe.WddmVersion,
                   WarpInfoProbe.DriverCaps);
      if (ExpectWarpPrivateInfo &&
          (!WarpInfoProbe.Attempted || WarpInfoProbe.Result != S_OK ||
           !WarpInfoProbe.Version || !WarpInfoProbe.WddmVersion)) {
        report(L"Expected a valid WARP private adapter info response", E_FAIL);
        ExpectedDDICalls = false;
      }
    }
    if (ReadWarpContextProbe) {
      feme::probe::WarpContextProbe WarpContext;
      ReadWarpContextProbe(&WarpContext);
      std::wprintf(
          L"WARP private context lifecycle: CreateAttempted=%u "
          L"CreateReturned=%u CreateResult=0x%08lx HasContext=%u "
          L"CommandBufferAddress=0x%016llx CommandBufferSize=%llu "
          L"PrivateDataPointersIntact=%u DestroyAttempted=%u "
          L"DestroyReturned=%u DestroyResult=0x%08lx ExceptionCode=0x%08lx\n",
          unsigned(WarpContext.CreateAttempted),
          unsigned(WarpContext.CreateReturned),
          static_cast<unsigned long>(WarpContext.CreateResult),
          unsigned(WarpContext.HasContext),
          static_cast<unsigned long long>(WarpContext.CommandBufferAddress),
          static_cast<unsigned long long>(WarpContext.CommandBufferSize),
          unsigned(WarpContext.PrivateDataPointersIntact),
          unsigned(WarpContext.DestroyAttempted),
          unsigned(WarpContext.DestroyReturned),
          static_cast<unsigned long>(WarpContext.DestroyResult),
          static_cast<unsigned long>(WarpContext.ExceptionCode));
      if (ExpectWarpContext &&
          (!WarpContext.CreateAttempted || !WarpContext.CreateReturned ||
           WarpContext.CreateResult != S_OK || !WarpContext.HasContext ||
           !WarpContext.CommandBufferSize ||
           !WarpContext.PrivateDataPointersIntact ||
           !WarpContext.DestroyAttempted || !WarpContext.DestroyReturned ||
           WarpContext.DestroyResult != S_OK || WarpContext.ExceptionCode)) {
        report(L"Expected successful WARP context create/destroy lifecycle",
               E_FAIL);
        ExpectedDDICalls = false;
      }
    }
    if (ReadWarpDeviceContextProbe) {
      feme::probe::WarpDeviceContextProbe DeviceProbe;
      ReadWarpDeviceContextProbe(&DeviceProbe);
      std::wprintf(
          L"WARP device context: VersionNumberEscapeAttempted=%u "
          L"VersionNumberEscapeReturned=%u VersionNumberEscapeResult=0x%08lx "
          L"BasicRenderVersion=%u PreferredNodeEscapeAttempted=%u "
          L"PreferredNodeEscapeReturned=%u PreferredNodeEscapeResult=0x%08lx "
          L"PreferredNodeOrdinal=%u PreferredEngineAffinity=%u "
          L"PagingQueueCreateAttempted=%u "
          L"PagingQueueCreated=%u PagingQueueCreateResult=0x%08lx "
          L"PagingQueueHasSyncObject=%u PagingQueueHasFenceMapping=%u "
          L"CreateAttempted=%u ContextCreated=%u "
          L"CreateContextResult=0x%08lx CommandBufferAddress=0x%016llx "
          L"CommandBufferSize=%llu PrivateDataPointersIntact=%u "
          L"InitContextEscapeAttempted=%u InitContextEscapeReturned=%u "
          L"InitContextEscapeResult=0x%08lx KernelContextHandle=0x%016llx "
          L"DmaBufferAddress=0x%016llx DmaBufferSize=%u "
          L"DmaAllocationAttempted=%u DmaAllocationReturned=%u "
          L"DmaAllocationResult=0x%08lx DmaAllocationHandle=%u "
          L"DmaLockAttempted=%u DmaLockReturned=%u DmaLockResult=0x%08lx "
          L"DmaCpuAddress=0x%016llx DmaFenceCreateAttempted=%u "
          L"DmaFenceCreateReturned=%u DmaFenceCreateResult=0x%08lx "
          L"DmaFenceHandle=%u InitDeviceEscapeAttempted=%u "
          L"InitDeviceEscapeReturned=%u InitDeviceEscapeResult=0x%08lx "
          L"FillTableAttempted=%u FillTableType=%u FillTableSize=%llu "
          L"FillTableResult=0x%08lx PagingQueueDestroyAttempted=%u "
          L"PagingQueueDestroyReturned=%u "
          L"PagingQueueDestroyResult=0x%08lx DestroyAttempted=%u "
          L"DmaFenceDestroyAttempted=%u DmaFenceDestroyReturned=%u "
          L"DmaFenceDestroyResult=0x%08lx DmaUnlockAttempted=%u "
          L"DmaUnlockReturned=%u DmaUnlockResult=0x%08lx "
          L"DmaDeallocateAttempted=%u DmaDeallocateReturned=%u "
          L"DmaDeallocateResult=0x%08lx "
          L"DestroyReturned=%u DestroyResult=0x%08lx ExceptionCode=0x%08lx\n",
          unsigned(DeviceProbe.VersionNumberEscapeAttempted),
          unsigned(DeviceProbe.VersionNumberEscapeReturned),
          static_cast<unsigned long>(DeviceProbe.VersionNumberEscapeResult),
          DeviceProbe.BasicRenderVersion,
          unsigned(DeviceProbe.PreferredNodeEscapeAttempted),
          unsigned(DeviceProbe.PreferredNodeEscapeReturned),
          static_cast<unsigned long>(DeviceProbe.PreferredNodeEscapeResult),
          DeviceProbe.PreferredNodeOrdinal, DeviceProbe.PreferredEngineAffinity,
          unsigned(DeviceProbe.PagingQueueCreateAttempted),
          unsigned(DeviceProbe.PagingQueueCreated),
          static_cast<unsigned long>(DeviceProbe.PagingQueueCreateResult),
          unsigned(DeviceProbe.PagingQueueHasSyncObject),
          unsigned(DeviceProbe.PagingQueueHasFenceMapping),
          unsigned(DeviceProbe.CreateAttempted),
          unsigned(DeviceProbe.ContextCreated),
          static_cast<unsigned long>(DeviceProbe.CreateContextResult),
          static_cast<unsigned long long>(DeviceProbe.CommandBufferAddress),
          static_cast<unsigned long long>(DeviceProbe.CommandBufferSize),
          unsigned(DeviceProbe.PrivateDataPointersIntact),
          unsigned(DeviceProbe.InitContextEscapeAttempted),
          unsigned(DeviceProbe.InitContextEscapeReturned),
          static_cast<unsigned long>(DeviceProbe.InitContextEscapeResult),
          static_cast<unsigned long long>(DeviceProbe.KernelContextHandle),
          static_cast<unsigned long long>(DeviceProbe.DmaBufferAddress),
          DeviceProbe.DmaBufferSize,
          unsigned(DeviceProbe.DmaAllocationAttempted),
          unsigned(DeviceProbe.DmaAllocationReturned),
          static_cast<unsigned long>(DeviceProbe.DmaAllocationResult),
          DeviceProbe.DmaAllocationHandle,
          unsigned(DeviceProbe.DmaLockAttempted),
          unsigned(DeviceProbe.DmaLockReturned),
          static_cast<unsigned long>(DeviceProbe.DmaLockResult),
          static_cast<unsigned long long>(DeviceProbe.DmaCpuAddress),
          unsigned(DeviceProbe.DmaFenceCreateAttempted),
          unsigned(DeviceProbe.DmaFenceCreateReturned),
          static_cast<unsigned long>(DeviceProbe.DmaFenceCreateResult),
          DeviceProbe.DmaFenceHandle,
          unsigned(DeviceProbe.InitDeviceEscapeAttempted),
          unsigned(DeviceProbe.InitDeviceEscapeReturned),
          static_cast<unsigned long>(DeviceProbe.InitDeviceEscapeResult),
          unsigned(DeviceProbe.FillTableAttempted), DeviceProbe.FillTableType,
          static_cast<unsigned long long>(DeviceProbe.FillTableSize),
          static_cast<unsigned long>(DeviceProbe.FillTableResult),
          unsigned(DeviceProbe.PagingQueueDestroyAttempted),
          unsigned(DeviceProbe.PagingQueueDestroyReturned),
          static_cast<unsigned long>(DeviceProbe.PagingQueueDestroyResult),
          unsigned(DeviceProbe.DestroyAttempted),
          unsigned(DeviceProbe.DmaFenceDestroyAttempted),
          unsigned(DeviceProbe.DmaFenceDestroyReturned),
          static_cast<unsigned long>(DeviceProbe.DmaFenceDestroyResult),
          unsigned(DeviceProbe.DmaUnlockAttempted),
          unsigned(DeviceProbe.DmaUnlockReturned),
          static_cast<unsigned long>(DeviceProbe.DmaUnlockResult),
          unsigned(DeviceProbe.DmaDeallocateAttempted),
          unsigned(DeviceProbe.DmaDeallocateReturned),
          static_cast<unsigned long>(DeviceProbe.DmaDeallocateResult),
          unsigned(DeviceProbe.DestroyReturned),
          static_cast<unsigned long>(DeviceProbe.DestroyResult),
          static_cast<unsigned long>(DeviceProbe.ExceptionCode));
      if (ExpectWarpDeviceContext &&
          (!DeviceProbe.VersionNumberEscapeAttempted ||
           !DeviceProbe.VersionNumberEscapeReturned ||
           DeviceProbe.VersionNumberEscapeResult != S_OK ||
           DeviceProbe.BasicRenderVersion != 2 ||
           !DeviceProbe.PreferredNodeEscapeAttempted ||
           !DeviceProbe.PreferredNodeEscapeReturned ||
           DeviceProbe.PreferredNodeEscapeResult != S_OK ||
           !DeviceProbe.PagingQueueCreateAttempted ||
           !DeviceProbe.PagingQueueCreated ||
           DeviceProbe.PagingQueueCreateResult != S_OK ||
           !DeviceProbe.PagingQueueHasSyncObject ||
           !DeviceProbe.PagingQueueHasFenceMapping ||
           !DeviceProbe.CreateAttempted || !DeviceProbe.ContextCreated ||
           DeviceProbe.CreateContextResult != S_OK ||
           !DeviceProbe.CommandBufferSize ||
           !DeviceProbe.PrivateDataPointersIntact ||
           !DeviceProbe.InitContextEscapeAttempted ||
           !DeviceProbe.InitContextEscapeReturned ||
           DeviceProbe.InitContextEscapeResult != S_OK ||
           !DeviceProbe.KernelContextHandle || !DeviceProbe.DmaBufferSize ||
           !DeviceProbe.DmaAllocationAttempted ||
           !DeviceProbe.DmaAllocationReturned ||
           DeviceProbe.DmaAllocationResult != S_OK ||
           !DeviceProbe.DmaAllocationCreated || !DeviceProbe.DmaLockAttempted ||
           !DeviceProbe.DmaLockReturned || DeviceProbe.DmaLockResult != S_OK ||
           !DeviceProbe.DmaCpuAddress || !DeviceProbe.DmaFenceCreateAttempted ||
           !DeviceProbe.DmaFenceCreateReturned ||
           DeviceProbe.DmaFenceCreateResult != S_OK ||
           !DeviceProbe.DmaFenceCreated ||
           !DeviceProbe.InitDeviceEscapeAttempted ||
           !DeviceProbe.InitDeviceEscapeReturned ||
           DeviceProbe.InitDeviceEscapeResult != S_OK ||
           DeviceProbe.FillTableAttempted ||
           !DeviceProbe.PagingQueueDestroyAttempted ||
           !DeviceProbe.PagingQueueDestroyReturned ||
           DeviceProbe.PagingQueueDestroyResult != S_OK ||
           !DeviceProbe.DmaFenceDestroyAttempted ||
           !DeviceProbe.DmaFenceDestroyReturned ||
           DeviceProbe.DmaFenceDestroyResult != S_OK ||
           !DeviceProbe.DmaUnlockAttempted || !DeviceProbe.DmaUnlockReturned ||
           DeviceProbe.DmaUnlockResult != S_OK ||
           !DeviceProbe.DmaDeallocateAttempted ||
           !DeviceProbe.DmaDeallocateReturned ||
           DeviceProbe.DmaDeallocateResult != S_OK ||
           !DeviceProbe.DestroyAttempted || !DeviceProbe.DestroyReturned ||
           DeviceProbe.DestroyResult != S_OK || DeviceProbe.ExceptionCode)) {
        report(L"Expected context cleanup after pre-table runtime rejection",
               E_FAIL);
        ExpectedDDICalls = false;
      }
    }
  }
  if ((ExpectCreateDevice || ExpectUnsupported) && !ExpectedDDICalls)
    report(L"Explicit bootstrap expectation failed", E_FAIL);
  bool Released = FreeLibrary(Module) != FALSE;
  if (!Released)
    report(L"FreeLibrary", HRESULT_FROM_WIN32(GetLastError()));
  std::wprintf(
      SoftwareCompute
          ? L"Software WARP compute-shader probe finished; not an owned FeMe "
            L"device.\n"
      : BufferCopy
          ? L"Software WARP buffer-copy probe finished; not FeMe coverage.\n"
          : L"Software adapter objects released. Stub rejection is an "
            L"experiment, not a working FeMe adapter.\n");
  return Released && Opened && ExpectedDDICalls &&
                 (WarpBootstrapAudit ? FAILED(Result)
                                     : (Stub || WarpShim || WarpBootstrap
                                            ? Result == DXGI_ERROR_UNSUPPORTED
                                            : SUCCEEDED(Result)))
             ? 0
             : 1;
}
