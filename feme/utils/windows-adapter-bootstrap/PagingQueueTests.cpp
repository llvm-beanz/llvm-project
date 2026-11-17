//===- PagingQueueTests.cpp - Paging-queue probe failure-path tests
//--------===//
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

extern "C" HRESULT APIENTRY OpenAdapter12(D3D12DDIARG_OPENADAPTER *);
extern "C" void APIENTRY
FeMeBootstrapGetPagingQueueProbe(feme::probe::PagingQueueProbe *);

namespace {
enum class Mode {
  Success,
  CreateFailure,
  DestroyFailure,
  MissingCreate,
  MissingDestroy,
  MissingDevice,
  MissingTable,
  MissingQueue,
  MissingSync,
  MissingFence,
  CreateException,
  DestroyException,
  CreateStatus,
  DestroyStatus
};

constexpr const wchar_t *ModeNames[] = {
    L"success",        L"create-failure",   L"destroy-failure",
    L"missing-create", L"missing-destroy",  L"missing-device",
    L"missing-table",  L"missing-queue",    L"missing-sync",
    L"missing-fence",  L"create-exception", L"destroy-exception",
    L"create-status",  L"destroy-status"};
constexpr DWORD FixtureException = 0xe0420001;
struct TestState {
  Mode Scenario = Mode::Success;
  bool ValidInputs = true;
  UINT CreateCalls = 0;
  UINT DestroyCalls = 0;
  UINT64 Fence = 0;
};
TestState GState;

HRESULT APIENTRY createPagingQueue(HANDLE Device,
                                   D3DDDICB_CREATEPAGINGQUEUE *Queue) {
  ++GState.CreateCalls;
  if (Device != &GState || !Queue ||
      Queue->Priority != D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL ||
      Queue->PhysicalAdapterIndex || Queue->hPagingQueue ||
      Queue->hSyncObject || Queue->FenceValueCPUVirtualAddress) {
    GState.ValidInputs = false;
    return E_INVALIDARG;
  }
  if (GState.Scenario == Mode::CreateFailure)
    return E_OUTOFMEMORY;
  if (GState.Scenario == Mode::CreateStatus)
    return S_FALSE;
  if (GState.Scenario == Mode::CreateException)
    RaiseException(FixtureException, 0, 0, nullptr);
  Queue->hPagingQueue = GState.Scenario == Mode::MissingQueue ? 0 : 11;
  Queue->hSyncObject = GState.Scenario == Mode::MissingSync ? 0 : 22;
  Queue->FenceValueCPUVirtualAddress =
      GState.Scenario == Mode::MissingFence ? nullptr : &GState.Fence;
  return S_OK;
}

HRESULT APIENTRY destroyPagingQueue(HANDLE Device,
                                    const D3DDDI_DESTROYPAGINGQUEUE *Queue) {
  ++GState.DestroyCalls;
  if (Device != &GState || !Queue || Queue->hPagingQueue != 11) {
    GState.ValidInputs = false;
    return E_INVALIDARG;
  }
  if (GState.Scenario == Mode::DestroyFailure)
    return E_ACCESSDENIED;
  if (GState.Scenario == Mode::DestroyStatus)
    return S_FALSE;
  if (GState.Scenario == Mode::DestroyException)
    RaiseException(FixtureException, 0, 0, nullptr);
  return S_OK;
}
} // namespace

int wmain(int Argc, wchar_t **Argv) {
  UINT Index = 0;
  if (Argc == 2)
    for (; Index < ARRAYSIZE(ModeNames); ++Index)
      if (std::wcscmp(Argv[1], ModeNames[Index]) == 0)
        break;
  if (Argc != 2 || Index == ARRAYSIZE(ModeNames)) {
    std::fwprintf(stderr, L"Expected a paging-queue fixture scenario.\n");
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
  if (Scenario != Mode::MissingCreate)
    Callbacks.pfnCreatePagingQueueCb = createPagingQueue;
  if (Scenario != Mode::MissingDestroy)
    Callbacks.pfnDestroyPagingQueueCb = destroyPagingQueue;
  D3D12DDIARG_CREATEDEVICE_0003 Args{};
  Args.Interface = D3D12DDI_INTERFACE_VERSION_R1;
  Args.hRTDevice.handle = Scenario == Mode::MissingDevice ? nullptr : &GState;
  Args.pKTCallbacks = Scenario == Mode::MissingTable ? nullptr : &Callbacks;
  HRESULT Result = Funcs.pfnCreateDevice(OpenArgs.hAdapter, &Args);
  HRESULT CloseResult = Funcs.pfnCloseAdapter(OpenArgs.hAdapter);
  feme::probe::PagingQueueProbe Probe;
  FeMeBootstrapGetPagingQueueProbe(&Probe);

  bool Missing =
      Scenario == Mode::MissingCreate || Scenario == Mode::MissingDestroy ||
      Scenario == Mode::MissingDevice || Scenario == Mode::MissingTable;
  bool CreateReturned = !Missing && Scenario != Mode::CreateException;
  bool Created = CreateReturned && Scenario != Mode::CreateFailure &&
                 Scenario != Mode::CreateStatus;
  bool HasQueue = Created && Scenario != Mode::MissingQueue;
  bool DestroyReturned = HasQueue && Scenario != Mode::DestroyException;
  HRESULT CreateResult = !CreateReturned                   ? S_OK
                         : Scenario == Mode::CreateFailure ? E_OUTOFMEMORY
                         : Scenario == Mode::CreateStatus  ? S_FALSE
                                                           : S_OK;
  HRESULT DestroyResult = !DestroyReturned                   ? S_OK
                          : Scenario == Mode::DestroyFailure ? E_ACCESSDENIED
                          : Scenario == Mode::DestroyStatus  ? S_FALSE
                                                             : S_OK;
  DWORD Exception =
      Scenario == Mode::CreateException || Scenario == Mode::DestroyException
          ? FixtureException
          : 0;
  HRESULT Expected = Scenario == Mode::Success          ? E_NOTIMPL
                     : Scenario == Mode::MissingDevice  ? E_INVALIDARG
                     : Missing                          ? E_NOINTERFACE
                     : Scenario == Mode::CreateFailure  ? E_OUTOFMEMORY
                     : Scenario == Mode::DestroyFailure ? E_ACCESSDENIED
                                                        : E_FAIL;
  bool Passed =
      Result == Expected && CloseResult == S_OK && GState.ValidInputs &&
      GState.CreateCalls == unsigned(!Missing) &&
      GState.DestroyCalls == unsigned(HasQueue) && Probe.Enabled &&
      Probe.CreateAttempted == !Missing &&
      Probe.CreateReturned == CreateReturned &&
      Probe.CreateResult == CreateResult && Probe.HasPagingQueue == HasQueue &&
      Probe.HasSyncObject == (Created && Scenario != Mode::MissingSync) &&
      Probe.HasFenceMapping == (Created && Scenario != Mode::MissingFence) &&
      Probe.DestroyAttempted == HasQueue &&
      Probe.DestroyReturned == DestroyReturned &&
      Probe.DestroyResult == DestroyResult && Probe.ExceptionCode == Exception;
  std::wprintf(
      L"Mock callbacks only, not kernel evidence: %ls; "
      L"Result=0x%08lx Expected=0x%08lx CreateCalls=%u DestroyCalls=%u "
      L"Exception=0x%08lx; %ls\n",
      ModeNames[Index], static_cast<unsigned long>(Result),
      static_cast<unsigned long>(Expected), GState.CreateCalls,
      GState.DestroyCalls, static_cast<unsigned long>(Probe.ExceptionCode),
      Passed ? L"passed" : L"FAILED");
  return Passed ? 0 : 1;
}
