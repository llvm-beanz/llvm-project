//===- AdapterProbe.cpp - Stock Windows software adapter boundary probe
//----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

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

static HRESULT exerciseAdapter(IDXGIAdapter1 *Adapter) {
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
  QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  ComPtr<ID3D12CommandQueue> Queue;
  Result = report(L"CreateCommandQueue",
                  Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue)));
  if (FAILED(Result))
    return Result;
  ComPtr<ID3D12Fence> Fence;
  Result = report(L"CreateFence", Device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                      IID_PPV_ARGS(&Fence)));
  if (FAILED(Result))
    return Result;
  HANDLE Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!Event)
    return report(L"CreateEventW", HRESULT_FROM_WIN32(GetLastError()));
  Result =
      report(L"SetEventOnCompletion", Fence->SetEventOnCompletion(1, Event));
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
  if (!CloseHandle(Event))
    return report(L"CloseHandle", HRESULT_FROM_WIN32(GetLastError()));
  return Result;
}

int wmain(int Argc, wchar_t **Argv) {
  bool Stub = Argc == 3 && std::wcscmp(Argv[1], L"--stub") == 0;
  bool Software = Argc == 3 && std::wcscmp(Argv[1], L"--software") == 0;
  bool WarpShim = Argc == 3 && std::wcscmp(Argv[1], L"--warp-shim") == 0;
  bool WarpBootstrap =
      Argc == 3 && std::wcscmp(Argv[1], L"--warp-bootstrap") == 0;
  bool WarpBootstrapAudit =
      Argc == 3 && std::wcscmp(Argv[1], L"--warp-bootstrap-audit") == 0;
  if (Argc != 1 && !Stub && !Software && !WarpShim && !WarpBootstrap &&
      !WarpBootstrapAudit) {
    std::fwprintf(
        stderr,
        L"Usage: %ls [--software DLL | --stub DLL | --warp-shim DLL | "
        L"--warp-bootstrap DLL | --warp-bootstrap-audit DLL]\n",
        Argv[0]);
    return 1;
  }
  ComPtr<IDXGIFactory4> Factory;
  if (FAILED(report(L"CreateDXGIFactory1",
                    CreateDXGIFactory1(IID_PPV_ARGS(&Factory)))))
    return 1;
  ComPtr<IDXGIAdapter1> Adapter;
  if (Argc == 1) {
    if (FAILED(report(L"EnumWarpAdapter",
                      Factory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter)))))
      return 1;
    if (FAILED(checkWarpIdentity(Adapter.Get())))
      return 1;
    HRESULT Result = exerciseAdapter(Adapter.Get());
    Adapter.Reset();
    Factory.Reset();
    std::wprintf(L"Reference WARP objects released; not FeMe coverage.\n");
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
  using GetCallbackProbe =
      void(APIENTRY *)(LONG *, LONG *, LONG *, DWORD *);
  GetCalls ReadCalls = nullptr;
  GetBootstrapCalls ReadBootstrapCalls = nullptr;
  GetAuditCapacity ReadAuditCapacity = nullptr;
  GetAudit ReadAudit = nullptr;
  GetCallbackProbe ReadCallbackProbe = nullptr;
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
    if (SUCCEEDED(Result) && (WarpShim || WarpBootstrap || WarpBootstrapAudit))
      Result = checkWarpIdentity(Adapter.Get());
    if (SUCCEEDED(Result)) {
      Opened = true;
      Result = exerciseAdapter(Adapter.Get());
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
      ExpectedDDICalls = Open == 1 && Versions > 0 && Caps > 0 &&
                         Close == 1 && Destroy == 0;
    } else {
      ExpectedDDICalls = Open == 1 && Versions > 0 && Caps > 0 && Close == 1 &&
                         Size == 0 && Optional == 0 && Fill == 0 &&
                         Create == 0 && Destroy == 0;
    }
    if (GetModuleHandleW(L"feme_warp_reference.dll")) {
      report(L"Unexpected reference WARP dependency", E_FAIL);
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
    }
  }
  bool Released = FreeLibrary(Module) != FALSE;
  if (!Released)
    report(L"FreeLibrary", HRESULT_FROM_WIN32(GetLastError()));
  std::wprintf(L"Software adapter objects released. Stub rejection is an "
               L"experiment, not a working FeMe adapter.\n");
  return Released && Opened && ExpectedDDICalls &&
                 (WarpBootstrapAudit
                      ? FAILED(Result)
                      : (Stub || WarpShim || WarpBootstrap
                             ? Result == DXGI_ERROR_UNSUPPORTED
                             : SUCCEEDED(Result)))
             ? 0
             : 1;
}
