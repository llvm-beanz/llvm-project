//===- WarpHeap.cpp - Owned buffer heap DDIs
//-------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "WarpHeap.h"

#include "BootstrapAudit.h"
#include "WarpAllocationInfo.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace feme {
namespace probe {
namespace {
constexpr UINT32 BufferAlignment = 64 * 1024;
std::atomic<UINT> AppCreated{0}, AppDestroyed{0}, AppMaps{0}, AppUnmaps{0};
std::atomic<UINT> AppMakeResident{0}, AppEvict{0};
std::atomic<bool> HasRTPagingQueue{false};

struct HeapState {
  WarpDeviceState *Owner;
  HANDLE RTResource;
  SIZE_T ByteSize;
  D3D12DDI_CPU_PAGE_PROPERTY CPUPageProperty;
  D3DKMT_HANDLE Allocation = 0;
  void *Memory = nullptr;
  bool Locked = false;
  SRWLOCK MapLock = SRWLOCK_INIT;
  SIZE_T MapCount = 0;
  SIZE_T ResourceCount = 0;
};

struct ResourceState {
  WarpDeviceState *Owner;
  HeapState *Heap;
  D3D12DDIARG_CREATERESOURCE_0003 Desc;
  ResourceState *Next = nullptr;
  UINT64 Offset = 0;
  bool PlacementBacking = false;
};

WarpDeviceState *deviceState(D3D12DDI_HDEVICE Device) {
  return static_cast<WarpDeviceState *>(Device.pDrvPrivate);
}

void reportError(WarpDeviceState *State, HRESULT Result) {
  std::fprintf(stderr, "FeMe WARP heap DDI error: 0x%08lx\n",
               static_cast<unsigned long>(Result));
  if (State && State->RTDevice && State->UMCallbacks &&
      State->UMCallbacks->pfnSetErrorCb) {
    D3D12DDI_HRTDEVICE RTDevice{};
    RTDevice.handle = State->RTDevice;
    State->UMCallbacks->pfnSetErrorCb(RTDevice, Result);
    return;
  }
  OutputDebugStringA(
      "FeMe WARP heap DDI failed without a runtime error callback.\n");
}

HRESULT validateResource(const D3D12DDIARG_CREATERESOURCE_0003 *Args,
                         UINT64 &Size) {
  if (!Args)
    return E_INVALIDARG;
  if (Args->ResourceType != D3D12DDI_RT_BUFFER ||
      Args->Layout != D3D12DDI_TL_ROW_MAJOR || Args->pRowMajorLayout)
    return E_NOTIMPL;
  if (!Args->ReuseBufferGPUVA.BaseAddress.UMD.hResource.pDrvPrivate &&
      Args->ReuseBufferGPUVA.BaseAddress.UMD.Offset)
    return E_INVALIDARG;
  constexpr UINT AllowedFlags =
      D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE |
      D3D12DDI_RESOURCE_FLAG_0003_SIMULTANEOUS_ACCESS |
      D3D12DDI_RESOURCE_FLAG_0022_UNORDERED_ACCESS;
  if (static_cast<UINT>(Args->Flags) & ~AllowedFlags)
    return E_NOTIMPL;
  if (!Args->Width || Args->Height != 1 || Args->DepthOrArraySize != 1 ||
      Args->MipLevels != 1 || Args->Format != DXGI_FORMAT_UNKNOWN ||
      Args->SampleDesc.Count != 1 || Args->SampleDesc.Quality ||
      Args->Width > std::numeric_limits<SIZE_T>::max() - BufferAlignment + 1)
    return E_INVALIDARG;
  if (Args->Width > std::numeric_limits<UINT32>::max())
    return E_NOTIMPL;
  Size = (Args->Width + BufferAlignment - 1) & ~UINT64(BufferAlignment - 1);
  return S_OK;
}

HRESULT validateHeap(const D3D12DDIARG_CREATEHEAP_0001 *Args) {
  if (!Args)
    return E_NOTIMPL;
  constexpr UINT AllowedFlags = D3D12DDI_HEAP_FLAG_BUFFERS;
  if (Args->MemoryPool != D3D12DDI_MEMORY_POOL_L0 ||
      (static_cast<UINT>(Args->Flags) & ~AllowedFlags))
    return E_NOTIMPL;
  if (!Args->ByteSize || Args->ByteSize > std::numeric_limits<SIZE_T>::max() ||
      Args->ByteSize % BufferAlignment ||
      (Args->Alignment && Args->Alignment != BufferAlignment) ||
      Args->CreationNodeMask != 1 || Args->VisibleNodeMask != 1 ||
      (Args->CPUPageProperty != D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE &&
       Args->CPUPageProperty != D3D12DDI_CPU_PAGE_PROPERTY_WRITE_COMBINE &&
       Args->CPUPageProperty != D3D12DDI_CPU_PAGE_PROPERTY_WRITE_BACK))
    return E_INVALIDARG;
  if (Args->ByteSize > std::numeric_limits<UINT32>::max())
    return E_NOTIMPL;
  return S_OK;
}

HRESULT validateCreation(const D3D12DDIARG_CREATEHEAP_0001 *Heap,
                         const D3D12DDIARG_CREATERESOURCE_0003 *Resource,
                         D3D12DDI_HPROTECTEDRESOURCESESSION_0030 Protected) {
  if (Protected.pDrvPrivate)
    return E_NOTIMPL;
  if (!Heap) {
    if (!Resource ||
        !Resource->ReuseBufferGPUVA.BaseAddress.UMD.hResource.pDrvPrivate)
      return E_NOTIMPL;
    UINT64 Size = 0;
    return validateResource(Resource, Size);
  }
  if (Resource &&
      Resource->ReuseBufferGPUVA.BaseAddress.UMD.hResource.pDrvPrivate)
    return E_INVALIDARG;
  HRESULT Result = validateHeap(Heap);
  if (FAILED(Result))
    return Result;
  if (Resource) {
    UINT64 Size = 0;
    Result = validateResource(Resource, Size);
    if (FAILED(Result))
      return Result;
    if (Size > Heap->ByteSize)
      return E_INVALIDARG;
  }
  return S_OK;
}

// The caller holds the resource registry lock throughout lookup and use.
HRESULT validatePlacement(WarpDeviceState *Owner,
                          const D3D12DDIARG_CREATERESOURCE_0003 *Args,
                          ResourceState *&Backing, UINT64 &Offset) {
  Backing = nullptr;
  if (!Owner || !Args)
    return E_INVALIDARG;
  const auto &Placement = Args->ReuseBufferGPUVA.BaseAddress.UMD;
  for (auto *Buffer = static_cast<ResourceState *>(Owner->ResourceList); Buffer;
       Buffer = Buffer->Next)
    if (Buffer == Placement.hResource.pDrvPrivate) {
      Backing = Buffer;
      break;
    }
  UINT64 Size = 0;
  HRESULT Result = validateResource(Args, Size);
  if (FAILED(Result))
    return Result;
  if (!Backing || !Backing->Heap->Locked || !Backing->Heap->Memory ||
      Placement.Offset % BufferAlignment ||
      Placement.Offset > Backing->Desc.Width ||
      Size > Backing->Desc.Width - Placement.Offset)
    return E_INVALIDARG;
  Offset = Backing->Offset + Placement.Offset;
  return S_OK;
}

D3D12DDI_HEAP_AND_RESOURCE_SIZES APIENTRY calcPrivateSizes(
    D3D12DDI_HDEVICE Device, const D3D12DDIARG_CREATEHEAP_0001 *Heap,
    const D3D12DDIARG_CREATERESOURCE_0003 *Resource,
    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 Protected) {
  HRESULT Result = validateCreation(Heap, Resource, Protected);
  if (SUCCEEDED(Result) && !Heap) {
    auto *Owner = deviceState(Device);
    if (!Owner)
      Result = E_INVALIDARG;
    else {
      AcquireSRWLockShared(&Owner->ResourceLock);
      ResourceState *Backing = nullptr;
      UINT64 Offset = 0;
      Result = validatePlacement(Owner, Resource, Backing, Offset);
      ReleaseSRWLockShared(&Owner->ResourceLock);
    }
  }
  if (FAILED(Result)) {
    if (Heap)
      std::fprintf(stderr,
                   "FeMe WARP heap size: bytes=%llu align=%llu flags=0x%x "
                   "pool=%u cpu=%u nodes=%u/%u\n",
                   Heap->ByteSize, Heap->Alignment, Heap->Flags,
                   Heap->MemoryPool, Heap->CPUPageProperty,
                   Heap->CreationNodeMask, Heap->VisibleNodeMask);
    reportError(deviceState(Device), Result);
    return {};
  }
  return {Heap ? sizeof(HeapState) : 0, Resource ? sizeof(ResourceState) : 0};
}

HRESULT releaseAllocation(HeapState *State) {
  auto *Owner = State->Owner;
  HRESULT Result = S_OK;
  if (State->Locked) {
    D3DDDICB_UNLOCK Unlock{};
    Unlock.NumAllocations = 1;
    Unlock.phAllocations = &State->Allocation;
    Result = Owner->KTCallbacks->pfnUnlockCb(Owner->RTDevice, &Unlock);
    if (Result == S_OK)
      State->Locked = false;
    else if (SUCCEEDED(Result))
      Result = E_UNEXPECTED;
  }
  if (State->Allocation) {
    D3D12DDICB_DEALLOCATE_0022 Deallocate{};
    if (State->RTResource)
      Deallocate.hResource = State->RTResource;
    else {
      Deallocate.NumAllocations = 1;
      Deallocate.HandleList = &State->Allocation;
    }
    Deallocate.Flags = D3D12DDI_DEALLOCATE_FLAGS_0022_SYNCHRONOUS_DESTROY;
    D3D12DDI_HRTDEVICE Device{};
    Device.handle = Owner->RTDevice;
    HRESULT Released = Owner->UMCallbacks->pfnDeallocateCb(Device, &Deallocate);
    if (Released == S_OK) {
      State->Allocation = 0;
      State->Memory = nullptr;
      State->Locked = false;
    }
    if (Result == S_OK && Released != S_OK)
      Result = FAILED(Released) ? Released : E_UNEXPECTED;
  }
  return Result;
}

HRESULT allocateHeap(HeapState *State,
                     const D3D12DDIARG_CREATERESOURCE_0003 *Resource) {
  WarpKmdAllocationInfo PrivateInfo{};
  PrivateInfo.ResourceDimension = D3D10DDIRESOURCE_BUFFER;
  PrivateInfo.Width = Resource ? static_cast<UINT32>(Resource->Width)
                               : static_cast<UINT32>(State->ByteSize);
  PrivateInfo.Height = 1;
  PrivateInfo.Depth = 1;
  PrivateInfo.Pitch = Resource ? PrivateInfo.Width : 0;
  PrivateInfo.TotalSize = static_cast<UINT32>(State->ByteSize);
  PrivateInfo.ArraySize = 1;
  PrivateInfo.MipLevels = 1;
  PrivateInfo.SampleDesc.Count = 1;
  if (Resource)
    PrivateInfo.BindFlags =
        D3D10_DDI_BIND_SHADER_RESOURCE | D3D11_DDI_BIND_UNORDERED_ACCESS;
  // This DDI does not expose the application's sharing choice.
  PrivateInfo.Flags = WarpAllocationFlagInAperture | WarpAllocationFlagShared;
  if (!Resource)
    PrivateInfo.Flags |= WarpAllocationFlagTilePool;
  if (State->CPUPageProperty != D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
    PrivateInfo.Flags |= WarpAllocationFlagCpuVisible;
  if (State->CPUPageProperty == D3D12DDI_CPU_PAGE_PROPERTY_WRITE_COMBINE)
    PrivateInfo.Flags |= WarpAllocationFlagWriteCombine;
  D3D12DDI_ALLOCATION_INFO_0022 Info{};
  Info.pPrivateDriverData = &PrivateInfo;
  Info.PrivateDriverDataSize = sizeof(PrivateInfo);
  D3D12DDICB_ALLOCATE_0022 Allocate{};
  Allocate.hResource = State->RTResource;
  Allocate.NumAllocations = 1;
  Allocate.pAllocationInfo = &Info;
  D3D12DDI_HRTDEVICE Device{};
  Device.handle = State->Owner->RTDevice;
  HRESULT Result = State->Owner->UMCallbacks->pfnAllocateCb(Device, &Allocate);
  if (Result != S_OK)
    return FAILED(Result) ? Result : E_UNEXPECTED;
  State->Allocation = Info.hAllocation;
  if (!State->Allocation)
    return E_UNEXPECTED;
  D3DDDICB_LOCK Lock{};
  Lock.hAllocation = State->Allocation;
  Lock.Flags.IgnoreSync = 1;
  Lock.Flags.DonotWait = 1;
  Result = State->Owner->KTCallbacks->pfnLockCb(State->Owner->RTDevice, &Lock);
  if (Result != S_OK)
    return FAILED(Result) ? Result : E_UNEXPECTED;
  State->Locked = true;
  State->Memory = Lock.pData;
  if (!State->Memory)
    return E_UNEXPECTED;
  std::memset(State->Memory, 0, State->ByteSize);
  return S_OK;
}

HRESULT APIENTRY createHeapAndResource(
    D3D12DDI_HDEVICE Device, const D3D12DDIARG_CREATEHEAP_0001 *HeapArgs,
    D3D12DDI_HHEAP Heap, D3D12DDI_HRTRESOURCE RTResource,
    const D3D12DDIARG_CREATERESOURCE_0003 *ResourceArgs,
    const D3D12DDI_CLEAR_VALUES *ClearValues,
    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 Protected,
    D3D12DDI_HRESOURCE Resource) {
  auto *Owner = deviceState(Device);
  if (!Owner || (HeapArgs && !Heap.pDrvPrivate) ||
      (ResourceArgs && !Resource.pDrvPrivate) ||
      (!ResourceArgs && Resource.pDrvPrivate) || ClearValues)
    return E_INVALIDARG;
  HRESULT Result = validateCreation(HeapArgs, ResourceArgs, Protected);
  if (FAILED(Result))
    return Result;
  if (!HeapArgs) {
    AcquireSRWLockExclusive(&Owner->ResourceLock);
    ResourceState *Backing = nullptr;
    UINT64 Offset = 0;
    Result = validatePlacement(Owner, ResourceArgs, Backing, Offset);
    if (SUCCEEDED(Result) &&
        (Resource.pDrvPrivate == ResourceArgs->ReuseBufferGPUVA.BaseAddress.UMD
                                     .hResource.pDrvPrivate ||
         (Heap.pDrvPrivate && Heap.pDrvPrivate != Backing->Heap)))
      Result = E_INVALIDARG;
    if (SUCCEEDED(Result))
      for (auto *Buffer = static_cast<ResourceState *>(Owner->ResourceList);
           Buffer; Buffer = Buffer->Next)
        if (Buffer == Resource.pDrvPrivate) {
          Result = E_INVALIDARG;
          break;
        }
    if (SUCCEEDED(Result)) {
      auto *Buffer = ::new (Resource.pDrvPrivate)
          ResourceState{Owner, Backing->Heap, *ResourceArgs};
      Buffer->Offset = Offset;
      Buffer->Next = static_cast<ResourceState *>(Owner->ResourceList);
      Owner->ResourceList = Buffer;
      Backing->PlacementBacking = true;
      ++Buffer->Heap->ResourceCount;
      ++Owner->LiveResources;
    }
    ReleaseSRWLockExclusive(&Owner->ResourceLock);
    return Result;
  }
  if (!Owner->RTDevice || !Owner->KTCallbacks ||
      !Owner->KTCallbacks->pfnLockCb || !Owner->KTCallbacks->pfnUnlockCb ||
      !Owner->UMCallbacks || !Owner->UMCallbacks->pfnAllocateCb ||
      !Owner->UMCallbacks->pfnDeallocateCb)
    return E_NOINTERFACE;
  auto *State = ::new (Heap.pDrvPrivate) HeapState{
      Owner, RTResource.handle, static_cast<SIZE_T>(HeapArgs->ByteSize),
      HeapArgs->CPUPageProperty};
  Result = allocateHeap(State, ResourceArgs);
  if (FAILED(Result)) {
    HRESULT Cleanup = releaseAllocation(State);
    Owner->HeapCleanupResult = Cleanup;
    if (FAILED(Cleanup))
      reportError(Owner, Cleanup);
    State->~HeapState();
    return Result;
  }
  ++Owner->LiveHeaps;
  if (State->RTResource)
    ++AppCreated;
  if (ResourceArgs) {
    auto *Buffer =
        ::new (Resource.pDrvPrivate) ResourceState{Owner, State, *ResourceArgs};
    AcquireSRWLockExclusive(&Owner->ResourceLock);
    Buffer->Next = static_cast<ResourceState *>(Owner->ResourceList);
    Owner->ResourceList = Buffer;
    ReleaseSRWLockExclusive(&Owner->ResourceLock);
    ++State->ResourceCount;
    ++Owner->LiveResources;
  }
  return S_OK;
}

void APIENTRY destroyHeapAndResource(D3D12DDI_HDEVICE Device,
                                     D3D12DDI_HHEAP Heap,
                                     D3D12DDI_HRESOURCE Resource) {
  auto *Owner = deviceState(Device);
  auto *State = static_cast<HeapState *>(Heap.pDrvPrivate);
  auto *Buffer = static_cast<ResourceState *>(Resource.pDrvPrivate);
  if (!Owner || (!State && !Buffer) || (State && State->Owner != Owner) ||
      (Buffer &&
       (Buffer->Owner != Owner || (State && Buffer->Heap != State))) ||
      (State && State->ResourceCount != (Buffer ? 1u : 0u))) {
    reportError(Owner, E_INVALIDARG);
    return;
  }
  if (Buffer) {
    AcquireSRWLockExclusive(&Owner->ResourceLock);
    auto *First = static_cast<ResourceState *>(Owner->ResourceList);
    if (First == Buffer)
      Owner->ResourceList = Buffer->Next;
    else {
      while (First && First->Next != Buffer)
        First = First->Next;
      if (!First) {
        ReleaseSRWLockExclusive(&Owner->ResourceLock);
        reportError(Owner, E_INVALIDARG);
        return;
      }
      First->Next = Buffer->Next;
    }
    ReleaseSRWLockExclusive(&Owner->ResourceLock);
    --Buffer->Heap->ResourceCount;
    --Owner->LiveResources;
    Buffer->~ResourceState();
  }
  if (State) {
    HRESULT Result = releaseAllocation(State);
    Owner->HeapCleanupResult = Result;
    if (FAILED(Result))
      reportError(Owner, Result);
    if (State->Allocation)
      return;
    --Owner->LiveHeaps;
    if (State->RTResource)
      ++AppDestroyed;
    State->~HeapState();
  }
}

HRESULT APIENTRY mapHeap(D3D12DDI_HDEVICE Device, D3D12DDI_HHEAP Heap,
                         void **Data) {
  auto *State = static_cast<HeapState *>(Heap.pDrvPrivate);
  if (!State || !Data || State->Owner != deviceState(Device) ||
      State->CPUPageProperty == D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
    return E_INVALIDARG;
  AcquireSRWLockExclusive(&State->MapLock);
  if (State->MapCount == std::numeric_limits<SIZE_T>::max()) {
    ReleaseSRWLockExclusive(&State->MapLock);
    return E_UNEXPECTED;
  }
  ++State->MapCount;
  if (State->RTResource)
    ++AppMaps;
  *Data = State->Memory;
  ReleaseSRWLockExclusive(&State->MapLock);
  return S_OK;
}

void APIENTRY unmapHeap(D3D12DDI_HDEVICE Device, D3D12DDI_HHEAP Heap) {
  auto *State = static_cast<HeapState *>(Heap.pDrvPrivate);
  if (!State || State->Owner != deviceState(Device)) {
    reportError(deviceState(Device), E_INVALIDARG);
    return;
  }
  AcquireSRWLockExclusive(&State->MapLock);
  if (!State->MapCount) {
    ReleaseSRWLockExclusive(&State->MapLock);
    reportError(deviceState(Device), E_INVALIDARG);
    return;
  }
  --State->MapCount;
  if (State->RTResource)
    ++AppUnmaps;
  ReleaseSRWLockExclusive(&State->MapLock);
}

void APIENTRY checkAllocationInfo(
    D3D12DDI_HDEVICE Device, const D3D12DDIARG_CREATERESOURCE_0003 *Args,
    D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS Flags, UINT32 Alignment,
    UINT VisibleNodeMask, D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 *Info) {
  UINT64 Size = 0;
  HRESULT Result = validateResource(Args, Size);
  if (SUCCEEDED(Result) && (!Info || VisibleNodeMask != 1 ||
                            (Alignment && Alignment != BufferAlignment)))
    Result = E_INVALIDARG;
  constexpr UINT AllowedFlags =
      D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_SHADER_RESOURCE |
      D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_UNORDERED_ACCESS |
      D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_DETERMINISTIC;
  if (SUCCEEDED(Result) && (static_cast<UINT>(Flags) & ~AllowedFlags))
    Result = E_NOTIMPL;
  if (FAILED(Result)) {
    if (Args)
      std::fprintf(
          stderr,
          "FeMe WARP allocation info: type=%u width=%llu layout=%u "
          "flags=0x%x optimizations=0x%x align=%u nodes=%u "
          "rowMajor=%u reuse=%u\n",
          Args->ResourceType, Args->Width, Args->Layout, Args->Flags, Flags,
          Alignment, VisibleNodeMask, Args->pRowMajorLayout != nullptr,
          Args->ReuseBufferGPUVA.BaseAddress.UMD.hResource.pDrvPrivate !=
              nullptr);
    reportError(deviceState(Device), Result);
    return;
  }
  *Info = {};
  Info->ResourceDataSize = Size;
  Info->ResourceDataAlignment = BufferAlignment;
  Info->AdditionalDataHeaderAlignment = 1;
  Info->AdditionalDataAlignment = 1;
  Info->Layout = D3D12DDI_TL_ROW_MAJOR;
}

void APIENTRY checkExistingAllocationInfo(
    D3D12DDI_HDEVICE Device, D3D12DDI_HRESOURCE Resource,
    D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 *Info) {
  auto *Buffer = static_cast<ResourceState *>(Resource.pDrvPrivate);
  if (!Buffer || Buffer->Owner != deviceState(Device)) {
    reportError(deviceState(Device), E_INVALIDARG);
    return;
  }
  checkAllocationInfo(Device, &Buffer->Desc,
                      D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_NONE, 0, 1, Info);
}

D3D12DDI_GPU_VIRTUAL_ADDRESS APIENTRY
checkVirtualAddress(D3D12DDI_HDEVICE Device, D3D12DDI_HRESOURCE Resource) {
  auto *Buffer = static_cast<ResourceState *>(Resource.pDrvPrivate);
  if (!Buffer || Buffer->Owner != deviceState(Device)) {
    reportError(deviceState(Device), E_INVALIDARG);
    return 0;
  }
  return reinterpret_cast<ULONG_PTR>(Buffer->Heap->Memory) + Buffer->Offset;
}

D3DKMT_HANDLE APIENTRY checkAllocationHandle(D3D12DDI_HDEVICE Device,
                                             D3D10DDI_HRESOURCE Resource) {
  auto *Buffer = static_cast<ResourceState *>(Resource.pDrvPrivate);
  if (!Buffer || Buffer->Owner != deviceState(Device)) {
    reportError(deviceState(Device), E_INVALIDARG);
    return 0;
  }
  return Buffer->Heap->Allocation;
}

void APIENTRY checkSubresourceInfo(D3D12DDI_HDEVICE Device,
                                   D3D12DDI_HRESOURCE Resource,
                                   UINT Subresource,
                                   D3D12DDI_SUBRESOURCE_INFO *Info) {
  auto *Buffer = static_cast<ResourceState *>(Resource.pDrvPrivate);
  if (!Buffer || Buffer->Owner != deviceState(Device) || Subresource || !Info) {
    reportError(deviceState(Device), E_INVALIDARG);
    return;
  }
  *Info = {};
  Info->RowStride = Buffer->Desc.Width;
  Info->DepthStride = Buffer->Desc.Width;
}

HRESULT validateObjects(WarpDeviceState *Owner, UINT Count,
                        const D3D12DDI_HANDLE_AND_TYPE *Objects) {
  if (!Owner || (Count && !Objects))
    return E_INVALIDARG;
  for (UINT I = 0; I < Count; ++I) {
    if (Objects[I].Type == D3D12DDI_HT_DESCRIPTOR_HEAP) {
      bool Found = false;
      AcquireSRWLockShared(&Owner->DescriptorLock);
      for (auto *Entry = Owner->DescriptorList; Entry; Entry = Entry->Next)
        Found |= Entry->Handle == Objects[I].Handle;
      ReleaseSRWLockShared(&Owner->DescriptorLock);
      if (!Found)
        return E_INVALIDARG;
      continue;
    }
    if (Objects[I].Type != D3D12DDI_HT_HEAP) {
      std::fprintf(stderr, "FeMe residency unsupported object type=%u\n",
                   Objects[I].Type);
      return E_NOTIMPL;
    }
    auto *Heap = static_cast<HeapState *>(Objects[I].Handle);
    if (!Heap || Heap->Owner != Owner)
      return E_INVALIDARG;
  }
  return S_OK;
}

HRESULT APIENTRY makeResident(D3D12DDI_HDEVICE Device,
                              D3D12DDIARG_MAKERESIDENT_0001 *Args) {
  if (!Args || Args->NumAdapters != 1 || !Args->pRTPagingQueue ||
      !Args->pPagingFenceValue)
    return E_INVALIDARG;
  auto *Owner = deviceState(Device);
  HRESULT Result = validateObjects(Owner, Args->NumObjects, Args->pObjects);
  if (FAILED(Result))
    return Result;
  UINT HeapCount = 0;
  for (UINT I = 0; I < Args->NumObjects; ++I)
    HeapCount += Args->pObjects[I].Type == D3D12DDI_HT_HEAP;
  // Descriptor heaps are live CPU metadata, not KMD allocations.
  if (!HeapCount) {
    Args->WaitMask = 0;
    Args->pPagingFenceValue[0] = 0;
    return S_OK;
  }
  if (!Args->pRTPagingQueue[0].handle)
    return E_INVALIDARG;
  if (!Owner->UMCallbacks || !Owner->UMCallbacks->pfnMakeResidentCb)
    return E_NOINTERFACE;
  std::unique_ptr<D3DKMT_HANDLE[]> Handles(new (std::nothrow)
                                               D3DKMT_HANDLE[HeapCount]);
  if (!Handles)
    return E_OUTOFMEMORY;
  UINT Index = 0;
  for (UINT I = 0; I < Args->NumObjects; ++I)
    if (Args->pObjects[I].Type == D3D12DDI_HT_HEAP)
      Handles[Index++] =
          static_cast<HeapState *>(Args->pObjects[I].Handle)->Allocation;
  D3DDDI_MAKERESIDENT Resident{};
  Resident.NumAllocations = HeapCount;
  Resident.AllocationList = Handles.get();
  Resident.Flags = Args->Flags;
  D3D12DDI_HRTDEVICE RTDevice{};
  RTDevice.handle = Owner->RTDevice;
  Result = Owner->UMCallbacks->pfnMakeResidentCb(
      RTDevice, Args->pRTPagingQueue[0], &Resident);
  if (Result == E_PENDING && !Resident.PagingFenceValue)
    return E_UNEXPECTED;
  if (Result == S_OK || Result == E_PENDING) {
    for (UINT I = 0; I < Args->NumObjects; ++I)
      if (Args->pObjects[I].Type == D3D12DDI_HT_HEAP &&
          static_cast<HeapState *>(Args->pObjects[I].Handle)->RTResource) {
        ++AppMakeResident;
        HasRTPagingQueue = true;
      }
    Args->WaitMask = Result == E_PENDING ? 1 : 0;
    Args->pPagingFenceValue[0] = Resident.PagingFenceValue;
    return Result;
  }
  return FAILED(Result) ? Result : E_UNEXPECTED;
}

HRESULT APIENTRY evict(D3D12DDI_HDEVICE Device, const D3D12DDIARG_EVICT *Args) {
  if (!Args)
    return E_INVALIDARG;
  auto *Owner = deviceState(Device);
  HRESULT Result = validateObjects(Owner, Args->NumObjects, Args->pObjects);
  if (FAILED(Result))
    return Result;
  UINT HeapCount = 0;
  for (UINT I = 0; I < Args->NumObjects; ++I)
    HeapCount += Args->pObjects[I].Type == D3D12DDI_HT_HEAP;
  if (!HeapCount)
    return S_OK;
  if (!Owner->UMCallbacks || !Owner->UMCallbacks->pfnEvictCb)
    return E_NOINTERFACE;
  std::unique_ptr<D3DKMT_HANDLE[]> Handles(new (std::nothrow)
                                               D3DKMT_HANDLE[HeapCount]);
  if (!Handles)
    return E_OUTOFMEMORY;
  UINT Index = 0;
  for (UINT I = 0; I < Args->NumObjects; ++I)
    if (Args->pObjects[I].Type == D3D12DDI_HT_HEAP)
      Handles[Index++] =
          static_cast<HeapState *>(Args->pObjects[I].Handle)->Allocation;
  D3DDDICB_EVICT Evict{};
  Evict.NumAllocations = HeapCount;
  Evict.AllocationList = Handles.get();
  Evict.Flags = Args->Flags;
  D3D12DDI_HRTDEVICE RTDevice{};
  RTDevice.handle = Owner->RTDevice;
  Result = Owner->UMCallbacks->pfnEvictCb(RTDevice, &Evict);
  if (Result == S_OK)
    for (UINT I = 0; I < Args->NumObjects; ++I)
      if (Args->pObjects[I].Type == D3D12DDI_HT_HEAP &&
          static_cast<HeapState *>(Args->pObjects[I].Handle)->RTResource)
        ++AppEvict;
  return Result == S_OK || FAILED(Result) ? Result : E_UNEXPECTED;
}
} // namespace

WarpBufferAccess::WarpBufferAccess(WarpDeviceState *Owner) : Owner(Owner) {
  if (Owner)
    AcquireSRWLockShared(&Owner->ResourceLock);
}

WarpBufferAccess::~WarpBufferAccess() {
  if (Owner)
    ReleaseSRWLockShared(&Owner->ResourceLock);
}

HRESULT WarpBufferAccess::range(D3D12DDI_HRESOURCE Resource, UINT64 Offset,
                                UINT64 Size, void *&Data,
                                bool UnorderedAccess) {
  Data = nullptr;
  if (!Owner || !Resource.pDrvPrivate)
    return E_INVALIDARG;
  for (auto *Buffer = static_cast<ResourceState *>(Owner->ResourceList); Buffer;
       Buffer = Buffer->Next) {
    if (Buffer != Resource.pDrvPrivate)
      continue;
    if (!Buffer->Heap->Locked || !Buffer->Heap->Memory ||
        Offset > Buffer->Desc.Width || Size > Buffer->Desc.Width - Offset ||
        (UnorderedAccess &&
         !(Buffer->Desc.Flags & D3D12DDI_RESOURCE_FLAG_0022_UNORDERED_ACCESS)))
      return E_INVALIDARG;
    Data = static_cast<unsigned char *>(Buffer->Heap->Memory) + Buffer->Offset +
           Offset;
    return S_OK;
  }
  return E_INVALIDARG;
}

HRESULT WarpBufferAccess::address(UINT64 Address, void *&Data, UINT64 &Size,
                                  bool UnorderedAccess) {
  Data = nullptr;
  Size = 0;
  if (!Owner || !Address)
    return E_INVALIDARG;
  for (auto *Buffer = static_cast<ResourceState *>(Owner->ResourceList); Buffer;
       Buffer = Buffer->Next) {
    UINT64 Base =
        reinterpret_cast<UINT64>(Buffer->Heap->Memory) + Buffer->Offset;
    if (Buffer->PlacementBacking || !Buffer->Heap->Locked || !Base ||
        Address < Base || Address - Base >= Buffer->Desc.Width)
      continue;
    if (UnorderedAccess &&
        !(Buffer->Desc.Flags & D3D12DDI_RESOURCE_FLAG_0022_UNORDERED_ACCESS))
      continue;
    // A root GPUVA has no resource identity. Bound overlapping aliases by the
    // largest live logical extent, never by the implicit backing allocation.
    UINT64 Extent = Buffer->Desc.Width - (Address - Base);
    if (Extent > Size) {
      Data = reinterpret_cast<void *>(Address);
      Size = Extent;
    }
  }
  return Data ? S_OK : E_INVALIDARG;
}

HRESULT getWarpBufferRange(WarpDeviceState *Owner, D3D12DDI_HRESOURCE Resource,
                           UINT64 Offset, UINT64 Size, void *&Data,
                           bool UnorderedAccess) {
  WarpBufferAccess Access(Owner);
  return Access.range(Resource, Offset, Size, Data, UnorderedAccess);
}

HRESULT getWarpBufferAddress(WarpDeviceState *Owner, UINT64 Address,
                             void *&Data, UINT64 &Size, bool UnorderedAccess) {
  WarpBufferAccess Access(Owner);
  return Access.address(Address, Data, Size, UnorderedAccess);
}

void populateWarpHeapDDIs(D3D12DDI_DEVICE_FUNCS_CORE_0072 &Table) {
  Table.pfnCalcPrivateHeapAndResourceSizes = calcPrivateSizes;
  Table.pfnCreateHeapAndResource = createHeapAndResource;
  Table.pfnDestroyHeapAndResource = destroyHeapAndResource;
  Table.pfnMapHeap = mapHeap;
  Table.pfnUnmapHeap = unmapHeap;
  Table.pfnCheckResourceAllocationInfo = checkAllocationInfo;
  Table.pfnCheckExistingResourceAllocationInfo = checkExistingAllocationInfo;
  Table.pfnCheckResourceVirtualAddress = checkVirtualAddress;
  Table.pfnCheckResourceAllocationHandle = checkAllocationHandle;
  Table.pfnCheckSubresourceInfo = checkSubresourceInfo;
  Table.pfnMakeResident = makeResident;
  Table.pfnEvict = evict;
}
} // namespace probe
} // namespace feme

extern "C" __declspec(dllexport) void APIENTRY
FeMeWarpGetApplicationHeapAudit(feme::probe::WarpApplicationHeapAudit *Audit) {
  if (!Audit) {
    OutputDebugStringA("FeMe WARP heap audit requires an output pointer.\n");
    return;
  }
  *Audit = {
      feme::probe::AppCreated.load(),      feme::probe::AppDestroyed.load(),
      feme::probe::AppMaps.load(),         feme::probe::AppUnmaps.load(),
      feme::probe::AppMakeResident.load(), feme::probe::AppEvict.load(),
      feme::probe::HasRTPagingQueue.load()};
}
