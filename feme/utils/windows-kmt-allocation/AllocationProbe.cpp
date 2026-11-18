//===- AllocationProbe.cpp - Public WARP KMT allocation control
//------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <windows.h>
#include <winternl.h>

#include <d3dkmthk.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>
#include <cwchar>

using Microsoft::WRL::ComPtr;

namespace {
struct KmtApi {
  decltype(&D3DKMTOpenAdapterFromLuid) OpenAdapter = D3DKMTOpenAdapterFromLuid;
  decltype(&D3DKMTCreateDevice) CreateDevice = D3DKMTCreateDevice;
  decltype(&D3DKMTCreateAllocation2) CreateAllocation = D3DKMTCreateAllocation2;
  decltype(&D3DKMTCreatePagingQueue) CreateQueue = D3DKMTCreatePagingQueue;
  decltype(&D3DKMTMakeResident) MakeResident = D3DKMTMakeResident;
  decltype(&D3DKMTWaitForSynchronizationObjectFromCpu) WaitForFence =
      D3DKMTWaitForSynchronizationObjectFromCpu;
  decltype(&WaitForSingleObject) WaitForEvent = WaitForSingleObject;
  decltype(&D3DKMTEvict) Evict = D3DKMTEvict;
  decltype(&D3DKMTDestroyAllocation2) DestroyAllocation =
      D3DKMTDestroyAllocation2;
  decltype(&D3DKMTDestroyPagingQueue) DestroyQueue = D3DKMTDestroyPagingQueue;
  decltype(&D3DKMTDestroyDevice) DestroyDevice = D3DKMTDestroyDevice;
  decltype(&D3DKMTCloseAdapter) CloseAdapter = D3DKMTCloseAdapter;
};

HRESULT report(const wchar_t *Operation, HRESULT Result) {
  std::wprintf(L"%ls: HRESULT=0x%08lx\n", Operation,
               static_cast<unsigned long>(Result));
  return Result;
}

constexpr NTSTATUS StatusPending = 0x103;

HRESULT reportStatus(const wchar_t *Operation, NTSTATUS Status,
                     bool AllowPending = false) {
  std::wprintf(L"%ls: NTSTATUS=0x%08lx\n", Operation,
               static_cast<unsigned long>(Status));
  if (Status == 0 || (AllowPending && Status == StatusPending))
    return S_OK;
  return Status < 0 ? HRESULT_FROM_NT(Status) : E_UNEXPECTED;
}

struct AllocationProbe {
  KmtApi Api;
  D3DKMT_HANDLE Adapter = 0;
  D3DKMT_HANDLE Device = 0;
  D3DKMT_HANDLE Resource = 0;
  D3DKMT_HANDLE Allocation = 0;
  D3DKMT_HANDLE PagingQueue = 0;
  HANDLE Event = nullptr;
  void *Memory = nullptr;

  HRESULT run(LUID AdapterLuid) {
    D3DKMT_OPENADAPTERFROMLUID Open{};
    Open.AdapterLuid = AdapterLuid;
    HRESULT Result =
        reportStatus(L"D3DKMTOpenAdapterFromLuid", Api.OpenAdapter(&Open));
    if (FAILED(Result))
      return Result;
    Adapter = Open.hAdapter;
    if (!Adapter)
      return report(L"Missing KMT adapter handle", E_FAIL);

    D3DKMT_CREATEDEVICE CreateDevice{};
    CreateDevice.hAdapter = Adapter;
    Result =
        reportStatus(L"D3DKMTCreateDevice", Api.CreateDevice(&CreateDevice));
    if (FAILED(Result))
      return Result;
    Device = CreateDevice.hDevice;
    if (!Device)
      return report(L"Missing KMT device handle", E_FAIL);

    constexpr SIZE_T HeapSize = 65536;
    Memory = VirtualAlloc(nullptr, HeapSize, MEM_RESERVE | MEM_COMMIT,
                          PAGE_READWRITE);
    if (!Memory)
      return report(L"VirtualAlloc", HRESULT_FROM_WIN32(GetLastError()));
    D3DKMT_CREATESTANDARDALLOCATION Standard{};
    Standard.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
    Standard.ExistingHeapData.Size = HeapSize;
    D3DDDI_ALLOCATIONINFO2 Info{};
    Info.pSystemMem = Memory;
    D3DKMT_CREATEALLOCATION Create{};
    Create.hDevice = Device;
    Create.pStandardAllocation = &Standard;
    Create.NumAllocations = 1;
    Create.pAllocationInfo2 = &Info;
    Create.Flags.CreateResource = 1;
    Create.Flags.CreateShared = 1;
    Create.Flags.NtSecuritySharing = 1;
    Create.Flags.CrossAdapter = 1;
    Create.Flags.ExistingSysMem = 1;
    Create.Flags.StandardAllocation = 1;
    std::wprintf(L"Existing-heap control: Size=%zu NumAllocations=1; "
                 L"no private driver data, NoKmdAccess=0\n",
                 HeapSize);
    Result =
        reportStatus(L"D3DKMTCreateAllocation2", Api.CreateAllocation(&Create));
    if (FAILED(Result))
      return Result;
    Resource = Create.hResource;
    Allocation = Info.hAllocation;
    if (!Resource || !Allocation)
      return report(L"Missing standard resource/allocation handle", E_FAIL);
    return residency();
  }

  HRESULT residency() {
    D3DKMT_CREATEPAGINGQUEUE Queue{};
    Queue.hDevice = Device;
    Queue.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    HRESULT Result =
        reportStatus(L"D3DKMTCreatePagingQueue", Api.CreateQueue(&Queue));
    if (FAILED(Result))
      return Result;
    PagingQueue = Queue.hPagingQueue;
    if (!PagingQueue || !Queue.hSyncObject ||
        !Queue.FenceValueCPUVirtualAddress)
      return report(L"Missing paging queue/sync/fence output", E_FAIL);

    Event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!Event)
      return report(L"CreateEvent", HRESULT_FROM_WIN32(GetLastError()));
    D3DDDI_MAKERESIDENT Resident{};
    Resident.hPagingQueue = PagingQueue;
    Resident.NumAllocations = 1;
    Resident.AllocationList = &Allocation;
    NTSTATUS Status = Api.MakeResident(&Resident);
    std::wprintf(L"Residency: NumAllocations=%u PagingFenceValue=%llu "
                 L"NumBytesToTrim=%llu\n",
                 Resident.NumAllocations, Resident.PagingFenceValue,
                 Resident.NumBytesToTrim);
    Result = reportStatus(L"D3DKMTMakeResident", Status, true);
    if (FAILED(Result))
      return Result;
    if (Resident.NumAllocations != 1)
      return report(L"Incomplete make-resident result", E_FAIL);
    if (Status == StatusPending) {
      D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait{};
      Wait.hDevice = Device;
      Wait.ObjectCount = 1;
      Wait.ObjectHandleArray = &Queue.hSyncObject;
      Wait.FenceValueArray = &Resident.PagingFenceValue;
      Wait.hAsyncEvent = Event;
      Result = reportStatus(L"D3DKMTWaitForSynchronizationObjectFromCpu",
                            Api.WaitForFence(&Wait));
      if (FAILED(Result))
        return Result;
      constexpr DWORD TimeoutMs = 10000;
      DWORD WaitResult = Api.WaitForEvent(Event, TimeoutMs);
      DWORD WaitError =
          WaitResult == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
      std::wprintf(L"Paging fence event wait: result=0x%08lx timeout=%lu ms\n",
                   WaitResult, TimeoutMs);
      if (WaitResult != WAIT_OBJECT_0)
        return report(L"Paging fence event wait",
                      HRESULT_FROM_WIN32(WaitError));
    }

    D3DKMT_EVICT Evict{};
    Evict.hDevice = Device;
    Evict.NumAllocations = 1;
    Evict.AllocationList = &Allocation;
    return reportStatus(L"D3DKMTEvict", Api.Evict(&Evict));
  }

  bool close() {
    bool Released = true;
    if (Resource || Allocation) {
      D3DKMT_DESTROYALLOCATION2 Destroy{};
      Destroy.hDevice = Device;
      Destroy.hResource = Resource;
      if (!Resource) {
        Destroy.phAllocationList = &Allocation;
        Destroy.AllocationCount = 1;
      }
      if (SUCCEEDED(reportStatus(L"D3DKMTDestroyAllocation2",
                                 Api.DestroyAllocation(&Destroy)))) {
        Resource = Allocation = 0;
      } else {
        Released = false;
      }
    }
    if (PagingQueue) {
      D3DDDI_DESTROYPAGINGQUEUE Destroy{};
      Destroy.hPagingQueue = PagingQueue;
      if (SUCCEEDED(reportStatus(L"D3DKMTDestroyPagingQueue",
                                 Api.DestroyQueue(&Destroy))))
        PagingQueue = 0;
      else
        Released = false;
    }
    if (Device) {
      D3DKMT_DESTROYDEVICE Destroy{};
      Destroy.hDevice = Device;
      if (SUCCEEDED(reportStatus(L"D3DKMTDestroyDevice",
                                 Api.DestroyDevice(&Destroy)))) {
        Device = Resource = Allocation = PagingQueue = 0;
      } else {
        Released = false;
      }
    }
    if (Adapter) {
      D3DKMT_CLOSEADAPTER Close{};
      Close.hAdapter = Adapter;
      if (SUCCEEDED(
              reportStatus(L"D3DKMTCloseAdapter", Api.CloseAdapter(&Close))))
        Adapter = 0;
      else
        Released = false;
    }
    if (Event) {
      if (CloseHandle(Event))
        Event = nullptr;
      else {
        report(L"CloseHandle(event)", HRESULT_FROM_WIN32(GetLastError()));
        Released = false;
      }
    }
    if (Memory) {
      if (Device) {
        report(L"Retaining backing memory until process exit: "
               L"KMT device teardown failed",
               E_FAIL);
        Released = false;
      } else if (VirtualFree(Memory, 0, MEM_RELEASE)) {
        Memory = nullptr;
      } else {
        report(L"VirtualFree", HRESULT_FROM_WIN32(GetLastError()));
        Released = false;
      }
    }
    return Released;
  }
};
} // namespace

#ifndef FEME_KMT_ALLOCATION_TEST
int wmain() {
  ComPtr<IDXGIFactory4> Factory;
  HRESULT Result =
      report(L"CreateDXGIFactory1", CreateDXGIFactory1(IID_PPV_ARGS(&Factory)));
  if (FAILED(Result))
    return 1;
  ComPtr<IDXGIAdapter1> Adapter;
  Result = report(L"EnumWarpAdapter",
                  Factory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter)));
  if (FAILED(Result))
    return 1;
  DXGI_ADAPTER_DESC1 Desc{};
  Result = report(L"WARP GetDesc1", Adapter->GetDesc1(&Desc));
  if (FAILED(Result))
    return 1;
  std::wprintf(L"Adapter: %ls; vendor=%04x device=%04x flags=%x "
               L"LUID=%08lx:%08lx\n",
               Desc.Description, Desc.VendorId, Desc.DeviceId, Desc.Flags,
               static_cast<unsigned long>(Desc.AdapterLuid.HighPart),
               Desc.AdapterLuid.LowPart);
  if (Desc.VendorId != 0x1414 || Desc.DeviceId != 0x008c ||
      !(Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
    report(L"Microsoft WARP identity mismatch", E_FAIL);
    return 1;
  }
  AllocationProbe Probe;
  Result = Probe.run(Desc.AdapterLuid);
  bool Released = Probe.close();
  std::wprintf(L"Direct KMT control only: not an owned UMD, runtime callback "
               L"allocation or D3D12 device.\n");
  if (FAILED(Result) || !Released)
    return 1;
  std::wprintf(L"Public KMT allocation/residency lifecycle passed.\n");
  return 0;
}
#endif
