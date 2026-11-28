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

#ifdef FEME_WARP_COEXIST
#include <dxcapi.h>
#include <vulkan/vulkan.h>
#endif

#include <array>
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

template <size_t Count>
HRESULT checkExports(HMODULE Module, const char *const (&Names)[Count]) {
  auto *Base = reinterpret_cast<unsigned char *>(Module);
  auto *DOS = reinterpret_cast<IMAGE_DOS_HEADER *>(Base);
  auto *PE = reinterpret_cast<IMAGE_NT_HEADERS *>(Base + DOS->e_lfanew);
  if (DOS->e_magic != IMAGE_DOS_SIGNATURE ||
      PE->Signature != IMAGE_NT_SIGNATURE)
    return report("Invalid owned module image", E_FAIL);
  DWORD Offset = PE->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]
                     .VirtualAddress;
  if (!Offset)
    return report("Missing owned export directory", E_FAIL);
  auto *Exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY *>(Base + Offset);
  if (Exports->NumberOfNames != Count || Exports->NumberOfFunctions != Count ||
      GetProcAddress(Module, MAKEINTRESOURCEA(199)))
    return report("Owned export set mismatch", E_FAIL);
  for (auto *Name : Names) {
    FARPROC Address = GetProcAddress(Module, Name);
    MEMORY_BASIC_INFORMATION Memory{};
    if (!Address ||
        !VirtualQuery(reinterpret_cast<const void *>(Address), &Memory,
                      sizeof(Memory)) ||
        Memory.AllocationBase != Module) {
      std::fprintf(stderr, "Missing or forwarded owned export: %s\n", Name);
      return report("Owned export provenance mismatch", E_FAIL);
    }
  }
  return S_OK;
}

constexpr const char *WarpExports[] = {
    "FeMeBootstrapGetAudit",
    "FeMeBootstrapGetAuditCapacity",
    "FeMeBootstrapGetCallbackProbe",
    "FeMeBootstrapGetCalls",
    "FeMeBootstrapGetContractAudit",
    "FeMeBootstrapGetGpuVirtualAddressProbe",
    "FeMeBootstrapGetMonitoredFenceProbe",
    "FeMeBootstrapGetPagingQueueProbe",
    "FeMeBootstrapGetWarpContextProbe",
    "FeMeBootstrapGetWarpDeviceContextProbe",
    "FeMeBootstrapGetWarpPrivateAdapterInfoProbe",
    "FeMeWarpGetApplicationHeapAudit",
    "FeMeWarpGetCommandAudit",
    "FeMeWarpGetLastUnsupportedDDI",
    "FeMeWarpGetTableAudit",
    "OpenAdapter10",
    "OpenAdapter10_2",
    "OpenAdapter12"};

constexpr const char *CompilerExports[] = {
    "?registerImplicitTypeID@FallbackTypeIDResolver@detail@mlir@@KA?AVTypeID@3@"
    "VStringRef@llvm@@@Z",
    "FeMeWarpCompileDXILProgram",
    "FeMeWarpCompileRawDXBC",
    "FeMeWarpCompilerRevision",
    "FeMeWarpCreateComputePipeline",
    "FeMeWarpCreateVertexPipeline",
    "FeMeWarpDestroyCompiledShader",
    "FeMeWarpDestroyComputePipeline",
    "FeMeWarpDestroyVertexPipeline",
    "FeMeWarpDispatchCompute",
    "FeMeWarpRetainComputePipeline",
    "__jit_debug_descriptor",
    "__jit_debug_register_code"};

#ifdef FEME_WARP_COEXIST
struct CoexistState {
  HMODULE DXCModule = nullptr;
  HMODULE VulkanModule = nullptr;
  ComPtr<IDxcCompiler3> Compiler;
  ComPtr<IDxcBlob> Shader;
  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
  PFN_vkDestroyInstance DestroyInstance = nullptr;
  PFN_vkGetPhysicalDeviceProperties GetProperties = nullptr;

  ~CoexistState() { close(); }

  HRESULT compile() {
    constexpr char Source[] = "[numthreads(1,1,1)] void main() {}";
    DxcBuffer Buffer{Source, sizeof(Source) - 1, DXC_CP_UTF8};
    const wchar_t *Args[] = {L"-T", L"cs_6_0", L"-E", L"main"};
    ComPtr<IDxcResult> Output;
    HRESULT Result = report("Compile coexistence DXIL",
                            Compiler->Compile(&Buffer, Args, ARRAYSIZE(Args),
                                              nullptr, IID_PPV_ARGS(&Output)));
    if (FAILED(Result))
      return Result;
    HRESULT Status = E_FAIL;
    Result = Output->GetStatus(&Status);
    if (FAILED(Result))
      return report("Get DXC compilation status", Result);
    if (FAILED(Status)) {
      ComPtr<IDxcBlobUtf8> Errors;
      if (SUCCEEDED(Output->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&Errors),
                                      nullptr)) &&
          Errors)
        std::fprintf(stderr, "%s\n", Errors->GetStringPointer());
      return report("DXC coexistence compilation failed", Status);
    }
    Shader.Reset();
    Result = report(
        "Get coexistence DXIL",
        Output->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&Shader), nullptr));
    return SUCCEEDED(Result) && (!Shader || !Shader->GetBufferSize())
               ? report("Missing coexistence DXIL", E_FAIL)
               : Result;
  }

  HRESULT query() {
    VkPhysicalDeviceProperties Properties{};
    GetProperties(PhysicalDevice, &Properties);
    if (std::strcmp(Properties.deviceName, "FeMe CPU Vulkan Device"))
      return report("Coexistence Vulkan identity mismatch", E_FAIL);
    std::printf("Coexisting Vulkan device: %s\n", Properties.deviceName);
    return S_OK;
  }

  HRESULT initialize(const wchar_t *DXCPath, const wchar_t *VulkanPath) {
    constexpr DWORD Flags =
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32;
    DXCModule = LoadLibraryExW(DXCPath, nullptr, Flags);
    if (!DXCModule)
      return report("Load coexistence DXC", HRESULT_FROM_WIN32(GetLastError()));
    auto CreateCompiler = reinterpret_cast<DxcCreateInstanceProc>(
        GetProcAddress(DXCModule, "DxcCreateInstance"));
    if (!CreateCompiler)
      return report("Missing DXC factory", E_FAIL);
    HRESULT Result =
        report("Create coexistence DXC",
               CreateCompiler(CLSID_DxcCompiler, IID_PPV_ARGS(&Compiler)));
    if (FAILED(Result))
      return Result;
    Result = compile();
    if (FAILED(Result))
      return Result;
    VulkanModule = LoadLibraryExW(VulkanPath, nullptr, Flags);
    if (!VulkanModule)
      return report("Load coexistence Vulkan ICD",
                    HRESULT_FROM_WIN32(GetLastError()));
    auto GetProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(VulkanModule, "vkGetInstanceProcAddr"));
    if (!GetProc)
      return report("Missing Vulkan ICD entry", E_FAIL);
    auto CreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
        GetProc(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!CreateInstance)
      return report("Missing Vulkan instance entry", E_FAIL);
    VkApplicationInfo Application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    Application.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo Create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    Create.pApplicationInfo = &Application;
    VkResult VulkanResult = CreateInstance(&Create, nullptr, &Instance);
    if (VulkanResult != VK_SUCCESS) {
      std::fprintf(stderr, "Coexistence Vulkan instance result: %d\n",
                   VulkanResult);
      return report("Create coexistence Vulkan instance", E_FAIL);
    }
    DestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(
        GetProc(Instance, "vkDestroyInstance"));
    GetProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        GetProc(Instance, "vkGetPhysicalDeviceProperties"));
    auto Enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        GetProc(Instance, "vkEnumeratePhysicalDevices"));
    if (!DestroyInstance || !GetProperties || !Enumerate)
      return report("Missing coexistence Vulkan functions", E_FAIL);
    uint32_t Count = 1;
    VulkanResult = Enumerate(Instance, &Count, &PhysicalDevice);
    if (VulkanResult != VK_SUCCESS || Count != 1 || !PhysicalDevice) {
      std::fprintf(stderr,
                   "Coexistence Vulkan enumeration: result=%d count=%u\n",
                   VulkanResult, Count);
      return report("Enumerate coexistence Vulkan device", E_FAIL);
    }
    return query();
  }

  HRESULT close() {
    HRESULT Result = S_OK;
    if (Instance) {
      if (DestroyInstance)
        DestroyInstance(Instance, nullptr);
      else
        Result = report("Missing coexistence Vulkan teardown", E_FAIL);
      Instance = VK_NULL_HANDLE;
    }
    Shader.Reset();
    Compiler.Reset();
    for (auto *Module : {&DXCModule, &VulkanModule}) {
      if (*Module && !FreeLibrary(*Module))
        Result = report("Close coexistence module",
                        HRESULT_FROM_WIN32(GetLastError()));
      *Module = nullptr;
    }
    return Result;
  }
};
#endif

HRESULT queryWavePolicy(ID3D12Device *Device) {
#ifdef FEME_WARP_PROFILE_VERSION
  std::printf("Internal capability inventory: version=%u; product FL=none\n",
              static_cast<unsigned>(FEME_WARP_PROFILE_VERSION));
#endif
  D3D12_FEATURE_DATA_D3D12_OPTIONS1 Options{};
  HRESULT Result =
      report("Query owned wave policy",
             Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &Options,
                                         sizeof(Options)));
  if (FAILED(Result))
    return Result;
  std::printf("Owned wave policy: min=%u max=%u total=%u ops=%u\n",
              Options.WaveLaneCountMin, Options.WaveLaneCountMax,
              Options.TotalLaneCount, Options.WaveOps);
  return Options.WaveLaneCountMin == 4 && Options.WaveLaneCountMax == 4 &&
                 Options.TotalLaneCount == 4 && !Options.WaveOps
             ? S_OK
             : report("Owned wave policy mismatch", E_FAIL);
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
                              ComPtr<ID3D12PipelineState> &Pipeline,
                              const D3D12_SHADER_BYTECODE *DXIL = nullptr,
                              const char *LegacyProfile = "cs_5_0",
                              const D3D_SHADER_MACRO *Defines = nullptr) {
  ComPtr<ID3DBlob> Shader, Errors, RootBlob;
  HRESULT Result = S_OK;
  if (!DXIL) {
    Result = report("Compile application compute shader",
                    D3DCompile(Source, SourceSize, "FeMe command test", Defines,
                               nullptr, "main", LegacyProfile, 0, 0, &Shader,
                               &Errors));
    if (FAILED(Result)) {
      if (Errors)
        std::fprintf(stderr, "%.*s\n",
                     static_cast<int>(Errors->GetBufferSize()),
                     static_cast<const char *>(Errors->GetBufferPointer()));
      return Result;
    }
  } else if (!DXIL->pShaderBytecode || !DXIL->BytecodeLength) {
    return report("Missing original DXC shader", E_INVALIDARG);
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
  PipelineDesc.CS = DXIL ? *DXIL
                         : D3D12_SHADER_BYTECODE{Shader->GetBufferPointer(),
                                                 Shader->GetBufferSize()};
  Result = report("Create application compute pipeline",
                  Device->CreateComputePipelineState(&PipelineDesc,
                                                     IID_PPV_ARGS(&Pipeline)));
  return Result;
}

HRESULT exerciseFences(ID3D12Device *Device) {
  ComPtr<ID3D12Fence> Fence;
  ComPtr<ID3D12CommandQueue> Queue;
  HRESULT Result = report(
      "Create application fence",
      Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)));
  if (FAILED(Result))
    return Result;
  D3D12_COMMAND_QUEUE_DESC QueueDesc{};
  QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  Result = report("Create fence command queue",
                  Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue)));
  if (FAILED(Result))
    return Result;
  HANDLE Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!Event)
    return report("Create fence completion event",
                  HRESULT_FROM_WIN32(GetLastError()));
  bool PendingWait = false;
  Result = [&]() -> HRESULT {
    if (Fence->GetCompletedValue() != 0)
      return report("Initial fence value mismatch", E_FAIL);
    HRESULT Status = report("Arm CPU fence completion",
                            Fence->SetEventOnCompletion(1, Event));
    if (FAILED(Status))
      return Status;
    if (WaitForSingleObject(Event, 0) != WAIT_TIMEOUT)
      return report("Unsignaled CPU fence completed", E_FAIL);
    Status = report("Signal CPU fence", Fence->Signal(1));
    if (FAILED(Status))
      return Status;
    if (WaitForSingleObject(Event, 5000) != WAIT_OBJECT_0 ||
        Fence->GetCompletedValue() != 1)
      return report("CPU fence completion mismatch", E_FAIL);
    Status = report("Decrease CPU fence value", Fence->Signal(0));
    if (FAILED(Status))
      return Status;
    if (Fence->GetCompletedValue() != 0)
      return report("Decreasing CPU fence value was lost", E_FAIL);
    Status = report("Queue future fence wait", Queue->Wait(Fence.Get(), 3));
    if (FAILED(Status))
      return Status;
    PendingWait = true;
    Status =
        report("Queue ordered fence signal", Queue->Signal(Fence.Get(), 4));
    if (FAILED(Status))
      return Status;
    Status = report("Arm queue fence completion",
                    Fence->SetEventOnCompletion(4, Event));
    if (FAILED(Status))
      return Status;
    if (WaitForSingleObject(Event, 100) != WAIT_TIMEOUT)
      return report("Queue signal bypassed future wait", E_FAIL);
    Status = report("Decrease pending-wait fence", Fence->Signal(0));
    if (FAILED(Status))
      return Status;
    if (WaitForSingleObject(Event, 100) != WAIT_TIMEOUT ||
        Fence->GetCompletedValue() != 0)
      return report("Decreasing signal bypassed future wait", E_FAIL);
    Status = report("Release future fence wait", Fence->Signal(3));
    if (FAILED(Status))
      return Status;
    PendingWait = false;
    if (WaitForSingleObject(Event, 5000) != WAIT_OBJECT_0 ||
        Fence->GetCompletedValue() != 4)
      return report("Kernel-ordered fence completion mismatch", E_FAIL);
    std::printf(
        "Owned monitored fence lifecycle and future kernel wait PASS\n");
    return report("Fence removal status", Device->GetDeviceRemovedReason());
  }();
  if (PendingWait) {
    HRESULT Cleanup =
        report("Release failed fence test wait", Fence->Signal(3));
    if (FAILED(Cleanup))
      Result = Cleanup;
  }
  if (!CloseHandle(Event))
    Result = report("Close fence completion event",
                    HRESULT_FROM_WIN32(GetLastError()));
  return Result;
}

HRESULT waitFence(ID3D12Fence *Fence, UINT64 Value) {
  HANDLE Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!Event)
    return report("Create command completion event",
                  HRESULT_FROM_WIN32(GetLastError()));
  HRESULT Result = report("Arm command completion",
                          Fence->SetEventOnCompletion(Value, Event));
  if (SUCCEEDED(Result)) {
    DWORD Wait = WaitForSingleObject(Event, 10000);
    if (Wait != WAIT_OBJECT_0)
      Result = report("Wait command completion",
                      HRESULT_FROM_WIN32(Wait == WAIT_FAILED ? GetLastError()
                                                             : ERROR_TIMEOUT));
  }
  if (!CloseHandle(Event))
    Result = report("Close command completion event",
                    HRESULT_FROM_WIN32(GetLastError()));
  return Result;
}

HRESULT waitQueue(ID3D12CommandQueue *Queue, ID3D12Fence *Fence, UINT64 Value) {
  HRESULT Result =
      report("Signal command completion", Queue->Signal(Fence, Value));
  return FAILED(Result) ? Result : waitFence(Fence, Value);
}

HRESULT exercisePlacedBuffers(ID3D12Device *Device) {
  constexpr UINT64 Bytes = 4097;
  D3D12_RESOURCE_DESC Desc{};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = Bytes;
  Desc.Height = 1;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  auto Info = Device->GetResourceAllocationInfo(0, 1, &Desc);
  if (Info.SizeInBytes != 65536 || Info.Alignment != 65536)
    return report("Placed buffer allocation footprint mismatch", E_FAIL);
  D3D12_HEAP_DESC HeapDesc{};
  HeapDesc.SizeInBytes = 3 * Info.SizeInBytes;
  HeapDesc.Alignment = Info.Alignment;
  HeapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
  HeapDesc.Properties.CreationNodeMask = 1;
  HeapDesc.Properties.VisibleNodeMask = 1;
  HeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  ComPtr<ID3D12Heap> Heap;
  HRESULT Result = report("Create placed-buffer heap",
                          Device->CreateHeap(&HeapDesc, IID_PPV_ARGS(&Heap)));
  if (FAILED(Result))
    return Result;
  ComPtr<ID3D12Resource> Buffers[3];
  for (UINT I = 0; I < 3; ++I) {
    Result = report(
        "CreatePlacedResource",
        Device->CreatePlacedResource(Heap.Get(), I == 1 ? Info.SizeInBytes : 0,
                                     &Desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                     nullptr, IID_PPV_ARGS(&Buffers[I])));
    if (FAILED(Result))
      return Result;
  }
  if (!Buffers[0]->GetGPUVirtualAddress() ||
      Buffers[1]->GetGPUVirtualAddress() !=
          Buffers[0]->GetGPUVirtualAddress() + Info.SizeInBytes ||
      Buffers[2]->GetGPUVirtualAddress() != Buffers[0]->GetGPUVirtualAddress())
    return report("Placed buffer GPUVA mismatch", E_FAIL);
  D3D12_RANGE NoRead{0, 0}, Written{0, static_cast<SIZE_T>(Bytes)};
  for (UINT I = 0; I < 2; ++I) {
    void *Data = nullptr;
    Result = report("Map placed buffer", Buffers[I]->Map(0, &NoRead, &Data));
    if (FAILED(Result))
      return Result;
    if (!Data) {
      Buffers[I]->Unmap(0, &NoRead);
      return report("Missing placed buffer mapping", E_FAIL);
    }
    for (UINT64 J = 0; J < Bytes; ++J)
      static_cast<unsigned char *>(Data)[J] =
          static_cast<unsigned char>(J * (I ? 5 : 3) + (I ? 11 : 7));
    Buffers[I]->Unmap(0, &Written);
  }
  void *Alias = nullptr;
  Result =
      report("Map overlapping placement", Buffers[2]->Map(0, &NoRead, &Alias));
  if (FAILED(Result))
    return Result;
  if (!Alias) {
    Buffers[2]->Unmap(0, &NoRead);
    return report("Missing alias mapping", E_FAIL);
  }
  static_cast<unsigned char *>(Alias)[0] = 0x42;
  Buffers[2]->Unmap(0, &Written);
  D3D12_HEAP_PROPERTIES ReadbackHeap{};
  ReadbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
  ReadbackHeap.CreationNodeMask = 1;
  ReadbackHeap.VisibleNodeMask = 1;
  Desc.Width = 2 * Bytes;
  ComPtr<ID3D12Resource> Readback;
  Result = report(
      "Create placed-buffer readback",
      Device->CreateCommittedResource(&ReadbackHeap, D3D12_HEAP_FLAG_NONE,
                                      &Desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                      nullptr, IID_PPV_ARGS(&Readback)));
  if (FAILED(Result))
    return Result;
  D3D12_COMMAND_QUEUE_DESC QueueDesc{};
  QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12CommandQueue> Queue;
  ComPtr<ID3D12CommandAllocator> Allocator;
  ComPtr<ID3D12GraphicsCommandList> List;
  ComPtr<ID3D12Fence> Fence;
  Result = Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue));
  if (SUCCEEDED(Result))
    Result = Device->CreateCommandAllocator(QueueDesc.Type,
                                            IID_PPV_ARGS(&Allocator));
  if (SUCCEEDED(Result))
    Result = Device->CreateCommandList(0, QueueDesc.Type, Allocator.Get(),
                                       nullptr, IID_PPV_ARGS(&List));
  if (SUCCEEDED(Result))
    Result =
        Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence));
  if (FAILED(Result))
    return report("Create placed-buffer copy objects", Result);
  D3D12_RESOURCE_BARRIER AliasBarrier{};
  AliasBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
  AliasBarrier.Aliasing.pResourceBefore = Buffers[2].Get();
  AliasBarrier.Aliasing.pResourceAfter = Buffers[0].Get();
  List->ResourceBarrier(1, &AliasBarrier);
  List->CopyBufferRegion(Readback.Get(), 0, Buffers[0].Get(), 0, Bytes);
  AliasBarrier.Aliasing = {};
  List->ResourceBarrier(1, &AliasBarrier);
  List->CopyBufferRegion(Readback.Get(), Bytes, Buffers[1].Get(), 0, Bytes);
  Result = List->Close();
  if (FAILED(Result))
    return report("Close placed-buffer copy list", Result);
  ID3D12CommandList *Lists[] = {List.Get()};
  Queue->ExecuteCommandLists(1, Lists);
  Result = waitQueue(Queue.Get(), Fence.Get(), 1);
  if (FAILED(Result))
    return Result;
  Buffers[2].Reset();
  void *Data = nullptr;
  D3D12_RANGE Read{0, static_cast<SIZE_T>(Desc.Width)};
  Result = Readback->Map(0, &Read, &Data);
  if (FAILED(Result))
    return report("Map placed-buffer readback", Result);
  bool Valid = Data != nullptr;
  if (Data)
    for (UINT64 J = 0; J < Bytes; ++J) {
      auto *Values = static_cast<const unsigned char *>(Data);
      Valid &=
          Values[J] == (J ? static_cast<unsigned char>(J * 3 + 7) : 0x42) &&
          Values[Bytes + J] == static_cast<unsigned char>(J * 5 + 11);
    }
  Readback->Unmap(0, &NoRead);
  if (!Valid)
    return report("Placed-buffer alias/copy readback mismatch", E_FAIL);
  return report("Placed-buffer removal status",
                Device->GetDeviceRemovedReason());
}

struct DelayedFenceSignal {
  ID3D12CommandQueue *Queue;
  ID3D12Fence *Fence;
  UINT64 Value;
  HRESULT Result = E_FAIL;
};

DWORD WINAPI signalDelayedFence(void *Argument) {
  auto *Signal = static_cast<DelayedFenceSignal *>(Argument);
  Sleep(100);
  Signal->Result = Signal->Queue->Signal(Signal->Fence, Signal->Value);
  if (FAILED(Signal->Result))
    report("Release failed delayed queue signal",
           Signal->Fence->Signal(Signal->Value));
  return 0;
}

HRESULT exerciseCommands(ID3D12Device *Device,
                         const D3D12_SHADER_BYTECODE *DXIL = nullptr) {
  constexpr char Source[] = "[numthreads(1,1,1)] void main() {}";
  D3D12_ROOT_SIGNATURE_DESC RootDesc{};
  ComPtr<ID3D12RootSignature> Root;
  ComPtr<ID3D12PipelineState> Pipeline;
  HRESULT Result = createComputePipeline(Device, Source, sizeof(Source) - 1,
                                         RootDesc, Root, Pipeline, DXIL);
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
    ComPtr<ID3D12Fence> Completion;
    Result =
        report("Create application queue",
               Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue)));
    if (FAILED(Result))
      return Result;
    Result = report("Create command completion fence",
                    Device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                        IID_PPV_ARGS(&Completion)));
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
      Result = waitQueue(Queue.Get(), Completion.Get(), Repeat + 1);
      if (FAILED(Result))
        return Result;
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

enum class BufferCounterOperation {
  None,
  Increment,
  Decrement,
  Append,
  Consume
};

HRESULT exerciseFootprintCopies(ID3D12Device *Device, bool Ordered,
                                bool TextureStorage, bool ArrayTexture) {
  constexpr UINT Bytes = 20480, SourceOffset = 512, DestinationOffset = 1024;
  constexpr UINT DestinationPitch = 512;
  const UINT SourcePitch = ArrayTexture ? 512 : 256;
  const UINT SourceSlicePitch = SourcePitch * 13;
  constexpr UINT DestinationSlicePitch = DestinationPitch * 17;
  std::vector<unsigned char> SourceData(Bytes, 0xa5), Expected(Bytes, 0xcd);
  for (UINT Z = 0; Z < 2; ++Z)
    for (UINT Y = 0; Y < 13; ++Y)
      for (UINT X = 0; X < 19; ++X)
        for (UINT Channel = 0; Channel < 4; ++Channel)
          SourceData[SourceOffset + Z * SourceSlicePitch + Y * SourcePitch +
                     X * 4 + Channel] =
              static_cast<unsigned char>(X * 17 + Y * 31 + Z * 113 +
                                         Channel * 53);
  D3D12_HEAP_PROPERTIES Heap{};
  Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  Heap.CreationNodeMask = Heap.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC Desc{};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = Bytes;
  Desc.Height = Desc.DepthOrArraySize = Desc.MipLevels = 1;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> Upload, Readback;
  HRESULT Result =
      report("Create footprint source",
             Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ,
                                             nullptr, IID_PPV_ARGS(&Upload)));
  if (FAILED(Result))
    return Result;
  Heap.Type = D3D12_HEAP_TYPE_READBACK;
  Result =
      report("Create footprint destination",
             Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc,
                                             D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr, IID_PPV_ARGS(&Readback)));
  if (FAILED(Result))
    return Result;
  ComPtr<ID3D12Resource> Texture;
  if (TextureStorage) {
    Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    Desc.Dimension = ArrayTexture ? D3D12_RESOURCE_DIMENSION_TEXTURE2D
                                  : D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    Desc.Width = 19;
    Desc.Height = 13;
    Desc.DepthOrArraySize = 2;
    Desc.MipLevels = 3;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UINT;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    Result = report(
        "Create mipmapped copy texture",
        Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc,
                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                        IID_PPV_ARGS(&Texture)));
    if (FAILED(Result))
      return Result;
  }
  D3D12_RANGE NoRead{0, 0}, Full{0, Bytes};
  void *Memory = nullptr;
  for (auto *Resource : {Upload.Get(), Readback.Get()}) {
    Result = report("Map footprint seed", Resource->Map(0, &NoRead, &Memory));
    if (FAILED(Result))
      return Result;
    if (Memory)
      std::memcpy(Memory,
                  Resource == Upload.Get() ? SourceData.data()
                                           : Expected.data(),
                  Bytes);
    Resource->Unmap(0, &Full);
    if (!Memory)
      return report("Missing footprint seed mapping", E_FAIL);
  }
  ComPtr<ID3D12CommandQueue> Queue;
  ComPtr<ID3D12CommandAllocator> Allocator;
  ComPtr<ID3D12GraphicsCommandList> List;
  D3D12_COMMAND_QUEUE_DESC QueueDesc{};
  Result = report("Create footprint queue",
                  Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue)));
  if (SUCCEEDED(Result))
    Result =
        report("Create footprint allocator",
               Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&Allocator)));
  if (SUCCEEDED(Result))
    Result = report("Create footprint list",
                    Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              Allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&List)));
  if (FAILED(Result))
    return Result;
  D3D12_TEXTURE_COPY_LOCATION Source{}, Destination{};
  Source.pResource = Upload.Get();
  Destination.pResource = Readback.Get();
  Source.Type = Destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  Source.PlacedFootprint.Offset = SourceOffset;
  Destination.PlacedFootprint.Offset = DestinationOffset;
  Source.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UINT, 19, 13, 2,
                                      SourcePitch};
  Destination.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UINT, 23, 17, 2,
                                           DestinationPitch};
  if (TextureStorage) {
    D3D12_TEXTURE_COPY_LOCATION Target{};
    Target.pResource = Texture.Get();
    Target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    for (UINT Layer = 0; Layer < (ArrayTexture ? 2u : 1u); ++Layer) {
      auto Input = Source;
      Input.PlacedFootprint.Offset += Layer * SourceSlicePitch;
      if (ArrayTexture)
        Input.PlacedFootprint.Footprint.Depth = 1;
      Target.SubresourceIndex = Layer * 3;
      List->CopyTextureRegion(&Target, 0, 0, 0, &Input, nullptr);
      for (UINT Mip = 1; Mip < 3; ++Mip) {
        Target.SubresourceIndex = Layer * 3 + Mip;
        Input = Source;
        Input.PlacedFootprint.Offset +=
            Layer * SourceSlicePitch + Mip * 2 * SourcePitch;
        Input.PlacedFootprint.Footprint.Width = 19 >> Mip;
        Input.PlacedFootprint.Footprint.Height = 13 >> Mip;
        Input.PlacedFootprint.Footprint.Depth = 1;
        List->CopyTextureRegion(&Target, 0, 0, 0, &Input, nullptr);
      }
      Input = {};
    }
    D3D12_RESOURCE_BARRIER Barrier{};
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Texture.Get();
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    List->ResourceBarrier(1, &Barrier);
    Source = {};
    Source.pResource = Texture.Get();
    Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  }
  auto UpdateExpected = [&](UINT X, UINT Y, UINT Z, const D3D12_BOX &Box) {
    for (UINT Slice = Box.front; Slice < Box.back; ++Slice)
      for (UINT Row = Box.top; Row < Box.bottom; ++Row)
        std::memcpy(Expected.data() + DestinationOffset +
                        (Z + Slice - Box.front) * DestinationSlicePitch +
                        (Y + Row - Box.top) * DestinationPitch + X * 4,
                    SourceData.data() + SourceOffset +
                        Slice * SourceSlicePitch + Row * SourcePitch +
                        Box.left * 4,
                    (Box.right - Box.left) * 4);
  };
  if (ArrayTexture)
    for (UINT Layer = 0; Layer < 2; ++Layer) {
      Source.SubresourceIndex = Layer * 3;
      List->CopyTextureRegion(&Destination, 2, 3, Layer, &Source, nullptr);
      UpdateExpected(2, 3, Layer, {0, 0, Layer, 19, 13, Layer + 1});
    }
  else {
    List->CopyTextureRegion(&Destination, 2, 3, 0, &Source, nullptr);
    UpdateExpected(2, 3, 0, {0, 0, 0, 19, 13, 2});
  }
  D3D12_BOX Box{4, 2, 1, 11, 8, 2};
  if (ArrayTexture) {
    Source.SubresourceIndex = 3;
    Box.front = 0;
    Box.back = 1;
  }
  List->CopyTextureRegion(&Destination, 3, 5, 0, &Source, &Box);
  UpdateExpected(3, 5, 0, {4, 2, 1, 11, 8, 2});
  if (TextureStorage)
    for (UINT Layer = 0; Layer < (ArrayTexture ? 2u : 1u); ++Layer)
      for (UINT Mip = 1; Mip < 3; ++Mip) {
        Source.SubresourceIndex = Layer * 3 + Mip;
        List->CopyTextureRegion(&Destination, 4 + Mip * 2, 2 + Mip * 2,
                                ArrayTexture ? Layer : 1, &Source, nullptr);
        UpdateExpected(
            4 + Mip * 2, 2 + Mip * 2, ArrayTexture ? Layer : 1,
            {0, Mip * 2, Layer, 19u >> Mip, Mip * 2 + (13u >> Mip), Layer + 1});
      }
  Box = {};
  Source.PlacedFootprint.Footprint = {};
  Destination.PlacedFootprint.Footprint = {};
  Result = report("Close footprint list", List->Close());
  if (FAILED(Result))
    return Result;
  ComPtr<ID3D12Fence> Completion, Gate;
  Result = report(
      "Create footprint completion",
      Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Completion)));
  if (Ordered && SUCCEEDED(Result))
    Result = report(
        "Create footprint gate",
        Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Gate)));
  if (FAILED(Result))
    return Result;
  if (Ordered) {
    Result = report("Hold footprint queue", Queue->Wait(Gate.Get(), 1));
    if (FAILED(Result))
      return Result;
  }
  ID3D12CommandList *Lists[] = {List.Get()};
  Queue->ExecuteCommandLists(1, Lists);
  Result =
      report("Signal footprint completion", Queue->Signal(Completion.Get(), 1));
  if (Ordered) {
    HRESULT MapResult =
        report("Map held footprint output", Readback->Map(0, &Full, &Memory));
    bool Untouched = SUCCEEDED(MapResult) && Memory;
    if (Untouched)
      for (UINT I = 0; I < Bytes; ++I)
        Untouched &= static_cast<const unsigned char *>(Memory)[I] == 0xcd;
    if (SUCCEEDED(MapResult))
      Readback->Unmap(0, &NoRead);
    List.Reset();
    Allocator.Reset();
    HRESULT Released = report("Release footprint gate", Gate->Signal(1));
    if (FAILED(Released))
      return Released;
    if (FAILED(MapResult))
      return MapResult;
    if (!Untouched)
      return report("Footprint copy ran before its queue gate", E_FAIL);
  }
  if (FAILED(Result))
    return Result;
  Result = waitFence(Completion.Get(), 1);
  if (SUCCEEDED(Result))
    Result =
        report("Footprint removal status", Device->GetDeviceRemovedReason());
  if (SUCCEEDED(Result))
    Result = report("Map footprint output", Readback->Map(0, &Full, &Memory));
  if (FAILED(Result))
    return Result;
  bool Valid = Memory && std::memcmp(Memory, Expected.data(), Bytes) == 0;
  Readback->Unmap(0, &NoRead);
  if (!Valid)
    return report("Pitched footprint bytes or guards mismatch", E_FAIL);
  if (Upload) {
    Result =
        report("Map footprint source check", Upload->Map(0, &Full, &Memory));
    if (FAILED(Result))
      return Result;
    Valid = Memory && std::memcmp(Memory, SourceData.data(), Bytes) == 0;
    Upload->Unmap(0, &NoRead);
    if (!Valid)
      return report("Pitched footprint copy modified source storage", E_FAIL);
  }
  std::printf(
      "Pitched footprint rows/slices, regions, guards and lifetime PASS "
      "(texture=%u array=%u)\n",
      TextureStorage, ArrayTexture);
  return S_OK;
}

HRESULT exerciseTextureViews(ID3D12Device *Device,
                             const D3D12_SHADER_BYTECODE &Shader, bool Ordered,
                             bool SingleLayer) {
  constexpr UINT UploadBytes = 16384, SeedOffset = 15872;
  constexpr UINT Words = 34, Bytes = Words * sizeof(UINT);
  std::array<UINT, Words> Seed;
  Seed.fill(42);
  auto CreateBuffer = [&](D3D12_HEAP_TYPE Type, UINT Size,
                          D3D12_RESOURCE_FLAGS Flags,
                          D3D12_RESOURCE_STATES State,
                          ComPtr<ID3D12Resource> &Resource) {
    D3D12_HEAP_PROPERTIES Heap{};
    Heap.Type = Type;
    Heap.CreationNodeMask = Heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC Desc{};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = Size;
    Desc.Height = Desc.DepthOrArraySize = Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Desc.Flags = Flags;
    return report("Create texture-view buffer",
                  Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE,
                                                  &Desc, State, nullptr,
                                                  IID_PPV_ARGS(&Resource)));
  };
  ComPtr<ID3D12Resource> Upload, Output, Readback;
  HRESULT Result = CreateBuffer(D3D12_HEAP_TYPE_UPLOAD, UploadBytes,
                                D3D12_RESOURCE_FLAG_NONE,
                                D3D12_RESOURCE_STATE_GENERIC_READ, Upload);
  if (SUCCEEDED(Result))
    Result = CreateBuffer(D3D12_HEAP_TYPE_DEFAULT, Bytes,
                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                          D3D12_RESOURCE_STATE_COPY_DEST, Output);
  if (SUCCEEDED(Result))
    Result =
        CreateBuffer(D3D12_HEAP_TYPE_READBACK, Bytes, D3D12_RESOURCE_FLAG_NONE,
                     D3D12_RESOURCE_STATE_COPY_DEST, Readback);
  if (FAILED(Result))
    return Result;
  std::array<ComPtr<ID3D12Resource>, 2> Textures;
  std::array<std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 6>, 2> Footprints{};
  std::vector<unsigned char> Data(UploadBytes, 0xa5);
  std::memcpy(Data.data() + SeedOffset, Seed.data(), Bytes);
  D3D12_RESOURCE_DESC TextureDesc{};
  TextureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  TextureDesc.Width = 8;
  TextureDesc.Height = 4;
  TextureDesc.DepthOrArraySize = 3;
  TextureDesc.MipLevels = 2;
  TextureDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
  TextureDesc.SampleDesc.Count = 1;
  D3D12_HEAP_PROPERTIES Heap{};
  Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  Heap.CreationNodeMask = Heap.VisibleNodeMask = 1;
  for (UINT I = 0; I < Textures.size(); ++I) {
    Result = report("Create shader-readable mip/array texture",
                    Device->CreateCommittedResource(
                        &Heap, D3D12_HEAP_FLAG_NONE, &TextureDesc,
                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                        IID_PPV_ARGS(&Textures[I])));
    if (FAILED(Result))
      return Result;
    UINT64 Total = 0;
    Device->GetCopyableFootprints(&TextureDesc, 0, 6, I * 8192,
                                  Footprints[I].data(), nullptr, nullptr,
                                  &Total);
    if (Total > 8192)
      return report("Unexpected texture-view upload footprint extent", E_FAIL);
    for (UINT Subresource = 0; Subresource < 6; ++Subresource) {
      const auto &Layout = Footprints[I][Subresource];
      UINT Mip = Subresource % 2, Layer = Subresource / 2;
      for (UINT Y = 0; Y < Layout.Footprint.Height; ++Y)
        for (UINT X = 0; X < Layout.Footprint.Width; ++X) {
          std::array<UINT, 4> Texel{};
          for (UINT C = 0; C < Texel.size(); ++C)
            Texel[C] =
                I * 1000 + Mip * 100 + Layer * 40 + Y * 10 + X + C * 10000;
          auto Offset = Layout.Offset + UINT64(Y) * Layout.Footprint.RowPitch +
                        X * sizeof(Texel);
          if (Offset > SeedOffset || sizeof(Texel) > SeedOffset - Offset)
            return report("Texture-view upload span overlaps output seed",
                          E_FAIL);
          std::memcpy(Data.data() + Offset, Texel.data(), sizeof(Texel));
        }
    }
  }
  D3D12_RANGE NoRead{0, 0}, Full{0, Bytes}, UploadRange{0, UploadBytes};
  void *Memory = nullptr;
  Result = report("Map texture-view upload", Upload->Map(0, &NoRead, &Memory));
  if (FAILED(Result))
    return Result;
  if (!Memory)
    return report("Missing texture-view upload mapping", E_FAIL);
  std::memcpy(Memory, Data.data(), UploadBytes);
  Upload->Unmap(0, &UploadRange);
  Result =
      report("Seed texture-view readback", Readback->Map(0, &NoRead, &Memory));
  if (FAILED(Result))
    return Result;
  if (!Memory)
    return report("Missing texture-view readback mapping", E_FAIL);
  std::memcpy(Memory, Seed.data(), Bytes);
  Readback->Unmap(0, &Full);

  D3D12_DESCRIPTOR_RANGE Range{};
  Range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  Range.NumDescriptors = 2;
  Range.BaseShaderRegister = 3;
  Range.RegisterSpace = 7;
  std::array<D3D12_ROOT_PARAMETER, 2> Parameters{};
  Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  Parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  Parameters[1].DescriptorTable = {1, &Range};
  D3D12_ROOT_SIGNATURE_DESC RootDesc{};
  RootDesc.NumParameters = static_cast<UINT>(Parameters.size());
  RootDesc.pParameters = Parameters.data();
  ComPtr<ID3D12RootSignature> Root;
  ComPtr<ID3D12PipelineState> Pipeline;
  Result = createComputePipeline(Device, nullptr, 0, RootDesc, Root, Pipeline,
                                 &Shader);
  if (FAILED(Result))
    return Result;
  D3D12_DESCRIPTOR_HEAP_DESC HeapDesc{};
  HeapDesc.NumDescriptors = 2;
  HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  ComPtr<ID3D12DescriptorHeap> CPUHeap, GPUHeap;
  Result =
      report("Create texture-view CPU heap",
             Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&CPUHeap)));
  HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (SUCCEEDED(Result))
    Result =
        report("Create texture-view GPU heap",
               Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&GPUHeap)));
  if (FAILED(Result))
    return Result;
  UINT Step = Device->GetDescriptorHandleIncrementSize(HeapDesc.Type);
  auto CPU = CPUHeap->GetCPUDescriptorHandleForHeapStart();
  auto GPUCPU = GPUHeap->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC View{};
  View.Format = TextureDesc.Format;
  View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
  View.Texture2DArray.ArraySize = 3;
  View.Texture2DArray.MipLevels = 2;
  auto WriteViews = [&](bool Selected, bool Null) {
    View.Texture2DArray.MostDetailedMip = Selected ? 1 : 0;
    View.Texture2DArray.FirstArraySlice = Selected ? 1 : 0;
    View.Texture2DArray.ArraySize = Selected ? (SingleLayer ? 1 : 2) : 3;
    View.Texture2DArray.MipLevels = Selected ? 1 : 2;
    for (UINT I = 0; I < 2; ++I) {
      View.Shader4ComponentMapping =
          I ? D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING
            : D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(2, 0, 5, 4);
      Device->CreateShaderResourceView(Null && !I ? nullptr : Textures[I].Get(),
                                       &View, {CPU.ptr + I * Step});
    }
    Device->CopyDescriptorsSimple(2, GPUCPU, CPU, HeapDesc.Type);
  };
  WriteViews(false, false);
  ComPtr<ID3D12CommandQueue> Queue;
  ComPtr<ID3D12CommandAllocator> Allocator;
  ComPtr<ID3D12GraphicsCommandList> List;
  ComPtr<ID3D12Fence> Completion, Gate;
  D3D12_COMMAND_QUEUE_DESC QueueDesc{};
  Result = report("Create texture-view queue",
                  Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue)));
  if (SUCCEEDED(Result))
    Result = Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&Allocator));
  if (SUCCEEDED(Result))
    Result = Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       Allocator.Get(), nullptr,
                                       IID_PPV_ARGS(&List));
  if (SUCCEEDED(Result))
    Result = Device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                 IID_PPV_ARGS(&Completion));
  if (Ordered && SUCCEEDED(Result))
    Result = Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Gate));
  if (FAILED(Result))
    return report("Create texture-view command objects", Result);
  D3D12_RESOURCE_BARRIER Barrier{};
  Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  Barrier.Transition.StateAfter =
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  for (UINT I = 0; I < 2; ++I) {
    for (UINT Subresource = 0; Subresource < 6; ++Subresource) {
      D3D12_TEXTURE_COPY_LOCATION Source{}, Destination{};
      Source.pResource = Upload.Get();
      Source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      Source.PlacedFootprint = Footprints[I][Subresource];
      Destination.pResource = Textures[I].Get();
      Destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      Destination.SubresourceIndex = Subresource;
      List->CopyTextureRegion(&Destination, 0, 0, 0, &Source, nullptr);
    }
    Barrier.Transition.pResource = Textures[I].Get();
    List->ResourceBarrier(1, &Barrier);
  }
  List->CopyBufferRegion(Output.Get(), 0, Upload.Get(), SeedOffset, Bytes);
  Barrier.Transition.pResource = Output.Get();
  Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  List->ResourceBarrier(1, &Barrier);
  Result = List->Close();
  if (FAILED(Result))
    return report("Close texture-view upload list", Result);
  ID3D12CommandList *Lists[] = {List.Get()};
  Queue->ExecuteCommandLists(1, Lists);
  Result = Queue->Signal(Completion.Get(), 1);
  if (SUCCEEDED(Result))
    Result = waitFence(Completion.Get(), 1);
  if (SUCCEEDED(Result))
    Result = Allocator->Reset();
  if (SUCCEEDED(Result))
    Result = List->Reset(Allocator.Get(), Pipeline.Get());
  if (FAILED(Result))
    return report("Prepare texture-view dispatch recording", Result);
  ID3D12DescriptorHeap *Heaps[] = {GPUHeap.Get()};
  List->SetDescriptorHeaps(1, Heaps);
  List->SetComputeRootSignature(Root.Get());
  List->SetComputeRootUnorderedAccessView(0, Output->GetGPUVirtualAddress());
  List->SetComputeRootDescriptorTable(
      1, GPUHeap->GetGPUDescriptorHandleForHeapStart());
  List->Dispatch(1, 1, 1);
  Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  List->ResourceBarrier(1, &Barrier);
  List->CopyBufferRegion(Readback.Get(), 0, Output.Get(), 0, Bytes);
  Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
  Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  List->ResourceBarrier(1, &Barrier);
  Result = List->Close();
  if (FAILED(Result))
    return report("Close texture-view dispatch list", Result);
  WriteViews(true, false);
  for (UINT Repeat = 0; Repeat < (Ordered ? 1u : 3u); ++Repeat) {
    bool Null = Repeat == 2;
    if (Null)
      WriteViews(true, true);
    if (Ordered) {
      Result = Queue->Wait(Gate.Get(), 1);
      if (FAILED(Result))
        return report("Hold texture-view queue", Result);
    }
    Lists[0] = List.Get();
    Queue->ExecuteCommandLists(1, Lists);
    Result = Queue->Signal(Completion.Get(), Repeat + 2);
    if (Ordered) {
      HRESULT Status = Readback->Map(0, &Full, &Memory);
      bool Untouched = SUCCEEDED(Status) && Memory &&
                       std::memcmp(Memory, Seed.data(), Bytes) == 0;
      if (SUCCEEDED(Status))
        Readback->Unmap(0, &NoRead);
      List.Reset();
      Allocator.Reset();
      CPUHeap.Reset();
      HRESULT Released = Gate->Signal(1);
      if (FAILED(Released))
        return report("Release texture-view queue", Released);
      if (FAILED(Status))
        return report("Map held texture-view output", Status);
      if (!Untouched)
        return report("Texture shader ran before its queue gate", E_FAIL);
    }
    if (SUCCEEDED(Result))
      Result = waitFence(Completion.Get(), Repeat + 2);
    if (SUCCEEDED(Result))
      Result = Device->GetDeviceRemovedReason();
    if (SUCCEEDED(Result))
      Result = Readback->Map(0, &Full, &Memory);
    if (FAILED(Result))
      return report("Read texture-view results", Result);
    auto Expected = Seed;
    for (UINT Thread = 0; Thread < 4; ++Thread) {
      UINT Index = Thread & 1;
      UINT Base = Index * 1000 + 100 + (1 + Thread / 2) * 40 + Thread;
      for (UINT C = 0; C < 4; ++C)
        Expected[1 + Thread * 8 + C] =
            (Null && !Index) || (SingleLayer && Thread >= 2) ? 0
            : Index                                          ? Base + C * 10000
            : C == 0                                         ? Base + 20000
            : C == 1                                         ? Base
            : C == 2                                         ? 1
                                                             : 0;
      for (UINT C = 0; C < 4; ++C)
        Expected[5 + Thread * 8 + C] = C == 0   ? 4
                                       : C == 1 ? 2
                                       : C == 2 ? (SingleLayer ? 1 : 2)
                                                : 1;
    }
    bool Valid = Memory && std::memcmp(Memory, Expected.data(), Bytes) == 0;
    if (!Valid && Memory) {
      const auto *Actual = static_cast<const UINT *>(Memory);
      for (UINT I = 0; I < Words; ++I)
        if (Actual[I] != Expected[I])
          std::printf("Texture-view replay %u word %u: actual=%u expected=%u\n",
                      Repeat, I, Actual[I], Expected[I]);
    }
    Readback->Unmap(0, &NoRead);
    if (!Valid)
      return report("Texture-view data/dimensions/nulls or guards mismatch",
                    E_FAIL);
  }
  Result = Upload->Map(0, &UploadRange, &Memory);
  if (FAILED(Result))
    return report("Map texture-view source check", Result);
  bool Valid = Memory && std::memcmp(Memory, Data.data(), UploadBytes) == 0;
  Upload->Unmap(0, &NoRead);
  if (!Valid)
    return report("Texture-view execution changed upload storage", E_FAIL);
  std::printf("Live texture SRV mips/layers, mappings, dimensions, guards and "
              "lifetime PASS (ordered=%u single-layer=%u)\n",
              Ordered, SingleLayer);
  return S_OK;
}

HRESULT exerciseBufferCommands(
    ID3D12Device *Device, const D3D12_SHADER_BYTECODE *DXIL = nullptr,
    bool Ordered = false, bool Indirect = false, bool CBV = false,
    bool Inline = false, bool Tables = false, bool Views = false,
    bool Structured = false, bool Arrays = false,
    DXGI_FORMAT Typed = DXGI_FORMAT_UNKNOWN, bool Predication = false,
    BufferCounterOperation CounterOperation = BufferCounterOperation::None,
    bool MappedTyped = false, bool TypedUAV = false, bool Visibility = false) {
  bool Append = CounterOperation == BufferCounterOperation::Append;
  bool Consume = CounterOperation == BufferCounterOperation::Consume;
  int CounterDelta =
      CounterOperation == BufferCounterOperation::None                   ? 0
      : CounterOperation == BufferCounterOperation::Decrement || Consume ? -1
                                                                         : 1;
  bool ConstantBindings = CBV || Inline;
  UINT Components = 1;
  bool Normalized =
      Typed == DXGI_FORMAT_R8_UNORM || Typed == DXGI_FORMAT_R8G8B8A8_UNORM;
  bool Floating = Typed == DXGI_FORMAT_R32_FLOAT ||
                  Typed == DXGI_FORMAT_R32G32_FLOAT ||
                  Typed == DXGI_FORMAT_R32G32B32A32_FLOAT ||
                  Typed == DXGI_FORMAT_R16_FLOAT ||
                  Typed == DXGI_FORMAT_R16G16B16A16_FLOAT || Normalized;
  bool Signed =
      Typed == DXGI_FORMAT_R32_SINT || Typed == DXGI_FORMAT_R32G32_SINT ||
      Typed == DXGI_FORMAT_R32G32B32A32_SINT || Typed == DXGI_FORMAT_R8_SINT ||
      Typed == DXGI_FORMAT_R16_SINT || Typed == DXGI_FORMAT_R8G8B8A8_SINT ||
      Typed == DXGI_FORMAT_R16G16B16A16_SINT;
  if (Typed == DXGI_FORMAT_R32G32_FLOAT || Typed == DXGI_FORMAT_R32G32_UINT ||
      Typed == DXGI_FORMAT_R32G32_SINT)
    Components = 2;
  if (Typed == DXGI_FORMAT_R32G32B32A32_FLOAT ||
      Typed == DXGI_FORMAT_R32G32B32A32_UINT ||
      Typed == DXGI_FORMAT_R32G32B32A32_SINT ||
      Typed == DXGI_FORMAT_R8G8B8A8_UNORM ||
      Typed == DXGI_FORMAT_R8G8B8A8_UINT ||
      Typed == DXGI_FORMAT_R8G8B8A8_SINT ||
      Typed == DXGI_FORMAT_R16G16B16A16_FLOAT ||
      Typed == DXGI_FORMAT_R16G16B16A16_UINT ||
      Typed == DXGI_FORMAT_R16G16B16A16_SINT)
    Components = 4;
  UINT ComponentBytes = 4;
  if (Typed == DXGI_FORMAT_R8_UNORM || Typed == DXGI_FORMAT_R8_UINT ||
      Typed == DXGI_FORMAT_R8_SINT || Typed == DXGI_FORMAT_R8G8B8A8_UNORM ||
      Typed == DXGI_FORMAT_R8G8B8A8_UINT || Typed == DXGI_FORMAT_R8G8B8A8_SINT)
    ComponentBytes = 1;
  if (Typed == DXGI_FORMAT_R16_FLOAT || Typed == DXGI_FORMAT_R16_UINT ||
      Typed == DXGI_FORMAT_R16_SINT ||
      Typed == DXGI_FORMAT_R16G16B16A16_FLOAT ||
      Typed == DXGI_FORMAT_R16G16B16A16_UINT ||
      Typed == DXGI_FORMAT_R16G16B16A16_SINT)
    ComponentBytes = 2;
  bool Packed = ComponentBytes != 4;
  bool SeparateInput = Packed || TypedUAV;
  if (TypedUAV) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS Options{};
    HRESULT Status =
        report("Check additional typed UAV formats",
               Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,
                                           &Options, sizeof(Options)));
    if (FAILED(Status))
      return Status;
    if (!Options.TypedUAVLoadAdditionalFormats)
      return report("Missing complete additional typed UAV format set", E_FAIL);
  }
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
  constexpr char CBVSource[] =
      "#ifdef TYPED_VIEWS\n"
      "#ifdef TYPED_UNORM\n"
      "#define TYPED_INPUT(v) uint((v) * 255.0f + 0.5f)\n"
      "#elif defined(TYPED_HALF)\n"
      "#define TYPED_INPUT(v) uint((v) * 2.0f)\n"
      "#else\n"
      "#define TYPED_INPUT(v) uint(v)\n"
      "#endif\n"
      "#ifdef DESCRIPTOR_ARRAYS\n"
      "RWBuffer<TYPED_OUTPUT_SCALAR> output[2] : register(u3);\n"
      "Buffer<TYPED_SCALAR> input[2] : register(t3);\n"
      "#else\n"
      "RWBuffer<TYPED_OUTPUT_SCALAR> output : register(u3);\n"
      "Buffer<TYPED_SCALAR> input : register(t3);\n"
      "#endif\n"
      "#elif defined(STRUCTURED_VIEWS)\n"
      "#ifdef DESCRIPTOR_ARRAYS\n"
      "#ifdef APPEND_VIEWS\n"
      "AppendStructuredBuffer<uint> output[2] : register(u3);\n"
      "#else\n"
      "RWStructuredBuffer<uint> output[2] : register(u3);\n"
      "#endif\n"
      "#ifdef CONSUME_VIEWS\n"
      "ConsumeStructuredBuffer<uint> counterInput[2] : register(u7);\n"
      "#endif\n"
      "#else\n"
      "RWStructuredBuffer<uint> output : register(u3);\n"
      "#endif\n"
      "#ifdef DESCRIPTOR_ARRAYS\n"
      "StructuredBuffer<uint2> input[2] : register(t3);\n"
      "#else\n"
      "StructuredBuffer<uint2> input : register(t3);\n"
      "#endif\n"
      "#else\n"
      "#ifdef DESCRIPTOR_ARRAYS\n"
      "RWByteAddressBuffer output[2] : register(u3);\n"
      "#else\n"
      "RWByteAddressBuffer output : register(u3);\n"
      "#endif\n"
      "#ifdef BUFFER_VIEWS\n"
      "#ifdef DESCRIPTOR_ARRAYS\n"
      "ByteAddressBuffer input[2] : register(t3);\n"
      "#else\n"
      "ByteAddressBuffer input : register(t3);\n"
      "#endif\n"
      "#endif\n"
      "#endif\n"
      "#ifdef DESCRIPTOR_ARRAYS\n"
      "struct ConstantsType { uint values[2]; };\n"
      "ConstantBuffer<ConstantsType> A[2] : register(b3);\n"
      "ConstantBuffer<ConstantsType> B[2] : register(b3, space7);\n"
      "#define OUTPUT output[NonUniformResourceIndex(index % 2)]\n"
      "#else\n"
      "#define OUTPUT output\n"
      "#ifdef INLINE_CONSTANTS\n"
      "cbuffer A : register(b3) { uint4 firstA; uint lastA; };\n"
      "cbuffer B : register(b3, space7) { uint4 firstB; uint lastB; };\n"
      "#else\n"
      "cbuffer A : register(b3) { uint valuesA[2]; };\n"
      "cbuffer B : register(b3, space7) { uint valuesB[2]; };\n"
      "#endif\n"
      "#endif\n"
      "groupshared uint sharedValues[30];\n"
      "[numthreads(5,3,2)] void main(uint3 id : SV_DispatchThreadID,\n"
      "uint3 local : SV_GroupThreadID) {\n"
      "uint index = id.x + 15 * (id.y + 6 * id.z);\n"
      "uint slot = local.x + 5 * (local.y + 3 * local.z);\n"
      "sharedValues[slot] = index;\n"
      "GroupMemoryBarrierWithGroupSync();\n"
      "uint peer = sharedValues[slot == 29 ? 0 : slot + 1];\n"
      "#ifdef DESCRIPTOR_ARRAYS\n"
      "uint bias = A[NonUniformResourceIndex(index % 2)].values[id.x % 2] + "
      "B[NonUniformResourceIndex(index % 2)].values[id.x % 2];\n"
      "#else\n"
      "#ifdef INLINE_CONSTANTS\n"
      "uint bias = id.x % 2 ? lastA + lastB : firstA.x + firstB.x;\n"
      "#else\n"
      "uint bias = valuesA[id.x % 2] + valuesB[id.x % 2];\n"
      "#endif\n"
      "#endif\n"
      "#ifdef PACKED_VIEWS\n"
      "if (A[NonUniformResourceIndex(index % 2)].values[0] < "
      "17 + (index % 2) * 11) {\n"
      "uint viewElements;\n"
      "input[NonUniformResourceIndex(index % 2)].GetDimensions(viewElements);\n"
      "bias += viewElements;\n"
      "}\n"
      "#endif\n"
      "#ifdef COUNTER_VIEWS\n"
      "#ifdef APPEND_VIEWS\n"
      "if (index % 2) output[1].Append(index);\n"
      "else output[0].Append(index);\n"
      "#elif defined(CONSUME_VIEWS)\n"
      "if (index % 2) OUTPUT[index] = counterInput[1].Consume();\n"
      "else OUTPUT[index] = counterInput[0].Consume();\n"
      "#else\n"
      "if (index % 2)\n"
      "  OUTPUT[index] = output[1].COUNTER_OPERATION();\n"
      "else\n"
      "  OUTPUT[index] = output[0].COUNTER_OPERATION();\n"
      "#endif\n"
      "#elif defined(TYPED_UAV_VIEWS)\n"
      "OUTPUT[index] = OUTPUT[index] + "
      "input[NonUniformResourceIndex(index % 2)][index] + "
      "output[NonUniformResourceIndex(1 - index % 2)][index];\n"
      "#elif defined(TYPED_VIEWS)\n"
      "#ifdef TYPED_VECTOR\n"
      "uint value = uint(OUTPUT[index]) + peer + bias + "
      "TYPED_INPUT(input[NonUniformResourceIndex(index % 2)][index].y);\n"
      "OUTPUT[index] = value;\n"
      "#else\n"
      "#ifdef DESCRIPTOR_ARRAYS\n"
      "OUTPUT[index] = uint(OUTPUT[index]) + peer + bias + "
      "TYPED_INPUT(input[NonUniformResourceIndex(index % 2)][index]);\n"
      "#else\n"
      "OUTPUT[index] = uint(OUTPUT[index]) + peer + bias + "
      "TYPED_INPUT(input[index]);\n"
      "#endif\n"
      "#endif\n"
      "#elif defined(STRUCTURED_VIEWS)\n"
      "#ifdef DESCRIPTOR_ARRAYS\n"
      "OUTPUT[index] = OUTPUT[index] + peer + bias + "
      "input[NonUniformResourceIndex(index % 2)][index].y;\n"
      "#else\n"
      "OUTPUT[index] = OUTPUT[index] + peer + bias + input[index].y;\n"
      "#endif\n"
      "#else\n"
      "#ifdef BUFFER_VIEWS\n"
      "#ifdef DESCRIPTOR_ARRAYS\n"
      "bias += input[NonUniformResourceIndex(index % 2)].Load(index * 4);\n"
      "#else\n"
      "bias += input.Load(index * 4);\n"
      "#endif\n"
      "#endif\n"
      "OUTPUT.Store(index * 4, OUTPUT.Load(index * 4) + peer + bias);\n"
      "#endif\n"
      "}\n";
  std::array<D3D12_ROOT_PARAMETER, 6> Parameters{};
  for (UINT I = 0; I < Parameters.size(); ++I) {
    Parameters[I].ParameterType =
        !I       ? D3D12_ROOT_PARAMETER_TYPE_UAV
        : Inline ? D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS
                 : D3D12_ROOT_PARAMETER_TYPE_CBV;
    if (I && Inline) {
      Parameters[I].Constants.ShaderRegister = I == 3 ? 9 : 3;
      Parameters[I].Constants.RegisterSpace = I == 2 ? 7 : 0;
      Parameters[I].Constants.Num32BitValues = I == 3 ? 1 : 5;
    } else {
      Parameters[I].Descriptor.ShaderRegister = I == 3 ? 9 : 3;
      Parameters[I].Descriptor.RegisterSpace = I == 2 ? 7 : 0;
    }
    Parameters[I].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  if (Visibility)
    for (UINT I = 4; I < 6; ++I) {
      Parameters[I].Constants.ShaderRegister = 99;
      Parameters[I].Constants.RegisterSpace = 999;
      Parameters[I].Constants.Num32BitValues = 1;
      Parameters[I].ShaderVisibility = I == 4 ? D3D12_SHADER_VISIBILITY_VERTEX
                                              : D3D12_SHADER_VISIBILITY_PIXEL;
    }
  D3D12_DESCRIPTOR_RANGE Ranges[2]{};
  D3D12_DESCRIPTOR_RANGE ViewRanges[3]{};
  if (Views) {
    ViewRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ViewRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ViewRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    for (auto &Range : ViewRanges) {
      Range.NumDescriptors = 1;
      Range.BaseShaderRegister = 3;
      Range.OffsetInDescriptorsFromTableStart =
          D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }
    if (Arrays)
      for (auto &Range : ViewRanges)
        Range.NumDescriptors = 2;
    ViewRanges[2].BaseShaderRegister = 7;
    Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Parameters[0].DescriptorTable.NumDescriptorRanges = Consume ? 3 : 2;
    Parameters[0].DescriptorTable.pDescriptorRanges = ViewRanges;
  }
  if (Tables) {
    for (UINT I = 0; I < 2; ++I) {
      Ranges[I].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
      Ranges[I].NumDescriptors = Arrays ? 2 : 1;
      Ranges[I].BaseShaderRegister = 3;
      Ranges[I].RegisterSpace = I ? 7 : 0;
      Ranges[I].OffsetInDescriptorsFromTableStart = I ? 3 : 1;
    }
    Parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Parameters[1].DescriptorTable.NumDescriptorRanges = 2;
    Parameters[1].DescriptorTable.pDescriptorRanges = Ranges;
    Parameters[2].Descriptor.ShaderRegister = 10;
  }
  D3D12_ROOT_SIGNATURE_DESC RootDesc{};
  RootDesc.NumParameters = Visibility ? 6 : ConstantBindings ? 4 : 1;
  RootDesc.pParameters = Parameters.data();
  ComPtr<ID3D12RootSignature> Root;
  ComPtr<ID3D12PipelineState> Pipeline;
  std::vector<D3D_SHADER_MACRO> Defines;
  if (Inline)
    Defines.push_back({"INLINE_CONSTANTS", "1"});
  if (Views)
    Defines.push_back({"BUFFER_VIEWS", "1"});
  if (Structured)
    Defines.push_back({"STRUCTURED_VIEWS", "1"});
  if (Arrays)
    Defines.push_back({"DESCRIPTOR_ARRAYS", "1"});
  if (CounterDelta) {
    Defines.push_back({"COUNTER_VIEWS", "1"});
    Defines.push_back({"COUNTER_OPERATION", CounterDelta > 0
                                                ? "IncrementCounter"
                                                : "DecrementCounter"});
    if (Append)
      Defines.push_back({"APPEND_VIEWS", "1"});
    if (Consume)
      Defines.push_back({"CONSUME_VIEWS", "1"});
  }
  const char *OutputType = Floating ? "float" : Signed ? "int" : "uint";
  std::string TypedName = OutputType;
  if (Components > 1)
    TypedName += std::to_string(Components);
  if (Typed != DXGI_FORMAT_UNKNOWN) {
    Defines.push_back({"TYPED_VIEWS", "1"});
    Defines.push_back({"TYPED_SCALAR", TypedName.c_str()});
    Defines.push_back(
        {"TYPED_OUTPUT_SCALAR", TypedUAV ? TypedName.c_str() : OutputType});
    if (TypedUAV)
      Defines.push_back({"TYPED_UAV_VIEWS", "1"});
    if (Components > 1)
      Defines.push_back({"TYPED_VECTOR", "1"});
    if (Normalized)
      Defines.push_back({"TYPED_UNORM", "1"});
    if (Packed && Floating && !Normalized)
      Defines.push_back({"TYPED_HALF", "1"});
    if (Packed)
      Defines.push_back({"PACKED_VIEWS", "1"});
    D3D12_FEATURE_DATA_FORMAT_SUPPORT Support{Typed};
    HRESULT Status =
        report("Check typed buffer format",
               Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                           &Support, sizeof(Support)));
    if (FAILED(Status))
      return Status;
    if (!(Support.Support1 & D3D12_FORMAT_SUPPORT1_BUFFER))
      return report("Missing typed SRV buffer format support", E_FAIL);
    Support = {TypedUAV   ? Typed
               : Floating ? DXGI_FORMAT_R32_FLOAT
               : Signed   ? DXGI_FORMAT_R32_SINT
                          : DXGI_FORMAT_R32_UINT};
    Status = report("Check scalar output buffer format",
                    Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                                &Support, sizeof(Support)));
    if (FAILED(Status))
      return Status;
    if (!(Support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) ||
        !(Support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))
      return report("Missing typed buffer load/store support", E_FAIL);
  }
  Defines.push_back({nullptr, nullptr});
  HRESULT Result = createComputePipeline(
      Device, ConstantBindings ? CBVSource : Source,
      ConstantBindings ? sizeof(CBVSource) - 1 : sizeof(Source) - 1, RootDesc,
      Root, Pipeline, DXIL, ConstantBindings ? "cs_5_1" : "cs_5_0",
      Inline || Views ? Defines.data() : nullptr);
  if (FAILED(Result))
    return Result;
  constexpr UINT Threads = 180, Words = Threads + 16;
  const UINT Bytes =
      Words * (TypedUAV ? Components * ComponentBytes : sizeof(UINT));
  auto InitialCounter = [Append, Consume, Threads](UINT Repeat, UINT Element) {
    return Append    ? 0u
           : Consume ? Threads / 2
                     : 500 + Repeat * 100 + Element * 200;
  };
  D3D12_HEAP_PROPERTIES Heap{};
  Heap.CreationNodeMask = Heap.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC Desc{};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  UINT UploadBytes = SeparateInput ? Bytes + Words * Components * ComponentBytes
                     : Structured  ? 2 * Bytes
                                   : Components * Bytes;
  Desc.Width = UploadBytes;
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
  Desc.Width = Bytes;
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
  constexpr UINT CounterStride = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
  constexpr UINT CounterDataOffset = 2 * CounterStride;
  UINT CounterBytes = CounterDataOffset + (Consume ? Bytes : 0);
  ComPtr<ID3D12Resource> CounterUpload, Counters, CounterReadback;
  if (CounterDelta) {
    D3D12_RESOURCE_DESC CounterDesc = Desc;
    CounterDesc.Width = CounterBytes;
    Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    Result = report("Create counter upload",
                    Device->CreateCommittedResource(
                        &Heap, D3D12_HEAP_FLAG_NONE, &CounterDesc,
                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                        IID_PPV_ARGS(&CounterUpload)));
    if (FAILED(Result))
      return Result;
    Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    CounterDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    Result = report("Create UAV counters",
                    Device->CreateCommittedResource(
                        &Heap, D3D12_HEAP_FLAG_NONE, &CounterDesc,
                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                        IID_PPV_ARGS(&Counters)));
    if (FAILED(Result))
      return Result;
    Heap.Type = D3D12_HEAP_TYPE_READBACK;
    CounterDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    Result = report("Create counter readback",
                    Device->CreateCommittedResource(
                        &Heap, D3D12_HEAP_FLAG_NONE, &CounterDesc,
                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                        IID_PPV_ARGS(&CounterReadback)));
    if (FAILED(Result))
      return Result;
  }
  std::array<ComPtr<ID3D12Resource>, 2> Constants;
  if (CBV) {
    D3D12_RESOURCE_DESC ConstantDesc = Desc;
    ConstantDesc.Width = 512;
    Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    for (auto &Constant : Constants) {
      Result = report("Create root constant buffer",
                      Device->CreateCommittedResource(
                          &Heap, D3D12_HEAP_FLAG_NONE, &ConstantDesc,
                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                          IID_PPV_ARGS(&Constant)));
      if (FAILED(Result))
        return Result;
    }
  }
  ComPtr<ID3D12DescriptorHeap> CPUDescriptors, GPUDescriptors;
  UINT DescriptorStride = 0;
  D3D12_CPU_DESCRIPTOR_HANDLE CPUStart{}, GPUCPUStart{};
  D3D12_GPU_DESCRIPTOR_HANDLE GPUStart{};
  if (Tables) {
    D3D12_DESCRIPTOR_HEAP_DESC DescriptorDesc{};
    DescriptorDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    DescriptorDesc.NumDescriptors = Consume ? 10 : Views ? (Arrays ? 8 : 4) : 3;
    Result = report("Create CPU descriptor heap",
                    Device->CreateDescriptorHeap(
                        &DescriptorDesc, IID_PPV_ARGS(&CPUDescriptors)));
    if (FAILED(Result))
      return Result;
    DescriptorDesc.NumDescriptors = Consume ? 12
                                    : Views ? (Arrays ? 10 : 8)
                                            : 6;
    DescriptorDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Result = report("Create shader-visible descriptor heap",
                    Device->CreateDescriptorHeap(
                        &DescriptorDesc, IID_PPV_ARGS(&GPUDescriptors)));
    if (FAILED(Result))
      return Result;
    DescriptorStride = Device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CPUStart = CPUDescriptors->GetCPUDescriptorHandleForHeapStart();
    GPUCPUStart = GPUDescriptors->GetCPUDescriptorHandleForHeapStart();
    GPUStart = GPUDescriptors->GetGPUDescriptorHandleForHeapStart();
    if (!DescriptorStride || !CPUStart.ptr || !GPUCPUStart.ptr || !GPUStart.ptr)
      return report("Missing owned descriptor handles", E_FAIL);
    ID3D12Pageable *Pageables[] = {CPUDescriptors.Get(), GPUDescriptors.Get()};
    Result = report("Evict descriptor metadata", Device->Evict(2, Pageables));
    if (FAILED(Result))
      return Result;
    Result = report("Make descriptor metadata resident",
                    Device->MakeResident(2, Pageables));
    if (FAILED(Result))
      return Result;
  }
  ComPtr<ID3D12Resource> ArgumentUpload, Arguments;
  constexpr UINT ArgumentBytes = 64;
  if (Indirect || Predication) {
    D3D12_RESOURCE_DESC ArgumentDesc = Desc;
    ArgumentDesc.Width = ArgumentBytes;
    Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    Result = report("Create indirect argument upload",
                    Device->CreateCommittedResource(
                        &Heap, D3D12_HEAP_FLAG_NONE, &ArgumentDesc,
                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                        IID_PPV_ARGS(&ArgumentUpload)));
    if (FAILED(Result))
      return Result;
    Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    Result = report("Create indirect arguments",
                    Device->CreateCommittedResource(
                        &Heap, D3D12_HEAP_FLAG_NONE, &ArgumentDesc,
                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                        IID_PPV_ARGS(&Arguments)));
    if (FAILED(Result))
      return Result;
  }
  void *Memory = nullptr;
  D3D12_RANGE NoRead{0, 0}, Full{0, Bytes};
  Result = report("Map command upload", Upload->Map(0, &NoRead, &Memory));
  if (FAILED(Result))
    return Result;
  if (!Memory) {
    Upload->Unmap(0, &NoRead);
    return report("Missing command upload memory", E_FAIL);
  }
  auto InitialValue = [Signed](UINT I) {
    UINT Value = I * 7 + 5;
    return Signed ? 0u - Value : Value;
  };
  auto PackedSample = [Signed, Floating, Normalized,
                       ComponentBytes](UINT Element, UINT Channel) {
    UINT Hash = Element * 997 + Channel * 331 + 17;
    if (Floating && !Normalized)
      return Hash % 8;
    if (Signed)
      return 0u - (Hash % (ComponentBytes == 1 ? 127 : 32767) + 1);
    return Hash % (ComponentBytes == 1 ? 256 : 65536);
  };
  auto EncodeSample = [Floating, ComponentBytes](unsigned char *Destination,
                                                 UINT Sample) {
    if (Floating && ComponentBytes > 1) {
      float Value = static_cast<float>(Sample) * 0.5f;
      if (ComponentBytes == 4)
        std::memcpy(Destination, &Value, sizeof(Value));
      else {
        UINT Bits;
        std::memcpy(&Bits, &Value, sizeof(Bits));
        // All generated values are zero or exactly representable normal halves.
        UINT16 Half =
            !Sample ? 0 : ((Bits >> 13) & 0x3ff) | (((Bits >> 23) - 112) << 10);
        std::memcpy(Destination, &Half, sizeof(Half));
      }
    } else
      std::memcpy(Destination, &Sample, ComponentBytes);
  };
  if (TypedUAV)
    for (UINT Element = 0; Element < Words; ++Element)
      for (UINT Channel = 0; Channel < Components; ++Channel)
        EncodeSample(static_cast<unsigned char *>(Memory) +
                         (Element * Components + Channel) * ComponentBytes,
                     PackedSample(Element, Channel));
  else
    for (UINT I = 0;
         I < Words * (Packed ? 1 : Components * (Structured ? 2 : 1)); ++I) {
      if (Floating)
        static_cast<float *>(Memory)[I] = static_cast<float>(InitialValue(I));
      else
        static_cast<UINT *>(Memory)[I] = InitialValue(I);
    }
  if (SeparateInput)
    for (UINT Element = 0; Element < Words; ++Element)
      for (UINT Channel = 0; Channel < Components; ++Channel) {
        auto *Destination = static_cast<unsigned char *>(Memory) + Bytes +
                            (Element * Components + Channel) * ComponentBytes;
        EncodeSample(Destination, PackedSample(Element, Channel));
      }
  D3D12_RANGE UploadWritten{0, UploadBytes};
  Upload->Unmap(0, &UploadWritten);
  D3D12_COMMAND_QUEUE_DESC QueueDesc{};
  QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  ComPtr<ID3D12CommandQueue> Queue;
  ComPtr<ID3D12CommandAllocator> Allocator;
  ComPtr<ID3D12GraphicsCommandList> List;
  ComPtr<ID3D12Fence> Completion;
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
  Result = report(
      "Create buffer completion fence",
      Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Completion)));
  if (FAILED(Result))
    return Result;
  ComPtr<ID3D12CommandQueue> GateQueue;
  ComPtr<ID3D12Fence> Gate;
  std::vector<UINT> Previous(Bytes / sizeof(UINT), 0xcdcdcdcd);
  if (Ordered) {
    D3D12_COMMAND_QUEUE_DESC GateDesc{};
    GateDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    Result =
        report("Create cross-queue signal producer",
               Device->CreateCommandQueue(&GateDesc, IID_PPV_ARGS(&GateQueue)));
    if (FAILED(Result))
      return Result;
    Result = report(
        "Create cross-queue gate",
        Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Gate)));
    if (FAILED(Result))
      return Result;
    Result =
        report("Initialize held readback", Readback->Map(0, &NoRead, &Memory));
    if (FAILED(Result))
      return Result;
    if (Memory)
      std::memcpy(Memory, Previous.data(), Bytes);
    Readback->Unmap(0, &Full);
    if (!Memory)
      return report("Missing held readback memory", E_FAIL);
  }
  for (UINT Repeat = 0; Repeat < 3; ++Repeat) {
    if (CounterDelta) {
      D3D12_RESOURCE_BARRIER CounterBarrier{};
      CounterBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      CounterBarrier.Transition.pResource = Counters.Get();
      CounterBarrier.Transition.Subresource =
          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      if (Repeat) {
        CounterBarrier.Transition.StateBefore =
            D3D12_RESOURCE_STATE_COPY_SOURCE;
        CounterBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        List->ResourceBarrier(1, &CounterBarrier);
      }
      List->CopyBufferRegion(Counters.Get(), 0, CounterUpload.Get(), 0,
                             CounterBytes);
      CounterBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      CounterBarrier.Transition.StateAfter =
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      List->ResourceBarrier(1, &CounterBarrier);
    }
    ComPtr<ID3D12CommandSignature> Signature;
    if (Indirect) {
      D3D12_INDIRECT_ARGUMENT_DESC Argument{};
      Argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
      D3D12_COMMAND_SIGNATURE_DESC SignatureDesc{};
      SignatureDesc.ByteStride = 16;
      SignatureDesc.NumArgumentDescs = 1;
      SignatureDesc.pArgumentDescs = &Argument;
      Result = report("Create indirect dispatch signature",
                      Device->CreateCommandSignature(&SignatureDesc, nullptr,
                                                     IID_PPV_ARGS(&Signature)));
      if (FAILED(Result))
        return Result;
    }
    if (Indirect || Predication) {
      if (Repeat) {
        D3D12_RESOURCE_BARRIER ToCopy{};
        ToCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ToCopy.Transition.pResource = Arguments.Get();
        ToCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ToCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        ToCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        List->ResourceBarrier(1, &ToCopy);
      }
      List->CopyBufferRegion(Arguments.Get(), 0, ArgumentUpload.Get(), 0,
                             ArgumentBytes);
      D3D12_RESOURCE_BARRIER ToIndirect{};
      ToIndirect.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      ToIndirect.Transition.pResource = Arguments.Get();
      ToIndirect.Transition.Subresource =
          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      ToIndirect.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      ToIndirect.Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
      List->ResourceBarrier(1, &ToIndirect);
    }
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
    if (!Views)
      List->SetComputeRootUnorderedAccessView(0,
                                              Output->GetGPUVirtualAddress());
    if (CBV && !Tables) {
      for (UINT I = 0; I < Constants.size(); ++I)
        List->SetComputeRootConstantBufferView(
            I + 1, Constants[I]->GetGPUVirtualAddress() + 256);
      if (Repeat == 1)
        List->SetComputeRootConstantBufferView(
            3, Constants[0]->GetGPUVirtualAddress());
    }
    if (Tables) {
      ID3D12DescriptorHeap *Heaps[] = {GPUDescriptors.Get()};
      List->SetDescriptorHeaps(1, Heaps);
      D3D12_GPU_DESCRIPTOR_HANDLE Base{GPUStart.ptr + DescriptorStride};
      List->SetComputeRootDescriptorTable(1, Base);
      if (Views)
        List->SetComputeRootDescriptorTable(
            0, {GPUStart.ptr + (Arrays ? 6 : 5) * DescriptorStride});
    }
    if (Inline) {
      UINT ValuesA[] = {7 + Repeat * 5, 0xab, 0xab, 0xab, 0xab};
      UINT ValuesB[] = {16, 0xcd, 0xcd, 0xcd, 10};
      List->SetComputeRoot32BitConstants(1, 5, ValuesA, 0);
      List->SetComputeRoot32BitConstant(1, 13 + Repeat * 5, 4);
      List->SetComputeRoot32BitConstants(2, 5, ValuesB, 0);
      if (Repeat == 1)
        List->SetComputeRoot32BitConstant(3, 0xabababab, 0);
      ValuesA[0] = ValuesB[0] = 0xefefefef;
    }
    if (Visibility) {
      List->SetComputeRoot32BitConstant(4, 0x11111111 + Repeat, 0);
      List->SetComputeRoot32BitConstant(5, 0xeeeeeeee - Repeat, 0);
    }
    List->SetComputeRootSignature(Root.Get());
    if (Predication) {
      auto DispatchPredicated = [&]() {
        if (Indirect)
          List->ExecuteIndirect(Signature.Get(), 1, Arguments.Get(), 32,
                                nullptr, 0);
        else
          List->Dispatch(3, 2, 1);
      };
      List->SetPredication(Arguments.Get(), 8, D3D12_PREDICATION_OP_EQUAL_ZERO);
      DispatchPredicated();
      List->SetPredication(Arguments.Get(), 16,
                           D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
      DispatchPredicated();
      List->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
      DispatchPredicated();
      Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
      List->ResourceBarrier(1, &Barrier);
      List->SetPredication(Arguments.Get(), 24,
                           D3D12_PREDICATION_OP_EQUAL_ZERO);
      List->CopyBufferRegion(Output.Get(), 0, Upload.Get(), 0, Bytes);
      Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      List->ResourceBarrier(1, &Barrier);
      List->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    } else if (Indirect) {
      List->ExecuteIndirect(Signature.Get(), 2, Arguments.Get(), 16,
                            Arguments.Get(), 4);
      if (!Repeat)
        List->ExecuteIndirect(Signature.Get(), 1, Arguments.Get(), 16, nullptr,
                              0);
    } else
      List->Dispatch(3, 2, 1);
    if (Views)
      List->SetComputeRootDescriptorTable(0, GPUStart);
    else
      List->SetComputeRootUnorderedAccessView(
          0, Output->GetGPUVirtualAddress() + 16);
    if (CBV && !Tables)
      List->SetComputeRootConstantBufferView(
          1, Constants[0]->GetGPUVirtualAddress());
    if (Inline)
      List->SetComputeRoot32BitConstant(1, 0xeeeeeeee, 0);
    if (Tables)
      List->SetComputeRootDescriptorTable(1, GPUStart);
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    List->ResourceBarrier(1, &Barrier);
    List->CopyBufferRegion(Readback.Get(), 0, Output.Get(), 0, Bytes);
    if (CounterDelta) {
      D3D12_RESOURCE_BARRIER CounterBarrier{};
      CounterBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      CounterBarrier.Transition.pResource = Counters.Get();
      CounterBarrier.Transition.Subresource =
          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      CounterBarrier.Transition.StateBefore =
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      CounterBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      List->ResourceBarrier(1, &CounterBarrier);
      List->CopyBufferRegion(CounterReadback.Get(), 0, Counters.Get(), 0,
                             CounterBytes);
    }
    if (Predication)
      List->SetPredication(Arguments.Get(), 16,
                           D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
    Result = report("Close buffer command list", List->Close());
    if (FAILED(Result))
      return Result;
    if (CBV) {
      for (UINT I = 0; I < Constants.size(); ++I) {
        Result = report("Map execution-time constants",
                        Constants[I]->Map(0, &NoRead, &Memory));
        if (FAILED(Result))
          return Result;
        if (!Memory) {
          Constants[I]->Unmap(0, &NoRead);
          return report("Missing constant buffer memory", E_FAIL);
        }
        std::memset(Memory, 0xab, 512);
        for (UINT Element = 0; Element < (Arrays ? 2u : 1u); ++Element) {
          auto *Values =
              static_cast<UINT *>(Memory) + (Arrays ? Element : 1) * 64;
          Values[0] = (I ? 16 : 7 + Repeat * 5) + Element * (I ? 19 : 11);
          Values[4] = (I ? 10 : 13 + Repeat * 5) + Element * (I ? 19 : 11);
        }
        D3D12_RANGE Written{0, 512};
        Constants[I]->Unmap(0, &Written);
      }
    }
    if (Tables) {
      for (UINT I = 0; I < 2; ++I) {
        for (UINT Element = 0; Element < (Arrays ? 2u : 1u); ++Element) {
          D3D12_CONSTANT_BUFFER_VIEW_DESC View{};
          bool Null = I == 1 && Repeat == 2 && Element == 0;
          View.BufferLocation = Null ? 0
                                     : Constants[I]->GetGPUVirtualAddress() +
                                           (Arrays ? Element : 1) * 256;
          View.SizeInBytes = 256;
          D3D12_CPU_DESCRIPTOR_HANDLE Destination{
              CPUStart.ptr + (Arrays ? I * 2 + Element : I) * DescriptorStride};
          Device->CreateConstantBufferView(&View, Destination);
        }
      }
      if (Views) {
        D3D12_SHADER_RESOURCE_VIEW_DESC Input{};
        Input.Format = Typed != DXGI_FORMAT_UNKNOWN ? Typed
                       : Structured                 ? DXGI_FORMAT_UNKNOWN
                                                    : DXGI_FORMAT_R32_TYPELESS;
        Input.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        Input.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Input.Buffer.FirstElement = Structured ? 2 : 4;
        Input.Buffer.NumElements = Threads;
        Input.Buffer.StructureByteStride = Structured ? 8 : 0;
        Input.Buffer.Flags = Structured || Typed != DXGI_FORMAT_UNKNOWN
                                 ? D3D12_BUFFER_SRV_FLAG_NONE
                                 : D3D12_BUFFER_SRV_FLAG_RAW;
        UINT InputCount = Arrays ? 2 : 1;
        for (UINT I = 0; I < InputCount; ++I) {
          if (MappedTyped) {
            UINT MappingSource = Repeat == 0   ? (Components == 1 ? 3 : 0)
                                 : Repeat == 1 ? (I ? 5 : 4)
                                               : 5;
            Input.Shader4ComponentMapping =
                D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
                    Components == 1 ? MappingSource : 0,
                    Components > 1 ? MappingSource : 1, 2, 3);
          }
          Input.Buffer.FirstElement =
              SeparateInput ? Bytes / (Components * ComponentBytes) + 4 + I * 4
                            : (Structured ? 2 : 4) + I * (Structured ? 2 : 4);
          bool Null = Repeat == 2 && I == 0;
          Device->CreateShaderResourceView(
              Null ? nullptr : Upload.Get(), &Input,
              {CPUStart.ptr + ((Arrays ? 4 : 2) + I) * DescriptorStride});
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC OutputView{};
        OutputView.Format = Input.Format;
        if (Typed != DXGI_FORMAT_UNKNOWN && !TypedUAV)
          OutputView.Format = Floating ? DXGI_FORMAT_R32_FLOAT
                              : Signed ? DXGI_FORMAT_R32_SINT
                                       : DXGI_FORMAT_R32_UINT;
        OutputView.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        OutputView.Buffer.NumElements = Threads;
        OutputView.Buffer.StructureByteStride = Structured ? 4 : 0;
        OutputView.Buffer.Flags = Structured || Typed != DXGI_FORMAT_UNKNOWN
                                      ? D3D12_BUFFER_UAV_FLAG_NONE
                                      : D3D12_BUFFER_UAV_FLAG_RAW;
        UINT OutputCount = Arrays ? 2 : 1;
        for (UINT I = 0; I < OutputCount; ++I) {
          bool Null = Arrays && Repeat == 2 && I == 1;
          OutputView.Buffer.FirstElement = Append ? I * Threads / 2 : 0;
          OutputView.Buffer.NumElements = Append ? Threads / 2 : Threads;
          OutputView.Buffer.CounterOffsetInBytes =
              CounterDelta && !Consume && !Null ? I * CounterStride : 0;
          Device->CreateUnorderedAccessView(
              Null ? nullptr : Output.Get(),
              CounterDelta && !Consume && !Null ? Counters.Get() : nullptr,
              &OutputView,
              {CPUStart.ptr +
               ((Arrays ? 4 : 2) + InputCount + I) * DescriptorStride});
        }
        if (Consume)
          for (UINT I = 0; I < 2; ++I) {
            bool Null = Repeat == 2 && I == 1;
            D3D12_UNORDERED_ACCESS_VIEW_DESC SourceView{};
            SourceView.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            SourceView.Buffer.FirstElement =
                CounterDataOffset / sizeof(UINT) + I * Threads / 2;
            SourceView.Buffer.NumElements = Threads / 2;
            SourceView.Buffer.StructureByteStride = sizeof(UINT);
            SourceView.Buffer.CounterOffsetInBytes =
                Null ? 0 : I * CounterStride;
            Device->CreateUnorderedAccessView(
                Null ? nullptr : Counters.Get(),
                Null ? nullptr : Counters.Get(), &SourceView,
                {CPUStart.ptr + (8 + I) * DescriptorStride});
          }
        Device->CopyDescriptorsSimple(
            InputCount + OutputCount + (Consume ? 2 : 0),
            {GPUCPUStart.ptr + (Arrays ? 6 : 5) * DescriptorStride},
            {CPUStart.ptr + (Arrays ? 4 : 2) * DescriptorStride},
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      }
      D3D12_CPU_DESCRIPTOR_HANDLE Destinations[] = {
          {GPUCPUStart.ptr + 2 * DescriptorStride},
          {GPUCPUStart.ptr + 4 * DescriptorStride}};
      if (Arrays) {
        D3D12_CPU_DESCRIPTOR_HANDLE Destination{GPUCPUStart.ptr +
                                                2 * DescriptorStride};
        UINT Count = 4;
        Device->CopyDescriptors(1, &Destination, &Count, 1, &CPUStart, &Count,
                                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      } else if (!Repeat) {
        for (UINT I = 0; I < 2; ++I) {
          D3D12_CPU_DESCRIPTOR_HANDLE SourceDescriptor{CPUStart.ptr +
                                                       I * DescriptorStride};
          Device->CopyDescriptorsSimple(1, Destinations[I], SourceDescriptor,
                                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
      } else {
        UINT Count = 2;
        Device->CopyDescriptors(2, Destinations, nullptr, 1, &CPUStart, &Count,
                                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      }
    }
    if (CounterDelta) {
      Result = report("Map execution-time counter values",
                      CounterUpload->Map(0, &NoRead, &Memory));
      if (FAILED(Result))
        return Result;
      if (!Memory) {
        CounterUpload->Unmap(0, &NoRead);
        return report("Missing counter upload memory", E_FAIL);
      }
      std::memset(Memory, 0xcd, CounterBytes);
      for (UINT I = 0; I < 2; ++I)
        static_cast<UINT *>(Memory)[I * CounterStride / sizeof(UINT)] =
            InitialCounter(Repeat, I);
      if (Consume)
        for (UINT I = 0; I < Words; ++I)
          static_cast<UINT *>(Memory)[CounterDataOffset / sizeof(UINT) + I] =
              InitialValue(I);
      D3D12_RANGE Written{0, CounterBytes};
      CounterUpload->Unmap(0, &Written);
    }
    if (Indirect || Predication) {
      Result = report("Map execution-time arguments",
                      ArgumentUpload->Map(0, &NoRead, &Memory));
      if (FAILED(Result))
        return Result;
      if (!Memory) {
        ArgumentUpload->Unmap(0, &NoRead);
        return report("Missing indirect argument upload memory", E_FAIL);
      }
      if (Predication) {
        const UINT64 HighBit = UINT64(1) << 40;
        const UINT64 Values[8] = {0xabababab,
                                  Repeat == 1 ? HighBit : 0,
                                  Repeat ? HighBit : 0,
                                  0,
                                  UINT64(3) | (UINT64(2) << 32),
                                  1,
                                  0,
                                  0};
        std::memcpy(Memory, Values, sizeof(Values));
      } else {
        const UINT Values[16] = {0xabababab,
                                 Repeat == 0   ? 1u
                                 : Repeat == 1 ? 99u
                                               : 0u,
                                 0,
                                 0,
                                 3,
                                 2,
                                 1,
                                 0xcdcdcdcd,
                                 Repeat == 0 ? 65536u : 3u,
                                 2,
                                 1,
                                 0xefefefef,
                                 0,
                                 0,
                                 0,
                                 0};
        std::memcpy(Memory, Values, sizeof(Values));
      }
      D3D12_RANGE Written{0, ArgumentBytes};
      ArgumentUpload->Unmap(0, &Written);
    }
    ID3D12CommandList *Commands[] = {List.Get()};
    bool GatePending = false;
    Result = [&]() -> HRESULT {
      if (Ordered) {
        HRESULT Status = report("Queue future cross-queue wait",
                                Queue->Wait(Gate.Get(), Repeat + 1));
        if (FAILED(Status))
          return Status;
        GatePending = true;
      }
      Queue->ExecuteCommandLists(1, Commands);
      if (!Ordered)
        return waitQueue(Queue.Get(), Completion.Get(), Repeat + 1);
      Sleep(100);
      HRESULT Status =
          report("Map held readback", Readback->Map(0, &Full, &Memory));
      if (FAILED(Status))
        return Status;
      bool Unchanged =
          Memory && std::memcmp(Memory, Previous.data(), Bytes) == 0;
      Readback->Unmap(0, &NoRead);
      if (!Unchanged)
        return report("CPU commands bypassed future kernel wait", E_FAIL);
      if (Repeat != 2) {
        Status = report("Release cross-queue gate",
                        GateQueue->Signal(Gate.Get(), Repeat + 1));
        if (FAILED(Status))
          return Status;
        GatePending = false;
        return waitQueue(Queue.Get(), Completion.Get(), Repeat + 1);
      }
      Status = report("Signal pending teardown completion",
                      Queue->Signal(Completion.Get(), Repeat + 1));
      if (FAILED(Status))
        return Status;
      DelayedFenceSignal Signal{GateQueue.Get(), Gate.Get(), Repeat + 1};
      HANDLE Thread =
          CreateThread(nullptr, 0, signalDelayedFence, &Signal, 0, nullptr);
      if (!Thread)
        return report("Create delayed signal thread",
                      HRESULT_FROM_WIN32(GetLastError()));
      List.Reset();
      Allocator.Reset();
      Pipeline.Reset();
      Root.Reset();
      Queue.Reset();
      DWORD Wait = WaitForSingleObject(Thread, INFINITE);
      DWORD WaitError = Wait == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
      if (!CloseHandle(Thread))
        return report("Close delayed signal thread",
                      HRESULT_FROM_WIN32(GetLastError()));
      if (Wait != WAIT_OBJECT_0)
        return report("Join delayed signal thread",
                      HRESULT_FROM_WIN32(WaitError));
      if (FAILED(Signal.Result))
        return report("Delayed cross-queue signal", Signal.Result);
      GatePending = false;
      std::printf("Pending queue teardown and recorded code lifetime PASS\n");
      return waitFence(Completion.Get(), Repeat + 1);
    }();
    if (GatePending) {
      HRESULT Cleanup =
          report("Release failed cross-queue gate", Gate->Signal(Repeat + 1));
      if (FAILED(Cleanup))
        Result = Cleanup;
    }
    if (FAILED(Result))
      return Result;
    Result = report("Buffer execution removal status",
                    Device->GetDeviceRemovedReason());
    if (FAILED(Result))
      return Result;
    Result = report("Map command readback", Readback->Map(0, &Full, &Memory));
    if (FAILED(Result))
      return Result;
    bool Valid = Memory != nullptr;
    std::array<std::array<bool, Threads / 2>, 2> Seen{};
    for (UINT I = 0; Valid && I < Words; ++I) {
      if (TypedUAV) {
        for (UINT Channel = 0; Valid && Channel < Components; ++Channel) {
          UINT Expected = PackedSample(I, Channel);
          if (I < Threads)
            for (UINT Step = 0; Step <= Repeat && Step < 2; ++Step) {
              Expected =
                  2 * Expected + PackedSample(I + 4 + (I % 2) * 4, Channel);
              if (Normalized)
                Expected = Expected > 255 ? 255 : Expected;
              else if (!Floating && ComponentBytes < 4) {
                if (Signed) {
                  INT32 Value;
                  std::memcpy(&Value, &Expected, sizeof(Value));
                  INT32 Min = -(1 << (ComponentBytes * 8 - 1));
                  INT32 Max = (1 << (ComponentBytes * 8 - 1)) - 1;
                  Value = Value < Min ? Min : Value > Max ? Max : Value;
                  std::memcpy(&Expected, &Value, sizeof(Expected));
                } else {
                  UINT Max = (1u << (ComponentBytes * 8)) - 1;
                  Expected = Expected > Max ? Max : Expected;
                }
              }
            }
          std::array<unsigned char, sizeof(UINT)> Encoded{};
          EncodeSample(Encoded.data(), Expected);
          const auto *Actual = static_cast<const unsigned char *>(Memory) +
                               (I * Components + Channel) * ComponentBytes;
          Valid = std::memcmp(Actual, Encoded.data(), ComponentBytes) == 0;
          if (!Valid)
            std::fprintf(stderr, "Typed UAV element %u channel %u mismatch\n",
                         I, Channel);
        }
        continue;
      }
      if (CounterDelta && I < Threads) {
        UINT Element = Append ? I / (Threads / 2) : I % 2;
        UINT Step = Repeat == 2 && Element == 1 ? 1 : Repeat;
        UINT Start = InitialCounter(Step, Element);
        UINT Lower = CounterDelta > 0 ? Start : Start - Threads / 2;
        UINT Value = static_cast<const UINT *>(Memory)[I];
        UINT Slot = Value - Lower;
        if (Append) {
          Valid = Value < Threads && Value % 2 == Element;
          Slot = Value / 2;
        } else if (Consume) {
          UINT SourceIndex = (Value - 5) / 7;
          Valid = Value >= 5 && (Value - 5) % 7 == 0 &&
                  SourceIndex >= Element * Threads / 2 &&
                  SourceIndex < (Element + 1) * Threads / 2;
          Slot = SourceIndex - Element * Threads / 2;
        } else
          Valid = Value >= Lower && Slot < Threads / 2;
        Valid = Valid && !Seen[Element][Slot];
        if (Repeat == 2 && Element == 1)
          Valid = Valid && Value == Previous[I];
        if (Valid)
          Seen[Element][Slot] = true;
        else
          std::fprintf(stderr,
                       "Counter result %u: got %u outside unique [%u,%u)\n", I,
                       Value, Lower, Lower + Threads / 2);
        continue;
      }
      UINT Expected = InitialValue(I);
      if (I < Threads) {
        UINT X = I % 15, Y = I / 15 % 6, Z = I / 90;
        UINT Peer = (X % 5 + 5 * (Y % 3 + 3 * Z) + 1) % 30;
        UINT PeerGlobal = X / 5 * 5 + Peer % 5 +
                          15 * (Y / 3 * 3 + Peer / 5 % 3 + 6 * (Peer / 15));
        UINT Executions = Predication ? (Repeat == 2 ? 5u : (Repeat + 1) * 2)
                          : Indirect  ? (Repeat ? 4u : 2u)
                                      : Repeat + 1;
        if (Arrays) {
          UINT Element = I % 2;
          UINT InputWord = Packed       ? I + 4 + 4 * Element
                           : Structured ? 2 * I + 5 + 4 * Element
                           : Components > 1
                               ? Components * (I + 4 + 4 * Element) + 1
                               : I + 4 + 4 * Element;
          UINT InputValue =
              Packed ? PackedSample(InputWord, Components > 1 ? 1 : 0)
                     : InitialValue(InputWord);
          UINT MappedOne = Normalized ? 255 : Packed && Floating ? 2 : 1;
          for (UINT Step = 0; Step <= Repeat; ++Step) {
            if (Step == 2 && Element == 1)
              continue;
            UINT Bias = 23 + Step * 5 + Element * 30;
            if (Packed && Step != 2)
              Bias += Threads;
            if (Step == 2)
              Bias -= X % 2 ? 10 : 16;
            UINT Value = Step == 2 ? 0 : InputValue;
            if (MappedTyped && Step != 2)
              Value = Step == 0 ? (Components == 1 ? MappedOne
                                   : Packed        ? PackedSample(InputWord, 0)
                                            : InitialValue(InputWord - 1))
                                : (Element ? MappedOne : 0);
            Expected += PeerGlobal + Bias + Value;
          }
        } else {
          Expected += Executions * (PeerGlobal + 23);
          if (ConstantBindings)
            Expected += 5 * Repeat * (Repeat + 1) / 2;
          if (Tables && Repeat == 2)
            Expected -= X % 2 ? 10 : 16;
          if (Views)
            Expected += (Repeat == 2 ? Executions - 1 : Executions) *
                        InitialValue(Structured ? 2 * I + 5 : I + 4);
        }
      }
      if (Floating) {
        float Value = static_cast<float>(Expected);
        std::memcpy(&Expected, &Value, sizeof(Expected));
      }
      Valid = static_cast<const UINT *>(Memory)[I] == Expected;
      if (!Valid)
        std::fprintf(stderr, "Buffer result %u: got %u expected %u\n", I,
                     static_cast<const UINT *>(Memory)[I], Expected);
    }
    if (Valid)
      std::memcpy(Previous.data(), Memory, Bytes);
    Readback->Unmap(0, &NoRead);
    if (!Valid)
      return report("Owned grouped compute/readback mismatch", E_FAIL);
    if (CounterDelta) {
      D3D12_RANGE CounterRange{0, CounterBytes};
      Result = report("Map counter readback",
                      CounterReadback->Map(0, &CounterRange, &Memory));
      if (FAILED(Result))
        return Result;
      Valid = Memory != nullptr;
      for (UINT I = 0; Valid && I < CounterBytes / sizeof(UINT); ++I) {
        UINT Expected = 0xcdcdcdcd;
        if (I == 0 || I == CounterStride / sizeof(UINT)) {
          UINT Element = I ? 1 : 0;
          UINT Start = InitialCounter(Repeat, Element);
          UINT Count = Repeat == 2 && Element == 1 ? 0 : Threads / 2;
          Expected = CounterDelta > 0 ? Start + Count : Start - Count;
        } else if (Consume && I >= CounterDataOffset / sizeof(UINT))
          Expected = InitialValue(I - CounterDataOffset / sizeof(UINT));
        Valid = static_cast<const UINT *>(Memory)[I] == Expected;
      }
      CounterReadback->Unmap(0, &NoRead);
      if (!Valid)
        return report("Counter values or guards mismatch", E_FAIL);
      std::printf("Structured UAV counters and guards PASS\n");
    }
    std::printf("Grouped buffer oracle: repeat=%u values=%u guards=%u\n",
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
#ifdef FEME_WARP_COEXIST
  bool Coexist = Argc == 4 && std::wcscmp(Argv[1], L"--coexist") == 0;
  CoexistState Coexistence;
#else
  constexpr bool Coexist = false;
#endif
  bool ReferenceBufferCommands =
      Argc == 2 && std::wcscmp(Argv[1], L"--reference-buffer-commands") == 0;
  bool ReferenceCBV =
      Argc == 2 && std::wcscmp(Argv[1], L"--reference-cbv-commands") == 0;
  bool OrderedCBV =
      Argc == 2 && std::wcscmp(Argv[1], L"--ordered-cbv-commands") == 0;
  bool DXILCBV = Argc == 3 && std::wcscmp(Argv[1], L"--dxil-cbv-commands") == 0;
  bool CBV = ReferenceCBV || OrderedCBV || DXILCBV ||
             (Argc == 2 && std::wcscmp(Argv[1], L"--cbv-commands") == 0);
  bool ReferenceVisibility =
      Argc == 2 &&
      std::wcscmp(Argv[1], L"--reference-visibility-commands") == 0;
  bool OrderedVisibility =
      Argc == 2 && std::wcscmp(Argv[1], L"--ordered-visibility-commands") == 0;
  bool DXILVisibility =
      Argc == 3 && std::wcscmp(Argv[1], L"--dxil-visibility-commands") == 0;
  bool Visibility =
      ReferenceVisibility || OrderedVisibility || DXILVisibility ||
      (Argc == 2 && std::wcscmp(Argv[1], L"--visibility-commands") == 0);
  bool ReferenceConstants =
      ReferenceVisibility ||
      (Argc == 2 &&
       std::wcscmp(Argv[1], L"--reference-constant-commands") == 0);
  bool OrderedConstants =
      OrderedVisibility ||
      (Argc == 2 && std::wcscmp(Argv[1], L"--ordered-constant-commands") == 0);
  bool DXILConstants =
      DXILVisibility ||
      (Argc == 3 && std::wcscmp(Argv[1], L"--dxil-constant-commands") == 0);
  bool Inline =
      Visibility || ReferenceConstants || OrderedConstants || DXILConstants ||
      (Argc == 2 && std::wcscmp(Argv[1], L"--constant-commands") == 0);
  bool ReferenceTables =
      Argc == 2 && std::wcscmp(Argv[1], L"--reference-table-commands") == 0;
  bool OrderedTables =
      Argc == 2 && std::wcscmp(Argv[1], L"--ordered-table-commands") == 0;
  bool DXILTables =
      Argc == 3 && std::wcscmp(Argv[1], L"--dxil-table-commands") == 0;
  bool Tables = ReferenceTables || OrderedTables || DXILTables ||
                (Argc == 2 && std::wcscmp(Argv[1], L"--table-commands") == 0);
  bool StructuredViews =
      (Argc == 2 &&
       (std::wcscmp(Argv[1], L"--structured-view-commands") == 0 ||
        std::wcscmp(Argv[1], L"--reference-structured-view-commands") == 0 ||
        std::wcscmp(Argv[1], L"--ordered-structured-view-commands") == 0)) ||
      (Argc == 3 &&
       std::wcscmp(Argv[1], L"--dxil-structured-view-commands") == 0);
  bool ReferenceViews =
      Argc == 2 &&
      (std::wcscmp(Argv[1], L"--reference-view-commands") == 0 ||
       std::wcscmp(Argv[1], L"--reference-structured-view-commands") == 0);
  bool OrderedViews =
      Argc == 2 &&
      (std::wcscmp(Argv[1], L"--ordered-view-commands") == 0 ||
       std::wcscmp(Argv[1], L"--ordered-structured-view-commands") == 0);
  bool DXILViews =
      Argc == 3 &&
      (std::wcscmp(Argv[1], L"--dxil-view-commands") == 0 ||
       std::wcscmp(Argv[1], L"--dxil-structured-view-commands") == 0);
  bool Views = StructuredViews || ReferenceViews || OrderedViews || DXILViews ||
               (Argc == 2 && std::wcscmp(Argv[1], L"--view-commands") == 0);
  bool StructuredArrays =
      (Argc == 2 &&
       (std::wcscmp(Argv[1], L"--structured-array-commands") == 0 ||
        std::wcscmp(Argv[1], L"--reference-structured-array-commands") == 0 ||
        std::wcscmp(Argv[1], L"--ordered-structured-array-commands") == 0)) ||
      (Argc == 3 &&
       std::wcscmp(Argv[1], L"--dxil-structured-array-commands") == 0);
  bool ReferenceArrays =
      Argc == 2 &&
      (std::wcscmp(Argv[1], L"--reference-array-commands") == 0 ||
       std::wcscmp(Argv[1], L"--reference-structured-array-commands") == 0);
  bool OrderedArrays =
      Argc == 2 &&
      (std::wcscmp(Argv[1], L"--ordered-array-commands") == 0 ||
       std::wcscmp(Argv[1], L"--ordered-structured-array-commands") == 0);
  bool DXILArrays =
      Argc == 3 &&
      (std::wcscmp(Argv[1], L"--dxil-array-commands") == 0 ||
       std::wcscmp(Argv[1], L"--dxil-structured-array-commands") == 0);
  bool Arrays = StructuredArrays || ReferenceArrays || OrderedArrays ||
                DXILArrays ||
                (Argc == 2 && std::wcscmp(Argv[1], L"--array-commands") == 0);
  DXGI_FORMAT Typed = DXGI_FORMAT_UNKNOWN;
  bool ReferenceTyped = false, OrderedTyped = false, DXILTyped = false;
  bool MappedTyped = false, TypedUAV = false;
  const wchar_t *TypedModes[][4] = {
      {L"--typed-uint-commands", L"--reference-typed-uint-commands",
       L"--ordered-typed-uint-commands", L"--dxil-typed-uint-commands"},
      {L"--typed-sint-commands", L"--reference-typed-sint-commands",
       L"--ordered-typed-sint-commands", L"--dxil-typed-sint-commands"},
      {L"--typed-float-commands", L"--reference-typed-float-commands",
       L"--ordered-typed-float-commands", L"--dxil-typed-float-commands"},
      {L"--typed-uint2-commands", L"--reference-typed-uint2-commands",
       L"--ordered-typed-uint2-commands", L"--dxil-typed-uint2-commands"},
      {L"--typed-sint2-commands", L"--reference-typed-sint2-commands",
       L"--ordered-typed-sint2-commands", L"--dxil-typed-sint2-commands"},
      {L"--typed-float2-commands", L"--reference-typed-float2-commands",
       L"--ordered-typed-float2-commands", L"--dxil-typed-float2-commands"},
      {L"--typed-uint4-commands", L"--reference-typed-uint4-commands",
       L"--ordered-typed-uint4-commands", L"--dxil-typed-uint4-commands"},
      {L"--typed-sint4-commands", L"--reference-typed-sint4-commands",
       L"--ordered-typed-sint4-commands", L"--dxil-typed-sint4-commands"},
      {L"--typed-float4-commands", L"--reference-typed-float4-commands",
       L"--ordered-typed-float4-commands", L"--dxil-typed-float4-commands"},
      {L"--typed-uint16-commands", L"--reference-typed-uint16-commands",
       L"--ordered-typed-uint16-commands", L"--dxil-typed-uint16-commands"},
      {L"--typed-sint16-commands", L"--reference-typed-sint16-commands",
       L"--ordered-typed-sint16-commands", L"--dxil-typed-sint16-commands"},
      {L"--typed-float16-commands", L"--reference-typed-float16-commands",
       L"--ordered-typed-float16-commands", L"--dxil-typed-float16-commands"},
      {L"--typed-uint8-commands", L"--reference-typed-uint8-commands",
       L"--ordered-typed-uint8-commands", L"--dxil-typed-uint8-commands"},
      {L"--typed-sint8-commands", L"--reference-typed-sint8-commands",
       L"--ordered-typed-sint8-commands", L"--dxil-typed-sint8-commands"},
      {L"--typed-unorm8-commands", L"--reference-typed-unorm8-commands",
       L"--ordered-typed-unorm8-commands", L"--dxil-typed-unorm8-commands"},
      {L"--typed-uint16x4-commands", L"--reference-typed-uint16x4-commands",
       L"--ordered-typed-uint16x4-commands", L"--dxil-typed-uint16x4-commands"},
      {L"--typed-sint16x4-commands", L"--reference-typed-sint16x4-commands",
       L"--ordered-typed-sint16x4-commands", L"--dxil-typed-sint16x4-commands"},
      {L"--typed-float16x4-commands", L"--reference-typed-float16x4-commands",
       L"--ordered-typed-float16x4-commands",
       L"--dxil-typed-float16x4-commands"},
      {L"--typed-uint8x4-commands", L"--reference-typed-uint8x4-commands",
       L"--ordered-typed-uint8x4-commands", L"--dxil-typed-uint8x4-commands"},
      {L"--typed-sint8x4-commands", L"--reference-typed-sint8x4-commands",
       L"--ordered-typed-sint8x4-commands", L"--dxil-typed-sint8x4-commands"},
      {L"--typed-unorm8x4-commands", L"--reference-typed-unorm8x4-commands",
       L"--ordered-typed-unorm8x4-commands",
       L"--dxil-typed-unorm8x4-commands"}};
  const DXGI_FORMAT TypedFormats[] = {DXGI_FORMAT_R32_UINT,
                                      DXGI_FORMAT_R32_SINT,
                                      DXGI_FORMAT_R32_FLOAT,
                                      DXGI_FORMAT_R32G32_UINT,
                                      DXGI_FORMAT_R32G32_SINT,
                                      DXGI_FORMAT_R32G32_FLOAT,
                                      DXGI_FORMAT_R32G32B32A32_UINT,
                                      DXGI_FORMAT_R32G32B32A32_SINT,
                                      DXGI_FORMAT_R32G32B32A32_FLOAT,
                                      DXGI_FORMAT_R16_UINT,
                                      DXGI_FORMAT_R16_SINT,
                                      DXGI_FORMAT_R16_FLOAT,
                                      DXGI_FORMAT_R8_UINT,
                                      DXGI_FORMAT_R8_SINT,
                                      DXGI_FORMAT_R8_UNORM,
                                      DXGI_FORMAT_R16G16B16A16_UINT,
                                      DXGI_FORMAT_R16G16B16A16_SINT,
                                      DXGI_FORMAT_R16G16B16A16_FLOAT,
                                      DXGI_FORMAT_R8G8B8A8_UINT,
                                      DXGI_FORMAT_R8G8B8A8_SINT,
                                      DXGI_FORMAT_R8G8B8A8_UNORM};
  static_assert(ARRAYSIZE(TypedModes) == ARRAYSIZE(TypedFormats));
  for (UINT I = 0; I < ARRAYSIZE(TypedModes); ++I)
    for (UINT J = 0; J < 4; ++J) {
      std::wstring MappedMode =
          L"--mapped-" + std::wstring(TypedModes[I][J] + 2);
      bool Mapped = Argc == (J == 3 ? 3 : 2) &&
                    std::wcscmp(Argv[1], MappedMode.c_str()) == 0;
      std::wstring UAVMode(TypedModes[I][J]);
      UAVMode.insert(UAVMode.find(L"typed-") + 6, L"uav-");
      bool UAV = Argc == (J == 3 ? 3 : 2) &&
                 std::wcscmp(Argv[1], UAVMode.c_str()) == 0;
      if (Argc == (J == 3 ? 3 : 2) &&
          (std::wcscmp(Argv[1], TypedModes[I][J]) == 0 || Mapped || UAV)) {
        Typed = TypedFormats[I];
        ReferenceTyped = J == 1;
        OrderedTyped = J == 2;
        DXILTyped = J == 3;
        MappedTyped = Mapped;
        TypedUAV = UAV;
      }
    }
  int CounterDelta = 0;
  BufferCounterOperation CounterOperation = BufferCounterOperation::None;
  const BufferCounterOperation CounterOperations[] = {
      BufferCounterOperation::Increment, BufferCounterOperation::Decrement,
      BufferCounterOperation::Append, BufferCounterOperation::Consume};
  const wchar_t *CounterModes[][4] = {
      {L"--counter-increment-commands",
       L"--reference-counter-increment-commands",
       L"--ordered-counter-increment-commands",
       L"--dxil-counter-increment-commands"},
      {L"--counter-decrement-commands",
       L"--reference-counter-decrement-commands",
       L"--ordered-counter-decrement-commands",
       L"--dxil-counter-decrement-commands"},
      {L"--counter-append-commands", L"--reference-counter-append-commands",
       L"--ordered-counter-append-commands", L"--dxil-counter-append-commands"},
      {L"--counter-consume-commands", L"--reference-counter-consume-commands",
       L"--ordered-counter-consume-commands",
       L"--dxil-counter-consume-commands"}};
  for (UINT I = 0; I < ARRAYSIZE(CounterModes); ++I)
    for (UINT J = 0; J < 4; ++J)
      if (Argc == (J == 3 ? 3 : 2) &&
          std::wcscmp(Argv[1], CounterModes[I][J]) == 0) {
        CounterDelta = I == 1 || I == 3 ? -1 : 1;
        CounterOperation = CounterOperations[I];
        ReferenceArrays |= J == 1;
        OrderedArrays |= J == 2;
        DXILArrays |= J == 3;
      }
  Arrays |= Typed != DXGI_FORMAT_UNKNOWN || CounterDelta;
  ReferenceArrays |= ReferenceTyped;
  OrderedArrays |= OrderedTyped;
  DXILArrays |= DXILTyped;
  StructuredViews |= StructuredArrays || CounterDelta;
  ReferenceViews |= ReferenceArrays;
  OrderedViews |= OrderedArrays;
  DXILViews |= DXILArrays;
  Views |= Arrays;
  Tables |= Views;
  CBV |= Tables;
  bool ReferenceIndirect =
      Argc == 2 && std::wcscmp(Argv[1], L"--reference-indirect-commands") == 0;
  bool OrderedIndirect =
      Argc == 2 && std::wcscmp(Argv[1], L"--ordered-indirect-commands") == 0;
  bool DXILIndirect =
      Argc == 3 && std::wcscmp(Argv[1], L"--dxil-indirect-commands") == 0;
  bool Indirect =
      ReferenceIndirect || OrderedIndirect || DXILIndirect ||
      (Argc == 2 && std::wcscmp(Argv[1], L"--indirect-commands") == 0);
  bool ReferencePredication =
      Argc == 2 && std::wcscmp(Argv[1], L"--reference-predicate-commands") == 0;
  bool OrderedPredication =
      Argc == 2 && std::wcscmp(Argv[1], L"--ordered-predicate-commands") == 0;
  bool DXILPredication =
      Argc == 3 && std::wcscmp(Argv[1], L"--dxil-predicate-commands") == 0;
  bool Predication =
      ReferencePredication || OrderedPredication || DXILPredication ||
      (Argc == 2 && std::wcscmp(Argv[1], L"--predicate-commands") == 0);
  bool ReferenceIndirectPredication =
      Argc == 2 &&
      std::wcscmp(Argv[1], L"--reference-predicate-indirect-commands") == 0;
  bool OrderedIndirectPredication =
      Argc == 2 &&
      std::wcscmp(Argv[1], L"--ordered-predicate-indirect-commands") == 0;
  bool DXILIndirectPredication =
      Argc == 3 &&
      std::wcscmp(Argv[1], L"--dxil-predicate-indirect-commands") == 0;
  bool IndirectPredication =
      ReferenceIndirectPredication || OrderedIndirectPredication ||
      DXILIndirectPredication ||
      (Argc == 2 &&
       std::wcscmp(Argv[1], L"--predicate-indirect-commands") == 0);
  Predication |= IndirectPredication;
  Indirect |= IndirectPredication;
  ReferencePredication |= ReferenceIndirectPredication;
  OrderedPredication |= OrderedIndirectPredication;
  DXILPredication |= DXILIndirectPredication;
  bool ReferencePlacedBuffers =
      Argc == 2 && std::wcscmp(Argv[1], L"--reference-placed-buffers") == 0;
  bool PlacedBuffers =
      ReferencePlacedBuffers ||
      (Argc == 2 && std::wcscmp(Argv[1], L"--placed-buffers") == 0);
  bool ArrayTexture =
      Argc == 2 &&
      (std::wcscmp(Argv[1], L"--texture-array-copies") == 0 ||
       std::wcscmp(Argv[1], L"--ordered-texture-array-copies") == 0 ||
       std::wcscmp(Argv[1], L"--reference-texture-array-copies") == 0 ||
       std::wcscmp(Argv[1], L"--reference-ordered-texture-array-copies") == 0);
  bool TextureCopies =
      ArrayTexture ||
      Argc == 2 &&
          (std::wcscmp(Argv[1], L"--texture-copies") == 0 ||
           std::wcscmp(Argv[1], L"--ordered-texture-copies") == 0 ||
           std::wcscmp(Argv[1], L"--reference-texture-copies") == 0 ||
           std::wcscmp(Argv[1], L"--reference-ordered-texture-copies") == 0);
  bool ReferenceFootprints =
      Argc == 2 &&
      (std::wcscmp(Argv[1], L"--reference-footprint-copies") == 0 ||
       std::wcscmp(Argv[1], L"--reference-ordered-footprint-copies") == 0 ||
       std::wcscmp(Argv[1], L"--reference-texture-copies") == 0 ||
       std::wcscmp(Argv[1], L"--reference-ordered-texture-copies") == 0 ||
       std::wcscmp(Argv[1], L"--reference-texture-array-copies") == 0 ||
       std::wcscmp(Argv[1], L"--reference-ordered-texture-array-copies") == 0);
  bool OrderedFootprints =
      Argc == 2 &&
      (std::wcscmp(Argv[1], L"--ordered-footprint-copies") == 0 ||
       std::wcscmp(Argv[1], L"--reference-ordered-footprint-copies") == 0 ||
       std::wcscmp(Argv[1], L"--ordered-texture-copies") == 0 ||
       std::wcscmp(Argv[1], L"--reference-ordered-texture-copies") == 0 ||
       std::wcscmp(Argv[1], L"--ordered-texture-array-copies") == 0 ||
       std::wcscmp(Argv[1], L"--reference-ordered-texture-array-copies") == 0);
  bool Footprints =
      TextureCopies || ReferenceFootprints || OrderedFootprints ||
      (Argc == 2 && std::wcscmp(Argv[1], L"--footprint-copies") == 0);
  bool ReferenceTextureViews =
      Argc == 3 &&
      (std::wcscmp(Argv[1], L"--reference-texture-views") == 0 ||
       std::wcscmp(Argv[1], L"--reference-ordered-texture-views") == 0 ||
       std::wcscmp(Argv[1], L"--reference-single-layer-texture-views") == 0 ||
       std::wcscmp(Argv[1],
                   L"--reference-ordered-single-layer-texture-views") == 0);
  bool OrderedTextureViews =
      Argc == 3 &&
      (std::wcscmp(Argv[1], L"--ordered-texture-views") == 0 ||
       std::wcscmp(Argv[1], L"--reference-ordered-texture-views") == 0 ||
       std::wcscmp(Argv[1], L"--ordered-single-layer-texture-views") == 0 ||
       std::wcscmp(Argv[1],
                   L"--reference-ordered-single-layer-texture-views") == 0);
  bool SingleLayerTextureViews =
      Argc == 3 &&
      (std::wcscmp(Argv[1], L"--single-layer-texture-views") == 0 ||
       std::wcscmp(Argv[1], L"--ordered-single-layer-texture-views") == 0 ||
       std::wcscmp(Argv[1], L"--reference-single-layer-texture-views") == 0 ||
       std::wcscmp(Argv[1],
                   L"--reference-ordered-single-layer-texture-views") == 0);
  bool TextureViews =
      ReferenceTextureViews || OrderedTextureViews || SingleLayerTextureViews ||
      (Argc == 3 && std::wcscmp(Argv[1], L"--texture-views") == 0);
  bool Reference = ReferenceViews || ReferenceTables || ReferenceConstants ||
                   ReferenceCBV || ReferenceIndirect || ReferencePredication ||
                   ReferenceBufferCommands || ReferencePlacedBuffers ||
                   ReferenceFootprints || ReferenceTextureViews ||
                   (Argc == 2 && std::wcscmp(Argv[1], L"--reference") == 0);
  bool OrderedBufferCommands =
      OrderedViews || OrderedTables || OrderedConstants || OrderedCBV ||
      OrderedIndirect || OrderedPredication ||
      (Argc == 2 && std::wcscmp(Argv[1], L"--ordered-buffer-commands") == 0);
  bool Fences = Argc == 2 && std::wcscmp(Argv[1], L"--fences") == 0;
  bool DXILBufferCommands =
      DXILViews || DXILTables || DXILConstants || DXILCBV || DXILIndirect ||
      DXILPredication ||
      (Argc == 3 && std::wcscmp(Argv[1], L"--dxil-buffer-commands") == 0);
  bool BufferCommands =
      Inline || CBV || Indirect || Predication || DXILBufferCommands ||
      OrderedBufferCommands || ReferenceBufferCommands ||
      (Argc == 2 && std::wcscmp(Argv[1], L"--buffer-commands") == 0);
  bool DXILCommands =
      Argc == 3 && std::wcscmp(Argv[1], L"--dxil-commands") == 0;
  bool Commands = Coexist || DXILCommands || BufferCommands ||
                  (Argc == 2 && std::wcscmp(Argv[1], L"--commands") == 0);
  bool ExpectShaderRejection =
      Argc == 2 && std::wcscmp(Argv[1], L"--expect-shader-rejection") == 0;
  if (Argc != 1 && !Reference && !ExpectShaderRejection && !Commands &&
      !Fences && !PlacedBuffers && !Footprints && !TextureViews) {
    std::fprintf(stderr,
                 "Usage: warp-prototype-harness "
                 "[--reference|--expect-shader-rejection|--commands|"
                 "--buffer-commands|--dxil-commands FILE|"
                 "--dxil-buffer-commands FILE|--fences|"
                 "--ordered-buffer-commands|"
                 "--reference-buffer-commands|"
                 "--indirect-commands|--reference-indirect-commands|"
                 "--ordered-indirect-commands|"
                 "--dxil-indirect-commands FILE|"
                 "--predicate-commands|--reference-predicate-commands|"
                 "--ordered-predicate-commands|"
                 "--dxil-predicate-commands FILE|"
                 "--predicate-indirect-commands|"
                 "--reference-predicate-indirect-commands|"
                 "--ordered-predicate-indirect-commands|"
                 "--dxil-predicate-indirect-commands FILE|"
                 "--cbv-commands|--reference-cbv-commands|"
                 "--ordered-cbv-commands|--dxil-cbv-commands FILE|"
                 "--constant-commands|--reference-constant-commands|"
                 "--ordered-constant-commands|"
                 "--dxil-constant-commands FILE|"
                 "--visibility-commands|--reference-visibility-commands|"
                 "--ordered-visibility-commands|"
                 "--dxil-visibility-commands FILE|"
                 "--table-commands|--reference-table-commands|"
                 "--ordered-table-commands|--dxil-table-commands FILE|"
                 "--view-commands|--reference-view-commands|"
                 "--ordered-view-commands|--dxil-view-commands FILE|"
                 "--structured-view-commands|"
                 "--reference-structured-view-commands|"
                 "--ordered-structured-view-commands|"
                 "--dxil-structured-view-commands FILE|"
                 "--array-commands|--reference-array-commands|"
                 "--ordered-array-commands|--dxil-array-commands FILE|"
                 "--structured-array-commands|"
                 "--reference-structured-array-commands|"
                 "--ordered-structured-array-commands|"
                 "--dxil-structured-array-commands FILE|"
                 "--typed-{uint,sint,float}-commands|"
                 "--reference-typed-{uint,sint,float}-commands|"
                 "--ordered-typed-{uint,sint,float}-commands|"
                 "--dxil-typed-{uint,sint,float}-commands FILE|"
                 "--placed-buffers|--reference-placed-buffers|"
                 "--footprint-copies|--reference-footprint-copies|"
                 "--ordered-footprint-copies|"
                 "--reference-ordered-footprint-copies|"
                 "--texture-copies|--ordered-texture-copies|"
                 "--reference-texture-copies|"
                 "--reference-ordered-texture-copies|"
                 "--texture-array-copies|--ordered-texture-array-copies|"
                 "--reference-texture-array-copies|"
                 "--reference-ordered-texture-array-copies|"
                 "--texture-views FILE|--ordered-texture-views FILE|"
                 "--reference-texture-views FILE|"
                 "--reference-ordered-texture-views FILE|"
                 "--[reference-][ordered-]single-layer-texture-views FILE|"
                 "--coexist DXCOMPILER_DLL VULKAN_ICD_DLL]\n");
    return 1;
  }
  std::vector<unsigned char> DXILBytes;
  D3D12_SHADER_BYTECODE DXIL{};
#ifdef FEME_WARP_COEXIST
  if (Coexist) {
    if (FAILED(Coexistence.initialize(Argv[2], Argv[3])))
      return 1;
    DXIL = {Coexistence.Shader->GetBufferPointer(),
            Coexistence.Shader->GetBufferSize()};
  }
#endif
  if (DXILCommands || DXILBufferCommands || TextureViews) {
    FILE *Input = nullptr;
    if (_wfopen_s(&Input, Argv[2], L"rb") || !Input) {
      report("Open original DXC shader", E_FAIL);
      return 1;
    }
    bool Valid = std::fseek(Input, 0, SEEK_END) == 0;
    long Size = Valid ? std::ftell(Input) : -1;
    Valid = Valid && Size > 0 && Size <= 32 * 1024 * 1024;
    if (Valid)
      Valid = std::fseek(Input, 0, SEEK_SET) == 0;
    if (Valid) {
      DXILBytes.resize(static_cast<SIZE_T>(Size));
      Valid = std::fread(DXILBytes.data(), 1, DXILBytes.size(), Input) ==
              DXILBytes.size();
    }
    Valid &= std::fclose(Input) == 0;
    if (!Valid) {
      report("Read original DXC shader", E_FAIL);
      return 1;
    }
    DXIL = {DXILBytes.data(), DXILBytes.size()};
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
  if (!Reference && SUCCEEDED(Result)) {
    Result = checkExports(Module, WarpExports);
    HMODULE Compiler = GetModuleHandleW(L"feme_warp_compiler.dll");
    if (Compiler && SUCCEEDED(Result))
      Result = checkExports(Compiler, CompilerExports);
    else if (!ExpectShaderRejection && SUCCEEDED(Result))
      Result = report("Missing owned compiler exports", E_FAIL);
  }
  if (SUCCEEDED(Result))
    Result = exerciseBuffer(Device.Get());
  if (SUCCEEDED(Result) && PlacedBuffers)
    Result = exercisePlacedBuffers(Device.Get());
  if (SUCCEEDED(Result) && Footprints)
    Result = exerciseFootprintCopies(Device.Get(), OrderedFootprints,
                                     TextureCopies, ArrayTexture);
  if (SUCCEEDED(Result) && Fences)
    Result = exerciseFences(Device.Get());
  if (SUCCEEDED(Result) && (Commands || TextureViews))
    if (!Reference)
      Result = queryWavePolicy(Device.Get());
  if (SUCCEEDED(Result) && TextureViews)
    Result = exerciseTextureViews(Device.Get(), DXIL, OrderedTextureViews,
                                  SingleLayerTextureViews);
  if (SUCCEEDED(Result) && Commands)
    Result = exerciseCommands(Device.Get(),
                              DXILCommands || Coexist ? &DXIL : nullptr);
  if (SUCCEEDED(Result) && BufferCommands)
    Result = exerciseBufferCommands(
        Device.Get(), DXILBufferCommands ? &DXIL : nullptr,
        OrderedBufferCommands, Indirect, CBV, Inline, Tables, Views,
        StructuredViews, Arrays, Typed, Predication, CounterOperation,
        MappedTyped, TypedUAV, Visibility);
#ifdef FEME_WARP_COEXIST
  if (SUCCEEDED(Result) && Coexist)
    Result = Coexistence.compile();
  if (SUCCEEDED(Result) && Coexist)
    Result = Coexistence.query();
#endif
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
      UINT ExpectedShaders = BufferCommands             ? 4
                             : Commands || TextureViews ? 3
                                                        : 2;
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
          !Data.LiveFencesAtDestroy && !Data.ExceptionCode;
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
      UINT ExpectedHeaps = TextureViews                     ? 6
                           : TextureCopies                  ? 4
                           : CounterDelta                   ? 9
                           : CBV || Indirect || Predication ? 6
                           : BufferCommands                 ? 4
                           : PlacedBuffers || Footprints    ? 3
                                                            : 1;
      ValidOwned &=
          Data.Created == ExpectedHeaps && Data.Destroyed == ExpectedHeaps &&
          Data.Maps > 0 && Data.Maps == Data.Unmaps &&
          Data.MakeResidentCallbacks == (TextureViews                     ? 7u
                                         : TextureCopies                  ? 5u
                                         : CounterDelta                   ? 10u
                                         : CBV || Indirect || Predication ? 7u
                                         : BufferCommands                 ? 5u
                                         : PlacedBuffers || Footprints    ? 4u
                                                                       : 2u) &&
          Data.EvictCallbacks == 1 && Data.HasRuntimePagingQueue;
    } else
      ValidOwned = false;
    if (Commands || PlacedBuffers || Footprints || TextureViews) {
      using CommandAuditFn = void(APIENTRY *)(UINT *, UINT *);
      auto Audit = reinterpret_cast<CommandAuditFn>(
          GetProcAddress(Module, "FeMeWarpGetCommandAudit"));
      UINT Dispatches = 0, Copies = 0;
      if (Audit)
        Audit(&Dispatches, &Copies);
      std::printf("Native command execution: dispatches=%u copies=%u\n",
                  Dispatches, Copies);
      ValidOwned &=
          Audit &&
          Dispatches == (TextureViews     ? (OrderedTextureViews ? 1u : 3u)
                         : Predication    ? 11u
                         : Indirect       ? 10u
                         : BufferCommands ? 9u
                         : PlacedBuffers || Footprints ? 0u
                                                       : 6u) &&
          Copies == (TextureViews    ? (OrderedTextureViews ? 14u : 16u)
                     : ArrayTexture  ? 13u
                     : TextureCopies ? 7u
                     : CounterDelta  ? 10u
                     : Indirect || Predication     ? 7u
                     : BufferCommands              ? 4u
                     : PlacedBuffers || Footprints ? 2u
                                                   : 0u);
    }
  }
#ifdef FEME_WARP_COEXIST
  if (Coexist) {
    HRESULT Cleanup = Coexistence.close();
    if (FAILED(Cleanup))
      Result = Cleanup;
    if (SUCCEEDED(Result))
      std::printf("DXC, FeMe Vulkan ICD and owned WARP coexistence PASS\n");
  }
#endif
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
