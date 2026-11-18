//===- AllocationTests.cpp - Public KMT probe failure-path fixtures
//--------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Keep the isolated diagnostic and its injected-call fixtures in one
// translation unit; no implementation interface is exported for this standalone
// probe.
#define FEME_KMT_ALLOCATION_TEST
#include "AllocationProbe.cpp"

#include <cstring>
#include <string>

namespace {
enum class Mode {
  Success,
  Pending,
  OpenFailure,
  OpenStatus,
  MissingAdapter,
  DeviceFailure,
  MissingDevice,
  AllocationFailure,
  MissingResource,
  MissingAllocation,
  QueueFailure,
  MissingQueue,
  MissingSync,
  MissingFence,
  ResidentFailure,
  ResidentStatus,
  IncompleteResident,
  WaitFailure,
  WaitStatus,
  WaitTimeout,
  WaitFailed,
  EvictFailure,
  DestroyAllocationFailure,
  DestroyAllocationStatus,
  DestroyQueueFailure,
  DestroyQueueStatus,
  DestroyDeviceFailure,
  DestroyDeviceStatus,
  CloseAdapterFailure,
  CloseAdapterStatus
};

struct TestCase {
  const wchar_t *Name;
  const char *Calls;
  bool RunSucceeded = false;
  bool Closed = true;
};
constexpr TestCase Cases[] = {
    {L"success", "odaqreAQDC", true},
    {L"pending", "odaqrwteAQDC", true},
    {L"open-failure", "o"},
    {L"open-status", "o"},
    {L"missing-adapter", "o"},
    {L"device-failure", "odC"},
    {L"missing-device", "odC"},
    {L"allocation-failure", "odaDC"},
    {L"missing-resource", "odaADC"},
    {L"missing-allocation", "odaADC"},
    {L"queue-failure", "odaqADC"},
    {L"missing-queue", "odaqADC"},
    {L"missing-sync", "odaqAQDC"},
    {L"missing-fence", "odaqAQDC"},
    {L"resident-failure", "odaqrAQDC"},
    {L"resident-status", "odaqrAQDC"},
    {L"incomplete-resident", "odaqrAQDC"},
    {L"wait-failure", "odaqrwAQDC"},
    {L"wait-status", "odaqrwAQDC"},
    {L"wait-timeout", "odaqrwtAQDC"},
    {L"wait-failed", "odaqrwtAQDC"},
    {L"evict-failure", "odaqreAQDC"},
    {L"destroy-allocation-failure", "odaqreAQDC", true, false},
    {L"destroy-allocation-status", "odaqreAQDC", true, false},
    {L"destroy-queue-failure", "odaqreAQDC", true, false},
    {L"destroy-queue-status", "odaqreAQDC", true, false},
    {L"destroy-device-failure", "odaqreAQDC", true, false},
    {L"destroy-device-status", "odaqreAQDC", true, false},
    {L"close-adapter-failure", "odaqreAQDC", true, false},
    {L"close-adapter-status", "odaqreAQDC", true, false},
};

struct TestState {
  Mode Scenario;
  std::string Calls;
  bool ValidInputs = true;
};
TestState *State = nullptr;
constexpr NTSTATUS Failure = static_cast<NTSTATUS>(0xc000000dL);

void check(char Call, bool Valid) {
  State->Calls += Call;
  State->ValidInputs &= Valid;
}

NTSTATUS APIENTRY openAdapter(const D3DKMT_OPENADAPTERFROMLUID *Args) {
  check('o', Args->AdapterLuid.LowPart == 77 &&
                 Args->AdapterLuid.HighPart == 0 && !Args->hAdapter);
  if (State->Scenario == Mode::OpenFailure)
    return Failure;
  if (State->Scenario == Mode::OpenStatus)
    return 1;
  // The SDK declares this output parameter "_Inout_ CONST".
  const_cast<D3DKMT_OPENADAPTERFROMLUID *>(Args)->hAdapter =
      State->Scenario == Mode::MissingAdapter ? 0 : 1;
  return 0;
}

NTSTATUS APIENTRY createDevice(D3DKMT_CREATEDEVICE *Args) {
  D3DKMT_CREATEDEVICEFLAGS Flags{};
  check('d', Args->hAdapter == 1 && !Args->hDevice &&
                 std::memcmp(&Args->Flags, &Flags, sizeof(Flags)) == 0);
  if (State->Scenario == Mode::DeviceFailure)
    return Failure;
  Args->hDevice = State->Scenario == Mode::MissingDevice ? 0 : 2;
  return 0;
}

NTSTATUS APIENTRY createAllocation(D3DKMT_CREATEALLOCATION *Args) {
  D3DKMT_CREATEALLOCATIONFLAGS Flags{};
  Flags.CreateResource = Flags.CreateShared = Flags.CrossAdapter = 1;
  Flags.NtSecuritySharing = Flags.ExistingSysMem = Flags.StandardAllocation = 1;
  const auto &Standard = *Args->pStandardAllocation;
  const auto &Info = *Args->pAllocationInfo2;
  check('a', Args->hDevice == 2 && !Args->hResource && !Args->hGlobalShare &&
                 !Args->pPrivateRuntimeData && !Args->PrivateRuntimeDataSize &&
                 !Args->PrivateDriverDataSize && Args->NumAllocations == 1 &&
                 !Args->hPrivateRuntimeResourceHandle &&
                 std::memcmp(&Args->Flags, &Flags, sizeof(Flags)) == 0 &&
                 Standard.Type == D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP &&
                 Standard.ExistingHeapData.Size == 65536 &&
                 !Standard.Flags.Value && Info.pSystemMem &&
                 !Info.hAllocation && !Info.pPrivateDriverData &&
                 !Info.PrivateDriverDataSize && !Info.Flags.Value &&
                 !Info.GpuVirtualAddress);
  if (State->Scenario == Mode::AllocationFailure)
    return Failure;
  Args->hResource = State->Scenario == Mode::MissingResource ? 0 : 3;
  Args->pAllocationInfo2->hAllocation =
      State->Scenario == Mode::MissingAllocation ? 0 : 4;
  return 0;
}

NTSTATUS APIENTRY createQueue(D3DKMT_CREATEPAGINGQUEUE *Args) {
  check('q', Args->hDevice == 2 && !Args->Priority &&
                 !Args->PhysicalAdapterIndex && !Args->hPagingQueue &&
                 !Args->hSyncObject && !Args->FenceValueCPUVirtualAddress);
  if (State->Scenario == Mode::QueueFailure)
    return Failure;
  Args->hPagingQueue = State->Scenario == Mode::MissingQueue ? 0 : 5;
  Args->hSyncObject = State->Scenario == Mode::MissingSync ? 0 : 6;
  // The probe must only check presence, never dereference this fixture pointer.
  Args->FenceValueCPUVirtualAddress =
      State->Scenario == Mode::MissingFence ? nullptr : State;
  return 0;
}

NTSTATUS APIENTRY makeResident(D3DDDI_MAKERESIDENT *Args) {
  check('r', Args->hPagingQueue == 5 && Args->NumAllocations == 1 &&
                 *Args->AllocationList == 4 && !Args->PriorityList &&
                 !Args->Flags.Value && !Args->PagingFenceValue);
  if (State->Scenario == Mode::ResidentFailure)
    return Failure;
  if (State->Scenario == Mode::ResidentStatus)
    return 1;
  Args->NumAllocations = State->Scenario == Mode::IncompleteResident ? 0 : 1;
  Args->PagingFenceValue = 7;
  switch (State->Scenario) {
  case Mode::Pending:
  case Mode::WaitFailure:
  case Mode::WaitStatus:
  case Mode::WaitTimeout:
  case Mode::WaitFailed:
    return 0x103;
  default:
    return 0;
  }
}

NTSTATUS APIENTRY
waitForFence(const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *Args) {
  check('w', Args->hDevice == 2 && Args->ObjectCount == 1 &&
                 *Args->ObjectHandleArray == 6 && *Args->FenceValueArray == 7 &&
                 Args->hAsyncEvent && !Args->Flags.Value);
  if (State->Scenario == Mode::WaitFailure)
    return Failure;
  return State->Scenario == Mode::WaitStatus ? 1 : 0;
}

DWORD WINAPI waitForEvent(HANDLE Event, DWORD Timeout) {
  check('t', Event && Timeout == 10000);
  if (State->Scenario == Mode::WaitTimeout)
    return WAIT_TIMEOUT;
  if (State->Scenario == Mode::WaitFailed) {
    SetLastError(ERROR_INVALID_HANDLE);
    return WAIT_FAILED;
  }
  return WAIT_OBJECT_0;
}

NTSTATUS APIENTRY evict(D3DKMT_EVICT *Args) {
  check('e', Args->hDevice == 2 && Args->NumAllocations == 1 &&
                 *Args->AllocationList == 4 && !Args->Flags.Value);
  return State->Scenario == Mode::EvictFailure ? Failure : 0;
}

NTSTATUS APIENTRY destroyAllocation(const D3DKMT_DESTROYALLOCATION2 *Args) {
  bool ByResource = State->Scenario != Mode::MissingResource;
  check('A',
        Args->hDevice == 2 && !Args->Flags.Value &&
            (ByResource ? Args->hResource == 3 && !Args->phAllocationList &&
                              !Args->AllocationCount
                        : !Args->hResource && Args->AllocationCount == 1 &&
                              *Args->phAllocationList == 4));
  if (State->Scenario == Mode::DestroyAllocationFailure)
    return Failure;
  return State->Scenario == Mode::DestroyAllocationStatus ? 1 : 0;
}

NTSTATUS APIENTRY destroyQueue(D3DDDI_DESTROYPAGINGQUEUE *Args) {
  check('Q', Args->hPagingQueue == 5);
  if (State->Scenario == Mode::DestroyQueueFailure)
    return Failure;
  return State->Scenario == Mode::DestroyQueueStatus ? 1 : 0;
}

NTSTATUS APIENTRY destroyDevice(const D3DKMT_DESTROYDEVICE *Args) {
  check('D', Args->hDevice == 2);
  if (State->Scenario == Mode::DestroyDeviceFailure)
    return Failure;
  return State->Scenario == Mode::DestroyDeviceStatus ? 1 : 0;
}

NTSTATUS APIENTRY closeAdapter(const D3DKMT_CLOSEADAPTER *Args) {
  check('C', Args->hAdapter == 1);
  if (State->Scenario == Mode::CloseAdapterFailure)
    return Failure;
  return State->Scenario == Mode::CloseAdapterStatus ? 1 : 0;
}
} // namespace

int wmain(int Argc, wchar_t **Argv) {
  size_t Index = 0;
  if (Argc == 2)
    for (; Index < ARRAYSIZE(Cases); ++Index)
      if (std::wcscmp(Argv[1], Cases[Index].Name) == 0)
        break;
  if (Argc != 2 || Index == ARRAYSIZE(Cases)) {
    std::fwprintf(stderr, L"Expected a KMT fixture scenario.\n");
    return 1;
  }
  TestState Test{static_cast<Mode>(Index), {}};
  State = &Test;
  AllocationProbe Probe;
  Probe.Api = {openAdapter,       createDevice, createAllocation, createQueue,
               makeResident,      waitForFence, waitForEvent,     evict,
               destroyAllocation, destroyQueue, destroyDevice,    closeAdapter};
  LUID Luid{77, 0};
  HRESULT Result = Probe.run(Luid);
  bool Closed = Probe.close();
  bool RetainMemory = Test.Scenario == Mode::DestroyDeviceFailure ||
                      Test.Scenario == Mode::DestroyDeviceStatus;
  bool Passed = Test.ValidInputs && Test.Calls == Cases[Index].Calls &&
                (SUCCEEDED(Result) == Cases[Index].RunSucceeded) &&
                (Closed == Cases[Index].Closed) &&
                (bool(Probe.Memory) == RetainMemory) && !Probe.Event;
  if (Test.Scenario == Mode::WaitFailed)
    Passed &= Result == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
  if (Test.Scenario == Mode::WaitTimeout)
    Passed &= Result == HRESULT_FROM_WIN32(ERROR_TIMEOUT);
  // All KMT objects were fake. Release backing retained after simulated
  // failure.
  if (Probe.Memory && !VirtualFree(Probe.Memory, 0, MEM_RELEASE))
    Passed = false;
  if (!Passed) {
    std::fwprintf(stderr,
                  L"Fixture %ls failed: calls=%hs expected=%hs "
                  L"inputs=%d run=0x%08lx closed=%d\n",
                  Cases[Index].Name, Test.Calls.c_str(), Cases[Index].Calls,
                  Test.ValidInputs, static_cast<unsigned long>(Result), Closed);
    return 1;
  }
  return 0;
}
