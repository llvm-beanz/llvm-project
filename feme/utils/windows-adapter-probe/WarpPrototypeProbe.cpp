//===- WarpPrototypeProbe.cpp - Stock-runtime buffer feasibility
//-----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "..\windows-adapter-bootstrap\BootstrapAudit.h"

#include <windows.h>
#include <winternl.h>

#include <d3d12.h>
#include <d3d12umddi.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
HRESULT report(const char *Name, HRESULT Result) {
  std::printf("%s: 0x%08lx\n", Name, static_cast<unsigned long>(Result));
  std::fflush(stdout);
  return Result;
}

HRESULT exerciseBuffer(ID3D12Device *Device) {
  D3D12_HEAP_PROPERTIES Heap{};
  Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  Heap.CreationNodeMask = 1;
  Heap.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC Desc{};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = 4097;
  Desc.Height = 1;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> Buffer;
  HRESULT Result =
      report("CreateCommittedResource(upload)",
             Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ,
                                             nullptr, IID_PPV_ARGS(&Buffer)));
  if (FAILED(Result))
    return Result;
  void *Memory = nullptr;
  D3D12_RANGE NoRead{0, 0};
  Result = report("Map(upload)", Buffer->Map(0, &NoRead, &Memory));
  if (FAILED(Result))
    return Result;
  if (!Memory) {
    Buffer->Unmap(0, &NoRead);
    return report("Missing mapped buffer", E_FAIL);
  }
  std::memset(Memory, 0x5a, static_cast<SIZE_T>(Desc.Width));
  D3D12_RANGE Written{0, static_cast<SIZE_T>(Desc.Width)};
  Buffer->Unmap(0, &Written);
  Memory = nullptr;
  Result = report("Remap(upload)", Buffer->Map(0, &Written, &Memory));
  if (FAILED(Result))
    return Result;
  bool Retained = Memory != nullptr;
  if (Memory)
    for (SIZE_T I = 0; I < Desc.Width; ++I)
      Retained &= static_cast<const unsigned char *>(Memory)[I] == 0x5a;
  Buffer->Unmap(0, &NoRead);
  if (!Retained || !Buffer->GetGPUVirtualAddress())
    return report("Buffer data/VA mismatch", E_FAIL);
  ID3D12Pageable *Objects[] = {Buffer.Get()};
  Result = report("Evict(buffer)", Device->Evict(1, Objects));
  if (FAILED(Result))
    return Result;
  Result = report("MakeResident(buffer)", Device->MakeResident(1, Objects));
  if (FAILED(Result))
    return Result;
  return report("GetDeviceRemovedReason", Device->GetDeviceRemovedReason());
}

HRESULT createComputePipeline(ID3D12Device *Device, const char *Source,
                              SIZE_T SourceSize,
                              const D3D12_ROOT_SIGNATURE_DESC &RootDesc,
                              ComPtr<ID3D12RootSignature> &Root,
                              ComPtr<ID3D12PipelineState> &Pipeline) {
  ComPtr<ID3DBlob> Shader, Errors, RootBlob;
  HRESULT Result =
      report("Compile application compute shader",
             D3DCompile(Source, SourceSize, "FeMe command test", nullptr,
                        nullptr, "main", "cs_5_0", 0, 0, &Shader, &Errors));
  if (FAILED(Result)) {
    if (Errors)
      std::fprintf(stderr, "%.*s\n", static_cast<int>(Errors->GetBufferSize()),
                   static_cast<const char *>(Errors->GetBufferPointer()));
    return Result;
  }
  Result =
      report("Serialize application root",
             D3D12SerializeRootSignature(
                 &RootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &RootBlob, &Errors));
  if (FAILED(Result))
    return Result;
  Result = report("Create application root",
                  Device->CreateRootSignature(0, RootBlob->GetBufferPointer(),
                                              RootBlob->GetBufferSize(),
                                              IID_PPV_ARGS(&Root)));
  if (FAILED(Result))
    return Result;
  D3D12_COMPUTE_PIPELINE_STATE_DESC PipelineDesc{};
  PipelineDesc.pRootSignature = Root.Get();
  PipelineDesc.CS = {Shader->GetBufferPointer(), Shader->GetBufferSize()};
  Result = report("Create application compute pipeline",
                  Device->CreateComputePipelineState(&PipelineDesc,
                                                     IID_PPV_ARGS(&Pipeline)));
  return Result;
}

HRESULT exerciseCommands(ID3D12Device *Device) {
  constexpr char Source[] = "[numthreads(1,1,1)] void main() {}";
  D3D12_ROOT_SIGNATURE_DESC RootDesc{};
  ComPtr<ID3D12RootSignature> Root;
  ComPtr<ID3D12PipelineState> Pipeline;
  HRESULT Result = createComputePipeline(Device, Source, sizeof(Source) - 1,
                                         RootDesc, Root, Pipeline);
  if (FAILED(Result))
    return Result;
  for (auto Type :
       {D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE,
        D3D12_COMMAND_LIST_TYPE_COPY}) {
    D3D12_COMMAND_QUEUE_DESC QueueDesc{};
    QueueDesc.Type = Type;
    ComPtr<ID3D12CommandQueue> Queue;
    ComPtr<ID3D12CommandAllocator> Allocator;
    ComPtr<ID3D12GraphicsCommandList> List;
    Result =
        report("Create application queue",
               Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue)));
    if (FAILED(Result))
      return Result;
    Result =
        report("Create application allocator",
               Device->CreateCommandAllocator(Type, IID_PPV_ARGS(&Allocator)));
    if (FAILED(Result))
      return Result;
    Result = report("Create application list",
                    Device->CreateCommandList(0, Type, Allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&List)));
    if (FAILED(Result))
      return Result;
    for (unsigned Repeat = 0; Repeat != 3; ++Repeat) {
      if (Type != D3D12_COMMAND_LIST_TYPE_COPY) {
        List->SetPipelineState(Pipeline.Get());
        List->SetComputeRootSignature(Root.Get());
        List->Dispatch(2, 3, 1);
      }
      Result = report("Close application list", List->Close());
      if (FAILED(Result))
        return Result;
      ID3D12CommandList *Commands[] = {List.Get()};
      Queue->ExecuteCommandLists(1, Commands);
      Result = report("Command execution removal status",
                      Device->GetDeviceRemovedReason());
      if (FAILED(Result))
        return Result;
      if (Repeat != 2) {
        Result = report("Reset application allocator", Allocator->Reset());
        if (FAILED(Result))
          return Result;
        Result = report("Reset application list",
                        List->Reset(Allocator.Get(), nullptr));
        if (FAILED(Result))
          return Result;
      }
    }
  }
  return S_OK;
}

HRESULT exerciseBufferCommands(ID3D12Device *Device) {
  constexpr char Source[] =
      "RWByteAddressBuffer output : register(u3);\n"
      "groupshared uint sharedValues[30];\n"
      "[numthreads(5,3,2)] void main(uint3 id : SV_DispatchThreadID,\n"
      "uint3 local : SV_GroupThreadID) {\n"
      "uint index = id.x + 15 * (id.y + 6 * id.z);\n"
      "uint slot = local.x + 5 * (local.y + 3 * local.z);\n"
      "sharedValues[slot] = index;\n"
      "GroupMemoryBarrierWithGroupSync();\n"
      "uint peer = sharedValues[slot == 29 ? 0 : slot + 1];\n"
      "output.Store(index * 4, output.Load(index * 4) + peer + 23);\n"
      "}\n";
  D3D12_ROOT_PARAMETER Parameter{};
  Parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  Parameter.Descriptor.ShaderRegister = 3;
  Parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC RootDesc{};
  RootDesc.NumParameters = 1;
  RootDesc.pParameters = &Parameter;
  ComPtr<ID3D12RootSignature> Root;
  ComPtr<ID3D12PipelineState> Pipeline;
  HRESULT Result = createComputePipeline(Device, Source, sizeof(Source) - 1,
                                         RootDesc, Root, Pipeline);
  if (FAILED(Result))
    return Result;
  constexpr UINT Threads = 180, Words = Threads + 16;
  constexpr UINT Bytes = Words * sizeof(UINT);
  D3D12_HEAP_PROPERTIES Heap{};
  Heap.CreationNodeMask = Heap.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC Desc{};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = Bytes;
  Desc.Height = Desc.DepthOrArraySize = Desc.MipLevels = 1;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> Upload, Output, Readback;
  Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  Result =
      report("Create command upload",
             Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ,
                                             nullptr, IID_PPV_ARGS(&Upload)));
  if (FAILED(Result))
    return Result;
  Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  Result =
      report("Create command UAV",
             Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc,
                                             D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr, IID_PPV_ARGS(&Output)));
  if (FAILED(Result))
    return Result;
  Heap.Type = D3D12_HEAP_TYPE_READBACK;
  Desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  Result =
      report("Create command readback",
             Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc,
                                             D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr, IID_PPV_ARGS(&Readback)));
  if (FAILED(Result))
    return Result;
  void *Memory = nullptr;
  D3D12_RANGE NoRead{0, 0}, Full{0, Bytes};
  Result = report("Map command upload", Upload->Map(0, &NoRead, &Memory));
  if (FAILED(Result))
    return Result;
  if (!Memory) {
    Upload->Unmap(0, &NoRead);
    return report("Missing command upload memory", E_FAIL);
  }
  for (UINT I = 0; I < Words; ++I)
    static_cast<UINT *>(Memory)[I] = I * 7 + 5;
  Upload->Unmap(0, &Full);
  D3D12_COMMAND_QUEUE_DESC QueueDesc{};
  QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  ComPtr<ID3D12CommandQueue> Queue;
  ComPtr<ID3D12CommandAllocator> Allocator;
  ComPtr<ID3D12GraphicsCommandList> List;
  Result = report("Create buffer command queue",
                  Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue)));
  if (FAILED(Result))
    return Result;
  Result = report(
      "Create buffer command allocator",
      Device->CreateCommandAllocator(QueueDesc.Type, IID_PPV_ARGS(&Allocator)));
  if (FAILED(Result))
    return Result;
  Result =
      report("Create buffer command list",
             Device->CreateCommandList(0, QueueDesc.Type, Allocator.Get(),
                                       Pipeline.Get(), IID_PPV_ARGS(&List)));
  if (FAILED(Result))
    return Result;
  for (UINT Repeat = 0; Repeat < 3; ++Repeat) {
    if (!Repeat)
      List->CopyBufferRegion(Output.Get(), 0, Upload.Get(), 0, Bytes);
    D3D12_RESOURCE_BARRIER Barrier{};
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Output.Get();
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = Repeat ? D3D12_RESOURCE_STATE_COPY_SOURCE
                                            : D3D12_RESOURCE_STATE_COPY_DEST;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    List->ResourceBarrier(1, &Barrier);
    List->SetComputeRootSignature(Root.Get());
    List->SetComputeRootUnorderedAccessView(0, Output->GetGPUVirtualAddress());
    List->SetComputeRootSignature(Root.Get());
    List->Dispatch(3, 2, 1);
    List->SetComputeRootUnorderedAccessView(0, Output->GetGPUVirtualAddress() +
                                                   16);
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    List->ResourceBarrier(1, &Barrier);
    List->CopyBufferRegion(Readback.Get(), 0, Output.Get(), 0, Bytes);
    Result = report("Close buffer command list", List->Close());
    if (FAILED(Result))
      return Result;
    ID3D12CommandList *Commands[] = {List.Get()};
    Queue->ExecuteCommandLists(1, Commands);
    Result = report("Buffer execution removal status",
                    Device->GetDeviceRemovedReason());
    if (FAILED(Result))
      return Result;
    Result = report("Map command readback", Readback->Map(0, &Full, &Memory));
    if (FAILED(Result))
      return Result;
    bool Valid = Memory != nullptr;
    for (UINT I = 0; Valid && I < Words; ++I) {
      UINT Expected = I * 7 + 5;
      if (I < Threads) {
        UINT X = I % 15, Y = I / 15 % 6, Z = I / 90;
        UINT Peer = (X % 5 + 5 * (Y % 3 + 3 * Z) + 1) % 30;
        UINT PeerGlobal = X / 5 * 5 + Peer % 5 +
                          15 * (Y / 3 * 3 + Peer / 5 % 3 + 6 * (Peer / 15));
        Expected += (Repeat + 1) * (PeerGlobal + 23);
      }
      Valid = static_cast<const UINT *>(Memory)[I] == Expected;
      if (!Valid)
        std::fprintf(stderr, "Buffer result %u: got %u expected %u\n", I,
                     static_cast<const UINT *>(Memory)[I], Expected);
    }
    Readback->Unmap(0, &NoRead);
    if (!Valid)
      return report("Owned grouped compute/readback mismatch", E_FAIL);
    std::printf("Owned grouped buffer oracle: repeat=%u values=%u guards=%u\n",
                Repeat + 1, Threads, Words - Threads);
    if (Repeat != 2) {
      Result = report("Reset buffer command allocator", Allocator->Reset());
      if (FAILED(Result))
        return Result;
      Result = report("Reset buffer command list",
                      List->Reset(Allocator.Get(), Pipeline.Get()));
      if (FAILED(Result))
        return Result;
    }
  }
  return S_OK;
}

bool printCaps(HMODULE Module, bool OwnedDevice) {
  using CapacityFn = UINT32(APIENTRY *)();
  using AuditFn =
      void(APIENTRY *)(LONG *, LONG *, UINT32 *, LONG *, UINT *, UINT *, LONG *,
                       LONG *, LONG *, LONG *, LONG *);
  auto Capacity = reinterpret_cast<CapacityFn>(
      GetProcAddress(Module, "FeMeBootstrapGetAuditCapacity"));
  auto Audit = reinterpret_cast<AuditFn>(
      GetProcAddress(Module, "FeMeBootstrapGetAudit"));
  if (!Capacity || !Audit)
    return false;
  UINT32 Size = Capacity();
  std::vector<LONG> Types(Size), Results(Size);
  std::vector<UINT32> Sizes(Size);
  LONG Count = 0, KT = 0, UM = 0, Allocate = 0, Deallocate = 0, SetError = 0;
  UINT Interface = 0, Version = 0;
  Audit(&Count, Types.data(), Sizes.data(), Results.data(), &Interface,
        &Version, &KT, &UM, &Allocate, &Deallocate, &SetError);
  std::printf("Negotiated interface=0x%08x version=0x%08x\n", Interface,
              Version);
  for (UINT32 I = 0; Count > 0 && I < static_cast<UINT32>(Count) && I < Size;
       ++I)
    std::printf("Cap %ld size=%u result=0x%08lx\n", Types[I], Sizes[I],
                static_cast<unsigned long>(Results[I]));
  constexpr D3D12DDICAPS_TYPE Required[] = {
      D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1,
      D3D12DDICAPS_TYPE_D3D12_OPTIONS,
      D3D12DDICAPS_TYPE_SHADER,
      D3D12DDICAPS_TYPE_0011_SHADER_MODELS,
      D3D12DDICAPS_TYPE_ARCHITECTURE_INFO,
      D3D12DDICAPS_TYPE_0023_UMD_BASED_COMMAND_QUEUE_PRIORITY,
      D3D12DDICAPS_TYPE_0050_HARDWARE_SCHEDULING_CAPS,
      D3D12DDICAPS_TYPE_0022_TEXTURE_LAYOUT,
      D3D12DDICAPS_TYPE_MEMORY_ARCHITECTURE,
      D3D12DDICAPS_TYPE_TEXTURE_LAYOUT_SETS,
      D3D12DDICAPS_TYPE_0022_CPU_PAGE_TABLE_FALSE_POSITIVES};
  bool Valid = ((static_cast<UINT64>(Interface) << 32) | Version) ==
                   D3D12DDI_SUPPORTED_0072 &&
               KT && UM && SetError && Count > 0 &&
               static_cast<UINT32>(Count) <= Size;
  for (auto Type : Required) {
    bool Found = false;
    for (UINT32 I = 0; Count > 0 && I < static_cast<UINT32>(Count) && I < Size;
         ++I)
      if (Types[I] == Type) {
        Found = true;
        Valid &= Results[I] == S_OK;
      }
    Valid &= Found;
  }
  if (OwnedDevice)
    for (auto Type : {D3D12DDICAPS_TYPE_GPUVA_CAPS,
                      D3D12DDICAPS_TYPE_0073_SUPPORT_BATCHED_MARKERS}) {
      bool Found = false;
      for (UINT32 I = 0; I < static_cast<UINT32>(Count) && I < Size; ++I)
        if (Types[I] == Type) {
          Found = true;
          Valid &= Results[I] == S_OK;
        }
      Valid &= Found;
    }
  return Valid;
}
} // namespace

int wmain(int Argc, wchar_t **Argv) {
  bool Reference = Argc == 2 && std::wcscmp(Argv[1], L"--reference") == 0;
  bool BufferCommands =
      Argc == 2 && std::wcscmp(Argv[1], L"--buffer-commands") == 0;
  bool Commands =
      BufferCommands || (Argc == 2 && std::wcscmp(Argv[1], L"--commands") == 0);
  bool ExpectShaderRejection =
      Argc == 2 && std::wcscmp(Argv[1], L"--expect-shader-rejection") == 0;
  if (Argc != 1 && !Reference && !ExpectShaderRejection && !Commands) {
    std::fprintf(stderr, "Usage: warp-prototype-harness "
                         "[--reference|--expect-shader-rejection|--commands|"
                         "--buffer-commands]\n");
    return 1;
  }
  wchar_t Path[32768];
  DWORD Length = Reference ? GetSystemDirectoryW(Path, ARRAYSIZE(Path))
                           : GetModuleFileNameW(nullptr, Path, ARRAYSIZE(Path));
  if (!Length || Length >= ARRAYSIZE(Path)) {
    report("Module directory",
           HRESULT_FROM_WIN32(Length ? ERROR_INSUFFICIENT_BUFFER
                                     : GetLastError()));
    return 1;
  }
  std::wstring ModulePath(Path, Length);
  if (!Reference)
    ModulePath.resize(ModulePath.find_last_of(L'\\'));
  ModulePath += L"\\d3d10warp.dll";
  HMODULE Module = LoadLibraryExW(ModulePath.c_str(), nullptr,
                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                      LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!Module) {
    report("LoadLibraryExW", HRESULT_FROM_WIN32(GetLastError()));
    return 1;
  }
  std::wprintf(L"Selected module: %ls\n", ModulePath.c_str());
  ComPtr<IDXGIFactory4> Factory;
  ComPtr<IDXGIAdapter1> Adapter;
  ComPtr<ID3D12Device> Device;
  HRESULT Result = report("CreateDXGIFactory2",
                          CreateDXGIFactory2(0, IID_PPV_ARGS(&Factory)));
  if (SUCCEEDED(Result))
    Result = report("EnumWarpAdapter",
                    Factory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter)));
  DXGI_ADAPTER_DESC1 Desc{};
  if (SUCCEEDED(Result))
    Result = report("GetDesc1", Adapter->GetDesc1(&Desc));
  if (SUCCEEDED(Result) &&
      (Desc.VendorId != 0x1414 || Desc.DeviceId != 0x008c ||
       !(Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)))
    Result = report("WARP identity mismatch", E_FAIL);
  if (SUCCEEDED(Result))
    std::printf(
        "WARP vendor=0x%04x device=0x%04x software=%u LUID=%08lx:%08lx\n",
        Desc.VendorId, Desc.DeviceId,
        (Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0,
        static_cast<unsigned long>(Desc.AdapterLuid.HighPart),
        static_cast<unsigned long>(Desc.AdapterLuid.LowPart));
  HRESULT DeviceResult = E_FAIL;
  if (SUCCEEDED(Result)) {
    DeviceResult = D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                     IID_PPV_ARGS(&Device));
    Result = report("D3D12CreateDevice(11_0)", DeviceResult);
  }
  bool ValidRejection = ExpectShaderRejection &&
                        DeviceResult == DXGI_ERROR_DEVICE_REMOVED && !Device;
  if (GetModuleHandleW(L"d3d10warp.dll") != Module) {
    Result = report("Selected module mismatch", E_FAIL);
    ValidRejection = false;
  }
  if (!Reference && GetModuleHandleW(L"feme_warp_reference.dll")) {
    Result = report("Unexpected reference WARP dependency", E_FAIL);
    ValidRejection = false;
  }
  if (SUCCEEDED(Result))
    Result = exerciseBuffer(Device.Get());
  if (SUCCEEDED(Result) && Commands)
    Result = exerciseCommands(Device.Get());
  if (SUCCEEDED(Result) && BufferCommands)
    Result = exerciseBufferCommands(Device.Get());
  bool ValidOwned = !Reference && !ExpectShaderRejection &&
                    DeviceResult == S_OK && SUCCEEDED(Result);
  if (ValidOwned) {
    LUID DeviceLuid = Device->GetAdapterLuid();
    ValidOwned &= DeviceLuid.HighPart == Desc.AdapterLuid.HighPart &&
                  DeviceLuid.LowPart == Desc.AdapterLuid.LowPart &&
                  Device->GetNodeCount() == 1;
    HMODULE Compiler = GetModuleHandleW(L"feme_warp_compiler.dll");
    using RevisionFn = const char *(*)();
    auto Revision = Compiler ? reinterpret_cast<RevisionFn>(GetProcAddress(
                                   Compiler, "FeMeWarpCompilerRevision"))
                             : nullptr;
    const char *Source = Revision ? Revision() : nullptr;
    std::wstring Expected =
        ModulePath.substr(0, ModulePath.find_last_of(L'\\')) +
        L"\\feme_warp_compiler.dll";
    DWORD CompilerLength =
        Compiler ? GetModuleFileNameW(Compiler, Path, ARRAYSIZE(Path)) : 0;
    ValidOwned &= CompilerLength && CompilerLength < ARRAYSIZE(Path) &&
                  _wcsicmp(Expected.c_str(), Path) == 0 && Source &&
                  std::strlen(Source) == 40 &&
                  std::strspn(Source, "0123456789abcdef") == 40;
    if (CompilerLength && Source) {
      std::wprintf(L"Owned compiler: %ls\n", Path);
      std::printf("Compiler source revision: %s\n", Source);
    }
  }
  Device.Reset();
  Adapter.Reset();
  Factory.Reset();
  if (!Reference) {
    bool CapsValid = printCaps(Module, ValidOwned);
    ValidRejection &= CapsValid;
    ValidOwned &= CapsValid;
    if (!CapsValid)
      Result = report("Mandatory capability evidence mismatch", E_FAIL);
    using CallsFn = void(APIENTRY *)(LONG *, LONG *, LONG *, LONG *, LONG *,
                                     LONG *, LONG *, LONG *, LONG *);
    auto Calls = reinterpret_cast<CallsFn>(
        GetProcAddress(Module, "FeMeBootstrapGetCalls"));
    if (Calls) {
      LONG Open, Size, Versions, Caps, Optional, Fill, Create, Close, Destroy;
      Calls(&Open, &Size, &Versions, &Caps, &Optional, &Fill, &Create, &Close,
            &Destroy);
      std::printf("Calls: open=%ld size=%ld versions=%ld caps=%ld optional=%ld "
                  "fill=%ld create=%ld close=%ld destroy=%ld\n",
                  Open, Size, Versions, Caps, Optional, Fill, Create, Close,
                  Destroy);
      ValidRejection &= Open == 1 && Size == 1 && Versions == 2 && Caps == 14 &&
                        Optional == 1 && Fill == 5 && Create == 1 &&
                        Close == 1 && Destroy == 1;
      ValidOwned &= Open == 1 && Size == 1 && Versions == 2 && Caps == 16 &&
                    Optional == 1 && Fill == 5 && Create == 1 && Close == 1 &&
                    Destroy == 1;
    } else {
      Result = report("Missing owned lifecycle counters", E_FAIL);
      ValidRejection = false;
      ValidOwned = false;
    }
    using LastFn = const char *(APIENTRY *)();
    auto Last = reinterpret_cast<LastFn>(
        GetProcAddress(Module, "FeMeWarpGetLastUnsupportedDDI"));
    if (!Last) {
      Result = report("Missing owned diagnostic export", E_FAIL);
      ValidRejection = false;
      ValidOwned = false;
    } else if (const char *Name = Last()) {
      std::printf("Last unavailable DDI: %s\n", Name);
      ValidRejection &= std::strcmp(Name, "pfnCalcPrivateShaderSize") == 0;
      ValidOwned = false;
    } else {
      ValidRejection = false;
    }
    using TableAuditFn =
        void(APIENTRY *)(feme::probe::WarpPrototypeTableAudit *);
    auto TableAudit = reinterpret_cast<TableAuditFn>(
        GetProcAddress(Module, "FeMeWarpGetTableAudit"));
    if (TableAudit) {
      feme::probe::WarpPrototypeTableAudit Audit;
      TableAudit(&Audit);
      std::printf("Tables: core=%u queue=%u lists=%u extended=%u; "
                  "executable shader queries=%u other unavailable calls=%u\n",
                  Audit.CoreTables, Audit.QueueTables, Audit.CommandListTables,
                  Audit.ExtendedTables, Audit.ExecutableShaderQueries,
                  Audit.UnsupportedCalls);
      std::printf("Native shader lifetimes: %u/%u; shared pipelines: %u/%u\n",
                  Audit.CompiledShadersCreated, Audit.CompiledShadersDestroyed,
                  Audit.PipelinesCreated, Audit.PipelinesDestroyed);
      ValidRejection &=
          Audit.CoreTables == 1 && Audit.QueueTables == 1 &&
          Audit.CommandListTables == 2 && Audit.ExtendedTables == 1 &&
          Audit.ExecutableShaderQueries == 1 && Audit.UnsupportedCalls == 0;
      UINT ExpectedShaders = BufferCommands ? 4 : Commands ? 3 : 2;
      ValidOwned &=
          Audit.CoreTables == 1 && Audit.QueueTables == 1 &&
          Audit.CommandListTables == 2 && Audit.ExtendedTables == 1 &&
          Audit.ExecutableShaderQueries == ExpectedShaders &&
          Audit.UnsupportedCalls == 0 &&
          Audit.CompiledShadersCreated == ExpectedShaders &&
          Audit.CompiledShadersCreated == Audit.CompiledShadersDestroyed &&
          Audit.PipelinesCreated == ExpectedShaders &&
          Audit.PipelinesCreated == Audit.PipelinesDestroyed;
    } else {
      ValidRejection = false;
      ValidOwned = false;
      Result = report("Missing table audit export", E_FAIL);
    }
    using ProbeFn = void(APIENTRY *)(feme::probe::WarpDeviceContextProbe *);
    auto Probe = reinterpret_cast<ProbeFn>(
        GetProcAddress(Module, "FeMeBootstrapGetWarpDeviceContextProbe"));
    if (Probe) {
      feme::probe::WarpDeviceContextProbe Data;
      Probe(&Data);
      std::printf("Last table type=%u size=%llu result=0x%08lx; "
                  "device cleanup=0x%08lx\n",
                  Data.FillTableType, Data.FillTableSize,
                  static_cast<unsigned long>(Data.FillTableResult),
                  static_cast<unsigned long>(Data.DestroyResult));
      bool CleanupValid =
          Data.DestroyReturned && Data.DestroyResult == S_OK &&
          Data.PagingQueueDestroyReturned &&
          Data.PagingQueueDestroyResult == S_OK &&
          Data.DmaFenceDestroyReturned && Data.DmaFenceDestroyResult == S_OK &&
          Data.DmaUnlockReturned && Data.DmaUnlockResult == S_OK &&
          Data.DmaDeallocateReturned && Data.DmaDeallocateResult == S_OK &&
          !Data.LiveRootSignaturesAtDestroy && !Data.LiveNullShadersAtDestroy &&
          !Data.LiveCompiledShadersAtDestroy &&
          !Data.LiveImmutableStatesAtDestroy && !Data.LivePipelinesAtDestroy &&
          !Data.LiveCommandPoolsAtDestroy && !Data.LiveHeapsAtDestroy &&
          !Data.LiveCommandQueuesAtDestroy &&
          !Data.LiveCommandRecordersAtDestroy &&
          !Data.LiveCommandListsAtDestroy && !Data.LiveResourcesAtDestroy &&
          !Data.ExceptionCode;
      ValidRejection &= CleanupValid;
      ValidOwned &= CleanupValid;
      if (!CleanupValid)
        Result = report("Device cleanup mismatch", E_FAIL);
    } else {
      Result = report("Missing owned context export", E_FAIL);
      ValidRejection = false;
      ValidOwned = false;
    }
    using HeapAuditFn =
        void(APIENTRY *)(feme::probe::WarpApplicationHeapAudit *);
    auto HeapAudit = reinterpret_cast<HeapAuditFn>(
        GetProcAddress(Module, "FeMeWarpGetApplicationHeapAudit"));
    if (HeapAudit) {
      feme::probe::WarpApplicationHeapAudit Data;
      HeapAudit(&Data);
      std::printf("Application KMD heaps: %u/%u maps=%u/%u "
                  "resident=%u evict=%u runtime paging queue=%u\n",
                  Data.Created, Data.Destroyed, Data.Maps, Data.Unmaps,
                  Data.MakeResidentCallbacks, Data.EvictCallbacks,
                  Data.HasRuntimePagingQueue);
      UINT ExpectedHeaps = BufferCommands ? 4 : 1;
      ValidOwned &= Data.Created == ExpectedHeaps &&
                    Data.Destroyed == ExpectedHeaps && Data.Maps > 0 &&
                    Data.Maps == Data.Unmaps &&
                    Data.MakeResidentCallbacks == (BufferCommands ? 5u : 2u) &&
                    Data.EvictCallbacks == 1 && Data.HasRuntimePagingQueue;
    } else
      ValidOwned = false;
    if (Commands) {
      using CommandAuditFn = void(APIENTRY *)(UINT *, UINT *);
      auto Audit = reinterpret_cast<CommandAuditFn>(
          GetProcAddress(Module, "FeMeWarpGetCommandAudit"));
      UINT Dispatches = 0, Copies = 0;
      if (Audit)
        Audit(&Dispatches, &Copies);
      std::printf("Native command execution: dispatches=%u copies=%u\n",
                  Dispatches, Copies);
      ValidOwned &= Audit && Dispatches == (BufferCommands ? 9u : 6u) &&
                    Copies == (BufferCommands ? 4u : 0u);
    }
  }
  if (!FreeLibrary(Module)) {
    Result = report("FreeLibrary", HRESULT_FROM_WIN32(GetLastError()));
    ValidRejection = false;
    ValidOwned = false;
  }
  if (ExpectShaderRejection) {
    report("Expected executable-shader rejection",
           ValidRejection ? S_OK : E_FAIL);
    return ValidRejection ? 0 : 1;
  }
  if (!Reference) {
    report("Owned device/resource/provenance gate", ValidOwned ? S_OK : E_FAIL);
    return ValidOwned && SUCCEEDED(Result) ? 0 : 1;
  }
  return SUCCEEDED(Result) ? 0 : 1;
}
