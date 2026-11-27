//===- WarpHeapTests.cpp - Committed buffer DDI lifetimes
//------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "WarpAllocationInfo.h"
#include "WarpHeap.h"

#include <cstdio>
#include <cwchar>
#include <limits>
#include <thread>

namespace {
struct ErrorState {
  std::atomic<UINT> Calls{0};
  std::atomic<HRESULT> Result{S_OK};
  bool ValidCallbacks = true;
  UINT Allocations = 0;
  UINT Locks = 0;
  UINT Unlocks = 0;
  UINT Deallocations = 0;
  UINT ResidentCalls = 0;
  UINT EvictCalls = 0;
  void *Memory = nullptr;
  HANDLE Resource = nullptr;
  HRESULT AllocateResult = S_OK;
  HRESULT LockResult = S_OK;
  HRESULT ResidentResult = S_OK;
  HRESULT EvictResult = S_OK;
  UINT32 ResidentFlags = 0;
};

void APIENTRY setError(D3D12DDI_HRTDEVICE Device, HRESULT Result) {
  auto *State = static_cast<ErrorState *>(Device.handle);
  ++State->Calls;
  State->Result = Result;
}

HRESULT APIENTRY allocate(D3D12DDI_HRTDEVICE Device,
                          D3D12DDICB_ALLOCATE_0022 *Args) {
  auto *State = static_cast<ErrorState *>(Device.handle);
  ++State->Allocations;
  if (!Args || Args->NumAllocations != 1 || !Args->pAllocationInfo ||
      Args->pAllocationInfo->PrivateDriverDataSize !=
          sizeof(feme::probe::WarpKmdAllocationInfo)) {
    State->ValidCallbacks = false;
    return E_INVALIDARG;
  }
  auto *Info = static_cast<feme::probe::WarpKmdAllocationInfo *>(
      Args->pAllocationInfo->pPrivateDriverData);
  if (!Info || Info->ResourceDimension != D3D10DDIRESOURCE_BUFFER ||
      Info->TotalSize != 65536 || Info->Height != 1 || Info->Depth != 1 ||
      Info->ArraySize != 1 || Info->MipLevels != 1 ||
      !(Info->Flags & feme::probe::WarpAllocationFlagShared) ||
      !(Info->Flags & feme::probe::WarpAllocationFlagInAperture)) {
    State->ValidCallbacks = false;
    return E_INVALIDARG;
  }
  if (State->AllocateResult != S_OK)
    return State->AllocateResult;
  DWORD Protection = PAGE_READWRITE;
  if (Info->Flags & feme::probe::WarpAllocationFlagWriteCombine)
    Protection |= PAGE_WRITECOMBINE;
  State->Memory = VirtualAlloc(nullptr, Info->TotalSize,
                               MEM_COMMIT | MEM_RESERVE, Protection);
  if (!State->Memory)
    return HRESULT_FROM_WIN32(GetLastError());
  State->Resource = Args->hResource;
  Args->pAllocationInfo->hAllocation = 42;
  return S_OK;
}

HRESULT APIENTRY lock(HANDLE Device, D3DDDICB_LOCK *Args) {
  auto *State = static_cast<ErrorState *>(Device);
  ++State->Locks;
  if (!Args || Args->hAllocation != 42 || !State->Memory) {
    State->ValidCallbacks = false;
    return E_INVALIDARG;
  }
  if (State->LockResult != S_OK)
    return State->LockResult;
  Args->pData = State->Memory;
  return S_OK;
}

HRESULT APIENTRY unlock(HANDLE Device, const D3DDDICB_UNLOCK *Args) {
  auto *State = static_cast<ErrorState *>(Device);
  ++State->Unlocks;
  if (!Args || Args->NumAllocations != 1 || !Args->phAllocations ||
      Args->phAllocations[0] != 42) {
    State->ValidCallbacks = false;
    return E_INVALIDARG;
  }
  return S_OK;
}

HRESULT APIENTRY deallocate(D3D12DDI_HRTDEVICE Device,
                            const D3D12DDICB_DEALLOCATE_0022 *Args) {
  auto *State = static_cast<ErrorState *>(Device.handle);
  ++State->Deallocations;
  if (!Args ||
      Args->Flags != D3D12DDI_DEALLOCATE_FLAGS_0022_SYNCHRONOUS_DESTROY ||
      (State->Resource ? Args->hResource != State->Resource ||
                             Args->NumAllocations || Args->HandleList
                       : Args->hResource || Args->NumAllocations != 1 ||
                             !Args->HandleList || Args->HandleList[0] != 42)) {
    State->ValidCallbacks = false;
    return E_INVALIDARG;
  }
  if (!VirtualFree(State->Memory, 0, MEM_RELEASE))
    return HRESULT_FROM_WIN32(GetLastError());
  State->Memory = nullptr;
  State->Resource = nullptr;
  return S_OK;
}

HRESULT APIENTRY makeResident(D3D12DDI_HRTDEVICE Device,
                              D3D12DDI_HRTPAGINGQUEUE Queue,
                              D3DDDI_MAKERESIDENT *Args) {
  auto *State = static_cast<ErrorState *>(Device.handle);
  ++State->ResidentCalls;
  if (!Args || Queue.handle != State || Args->hPagingQueue ||
      Args->NumAllocations != 1 || !Args->AllocationList ||
      Args->AllocationList[0] != 42) {
    State->ValidCallbacks = false;
    return E_INVALIDARG;
  }
  State->ResidentFlags = Args->Flags.Value;
  Args->PagingFenceValue = State->ResidentResult == E_PENDING ? 77 : 0;
  return State->ResidentResult;
}

HRESULT APIENTRY evict(D3D12DDI_HRTDEVICE Device, const D3DDDICB_EVICT *Args) {
  auto *State = static_cast<ErrorState *>(Device.handle);
  ++State->EvictCalls;
  if (!Args || Args->NumAllocations != 1 || !Args->AllocationList ||
      Args->AllocationList[0] != 42) {
    State->ValidCallbacks = false;
    return E_INVALIDARG;
  }
  return State->EvictResult;
}

struct Fixture {
  ErrorState Errors;
  D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 Callbacks{};
  D3DDDI_DEVICECALLBACKS KTCallbacks{};
  feme::probe::WarpDeviceState State;
  D3D12DDI_HDEVICE Device{};
  D3D12DDI_DEVICE_FUNCS_CORE_0072 Table{};
  D3D12DDIARG_CREATEHEAP_0001 HeapArgs{};
  D3D12DDIARG_CREATERESOURCE_0003 ResourceArgs{};
  D3D12DDI_HHEAP Heap{};
  D3D12DDI_HRESOURCE Resource{};
  D3D12DDI_HRTRESOURCE RuntimeResource{};
  bool Created = false;

  Fixture() {
    Callbacks.pfnSetErrorCb = setError;
    Callbacks.pfnAllocateCb = allocate;
    Callbacks.pfnDeallocateCb = deallocate;
    Callbacks.pfnMakeResidentCb = makeResident;
    Callbacks.pfnEvictCb = evict;
    KTCallbacks.pfnLockCb = lock;
    KTCallbacks.pfnUnlockCb = unlock;
    State.UMCallbacks = &Callbacks;
    State.KTCallbacks = &KTCallbacks;
    State.RTDevice = &Errors;
    Device.pDrvPrivate = &State;
    feme::probe::populateWarpHeapDDIs(Table);
    HeapArgs.ByteSize = 65536;
    HeapArgs.Alignment = 65536;
    HeapArgs.MemoryPool = D3D12DDI_MEMORY_POOL_L0;
    HeapArgs.CPUPageProperty = D3D12DDI_CPU_PAGE_PROPERTY_WRITE_BACK;
    HeapArgs.Flags = D3D12DDI_HEAP_FLAG_BUFFERS;
    HeapArgs.CreationNodeMask = 1;
    HeapArgs.VisibleNodeMask = 1;
    ResourceArgs.ResourceType = D3D12DDI_RT_BUFFER;
    ResourceArgs.Width = 4097;
    ResourceArgs.Height = 1;
    ResourceArgs.DepthOrArraySize = 1;
    ResourceArgs.MipLevels = 1;
    ResourceArgs.SampleDesc.Count = 1;
    ResourceArgs.Layout = D3D12DDI_TL_ROW_MAJOR;
    D3D12DDI_HEAP_AND_RESOURCE_SIZES Sizes =
        Table.pfnCalcPrivateHeapAndResourceSizes(Device, &HeapArgs,
                                                 &ResourceArgs, {});
    Heap.pDrvPrivate =
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Sizes.Heap);
    Resource.pDrvPrivate =
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Sizes.Resource);
  }

  ~Fixture() {
    if (Created)
      destroy();
    if (Resource.pDrvPrivate)
      HeapFree(GetProcessHeap(), 0, Resource.pDrvPrivate);
    if (Heap.pDrvPrivate)
      HeapFree(GetProcessHeap(), 0, Heap.pDrvPrivate);
  }

  HRESULT create() {
    HRESULT Result =
        Table.pfnCreateHeapAndResource(Device, &HeapArgs, Heap, RuntimeResource,
                                       &ResourceArgs, nullptr, {}, Resource);
    Created = Result == S_OK;
    return Result;
  }

  void destroy() {
    Table.pfnDestroyHeapAndResource(Device, Heap, Resource);
    Created = false;
  }
};

bool check(bool Condition, const char *Message) {
  if (!Condition)
    std::fprintf(stderr, "WARP heap test failed: %s\n", Message);
  return Condition;
}

bool lifecycle() {
  Fixture F;
  bool Valid = check(F.create() == S_OK, "create committed buffer");
  if (!F.Created)
    return false;
  D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 Allocation{};
  F.Table.pfnCheckResourceAllocationInfo(
      F.Device, &F.ResourceArgs, D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_NONE, 0, 1,
      &Allocation);
  Valid &= check(Allocation.ResourceDataSize == 65536 &&
                     Allocation.ResourceDataAlignment == 65536 &&
                     Allocation.Layout == D3D12DDI_TL_ROW_MAJOR,
                 "buffer allocation size and alignment");
  const auto OriginalAllocation = Allocation;
  F.Table.pfnCheckResourceAllocationInfo(
      F.Device, &F.ResourceArgs,
      D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_SHADER_RESOURCE |
          D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_UNORDERED_ACCESS |
          D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_DETERMINISTIC,
      65536, 1, &Allocation);
  Valid &=
      check(F.Errors.Calls == 0 && std::memcmp(&OriginalAllocation, &Allocation,
                                               sizeof(Allocation)) == 0,
            "deterministic runtime buffer query preserves footprint");
  D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 Existing{};
  F.ResourceArgs.Width = 65537;
  F.Table.pfnCheckExistingResourceAllocationInfo(F.Device, F.Resource,
                                                 &Existing);
  Valid &= check(
      Existing.ResourceDataSize == Allocation.ResourceDataSize &&
          Existing.ResourceDataAlignment == Allocation.ResourceDataAlignment &&
          Existing.Layout == Allocation.Layout,
      "existing allocation query uses retained resource description");
  F.ResourceArgs.Width = 4097;
  void *Data = nullptr;
  void *Again = nullptr;
  Valid &= check(F.Table.pfnMapHeap(F.Device, F.Heap, &Data) == S_OK && Data,
                 "map committed memory");
  if (!Data)
    return false;
  auto *Bytes = static_cast<unsigned char *>(Data);
  Valid &= check(Bytes[0] == 0 && Bytes[4096] == 0, "zero initialized buffer");
  Bytes[0] = 0x42;
  Bytes[4096] = 0x93;
  Valid &= check(F.Table.pfnMapHeap(F.Device, F.Heap, &Again) == S_OK &&
                     Data == Again,
                 "nested mapping retains address");
  bool ThreadValid[2] = {true, true};
  auto MapRepeatedly = [&](UINT Index) {
    for (UINT I = 0; I < 100; ++I) {
      void *Mapped = nullptr;
      if (F.Table.pfnMapHeap(F.Device, F.Heap, &Mapped) != S_OK ||
          Mapped != Data) {
        ThreadValid[Index] = false;
        return;
      }
      static_cast<unsigned char *>(Mapped)[8 + Index] =
          static_cast<unsigned char>(Index + 1);
      F.Table.pfnUnmapHeap(F.Device, F.Heap);
    }
  };
  std::thread First(MapRepeatedly, 0);
  std::thread Second(MapRepeatedly, 1);
  First.join();
  Second.join();
  Valid &=
      check(ThreadValid[0] && ThreadValid[1] && Bytes[8] == 1 && Bytes[9] == 2,
            "parallel maps retain balanced ownership");
  UINT64 Address = F.Table.pfnCheckResourceVirtualAddress(F.Device, F.Resource);
  UINT64 Extent = 0;
  void *Borrowed = nullptr;
  Valid &= check(feme::probe::getWarpBufferAddress(&F.State, Address + 4096,
                                                   Borrowed, Extent) == S_OK &&
                     Borrowed == Bytes + 4096 && Extent == 1,
                 "root buffer lookup respects the logical resource extent");
  feme::probe::WarpDeviceState Other;
  Valid &= check(feme::probe::getWarpBufferAddress(
                     &F.State, Address, Borrowed, Extent, true) == E_INVALIDARG,
                 "root UAV lookup rejects a buffer without UAV permission");
  Valid &= check(feme::probe::getWarpBufferAddress(&Other, Address, Borrowed,
                                                   Extent) == E_INVALIDARG &&
                     !Borrowed && !Extent,
                 "root buffer lookup rejects another device's VA");
  Valid &=
      check(feme::probe::getWarpBufferAddress(
                &F.State, Address + 4097, Borrowed, Extent) == E_INVALIDARG &&
                !Borrowed && !Extent,
            "root buffer lookup rejects the backing allocation padding");
  Valid &= check(Address == reinterpret_cast<ULONG_PTR>(Data),
                 "GPU VA preserves the actual KMD lock address");
  D3D12DDI_SUBRESOURCE_INFO Info{};
  F.Table.pfnCheckSubresourceInfo(F.Device, F.Resource, 0, &Info);
  Valid &= check(Info.Offset == 0 && Info.RowStride == 4097 &&
                     Info.DepthStride == 4097,
                 "logical buffer extent excludes allocation padding");
  F.Table.pfnUnmapHeap(F.Device, F.Heap);
  Valid &= check(Bytes[0] == 0x42 && Bytes[4096] == 0x93,
                 "unmap preserves data in remaining mapping");
  F.destroy();
  Valid &= check(feme::probe::getWarpBufferAddress(&F.State, Address, Borrowed,
                                                   Extent) == E_INVALIDARG &&
                     !Borrowed && !Extent,
                 "root buffer lookup rejects destroyed resource storage");
  MEMORY_BASIC_INFORMATION Memory{};
  Valid &= check(VirtualQuery(Data, &Memory, sizeof(Memory)) &&
                     Memory.State == MEM_FREE && F.State.LiveHeaps == 0 &&
                     F.State.LiveResources == 0 && F.Errors.Calls == 0 &&
                     F.Errors.ValidCallbacks && F.Errors.Allocations == 1 &&
                     F.Errors.Locks == 1 && F.Errors.Unlocks == 1 &&
                     F.Errors.Deallocations == 1,
                 "destroy persistently mapped buffer releases memory");
  return Valid;
}

bool validation() {
  Fixture F;
  F.ResourceArgs.Width = std::numeric_limits<UINT64>::max();
  bool Valid = check(F.create() == E_INVALIDARG, "reject allocation overflow");
  F.ResourceArgs.Width = 65537;
  Valid &= check(F.create() == E_INVALIDARG, "reject undersized heap");
  F.ResourceArgs.Width = 4097;
  F.ResourceArgs.ResourceType = D3D12DDI_RT_TEXTURE2D;
  Valid &= check(F.create() == E_NOTIMPL, "reject unimplemented texture");
  F.ResourceArgs.ResourceType = D3D12DDI_RT_BUFFER;
  F.HeapArgs.CPUPageProperty = D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
  Valid &= check(F.create() == S_OK, "create nonmappable buffer");
  if (!F.Created)
    return false;
  void *Data = &F;
  Valid &= check(F.Table.pfnMapHeap(F.Device, F.Heap, &Data) == E_INVALIDARG &&
                     Data == &F,
                 "failed map preserves output");
  F.Table.pfnUnmapHeap(F.Device, F.Heap);
  Valid &= check(F.Errors.Calls == 1 && F.Errors.Result == E_INVALIDARG,
                 "unbalanced unmap reports runtime error");
  feme::probe::WarpDeviceState OtherState;
  D3D12DDI_HDEVICE Other{};
  Other.pDrvPrivate = &OtherState;
  Valid &= check(F.Table.pfnMapHeap(Other, F.Heap, &Data) == E_INVALIDARG,
                 "reject cross-device heap access");
  D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 Info{};
  Info.ResourceDataSize = 123;
  F.ResourceArgs.Width = 0;
  F.Table.pfnCheckResourceAllocationInfo(
      F.Device, &F.ResourceArgs, D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_NONE, 0, 1,
      &Info);
  Valid &= check(F.Errors.Calls == 2 && F.Errors.Result == E_INVALIDARG &&
                     Info.ResourceDataSize == 123,
                 "void metadata failure reports error without fabricated size");
  F.destroy();
  F.ResourceArgs.Width = 4097;
  F.HeapArgs.CPUPageProperty = D3D12DDI_CPU_PAGE_PROPERTY_WRITE_COMBINE;
  F.RuntimeResource.handle = &F;
  Valid &= check(F.create() == S_OK, "create write-combined upload heap");
  if (!F.Created)
    return false;
  Data = nullptr;
  Valid &= check(F.Table.pfnMapHeap(F.Device, F.Heap, &Data) == S_OK && Data,
                 "map upload heap");
  if (!Data)
    return false;
  MEMORY_BASIC_INFORMATION Memory{};
  Valid &= check(VirtualQuery(Data, &Memory, sizeof(Memory)) &&
                     (Memory.Protect & PAGE_WRITECOMBINE),
                 "upload allocation has requested write-combined protection");
  F.destroy();
  HRESULT HeapOnly = F.Table.pfnCreateHeapAndResource(
      F.Device, &F.HeapArgs, F.Heap, {}, nullptr, nullptr, {}, {});
  Valid &= check(HeapOnly == S_OK && F.State.LiveHeaps == 1 &&
                     F.State.LiveResources == 0,
                 "heap-only allocation has no resource ownership");
  if (HeapOnly == S_OK)
    F.Table.pfnDestroyHeapAndResource(F.Device, F.Heap, {});
  Valid &= check(F.State.LiveHeaps == 0 && F.State.LiveResources == 0,
                 "heap-only destruction releases ownership");
  UINT Released = F.Errors.Deallocations;
  F.Errors.AllocateResult = E_OUTOFMEMORY;
  Valid &= check(F.create() == E_OUTOFMEMORY && !F.Errors.Memory &&
                     F.Errors.Deallocations == Released,
                 "failed allocation acquires no ownership");
  F.Errors.AllocateResult = S_OK;
  F.Errors.LockResult = E_ACCESSDENIED;
  Valid &= check(F.create() == E_ACCESSDENIED && !F.Errors.Memory &&
                     F.Errors.Deallocations == Released + 1 &&
                     F.State.LiveHeaps == 0 && F.State.LiveResources == 0 &&
                     F.Errors.ValidCallbacks,
                 "failed lock rolls back associated KMD allocation");
  F.Errors.LockResult = S_OK;
  F.Callbacks.pfnAllocateCb = nullptr;
  Valid &= check(F.create() == E_NOINTERFACE,
                 "missing allocation callback is not a successful heap");
  F.Callbacks.pfnAllocateCb = allocate;
  return Valid;
}

bool residency() {
  Fixture F;
  bool Valid = check(F.create() == S_OK, "create residency fixture");
  if (!F.Created)
    return false;
  void *Data = nullptr;
  if (!check(F.Table.pfnMapHeap(F.Device, F.Heap, &Data) == S_OK && Data,
             "map residency fixture"))
    return false;
  auto *Bytes = static_cast<unsigned char *>(Data);
  Bytes[4096] = 0xa5;
  D3D12DDI_HANDLE_AND_TYPE Object{};
  Object.Type = D3D12DDI_HT_HEAP;
  Object.Handle = F.Heap.pDrvPrivate;
  D3D12DDIARG_EVICT Evict{};
  Evict.NumObjects = 1;
  Evict.pObjects = &Object;
  Valid &= check(F.Table.pfnEvict(F.Device, &Evict) == S_OK,
                 "evict private CPU heap");
  D3D12DDI_HRTPAGINGQUEUE Queue{};
  Queue.handle = &F.Errors;
  UINT64 Fence = 99;
  D3D12DDIARG_MAKERESIDENT_0001 Resident{};
  Resident.NumAdapters = 1;
  Resident.pRTPagingQueue = &Queue;
  Resident.NumObjects = 1;
  Resident.pObjects = &Object;
  Resident.pPagingFenceValue = &Fence;
  Resident.WaitMask = 1;
  Valid &=
      check(F.Table.pfnMakeResident(F.Device, &Resident) == S_OK &&
                Resident.WaitMask == 0 && Fence == 0 && Bytes[4096] == 0xa5 &&
                F.Table.pfnCheckResourceVirtualAddress(F.Device, F.Resource) ==
                    reinterpret_cast<ULONG_PTR>(Data),
            "synchronous residency retains contents and VA");
  F.Errors.ResidentResult = E_PENDING;
  Resident.Flags.Value = 1;
  Valid &= check(F.Table.pfnMakeResident(F.Device, &Resident) == E_PENDING &&
                     Resident.WaitMask == 1 && Fence == 77 &&
                     F.Errors.ResidentFlags == 1,
                 "pending KMD residency preserves flags and paging fence");
  F.Errors.ResidentResult = E_ACCESSDENIED;
  Fence = 99;
  Resident.WaitMask = 8;
  Valid &=
      check(F.Table.pfnMakeResident(F.Device, &Resident) == E_ACCESSDENIED &&
                Fence == 99 && Resident.WaitMask == 8,
            "failed residency does not publish a successful fence");
  Object.Type = D3D12DDI_HT_COMMAND_QUEUE;
  Valid &= check(F.Table.pfnEvict(F.Device, &Evict) == E_NOTIMPL &&
                     F.Table.pfnMakeResident(F.Device, &Resident) == E_NOTIMPL,
                 "reject nonheap residency objects");
  Valid &= check(F.Errors.ValidCallbacks && F.Errors.ResidentCalls == 3 &&
                     F.Errors.EvictCalls == 1,
                 "only validated heap handles reach KMD paging callbacks");
  return Valid;
}
} // namespace

int wmain(int Argc, wchar_t **Argv) {
  if (Argc != 2)
    return 1;
  bool Passed = false;
  if (std::wcscmp(Argv[1], L"lifecycle") == 0)
    Passed = lifecycle();
  else if (std::wcscmp(Argv[1], L"validation") == 0)
    Passed = validation();
  else if (std::wcscmp(Argv[1], L"residency") == 0)
    Passed = residency();
  else {
    std::fprintf(stderr, "Unknown WARP heap fixture scenario.\n");
    return 1;
  }
  return Passed ? 0 : 1;
}
