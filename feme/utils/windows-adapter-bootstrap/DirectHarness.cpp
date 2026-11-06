//===- DirectHarness.cpp - Call OpenAdapter12 directly, bypassing DXGI --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// windows-adapter-probe's rejection-stub result establishes that DXGI/D3D12
// never call OpenAdapter10/10_2/12 for a module loaded through
// IDXGIFactory::CreateSoftwareAdapter: all three counters stay zero even
// after D3D12CreateDevice fails with DXGI_ERROR_UNSUPPORTED. Public Microsoft
// documentation for CreateSoftwareAdapter itself only references the Vista-
// era (D3D10) driver development kit, and offers no D3D12/OpenAdapter12
// contract; no other public API documents a third-party route to
// OpenAdapter12. This harness does not use DXGI or D3D12CreateDevice at all:
// it loads a module directly, resolves OpenAdapter12 with GetProcAddress, and
// invokes it by hand with a minimal, hand-built D3D12DDIARG_OPENADAPTER. This
// measures whether the UMD DDI entry point and the table it fills in are
// mechanically self-consistent when driven directly -- a distinct question
// from whether the OS will ever route to it for a third-party module, which
// this harness deliberately does not attempt to answer.
//
//===----------------------------------------------------------------------===//

#include <windows.h>
#include <winternl.h>

#include <d3d10umddi.h>
#include <d3d12umddi.h>
#include <dxgi.h>

#include <cstdio>
#include <cwchar>

namespace {
HRESULT report(const wchar_t *Operation, HRESULT Result) {
  std::wprintf(L"%ls: 0x%08lx\n", Operation, static_cast<unsigned long>(Result));
  return Result;
}

using GetCallsFn = void(APIENTRY *)(LONG *, LONG *, LONG *, LONG *, LONG *,
                                     LONG *, LONG *, LONG *, LONG *);
} // namespace

int wmain(int Argc, wchar_t **Argv) {
  if (Argc != 2) {
    std::fwprintf(stderr, L"Usage: %ls STUB_DLL_PATH\n", Argv[0]);
    return 1;
  }
  wchar_t FullPath[32768];
  DWORD Length =
      GetFullPathNameW(Argv[1], ARRAYSIZE(FullPath), FullPath, nullptr);
  if (!Length || Length >= ARRAYSIZE(FullPath)) {
    report(L"GetFullPathNameW", Length
                                    ? HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW)
                                    : HRESULT_FROM_WIN32(GetLastError()));
    return 1;
  }
  std::wprintf(L"Requested module: %ls\n", FullPath);
  HMODULE Module = LoadLibraryExW(FullPath, nullptr,
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                       LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!Module) {
    report(L"LoadLibraryExW", HRESULT_FROM_WIN32(GetLastError()));
    return 1;
  }

  using OpenAdapter12Fn = HRESULT(APIENTRY *)(D3D12DDIARG_OPENADAPTER *);
  auto OpenAdapter12 =
      reinterpret_cast<OpenAdapter12Fn>(GetProcAddress(Module, "OpenAdapter12"));
  auto ReadCalls =
      reinterpret_cast<GetCallsFn>(GetProcAddress(Module, "FeMeBootstrapGetCalls"));
  if (!OpenAdapter12 || !ReadCalls) {
    report(L"GetProcAddress", HRESULT_FROM_WIN32(GetLastError()));
    FreeLibrary(Module);
    return 1;
  }

  // The adapter and runtime callbacks are left zeroed: this stub's DDI
  // functions never invoke them, so a zeroed D3DDDI_ADAPTERCALLBACKS is safe
  // for this experiment only. A real driver would require valid callbacks.
  D3DDDI_ADAPTERCALLBACKS Callbacks{};
  D3D12DDI_ADAPTERFUNCS Funcs{};
  D3D12DDIARG_OPENADAPTER OpenArgs{};
  OpenArgs.pAdapterCallbacks = &Callbacks;
  OpenArgs.pAdapterFuncs = &Funcs;

  HRESULT Result = report(L"OpenAdapter12", OpenAdapter12(&OpenArgs));
  bool TableFilled = Funcs.pfnCalcPrivateDeviceSize && Funcs.pfnCreateDevice &&
                      Funcs.pfnCloseAdapter && Funcs.pfnGetSupportedVersions &&
                      Funcs.pfnGetCaps && Funcs.pfnGetOptionalDDITables &&
                      Funcs.pfnFillDDITable && Funcs.pfnDestroyDevice;
  std::wprintf(L"Adapter function table filled: %ls\n",
               TableFilled ? L"yes" : L"no");
  if (FAILED(Result) || !TableFilled) {
    FreeLibrary(Module);
    return 1;
  }

  UINT32 VersionCount = 0;
  HRESULT VersionsResult = report(
      L"pfnGetSupportedVersions",
      Funcs.pfnGetSupportedVersions(OpenArgs.hAdapter, &VersionCount, nullptr));
  std::wprintf(L"Supported DDI interface versions: %u\n", VersionCount);

  D3D12DDIARG_GETCAPS CapsArgs{};
  HRESULT CapsResult =
      report(L"pfnGetCaps", Funcs.pfnGetCaps(OpenArgs.hAdapter, &CapsArgs));

  D3D12DDIARG_CALCPRIVATEDEVICESIZE SizeArgs{};
  SIZE_T DeviceSize =
      Funcs.pfnCalcPrivateDeviceSize(OpenArgs.hAdapter, &SizeArgs);
  std::wprintf(L"pfnCalcPrivateDeviceSize: %zu\n", DeviceSize);

  UINT32 TableCount = 0;
  HRESULT TablesResult = report(
      L"pfnGetOptionalDDITables",
      Funcs.pfnGetOptionalDDITables(OpenArgs.hAdapter, &TableCount, nullptr));

  D3D12DDIARG_CREATEDEVICE_0003 DeviceArgs{};
  HRESULT CreateResult = report(
      L"pfnCreateDevice", Funcs.pfnCreateDevice(OpenArgs.hAdapter, &DeviceArgs));

  HRESULT CloseResult =
      report(L"pfnCloseAdapter", Funcs.pfnCloseAdapter(OpenArgs.hAdapter));

  LONG OpenAdapter12Count = 0, CalcPrivateDeviceSizeCount = 0,
       GetSupportedVersionsCount = 0, GetCapsCount = 0,
       GetOptionalDDITablesCount = 0, FillDDITableCount = 0,
       CreateDeviceCount = 0, CloseAdapterCount = 0, DestroyDeviceCount = 0;
  ReadCalls(&OpenAdapter12Count, &CalcPrivateDeviceSizeCount,
            &GetSupportedVersionsCount, &GetCapsCount,
            &GetOptionalDDITablesCount, &FillDDITableCount, &CreateDeviceCount,
            &CloseAdapterCount, &DestroyDeviceCount);
  std::wprintf(L"Calls: OpenAdapter12=%ld CalcPrivateDeviceSize=%ld "
               L"GetSupportedVersions=%ld GetCaps=%ld "
               L"GetOptionalDDITables=%ld FillDDITable=%ld CreateDevice=%ld "
               L"CloseAdapter=%ld DestroyDevice=%ld\n",
               OpenAdapter12Count, CalcPrivateDeviceSizeCount,
               GetSupportedVersionsCount, GetCapsCount,
               GetOptionalDDITablesCount, FillDDITableCount, CreateDeviceCount,
               CloseAdapterCount, DestroyDeviceCount);

  bool Released = FreeLibrary(Module) != FALSE;
  if (!Released)
    report(L"FreeLibrary", HRESULT_FROM_WIN32(GetLastError()));

  std::wprintf(L"This is a direct DDI call, not an OS-routed adapter; it is "
               L"not evidence that DXGI/D3D12CreateDevice can reach this "
               L"module.\n");

  bool ExpectedResults =
      SUCCEEDED(Result) && TableFilled && SUCCEEDED(VersionsResult) &&
      VersionCount == 0 && CapsResult == E_NOTIMPL && DeviceSize == 64 &&
      SUCCEEDED(TablesResult) && CreateResult == E_NOTIMPL &&
      SUCCEEDED(CloseResult) && OpenAdapter12Count == 1 &&
      CalcPrivateDeviceSizeCount == 1 && GetSupportedVersionsCount == 1 &&
      GetCapsCount == 1 && GetOptionalDDITablesCount == 1 &&
      CreateDeviceCount == 1 && CloseAdapterCount == 1;
  return Released && ExpectedResults ? 0 : 1;
}
