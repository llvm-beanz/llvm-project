//===- MonitoredFenceTests.cpp - Runtime CPU fence failure controls -------===//
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
#include <string>

extern "C" HRESULT APIENTRY OpenAdapter12(D3D12DDIARG_OPENADAPTER *);
extern "C" void APIENTRY
FeMeBootstrapGetMonitoredFenceProbe(feme::probe::MonitoredFenceProbe *);

namespace {
enum class Mode {
  Success,
  CreateFailure,
  CreateStatus,
  CreateException,
  MissingObject,
  MissingMapping,
  MisalignedMapping,
  InitialValue,
  WaitFailure,
  WaitStatus,
  WaitException,
  EarlyEvent,
  SignalFailure,
  SignalStatus,
  SignalException,
  Timeout,
  WrongCompleted,
  DestroyFailure,
  DestroyStatus,
  DestroyException,
  MissingCreate,
  MissingDestroy,
  MissingWait,
  MissingSignal,
  MissingDevice,
  MissingTable
};
struct TestCase {
  const wchar_t *Name;
  const char *Calls;
  HRESULT Result;
};
constexpr TestCase Cases[] = {
    {L"success", "cwsd", E_NOTIMPL},
    {L"create-failure", "c", E_OUTOFMEMORY},
    {L"create-status", "c", E_FAIL},
    {L"create-exception", "c", E_FAIL},
    {L"missing-object", "c", E_FAIL},
    {L"missing-mapping", "cd", E_FAIL},
    {L"misaligned-mapping", "cd", E_FAIL},
    {L"initial-value", "cd", E_FAIL},
    {L"wait-failure", "cwd", E_ACCESSDENIED},
    {L"wait-status", "cwd", E_FAIL},
    {L"wait-exception", "cwd", E_FAIL},
    {L"early-event", "cwd", E_FAIL},
    {L"signal-failure", "cwsd", E_ACCESSDENIED},
    {L"signal-status", "cwsd", E_FAIL},
    {L"signal-exception", "cwsd", E_FAIL},
    {L"timeout", "cwsd", static_cast<HRESULT>(0x800705b4L)},
    {L"wrong-completed", "cwsd", E_FAIL},
    {L"destroy-failure", "cwsd", E_ACCESSDENIED},
    {L"destroy-status", "cwsd", E_FAIL},
    {L"destroy-exception", "cwsd", E_FAIL},
    {L"missing-create", "", E_NOINTERFACE},
    {L"missing-destroy", "", E_NOINTERFACE},
    {L"missing-wait", "", E_NOINTERFACE},
    {L"missing-signal", "", E_NOINTERFACE},
    {L"missing-device", "", E_INVALIDARG},
    {L"missing-table", "", E_NOINTERFACE}};
constexpr DWORD FixtureException = 0xe0420003;
struct TestState {
  Mode Scenario;
  std::string Calls;
  bool ValidInputs = true;
  UINT64 Fence = 0;
  HANDLE Event = nullptr;
};
TestState *State = nullptr;

void check(char Call, bool Valid) {
  State->Calls += Call;
  State->ValidInputs &= Valid;
}

HRESULT APIENTRY createFence(HANDLE Device,
                             D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 *Args) {
  const auto &Info = Args->Info;
  check('c', Device == State && !Args->hSyncObject &&
                 Info.Type == D3DDDI_MONITORED_FENCE && !Info.Flags.Value &&
                 !Info.MonitoredFence.InitialFenceValue &&
                 !Info.MonitoredFence.FenceValueCPUVirtualAddress &&
                 !Info.MonitoredFence.FenceValueGPUVirtualAddress &&
                 Info.MonitoredFence.EngineAffinity == 1 &&
                 !Info.MonitoredFence.Padding && !Info.SharedHandle);
  if (State->Scenario == Mode::CreateFailure)
    return E_OUTOFMEMORY;
  if (State->Scenario == Mode::CreateStatus)
    return S_FALSE;
  if (State->Scenario == Mode::CreateException)
    RaiseException(FixtureException, 0, 0, nullptr);
  Args->hSyncObject = State->Scenario == Mode::MissingObject ? 0 : 11;
  if (State->Scenario != Mode::MissingMapping)
    Args->Info.MonitoredFence.FenceValueCPUVirtualAddress =
        State->Scenario == Mode::MisalignedMapping
            ? static_cast<void *>(reinterpret_cast<char *>(&State->Fence) + 1)
            : &State->Fence;
  if (State->Scenario == Mode::InitialValue)
    State->Fence = 1;
  return S_OK;
}

HRESULT APIENTRY waitFence(
    HANDLE Device, const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *Args) {
  check('w',
        Device == State && Args->ObjectCount == 1 && Args->ObjectHandleArray &&
            Args->ObjectHandleArray[0] == 11 && Args->FenceValueArray &&
            Args->FenceValueArray[0] == feme::probe::MonitoredFenceTarget &&
            Args->hAsyncEvent && !Args->Flags.Value);
  State->Event = Args->hAsyncEvent;
  if (State->Scenario == Mode::WaitFailure)
    return E_ACCESSDENIED;
  if (State->Scenario == Mode::WaitStatus)
    return S_FALSE;
  if (State->Scenario == Mode::WaitException)
    RaiseException(FixtureException, 0, 0, nullptr);
  if (State->Scenario == Mode::EarlyEvent)
    State->ValidInputs &= SetEvent(State->Event) != FALSE;
  return S_OK;
}

HRESULT APIENTRY signalFence(
    HANDLE Device, const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *Args) {
  check('s', Device == State && Args->ObjectCount == 1 &&
                 Args->ObjectHandleArray && Args->ObjectHandleArray[0] == 11 &&
                 Args->FenceValueArray &&
                 Args->FenceValueArray[0] == feme::probe::MonitoredFenceTarget);
  if (State->Scenario == Mode::SignalFailure)
    return E_ACCESSDENIED;
  if (State->Scenario == Mode::SignalStatus)
    return S_FALSE;
  if (State->Scenario == Mode::SignalException)
    RaiseException(FixtureException, 0, 0, nullptr);
  State->Fence = feme::probe::MonitoredFenceTarget +
                 unsigned(State->Scenario == Mode::WrongCompleted);
  if (State->Scenario != Mode::Timeout)
    State->ValidInputs &= SetEvent(State->Event) != FALSE;
  return S_OK;
}

HRESULT APIENTRY
destroyFence(HANDLE Device, const D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT *Args) {
  check('d', Device == State && Args->hSyncObject == 11);
  if (State->Scenario == Mode::DestroyFailure)
    return E_ACCESSDENIED;
  if (State->Scenario == Mode::DestroyStatus)
    return S_FALSE;
  if (State->Scenario == Mode::DestroyException)
    RaiseException(FixtureException, 0, 0, nullptr);
  return S_OK;
}
} // namespace

int wmain(int Argc, wchar_t **Argv) {
  UINT Index = 0;
  if (Argc == 2)
    for (; Index < ARRAYSIZE(Cases); ++Index)
      if (std::wcscmp(Argv[1], Cases[Index].Name) == 0)
        break;
  if (Argc != 2 || Index == ARRAYSIZE(Cases)) {
    std::fwprintf(stderr, L"Expected a monitored-fence fixture scenario.\n");
    return 1;
  }
  Mode Scenario = static_cast<Mode>(Index);
  TestState Storage{Scenario, {}};
  State = &Storage;
  D3DDDI_ADAPTERCALLBACKS AdapterCallbacks{};
  D3D12DDI_ADAPTERFUNCS Funcs{};
  D3D12DDIARG_OPENADAPTER Open{};
  Open.pAdapterCallbacks = &AdapterCallbacks;
  Open.pAdapterFuncs = &Funcs;
  if (OpenAdapter12(&Open) != S_OK) {
    std::fwprintf(stderr, L"Fixture adapter open failed.\n");
    return 1;
  }
  D3DDDI_DEVICECALLBACKS Callbacks{};
  if (Scenario != Mode::MissingCreate)
    Callbacks.pfnCreateSynchronizationObject2Cb = createFence;
  if (Scenario != Mode::MissingDestroy)
    Callbacks.pfnDestroySynchronizationObjectCb = destroyFence;
  if (Scenario != Mode::MissingWait)
    Callbacks.pfnWaitForSynchronizationObjectFromCpuCb = waitFence;
  if (Scenario != Mode::MissingSignal)
    Callbacks.pfnSignalSynchronizationObjectFromCpuCb = signalFence;
  D3D12DDIARG_CREATEDEVICE_0003 Args{};
  Args.Interface = D3D12DDI_INTERFACE_VERSION_R1;
  Args.hRTDevice.handle = Scenario == Mode::MissingDevice ? nullptr : State;
  Args.pKTCallbacks = Scenario == Mode::MissingTable ? nullptr : &Callbacks;
  HRESULT Result = Funcs.pfnCreateDevice(Open.hAdapter, &Args);
  HRESULT CloseResult = Funcs.pfnCloseAdapter(Open.hAdapter);
  feme::probe::MonitoredFenceProbe Probe;
  FeMeBootstrapGetMonitoredFenceProbe(&Probe);

  bool Create = Storage.Calls.find('c') != std::string::npos;
  bool Wait = Storage.Calls.find('w') != std::string::npos;
  bool Signal = Storage.Calls.find('s') != std::string::npos;
  bool Destroy = Storage.Calls.find('d') != std::string::npos;
  bool Created = Create && Scenario != Mode::CreateFailure &&
                 Scenario != Mode::CreateStatus &&
                 Scenario != Mode::CreateException;
  bool EventCompleted =
      Scenario == Mode::EarlyEvent ||
      (Signal && Scenario != Mode::SignalFailure &&
       Scenario != Mode::SignalStatus && Scenario != Mode::SignalException &&
       Scenario != Mode::Timeout);
  bool Exception =
      Scenario == Mode::CreateException || Scenario == Mode::WaitException ||
      Scenario == Mode::SignalException || Scenario == Mode::DestroyException;
  DWORD Win32Error = Scenario == Mode::Timeout ? ERROR_TIMEOUT : 0;
  bool Passed =
      Result == Cases[Index].Result && CloseResult == S_OK &&
      Storage.Calls == Cases[Index].Calls && Storage.ValidInputs &&
      Probe.Enabled && Probe.CreateAttempted == Create &&
      Probe.CreateReturned == (Create && Scenario != Mode::CreateException) &&
      Probe.CreateResult == (Scenario == Mode::CreateFailure  ? E_OUTOFMEMORY
                             : Scenario == Mode::CreateStatus ? S_FALSE
                                                              : S_OK) &&
      Probe.HasSyncObject == (Created && Scenario != Mode::MissingObject) &&
      Probe.HasFenceMapping == (Created && Scenario != Mode::MissingMapping &&
                                Scenario != Mode::MisalignedMapping) &&
      Probe.InitialValue == unsigned(Scenario == Mode::InitialValue) &&
      Probe.WaitAttempted == Wait &&
      Probe.WaitReturned == (Wait && Scenario != Mode::WaitException) &&
      Probe.WaitResult == (Scenario == Mode::WaitFailure  ? E_ACCESSDENIED
                           : Scenario == Mode::WaitStatus ? S_FALSE
                                                          : S_OK) &&
      Probe.WaitWasPending == Signal && Probe.SignalAttempted == Signal &&
      Probe.SignalReturned == (Signal && Scenario != Mode::SignalException) &&
      Probe.SignalResult == (Scenario == Mode::SignalFailure  ? E_ACCESSDENIED
                             : Scenario == Mode::SignalStatus ? S_FALSE
                                                              : S_OK) &&
      Probe.EventCompleted == EventCompleted &&
      Probe.EventRetained == (Wait && !EventCompleted) &&
      Probe.CompletedValue == (Signal && EventCompleted ? Storage.Fence : 0) &&
      Probe.DestroyAttempted == Destroy &&
      Probe.DestroyReturned ==
          (Destroy && Scenario != Mode::DestroyException) &&
      Probe.DestroyResult == (Scenario == Mode::DestroyFailure  ? E_ACCESSDENIED
                              : Scenario == Mode::DestroyStatus ? S_FALSE
                                                                : S_OK) &&
      Probe.ExceptionCode == (Exception ? FixtureException : 0) &&
      Probe.Win32Error == Win32Error;
  // These callbacks are synchronous mocks; no registration can outlive them.
  if (Probe.EventRetained && Storage.Event)
    Passed &= CloseHandle(Storage.Event) != FALSE;
  if (!Passed) {
    std::fwprintf(stderr,
                  L"Monitored-fence fixture %ls failed: "
                  L"result=0x%08lx calls=%hs exception=0x%08lx\n",
                  Argv[1], static_cast<unsigned long>(Result),
                  Storage.Calls.c_str(),
                  static_cast<unsigned long>(Probe.ExceptionCode));
    return 1;
  }
  std::wprintf(L"Monitored-fence fixture %ls passed.\n", Argv[1]);
  return 0;
}
