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
#include <cstring>
#include <cwchar>

namespace {
HRESULT report(const wchar_t *Operation, HRESULT Result) {
  std::wprintf(L"%ls: 0x%08lx\n", Operation,
               static_cast<unsigned long>(Result));
  return Result;
}

using GetCallsFn = void(APIENTRY *)(LONG *, LONG *, LONG *, LONG *, LONG *,
                                    LONG *, LONG *, LONG *, LONG *);

bool checkShaderCaps(D3D12DDI_HADAPTER Adapter, PFND3D12DDI_GETCAPS GetCaps) {
  struct {
    D3D12DDI_SHADER_CAPS_0042 Caps;
    UINT32 Guard;
  } Data{};
  Data.Guard = 0x12345678;
  Data.Caps.WaveOps = TRUE;
  D3D12DDIARG_GETCAPS Args{};
  Args.Type = D3D12DDICAPS_TYPE_SHADER;
  Args.pData = &Data.Caps;
  Args.DataSize = sizeof(Data.Caps);
  HRESULT Result = report(L"Modern shader caps", GetCaps(Adapter, &Args));
  bool Valid = Result == S_OK && !Data.Caps.WaveOps &&
               Data.Caps.WaveLaneCountMin >= 4 &&
               Data.Caps.WaveLaneCountMin <= Data.Caps.WaveLaneCountMax &&
               Data.Caps.TotalLaneCount >= Data.Caps.WaveLaneCountMax &&
               Data.Guard == 0x12345678;

  D3D12DDI_SHADER_CAPS_0003 Legacy{};
  Legacy.Doubles = TRUE;
  Args.pData = &Legacy;
  Args.DataSize = sizeof(Legacy);
  Result = report(L"Legacy shader caps", GetCaps(Adapter, &Args));
  Valid &= Result == S_OK && !Legacy.Doubles;

  const auto Original = Data;
  Args.pData = &Data.Caps;
  Args.DataSize = sizeof(Data.Caps) - 1;
  Result = report(L"Truncated shader caps", GetCaps(Adapter, &Args));
  Valid &= Result == E_INVALIDARG &&
           std::memcmp(&Data, &Original, sizeof(Data)) == 0;

  Args.pData = nullptr;
  Args.DataSize = sizeof(Data.Caps);
  Result = report(L"Missing shader caps output", GetCaps(Adapter, &Args));
  Valid &= Result == E_INVALIDARG;
  if (!Valid)
    report(L"Shader capability contract mismatch", E_FAIL);
  return Valid;
}
bool checkShaderModels(D3D12DDI_HADAPTER Adapter, PFND3D12DDI_GETCAPS GetCaps) {
  UINT Count = 0;
  D3D12DDI_D3D12_SHADER_MODELS_DATA_0011 Models{};
  Models.pNumShaderModelsSupported = &Count;
  D3D12DDIARG_GETCAPS Args{};
  Args.Type = D3D12DDICAPS_TYPE_0011_SHADER_MODELS;
  Args.pData = &Models;
  Args.DataSize = sizeof(Models);
  HRESULT Result = report(L"Shader model count", GetCaps(Adapter, &Args));
  bool Valid = Result == S_OK && Count == 1;

  D3D12DDI_SHADER_MODEL Output[] = {D3D12DDI_SHADER_MODEL_6_0_RELEASE_0011,
                                    D3D12DDI_SHADER_MODEL_6_0_RELEASE_0011};
  Models.pShaderModelsSupported = Output;
  Result = report(L"Shader model list", GetCaps(Adapter, &Args));
  Valid &= Result == S_OK && Count == 1 &&
           Output[0] == D3D12DDI_SHADER_MODEL_5_1_RELEASE_0011 &&
           Output[1] == D3D12DDI_SHADER_MODEL_6_0_RELEASE_0011;

  Count = 0;
  Output[0] = D3D12DDI_SHADER_MODEL_6_0_RELEASE_0011;
  Result = report(L"Insufficient shader model output", GetCaps(Adapter, &Args));
  Valid &= Result == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) &&
           Count == 1 && Output[0] == D3D12DDI_SHADER_MODEL_6_0_RELEASE_0011;

  Models.pNumShaderModelsSupported = nullptr;
  Result = report(L"Missing shader model count", GetCaps(Adapter, &Args));
  Valid &= Result == E_INVALIDARG;
  Args.DataSize = sizeof(Models) - 1;
  Result = report(L"Truncated shader model query", GetCaps(Adapter, &Args));
  Valid &= Result == E_INVALIDARG;
  Args.DataSize = sizeof(Models);
  Args.pData = nullptr;
  Result = report(L"Missing shader model query", GetCaps(Adapter, &Args));
  Valid &= Result == E_INVALIDARG;
  if (!Valid)
    report(L"Shader model negotiation mismatch", E_FAIL);
  return Valid;
}

bool checkIntegrationCaps(D3D12DDI_HADAPTER Adapter,
                          PFND3D12DDI_GETCAPS GetCaps) {
  struct {
    D3D12DDI_MEMORY_ARCHITECTURE_CAPS_0041 Caps;
    UINT Guard;
  } Data{};
  Data.Guard = 0x12345678;
  UINT Node = 0;
  D3D12DDIARG_GETCAPS Args{};
  Args.Type = D3D12DDICAPS_TYPE_MEMORY_ARCHITECTURE;
  Args.pInfo = &Node;
  Args.pData = &Data.Caps;
  Args.DataSize = sizeof(Data.Caps);
  bool Valid = GetCaps(Adapter, &Args) == S_OK && Data.Caps.UMA &&
               Data.Caps.IOCoherent && Data.Caps.CacheCoherent &&
               Data.Caps.HeapSerializationTier ==
                   D3D12DDI_HEAP_SERIALIZATION_TIER_0041_0 &&
               Data.Caps.ResourceSerializationTier ==
                   D3D12DDI_RESOURCE_SERIALIZATION_TIER_0041_0 &&
               Data.Guard == 0x12345678;
  const auto Original = Data;
  Node = 1;
  Valid &= GetCaps(Adapter, &Args) == E_INVALIDARG &&
           std::memcmp(&Data, &Original, sizeof(Data)) == 0;
  Node = 0;
  Args.pInfo = nullptr;
  Valid &= GetCaps(Adapter, &Args) == E_INVALIDARG;
  Args.pInfo = &Node;
  Args.DataSize = sizeof(Data.Caps) - 1;
  Valid &= GetCaps(Adapter, &Args) == E_INVALIDARG &&
           std::memcmp(&Data, &Original, sizeof(Data)) == 0;

  struct {
    D3D12DDI_MEMORY_ARCHITECTURE_CAPS Caps;
    UINT Guard;
  } Legacy{};
  Legacy.Guard = 0x12345678;
  Args.pData = &Legacy.Caps;
  Args.DataSize = sizeof(Legacy.Caps);
  Valid &= GetCaps(Adapter, &Args) == S_OK && Legacy.Caps.UMA &&
           Legacy.Caps.IOCoherent && Legacy.Caps.CacheCoherent &&
           Legacy.Guard == 0x12345678;

  const struct {
    D3D12DDICAPS_TYPE Type;
    UINT Size;
  } Queries[] = {
      {D3D12DDICAPS_TYPE_0023_UMD_BASED_COMMAND_QUEUE_PRIORITY,
       sizeof(D3D12DDICAPS_UMD_BASED_COMMAND_QUEUE_PRIORITY_DATA_0023)},
      {D3D12DDICAPS_TYPE_0050_HARDWARE_SCHEDULING_CAPS,
       sizeof(D3D12DDICAPS_HARDWARE_SCHEDULING_CAPS_0050)},
      {D3D12DDICAPS_TYPE_ARCHITECTURE_INFO,
       sizeof(D3D12DDI_ARCHITECTURE_INFO_DATA)},
      {D3D12DDICAPS_TYPE_0022_TEXTURE_LAYOUT,
       sizeof(D3D12DDI_TEXTURE_LAYOUT_CAPS_0026)}};
  for (const auto &Query : Queries) {
    alignas(D3D12DDI_TEXTURE_LAYOUT_CAPS_0026) unsigned char
        Output[sizeof(D3D12DDI_TEXTURE_LAYOUT_CAPS_0026) + 4];
    std::memset(Output, 0x5a, sizeof(Output));
    Args.Type = Query.Type;
    Args.pInfo = nullptr;
    Args.pData = Output;
    Args.DataSize = Query.Size;
    Valid &= GetCaps(Adapter, &Args) == S_OK;
    for (UINT I = 0; I < sizeof(Output); ++I)
      Valid &= Output[I] == (I < Query.Size ? 0 : 0x5a);
    std::memset(Output, 0x5a, sizeof(Output));
    Args.DataSize = Query.Size - 1;
    Valid &= GetCaps(Adapter, &Args) == E_INVALIDARG;
    for (unsigned char Byte : Output)
      Valid &= Byte == 0x5a;
    Args.DataSize = Query.Size;
    Args.pData = nullptr;
    Valid &= GetCaps(Adapter, &Args) == E_INVALIDARG;
  }
  UINT Info[] = {D3D12DDI_TL_ROW_MAJOR, D3D12DDI_FUNCUNIT_COMBINED};
  D3D12DDI_ROW_MAJOR_LAYOUT_CAPS Row{};
  Args.Type = D3D12DDICAPS_TYPE_TEXTURE_LAYOUT_SETS;
  Args.pInfo = Info;
  Args.pData = &Row;
  Args.DataSize = sizeof(Row);
  Valid &= GetCaps(Adapter, &Args) == S_OK &&
           Row.SubCaps[0].MaxElementSize == 16 &&
           Row.SubCaps[0].BaseOffsetAlignment == 512 &&
           Row.SubCaps[0].PitchAlignment == 256 &&
           Row.SubCaps[0].DepthPitchAlignment == 256 &&
           Row.SubCaps[1].MaxElementSize == 0;
  const auto OriginalRow = Row;
  Info[0] = D3D12DDI_TL_64KB_TILE_UNDEFINED_SWIZZLE;
  Valid &= GetCaps(Adapter, &Args) == E_INVALIDARG &&
           std::memcmp(&Row, &OriginalRow, sizeof(Row)) == 0;
  Info[0] = D3D12DDI_TL_ROW_MAJOR;
  Info[1] = 0xffffffff;
  Valid &= GetCaps(Adapter, &Args) == E_INVALIDARG;
  Args.pInfo = nullptr;
  Valid &= GetCaps(Adapter, &Args) == E_INVALIDARG;

  D3D12DDI_COMMAND_QUEUE_FLAGS Flags = D3D12DDI_COMMAND_QUEUE_FLAG_3D;
  Args.Type = D3D12DDICAPS_TYPE_0022_CPU_PAGE_TABLE_FALSE_POSITIVES;
  Args.pInfo = &Node;
  Args.pData = &Flags;
  Args.DataSize = sizeof(Flags);
  Valid &= GetCaps(Adapter, &Args) == S_OK && Flags == 0;
  Node = 1;
  Flags = D3D12DDI_COMMAND_QUEUE_FLAG_3D;
  Valid &= GetCaps(Adapter, &Args) == E_INVALIDARG &&
           Flags == D3D12DDI_COMMAND_QUEUE_FLAG_3D;
  report(L"Memory and scheduling capability contract", Valid ? S_OK : E_FAIL);
  return Valid;
}
} // namespace

int wmain(int Argc, wchar_t **Argv) {
  bool ShaderCaps = Argc == 3 && std::wcscmp(Argv[2], L"--shader-caps") == 0;
  bool ShaderModels =
      Argc == 3 && std::wcscmp(Argv[2], L"--shader-models") == 0;
  bool IntegrationCaps =
      Argc == 3 && std::wcscmp(Argv[2], L"--integration-caps") == 0;
  if (Argc != 2 && !ShaderCaps && !ShaderModels && !IntegrationCaps) {
    std::fwprintf(stderr,
                  L"Usage: %ls STUB_DLL_PATH "
                  L"[--shader-caps|--shader-models|--integration-caps]\n",
                  Argv[0]);
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
  auto OpenAdapter12 = reinterpret_cast<OpenAdapter12Fn>(
      GetProcAddress(Module, "OpenAdapter12"));
  auto ReadCalls = reinterpret_cast<GetCallsFn>(
      GetProcAddress(Module, "FeMeBootstrapGetCalls"));
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
  if (ShaderCaps || ShaderModels || IntegrationCaps) {
    bool Valid = IntegrationCaps
                     ? checkIntegrationCaps(OpenArgs.hAdapter, Funcs.pfnGetCaps)
                 : ShaderCaps
                     ? checkShaderCaps(OpenArgs.hAdapter, Funcs.pfnGetCaps)
                     : checkShaderModels(OpenArgs.hAdapter, Funcs.pfnGetCaps);
    Result =
        report(L"pfnCloseAdapter", Funcs.pfnCloseAdapter(OpenArgs.hAdapter));
    bool Released = FreeLibrary(Module) != FALSE;
    if (!Released)
      report(L"FreeLibrary", HRESULT_FROM_WIN32(GetLastError()));
    return Valid && SUCCEEDED(Result) && Released ? 0 : 1;
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
  HRESULT CreateResult =
      report(L"pfnCreateDevice",
             Funcs.pfnCreateDevice(OpenArgs.hAdapter, &DeviceArgs));

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
