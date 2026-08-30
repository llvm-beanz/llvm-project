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
// Update (roadmap F8b): the single-component depth (`D16_UNORM`/
// `D32_FLOAT`) and stencil (`S8_UINT`) formats are now decoded too --
// `feme::vulkan::buildSubpassInputHeap` (CommandBuffer.cpp) has been
// feeding a depth/stencil attachment's own `FemeImageDescriptor` into this
// same `feme.cpu.image.load.2d.v4f32` path since roadmap F8a, but every
// such fetch read back all-zero until now: neither
// `femeRTImageFormatElementSize` nor `femeRTUnpackImageTexel` had a case
// for any of the three, so the format-guarded `ElemSize == 0` check in
// `femeRTFetchTexel2D` silently rejected every access. `D16_UNORM`/
// `S8_UINT` normalize to `[0.0, 1.0]` in component 0 (matching
// `A8_UNORM`'s own normalized-component-0 convention above); `D32_FLOAT`
// is the identity case, like `R32_FLOAT`. A missing color/alpha component
// pads the same way every other single- or few-component format above
// does. `femeRTFetchTexel2D` also now derives its per-texel stride from
// `SampleStride`/`SampleCount` rather than assuming `SampleCount == 1`
// (`FemeImageSubresourceLayout::SampleStride`, previously unused --
// `buildSubpassInputHeap` now populates a multisample attachment's heap
// slot too), always reading sample 0 of a multisampled texel: no caller
// yet threads an explicit sample index through (`feme::StageOpKind::
// SubpassLoad` has no `Sample` operand, and `SubpassLoadPattern`
// (SPIRVToLLVMPatterns.cpp) rejects any `spirv.ImageRead` that carries
// one), so `dynamicRenderingLocalReadMultisampledAttachments` stayed
// `VK_FALSE` -- tracked as roadmap F8c.
//
// Update (roadmap F8c): `femeRTFetchTexel2D` and `feme.cpu.image.load.2d.
// v4f32` both gained an explicit `Sample` parameter, added to
// `Layout->Offset`'s existing `SampleStride`-aware address as
// `Sample * Layout->SampleStride` -- every other caller (point/bilinear
// sampling, the integer `Load2DI32` path) still passes a constant `0`,
// since none of them has a real per-sample index of its own to thread
// through; only `FragmentWrapper.cpp`'s `lowerFragmentSubpassLoad` (fed by
// `feme::StageOpKind::SubpassLoad`'s new `sample` operand and
// `SubpassLoadPattern`'s new `Sample` image-operand case,
// SPIRVToLLVMPatterns.cpp) now passes a real, possibly non-zero one.
//
// Update (roadmap H7b-a): `femeRTFetchTexel2D`/`femeRTFetchTexel2DI32` both
// gained an explicit `Layer` parameter, added to `Layout->Offset` as
// `Layer * Layout->SlicePitch` -- the same per-array-layer addressing
// roadmap H7b's own `materializeImageDescriptor` widening already uses
// host-side, reused here runtime-side. Every existing 2D (non-arrayed)
// caller still passes a constant `0`. Six new exported entry points build
// on this: `feme.cpu.image.sample.2darray.v4f32`/`.load.2darray.v4f32`/
// `.v4i32` (a `Texture2DArray`'s own explicit integer, or rounded-nearest
// float, array layer) and `.sample.cube.v4f32`/`.sample.cubearray.v4f32`
// (a `TextureCube`/`TextureCubeArray`'s direction-vector coordinate,
// converted to a face index plus 2D UV by the classic "major axis"
// algorithm, `femeRTSelectCubeFace` below, then addressed as an ordinary
// `Layer` -- `Face` for a plain cube, `CubeIndex * 6 + Face` for a cube
// array -- since a cube(array) is purely a view-level addressing
// convention over an ordinary 2D-array-shaped image, never a distinct
// physical layout of its own (see FeMeVulkanDesign.md's H7b update). No
// depth-comparison (`samplecmp`) counterpart is added for any of the five:
// SPIR-V's SPIR-V-to-LLVM conversion (SPIRVResourceLowering.cpp) does not
// lower a depth-comparison sample for *any* dimension yet, 2D included, so
// there is no existing shape to generalize from.
//
// Update (roadmap H19a): a plain 2D storage image (SPIR-V `OpTypeImage`
// with `Sampled == 2`, "used without a sampler") is now writable, not just
// readable -- `feme.cpu.image.load.2d.v4f32`/`.v4i32` already read one
// (see their own comments: "sampled or storage"), but no
// `feme.cpu.image.store.*` entry point existed for `OpImageWrite` to lower
// to (see `SPIRVResourceLowering.cpp`'s own former `classifySampledImage2DHandle`
// comment). Two new entry points, `feme.cpu.image.store.2d.v4f32`/`.v4i32`
// (`femeCpuImageStore2DV4F32`/`V4I32` below), and their own
// `femeRTStoreTexel2D`/`femeRTStoreTexel2DI32` helpers -- the write-side
// mirror of `femeRTFetchTexel2D`/`femeRTFetchTexel2DI32`, always level 0,
// layer 0, sample 0 (a storage image access never carries a mip, array
// layer or multisample index in the scope this row covers) -- pack a
// `<4 x float>`/`<4 x i32>` value back into a texel's own bytes via two new
// `femeRTPackImageTexel`/`femeRTPackImageTexelI32` functions, the write-side
// mirror of `femeRTUnpackImageTexel`/`femeRTUnpackImageTexelI32`. Scoped, for
// now, to exactly the Vulkan spec's own mandatory storage-image format
// floor (`VkPhysicalDeviceFeatures::shaderStorageImageExtendedFormats`
// unset): `R32G32B32A32_{SFLOAT,UINT,SINT}` and `R32_{SFLOAT,UINT,SINT}`.
// `R32_UINT`/`R32_SINT` (mandatory for storage images, but not for a
// *sampled* image, so roadmap E26 above never added them) gain their own
// `femeRTImageFormatElementSize`/`femeRTUnpackImageTexelI32` cases here too
// -- a small, pre-existing, adjacent gap this row's own format floor
// depends on closing to be honest (both previously silently decoded as
// all-zero, `ElemSize == 0`, exactly like every other unmodeled format).
// Every other format `femeRTUnpackImageTexel(I32)` already decodes stays
// read-only through a storage-image handle: `femeRTPackImageTexel(I32)`
// has no case for it, so a write silently drops (a real gap, not modeled
// as an error here, tracked as roadmap H19b/H19c/H19d's own follow-on
// scope alongside `shaderStorageImageExtendedFormats` itself) -- kept
// unreachable in practice by `Format.cpp`'s own `VK_FORMAT_FEATURE_
// STORAGE_IMAGE_BIT`, only now set for this same six-format floor.
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

// The narrower `vec2`/`vec3` counterparts of `FemeRTv4f32` above (roadmap
// H6g-b-a-i-a-i-c): a GLSL `vec2`/`vec3` mesh-shader input/output element
// compiles down to LLVM IR's `<2 x float>`/`<3 x float>`, un-padded (unlike
// e.g. a `<4 x float>`, a `<3 x float>`'s IR/memory layout is exactly three
// packed `float`s, not four), so both need their own raw-load/store
// overload rather than reusing `FemeRTv4f32`'s.
typedef float FemeRTv2f32 __attribute__((vector_size(8)));
typedef float FemeRTv2f32Unaligned __attribute__((vector_size(8), aligned(4)));
typedef float FemeRTv3f32 __attribute__((vector_size(12)));
typedef float FemeRTv3f32Unaligned
    __attribute__((vector_size(12), aligned(4)));

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

// The narrower `ivec2`/`ivec3` counterparts of `FemeRTv4i32` above, matching
// `FemeRTv2f32`/`FemeRTv3f32`'s un-padded `<2 x i32>`/`<3 x i32>` shape
// (roadmap H6g-b-a-i-a-i-c).
typedef int32_t FemeRTv2i32 __attribute__((vector_size(8)));
typedef int32_t FemeRTv2i32Unaligned
    __attribute__((vector_size(8), aligned(4)));
typedef int32_t FemeRTv3i32 __attribute__((vector_size(12)));
typedef int32_t FemeRTv3i32Unaligned
    __attribute__((vector_size(12), aligned(4)));

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

// `feme.cpu.resource.load.raw.v4f32`: read a whole `<4 x float>` (e.g. a
// GLSL `vec4` member of a uniform/storage block) through a bindless raw or
// structured buffer descriptor in one call -- the same descriptor/bounds
// check the scalar `.f32` variant above performs, just for a 16-byte
// element instead of a 4-byte one. `feme::cpu::mangleResourceCallName`
// already mangles any fixed-vector element type generically (see
// ResourceCalls.cpp), so `feme::cpu::SPIRVResourceLoweringPass` was always
// able to *emit* a call to this name; only this runtime definition itself
// was missing (roadmap H3a: this was the last of a chain of latent gaps
// H3a's `gl_ViewportIndex`-as-fragment-input repro surfaced, the actual
// CTS cases needing a whole-`vec4` load out of a `uniform Colors { vec4
// color[N]; }` block rather than a single scalar).
FemeRTv4f32
femeCpuResourceLoadRawV4F32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                            uint32_t DescriptorIndex, uint64_t ByteOffset,
                            _Bool Mask) asm("feme.cpu.resource.load.raw.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuResourceLoadRawV4F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 16);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 16);
  if (!((OkRaw || OkStructured) && Mask))
    return (FemeRTv4f32){0.0f, 0.0f, 0.0f, 0.0f};
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  return *(const FemeRTv4f32Unaligned *)Ptr;
}

void femeCpuResourceStoreRawV4F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv4f32 Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.v4f32");

__attribute__((always_inline)) void femeCpuResourceStoreRawV4F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv4f32 Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 16);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 16);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!((OkRaw || OkStructured) && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  *(FemeRTv4f32Unaligned *)Ptr = (FemeRTv4f32Unaligned)Value;
}

// `feme.cpu.resource.load.raw.v2f32`/`.v3f32`/`.v2i32`/`.v3i32`/`.v4i32`
// (roadmap H6g-b-a-i-a-i-c): the narrower-than-`vec4` and integer-vector
// raw-buffer-load/store overloads a `vec2`/`vec3`/`ivec2`/`ivec3`/`ivec4`
// mesh-shader input/output (e.g. a whole-`vec2` load out of a `uniform Foo
// { vec2 v[N]; }` block) actually needs -- `feme::cpu::mangleResourceCallName`
// already mangles any fixed-vector element type generically (see
// ResourceCalls.cpp), so `feme::cpu::ResourceLoweringPass` was always able
// to *emit* a call to these names; only these runtime definitions
// themselves, mirroring `V4F32` above's bindless-descriptor-lookup-then-
// masked-load/store shape one byte width at a time, were missing.
FemeRTv2f32
femeCpuResourceLoadRawV2F32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                            uint32_t DescriptorIndex, uint64_t ByteOffset,
                            _Bool Mask) asm("feme.cpu.resource.load.raw.v2f32");

__attribute__((always_inline)) FemeRTv2f32 femeCpuResourceLoadRawV2F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 8);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 8);
  if (!((OkRaw || OkStructured) && Mask))
    return (FemeRTv2f32){0.0f, 0.0f};
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  return *(const FemeRTv2f32Unaligned *)Ptr;
}

void femeCpuResourceStoreRawV2F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv2f32 Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.v2f32");

__attribute__((always_inline)) void femeCpuResourceStoreRawV2F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv2f32 Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 8);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 8);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!((OkRaw || OkStructured) && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  *(FemeRTv2f32Unaligned *)Ptr = (FemeRTv2f32Unaligned)Value;
}

FemeRTv3f32
femeCpuResourceLoadRawV3F32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                            uint32_t DescriptorIndex, uint64_t ByteOffset,
                            _Bool Mask) asm("feme.cpu.resource.load.raw.v3f32");

__attribute__((always_inline)) FemeRTv3f32 femeCpuResourceLoadRawV3F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 12);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 12);
  if (!((OkRaw || OkStructured) && Mask))
    return (FemeRTv3f32){0.0f, 0.0f, 0.0f};
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  return *(const FemeRTv3f32Unaligned *)Ptr;
}

void femeCpuResourceStoreRawV3F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv3f32 Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.v3f32");

__attribute__((always_inline)) void femeCpuResourceStoreRawV3F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv3f32 Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 12);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 12);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!((OkRaw || OkStructured) && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  // A plain `*(FemeRTv3f32Unaligned *)Ptr = ...` store here gets widened by
  // the optimizer into a 16-byte `<4 x float>` store (observed at -O2):
  // Clang's ABI lowering already receives `Value` coerced into a `<4 x
  // i32>` register pair, and folds the coerced-to-`<3 x float>` conversion
  // back into a single over-wide store instead of one that writes exactly
  // the 12 bytes `femeRTCheckAccess` above actually bounds-checked -- an
  // out-of-bounds write past a buffer's last element. `__builtin_memcpy`
  // with the explicit literal `12` (not `sizeof(Value)`, which is 16:
  // Clang pads a `<3 x float>`'s storage size up to the next power of two)
  // keeps the copy to exactly those 12 bytes.
  __builtin_memcpy(Ptr, &Value, 12);
}

FemeRTv2i32
femeCpuResourceLoadRawV2I32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                            uint32_t DescriptorIndex, uint64_t ByteOffset,
                            _Bool Mask) asm("feme.cpu.resource.load.raw.v2i32");

__attribute__((always_inline)) FemeRTv2i32 femeCpuResourceLoadRawV2I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 8);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 8);
  if (!((OkRaw || OkStructured) && Mask))
    return (FemeRTv2i32){0, 0};
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  return *(const FemeRTv2i32Unaligned *)Ptr;
}

void femeCpuResourceStoreRawV2I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv2i32 Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.v2i32");

__attribute__((always_inline)) void femeCpuResourceStoreRawV2I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv2i32 Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 8);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 8);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!((OkRaw || OkStructured) && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  *(FemeRTv2i32Unaligned *)Ptr = (FemeRTv2i32Unaligned)Value;
}

FemeRTv3i32
femeCpuResourceLoadRawV3I32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                            uint32_t DescriptorIndex, uint64_t ByteOffset,
                            _Bool Mask) asm("feme.cpu.resource.load.raw.v3i32");

__attribute__((always_inline)) FemeRTv3i32 femeCpuResourceLoadRawV3I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 12);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 12);
  if (!((OkRaw || OkStructured) && Mask))
    return (FemeRTv3i32){0, 0, 0};
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  return *(const FemeRTv3i32Unaligned *)Ptr;
}

void femeCpuResourceStoreRawV3I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv3i32 Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.v3i32");

__attribute__((always_inline)) void femeCpuResourceStoreRawV3I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv3i32 Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 12);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 12);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!((OkRaw || OkStructured) && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  // See `femeCpuResourceStoreRawV3F32`'s comment: `__builtin_memcpy` with
  // the explicit literal `12` (not `sizeof(Value)`, which is 16) avoids the
  // same out-of-bounds store-widening risk for the integer `<3 x i32>`
  // overload.
  __builtin_memcpy(Ptr, &Value, 12);
}

FemeRTv4i32
femeCpuResourceLoadRawV4I32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                            uint32_t DescriptorIndex, uint64_t ByteOffset,
                            _Bool Mask) asm("feme.cpu.resource.load.raw.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuResourceLoadRawV4I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 16);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 16);
  if (!((OkRaw || OkStructured) && Mask))
    return (FemeRTv4i32){0, 0, 0, 0};
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  return *(const FemeRTv4i32Unaligned *)Ptr;
}

void femeCpuResourceStoreRawV4I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv4i32 Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.v4i32");

__attribute__((always_inline)) void femeCpuResourceStoreRawV4I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv4i32 Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 16);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 16);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!((OkRaw || OkStructured) && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  *(FemeRTv4i32Unaligned *)Ptr = (FemeRTv4i32Unaligned)Value;
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
  // (Roadmap H19a) `R32_UINT`/`R32_SINT`: mandatory for a storage image
  // (unlike a *sampled* one, so roadmap E26 above never needed them), one
  // 32-bit scalar each, matching `R32_FLOAT`'s own size.
  case 5:  // R32_UINT
  case 9:  // R32_SINT
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
  // (Roadmap F8b) Single-component depth/stencil formats, as
  // `feme::vulkan::buildSubpassInputHeap` feeds them: a pure depth or
  // pure stencil attachment is one of these, never a combined format (see
  // `feme::graphics::DepthStencilAttachment`'s own comment).
  case 31: // D16_UNORM
    return 2;
  case 32: // D32_FLOAT
    return 4;
  case 35: // S8_UINT
    return 1;
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
  // `femeRTHalfToFloat` decodes it once its `5 + MantBits`-bit field (
  // exponent and mantissa together, packed contiguously from the LSB up)
  // is left-shifted by `10 - MantBits` -- placing the mantissa's top bits
  // at binary16's own mantissa field's own top bits (bits `10 - MantBits`
  // through 9) and the exponent right above it (bits 10 through 14),
  // exactly where binary16 expects it. (roadmap H18: this shift was
  // previously `11 - MantBits`, one bit too many -- pushing the exponent's
  // own top bit as high as binary16's *sign* bit and corrupting every
  // non-zero value; e.g. this format's own `1.0` decoded as `32768.0`.
  // Verified against the format's mantissa/exponent field widths this
  // function's own header comment already documented correctly -- only
  // the shift amount itself was wrong.)
  uint32_t R10 = ((Raw & 0x7FFu) << 4) & 0xFFFFu; // 6-bit mantissa -> 10.
  uint32_t G10 = (((Raw >> 11) & 0x7FFu) << 4) & 0xFFFFu;
  uint32_t B10 = (((Raw >> 22) & 0x3FFu) << 5) & 0xFFFFu; // 5-bit mantissa.
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
  case 31: { // D16_UNORM (roadmap F8b)
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    FemeRTv4f32 V = {(float)Raw / 65535.0f, 0.0f, 0.0f, 1.0f};
    return V;
  }
  case 32: { // D32_FLOAT (roadmap F8b): the identity case, like R32_FLOAT.
    float F;
    __builtin_memcpy(&F, Ptr, sizeof(F));
    FemeRTv4f32 V = {F, 0.0f, 0.0f, 1.0f};
    return V;
  }
  case 35: { // S8_UINT (roadmap F8b)
    FemeRTv4f32 V = {(float)*Ptr / 255.0f, 0.0f, 0.0f, 1.0f};
    return V;
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
  // (Roadmap H19a) `R32_UINT`/`R32_SINT`: a single 32-bit scalar,
  // reinterpreted directly like `R32G32B32A32_{UINT,SINT}` above; the
  // unread G/B components pad `0`, alpha pads `1`, matching
  // `femeRTUnpackImageTexel`'s own partial-component convention
  // (`R32_FLOAT` et al.).
  case 5:  // R32_UINT
  case 9: { // R32_SINT
    int32_t R;
    __builtin_memcpy(&R, Ptr, sizeof(R));
    FemeRTv4i32 V = {R, 0, 0, 1};
    return V;
  }
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

// Packs \p Texel back into `Format`'s own bytes at \p Ptr -- the write-side
// mirror of `femeRTUnpackImageTexel` above, for `feme.cpu.image.store.2d.
// v4f32` (roadmap H19a). Scoped to exactly the two float-channel formats
// the Vulkan spec's own mandatory storage-image format floor requires
// (`VkPhysicalDeviceFeatures::shaderStorageImageExtendedFormats` unset):
// `R32_FLOAT` and `R32G32B32A32_FLOAT`, both the identity case (no scalar
// conversion, unlike e.g. `R8G8B8A8_UNORM`'s own quantizing pack this
// function does not yet need to mirror). A write through any other format
// -- reachable only if a future row widens `Format.cpp`'s own
// `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT` gate beyond this floor without
// widening this switch to match -- is silently dropped, mirroring
// `femeRTUnpackImageTexel`'s own "all-zero for an unmodeled format"
// default rather than trapping.
__attribute__((always_inline)) static void
femeRTPackImageTexel(uint32_t Format, unsigned char *Ptr, FemeRTv4f32 Texel) {
  switch (Format) {
  case 1: { // R32_FLOAT: only the first component is stored.
    float R = Texel[0];
    __builtin_memcpy(Ptr, &R, sizeof(R));
    return;
  }
  case 4: // R32G32B32A32_FLOAT: identity format, no conversion.
    *(FemeRTv4f32Unaligned *)Ptr = (FemeRTv4f32Unaligned)Texel;
    return;
  default:
    return;
  }
}

// The integer counterpart of `femeRTPackImageTexel` above, covering the
// mandatory storage-image format floor's two integer-channel formats,
// `R32_UINT`/`R32_SINT` and `R32G32B32A32_UINT`/`_SINT` -- both identity
// formats (a signed/unsigned 32-bit scalar's bit pattern is stored as-is
// either way), mirroring `femeRTUnpackImageTexelI32`'s own read-side
// treatment of the same four formats.
__attribute__((always_inline)) static void
femeRTPackImageTexelI32(uint32_t Format, unsigned char *Ptr,
                        FemeRTv4i32 Texel) {
  switch (Format) {
  case 5:  // R32_UINT
  case 9: { // R32_SINT: only the first component is stored.
    int32_t R = Texel[0];
    __builtin_memcpy(Ptr, &R, sizeof(R));
    return;
  }
  case 8:  // R32G32B32A32_UINT
  case 12: // R32G32B32A32_SINT: identity format, no conversion.
    *(FemeRTv4i32Unaligned *)Ptr = (FemeRTv4i32Unaligned)Texel;
    return;
  default:
    return;
  }
}


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

// Reads one texel at integer coordinates `(X, Y)`, array layer `Layer`,
// sample `Sample`, of mip level `Level` of `Img`, or `BorderColor` if
// `UseBorder` is set (a `ClampToBorder` axis resolved out of range), or
// all-zero for any other unreadable access (no image bound, `Level` beyond
// `MipLayoutCount`, `Layer` at or beyond `Img->ArrayLayers`, an
// unrecognized format, or an access `femeRTImageFormatElementSize`/the mip
// layout's own `SizeInBytes` bound rejects) -- the same "out-of-range reads
// zero" rule buffers use (see "Bounds checking").
//
// Roadmap H7b-a: `Layer` selects one of `Img->ArrayLayers` array layers via
// `Layer * Layout->SlicePitch`, the same per-layer addressing
// `Image::texelPointer` (Image.cpp) and roadmap H7b's own descriptor-
// materialization widening already use -- a `TextureCube`/`TextureCubeArray`
// descriptor addresses its six-faces-per-array-element the identical way a
// plain `Texture2DArray` addresses its layers (see FeMeVulkanDesign.md's
// "cube(array) is a view-level addressing convention" note), so this one
// widening covers every arrayed 2D-shaped dimension uniformly. Every caller
// that reads a non-arrayed image (plain `Texture2D`) still passes a
// constant `0`, exactly like the roadmap F8c `Sample` parameter's own
// "every non-multisampled caller passes 0" convention.
//
// Roadmap F8b/F8c: a multisampled `Img` (`SampleCount > 1`) packs every
// sample of one texel contiguously (`Layout->SampleStride == ElemSize`, see
// Image.cpp's `computeSubresourceLayouts`), so stepping to the next texel
// along a row has to skip `SampleCount` samples, not one, and `Sample`
// (out of range for `Img->SampleCount`, the caller's responsibility to
// bound -- every caller today either passes a constant `0` or a
// `subpassLoad`-supplied index already checked against the bound
// attachment's own real sample count) selects which of those contiguous
// samples this fetch reads, via `Sample * Layout->SampleStride`.
__attribute__((always_inline)) static FemeRTv4f32
femeRTFetchTexel2D(const FemeRTImageDescriptor *Img, uint32_t Level,
                   uint32_t Layer, int32_t X, int32_t Y, uint32_t Sample,
                   _Bool UseBorder, const float BorderColor[4]) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (UseBorder) {
    FemeRTv4f32 Border = {BorderColor[0], BorderColor[1], BorderColor[2],
                          BorderColor[3]};
    return Border;
  }
  if (!Img->Data || Level >= Img->MipLayoutCount || Layer >= Img->ArrayLayers)
    return Zero;
  uint64_t ElemSize = femeRTImageFormatElementSize(Img->Format);
  if (ElemSize == 0)
    return Zero;
  const FemeRTImageSubresourceLayout *Layout = &Img->MipLayouts[Level];
  uint64_t TexelStride = Layout->SampleStride != 0
                             ? (uint64_t)Img->SampleCount * Layout->SampleStride
                             : ElemSize;
  uint64_t SampleOffset = (uint64_t)Sample * Layout->SampleStride;
  uint64_t Offset = Layout->Offset + (uint64_t)Layer * Layout->SlicePitch +
                    (uint64_t)Y * Layout->RowPitch +
                    (uint64_t)X * TexelStride + SampleOffset;
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
// mode, so there is no `ClampToBorder` case to honor here either. Shares
// `femeRTFetchTexel2D`'s roadmap F8b multisample-stride fix and roadmap
// H7b-a array-layer widening (see that function's own comments).
__attribute__((always_inline)) static FemeRTv4i32
femeRTFetchTexel2DI32(const FemeRTImageDescriptor *Img, uint32_t Level,
                      uint32_t Layer, int32_t X, int32_t Y) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Img->Data || Level >= Img->MipLayoutCount || Layer >= Img->ArrayLayers)
    return Zero;
  uint64_t ElemSize = femeRTImageFormatElementSize(Img->Format);
  if (ElemSize == 0)
    return Zero;
  const FemeRTImageSubresourceLayout *Layout = &Img->MipLayouts[Level];
  uint64_t TexelStride = Layout->SampleStride != 0
                             ? (uint64_t)Img->SampleCount * Layout->SampleStride
                             : ElemSize;
  uint64_t Offset = Layout->Offset + (uint64_t)Layer * Layout->SlicePitch +
                    (uint64_t)Y * Layout->RowPitch +
                    (uint64_t)X * TexelStride;
  if (Offset + ElemSize > Img->SizeInBytes)
    return Zero;
  const unsigned char *Ptr = (const unsigned char *)Img->Data + Offset;
  return femeRTUnpackImageTexelI32(Img->Format, Ptr);
}

// Writes \p Texel to the texel at integer coordinates `(X, Y)`, mip level
// 0, array layer 0, sample 0 of \p Img -- the write-side mirror of
// `femeRTFetchTexel2D` above, for `feme.cpu.image.store.2d.v4f32` (roadmap
// H19a). Scoped to a plain, non-arrayed, single-sample storage image (see
// `classifyStorageImage2DHandle`'s own comment, `SPIRVResourceLowering.cpp`),
// so unlike `femeRTFetchTexel2D` there is no `Level`/`Layer`/`Sample`
// parameter to take -- every write this row's own shader-side lowering
// produces addresses exactly one image, one mip, one layer, one sample. A
// write past the image's own bounds, an unrecognized format
// (`femeRTImageFormatElementSize` returning 0), or an empty (never bound)
// descriptor is silently dropped rather than trapping, mirroring
// `femeRTFetchTexel2D`'s own out-of-range read returning zero rather than
// erroring.
__attribute__((always_inline)) static void
femeRTStoreTexel2D(const FemeRTImageDescriptor *Img, int32_t X, int32_t Y,
                   FemeRTv4f32 Texel) {
  if (!Img->Data || Img->MipLayoutCount == 0 || Img->ArrayLayers == 0)
    return;
  if (X < 0 || Y < 0 || (uint32_t)X >= Img->Width || (uint32_t)Y >= Img->Height)
    return;
  uint64_t ElemSize = femeRTImageFormatElementSize(Img->Format);
  if (ElemSize == 0)
    return;
  const FemeRTImageSubresourceLayout *Layout = &Img->MipLayouts[0];
  uint64_t TexelStride = Layout->SampleStride != 0
                             ? (uint64_t)Img->SampleCount * Layout->SampleStride
                             : ElemSize;
  uint64_t Offset =
      Layout->Offset + (uint64_t)Y * Layout->RowPitch + (uint64_t)X * TexelStride;
  if (Offset + ElemSize > Img->SizeInBytes)
    return;
  unsigned char *Ptr = (unsigned char *)Img->Data + Offset;
  femeRTPackImageTexel(Img->Format, Ptr, Texel);
}

// The integer counterpart of `femeRTStoreTexel2D` above, for
// `feme.cpu.image.store.2d.v4i32` (roadmap H19a).
__attribute__((always_inline)) static void
femeRTStoreTexel2DI32(const FemeRTImageDescriptor *Img, int32_t X, int32_t Y,
                      FemeRTv4i32 Texel) {
  if (!Img->Data || Img->MipLayoutCount == 0 || Img->ArrayLayers == 0)
    return;
  if (X < 0 || Y < 0 || (uint32_t)X >= Img->Width || (uint32_t)Y >= Img->Height)
    return;
  uint64_t ElemSize = femeRTImageFormatElementSize(Img->Format);
  if (ElemSize == 0)
    return;
  const FemeRTImageSubresourceLayout *Layout = &Img->MipLayouts[0];
  uint64_t TexelStride = Layout->SampleStride != 0
                             ? (uint64_t)Img->SampleCount * Layout->SampleStride
                             : ElemSize;
  uint64_t Offset =
      Layout->Offset + (uint64_t)Y * Layout->RowPitch + (uint64_t)X * TexelStride;
  if (Offset + ElemSize > Img->SizeInBytes)
    return;
  unsigned char *Ptr = (unsigned char *)Img->Data + Offset;
  femeRTPackImageTexelI32(Img->Format, Ptr, Texel);
}


// either an explicit-LOD sample's own `Lod` operand or (`UseExplicitLod`
// false) an implicit-LOD sample's `Lod == 0.0` starting point (a caller
// that already derived a real implicit LOD from screen-space derivatives,
// e.g. `femeRTPlanImplicitLod`, instead calls this with its own derived
// value and `UseExplicitLod` true -- there is only one biased/clamped-LOD
// concept in the spec, not a separate one per source). Shared by
// `femeRTSelectMipLevels` (which mip level(s) `lod'` selects) and
// `femeRTUseLinearFilter` below (whether `lod'` itself is a magnifying or
// minifying sample) so the two decisions can never disagree about which
// LOD they are looking at.
__attribute__((always_inline)) static float
femeRTComputeClampedLod(float Lod, _Bool UseExplicitLod,
                        const FemeRTSamplerDescriptor *Samp) {
  float L = UseExplicitLod ? Lod : 0.0f;
  L += Samp->LodBias;
  return __builtin_fmaxf(Samp->MinLod, __builtin_fminf(L, Samp->MaxLod));
}

// (Roadmap H17) The two adjacent integer mip levels a real trilinear
// (`mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR`) sample blends between,
// plus the fractional weight toward the coarser of the two. `Level0`/
// `Level1` are already clamped to `Img`'s own valid level range, so
// `Level0 == Level1` at either end of the mip chain (`ClampedLod <= 0` or
// `>= MipLevels - 1`) is a valid degenerate case a caller can shortcut on
// (blending a level with itself is a same-value no-op, but not worth the
// extra sample).
typedef struct {
  uint32_t Level0, Level1;
  float Frac; // Fractional weight toward Level1, in [0, 1].
} FemeRTMipTrilinearPlan;

__attribute__((always_inline)) static FemeRTMipTrilinearPlan
femeRTSelectMipLevels(const FemeRTImageDescriptor *Img, float ClampedLod) {
  FemeRTMipTrilinearPlan Plan;
  if (Img->MipLevels == 0) {
    Plan.Level0 = Plan.Level1 = 0;
    Plan.Frac = 0.0f;
    return Plan;
  }
  float MaxLevel = (float)(Img->MipLevels - 1);
  float L = __builtin_fmaxf(0.0f, __builtin_fminf(ClampedLod, MaxLevel));
  float Floor = __builtin_floorf(L);
  Plan.Level0 = (uint32_t)Floor;
  Plan.Frac = L - Floor;
  uint32_t Level1 = Plan.Level0 + 1;
  Plan.Level1 = Level1 > Img->MipLevels - 1 ? Img->MipLevels - 1 : Level1;
  return Plan;
}

// (Roadmap H17) The single mip level a `mipmapMode=NEAREST` sampler reads
// from `Plan` (`femeRTSelectMipLevels`) -- `Level0` or `Level1`, whichever
// `Plan.Frac`'s own fractional part is closer to (a tie, `Frac == 0.5`,
// rounds up to `Level1`, matching this file's own pre-H17 `(uint32_t)(L +
// 0.5f)` rounding exactly). `femeRTSelectMipLevels` itself only ever
// floors to `Level0`; picking the single nearest level, rather than
// always the floor, is what `mipmapMode=NEAREST` -- as opposed to
// `LINEAR`'s own two-level blend -- means.
__attribute__((always_inline)) static uint32_t
femeRTNearestMipLevel(FemeRTMipTrilinearPlan Plan) {
  return Plan.Frac < 0.5f ? Plan.Level0 : Plan.Level1;
}

// (Roadmap H16) Chooses point vs. bilinear filtering for a sample at
// `ClampedLod` (`femeRTComputeClampedLod`'s own output), per the Vulkan
// spec's own magnification/minification filter-selection rule: `MagFilter`
// when `ClampedLod <= 0` (the sample is magnifying -- more than one screen
// pixel per texel), `MinFilter` otherwise (minifying), applied uniformly
// whether the sample's LOD was an explicit operand or derived implicitly
// from screen-space derivatives. Every sampling entry point in this file
// previously read `Samp->MagFilter` unconditionally regardless of this
// distinction, silently bilinear- or point-filtering a minifying sample
// with the *magnification* filter whenever a sampler's `minFilter` and
// `magFilter` disagreed (e.g. `VK_FILTER_NEAREST`/`VK_FILTER_LINEAR`) --
// confirmed via a real re-run of `dEQP-VK.texture.filtering.2d.combinations.*`
// that every failing case has exactly this `minFilter != magFilter` shape.
__attribute__((always_inline)) static _Bool
femeRTUseLinearFilter(float ClampedLod, const FemeRTSamplerDescriptor *Samp) {
  uint32_t Filter = ClampedLod <= 0.0f ? Samp->MagFilter : Samp->MinFilter;
  return Filter == 1; // SamplerFilter::Linear.
}

// Halves `BaseExtent` `Level` times (standard mip-chain downsampling),
// floored to a minimum of 1: mip level `Level`'s width/height, given the
// base (level 0) extent.
__attribute__((always_inline)) static uint32_t
femeRTMipExtent(uint32_t BaseExtent, uint32_t Level) {
  uint32_t Extent = BaseExtent >> Level;
  return Extent == 0 ? 1 : Extent;
}

// A fast, approximate base-2 logarithm of a positive, finite, normal
// `float`, used to turn `femeRTPlanImplicitLod`'s texel-space scale factor
// into a level-of-detail value -- exact at each power of two, and (roadmap
// H17) no longer just linear-enough-to-round-away in between: a real
// trilinear (`mipmapMode == Linear`) blend now consumes this value's own
// fractional part directly (`femeRTSelectMipLevels`), so this
// approximation's own small mid-octave error (this well-known "float-as-
// int" reinterpretation technique) shows up as a real, if minor, blend-
// weight inaccuracy rather than being rounded away -- acceptable for a
// software rasterizer's own conformance tolerance, but worth calling out
// as a real approximation, not an exact `log2`, now that its precision is
// directly observable. This file is compiled freestanding (see
// `femeRTHalfToFloat`'s own comment), so it reinterprets the value's own
// IEEE-754 bit pattern directly rather than call a transcendental libm
// routine this build cannot assume exists.
__attribute__((always_inline)) static float femeRTFastLog2(float X) {
  uint32_t Bits;
  __builtin_memcpy(&Bits, &X, sizeof(Bits));
  float Y = (float)Bits;
  return Y * (1.0f / 8388608.0f) - 126.94269504f;
}

// The mip level and (when the sampler enables anisotropic filtering) the
// multi-tap anisotropic footprint an implicit-LOD 2D color sample reads,
// computed from the caller's own screen-space partial derivatives of the
// normalized `(U, V)` coordinate (roadmap H7i) -- the standard OpenGL/
// Direct3D "scale factor" construction (see the OpenGL spec's "Scale
// Factor and Level of Detail"): the texel-space footprint's two screen-
// axis extents `Px`/`Py`, `Pmax`/`Pmin` their max/min, the isotropic LOD
// `log2(Pmax)` an ordinary (non-anisotropic) implicit sample would use,
// and -- only when anisotropy is enabled and the footprint is not already
// isotropic (`Pmin` unequal to `Pmax`) -- a `TapCount` (bounded by
// `MaxAnisotropy`) of bilinear taps spread along the major (most-
// minified) screen axis's own UV gradient, each reading a less-minified
// `log2(Pmax / TapCount)` level than the isotropic choice, matching how
// hardware anisotropic filtering trades extra same-level taps for a
// sharper per-tap level. `TapCount` is always at least 1 -- a plain
// isotropic sample -- so a caller with no anisotropy configured, or with
// no measurable minification along one axis, degenerates to the ordinary
// single-tap path unchanged.
typedef struct {
  uint32_t TapCount;
  float StepU, StepV; // Per-tap UV offset along the major axis.
  // (Roadmap H16/H17) The biased/clamped LOD this plan was derived from --
  // callers pass this to `femeRTSampleFiltered2D`, which uses it to decide
  // both the mag/min filter (`femeRTUseLinearFilter`) and, when `Samp`
  // enables trilinear filtering, which two adjacent mip levels to blend
  // (`femeRTSelectMipLevels`) for each of this plan's own taps.
  float ClampedLod;
} FemeRTImplicitLodPlan;

__attribute__((always_inline)) static FemeRTImplicitLodPlan
femeRTPlanImplicitLod(const FemeRTImageDescriptor *Img,
                     const FemeRTSamplerDescriptor *Samp, float DUdX,
                     float DUdY, float DVdX, float DVdY) {
  float Ux = DUdX * (float)Img->Width, Uy = DUdY * (float)Img->Width;
  float Vx = DVdX * (float)Img->Height, Vy = DVdY * (float)Img->Height;
  float Px = __builtin_sqrtf(Ux * Ux + Vx * Vx);
  float Py = __builtin_sqrtf(Uy * Uy + Vy * Vy);
  float Pmax = Px > Py ? Px : Py;
  float Pmin = Px < Py ? Px : Py;

  FemeRTImplicitLodPlan Plan;
  Plan.TapCount = 1;
  Plan.StepU = 0.0f;
  Plan.StepV = 0.0f;

  float Lod;
  if (Pmax <= 0.0f) {
    Lod = 0.0f; // No measurable minification (e.g. no derivatives at all).
  } else {
    _Bool AnisoEnabled =
        (Samp->Flags & 2u) != 0 && // FEME_SAMPLER_ANISOTROPY_ENABLE.
        Samp->MaxAnisotropy > 1.0f;
    if (AnisoEnabled && Pmax > Pmin) {
      // `Pmin == 0` (the footprint has no extent at all along its minor
      // axis -- e.g. a surface viewed edge-on, so one axis's own
      // derivatives are exactly zero) is the maximally anisotropic case,
      // capped by `MaxAnisotropy` like any other; `N` would otherwise be
      // an infinite (divide-by-zero) ratio.
      float N = Pmin > 0.0f ? Pmax / Pmin : Samp->MaxAnisotropy;
      if (N > Samp->MaxAnisotropy)
        N = Samp->MaxAnisotropy;
      uint32_t TapCount = (uint32_t)(N + 0.5f);
      if (TapCount < 1)
        TapCount = 1;
      Plan.TapCount = TapCount;
      Lod = femeRTFastLog2(Pmax / (float)TapCount);
      if (TapCount > 1) {
        _Bool MajorIsX = Px >= Py;
        float StepScale = 1.0f / (float)TapCount;
        Plan.StepU = (MajorIsX ? DUdX : DUdY) * StepScale;
        Plan.StepV = (MajorIsX ? DVdX : DVdY) * StepScale;
      }
    } else {
      Lod = femeRTFastLog2(Pmax);
    }
  }

  Plan.ClampedLod = femeRTComputeClampedLod(Lod, /*UseExplicitLod=*/1, Samp);
  return Plan;
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

// Point-samples (nearest texel) `Img` at `(U, V)`, array layer `Layer`
// (roadmap H7b-a; always `0` for a non-arrayed image).
__attribute__((always_inline)) static FemeRTv4f32
femeRTSamplePoint2D(const FemeRTImageDescriptor *Img,
                    const FemeRTSamplerDescriptor *Samp, float U, float V,
                    uint32_t Level, uint32_t Layer) {
  uint32_t LevelWidth = femeRTMipExtent(Img->Width, Level);
  uint32_t LevelHeight = femeRTMipExtent(Img->Height, Level);
  int32_t X = (int32_t)__builtin_floorf(U * (float)LevelWidth);
  int32_t Y = (int32_t)__builtin_floorf(V * (float)LevelHeight);
  _Bool BorderX = 0, BorderY = 0;
  int32_t AddrX =
      femeRTApplyAddressMode(X, (int32_t)LevelWidth, Samp->AddressU, &BorderX);
  int32_t AddrY =
      femeRTApplyAddressMode(Y, (int32_t)LevelHeight, Samp->AddressV, &BorderY);
  return femeRTFetchTexel2D(Img, Level, Layer, AddrX, AddrY, /*Sample=*/0,
                            BorderX || BorderY, Samp->BorderColor);
}

// Bilinearly filters `Img` at `(U, V)`, array layer `Layer` (roadmap
// H7b-a; always `0` for a non-arrayed image), blending the four texels
// `femeRTComputeBilinearSupport` selects.
__attribute__((always_inline)) static FemeRTv4f32
femeRTSampleLinear2D(const FemeRTImageDescriptor *Img,
                     const FemeRTSamplerDescriptor *Samp, float U, float V,
                     uint32_t Level, uint32_t Layer) {
  FemeRTBilinearSupport S =
      femeRTComputeBilinearSupport(Img, U, V, Samp, Level);
  FemeRTv4f32 T00 = femeRTFetchTexel2D(
      Img, Level, Layer, S.X0, S.Y0, /*Sample=*/0, S.BorderX0 || S.BorderY0,
      Samp->BorderColor);
  FemeRTv4f32 T10 = femeRTFetchTexel2D(
      Img, Level, Layer, S.X1, S.Y0, /*Sample=*/0, S.BorderX1 || S.BorderY0,
      Samp->BorderColor);
  FemeRTv4f32 T01 = femeRTFetchTexel2D(
      Img, Level, Layer, S.X0, S.Y1, /*Sample=*/0, S.BorderX0 || S.BorderY1,
      Samp->BorderColor);
  FemeRTv4f32 T11 = femeRTFetchTexel2D(
      Img, Level, Layer, S.X1, S.Y1, /*Sample=*/0, S.BorderX1 || S.BorderY1,
      Samp->BorderColor);
  FemeRTv4f32 Top = T00 + (T10 - T00) * S.Wx;
  FemeRTv4f32 Bottom = T01 + (T11 - T01) * S.Wx;
  return Top + (Bottom - Top) * S.Wy;
}

// (Roadmap H17) Filters `Img` at `(U, V)`, array layer `Layer`, given an
// already biased-and-clamped level-of-detail value `ClampedLod`
// (`femeRTComputeClampedLod`) -- the one place every sampling entry point
// below (2D, 2D-array, cube, cube-array, and each anisotropic tap) makes
// both filter-mode decisions the Vulkan spec's own image level-of-detail
// operation calls for: `femeRTUseLinearFilter` picks `MagFilter` vs.
// `MinFilter` (roadmap H16) to filter texels *within* a level, and (this
// row) `Samp->MipFilter == Linear` blends the two adjacent levels
// `femeRTSelectMipLevels` selects, weighted by `ClampedLod`'s own
// fractional part -- real trilinear filtering, rather than always
// rounding to a single nearest level (`Samp->MipFilter == Nearest`, or a
// single-level image, degenerates to exactly that single-level read, the
// pre-H17 behavior).
__attribute__((always_inline)) static FemeRTv4f32
femeRTSampleFiltered2D(const FemeRTImageDescriptor *Img,
                       const FemeRTSamplerDescriptor *Samp, float U, float V,
                       uint32_t Layer, float ClampedLod) {
  _Bool UseLinear = femeRTUseLinearFilter(ClampedLod, Samp);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(Img, ClampedLod);
  _Bool Trilinear = Samp->MipFilter == 1 && MipPlan.Level0 != MipPlan.Level1;
  uint32_t Level0 = Trilinear ? MipPlan.Level0 : femeRTNearestMipLevel(MipPlan);
  FemeRTv4f32 Lo = UseLinear
                       ? femeRTSampleLinear2D(Img, Samp, U, V, Level0, Layer)
                       : femeRTSamplePoint2D(Img, Samp, U, V, Level0, Layer);
  if (!Trilinear)
    return Lo;
  FemeRTv4f32 Hi =
      UseLinear ? femeRTSampleLinear2D(Img, Samp, U, V, MipPlan.Level1, Layer)
                : femeRTSamplePoint2D(Img, Samp, U, V, MipPlan.Level1, Layer);
  return Lo + (Hi - Lo) * MipPlan.Frac;
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
// `(U, V)`, using `Samp`'s `MagFilter` or `MinFilter` (roadmap H16; picked
// per-sample by `femeRTUseLinearFilter`, per the Vulkan spec's own
// magnification/minification rule, not always `MagFilter`) to choose
// point or bilinear filtering within a mip level, and `Samp`'s `MipFilter`
// (roadmap H17; `femeRTSampleFiltered2D`) to choose between reading a
// single nearest level or blending the two adjacent levels the LOD falls
// between. An explicit-LOD sample (`UseExplicitLod`) reads `Lod`'s own
// level(s) directly; an implicit-LOD one instead derives its own level --
// and, when `Samp` enables anisotropic filtering, a multi-tap anisotropic
// footprint -- from the caller's own screen-space partial derivatives of
// `(U, V)` (`DUdX`/`DUdY`/`DVdX`/`DVdY`, roadmap H7i; see
// `femeRTPlanImplicitLod`), ignoring `Lod` entirely in that case. An
// inactive lane, an unsampled or unwritten image, reads as zero (see
// "Bounds checking").
FemeRTv4f32 femeCpuImageSample2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float DUdX,
    float DUdY, float DVdX, float DVdY, float Lod, _Bool UseExplicitLod,
    _Bool Mask) asm("feme.cpu.image.sample.2d.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageSample2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float DUdX,
    float DUdY, float DVdX, float DVdY, float Lod, _Bool UseExplicitLod,
    _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);

  if (UseExplicitLod) {
    float ClampedLod =
        femeRTComputeClampedLod(Lod, /*UseExplicitLod=*/1, &Samp);
    return femeRTSampleFiltered2D(&Img, &Samp, U, V, /*Layer=*/0, ClampedLod);
  }

  FemeRTImplicitLodPlan Plan =
      femeRTPlanImplicitLod(&Img, &Samp, DUdX, DUdY, DVdX, DVdY);
  if (Plan.TapCount <= 1)
    return femeRTSampleFiltered2D(&Img, &Samp, U, V, /*Layer=*/0,
                                  Plan.ClampedLod);

  // Anisotropic footprint: average `Plan.TapCount` same-level taps spread
  // symmetrically along the major axis, centered on `(U, V)` so the mean
  // sample point is exactly the original coordinate.
  FemeRTv4f32 Sum = {0.0f, 0.0f, 0.0f, 0.0f};
  float FirstOffset = -0.5f * (float)(Plan.TapCount - 1);
  for (uint32_t Tap = 0; Tap != Plan.TapCount; ++Tap) {
    float Offset = FirstOffset + (float)Tap;
    float TapU = U + Offset * Plan.StepU;
    float TapV = V + Offset * Plan.StepV;
    Sum += femeRTSampleFiltered2D(&Img, &Samp, TapU, TapV, /*Layer=*/0,
                                  Plan.ClampedLod);
  }
  return Sum * (1.0f / (float)Plan.TapCount);
}

// `feme.cpu.image.samplecmp.2d.f32`: depth-comparison samples a 2D sampled
// image, comparing `Dref` against each fetched texel's first (depth)
// component via `Samp->CompareFunc`, then filters the per-texel 0/1
// comparison results with the same point/bilinear weights a color sample
// would use -- hardware "percentage-closer filtering" behaviour, not a
// filtered depth value compared once.
// (Roadmap H17) The single-level body of `femeCpuImageSampleCmp2DF32`
// below, factored out so the caller can call it once per level and
// trilinearly blend the two results, the same way `femeRTSampleFiltered2D`
// blends an ordinary color sample's two levels.
__attribute__((always_inline)) static float
femeRTSampleCmp2DAtLevel(const FemeRTImageDescriptor *Img,
                         const FemeRTSamplerDescriptor *Samp, float U, float V,
                         uint32_t Level, float Dref, _Bool UseLinear) {
  if (!UseLinear) { // Point (nearest).
    uint32_t LevelWidth = femeRTMipExtent(Img->Width, Level);
    uint32_t LevelHeight = femeRTMipExtent(Img->Height, Level);
    int32_t X = (int32_t)__builtin_floorf(U * (float)LevelWidth);
    int32_t Y = (int32_t)__builtin_floorf(V * (float)LevelHeight);
    _Bool BorderX = 0, BorderY = 0;
    int32_t AddrX = femeRTApplyAddressMode(X, (int32_t)LevelWidth,
                                           Samp->AddressU, &BorderX);
    int32_t AddrY = femeRTApplyAddressMode(Y, (int32_t)LevelHeight,
                                           Samp->AddressV, &BorderY);
    FemeRTv4f32 T =
        femeRTFetchTexel2D(Img, Level, /*Layer=*/0, AddrX, AddrY,
                           /*Sample=*/0, BorderX || BorderY, Samp->BorderColor);
    return femeRTApplyCompare(Samp->CompareFunc, Dref, T[0]);
  }

  FemeRTBilinearSupport S =
      femeRTComputeBilinearSupport(Img, U, V, Samp, Level);
  FemeRTv4f32 T00 =
      femeRTFetchTexel2D(Img, Level, /*Layer=*/0, S.X0, S.Y0, /*Sample=*/0,
                         S.BorderX0 || S.BorderY0, Samp->BorderColor);
  FemeRTv4f32 T10 =
      femeRTFetchTexel2D(Img, Level, /*Layer=*/0, S.X1, S.Y0, /*Sample=*/0,
                         S.BorderX1 || S.BorderY0, Samp->BorderColor);
  FemeRTv4f32 T01 =
      femeRTFetchTexel2D(Img, Level, /*Layer=*/0, S.X0, S.Y1, /*Sample=*/0,
                         S.BorderX0 || S.BorderY1, Samp->BorderColor);
  FemeRTv4f32 T11 =
      femeRTFetchTexel2D(Img, Level, /*Layer=*/0, S.X1, S.Y1, /*Sample=*/0,
                         S.BorderX1 || S.BorderY1, Samp->BorderColor);
  float C00 = femeRTApplyCompare(Samp->CompareFunc, Dref, T00[0]);
  float C10 = femeRTApplyCompare(Samp->CompareFunc, Dref, T10[0]);
  float C01 = femeRTApplyCompare(Samp->CompareFunc, Dref, T01[0]);
  float C11 = femeRTApplyCompare(Samp->CompareFunc, Dref, T11[0]);
  float Top = C00 + (C10 - C00) * S.Wx;
  float Bottom = C01 + (C11 - C01) * S.Wx;
  return Top + (Bottom - Top) * S.Wy;
}

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
  float ClampedLod = femeRTComputeClampedLod(Lod, UseExplicitLod, &Samp);
  _Bool UseLinear = femeRTUseLinearFilter(ClampedLod, &Samp);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(&Img, ClampedLod);
  _Bool Trilinear = Samp.MipFilter == 1 && MipPlan.Level0 != MipPlan.Level1;
  uint32_t Level0 = Trilinear ? MipPlan.Level0 : femeRTNearestMipLevel(MipPlan);
  float Lo =
      femeRTSampleCmp2DAtLevel(&Img, &Samp, U, V, Level0, Dref, UseLinear);
  if (!Trilinear)
    return Lo;
  float Hi = femeRTSampleCmp2DAtLevel(&Img, &Samp, U, V, MipPlan.Level1, Dref,
                                      UseLinear);
  return Lo + (Hi - Lo) * MipPlan.Frac;
}

// `feme.cpu.image.load.2d.v4f32`: reads one texel of a 2D image (sampled or
// storage) at integer coordinates `(X, Y)`, sample `Sample` (roadmap F8c;
// always `0` for a single-sample image or a caller with no per-sample
// index of its own -- see `ImageCalls.h`'s `createLoad2D` comment), and
// explicit mip `Mip`, with no sampler, no addressing mode and no filtering
// (DXIL's `Load`/Vulkan's `OpImageFetch`/`OpImageRead`): an out-of-range
// coordinate reads as zero rather than applying any address mode, since
// there is no sampler to supply one.
FemeRTv4f32
femeCpuImageLoad2DV4F32(const FemeRTImageDescriptor *ImageHeap,
                        uint32_t ImageHeapCount, uint32_t ImageIndex, int32_t X,
                        int32_t Y, uint32_t Mip, uint32_t Sample,
                        _Bool Mask) asm("feme.cpu.image.load.2d.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageLoad2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, uint32_t Mip, uint32_t Sample,
    _Bool Mask) {
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
  return femeRTFetchTexel2D(&Img, Mip, /*Layer=*/0, X, Y, Sample,
                            /*UseBorder=*/0, NoBorder);
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
  return femeRTFetchTexel2DI32(&Img, Mip, /*Layer=*/0, X, Y);
}

// `feme.cpu.image.store.2d.v4f32` (roadmap H19a): writes one texel of a 2D
// storage image at integer coordinates `(X, Y)`, mip level 0, array layer
// 0, sample 0 (see `femeRTStoreTexel2D`'s own scope comment) -- Vulkan's
// `OpImageWrite`. An unbound (`!Img.Data`) or masked-off write is silently
// dropped, mirroring `femeCpuImageLoad2DV4F32`'s own unbound-read-as-zero
// treatment.
void femeCpuImageStore2DV4F32(const FemeRTImageDescriptor *ImageHeap,
                              uint32_t ImageHeapCount, uint32_t ImageIndex,
                              int32_t X, int32_t Y, FemeRTv4f32 Texel,
                              _Bool Mask) asm("feme.cpu.image.store.2d.v4f32");

__attribute__((always_inline)) void femeCpuImageStore2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, FemeRTv4f32 Texel,
    _Bool Mask) {
  if (!Mask)
    return;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  femeRTStoreTexel2D(&Img, X, Y, Texel);
}

// `feme.cpu.image.store.2d.v4i32` (roadmap H19a): the integer-format
// counterpart of `feme.cpu.image.store.2d.v4f32` above.
void femeCpuImageStore2DV4I32(const FemeRTImageDescriptor *ImageHeap,
                              uint32_t ImageHeapCount, uint32_t ImageIndex,
                              int32_t X, int32_t Y, FemeRTv4i32 Texel,
                              _Bool Mask) asm("feme.cpu.image.store.2d.v4i32");

__attribute__((always_inline)) void femeCpuImageStore2DV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, FemeRTv4i32 Texel,
    _Bool Mask) {
  if (!Mask)
    return;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  femeRTStoreTexel2DI32(&Img, X, Y, Texel);
}

// Rounds `Value` to the nearest integer (per the Vulkan spec's "array
// layer... rounded to the nearest integer" rule for a sampled array
// layer) and clamps it to `[0, Count - 1]`, the same way an out-of-range
// mip level or address-mode axis already clamps rather than reading zero.
// `Count == 0` (a malformed or as-yet-uninitialized image descriptor)
// returns 0 to avoid an unsigned underflow computing `Count - 1`.
__attribute__((always_inline)) static uint32_t
femeRTRoundClampLayer(uint32_t Count, float Value) {
  if (Count == 0)
    return 0;
  float Rounded = __builtin_floorf(Value + 0.5f);
  if (Rounded < 0.0f)
    return 0;
  uint32_t Layer = (uint32_t)Rounded;
  return Layer >= Count ? Count - 1 : Layer;
}

// `feme.cpu.image.sample.2darray.v4f32` (roadmap H7b-a): the
// `Texture2DArray` counterpart of `feme.cpu.image.sample.2d.v4f32` above
// -- identical (U, V) filtering, plus `ArrayLayer` (SPIR-V's own arrayed-
// sample coordinate convention: a float, rounded to nearest and clamped
// to a valid layer by `femeRTRoundClampLayer` above).
FemeRTv4f32 femeCpuImageSample2DArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V,
    float ArrayLayer, float Lod, _Bool UseExplicitLod,
    _Bool Mask) asm("feme.cpu.image.sample.2darray.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageSample2DArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V,
    float ArrayLayer, float Lod, _Bool UseExplicitLod, _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  float ClampedLod = femeRTComputeClampedLod(Lod, UseExplicitLod, &Samp);
  uint32_t Layer = femeRTRoundClampLayer(Img.ArrayLayers, ArrayLayer);
  return femeRTSampleFiltered2D(&Img, &Samp, U, V, Layer, ClampedLod);
}

// `feme.cpu.image.load.2darray.v4f32` (roadmap H7b-a): the
// `Texture2DArray` counterpart of `feme.cpu.image.load.2d.v4f32` above --
// same bounds checking and no-sampler/no-filtering semantics, plus an
// explicit integer `Layer` (SPIR-V's `OpImageFetch` array-layer coordinate
// is always an integer, never a float, unlike the sampled path above).
FemeRTv4f32 femeCpuImageLoad2DArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Layer, uint32_t Mip,
    uint32_t Sample, _Bool Mask) asm("feme.cpu.image.load.2darray.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageLoad2DArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Layer, uint32_t Mip,
    uint32_t Sample, _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data)
    return Zero;
  if (X < 0 || Y < 0 || Layer < 0 || (uint32_t)X >= Img.Width ||
      (uint32_t)Y >= Img.Height || (uint32_t)Layer >= Img.ArrayLayers)
    return Zero;
  static const float NoBorder[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  return femeRTFetchTexel2D(&Img, Mip, (uint32_t)Layer, X, Y, Sample,
                            /*UseBorder=*/0, NoBorder);
}

// `feme.cpu.image.load.2darray.v4i32` (roadmap H7b-a): the integer-format
// counterpart of `feme.cpu.image.load.2darray.v4f32` above, mirroring how
// `feme.cpu.image.load.2d.v4i32` relates to `feme.cpu.image.load.2d.v4f32`.
FemeRTv4i32 femeCpuImageLoad2DArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Layer, uint32_t Mip,
    _Bool Mask) asm("feme.cpu.image.load.2darray.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuImageLoad2DArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Layer, uint32_t Mip,
    _Bool Mask) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data)
    return Zero;
  if (X < 0 || Y < 0 || Layer < 0 || (uint32_t)X >= Img.Width ||
      (uint32_t)Y >= Img.Height || (uint32_t)Layer >= Img.ArrayLayers)
    return Zero;
  return femeRTFetchTexel2DI32(&Img, Mip, (uint32_t)Layer, X, Y);
}

// The classic "major axis" cube-face-selection algorithm (Vulkan spec
// 16.3.3 "Cube Map Face Selection and Transformations", matching OpenGL
// and Direct3D's identical convention), converting a direction vector
// into which of the six faces (Vulkan's own array-layer order for a cube
// image view -- +X, -X, +Y, -Y, +Z, -Z, faces 0-5) it projects onto and
// the normalized (U, V) coordinate within that face. `Major` (the
// largest-magnitude component) is guaranteed nonzero for any
// non-degenerate direction; a literal zero vector (never produced by a
// real shader's own normalized direction, but not undefined behavior
// either) resolves to face 0 with `Major` clamped away from zero below
// to avoid a division by zero.
typedef struct {
  uint32_t Face;
  float U, V;
} FemeRTCubeFace;

__attribute__((always_inline)) static FemeRTCubeFace
femeRTSelectCubeFace(float X, float Y, float Z) {
  float AbsX = __builtin_fabsf(X), AbsY = __builtin_fabsf(Y),
        AbsZ = __builtin_fabsf(Z);
  FemeRTCubeFace R;
  float Major, U, V;
  if (AbsX >= AbsY && AbsX >= AbsZ) {
    Major = AbsX;
    if (X > 0.0f) {
      R.Face = 0; // +X.
      U = -Z;
      V = -Y;
    } else {
      R.Face = 1; // -X.
      U = Z;
      V = -Y;
    }
  } else if (AbsY >= AbsX && AbsY >= AbsZ) {
    Major = AbsY;
    if (Y > 0.0f) {
      R.Face = 2; // +Y.
      U = X;
      V = Z;
    } else {
      R.Face = 3; // -Y.
      U = X;
      V = -Z;
    }
  } else {
    Major = AbsZ;
    if (Z > 0.0f) {
      R.Face = 4; // +Z.
      U = X;
      V = -Y;
    } else {
      R.Face = 5; // -Z.
      U = -X;
      V = -Y;
    }
  }
  if (Major == 0.0f)
    Major = 1.0f; // Degenerate direction: avoid a division by zero.
  R.U = 0.5f * (U / Major + 1.0f);
  R.V = 0.5f * (V / Major + 1.0f);
  return R;
}

// `feme.cpu.image.sample.cube.v4f32` (roadmap H7b-a): samples a
// `TextureCube` sampled image at direction vector `(DirX, DirY, DirZ)`,
// converted to a face index (addressed as `femeRTSamplePoint2D`/
// `femeRTSampleLinear2D`'s own `Layer` parameter) and 2D UV by
// `femeRTSelectCubeFace` above. A cube face's own edges are always
// clamped, regardless of the bound sampler's own address mode: Vulkan,
// Direct3D, and OpenGL alike never wrap or mirror across a cube face
// boundary the way a plain 2D image's row/column wraps -- there is no
// "next" face along a U/V axis.
FemeRTv4f32 femeCpuImageSampleCubeV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float Lod, _Bool UseExplicitLod,
    _Bool Mask) asm("feme.cpu.image.sample.cube.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageSampleCubeV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float Lod, _Bool UseExplicitLod, _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u) || Img.ArrayLayers < 6) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  Samp.AddressU = 2; // ClampToEdge -- see comment above.
  Samp.AddressV = 2;
  float ClampedLod = femeRTComputeClampedLod(Lod, UseExplicitLod, &Samp);
  FemeRTCubeFace CF = femeRTSelectCubeFace(DirX, DirY, DirZ);
  return femeRTSampleFiltered2D(&Img, &Samp, CF.U, CF.V, CF.Face, ClampedLod);
}

// `feme.cpu.image.sample.cubearray.v4f32` (roadmap H7b-a): the
// `TextureCubeArray` counterpart of `feme.cpu.image.sample.cube.v4f32`
// above, adding `ArrayLayer` (SPIR-V's own arrayed-cube coordinate
// convention: a float selecting which six-layer cube element of the
// array, rounded to nearest and clamped by `femeRTRoundClampLayer`).
// `Img.ArrayLayers` is the whole array's own total layer count (roadmap
// H7b's own descriptor materialization already reports this, e.g. 12 for
// a two-element cube array); dividing by 6 recovers the number of
// selectable cube elements.
FemeRTv4f32 femeCpuImageSampleCubeArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float ArrayLayer, float Lod, _Bool UseExplicitLod,
    _Bool Mask) asm("feme.cpu.image.sample.cubearray.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageSampleCubeArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float ArrayLayer, float Lod, _Bool UseExplicitLod,
    _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u) || Img.ArrayLayers < 6)
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  Samp.AddressU = 2; // ClampToEdge -- see femeCpuImageSampleCubeV4F32.
  Samp.AddressV = 2;
  float ClampedLod = femeRTComputeClampedLod(Lod, UseExplicitLod, &Samp);
  FemeRTCubeFace CF = femeRTSelectCubeFace(DirX, DirY, DirZ);
  uint32_t NumCubes = Img.ArrayLayers / 6;
  uint32_t CubeIndex = femeRTRoundClampLayer(NumCubes, ArrayLayer);
  uint32_t Layer = CubeIndex * 6 + CF.Face;
  return femeRTSampleFiltered2D(&Img, &Samp, CF.U, CF.V, Layer, ClampedLod);
}
