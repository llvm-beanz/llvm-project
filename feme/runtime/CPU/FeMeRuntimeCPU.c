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
// Scope (roadmap milestone 3, V4): this file covers the typed-buffer views
// `<4 x float>` (covering the `R32G32B32A32_FLOAT` identity format and the
// packed `R8G8B8A8_UNORM` format, to establish the format-switch pattern
// concretely) and `<4 x i32>` (the `R32G32B32A32_UINT`/`_SINT` identity
// formats -- see "Typed-buffer `<4 x i32>` view" below), and the
// raw/structured-buffer views `i32`/`float`. Every other canonical call
// `feme::cpu::ResourceCalls` can create (other typed views, the remaining
// formats "Descriptor formats" lists, atomics) is a mechanical repeat of the
// same pattern once a call site actually needs it -- "Additional formats
// extend one helper implementation rather than every access site" -- and is
// added on demand rather than spelled out exhaustively up front.
//
// Scope (roadmap R30): the "Canonical image operations"/"Texture layout and
// formats" helpers below (`feme.cpu.image.*`) cover 2D images only --
// `femeRTApplyAddressMode` is itself dimension-agnostic (it addresses one
// coordinate axis at a time), so a 1D entry point is the same mechanical
// repeat the typed-buffer note above describes, not a new algorithm, and is
// left for the call site that first needs it. Point and (bilinear) linear
// filtering, all five `SamplerAddressMode`s, explicit-LOD mip selection and
// depth-comparison sampling (with PCF-style bilinear-weighted comparison
// results, matching real hardware's comparison-filter behaviour) are
// implemented; trilinear (cross-mip) blending and true derivative-based
// implicit LOD are not -- an implicit-LOD sample uses mip level 0, which is
// exact whenever the source shader supplies its own explicit level (the
// common compute-shader case) and only approximate otherwise, since this
// runtime does not yet compute fragment derivatives (see
// feme/docs/FeMeGraphicsDesign.md's "Canonical image operations"). The
// format table (roadmap E25, broadened from the original three-format
// table this comment used to describe) covers every non-integer,
// non-block-compressed, non-depth/stencil format `feme::cpu::ResourceFormat`
// lists: the identity `R32*_FLOAT` formats (`R32_FLOAT`/`R32G32_FLOAT`/
// `R32G32B32_FLOAT`/`R32G32B32A32_FLOAT`, padding a missing component the
// way `OpImageFetch` does: unread color channels `0.0`, an unread alpha
// channel `1.0`), the packed 8-bit formats (`R8G8B8A8_UNORM`/`_SNORM`/
// `_UNORM_SRGB`, the latter sRGB-decoded on every sample/load per "Texture
// layout and formats", and `B8G8R8A8_UNORM`), `R16G16B16A16_FLOAT` (a
// hand-written binary16-to-`float` conversion, since this file cannot
// assume hardware half-float support), and three more packed-word formats
// (`R11G11B10_FLOAT`, `R10G10B10A2_UNORM`, `A8_UNORM`, `A1B5G5R5_UNORM`).
// `_UINT`/`_SINT` formats are deliberately left out: every `feme.cpu.image.*`
// entry point below returns `<4 x float>`, and no integer-returning
// counterpart exists yet to consume an integer format's decoded value, so
// decoding one here would have nothing correct to feed it to -- the same
// "mechanical, added on demand" scoping as every other still-missing
// format, tracked as follow-up rather than a gap this row closes.
//
// Update (roadmap E26): `feme.cpu.image.load.2d.v4i32` now exists (see
// "`<4 x i32>` image load" below), giving an integer format's decoded
// value somewhere correct to go. `femeRTImageFormatElementSize`/a new
// `femeRTUnpackImageTexelI32` decode exactly the mandatory-sampled
// `_UINT`/`_SINT` formats the Vulkan spec's own "Mandatory Format Support"
// tables list (`R32G32B32A32_UINT`/`_SINT`, `R16G16B16A16_UINT`/`_SINT`,
// `R8G8B8A8_UINT`/`_SINT`, `R10G10B10A2_UINT`) -- narrower than the full
// integer format list `feme::cpu::ResourceFormat` has (e.g. the
// partial-component `R32_UINT`/`R32G32_UINT`/`R32G32B32_UINT` are not
// mandatory-sampled and are left for whichever call site first needs
// them, the same "mechanical, added on demand" scoping as every other
// still-missing format). No filtered-sample counterpart is added: SPIR-V
// never legalizes `OpImageSample*` against an integer-sampled image, only
// `OpImageFetch`, so there is nothing for one to mean.
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

// Unpacks a `R8G8B8A8_SNORM` value (four signed-normalized bytes,
// little-endian: R, G, B, A) into a `<4 x float>` in `[-1.0, 1.0]`, per the
// Vulkan spec's SNORM conversion (47.3 "Conversion from Normalized Fixed-
// Point to Floating-Point"): `value = max(c / 127, -1.0)`.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR8G8B8A8Snorm(uint32_t Raw) {
  int8_t B0 = (int8_t)(uint8_t)Raw;
  int8_t B1 = (int8_t)(uint8_t)(Raw >> 8);
  int8_t B2 = (int8_t)(uint8_t)(Raw >> 16);
  int8_t B3 = (int8_t)(uint8_t)(Raw >> 24);
  FemeRTv4f32 V;
  V[0] = __builtin_fmaxf((float)B0 / 127.0f, -1.0f);
  V[1] = __builtin_fmaxf((float)B1 / 127.0f, -1.0f);
  V[2] = __builtin_fmaxf((float)B2 / 127.0f, -1.0f);
  V[3] = __builtin_fmaxf((float)B3 / 127.0f, -1.0f);
  return V;
}

// The inverse of `femeRTUnpackR8G8B8A8Snorm`: clamps each component to
// `[-1.0, 1.0]`, scales to `[-127, 127]`, and packs the four rounded bytes
// little-endian into one `uint32_t`.
__attribute__((always_inline)) static uint32_t
femeRTPackR8G8B8A8Snorm(FemeRTv4f32 Value) {
  float C0 = __builtin_fminf(__builtin_fmaxf(Value[0], -1.0f), 1.0f);
  float C1 = __builtin_fminf(__builtin_fmaxf(Value[1], -1.0f), 1.0f);
  float C2 = __builtin_fminf(__builtin_fmaxf(Value[2], -1.0f), 1.0f);
  float C3 = __builtin_fminf(__builtin_fmaxf(Value[3], -1.0f), 1.0f);
  uint8_t I0 = (uint8_t)(int8_t)__builtin_roundf(C0 * 127.0f);
  uint8_t I1 = (uint8_t)(int8_t)__builtin_roundf(C1 * 127.0f);
  uint8_t I2 = (uint8_t)(int8_t)__builtin_roundf(C2 * 127.0f);
  uint8_t I3 = (uint8_t)(int8_t)__builtin_roundf(C3 * 127.0f);
  return (uint32_t)I0 | ((uint32_t)I1 << 8) | ((uint32_t)I2 << 16) |
         ((uint32_t)I3 << 24);
}

//--- Typed-buffer `<4 x float>` view ------------------------------------------

// `feme.cpu.resource.load.typed.v4f32` (see `feme::cpu::ResourceCalls`):
// reads a `<4 x float>` element through a bindless typed-buffer descriptor,
// switching on `Format` between the `R32G32B32A32_FLOAT` identity format
// (`== 4`) and the packed `R8G8B8A8_UNORM`/`_SNORM` formats (`== 13`/`14`)
// -- see `feme::cpu::ResourceFormat` in RuntimeABI.h for those numeric
// values, and "Descriptor formats" for why the conversion has to be a
// runtime switch rather than something the compiler can select at compile
// time. `ResourceKind::Typed == 1`. An inactive lane (`Mask == false`) or a
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
  _Bool IsUnorm = Desc.Format == 13; // ResourceFormat::R8G8B8A8_UNORM.
  _Bool IsSnorm = Desc.Format == 14; // ResourceFormat::R8G8B8A8_SNORM.
  _Bool IsPacked = IsUnorm || IsSnorm;
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
    return IsSnorm ? femeRTUnpackR8G8B8A8Snorm(Raw)
                   : femeRTUnpackR8G8B8A8Unorm(Raw);
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
  _Bool IsUnorm = Desc.Format == 13; // ResourceFormat::R8G8B8A8_UNORM.
  _Bool IsSnorm = Desc.Format == 14; // ResourceFormat::R8G8B8A8_SNORM.
  _Bool IsPacked = IsUnorm || IsSnorm;
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
    uint32_t Raw = IsSnorm ? femeRTPackR8G8B8A8Snorm(Value)
                           : femeRTPackR8G8B8A8Unorm(Value);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  *(FemeRTv4f32Unaligned *)Ptr = (FemeRTv4f32Unaligned)Value;
}

//--- Typed-buffer `<4 x i32>` view
//---------------------------------------------

// A 4-lane `int32_t` vector, compiled to LLVM IR's `<4 x i32>`.
typedef int32_t FemeRTv4i32 __attribute__((vector_size(16)));

// The same vector type, with its assumed pointer alignment relaxed to 4
// bytes, matching `FemeRTv4f32Unaligned` above.
typedef int32_t FemeRTv4i32Unaligned
    __attribute__((vector_size(16), aligned(4)));

// Unpacks a `R8G8B8A8_UINT` value (four unsigned bytes, little-endian: R,
// G, B, A) into a `<4 x i32>` by zero-extending each byte.
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackR8G8B8A8Uint(uint32_t Raw) {
  FemeRTv4i32 V;
  V[0] = (int32_t)(uint8_t)Raw;
  V[1] = (int32_t)(uint8_t)(Raw >> 8);
  V[2] = (int32_t)(uint8_t)(Raw >> 16);
  V[3] = (int32_t)(uint8_t)(Raw >> 24);
  return V;
}

// The inverse of `femeRTUnpackR8G8B8A8Uint`: truncates each lane to its low
// byte (clamping is the application's own responsibility, matching D3D/
// Vulkan's "out of range values ... produce undefined results" rule for a
// UINT format) and packs the four bytes little-endian into one `uint32_t`.
__attribute__((always_inline)) static uint32_t
femeRTPackR8G8B8A8Uint(FemeRTv4i32 Value) {
  uint8_t I0 = (uint8_t)Value[0];
  uint8_t I1 = (uint8_t)Value[1];
  uint8_t I2 = (uint8_t)Value[2];
  uint8_t I3 = (uint8_t)Value[3];
  return (uint32_t)I0 | ((uint32_t)I1 << 8) | ((uint32_t)I2 << 16) |
         ((uint32_t)I3 << 24);
}

// Unpacks a `R8G8B8A8_SINT` value (four signed bytes, little-endian: R, G,
// B, A) into a `<4 x i32>` by sign-extending each byte.
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackR8G8B8A8Sint(uint32_t Raw) {
  FemeRTv4i32 V;
  V[0] = (int32_t)(int8_t)(uint8_t)Raw;
  V[1] = (int32_t)(int8_t)(uint8_t)(Raw >> 8);
  V[2] = (int32_t)(int8_t)(uint8_t)(Raw >> 16);
  V[3] = (int32_t)(int8_t)(uint8_t)(Raw >> 24);
  return V;
}

// The inverse of `femeRTUnpackR8G8B8A8Sint`: truncating each lane to its low
// byte produces the same bit pattern regardless of signedness, so this
// shares `femeRTPackR8G8B8A8Uint`'s implementation exactly -- kept as a
// separate, identically named-per-format entry point for symmetry with the
// unpack side, where the sign extension does differ.
__attribute__((always_inline)) static uint32_t
femeRTPackR8G8B8A8Sint(FemeRTv4i32 Value) {
  return femeRTPackR8G8B8A8Uint(Value);
}

// `feme.cpu.resource.load.typed.v4i32` (V4, see `feme::cpu::ResourceCalls`):
// reads a `<4 x i32>` element through a bindless typed-buffer descriptor.
// The `R32G32B32A32_UINT`/`_SINT` identity formats need no scalar
// conversion switch: the four 32-bit lanes are reinterpreted directly,
// matching the signed/unsigned distinction entirely by the shader's own
// choice of `<4 x i32>` load/store type (SPIR-V's `OpTypeInt`'s signedness
// bit plays no role in the raw bytes). The packed `R8G8B8A8_UINT`/`_SINT`
// formats (`== 15`/`16` -- see `feme::cpu::ResourceFormat` in RuntimeABI.h)
// do need one, analogous to the `<4 x float>` view's `R8G8B8A8_UNORM`/
// `_SNORM` handling above. `ResourceKind::Typed == 1`. An inactive lane or
// a failing bounds/kind check reads as zero, never touching `Heap`'s
// memory, exactly like the `<4 x float>` load above (see "Bounds
// checking").
FemeRTv4i32 femeCpuResourceLoadTypedV4I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex,
    _Bool Mask) asm("feme.cpu.resource.load.typed.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuResourceLoadTypedV4I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool IsUint = Desc.Format == 15; // ResourceFormat::R8G8B8A8_UINT.
  _Bool IsSint = Desc.Format == 16; // ResourceFormat::R8G8B8A8_SINT.
  _Bool IsPacked = IsUint || IsSint;
  uint64_t ElemSize = IsPacked ? 4 : 16;
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  if (!(AccessOK && Mask)) {
    FemeRTv4i32 Zero = {0, 0, 0, 0};
    return Zero;
  }
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  if (IsPacked) {
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return IsSint ? femeRTUnpackR8G8B8A8Sint(Raw)
                  : femeRTUnpackR8G8B8A8Uint(Raw);
  }
  return (FemeRTv4i32) * (const FemeRTv4i32Unaligned *)Ptr;
}

// `feme.cpu.resource.store.typed.v4i32`: the store counterpart of
// `feme.cpu.resource.load.typed.v4i32` above, with the same UAV check every
// typed-buffer store requires (see `femeCpuResourceStoreTypedV4F32`).
void femeCpuResourceStoreTypedV4I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, FemeRTv4i32 Value,
    _Bool Mask) asm("feme.cpu.resource.store.typed.v4i32");

__attribute__((always_inline)) void
femeCpuResourceStoreTypedV4I32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                               uint32_t DescriptorIndex, uint64_t ElementIndex,
                               FemeRTv4i32 Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool IsUint = Desc.Format == 15; // ResourceFormat::R8G8B8A8_UINT.
  _Bool IsSint = Desc.Format == 16; // ResourceFormat::R8G8B8A8_SINT.
  _Bool IsPacked = IsUint || IsSint;
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
    uint32_t Raw =
        IsSint ? femeRTPackR8G8B8A8Sint(Value) : femeRTPackR8G8B8A8Uint(Value);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  *(FemeRTv4i32Unaligned *)Ptr = (FemeRTv4i32Unaligned)Value;
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

//--- Images and samplers (roadmap R30) ----------------------------------------

// Mirrors `feme::cpu::FemeImageSubresourceLayout` (RuntimeABI.h): { Offset,
// RowPitch, SlicePitch, SampleStride }.
typedef struct {
  uint64_t Offset;
  uint64_t RowPitch;
  uint64_t SlicePitch;
  uint64_t SampleStride;
} FemeRTImageSubresourceLayout;

// Mirrors `feme::cpu::FemeImageDescriptor` (RuntimeABI.h) field for field.
typedef struct {
  void *Data;
  uint64_t SizeInBytes;
  uint32_t Dimension;
  uint32_t Format;
  uint32_t Width;
  uint32_t Height;
  uint32_t Depth;
  uint32_t MipLevels;
  uint32_t ArrayLayers;
  uint32_t PlaneCount;
  uint32_t SampleCount;
  uint32_t Flags;
  const FemeRTImageSubresourceLayout *MipLayouts;
  uint32_t MipLayoutCount;
  uint32_t Reserved[3];
} FemeRTImageDescriptor;

// Mirrors `feme::cpu::FemeSamplerDescriptor` (RuntimeABI.h) field for field.
typedef struct {
  uint32_t MinFilter;
  uint32_t MagFilter;
  uint32_t MipFilter;
  uint32_t AddressU;
  uint32_t AddressV;
  uint32_t AddressW;
  float LodBias;
  float MinLod;
  float MaxLod;
  uint32_t CompareFunc;
  float BorderColor[4];
  float MaxAnisotropy;
  uint32_t ReductionMode;
  uint32_t Flags;
  uint32_t Reserved[3];
} FemeRTSamplerDescriptor;

// Loads image descriptor `Index` of `Heap`/`HeapCount`, or an all-zero
// (`Data == NULL`) descriptor if `Index >= HeapCount` -- the same
// "unwritten descriptor reads as empty" rule `femeRTLoadDescriptor` applies
// to buffers (see "Bounds checking"), reusing `FemeImageDescriptor`'s own
// zero-is-empty convention (see RuntimeABI.h) rather than a separate
// `IndexOK` flag.
__attribute__((always_inline)) static FemeRTImageDescriptor
femeRTLoadImageDescriptor(const FemeRTImageDescriptor *Heap, uint32_t HeapCount,
                          uint32_t Index) {
  if (Index >= HeapCount) {
    FemeRTImageDescriptor Empty;
    __builtin_memset(&Empty, 0, sizeof(Empty));
    return Empty;
  }
  return Heap[Index];
}

// Loads sampler descriptor `Index` of `Heap`/`HeapCount`. Unlike an image
// descriptor, an out-of-range index yields the all-zero sampler
// (`Nearest`/`Repeat` filtering, no comparison or anisotropy) rather than an
// invalid one: a sampler owns no host storage, so its zero value is always
// a legal, if unhelpful, sampler (see RuntimeABI.h's
// `FemeSamplerDescriptor` comment).
__attribute__((always_inline)) static FemeRTSamplerDescriptor
femeRTLoadSamplerDescriptor(const FemeRTSamplerDescriptor *Heap,
                            uint32_t HeapCount, uint32_t Index) {
  if (Index >= HeapCount) {
    FemeRTSamplerDescriptor Default;
    __builtin_memset(&Default, 0, sizeof(Default));
    return Default;
  }
  return Heap[Index];
}

// The byte size of one texel of `Format` (`feme::cpu::ResourceFormat`), or 0
// for a format this file does not (yet) decode -- see the file header
// comment's format-table scope note.
//
// Roadmap E25: broadened from the original three-format table
// (`R32G32B32A32_FLOAT`/`R8G8B8A8_UNORM`/`_UNORM_SRGB`) to every other
// non-integer, non-block-compressed, non-depth/stencil format
// `feme::cpu::ResourceFormat` lists.
//
// Roadmap E26: also covers the mandatory-sampled `_UINT`/`_SINT` formats
// (see the file header comment) -- one texel's *byte size* does not
// depend on whether the caller reads it back as `<4 x float>` or
// `<4 x i32>`, so this table is shared by both `femeRTUnpackImageTexel`
// and the new `femeRTUnpackImageTexelI32` below rather than duplicated.
__attribute__((always_inline)) static uint64_t
femeRTImageFormatElementSize(uint32_t Format) {
  switch (Format) {
  case 1:  // R32_FLOAT
    return 4;
  case 2:  // R32G32_FLOAT
    return 8;
  case 3:  // R32G32B32_FLOAT
    return 12;
  case 4: // R32G32B32A32_FLOAT
  case 8: // R32G32B32A32_UINT
  case 12: // R32G32B32A32_SINT
    return 16;
  case 13: // R8G8B8A8_UNORM
  case 14: // R8G8B8A8_SNORM
  case 15: // R8G8B8A8_UINT
  case 16: // R8G8B8A8_SINT
  case 17: // R8G8B8A8_UNORM_SRGB
    return 4;
  case 18: // R16G16B16A16_FLOAT
  case 21: // R16G16B16A16_UINT
  case 22: // R16G16B16A16_SINT
    return 8;
  case 23: // R11G11B10_FLOAT (packed into a single 4-byte word)
  case 24: // R10G10B10A2_UNORM (packed into a single 4-byte word)
  case 25: // R10G10B10A2_UINT (packed into a single 4-byte word)
    return 4;
  case 26: // B8G8R8A8_UNORM
    return 4;
  case 27: // A8_UNORM
    return 1;
  case 28: // A1B5G5R5_UNORM (packed into a single 2-byte word)
    return 2;
  default:
    return 0;
  }
}

// Decodes one sRGB-encoded component (`[0, 1]`) to linear light, the IEC
// 61966-2-1 piecewise transfer function "Texture layout and formats" calls
// for ("sRGB decode on sampling"). Alpha is never sRGB-encoded by
// convention, so callers apply this to color channels only.
__attribute__((always_inline)) static float femeRTSRGBToLinear(float C) {
  return C <= 0.04045f ? C / 12.92f
                       : __builtin_powf((C + 0.055f) / 1.055f, 2.4f);
}

// Converts one IEEE 754 binary16 ("half float") bit pattern to a `float`,
// by hand rather than via a hardware/`_Float16` conversion instruction --
// this file is compiled freestanding for whatever host runs the JIT/AOT
// backend (see the file header comment), so it cannot assume the target
// has (or that this build enables) F16C-style hardware half-float support.
// Handles zero, subnormal, normal, infinity and NaN inputs.
__attribute__((always_inline)) static float femeRTHalfToFloat(uint16_t H) {
  uint32_t Sign = (uint32_t)(H & 0x8000u) << 16;
  uint32_t Exp = (H >> 10) & 0x1Fu;
  uint32_t Mant = H & 0x3FFu;
  uint32_t Bits;
  if (Exp == 0) {
    if (Mant == 0) {
      Bits = Sign; // +/- zero.
    } else {
      // Subnormal half: normalize the mantissa into a normal float's
      // implicit-leading-1 form, adjusting the exponent for each shift.
      int32_t E = -1;
      uint32_t M = Mant;
      do {
        M <<= 1;
        ++E;
      } while (!(M & 0x400u));
      M &= 0x3FFu;
      Bits = Sign | ((uint32_t)(127 - 15 - E) << 23) | (M << 13);
    }
  } else if (Exp == 0x1Fu) {
    Bits = Sign | 0x7F800000u | (Mant << 13); // Infinity or NaN.
  } else {
    Bits = Sign | ((Exp - 15u + 127u) << 23) | (Mant << 13);
  }
  float F;
  __builtin_memcpy(&F, &Bits, sizeof(F));
  return F;
}

// Unpacks a `B8G8R8A8_UNORM` value (four normalized `[0, 255]` bytes,
// little-endian: B, G, R, A) into a `<4 x float>` in `[0.0, 1.0]` -- the
// same conversion as `femeRTUnpackR8G8B8A8Unorm`, just with the red and
// blue channels swapped in memory.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackB8G8R8A8Unorm(uint32_t Raw) {
  FemeRTv4f32 V = femeRTUnpackR8G8B8A8Unorm(Raw);
  float R = V[0];
  V[0] = V[2];
  V[2] = R;
  return V;
}

// Unpacks a `R10G10B10A2_UNORM` value (`VK_FORMAT_A2B10G10R10_UNORM_PACK32`:
// from the MSB down, 2 bits of A, 10 bits of B, 10 bits of G, 10 bits of R)
// into a `<4 x float>` in `[0.0, 1.0]`.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR10G10B10A2Unorm(uint32_t Raw) {
  FemeRTv4f32 V;
  V[0] = (float)(Raw & 0x3FFu) / 1023.0f;
  V[1] = (float)((Raw >> 10) & 0x3FFu) / 1023.0f;
  V[2] = (float)((Raw >> 20) & 0x3FFu) / 1023.0f;
  V[3] = (float)((Raw >> 30) & 0x3u) / 3.0f;
  return V;
}

// Unpacks a `R11G11B10_FLOAT` value (`VK_FORMAT_B10G11R11_UFLOAT_PACK32`:
// from the LSB up, an unsigned 6-bit-mantissa/5-bit-exponent 11-bit float
// for R, another for G, then a 5-bit-mantissa/5-bit-exponent 10-bit float
// for B) into a `<4 x float>`, alpha always `1.0` (this format carries no
// alpha channel).
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR11G11B10Float(uint32_t Raw) {
  // An unsigned 5-bit-exponent minifloat with `MantBits` mantissa bits
  // shares binary16's exponent bias (15) and special-value encoding, so
  // `femeRTHalfToFloat` decodes it once its mantissa is left-shifted into
  // binary16's own 10-bit mantissa field.
  uint32_t R10 = ((Raw & 0x7FFu) << 5) & 0xFFFFu;    // 6-bit mantissa -> 10.
  uint32_t G10 = (((Raw >> 11) & 0x7FFu) << 5) & 0xFFFFu;
  uint32_t B10 = (((Raw >> 22) & 0x3FFu) << 6) & 0xFFFFu; // 5-bit mantissa.
  FemeRTv4f32 V;
  V[0] = femeRTHalfToFloat((uint16_t)R10);
  V[1] = femeRTHalfToFloat((uint16_t)G10);
  V[2] = femeRTHalfToFloat((uint16_t)B10);
  V[3] = 1.0f;
  return V;
}

// Unpacks an `A8_UNORM` value (a single normalized `[0, 255]` alpha byte,
// no color channels at all) into a `<4 x float>`, color channels `0.0`.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackA8Unorm(uint8_t Raw) {
  FemeRTv4f32 V = {0.0f, 0.0f, 0.0f, (float)Raw / 255.0f};
  return V;
}

// Unpacks an `A1B5G5R5_UNORM` value
// (`VK_FORMAT_A1B5G5R5_UNORM_PACK16`: from the MSB down, 1 bit of A, 5
// bits of B, 5 bits of G, 5 bits of R) into a `<4 x float>` in
// `[0.0, 1.0]`.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackA1B5G5R5Unorm(uint16_t Raw) {
  FemeRTv4f32 V;
  V[0] = (float)(Raw & 0x1Fu) / 31.0f;
  V[1] = (float)((Raw >> 5) & 0x1Fu) / 31.0f;
  V[2] = (float)((Raw >> 10) & 0x1Fu) / 31.0f;
  V[3] = (float)((Raw >> 15) & 0x1u);
  return V;
}

// Unpacks one texel of `Format` at `Ptr` into a linear-light `<4 x float>`,
// or all-zero for a format `femeRTImageFormatElementSize` doesn't know
// (guarded by that function's 0 return at every call site below, so this
// default is unreachable in practice, but kept total rather than partial).
//
// Roadmap E25: extended alongside `femeRTImageFormatElementSize` above --
// see that function's comment for the scope this row does (and does not)
// cover. A format with fewer than four logical components (`R32_FLOAT`,
// `R32G32_FLOAT`, `R32G32B32_FLOAT`) pads the missing components the same
// way SPIR-V's own `OpImageFetch`/`OpImageSampleImplicitLod` do for a
// partial-component image format: an unread color channel reads `0.0`, an
// unread alpha channel reads `1.0`.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackImageTexel(uint32_t Format, const unsigned char *Ptr) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  switch (Format) {
  case 1: { // R32_FLOAT
    float R;
    __builtin_memcpy(&R, Ptr, sizeof(R));
    FemeRTv4f32 V = {R, 0.0f, 0.0f, 1.0f};
    return V;
  }
  case 2: { // R32G32_FLOAT
    float RG[2];
    __builtin_memcpy(RG, Ptr, sizeof(RG));
    FemeRTv4f32 V = {RG[0], RG[1], 0.0f, 1.0f};
    return V;
  }
  case 3: { // R32G32B32_FLOAT
    float RGB[3];
    __builtin_memcpy(RGB, Ptr, sizeof(RGB));
    FemeRTv4f32 V = {RGB[0], RGB[1], RGB[2], 1.0f};
    return V;
  }
  case 4: { // R32G32B32A32_FLOAT: identity format, no conversion.
    return (FemeRTv4f32) * (const FemeRTv4f32Unaligned *)Ptr;
  }
  case 13: { // R8G8B8A8_UNORM
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR8G8B8A8Unorm(Raw);
  }
  case 14: { // R8G8B8A8_SNORM
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR8G8B8A8Snorm(Raw);
  }
  case 17: { // R8G8B8A8_UNORM_SRGB
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    FemeRTv4f32 V = femeRTUnpackR8G8B8A8Unorm(Raw);
    V[0] = femeRTSRGBToLinear(V[0]);
    V[1] = femeRTSRGBToLinear(V[1]);
    V[2] = femeRTSRGBToLinear(V[2]);
    return V;
  }
  case 18: { // R16G16B16A16_FLOAT
    uint16_t Raw[4];
    __builtin_memcpy(Raw, Ptr, sizeof(Raw));
    FemeRTv4f32 V = {femeRTHalfToFloat(Raw[0]), femeRTHalfToFloat(Raw[1]),
                     femeRTHalfToFloat(Raw[2]), femeRTHalfToFloat(Raw[3])};
    return V;
  }
  case 23: { // R11G11B10_FLOAT
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR11G11B10Float(Raw);
  }
  case 24: { // R10G10B10A2_UNORM
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR10G10B10A2Unorm(Raw);
  }
  case 26: { // B8G8R8A8_UNORM
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackB8G8R8A8Unorm(Raw);
  }
  case 27: { // A8_UNORM
    return femeRTUnpackA8Unorm(*Ptr);
  }
  case 28: { // A1B5G5R5_UNORM
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackA1B5G5R5Unorm(Raw);
  }
  default:
    return Zero;
  }
}

// Unpacks a `R16G16B16A16_UINT` value (four unsigned 16-bit words,
// little-endian) into a `<4 x i32>` by zero-extending each word.
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackR16G16B16A16Uint(const uint16_t Raw[4]) {
  FemeRTv4i32 V;
  V[0] = (int32_t)Raw[0];
  V[1] = (int32_t)Raw[1];
  V[2] = (int32_t)Raw[2];
  V[3] = (int32_t)Raw[3];
  return V;
}

// Unpacks a `R16G16B16A16_SINT` value (four signed 16-bit words,
// little-endian) into a `<4 x i32>` by sign-extending each word.
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackR16G16B16A16Sint(const uint16_t Raw[4]) {
  FemeRTv4i32 V;
  V[0] = (int32_t)(int16_t)Raw[0];
  V[1] = (int32_t)(int16_t)Raw[1];
  V[2] = (int32_t)(int16_t)Raw[2];
  V[3] = (int32_t)(int16_t)Raw[3];
  return V;
}

// Unpacks a `R10G10B10A2_UINT` value
// (`VK_FORMAT_A2B10G10R10_UINT_PACK32`: from the MSB down, 2 bits of A, 10
// bits of B, 10 bits of G, 10 bits of R) into a `<4 x i32>` by
// zero-extending each field -- the integer counterpart of
// `femeRTUnpackR10G10B10A2Unorm`, with no `[0, 1]` normalization.
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackR10G10B10A2Uint(uint32_t Raw) {
  FemeRTv4i32 V;
  V[0] = (int32_t)(Raw & 0x3FFu);
  V[1] = (int32_t)((Raw >> 10) & 0x3FFu);
  V[2] = (int32_t)((Raw >> 20) & 0x3FFu);
  V[3] = (int32_t)((Raw >> 30) & 0x3u);
  return V;
}

// Unpacks one texel of `Format` at `Ptr` into a `<4 x i32>`, or all-zero
// for a format this table doesn't know (guarded by
// `femeRTImageFormatElementSize`'s own 0 return at every call site, so this
// default is unreachable in practice, but kept total rather than partial).
//
// Roadmap E26: the integer counterpart of `femeRTUnpackImageTexel` above,
// covering exactly the mandatory-sampled `_UINT`/`_SINT` formats -- see the
// file header comment's scope note for why this list is narrower than
// `femeRTUnpackImageTexel`'s own. `R32G32B32A32_UINT`/`_SINT` need no
// scalar conversion: the four 32-bit lanes are reinterpreted directly, the
// same identity-format shortcut `femeCpuResourceLoadTypedV4I32` already
// takes for the typed-buffer view.
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackImageTexelI32(uint32_t Format, const unsigned char *Ptr) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  switch (Format) {
  case 8:  // R32G32B32A32_UINT
  case 12: // R32G32B32A32_SINT: identity format, no conversion.
    return (FemeRTv4i32) * (const FemeRTv4i32Unaligned *)Ptr;
  case 15: { // R8G8B8A8_UINT
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR8G8B8A8Uint(Raw);
  }
  case 16: { // R8G8B8A8_SINT
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR8G8B8A8Sint(Raw);
  }
  case 21: { // R16G16B16A16_UINT
    uint16_t Raw[4];
    __builtin_memcpy(Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR16G16B16A16Uint(Raw);
  }
  case 22: { // R16G16B16A16_SINT
    uint16_t Raw[4];
    __builtin_memcpy(Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR16G16B16A16Sint(Raw);
  }
  case 25: { // R10G10B10A2_UINT
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR10G10B10A2Uint(Raw);
  }
  default:
    return Zero;
  }
}

// Applies `SamplerAddressMode` `Mode` to one coordinate axis: `Coord` (an
// integer texel index, possibly outside `[0, Size)`) against axis extent
// `Size`. Sets `*UseBorder` if the result should be replaced by the
// sampler's border color instead of a real texel (only possible for
// `ClampToBorder`, mode 3); every other mode always returns an in-range
// index. This is dimension-agnostic -- called once per axis -- so it is
// the shared building block for 1D and 2D addressing alike (see the file
// header comment's scope note).
__attribute__((always_inline)) static int32_t
femeRTApplyAddressMode(int32_t Coord, int32_t Size, uint32_t Mode,
                       _Bool *UseBorder) {
  if (Size <= 0) {
    *UseBorder = 1;
    return 0;
  }
  switch (Mode) {
  case 0: { // Repeat
    int32_t M = Coord % Size;
    return M < 0 ? M + Size : M;
  }
  case 1: { // MirroredRepeat
    int32_t Period = 2 * Size;
    int32_t M = Coord % Period;
    if (M < 0)
      M += Period;
    return M < Size ? M : (Period - 1 - M);
  }
  case 3: { // ClampToBorder
    if (Coord < 0 || Coord >= Size) {
      *UseBorder = 1;
      return 0;
    }
    return Coord;
  }
  case 4: { // MirrorClampToEdge
    int32_t M = Coord < 0 ? -1 - Coord : Coord;
    return M >= Size ? Size - 1 : M;
  }
  case 2: // ClampToEdge
  default:
    return Coord < 0 ? 0 : (Coord >= Size ? Size - 1 : Coord);
  }
}

// Reads one texel at integer coordinates `(X, Y)` of mip level `Level` of
// `Img`, or `BorderColor` if `UseBorder` is set (a `ClampToBorder` axis
// resolved out of range), or all-zero for any other unreadable access (no
// image bound, `Level` beyond `MipLayoutCount`, an unrecognized format, or
// an access `femeRTImageFormatElementSize`/the mip layout's own
// `SizeInBytes` bound rejects) -- the same "out-of-range reads zero" rule
// buffers use (see "Bounds checking").
__attribute__((always_inline)) static FemeRTv4f32
femeRTFetchTexel2D(const FemeRTImageDescriptor *Img, uint32_t Level, int32_t X,
                   int32_t Y, _Bool UseBorder, const float BorderColor[4]) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (UseBorder) {
    FemeRTv4f32 Border = {BorderColor[0], BorderColor[1], BorderColor[2],
                          BorderColor[3]};
    return Border;
  }
  if (!Img->Data || Level >= Img->MipLayoutCount)
    return Zero;
  uint64_t ElemSize = femeRTImageFormatElementSize(Img->Format);
  if (ElemSize == 0)
    return Zero;
  const FemeRTImageSubresourceLayout *Layout = &Img->MipLayouts[Level];
  uint64_t Offset =
      Layout->Offset + (uint64_t)Y * Layout->RowPitch + (uint64_t)X * ElemSize;
  if (Offset + ElemSize > Img->SizeInBytes)
    return Zero;
  const unsigned char *Ptr = (const unsigned char *)Img->Data + Offset;
  return femeRTUnpackImageTexel(Img->Format, Ptr);
}

// The integer counterpart of `femeRTFetchTexel2D` above, for
// `feme.cpu.image.load.2d.v4i32` (roadmap E26). Takes no border color: an
// integer-channel image is only ever reached through `Load2D`/
// `OpImageFetch` (see ImageCalls.h's `Load2DI32` comment), which -- like
// its float counterpart -- addresses no sampler and therefore no address
// mode, so there is no `ClampToBorder` case to honor here either.
__attribute__((always_inline)) static FemeRTv4i32
femeRTFetchTexel2DI32(const FemeRTImageDescriptor *Img, uint32_t Level,
                      int32_t X, int32_t Y) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Img->Data || Level >= Img->MipLayoutCount)
    return Zero;
  uint64_t ElemSize = femeRTImageFormatElementSize(Img->Format);
  if (ElemSize == 0)
    return Zero;
  const FemeRTImageSubresourceLayout *Layout = &Img->MipLayouts[Level];
  uint64_t Offset =
      Layout->Offset + (uint64_t)Y * Layout->RowPitch + (uint64_t)X * ElemSize;
  if (Offset + ElemSize > Img->SizeInBytes)
    return Zero;
  const unsigned char *Ptr = (const unsigned char *)Img->Data + Offset;
  return femeRTUnpackImageTexelI32(Img->Format, Ptr);
}

// Selects the mip level a sample reads from: `Lod` clamped to
// `[0, MipLevels - 1]` for an explicit-LOD sample, or level 0 for an
// implicit-LOD one (see the file header comment's scope note on why --
// no fragment-derivative computation exists yet). `MipFilter` is accepted
// for the API shape a future trilinear blend needs, but not yet consulted:
// both `Nearest` and `Linear` currently round to the nearer single level.
__attribute__((always_inline)) static uint32_t
femeRTSelectMipLevel(const FemeRTImageDescriptor *Img, float Lod,
                     _Bool UseExplicitLod, uint32_t MipFilter) {
  (void)MipFilter;
  if (Img->MipLevels == 0)
    return 0;
  float MaxLevel = (float)(Img->MipLevels - 1);
  float L = UseExplicitLod ? Lod : 0.0f;
  L = __builtin_fmaxf(0.0f, __builtin_fminf(L, MaxLevel));
  uint32_t Level = (uint32_t)(L + 0.5f);
  return Level > Img->MipLevels - 1 ? Img->MipLevels - 1 : Level;
}

// Halves `BaseExtent` `Level` times (standard mip-chain downsampling),
// floored to a minimum of 1: mip level `Level`'s width/height, given the
// base (level 0) extent.
__attribute__((always_inline)) static uint32_t
femeRTMipExtent(uint32_t BaseExtent, uint32_t Level) {
  uint32_t Extent = BaseExtent >> Level;
  return Extent == 0 ? 1 : Extent;
}

// The four address-mode-resolved texel corners and fractional weights a 2D
// bilinear sample at normalized coordinates `(U, V)` blends between, texel
// centers offset by half a texel per the standard "texel center at
// `i + 0.5`" convention (matching Direct3D and Vulkan's sampling rules).
typedef struct {
  int32_t X0, X1, Y0, Y1;
  _Bool BorderX0, BorderX1, BorderY0, BorderY1;
  float Wx, Wy; // Fractional weight toward the X1/Y1 corner.
} FemeRTBilinearSupport;

__attribute__((always_inline)) static FemeRTBilinearSupport
femeRTComputeBilinearSupport(const FemeRTImageDescriptor *Img, float U, float V,
                             const FemeRTSamplerDescriptor *Samp,
                             uint32_t Level) {
  uint32_t LevelWidth = femeRTMipExtent(Img->Width, Level);
  uint32_t LevelHeight = femeRTMipExtent(Img->Height, Level);
  float TexelU = U * (float)LevelWidth - 0.5f;
  float TexelV = V * (float)LevelHeight - 0.5f;
  float FloorU = __builtin_floorf(TexelU);
  float FloorV = __builtin_floorf(TexelV);
  int32_t BaseX = (int32_t)FloorU;
  int32_t BaseY = (int32_t)FloorV;

  FemeRTBilinearSupport S;
  S.Wx = TexelU - FloorU;
  S.Wy = TexelV - FloorV;
  S.BorderX0 = S.BorderX1 = S.BorderY0 = S.BorderY1 = 0;
  S.X0 = femeRTApplyAddressMode(BaseX, (int32_t)LevelWidth, Samp->AddressU,
                                &S.BorderX0);
  S.X1 = femeRTApplyAddressMode(BaseX + 1, (int32_t)LevelWidth, Samp->AddressU,
                                &S.BorderX1);
  S.Y0 = femeRTApplyAddressMode(BaseY, (int32_t)LevelHeight, Samp->AddressV,
                                &S.BorderY0);
  S.Y1 = femeRTApplyAddressMode(BaseY + 1, (int32_t)LevelHeight, Samp->AddressV,
                                &S.BorderY1);
  return S;
}

// Point-samples (nearest texel) `Img` at `(U, V)`.
__attribute__((always_inline)) static FemeRTv4f32
femeRTSamplePoint2D(const FemeRTImageDescriptor *Img,
                    const FemeRTSamplerDescriptor *Samp, float U, float V,
                    uint32_t Level) {
  uint32_t LevelWidth = femeRTMipExtent(Img->Width, Level);
  uint32_t LevelHeight = femeRTMipExtent(Img->Height, Level);
  int32_t X = (int32_t)__builtin_floorf(U * (float)LevelWidth);
  int32_t Y = (int32_t)__builtin_floorf(V * (float)LevelHeight);
  _Bool BorderX = 0, BorderY = 0;
  int32_t AddrX =
      femeRTApplyAddressMode(X, (int32_t)LevelWidth, Samp->AddressU, &BorderX);
  int32_t AddrY =
      femeRTApplyAddressMode(Y, (int32_t)LevelHeight, Samp->AddressV, &BorderY);
  return femeRTFetchTexel2D(Img, Level, AddrX, AddrY, BorderX || BorderY,
                            Samp->BorderColor);
}

// Bilinearly filters `Img` at `(U, V)`, blending the four texels
// `femeRTComputeBilinearSupport` selects.
__attribute__((always_inline)) static FemeRTv4f32
femeRTSampleLinear2D(const FemeRTImageDescriptor *Img,
                     const FemeRTSamplerDescriptor *Samp, float U, float V,
                     uint32_t Level) {
  FemeRTBilinearSupport S =
      femeRTComputeBilinearSupport(Img, U, V, Samp, Level);
  FemeRTv4f32 T00 = femeRTFetchTexel2D(
      Img, Level, S.X0, S.Y0, S.BorderX0 || S.BorderY0, Samp->BorderColor);
  FemeRTv4f32 T10 = femeRTFetchTexel2D(
      Img, Level, S.X1, S.Y0, S.BorderX1 || S.BorderY0, Samp->BorderColor);
  FemeRTv4f32 T01 = femeRTFetchTexel2D(
      Img, Level, S.X0, S.Y1, S.BorderX0 || S.BorderY1, Samp->BorderColor);
  FemeRTv4f32 T11 = femeRTFetchTexel2D(
      Img, Level, S.X1, S.Y1, S.BorderX1 || S.BorderY1, Samp->BorderColor);
  FemeRTv4f32 Top = T00 + (T10 - T00) * S.Wx;
  FemeRTv4f32 Bottom = T01 + (T11 - T01) * S.Wx;
  return Top + (Bottom - Top) * S.Wy;
}

// Applies `SamplerCompareFunc` `Func` as `Ref Func StoredTexel` (Direct3D's
// `SamplerComparisonFunc`/Vulkan's `VkCompareOp` convention), returning
// `1.0f` for pass and `0.0f` for fail.
__attribute__((always_inline)) static float
femeRTApplyCompare(uint32_t Func, float Ref, float Texel) {
  switch (Func) {
  case 0: // Never
    return 0.0f;
  case 1: // Less
    return Ref < Texel ? 1.0f : 0.0f;
  case 2: // Equal
    return Ref == Texel ? 1.0f : 0.0f;
  case 3: // LessEqual
    return Ref <= Texel ? 1.0f : 0.0f;
  case 4: // Greater
    return Ref > Texel ? 1.0f : 0.0f;
  case 5: // NotEqual
    return Ref != Texel ? 1.0f : 0.0f;
  case 6: // GreaterEqual
    return Ref >= Texel ? 1.0f : 0.0f;
  case 7: // Always
    return 1.0f;
  default:
    return 0.0f;
  }
}

// `feme.cpu.image.sample.2d.v4f32`: samples a 2D sampled image (an SRV-like
// texture, `FEME_IMAGE_SAMPLED == 1 << 0`) at normalized coordinates
// `(U, V)`, using `Samp`'s magnification filter (`MagFilter`) to choose
// point or bilinear filtering and `Lod`/`UseExplicitLod` to choose the mip
// level (see `femeRTSelectMipLevel`). An inactive lane, an unsampled or
// unwritten image, reads as zero (see "Bounds checking").
FemeRTv4f32 femeCpuImageSample2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float Lod,
    _Bool UseExplicitLod, _Bool Mask) asm("feme.cpu.image.sample.2d.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageSample2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float Lod,
    _Bool UseExplicitLod, _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  uint32_t Level =
      femeRTSelectMipLevel(&Img, Lod, UseExplicitLod, Samp.MipFilter);
  return Samp.MagFilter == 1 // SamplerFilter::Linear.
             ? femeRTSampleLinear2D(&Img, &Samp, U, V, Level)
             : femeRTSamplePoint2D(&Img, &Samp, U, V, Level);
}

// `feme.cpu.image.samplecmp.2d.f32`: depth-comparison samples a 2D sampled
// image, comparing `Dref` against each fetched texel's first (depth)
// component via `Samp->CompareFunc`, then filters the per-texel 0/1
// comparison results with the same point/bilinear weights a color sample
// would use -- hardware "percentage-closer filtering" behaviour, not a
// filtered depth value compared once.
float femeCpuImageSampleCmp2DF32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float Lod,
    _Bool UseExplicitLod, float Dref,
    _Bool Mask) asm("feme.cpu.image.samplecmp.2d.f32");

__attribute__((always_inline)) float femeCpuImageSampleCmp2DF32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float Lod,
    _Bool UseExplicitLod, float Dref, _Bool Mask) {
  if (!Mask)
    return 0.0f;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return 0.0f;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  uint32_t Level =
      femeRTSelectMipLevel(&Img, Lod, UseExplicitLod, Samp.MipFilter);

  if (Samp.MagFilter != 1) { // Point (nearest).
    uint32_t LevelWidth = femeRTMipExtent(Img.Width, Level);
    uint32_t LevelHeight = femeRTMipExtent(Img.Height, Level);
    int32_t X = (int32_t)__builtin_floorf(U * (float)LevelWidth);
    int32_t Y = (int32_t)__builtin_floorf(V * (float)LevelHeight);
    _Bool BorderX = 0, BorderY = 0;
    int32_t AddrX =
        femeRTApplyAddressMode(X, (int32_t)LevelWidth, Samp.AddressU, &BorderX);
    int32_t AddrY = femeRTApplyAddressMode(Y, (int32_t)LevelHeight,
                                           Samp.AddressV, &BorderY);
    FemeRTv4f32 T = femeRTFetchTexel2D(&Img, Level, AddrX, AddrY,
                                       BorderX || BorderY, Samp.BorderColor);
    return femeRTApplyCompare(Samp.CompareFunc, Dref, T[0]);
  }

  FemeRTBilinearSupport S =
      femeRTComputeBilinearSupport(&Img, U, V, &Samp, Level);
  FemeRTv4f32 T00 = femeRTFetchTexel2D(
      &Img, Level, S.X0, S.Y0, S.BorderX0 || S.BorderY0, Samp.BorderColor);
  FemeRTv4f32 T10 = femeRTFetchTexel2D(
      &Img, Level, S.X1, S.Y0, S.BorderX1 || S.BorderY0, Samp.BorderColor);
  FemeRTv4f32 T01 = femeRTFetchTexel2D(
      &Img, Level, S.X0, S.Y1, S.BorderX0 || S.BorderY1, Samp.BorderColor);
  FemeRTv4f32 T11 = femeRTFetchTexel2D(
      &Img, Level, S.X1, S.Y1, S.BorderX1 || S.BorderY1, Samp.BorderColor);
  float C00 = femeRTApplyCompare(Samp.CompareFunc, Dref, T00[0]);
  float C10 = femeRTApplyCompare(Samp.CompareFunc, Dref, T10[0]);
  float C01 = femeRTApplyCompare(Samp.CompareFunc, Dref, T01[0]);
  float C11 = femeRTApplyCompare(Samp.CompareFunc, Dref, T11[0]);
  float Top = C00 + (C10 - C00) * S.Wx;
  float Bottom = C01 + (C11 - C01) * S.Wx;
  return Top + (Bottom - Top) * S.Wy;
}

// `feme.cpu.image.load.2d.v4f32`: reads one texel of a 2D image (sampled or
// storage) at integer coordinates `(X, Y)` and explicit mip `Mip`, with no
// sampler, no addressing mode and no filtering (DXIL's `Load`/Vulkan's
// `OpImageFetch`/`OpImageRead`): an out-of-range coordinate reads as zero
// rather than applying any address mode, since there is no sampler to
// supply one.
FemeRTv4f32
femeCpuImageLoad2DV4F32(const FemeRTImageDescriptor *ImageHeap,
                        uint32_t ImageHeapCount, uint32_t ImageIndex, int32_t X,
                        int32_t Y, uint32_t Mip,
                        _Bool Mask) asm("feme.cpu.image.load.2d.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageLoad2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, uint32_t Mip, _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data)
    return Zero;
  if (X < 0 || Y < 0 || (uint32_t)X >= Img.Width || (uint32_t)Y >= Img.Height)
    return Zero;
  static const float NoBorder[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  return femeRTFetchTexel2D(&Img, Mip, X, Y, /*UseBorder=*/0, NoBorder);
}

// `feme.cpu.image.load.2d.v4i32` (roadmap E26): the integer-format
// counterpart of `feme.cpu.image.load.2d.v4f32` above -- same bounds
// checking and no-sampler/no-filtering semantics, decoded through
// `femeRTUnpackImageTexelI32`'s `_UINT`/`_SINT` table instead of
// `femeRTUnpackImageTexel`'s float one.
FemeRTv4i32
femeCpuImageLoad2DV4I32(const FemeRTImageDescriptor *ImageHeap,
                        uint32_t ImageHeapCount, uint32_t ImageIndex, int32_t X,
                        int32_t Y, uint32_t Mip,
                        _Bool Mask) asm("feme.cpu.image.load.2d.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuImageLoad2DV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, uint32_t Mip, _Bool Mask) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data)
    return Zero;
  if (X < 0 || Y < 0 || (uint32_t)X >= Img.Width || (uint32_t)Y >= Img.Height)
    return Zero;
  return femeRTFetchTexel2DI32(&Img, Mip, X, Y);
}
