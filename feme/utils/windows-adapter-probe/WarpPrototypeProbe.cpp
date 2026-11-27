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

bool printCaps(HMODULE Module) {
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
  return Valid;
}
} // namespace

int wmain(int Argc, wchar_t **Argv) {
  bool Reference = Argc == 2 && std::wcscmp(Argv[1], L"--reference") == 0;
  bool ExpectShaderRejection =
      Argc == 2 && std::wcscmp(Argv[1], L"--expect-shader-rejection") == 0;
  if (Argc != 1 && !Reference && !ExpectShaderRejection) {
    std::fprintf(stderr, "Usage: warp-prototype-harness "
                         "[--reference|--expect-shader-rejection]\n");
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
  Device.Reset();
  Adapter.Reset();
  Factory.Reset();
  if (!Reference) {
    bool CapsValid = printCaps(Module);
    ValidRejection &= CapsValid;
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
    } else {
      Result = report("Missing owned lifecycle counters", E_FAIL);
      ValidRejection = false;
    }
    using LastFn = const char *(APIENTRY *)();
    auto Last = reinterpret_cast<LastFn>(
        GetProcAddress(Module, "FeMeWarpGetLastUnsupportedDDI"));
    if (!Last) {
      Result = report("Missing owned diagnostic export", E_FAIL);
      ValidRejection = false;
    } else if (const char *Name = Last()) {
      std::printf("Last unavailable DDI: %s\n", Name);
      ValidRejection &= std::strcmp(Name, "pfnCalcPrivateShaderSize") == 0;
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
      ValidRejection &=
          Audit.CoreTables == 1 && Audit.QueueTables == 1 &&
          Audit.CommandListTables == 2 && Audit.ExtendedTables == 1 &&
          Audit.ExecutableShaderQueries == 1 && Audit.UnsupportedCalls == 0;
    } else {
      ValidRejection = false;
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
          !Data.ExceptionCode;
      ValidRejection &= CleanupValid;
      if (!CleanupValid)
        Result = report("Device cleanup mismatch", E_FAIL);
    } else {
      Result = report("Missing owned context export", E_FAIL);
      ValidRejection = false;
    }
  }
  if (!FreeLibrary(Module)) {
    Result = report("FreeLibrary", HRESULT_FROM_WIN32(GetLastError()));
    ValidRejection = false;
  }
  if (ExpectShaderRejection) {
    report("Expected executable-shader rejection",
           ValidRejection ? S_OK : E_FAIL);
    return ValidRejection ? 0 : 1;
  }
  return SUCCEEDED(Result) ? 0 : 1;
}
