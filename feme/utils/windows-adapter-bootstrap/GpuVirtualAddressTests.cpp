//===- GpuVirtualAddressTests.cpp - GPU VA callback failure controls
//-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BootstrapAudit.h"

#include <windows.h>
#include <winternl.h>

#include <d3d12umddi.h>

#include <cstdio>
#include <cwchar>
#include <limits>

extern "C" HRESULT APIENTRY OpenAdapter12(D3D12DDIARG_OPENADAPTER *);
extern "C" void APIENTRY
FeMeBootstrapGetGpuVirtualAddressProbe(feme::probe::GpuVirtualAddressProbe *);

namespace {
enum class Mode {
  Success,
  ReserveFailure,
  FreeFailure,
  MissingReserve,
  MissingFree,
  MissingDevice,
  MissingTable,
  MissingAddress,
  MisalignedAddress,
  OverflowAddress,
  ReserveException,
  FreeException,
  ReserveStatus,
  FreeStatus,
  ReserveInvalidArgument,
  ReserveInvalidOutput
};
constexpr const wchar_t *ModeNames[] = {L"success",
                                        L"reserve-failure",
                                        L"free-failure",
                                        L"missing-reserve",
                                        L"missing-free",
                                        L"missing-device",
                                        L"missing-table",
                                        L"missing-address",
                                        L"misaligned-address",
                                        L"overflow-address",
                                        L"reserve-exception",
                                        L"free-exception",
                                        L"reserve-status",
                                        L"free-status",
                                        L"reserve-invalid-argument",
                                        L"reserve-invalid-output"};
constexpr DWORD FixtureException = 0xe0420002;
struct TestState {
  Mode Scenario = Mode::Success;
  bool ValidInputs = true;
  UINT ReserveCalls = 0;
  UINT FreeCalls = 0;
};
TestState GState;

std::uint64_t fixtureAddress() {
  if (GState.Scenario == Mode::MissingAddress ||
      GState.Scenario == Mode::ReserveInvalidArgument)
    return 0;
  if (GState.Scenario == Mode::MisalignedAddress)
    return 0x400001;
  if (GState.Scenario == Mode::OverflowAddress)
    return std::numeric_limits<std::uint64_t>::max() -
           feme::probe::GpuVaReservationBytes + 1;
  return 0x400000;
}

HRESULT APIENTRY reserveAddress(HANDLE Device,
                                D3DDDI_RESERVEGPUVIRTUALADDRESS *Args) {
  ++GState.ReserveCalls;
  if (Device != &GState || !Args || Args->hAdapter || Args->BaseAddress ||
      Args->MinimumAddress || Args->MaximumAddress ||
      Args->Size != feme::probe::GpuVaReservationBytes || Args->Reserved0 ||
      Args->Reserved1 || Args->VirtualAddress || Args->Reserved2) {
    GState.ValidInputs = false;
    return E_INVALIDARG;
  }
  if (GState.Scenario == Mode::ReserveFailure)
    return E_OUTOFMEMORY;
  if (GState.Scenario == Mode::ReserveException)
    RaiseException(FixtureException, 0, 0, nullptr);
  Args->VirtualAddress = fixtureAddress();
  if (GState.Scenario == Mode::ReserveInvalidArgument ||
      GState.Scenario == Mode::ReserveInvalidOutput)
    return E_INVALIDARG;
  // Even an unexpected positive status with an output must not imply ownership.
  return GState.Scenario == Mode::ReserveStatus ? S_FALSE : S_OK;
}

HRESULT APIENTRY freeAddress(HANDLE Device,
                             const D3DDDICB_FREEGPUVIRTUALADDRESS *Args) {
  ++GState.FreeCalls;
  if (Device != &GState || !Args || Args->BaseAddress != fixtureAddress() ||
      Args->Size != feme::probe::GpuVaReservationBytes) {
    GState.ValidInputs = false;
    return E_INVALIDARG;
  }
  if (GState.Scenario == Mode::FreeFailure)
    return E_ACCESSDENIED;
  if (GState.Scenario == Mode::FreeException)
    RaiseException(FixtureException, 0, 0, nullptr);
  return GState.Scenario == Mode::FreeStatus ? S_FALSE : S_OK;
}
} // namespace

int wmain(int Argc, wchar_t **Argv) {
  UINT Index = 0;
  if (Argc == 2)
    for (; Index < ARRAYSIZE(ModeNames); ++Index)
      if (std::wcscmp(Argv[1], ModeNames[Index]) == 0)
        break;
  if (Argc != 2 || Index == ARRAYSIZE(ModeNames)) {
    std::fwprintf(stderr, L"Expected a GPU VA fixture scenario.\n");
    return 1;
  }
  Mode Scenario = GState.Scenario = static_cast<Mode>(Index);
  D3DDDI_ADAPTERCALLBACKS AdapterCallbacks{};
  D3D12DDI_ADAPTERFUNCS Funcs{};
  D3D12DDIARG_OPENADAPTER OpenArgs{};
  OpenArgs.pAdapterCallbacks = &AdapterCallbacks;
  OpenArgs.pAdapterFuncs = &Funcs;
  if (OpenAdapter12(&OpenArgs) != S_OK) {
    std::fwprintf(stderr, L"Fixture adapter open failed.\n");
    return 1;
  }
  D3DDDI_DEVICECALLBACKS Callbacks{};
  if (Scenario != Mode::MissingReserve)
    Callbacks.pfnReserveGpuVirtualAddressCb = reserveAddress;
  if (Scenario != Mode::MissingFree)
    Callbacks.pfnFreeGpuVirtualAddressCb = freeAddress;
  D3D12DDIARG_CREATEDEVICE_0003 Args{};
  Args.Interface = D3D12DDI_INTERFACE_VERSION_R1;
  Args.hRTDevice.handle = Scenario == Mode::MissingDevice ? nullptr : &GState;
  Args.pKTCallbacks = Scenario == Mode::MissingTable ? nullptr : &Callbacks;
  HRESULT Result = Funcs.pfnCreateDevice(OpenArgs.hAdapter, &Args);
  HRESULT CloseResult = Funcs.pfnCloseAdapter(OpenArgs.hAdapter);
  feme::probe::GpuVirtualAddressProbe Probe;
  FeMeBootstrapGetGpuVirtualAddressProbe(&Probe);

  bool Missing =
      Scenario == Mode::MissingReserve || Scenario == Mode::MissingFree ||
      Scenario == Mode::MissingDevice || Scenario == Mode::MissingTable;
  bool ReserveReturned = !Missing && Scenario != Mode::ReserveException;
  bool InvalidArgument = Scenario == Mode::ReserveInvalidArgument ||
                         Scenario == Mode::ReserveInvalidOutput;
  bool Reserved = ReserveReturned && Scenario != Mode::ReserveFailure &&
                  Scenario != Mode::ReserveStatus && !InvalidArgument;
  bool HasAddress = Reserved && Scenario != Mode::MissingAddress;
  bool FreeReturned = HasAddress && Scenario != Mode::FreeException;
  bool ValidRange = HasAddress && Scenario != Mode::MisalignedAddress &&
                    Scenario != Mode::OverflowAddress;
  HRESULT ReserveResult = !ReserveReturned                   ? S_OK
                          : Scenario == Mode::ReserveFailure ? E_OUTOFMEMORY
                          : Scenario == Mode::ReserveStatus  ? S_FALSE
                          : InvalidArgument                  ? E_INVALIDARG
                                                             : S_OK;
  HRESULT FreeResult = !FreeReturned                   ? S_OK
                       : Scenario == Mode::FreeFailure ? E_ACCESSDENIED
                       : Scenario == Mode::FreeStatus  ? S_FALSE
                                                       : S_OK;
  DWORD Exception =
      Scenario == Mode::ReserveException || Scenario == Mode::FreeException
          ? FixtureException
          : 0;
  HRESULT Expected = InvalidArgument                    ? E_INVALIDARG
                     : Scenario == Mode::Success        ? E_NOTIMPL
                     : Scenario == Mode::MissingDevice  ? E_INVALIDARG
                     : Missing                          ? E_NOINTERFACE
                     : Scenario == Mode::ReserveFailure ? E_OUTOFMEMORY
                     : Scenario == Mode::FreeFailure    ? E_ACCESSDENIED
                                                        : E_FAIL;
  bool Passed =
      Result == Expected && CloseResult == S_OK && GState.ValidInputs &&
      GState.ReserveCalls == unsigned(!Missing) &&
      GState.FreeCalls == unsigned(HasAddress) && Probe.Enabled &&
      Probe.ReserveAttempted == !Missing &&
      Probe.ReserveReturned == ReserveReturned &&
      Probe.ReserveResult == ReserveResult &&
      Probe.Address == (ReserveReturned && Scenario != Mode::ReserveFailure
                            ? fixtureAddress()
                            : 0) &&
      Probe.HasValidRange == ValidRange && Probe.FreeAttempted == HasAddress &&
      Probe.FreeReturned == FreeReturned && Probe.FreeResult == FreeResult &&
      Probe.ExceptionCode == Exception;
  if (!Passed) {
    std::fwprintf(stderr,
                  L"GPU VA fixture %ls failed: result=0x%08lx "
                  L"reserve=%u free=%u address=0x%016llx exception=0x%08lx\n",
                  Argv[1], static_cast<unsigned long>(Result),
                  GState.ReserveCalls, GState.FreeCalls,
                  static_cast<unsigned long long>(Probe.Address),
                  static_cast<unsigned long>(Probe.ExceptionCode));
    return 1;
  }
  std::wprintf(L"GPU VA fixture %ls passed.\n", Argv[1]);
  return 0;
}
