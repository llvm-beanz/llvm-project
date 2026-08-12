//===- FeMeRuntimeCPU.c - Resource access scalar helper source -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is `libFeMeRuntimeCPU`'s shader-side half (see "Runtime Support
// Library" in feme/docs/FeMeCPUDesign.md): the scalar implementations of the
// canonical `feme.cpu.resource.*` calls `feme::cpu::ResourceCalls`/
// `feme::cpu::ResourceLoweringPass` create. A later phase (widening, the
// entry wrapper) links only the referenced definitions from this file's
// compiled bitcode into a compiled shader module and internalizes them, so
// the ordinary optimizer can inline and constant-fold through them before
// codegen -- see "Descriptor formats"/"Bounds checking" in
// feme/docs/FeMeCPUDesign.md for why that has to happen before optimization
// rather than at link time against a separately-compiled library.
//
// Every externally visible helper below is given its canonical
// `feme.cpu.resource.*`/`feme.cpu.rt.*` name via a GNU `asm` label, since
// that dotted name is not a valid C identifier; `feme::cpu::ResourceCalls`
// and this file's unit tests refer to those names literally. Each is also
// marked `always_inline`: they are compiled into standalone bitcode
// definitions here (so a linker can pull them in by name), but every call
// site that matters is expected to inline and specialize them away, exactly
// as the previous hand-written IR did with the `alwaysinline` attribute.
//
// The descriptor layout (`FemeRTDescriptor`) mirrors
// `feme::cpu::FemeDescriptor` in feme/include/feme/Target/CPU/RuntimeABI.h
// field for field; keep the two in sync. `ResourceKind`/`ResourceFormat`/
// `FemeDescriptorFlagBits`'s numeric values are hardcoded as integer
// literals below (this file cannot include RuntimeABI.h: that header is
// C++, and this translation unit is compiled freestanding, as plain C, so
// it carries no dependency on FeMe's own C++ library code), and are
// likewise called out at each use so a future enumerator renumbering in
// RuntimeABI.h is easy to find and fix here.
//
// Scope (roadmap milestone 3): this file covers the typed-buffer view
// `<4 x float>` (covering the `R32G32B32A32_FLOAT` identity format and the
// packed `R8G8B8A8_UNORM` format, to establish the format-switch pattern
// concretely) and the raw/structured-buffer views `i32`/`float`. Every
// other canonical call `feme::cpu::ResourceCalls` can create (other typed
// views, the remaining formats "Descriptor formats" lists, atomics) is a
// mechanical repeat of the same pattern once a call site actually needs it
// -- "Additional formats extend one helper implementation rather than every
// access site" -- and is added on demand rather than spelled out
// exhaustively up front.
//
//===----------------------------------------------------------------------===//

#include <stdint.h>

// A 4-lane `float` vector, compiled to LLVM IR's `<4 x float>`.
typedef float FemeRTv4f32 __attribute__((vector_size(16)));

// The same vector type, but with its assumed pointer alignment relaxed to
// 4 bytes (its element alignment) rather than its natural 16-byte vector
// alignment: a typed-buffer element is only ever guaranteed to be aligned
// to its component size, not to the whole vector's width.
typedef float FemeRTv4f32Unaligned __attribute__((vector_size(16), aligned(4)));

// Mirrors `feme::cpu::FemeDescriptor` (RuntimeABI.h): { Data, SizeInBytes,
// Stride, Format, Kind, Flags, Counter }.
typedef struct {
  void *Data;
  uint64_t SizeInBytes;
  uint32_t Stride;
  uint32_t Format;
  uint32_t Kind;
  uint32_t Flags;
  void *Counter;
} FemeRTDescriptor;

// An internal, already-bounds-checked view of one descriptor's fields --
// returned instead of the raw struct so every caller shares one
// heap-index-check implementation. `IndexOK` records whether the heap
// index itself was in range (the "always on" check from "Bounds checking")
// but is otherwise redundant with `Kind`: an out-of-range index yields
// `Kind == ResourceKind::None`, which never matches any real access's
// expected kind, so no caller below needs to consult it separately.
typedef struct {
  void *Data;
  uint64_t SizeInBytes;
  uint32_t Stride;
  uint32_t Format;
  uint32_t Kind;
  uint32_t Flags;
  _Bool IndexOK;
} FemeRTLoaded;

//--- Shared helpers ----------------------------------------------------------

// Loads descriptor `Index` of `Heap`/`HeapCount`, or an all-zero
// (`Kind = None`) `FemeRTLoaded` with `IndexOK = false` if
// `Index >= HeapCount` -- "An index >= HeapCount yields the all-zero
// descriptor. Never skippable" (see "Per-descriptor control"). This never
// reads through `Heap` at all when the index is out of range, since
// `HeapCount == 0` means `Heap` may not point at anything.
__attribute__((always_inline)) static FemeRTLoaded
femeRTLoadDescriptor(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                     uint32_t Index) {
  if (Index >= HeapCount) {
    // ResourceKind::None == 0.
    FemeRTLoaded OutOfBounds = {0, 0, 0, 0, 0, 0, 0};
    return OutOfBounds;
  }
  const FemeRTDescriptor *Desc = &Heap[Index];
  FemeRTLoaded Loaded = {Desc->Data,
                         Desc->SizeInBytes,
                         Desc->Stride,
                         Desc->Format,
                         Desc->Kind,
                         Desc->Flags,
                         1};
  return Loaded;
}

// Whether an access of `AccessSize` bytes at `Offset` through a descriptor
// of kind `Kind`/size `Size`/flags `Flags` is allowed: the descriptor's
// kind must match `ExpectedKind` (which is never `ResourceKind::None == 0`,
// so this also implements "ignored when `Kind == None`" -- an all-zero
// descriptor never matches), and the access must either fit
// (`Offset + AccessSize <= SizeInBytes`, see "Bounds checking") or the
// descriptor must carry `FEME_DESCRIPTOR_TRUSTED == 1 << 3` (see
// "Per-descriptor control").
__attribute__((always_inline)) static _Bool
femeRTCheckAccess(uint32_t Kind, uint32_t ExpectedKind, uint64_t Size,
                  uint32_t Flags, uint64_t Offset, uint64_t AccessSize) {
  _Bool KindOK = Kind == ExpectedKind;
  _Bool RangeOK = (Offset + AccessSize) <= Size;
  _Bool Trusted = (Flags & 8u) != 0;
  return KindOK && (RangeOK || Trusted);
}

// Unpacks a `R8G8B8A8_UNORM` value (four normalized `[0, 255]` bytes,
// little-endian: R, G, B, A) into a `<4 x float>` in `[0.0, 1.0]`.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR8G8B8A8Unorm(uint32_t Raw) {
  uint8_t B0 = (uint8_t)Raw;
  uint8_t B1 = (uint8_t)(Raw >> 8);
  uint8_t B2 = (uint8_t)(Raw >> 16);
  uint8_t B3 = (uint8_t)(Raw >> 24);
  FemeRTv4f32 V;
  V[0] = (float)B0 / 255.0f;
  V[1] = (float)B1 / 255.0f;
  V[2] = (float)B2 / 255.0f;
  V[3] = (float)B3 / 255.0f;
  return V;
}

// The inverse of `femeRTUnpackR8G8B8A8Unorm`: clamps each component to
// `[0.0, 1.0]`, scales to `[0, 255]`, and packs the four rounded bytes
// little-endian into one `uint32_t`.
__attribute__((always_inline)) static uint32_t
femeRTPackR8G8B8A8Unorm(FemeRTv4f32 Value) {
  float C0 = __builtin_fminf(__builtin_fmaxf(Value[0], 0.0f), 1.0f);
  float C1 = __builtin_fminf(__builtin_fmaxf(Value[1], 0.0f), 1.0f);
  float C2 = __builtin_fminf(__builtin_fmaxf(Value[2], 0.0f), 1.0f);
  float C3 = __builtin_fminf(__builtin_fmaxf(Value[3], 0.0f), 1.0f);
  uint8_t I0 = (uint8_t)__builtin_roundf(C0 * 255.0f);
  uint8_t I1 = (uint8_t)__builtin_roundf(C1 * 255.0f);
  uint8_t I2 = (uint8_t)__builtin_roundf(C2 * 255.0f);
  uint8_t I3 = (uint8_t)__builtin_roundf(C3 * 255.0f);
  return (uint32_t)I0 | ((uint32_t)I1 << 8) | ((uint32_t)I2 << 16) |
         ((uint32_t)I3 << 24);
}

//--- Typed-buffer `<4 x float>` view ------------------------------------------

// `feme.cpu.resource.load.typed.v4f32` (see `feme::cpu::ResourceCalls`):
// reads a `<4 x float>` element through a bindless typed-buffer descriptor,
// switching on `Format` between the `R32G32B32A32_FLOAT` identity format
// (`== 4`) and the packed `R8G8B8A8_UNORM` format (`== 13`) -- see
// `feme::cpu::ResourceFormat` in RuntimeABI.h for those numeric values, and
// "Descriptor formats" for why the conversion has to be a runtime switch
// rather than something the compiler can select at compile time.
// `ResourceKind::Typed == 1`. An inactive lane (`Mask == false`) or a
// failing bounds/kind check reads as zero, never touching `Heap`'s memory
// (see "Bounds checking").
FemeRTv4f32 femeCpuResourceLoadTypedV4F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex,
    _Bool Mask) asm("feme.cpu.resource.load.typed.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuResourceLoadTypedV4F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool IsPacked = Desc.Format == 13; // ResourceFormat::R8G8B8A8_UNORM.
  uint64_t ElemSize = IsPacked ? 4 : 16;
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  if (!(AccessOK && Mask)) {
    FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
    return Zero;
  }
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  if (IsPacked) {
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR8G8B8A8Unorm(Raw);
  }
  return (FemeRTv4f32) * (const FemeRTv4f32Unaligned *)Ptr;
}

// `feme.cpu.resource.store.typed.v4f32`: the store counterpart of
// `feme.cpu.resource.load.typed.v4f32` above -- same descriptor lookup,
// bounds/kind check and format switch, plus the UAV check "Descriptor
// heaps" requires for any write (`FEME_DESCRIPTOR_UAV == 1 << 0`; a
// constant buffer or other read-only view's `Flags` never sets it, so a
// store through one is silently dropped rather than corrupting it). An
// out-of-bounds or inactive-lane write is dropped, never touching `Heap`'s
// memory, matching the load's "reads zero" rule with "writes ignored" (see
// "Bounds checking").
void femeCpuResourceStoreTypedV4F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, FemeRTv4f32 Value,
    _Bool Mask) asm("feme.cpu.resource.store.typed.v4f32");

__attribute__((always_inline)) void
femeCpuResourceStoreTypedV4F32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                               uint32_t DescriptorIndex, uint64_t ElementIndex,
                               FemeRTv4f32 Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool IsPacked = Desc.Format == 13; // ResourceFormat::R8G8B8A8_UNORM.
  uint64_t ElemSize = IsPacked ? 4 : 16;
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!(AccessOK && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  if (IsPacked) {
    uint32_t Raw = femeRTPackR8G8B8A8Unorm(Value);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  *(FemeRTv4f32Unaligned *)Ptr = (FemeRTv4f32Unaligned)Value;
}

//--- Raw/structured-buffer views ----------------------------------------------

// `feme.cpu.resource.load.raw.i32`/`.f32`: read a scalar through a bindless
// raw or structured buffer descriptor at a byte offset `feme::cpu::
// ResourceLoweringPass` already computed (see "Descriptor heaps": no format
// conversion applies to raw/structured views, only the bounds/kind check).
// Both `ResourceKind::Raw == 3` and `ResourceKind::Structured == 2` are
// accepted, since the two share this call family (an unstructured
// `ByteAddressBuffer`'s descriptor is `Kind::Raw`; a `StructuredBuffer`'s is
// `Kind::Structured`).
int32_t
femeCpuResourceLoadRawI32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                          uint32_t DescriptorIndex, uint64_t ByteOffset,
                          _Bool Mask) asm("feme.cpu.resource.load.raw.i32");

__attribute__((always_inline)) int32_t femeCpuResourceLoadRawI32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 4);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 4);
  if (!((OkRaw || OkStructured) && Mask))
    return 0;
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  int32_t V;
  __builtin_memcpy(&V, Ptr, sizeof(V));
  return V;
}

void femeCpuResourceStoreRawI32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.i32");

__attribute__((always_inline)) void
femeCpuResourceStoreRawI32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                           uint32_t DescriptorIndex, uint64_t ByteOffset,
                           int32_t Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 4);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 4);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!((OkRaw || OkStructured) && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  __builtin_memcpy(Ptr, &Value, sizeof(Value));
}

float femeCpuResourceLoadRawF32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) asm("feme.cpu.resource.load.raw.f32");

__attribute__((always_inline)) float
femeCpuResourceLoadRawF32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                          uint32_t DescriptorIndex, uint64_t ByteOffset,
                          _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 4);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 4);
  if (!((OkRaw || OkStructured) && Mask))
    return 0.0f;
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  float V;
  __builtin_memcpy(&V, Ptr, sizeof(V));
  return V;
}

void femeCpuResourceStoreRawF32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, float Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.f32");

__attribute__((always_inline)) void
femeCpuResourceStoreRawF32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                           uint32_t DescriptorIndex, uint64_t ByteOffset,
                           float Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 4);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 4);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!((OkRaw || OkStructured) && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  __builtin_memcpy(Ptr, &Value, sizeof(Value));
}
