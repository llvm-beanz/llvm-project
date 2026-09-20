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

// (Roadmap H124c) The `half`-element (`_Float16`) counterparts of
// `FemeRTv2f32`/`FemeRTv3f32`/`FemeRTv4f32` above, needed for an HLSL
// `half2`/`half3`/`half4` raw/structured-buffer load or store (e.g. the
// `*.fp16.test` `WaveOps` cases' own input buffers) -- only ever loaded or
// stored via `__builtin_memcpy`, never used in arithmetic here, so this
// bitcode's own deliberate avoidance of hardware half-float instructions
// (see `femeRTFloatToHalf`'s own comment) does not apply: a bare `memcpy`
// of a `_Float16`'s bit pattern needs no float-conversion support from the
// target at all.
typedef _Float16 FemeRTv2f16 __attribute__((vector_size(4)));
typedef _Float16 FemeRTv2f16Unaligned
    __attribute__((vector_size(4), aligned(2)));
typedef _Float16 FemeRTv3f16 __attribute__((vector_size(6)));
typedef _Float16 FemeRTv3f16Unaligned
    __attribute__((vector_size(6), aligned(2)));
typedef _Float16 FemeRTv4f16 __attribute__((vector_size(8)));
typedef _Float16 FemeRTv4f16Unaligned
    __attribute__((vector_size(8), aligned(2)));

// (Roadmap H124c) The `int16_t`/`uint16_t`-element counterparts of the
// `FemeRTv*f16` types above, needed for an HLSL `int16_t2`/`int16_t3`/
// `int16_t4` (or `uint16_t`) raw/structured-buffer load or store (e.g.
// `WaveOps/*.int16.test`'s own input buffers, the sibling gap to
// `*.fp16.test` above -- `appendScalarMangling` (ResourceCalls.cpp) mangles
// any integer type generically as `i<bitwidth>`, so a 16-bit-int element
// hits this exact same missing-runtime-definition gap).
typedef int16_t FemeRTv2i16 __attribute__((vector_size(4)));
typedef int16_t FemeRTv2i16Unaligned
    __attribute__((vector_size(4), aligned(2)));
typedef int16_t FemeRTv3i16 __attribute__((vector_size(6)));
typedef int16_t FemeRTv3i16Unaligned
    __attribute__((vector_size(6), aligned(2)));
typedef int16_t FemeRTv4i16 __attribute__((vector_size(8)));
typedef int16_t FemeRTv4i16Unaligned
    __attribute__((vector_size(8), aligned(2)));

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

// Unpacks a `R8_UNORM` value (one normalized `[0, 255]` byte) into a
// `<4 x float>` in `[0.0, 1.0]` -- roadmap H19j, the single-channel
// analogue of `femeRTUnpackR8G8B8A8Unorm` above. The unread G/B components
// pad `0.0`, alpha pads `1.0`, matching `femeRTUnpackImageTexel`'s own
// partial-component convention (`R32_FLOAT` et al.).
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR8Unorm(uint8_t Raw) {
  FemeRTv4f32 V = {(float)Raw / 255.0f, 0.0f, 0.0f, 1.0f};
  return V;
}

// The inverse of `femeRTUnpackR8Unorm`: clamps the first component to
// `[0.0, 1.0]`, scales to `[0, 255]`, and rounds to the nearest byte.
__attribute__((always_inline)) static uint8_t
femeRTPackR8Unorm(FemeRTv4f32 Value) {
  float C0 = __builtin_fminf(__builtin_fmaxf(Value[0], 0.0f), 1.0f);
  return (uint8_t)__builtin_roundf(C0 * 255.0f);
}

// Unpacks a `R8_SNORM` value (one signed-normalized byte) into a
// `<4 x float>` in `[-1.0, 1.0]` -- roadmap H19j, the single-channel
// analogue of `femeRTUnpackR8G8B8A8Snorm` above.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR8Snorm(uint8_t Raw) {
  int8_t B0 = (int8_t)Raw;
  FemeRTv4f32 V = {__builtin_fmaxf((float)B0 / 127.0f, -1.0f), 0.0f, 0.0f,
                   1.0f};
  return V;
}

// The inverse of `femeRTUnpackR8Snorm`: clamps the first component to
// `[-1.0, 1.0]`, scales to `[-127, 127]`, and rounds to the nearest byte.
__attribute__((always_inline)) static uint8_t
femeRTPackR8Snorm(FemeRTv4f32 Value) {
  float C0 = __builtin_fminf(__builtin_fmaxf(Value[0], -1.0f), 1.0f);
  return (uint8_t)(int8_t)__builtin_roundf(C0 * 127.0f);
}

// Unpacks a `R8G8_UNORM` value (two normalized `[0, 255]` bytes,
// little-endian: R, G) into a `<4 x float>` in `[0.0, 1.0]` -- roadmap
// H19n, the two-channel analogue of `femeRTUnpackR8Unorm` above. The
// unread B channel pads `0.0`, alpha pads `1.0`.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR8G8Unorm(uint16_t Raw) {
  uint8_t B0 = (uint8_t)Raw;
  uint8_t B1 = (uint8_t)(Raw >> 8);
  FemeRTv4f32 V = {(float)B0 / 255.0f, (float)B1 / 255.0f, 0.0f, 1.0f};
  return V;
}

// The inverse of `femeRTUnpackR8G8Unorm`: clamps each component to
// `[0.0, 1.0]`, scales to `[0, 255]`, and packs the two bytes
// little-endian.
__attribute__((always_inline)) static uint16_t
femeRTPackR8G8Unorm(FemeRTv4f32 Value) {
  float C0 = __builtin_fminf(__builtin_fmaxf(Value[0], 0.0f), 1.0f);
  float C1 = __builtin_fminf(__builtin_fmaxf(Value[1], 0.0f), 1.0f);
  uint8_t B0 = (uint8_t)__builtin_roundf(C0 * 255.0f);
  uint8_t B1 = (uint8_t)__builtin_roundf(C1 * 255.0f);
  return (uint16_t)B0 | ((uint16_t)B1 << 8);
}

// Unpacks a `R8G8_SNORM` value (two signed-normalized bytes,
// little-endian: R, G) into a `<4 x float>` in `[-1.0, 1.0]` -- roadmap
// H19n, the two-channel analogue of `femeRTUnpackR8Snorm` above.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR8G8Snorm(uint16_t Raw) {
  int8_t B0 = (int8_t)(uint8_t)Raw;
  int8_t B1 = (int8_t)(uint8_t)(Raw >> 8);
  FemeRTv4f32 V = {__builtin_fmaxf((float)B0 / 127.0f, -1.0f),
                   __builtin_fmaxf((float)B1 / 127.0f, -1.0f), 0.0f, 1.0f};
  return V;
}

// The inverse of `femeRTUnpackR8G8Snorm`: clamps each component to
// `[-1.0, 1.0]`, scales to `[-127, 127]`, and packs the two bytes
// little-endian.
__attribute__((always_inline)) static uint16_t
femeRTPackR8G8Snorm(FemeRTv4f32 Value) {
  float C0 = __builtin_fminf(__builtin_fmaxf(Value[0], -1.0f), 1.0f);
  float C1 = __builtin_fminf(__builtin_fmaxf(Value[1], -1.0f), 1.0f);
  uint8_t B0 = (uint8_t)(int8_t)__builtin_roundf(C0 * 127.0f);
  uint8_t B1 = (uint8_t)(int8_t)__builtin_roundf(C1 * 127.0f);
  return (uint16_t)B0 | ((uint16_t)B1 << 8);
}

// Unpacks a `R16_UNORM` value (one normalized `[0, 65535]` 16-bit word)
// into a `<4 x float>` in `[0.0, 1.0]` -- roadmap H19n, the
// single-channel analogue of `femeRTUnpackR16G16B16A16Unorm` below. The
// unread G/B components pad `0.0`, alpha pads `1.0`.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR16Unorm(uint16_t Raw) {
  FemeRTv4f32 V = {(float)Raw / 65535.0f, 0.0f, 0.0f, 1.0f};
  return V;
}

// The inverse of `femeRTUnpackR16Unorm`: clamps the first component to
// `[0.0, 1.0]`, scales to `[0, 65535]`, and rounds to the nearest word.
__attribute__((always_inline)) static uint16_t
femeRTPackR16Unorm(FemeRTv4f32 Value) {
  float C0 = __builtin_fminf(__builtin_fmaxf(Value[0], 0.0f), 1.0f);
  return (uint16_t)__builtin_roundf(C0 * 65535.0f);
}

// Unpacks a `R16_SNORM` value (one signed-normalized 16-bit word) into a
// `<4 x float>` in `[-1.0, 1.0]` -- roadmap H19n, the single-channel
// analogue of `femeRTUnpackR16G16B16A16Snorm` below.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR16Snorm(uint16_t Raw) {
  FemeRTv4f32 V = {__builtin_fmaxf((float)(int16_t)Raw / 32767.0f, -1.0f),
                   0.0f, 0.0f, 1.0f};
  return V;
}

// The inverse of `femeRTUnpackR16Snorm`: clamps the first component to
// `[-1.0, 1.0]`, scales to `[-32767, 32767]`, and rounds to the nearest
// word.
__attribute__((always_inline)) static uint16_t
femeRTPackR16Snorm(FemeRTv4f32 Value) {
  float C0 = __builtin_fminf(__builtin_fmaxf(Value[0], -1.0f), 1.0f);
  return (uint16_t)(int16_t)__builtin_roundf(C0 * 32767.0f);
}

// Unpacks a `R16G16_UNORM` value (two normalized `[0, 65535]` 16-bit
// words, little-endian: R, G) into a `<4 x float>` in `[0.0, 1.0]` --
// roadmap H19n, the two-channel analogue of `femeRTUnpackR16Unorm`
// above. The unread B channel pads `0.0`, alpha pads `1.0`.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR16G16Unorm(const uint16_t Raw[2]) {
  FemeRTv4f32 V = {(float)Raw[0] / 65535.0f, (float)Raw[1] / 65535.0f, 0.0f,
                   1.0f};
  return V;
}

// The inverse of `femeRTUnpackR16G16Unorm`: clamps each component to
// `[0.0, 1.0]`, scales to `[0, 65535]`, and packs the two rounded words
// little-endian into \p Out.
__attribute__((always_inline)) static void
femeRTPackR16G16Unorm(FemeRTv4f32 Value, uint16_t Out[2]) {
  float C0 = __builtin_fminf(__builtin_fmaxf(Value[0], 0.0f), 1.0f);
  float C1 = __builtin_fminf(__builtin_fmaxf(Value[1], 0.0f), 1.0f);
  Out[0] = (uint16_t)__builtin_roundf(C0 * 65535.0f);
  Out[1] = (uint16_t)__builtin_roundf(C1 * 65535.0f);
}

// Unpacks a `R16G16_SNORM` value (two signed-normalized `[-32767,
// 32767]` 16-bit words, little-endian: R, G) into a `<4 x float>` in
// `[-1.0, 1.0]` -- roadmap H19n, the two-channel analogue of
// `femeRTUnpackR16Snorm` above.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR16G16Snorm(const uint16_t Raw[2]) {
  FemeRTv4f32 V = {__builtin_fmaxf((float)(int16_t)Raw[0] / 32767.0f, -1.0f),
                   __builtin_fmaxf((float)(int16_t)Raw[1] / 32767.0f, -1.0f),
                   0.0f, 1.0f};
  return V;
}

// The inverse of `femeRTUnpackR16G16Snorm`: clamps each component to
// `[-1.0, 1.0]`, scales to `[-32767, 32767]`, and packs the two rounded
// words little-endian into \p Out.
__attribute__((always_inline)) static void
femeRTPackR16G16Snorm(FemeRTv4f32 Value, uint16_t Out[2]) {
  float C0 = __builtin_fminf(__builtin_fmaxf(Value[0], -1.0f), 1.0f);
  float C1 = __builtin_fminf(__builtin_fmaxf(Value[1], -1.0f), 1.0f);
  Out[0] = (uint16_t)(int16_t)__builtin_roundf(C0 * 32767.0f);
  Out[1] = (uint16_t)(int16_t)__builtin_roundf(C1 * 32767.0f);
}

// Unpacks a `R16G16B16A16_UNORM` value (four normalized `[0, 65535]`
// 16-bit words, little-endian) into a `<4 x float>` in `[0.0, 1.0]` --
// roadmap H19h, the 16-bit-per-component analogue of
// `femeRTUnpackR8G8B8A8Unorm` above.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR16G16B16A16Unorm(const uint16_t Raw[4]) {
  FemeRTv4f32 V;
  V[0] = (float)Raw[0] / 65535.0f;
  V[1] = (float)Raw[1] / 65535.0f;
  V[2] = (float)Raw[2] / 65535.0f;
  V[3] = (float)Raw[3] / 65535.0f;
  return V;
}

// The inverse of `femeRTUnpackR16G16B16A16Unorm`: clamps each component to
// `[0.0, 1.0]`, scales to `[0, 65535]`, and packs the four rounded words
// little-endian into \p Out.
__attribute__((always_inline)) static void
femeRTPackR16G16B16A16Unorm(FemeRTv4f32 Value, uint16_t Out[4]) {
  float C0 = __builtin_fminf(__builtin_fmaxf(Value[0], 0.0f), 1.0f);
  float C1 = __builtin_fminf(__builtin_fmaxf(Value[1], 0.0f), 1.0f);
  float C2 = __builtin_fminf(__builtin_fmaxf(Value[2], 0.0f), 1.0f);
  float C3 = __builtin_fminf(__builtin_fmaxf(Value[3], 0.0f), 1.0f);
  Out[0] = (uint16_t)__builtin_roundf(C0 * 65535.0f);
  Out[1] = (uint16_t)__builtin_roundf(C1 * 65535.0f);
  Out[2] = (uint16_t)__builtin_roundf(C2 * 65535.0f);
  Out[3] = (uint16_t)__builtin_roundf(C3 * 65535.0f);
}

// Unpacks a `R16G16B16A16_SNORM` value (four signed-normalized
// `[-32767, 32767]` 16-bit words, little-endian) into a `<4 x float>` in
// `[-1.0, 1.0]` -- roadmap H19h, the 16-bit-per-component analogue of
// `femeRTUnpackR8G8B8A8Snorm` above. `-32768` clamps to `-1.0` the same
// way `femeRTUnpackR8G8B8A8Snorm`'s own `-128` does, matching the Vulkan
// spec's own SNORM decode (`max(c / 32767, -1.0)`).
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR16G16B16A16Snorm(const uint16_t Raw[4]) {
  FemeRTv4f32 V;
  V[0] = __builtin_fmaxf((float)(int16_t)Raw[0] / 32767.0f, -1.0f);
  V[1] = __builtin_fmaxf((float)(int16_t)Raw[1] / 32767.0f, -1.0f);
  V[2] = __builtin_fmaxf((float)(int16_t)Raw[2] / 32767.0f, -1.0f);
  V[3] = __builtin_fmaxf((float)(int16_t)Raw[3] / 32767.0f, -1.0f);
  return V;
}

// The inverse of `femeRTUnpackR16G16B16A16Snorm`: clamps each component to
// `[-1.0, 1.0]`, scales to `[-32767, 32767]`, and packs the four rounded
// words little-endian into \p Out.
__attribute__((always_inline)) static void
femeRTPackR16G16B16A16Snorm(FemeRTv4f32 Value, uint16_t Out[4]) {
  float C0 = __builtin_fminf(__builtin_fmaxf(Value[0], -1.0f), 1.0f);
  float C1 = __builtin_fminf(__builtin_fmaxf(Value[1], -1.0f), 1.0f);
  float C2 = __builtin_fminf(__builtin_fmaxf(Value[2], -1.0f), 1.0f);
  float C3 = __builtin_fminf(__builtin_fmaxf(Value[3], -1.0f), 1.0f);
  Out[0] = (uint16_t)(int16_t)__builtin_roundf(C0 * 32767.0f);
  Out[1] = (uint16_t)(int16_t)__builtin_roundf(C1 * 32767.0f);
  Out[2] = (uint16_t)(int16_t)__builtin_roundf(C2 * 32767.0f);
  Out[3] = (uint16_t)(int16_t)__builtin_roundf(C3 * 32767.0f);
}

//--- Typed-buffer `<4 x float>` view ------------------------------------------

// Forward declarations: `femeRTImageFormatElementSize`/
// `femeRTUnpackImageTexel`/`femeRTPackImageTexel` (roadmap E25/E26/H19a)
// are defined later in this file (alongside the storage/sampled-image
// path that originated them), but roadmap H8d's typed-buffer `<4 x
// float>` load/store below reuse them directly rather than special-casing
// each format a second time -- see those functions' own doc comments for
// the full per-format scope this reuse inherits.
__attribute__((always_inline)) static uint64_t
femeRTImageFormatElementSize(uint32_t Format);
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackImageTexel(uint32_t Format, const unsigned char *Ptr);
__attribute__((always_inline)) static void
femeRTPackImageTexel(uint32_t Format, unsigned char *Ptr, FemeRTv4f32 Texel);

// `feme.cpu.resource.load.typed.v4f32` (see `feme::cpu::ResourceCalls`):
// reads a `<4 x float>` element through a bindless typed-buffer
// descriptor, switching on `Format` (see `feme::cpu::ResourceFormat` in
// RuntimeABI.h) via the shared `femeRTImageFormatElementSize`/
// `femeRTUnpackImageTexel` tables the storage/sampled-image path already
// uses -- see "Descriptor formats" for why the conversion has to be a
// runtime switch rather than something the compiler can select at compile
// time. `ResourceKind::Typed == 1`. An inactive lane (`Mask == false`) or a
// failing bounds/kind check reads as zero, never touching `Heap`'s memory
// (see "Bounds checking").
//
// `OpImageFetch`/`OpImageRead` always return a full `<4 x T>` per SPIR-V's
// own spec regardless of the underlying format's real channel count, so a
// narrower-than-4-component format's *read* side still goes through this
// 4-wide load, not the scalar `feme.cpu.resource.load.typed.f32` (roadmap
// L9, only reached by a scalar-typed *store*, whose Texel operand SPIR-V
// shapes to match the shader's own declared element type). The unread
// G/B lanes pad `0`, alpha pads `1`, matching `femeRTUnpackImageTexel`'s
// own partial-component convention for the same format.
FemeRTv4f32 femeCpuResourceLoadTypedV4F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex,
    _Bool Mask) asm("feme.cpu.resource.load.typed.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuResourceLoadTypedV4F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  // (Roadmap H8d) Reuses `femeRTImageFormatElementSize`/
  // `femeRTUnpackImageTexel` -- the same per-format conversion tables the
  // storage/sampled-image path and the scalar `feme.cpu.resource.load.
  // typed.f32` intrinsic (roadmap L9) already share -- rather than
  // special-casing each format a second time here. `Format.cpp`'s
  // `isTexelBufferFormatSupported` only ever admits a format this table
  // itself recognizes into a typed-buffer descriptor, so the `ElemSize ==
  // 0` fallback below is unreachable in practice; kept only so an
  // unrecognized format still reads a well-defined 16-byte-stride zero
  // rather than dividing by a zero stride.
  uint64_t ElemSize = femeRTImageFormatElementSize(Desc.Format);
  if (ElemSize == 0)
    ElemSize = 16;
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  if (!(AccessOK && Mask)) {
    FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
    return Zero;
  }
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  return femeRTUnpackImageTexel(Desc.Format, Ptr);
}

// `feme.cpu.resource.store.typed.v4f32`: the store counterpart of
// `feme.cpu.resource.load.typed.v4f32` above -- same descriptor lookup,
// bounds/kind check and `femeRTImageFormatElementSize`/
// `femeRTPackImageTexel` format dispatch, plus the UAV check "Descriptor
// heaps" requires for any write (`FEME_DESCRIPTOR_UAV == 1 << 0`; a
// constant buffer or other read-only view's `Flags` never sets it, so a
// store through one is silently dropped rather than corrupting it). An
// out-of-bounds or inactive-lane write is dropped, never touching `Heap`'s
// memory, matching the load's "reads zero" rule with "writes ignored" (see
// "Bounds checking"). `Format.cpp`'s `isStorageTexelBufferFormatSupported`
// (narrower than `isTexelBufferFormatSupported`, roadmap H8d) is what
// keeps a real driver from ever exposing
// `VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT` for a format
// `femeRTPackImageTexel` cannot actually pack (most notably
// `B8G8R8A8_UNORM`) -- this function itself stays total (silently drops
// an unpackable format's write) rather than additionally guarding against
// that here.
void femeCpuResourceStoreTypedV4F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, FemeRTv4f32 Value,
    _Bool Mask) asm("feme.cpu.resource.store.typed.v4f32");

__attribute__((always_inline)) void
femeCpuResourceStoreTypedV4F32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                               uint32_t DescriptorIndex, uint64_t ElementIndex,
                               FemeRTv4f32 Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  uint64_t ElemSize = femeRTImageFormatElementSize(Desc.Format);
  if (ElemSize == 0)
    ElemSize = 16;
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!(AccessOK && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  femeRTPackImageTexel(Desc.Format, Ptr, Value);
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

// (Roadmap H137) The `int64_t`/`uint64_t` element counterpart of
// `FemeRTv2i32` above, needed for an HLSL `int64_t2` (or `uint64_t2`-typed)
// raw/structured-buffer load or store (e.g. `WaveOps/
// WaveActiveAllEqual.int64.test`'s own input buffers) --
// `mangleResourceCallName` already mangles a 64-bit integer element as `i64`
// generically (see `appendScalarMangling`), only the runtime definitions
// themselves were missing. `<3 x i64>`/`<4 x i64>` (24/32 bytes) are
// deliberately not added alongside these: unlike every other vector width this
// runtime already defines, a value that size no longer fits this ABI's
// direct-return/ direct-by-value-argument register budget, so Clang coerces it
// to an indirect (`sret`-for-return, pointer-for-argument) calling convention
// -- a third coercion shape `feme::cpu::ResourceLoweringPass`'s call-building
// code does not yet generate to match, unlike the "widen a narrow vector"
// and "same-size bitcast" shapes it already handles (see the call-site
// comment on `feme::cpu::ResourceCalls`). Adding the `<3/4 x i64>` runtime
// definitions without that caller-side ABI awareness would silently link a
// type-mismatched declaration/definition pair, producing wrong results
// rather than the missing-symbol failure this milestone actually fixes
// (see agent_thoughts.md for the concrete analysis). Tracked as its own
// follow-on roadmap item.
typedef int64_t FemeRTv2i64 __attribute__((vector_size(16)));
typedef int64_t FemeRTv2i64Unaligned
    __attribute__((vector_size(16), aligned(8)));

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

// Unpacks a `R8_UINT` value (one unsigned byte) into a `<4 x i32>` by
// zero-extending it -- roadmap H19j, the single-channel analogue of
// `femeRTUnpackR8G8B8A8Uint` above.
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackR8Uint(uint8_t Raw) {
  FemeRTv4i32 V = {(int32_t)Raw, 0, 0, 1};
  return V;
}

// The inverse of `femeRTUnpackR8Uint`: truncates the first lane to its low
// byte (clamping is the application's own responsibility, matching
// `femeRTPackR8G8B8A8Uint`'s own "undefined results" rule for a UINT
// format).
__attribute__((always_inline)) static uint8_t
femeRTPackR8Uint(FemeRTv4i32 Value) {
  return (uint8_t)Value[0];
}

// Unpacks a `R8_SINT` value (one signed byte) into a `<4 x i32>` by
// sign-extending it -- roadmap H19j, the single-channel analogue of
// `femeRTUnpackR8G8B8A8Sint` above.
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackR8Sint(uint8_t Raw) {
  FemeRTv4i32 V = {(int32_t)(int8_t)Raw, 0, 0, 1};
  return V;
}

// The inverse of `femeRTUnpackR8Sint`: truncating the first lane to its
// low byte produces the same bit pattern regardless of signedness, so
// this shares `femeRTPackR8Uint`'s implementation exactly, mirroring
// `femeRTPackR8G8B8A8Sint`'s own analogous sharing.
__attribute__((always_inline)) static uint8_t
femeRTPackR8Sint(FemeRTv4i32 Value) {
  return femeRTPackR8Uint(Value);
}

// Unpacks a `R8G8_UINT` value (two unsigned bytes, little-endian: R, G)
// into a `<4 x i32>` by zero-extending each byte -- roadmap H19n, the
// two-channel analogue of `femeRTUnpackR8Uint` above.
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackR8G8Uint(uint16_t Raw) {
  FemeRTv4i32 V = {(int32_t)(uint8_t)Raw, (int32_t)(uint8_t)(Raw >> 8), 0, 1};
  return V;
}

// The inverse of `femeRTUnpackR8G8Uint`: truncates each lane to its low
// byte and packs the two bytes little-endian.
__attribute__((always_inline)) static uint16_t
femeRTPackR8G8Uint(FemeRTv4i32 Value) {
  uint8_t B0 = (uint8_t)Value[0];
  uint8_t B1 = (uint8_t)Value[1];
  return (uint16_t)B0 | ((uint16_t)B1 << 8);
}

// Unpacks a `R8G8_SINT` value (two signed bytes, little-endian: R, G)
// into a `<4 x i32>` by sign-extending each byte -- roadmap H19n, the
// two-channel analogue of `femeRTUnpackR8Sint` above.
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackR8G8Sint(uint16_t Raw) {
  FemeRTv4i32 V = {(int32_t)(int8_t)(uint8_t)Raw,
                   (int32_t)(int8_t)(uint8_t)(Raw >> 8), 0, 1};
  return V;
}

// The inverse of `femeRTUnpackR8G8Sint`: truncating each lane to its low
// byte produces the same bit pattern regardless of signedness, so this
// shares `femeRTPackR8G8Uint`'s implementation exactly, mirroring
// `femeRTPackR8Sint`'s own analogous sharing.
__attribute__((always_inline)) static uint16_t
femeRTPackR8G8Sint(FemeRTv4i32 Value) {
  return femeRTPackR8G8Uint(Value);
}

// Unpacks a `R16_UINT` value (one unsigned 16-bit word) into a
// `<4 x i32>` by zero-extending it -- roadmap H19n, the single-channel
// analogue of `femeRTUnpackR16G16B16A16Uint` below.
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackR16Uint(uint16_t Raw) {
  FemeRTv4i32 V = {(int32_t)Raw, 0, 0, 1};
  return V;
}

// The inverse of `femeRTUnpackR16Uint`: truncates the first lane to its
// low word (clamping is the application's own responsibility, matching
// `femeRTPackR8G8B8A8Uint`'s own "undefined results" rule for a UINT
// format).
__attribute__((always_inline)) static uint16_t
femeRTPackR16Uint(FemeRTv4i32 Value) {
  return (uint16_t)Value[0];
}

// Unpacks a `R16_SINT` value (one signed 16-bit word) into a
// `<4 x i32>` by sign-extending it -- roadmap H19n, the single-channel
// analogue of `femeRTUnpackR16G16B16A16Sint` below.
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackR16Sint(uint16_t Raw) {
  FemeRTv4i32 V = {(int32_t)(int16_t)Raw, 0, 0, 1};
  return V;
}

// The inverse of `femeRTUnpackR16Sint`: truncating the first lane to its
// low word produces the same bit pattern regardless of signedness, so
// this shares `femeRTPackR16Uint`'s implementation exactly, mirroring
// `femeRTPackR8G8Sint`'s own analogous sharing.
__attribute__((always_inline)) static uint16_t
femeRTPackR16Sint(FemeRTv4i32 Value) {
  return femeRTPackR16Uint(Value);
}

// Unpacks a `R16G16_UINT` value (two unsigned 16-bit words,
// little-endian) into a `<4 x i32>` by zero-extending each -- roadmap
// H19n, the two-channel analogue of `femeRTUnpackR16Uint` above.
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackR16G16Uint(const uint16_t Raw[2]) {
  FemeRTv4i32 V = {(int32_t)Raw[0], (int32_t)Raw[1], 0, 1};
  return V;
}

// The inverse of `femeRTUnpackR16G16Uint`: truncates each lane to its
// low word into \p Out, little-endian.
__attribute__((always_inline)) static void
femeRTPackR16G16Uint(FemeRTv4i32 Value, uint16_t Out[2]) {
  Out[0] = (uint16_t)Value[0];
  Out[1] = (uint16_t)Value[1];
}

// Unpacks a `R16G16_SINT` value (two signed 16-bit words, little-endian)
// into a `<4 x i32>` by sign-extending each -- roadmap H19n, the
// two-channel analogue of `femeRTUnpackR16Sint` above.
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackR16G16Sint(const uint16_t Raw[2]) {
  FemeRTv4i32 V = {(int32_t)(int16_t)Raw[0], (int32_t)(int16_t)Raw[1], 0, 1};
  return V;
}

// The inverse of `femeRTUnpackR16G16Sint`: truncating each lane to its
// low word produces the same bit pattern regardless of signedness, so
// this shares `femeRTPackR16G16Uint`'s implementation exactly, mirroring
// `femeRTPackR16Sint`'s own analogous sharing.
__attribute__((always_inline)) static void
femeRTPackR16G16Sint(FemeRTv4i32 Value, uint16_t Out[2]) {
  femeRTPackR16G16Uint(Value, Out);
}

// Forward declarations: `femeRTUnpackImageTexelI32`/`femeRTPackImageTexelI32`
// (roadmap E26/H19a) are defined later in this file, but roadmap H8d's
// typed-buffer `<4 x i32>` load/store below reuse them directly, the
// same way the `<4 x float>` view above reuses `femeRTUnpackImageTexel`/
// `femeRTPackImageTexel`.
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackImageTexelI32(uint32_t Format, const unsigned char *Ptr);
__attribute__((always_inline)) static void
femeRTPackImageTexelI32(uint32_t Format, unsigned char *Ptr,
                        FemeRTv4i32 Texel);

// `feme.cpu.resource.load.typed.v4i32` (V4, see `feme::cpu::ResourceCalls`):
// reads a `<4 x i32>` element through a bindless typed-buffer descriptor,
// via the shared `femeRTImageFormatElementSize`/`femeRTUnpackImageTexelI32`
// tables the storage/sampled-image path already uses (roadmap H8d) --
// see `feme::cpu::ResourceFormat` in RuntimeABI.h for this project's own
// format numbering. `ResourceKind::Typed == 1`. An inactive lane or a
// failing bounds/kind check reads as zero, never touching `Heap`'s
// memory, exactly like the `<4 x float>` load above (see "Bounds
// checking").
//
// `OpImageFetch`/`OpImageRead` always return a full `<4 x T>` per
// SPIR-V's own spec regardless of the underlying format's real channel
// count, the same "`femeCpuResourceLoadTypedV4F32`'s own comment
// explains for `R32_FLOAT`" reason applying here too. The unread G/B
// lanes pad `0`, alpha pads `1`.
FemeRTv4i32 femeCpuResourceLoadTypedV4I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex,
    _Bool Mask) asm("feme.cpu.resource.load.typed.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuResourceLoadTypedV4I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  uint64_t ElemSize = femeRTImageFormatElementSize(Desc.Format);
  if (ElemSize == 0)
    ElemSize = 16;
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  if (!(AccessOK && Mask)) {
    FemeRTv4i32 Zero = {0, 0, 0, 0};
    return Zero;
  }
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  return femeRTUnpackImageTexelI32(Desc.Format, Ptr);
}

// `feme.cpu.resource.store.typed.v4i32`: the store counterpart of
// `feme.cpu.resource.load.typed.v4i32` above, with the same UAV check every
// typed-buffer store requires (see `femeCpuResourceStoreTypedV4F32`).
// `Format.cpp`'s `isStorageTexelBufferFormatSupported` (roadmap H8d) is
// what keeps a real driver from ever exposing this format-agnostic
// dispatch for a format `femeRTPackImageTexelI32` cannot actually pack.
void femeCpuResourceStoreTypedV4I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, FemeRTv4i32 Value,
    _Bool Mask) asm("feme.cpu.resource.store.typed.v4i32");

__attribute__((always_inline)) void
femeCpuResourceStoreTypedV4I32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                               uint32_t DescriptorIndex, uint64_t ElementIndex,
                               FemeRTv4i32 Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  uint64_t ElemSize = femeRTImageFormatElementSize(Desc.Format);
  if (ElemSize == 0)
    ElemSize = 16;
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!(AccessOK && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  femeRTPackImageTexelI32(Desc.Format, Ptr, Value);
}

// `feme.cpu.resource.getdimensions.typed.i32` (roadmap H144): `Buffer<T>`/
// `RWBuffer<T>`'s (a typed buffer's own, format-based) `GetDimensions(out
// uint numElements)` -- SPIR-V `OpImageQuerySize` against a `Dim::Buffer`
// image handle (`feme::spirv::ImageQuerySizePattern`'s `.x`-width branch,
// SPIRVToLLVMPatterns.cpp), unlike `femeCpuImageGetDimensions2DV2I32`'s own
// counterpart for an ordinary 2D texture. A typed buffer's descriptor
// carries no separate "element count" field of its own (unlike
// `FemeRTImageDescriptor::Width`/`Height`) -- its declared byte range
// (`SizeInBytes`, set from the `VkBufferView`'s own range at bind time)
// divided by the per-format texel size (`femeRTImageFormatElementSize`,
// the same table `femeCpuResourceLoadTypedV4F32`/`V4I32` above already
// share) gives the element count directly. An unbound handle (`!Data`)
// or an inactive lane reads as `0`, mirroring
// `femeCpuImageGetDimensions2DV2I32`'s own all-zero convention; no bounds
// check applies (this reads only descriptor metadata, never `Data`
// itself, so there is nothing to bounds-check against).
uint32_t femeCpuResourceGetDimensionsTypedI32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    _Bool Mask) asm("feme.cpu.resource.getdimensions.typed.i32");

__attribute__((always_inline)) uint32_t femeCpuResourceGetDimensionsTypedI32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  if (!Mask || !Desc.Data)
    return 0;
  uint64_t ElemSize = femeRTImageFormatElementSize(Desc.Format);
  if (ElemSize == 0)
    return 0;
  return (uint32_t)(Desc.SizeInBytes / ElemSize);
}

// `feme.cpu.resource.getdimensions.raw.i32` (roadmap H160, extended by
// L124(a)): a raw or structured buffer's own `GetDimensions` -- see
// `feme::cpu::ResourceCallKind::GetDimensionsRaw`'s own doc for the
// operand shape. `PrefixOffset` (`0` for a one-member wrapper handle
// whose runtime array starts at byte 0, or the array member's own
// declared byte offset for a real, multi-field storage-buffer block --
// `SPIRVResourceLowering.cpp`'s `lowerAccesses` computes whichever
// applies) is subtracted from the descriptor's own declared byte size
// before dividing by `Stride`, computed here (not by the caller via
// ordinary IR arithmetic around this call) so an unbound handle's `0`
// `SizeInBytes` reads as `0` rather than wrapping around to a huge
// value the naive subtraction would otherwise produce.
uint32_t femeCpuResourceGetDimensionsRawI32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t Stride, uint64_t PrefixOffset,
    _Bool Mask) asm("feme.cpu.resource.getdimensions.raw.i32");

__attribute__((always_inline)) uint32_t femeCpuResourceGetDimensionsRawI32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t Stride, uint64_t PrefixOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  if (!Mask || !Desc.Data || Stride == 0 || Desc.SizeInBytes < PrefixOffset)
    return 0;
  return (uint32_t)((Desc.SizeInBytes - PrefixOffset) / Stride);
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

// (Roadmap H137) `feme.cpu.resource.load.raw.i64`/`.v2i64` and their
// `.store.raw.*` counterparts: the 64-bit-integer-element siblings of
// `.i32`/`.v2i32` above, needed for an HLSL `int64_t`/`int64_t2` (or
// `uint64_t`-typed) raw/structured-buffer load or store (e.g. `WaveOps/
// WaveActiveAllEqual.int64.test`'s own input buffers) -- as with `.i32`
// above, `mangleResourceCallName` already mangled these generically, only
// the runtime definitions themselves were missing. `.v3i64`/`.v4i64` are
// deliberately not added here yet -- see the `FemeRTv2i64` comment above
// for why.
int64_t
femeCpuResourceLoadRawI64(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                          uint32_t DescriptorIndex, uint64_t ByteOffset,
                          _Bool Mask) asm("feme.cpu.resource.load.raw.i64");

__attribute__((always_inline)) int64_t femeCpuResourceLoadRawI64(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 8);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 8);
  if (!((OkRaw || OkStructured) && Mask))
    return 0;
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  int64_t V;
  __builtin_memcpy(&V, Ptr, sizeof(V));
  return V;
}

void femeCpuResourceStoreRawI64(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int64_t Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.i64");

__attribute__((always_inline)) void
femeCpuResourceStoreRawI64(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                           uint32_t DescriptorIndex, uint64_t ByteOffset,
                           int64_t Value, _Bool Mask) {
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
  __builtin_memcpy(Ptr, &Value, sizeof(Value));
}

FemeRTv2i64
femeCpuResourceLoadRawV2I64(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                            uint32_t DescriptorIndex, uint64_t ByteOffset,
                            _Bool Mask) asm("feme.cpu.resource.load.raw.v2i64");

__attribute__((always_inline)) FemeRTv2i64 femeCpuResourceLoadRawV2I64(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 16);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 16);
  if (!((OkRaw || OkStructured) && Mask))
    return (FemeRTv2i64){0, 0};
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  return *(const FemeRTv2i64Unaligned *)Ptr;
}

void femeCpuResourceStoreRawV2I64(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv2i64 Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.v2i64");

__attribute__((always_inline)) void
femeCpuResourceStoreRawV2I64(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                             uint32_t DescriptorIndex, uint64_t ByteOffset,
                             FemeRTv2i64 Value, _Bool Mask) {
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
  *(FemeRTv2i64Unaligned *)Ptr = (FemeRTv2i64Unaligned)Value;
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

// (Roadmap H124c) `feme.cpu.resource.load.raw.f16`/`.store.raw.f16`: the
// `half`-element (`_Float16`) scalar counterpart of `.f32` above, needed
// for a raw/structured-buffer load or store of an HLSL `half` value (e.g.
// `WaveOps/*.fp16.test`'s own input/output buffers) -- `feme::cpu::
// mangleResourceCallName` (ResourceCalls.cpp) already mangles `half`-typed
// (LLVM `half`) elements as `f16` generically, so `feme::cpu::
// SPIRVResourceLoweringPass` was always able to *emit* a call to this
// name; only this runtime definition itself was missing.
_Float16 femeCpuResourceLoadRawF16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) asm("feme.cpu.resource.load.raw.f16");

__attribute__((always_inline)) _Float16
femeCpuResourceLoadRawF16(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                          uint32_t DescriptorIndex, uint64_t ByteOffset,
                          _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 2);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 2);
  if (!((OkRaw || OkStructured) && Mask))
    return (_Float16)0.0f;
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  _Float16 V;
  __builtin_memcpy(&V, Ptr, sizeof(V));
  return V;
}

void femeCpuResourceStoreRawF16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Float16 Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.f16");

__attribute__((always_inline)) void
femeCpuResourceStoreRawF16(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                           uint32_t DescriptorIndex, uint64_t ByteOffset,
                           _Float16 Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 2);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 2);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!((OkRaw || OkStructured) && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  __builtin_memcpy(Ptr, &Value, sizeof(Value));
}

// (Roadmap H124c) `feme.cpu.resource.load.raw.i16`/`.store.raw.i16`: the
// `int16_t`/`uint16_t` scalar sibling of `.f16` above, needed for a raw/
// structured-buffer load or store of an HLSL 16-bit integer value (e.g.
// `WaveOps/*.int16.test`'s own input/output buffers).
int16_t femeCpuResourceLoadRawI16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) asm("feme.cpu.resource.load.raw.i16");

__attribute__((always_inline)) int16_t
femeCpuResourceLoadRawI16(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                          uint32_t DescriptorIndex, uint64_t ByteOffset,
                          _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 2);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 2);
  if (!((OkRaw || OkStructured) && Mask))
    return 0;
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  int16_t V;
  __builtin_memcpy(&V, Ptr, sizeof(V));
  return V;
}

void femeCpuResourceStoreRawI16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int16_t Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.i16");

__attribute__((always_inline)) void
femeCpuResourceStoreRawI16(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                           uint32_t DescriptorIndex, uint64_t ByteOffset,
                           int16_t Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 2);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 2);
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

// (Roadmap H124c) `feme.cpu.resource.load.raw.v2f16`/`.v3f16`/`.v4f16` and
// their `.store.raw.*` counterparts: the `half`-element (`_Float16`)
// vector siblings of `.v2f32`/`.v3f32`/`.v4f32` above, needed for an HLSL
// `half2`/`half3`/`half4` raw/structured-buffer load or store (e.g.
// `WaveOps/*.fp16.test`'s own input buffers) -- as with `.f16` above,
// `mangleResourceCallName` already mangled these generically, only the
// runtime definitions themselves were missing.
FemeRTv2f16
femeCpuResourceLoadRawV2F16(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                            uint32_t DescriptorIndex, uint64_t ByteOffset,
                            _Bool Mask) asm("feme.cpu.resource.load.raw.v2f16");

__attribute__((always_inline)) FemeRTv2f16 femeCpuResourceLoadRawV2F16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 4);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 4);
  if (!((OkRaw || OkStructured) && Mask))
    return (FemeRTv2f16){0.0f, 0.0f};
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  return *(const FemeRTv2f16Unaligned *)Ptr;
}

void femeCpuResourceStoreRawV2F16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv2f16 Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.v2f16");

__attribute__((always_inline)) void femeCpuResourceStoreRawV2F16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv2f16 Value, _Bool Mask) {
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
  *(FemeRTv2f16Unaligned *)Ptr = (FemeRTv2f16Unaligned)Value;
}

FemeRTv3f16
femeCpuResourceLoadRawV3F16(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                            uint32_t DescriptorIndex, uint64_t ByteOffset,
                            _Bool Mask) asm("feme.cpu.resource.load.raw.v3f16");

__attribute__((always_inline)) FemeRTv3f16 femeCpuResourceLoadRawV3F16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 6);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 6);
  if (!((OkRaw || OkStructured) && Mask))
    return (FemeRTv3f16){0.0f, 0.0f, 0.0f};
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  FemeRTv3f16 V;
  __builtin_memcpy(&V, Ptr, 6);
  return V;
}

void femeCpuResourceStoreRawV3F16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv3f16 Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.v3f16");

__attribute__((always_inline)) void femeCpuResourceStoreRawV3F16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv3f16 Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 6);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 6);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!((OkRaw || OkStructured) && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  // See `femeCpuResourceStoreRawV3F32`'s comment: an explicit-size
  // `__builtin_memcpy` (not `sizeof(Value)`, which Clang may pad past 6
  // bytes) avoids the same out-of-bounds store-widening risk for this
  // odd-width `<3 x half>` overload.
  __builtin_memcpy(Ptr, &Value, 6);
}

FemeRTv4f16
femeCpuResourceLoadRawV4F16(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                            uint32_t DescriptorIndex, uint64_t ByteOffset,
                            _Bool Mask) asm("feme.cpu.resource.load.raw.v4f16");

__attribute__((always_inline)) FemeRTv4f16 femeCpuResourceLoadRawV4F16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 8);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 8);
  if (!((OkRaw || OkStructured) && Mask))
    return (FemeRTv4f16){0.0f, 0.0f, 0.0f, 0.0f};
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  return *(const FemeRTv4f16Unaligned *)Ptr;
}

void femeCpuResourceStoreRawV4F16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv4f16 Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.v4f16");

__attribute__((always_inline)) void femeCpuResourceStoreRawV4F16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv4f16 Value, _Bool Mask) {
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
  *(FemeRTv4f16Unaligned *)Ptr = (FemeRTv4f16Unaligned)Value;
}

// (Roadmap H124c) `feme.cpu.resource.load.raw.v2i16`/`.v3i16`/`.v4i16` and
// their `.store.raw.*` counterparts: the `int16_t`/`uint16_t`-element
// vector siblings of `.v2f16`/`.v3f16`/`.v4f16` above, needed for an HLSL
// `int16_t2`/`int16_t3`/`int16_t4` (or `uint16_t`) raw/structured-buffer
// load or store (e.g. `WaveOps/*.int16.test`'s own input buffers).
FemeRTv2i16
femeCpuResourceLoadRawV2I16(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                            uint32_t DescriptorIndex, uint64_t ByteOffset,
                            _Bool Mask) asm("feme.cpu.resource.load.raw.v2i16");

__attribute__((always_inline)) FemeRTv2i16 femeCpuResourceLoadRawV2I16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 4);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 4);
  if (!((OkRaw || OkStructured) && Mask))
    return (FemeRTv2i16){0, 0};
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  return *(const FemeRTv2i16Unaligned *)Ptr;
}

void femeCpuResourceStoreRawV2I16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv2i16 Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.v2i16");

__attribute__((always_inline)) void femeCpuResourceStoreRawV2I16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv2i16 Value, _Bool Mask) {
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
  *(FemeRTv2i16Unaligned *)Ptr = (FemeRTv2i16Unaligned)Value;
}

FemeRTv3i16
femeCpuResourceLoadRawV3I16(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                            uint32_t DescriptorIndex, uint64_t ByteOffset,
                            _Bool Mask) asm("feme.cpu.resource.load.raw.v3i16");

__attribute__((always_inline)) FemeRTv3i16 femeCpuResourceLoadRawV3I16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 6);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 6);
  if (!((OkRaw || OkStructured) && Mask))
    return (FemeRTv3i16){0, 0, 0};
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  FemeRTv3i16 V;
  __builtin_memcpy(&V, Ptr, 6);
  return V;
}

void femeCpuResourceStoreRawV3I16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv3i16 Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.v3i16");

__attribute__((always_inline)) void femeCpuResourceStoreRawV3I16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv3i16 Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 6);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 6);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!((OkRaw || OkStructured) && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  // See `femeCpuResourceStoreRawV3F32`'s comment: an explicit-size
  // `__builtin_memcpy` avoids the same out-of-bounds store-widening risk
  // for this odd-width `<3 x i16>` overload.
  __builtin_memcpy(Ptr, &Value, 6);
}

FemeRTv4i16
femeCpuResourceLoadRawV4I16(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                            uint32_t DescriptorIndex, uint64_t ByteOffset,
                            _Bool Mask) asm("feme.cpu.resource.load.raw.v4i16");

__attribute__((always_inline)) FemeRTv4i16 femeCpuResourceLoadRawV4I16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset, 8);
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset, 8);
  if (!((OkRaw || OkStructured) && Mask))
    return (FemeRTv4i16){0, 0, 0, 0};
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  return *(const FemeRTv4i16Unaligned *)Ptr;
}

void femeCpuResourceStoreRawV4I16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv4i16 Value,
    _Bool Mask) asm("feme.cpu.resource.store.raw.v4i16");

__attribute__((always_inline)) void femeCpuResourceStoreRawV4I16(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, FemeRTv4i16 Value, _Bool Mask) {
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
  *(FemeRTv4i16Unaligned *)Ptr = (FemeRTv4i16Unaligned)Value;
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
  // (Roadmap L125(d)) Packed per-output-channel component swizzle (one
  // byte per channel, mirroring `VK_COMPONENT_SWIZZLE_*`'s own numeric
  // ordering -- see `feme::cpu::ImageComponentSwizzle`'s own comment,
  // RuntimeABI.h): 0=Identity, 1=Zero, 2=One, 3=R, 4=G, 5=B, 6=A. Taken
  // from this struct's own former `Reserved[3]` headroom (now
  // `Reserved[2]`).
  uint32_t Swizzle;
  uint32_t Reserved[2];
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
  // (Roadmap H19n) `R32G32_UINT`/`R32G32_SINT`: the storage-mandatory
  // two-component partial siblings of `R32G32B32A32_{UINT,SINT}`, one
  // 32-bit scalar per lane, matching `R32G32_FLOAT`'s own size.
  case 6:  // R32G32_UINT
  case 10: // R32G32_SINT
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
  case 19: // R16G16B16A16_UNORM (roadmap H19h)
  case 20: // R16G16B16A16_SNORM (roadmap H19h)
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
  // (Roadmap H8e) `B4G4R4A4_UNORM`/`A1R5G5B5_UNORM`: two more of roadmap
  // H7r's own packed 16-bit formats, now also sampled (like
  // `A1B5G5R5_UNORM` above) rather than only `packClearColor`/
  // `unpackColor`-backed.
  case 79: // B4G4R4A4_UNORM (packed into a single 2-byte word)
    return 2;
  case 84: // A1R5G5B5_UNORM (packed into a single 2-byte word)
    return 2;
  // (Roadmap H8g) `R5G6B5_UNORM`/`B5G6R5_UNORM`: the last two of roadmap
  // H7r's own packed 16-bit formats without a real runtime sampling case,
  // a CTS-confirmed genuine `SAMPLED_IMAGE_BIT` gap (mirroring H8e's own
  // precedent for `B4G4R4A4_UNORM`/`A1R5G5B5_UNORM` above).
  case 80: // R5G6B5_UNORM (packed into a single 2-byte word)
    return 2;
  case 81: // B5G6R5_UNORM (packed into a single 2-byte word)
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
  // (Roadmap H19j) `R8_{UNORM,SNORM,UINT,SINT}`: a single byte each.
  case 85: // R8_UNORM
  case 86: // R8_SNORM
  case 87: // R8_UINT
  case 88: // R8_SINT
    return 1;
  // (Roadmap H19n) `R8G8_{UNORM,SNORM,UINT,SINT}`: two bytes each.
  case 89: // R8G8_UNORM
  case 90: // R8G8_SNORM
  case 91: // R8G8_UINT
  case 92: // R8G8_SINT
    return 2;
  // (Roadmap H19n) `R16_{FLOAT,UNORM,SNORM,UINT,SINT}`: two bytes each.
  case 93: // R16_FLOAT
  case 94: // R16_UNORM
  case 95: // R16_SNORM
  case 96: // R16_UINT
  case 97: // R16_SINT
    return 2;
  // (Roadmap H19n) `R16G16_{FLOAT,UNORM,SNORM,UINT,SINT}`: four bytes
  // each.
  case 98:  // R16G16_FLOAT
  case 99:  // R16G16_UNORM
  case 100: // R16G16_SNORM
  case 101: // R16G16_UINT
  case 102: // R16G16_SINT
    return 4;
  // (Roadmap H19o) `R10G10B10A2_{SNORM,SINT}`: packed into the same
  // single 4-byte word as their unsigned siblings (cases 24/25 above).
  case 103: // R10G10B10A2_SNORM
  case 104: // R10G10B10A2_SINT
    return 4;
  // (Roadmap H8q) `E5B9G9R9_UFLOAT` (`VK_FORMAT_E5B9G9R9_UFLOAT_PACK32`):
  // a shared-exponent RGB9E5 value, also packed into a single 4-byte
  // word like `R11G11B10_FLOAT` (case 23) above.
  case 131: // E5B9G9R9_UFLOAT
    return 4;
  // (Roadmap H8r) `B8G8R8A8_UNORM_SRGB`: the sRGB sibling of
  // `B8G8R8A8_UNORM` (case 26) above, same 4-byte-per-texel layout.
  case 132: // B8G8R8A8_UNORM_SRGB
    return 4;
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

// Converts a `float` to an IEEE 754 binary16 ("half float") bit pattern,
// the inverse of `femeRTHalfToFloat` above -- written by hand for the same
// freestanding-build reason. Ties round to nearest-even; a magnitude too
// large for binary16 saturates to +/-infinity rather than wrapping, and a
// magnitude too small to represent even as a subnormal flushes to +/-zero
// (matching SPIR-V's own `OpFConvert`-to-half rounding behavior, the
// operation `imageStore` into an `R16G16B16A16_SFLOAT` storage image
// implicitly performs).
__attribute__((always_inline)) static uint16_t femeRTFloatToHalf(float F) {
  uint32_t Bits;
  __builtin_memcpy(&Bits, &F, sizeof(Bits));
  uint32_t Sign = (Bits >> 16) & 0x8000u;
  int32_t Exp = (int32_t)((Bits >> 23) & 0xFFu) - 127 + 15;
  uint32_t Mant = Bits & 0x7FFFFFu;
  if (((Bits >> 23) & 0xFFu) == 0xFFu) {
    // Infinity or NaN: preserve, collapsing any mantissa down to binary16's
    // own 10-bit field (keeping at least one set bit so a NaN stays a NaN).
    return (uint16_t)(Sign | 0x7C00u | (Mant ? (Mant >> 13) | 1u : 0u));
  }
  if (Exp >= 0x1F) {
    return (uint16_t)(Sign | 0x7C00u); // Overflow: saturate to infinity.
  }
  if (Exp <= 0) {
    if (Exp < -10) {
      return (uint16_t)Sign; // Underflow: flush to zero.
    }
    // Subnormal half: shift the implicit-leading-1 mantissa right by the
    // exponent's own shortfall, rounding the bits shifted out to nearest,
    // ties to even.
    Mant |= 0x800000u;
    uint32_t Shift = (uint32_t)(14 - Exp);
    uint32_t Half = Mant >> Shift;
    uint32_t Rem = Mant & ((1u << Shift) - 1u);
    uint32_t RoundBit = 1u << (Shift - 1);
    if (Rem > RoundBit || (Rem == RoundBit && (Half & 1u)))
      ++Half;
    return (uint16_t)(Sign | Half);
  }
  // Normal half: round the 23-bit mantissa down to 10 bits, ties to even.
  uint32_t Half = Mant >> 13;
  uint32_t Rem = Mant & 0x1FFFu;
  if (Rem > 0x1000u || (Rem == 0x1000u && (Half & 1u)))
    ++Half;
  if (Half & 0x400u) { // Mantissa rounded up into the exponent.
    Half = 0;
    ++Exp;
    if (Exp >= 0x1F)
      return (uint16_t)(Sign | 0x7C00u);
  }
  return (uint16_t)(Sign | ((uint32_t)Exp << 10) | Half);
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

// (Roadmap H19n) The inverse of `femeRTUnpackR10G10B10A2Unorm` above:
// clamps each component to `[0.0, 1.0]`, scales R/G/B to `[0, 1023]` and A
// to `[0, 3]`, and packs the four rounded fields into one `uint32_t` in
// the same MSB-down `A2B10G10R10` bit layout the unpack side reads.
__attribute__((always_inline)) static uint32_t
femeRTPackR10G10B10A2Unorm(FemeRTv4f32 Value) {
  float C0 = __builtin_fminf(__builtin_fmaxf(Value[0], 0.0f), 1.0f);
  float C1 = __builtin_fminf(__builtin_fmaxf(Value[1], 0.0f), 1.0f);
  float C2 = __builtin_fminf(__builtin_fmaxf(Value[2], 0.0f), 1.0f);
  float C3 = __builtin_fminf(__builtin_fmaxf(Value[3], 0.0f), 1.0f);
  uint32_t I0 = (uint32_t)__builtin_roundf(C0 * 1023.0f);
  uint32_t I1 = (uint32_t)__builtin_roundf(C1 * 1023.0f);
  uint32_t I2 = (uint32_t)__builtin_roundf(C2 * 1023.0f);
  uint32_t I3 = (uint32_t)__builtin_roundf(C3 * 3.0f);
  return (I0 & 0x3FFu) | ((I1 & 0x3FFu) << 10) | ((I2 & 0x3FFu) << 20) |
         ((I3 & 0x3u) << 30);
}

// Unpacks a `R10G10B10A2_SNORM` value (`VK_FORMAT_A2B10G10R10_SNORM_PACK32`:
// same MSB-down `A2B10G10R10` bit layout as `R10G10B10A2_UNORM` above, but
// each field is a signed fixed-point value) into a `<4 x float>` in
// `[-1.0, 1.0]`, per the Vulkan spec's SNORM conversion (the same
// `max(c / (2^(bits-1) - 1), -1.0)` rule `femeRTUnpackR8G8B8A8Snorm` uses,
// just with 10-bit R/G/B fields (sign-extended from bit 9, scaled by 511)
// and a 2-bit A field (sign-extended from bit 1, scaled by 1)) -- roadmap
// H19o, the final mandatory `shaderStorageImageExtendedFormats` format.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR10G10B10A2Snorm(uint32_t Raw) {
  // Sign-extend each 10-bit field by left-shifting its sign bit to bit 31
  // then arithmetic-shifting back down, the same trick used elsewhere in
  // this file for narrower-than-32-bit signed fields.
  int32_t R10 = ((int32_t)(Raw << 22)) >> 22;
  int32_t G10 = ((int32_t)(Raw << 12)) >> 22;
  int32_t B10 = ((int32_t)(Raw << 2)) >> 22;
  int32_t A2 = ((int32_t)Raw) >> 30;
  FemeRTv4f32 V;
  V[0] = __builtin_fmaxf((float)R10 / 511.0f, -1.0f);
  V[1] = __builtin_fmaxf((float)G10 / 511.0f, -1.0f);
  V[2] = __builtin_fmaxf((float)B10 / 511.0f, -1.0f);
  V[3] = __builtin_fmaxf((float)A2 / 1.0f, -1.0f);
  return V;
}

// (Roadmap H19o) The inverse of `femeRTUnpackR10G10B10A2Snorm` above:
// clamps each component to `[-1.0, 1.0]`, scales R/G/B to `[-511, 511]`
// and A to `[-1, 1]`, and packs the four rounded fields into one
// `uint32_t` in the same MSB-down `A2B10G10R10` bit layout the unpack
// side reads.
__attribute__((always_inline)) static uint32_t
femeRTPackR10G10B10A2Snorm(FemeRTv4f32 Value) {
  float C0 = __builtin_fminf(__builtin_fmaxf(Value[0], -1.0f), 1.0f);
  float C1 = __builtin_fminf(__builtin_fmaxf(Value[1], -1.0f), 1.0f);
  float C2 = __builtin_fminf(__builtin_fmaxf(Value[2], -1.0f), 1.0f);
  float C3 = __builtin_fminf(__builtin_fmaxf(Value[3], -1.0f), 1.0f);
  uint32_t I0 = (uint32_t)(int32_t)__builtin_roundf(C0 * 511.0f);
  uint32_t I1 = (uint32_t)(int32_t)__builtin_roundf(C1 * 511.0f);
  uint32_t I2 = (uint32_t)(int32_t)__builtin_roundf(C2 * 511.0f);
  uint32_t I3 = (uint32_t)(int32_t)__builtin_roundf(C3 * 1.0f);
  return (I0 & 0x3FFu) | ((I1 & 0x3FFu) << 10) | ((I2 & 0x3FFu) << 20) |
         ((I3 & 0x3u) << 30);
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

// (Roadmap H19n) The inverse of `femeRTUnpackR11G11B10Float` above: clamps
// each of R/G/B to `[0.0, +inf)` (this format is unsigned, no sign bit),
// encodes each with `femeRTFloatToHalf`, then right-shifts each binary16
// result back down by the same amount the unpack side shifted up by (4
// for the 11-bit R/G fields' 6-bit mantissa, 5 for the 10-bit B field's
// 5-bit mantissa) to recover each field's own narrower exponent+mantissa
// bit pattern, and packs the three fields LSB-up into one `uint32_t`.
__attribute__((always_inline)) static uint32_t
femeRTPackR11G11B10Float(FemeRTv4f32 Value) {
  float C0 = __builtin_fmaxf(Value[0], 0.0f);
  float C1 = __builtin_fmaxf(Value[1], 0.0f);
  float C2 = __builtin_fmaxf(Value[2], 0.0f);
  uint32_t R11 = ((uint32_t)femeRTFloatToHalf(C0) >> 4) & 0x7FFu;
  uint32_t G11 = ((uint32_t)femeRTFloatToHalf(C1) >> 4) & 0x7FFu;
  uint32_t B10 = ((uint32_t)femeRTFloatToHalf(C2) >> 5) & 0x3FFu;
  return R11 | (G11 << 11) | (B10 << 22);
}

// (Roadmap H8q) Returns `2^Exp` as a `float`, built directly from its
// IEEE 754 bit pattern (an exponent field of `Exp + 127`, zero mantissa)
// rather than via `__builtin_powf` -- exact for every `Exp` this file's
// own `E5B9G9R9_UFLOAT` callers below pass it (that format's bounded
// exponent range keeps the result comfortably inside binary32's normal
// range), and cheaper.
__attribute__((always_inline)) static float femeRTExp2(int32_t Exp) {
  uint32_t Bits = (uint32_t)(Exp + 127) << 23;
  float F;
  __builtin_memcpy(&F, &Bits, sizeof(F));
  return F;
}

// (Roadmap H8q) Returns `floor(log2(X))` for a normalized, positive
// `float` `X` -- reads the IEEE 754 exponent field directly (valid since
// `1.0 <= mantissa < 2.0` for any normalized value, so the stored
// exponent field minus its 127 bias already equals the floor) rather
// than computing a real logarithm, matching the Khronos
// `EXT_texture_shared_exponent` spec's own reference "FloorLog2" helper's
// own bit-trick definition.
__attribute__((always_inline)) static int32_t femeRTFloorLog2(float X) {
  uint32_t Bits;
  __builtin_memcpy(&Bits, &X, sizeof(Bits));
  return (int32_t)((Bits >> 23) & 0xFFu) - 127;
}

// (Roadmap H8q) Unpacks an `E5B9G9R9_UFLOAT` value
// (`VK_FORMAT_E5B9G9R9_UFLOAT_PACK32`): from the LSB up, a 9-bit R
// mantissa, a 9-bit G mantissa, a 9-bit B mantissa, then a 5-bit shared
// exponent (bias 15) at the top -- each channel's real value is
// `mantissa * 2^(exponent - 15 - 9)`, per the Khronos
// `EXT_texture_shared_exponent` spec's own reference decode. Unlike
// `femeRTUnpackR11G11B10Float` above, all three channels share one
// exponent field rather than each carrying its own. Alpha is always
// `1.0` (this format carries no alpha channel).
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackRGB9E5(uint32_t Raw) {
  uint32_t ExpBits = (Raw >> 27) & 0x1Fu;
  float Scale = femeRTExp2((int32_t)ExpBits - 15 - 9);
  FemeRTv4f32 V;
  V[0] = (float)(Raw & 0x1FFu) * Scale;
  V[1] = (float)((Raw >> 9) & 0x1FFu) * Scale;
  V[2] = (float)((Raw >> 18) & 0x1FFu) * Scale;
  V[3] = 1.0f;
  return V;
}

// (Roadmap H8q) The inverse of `femeRTUnpackRGB9E5` above: chooses the
// smallest shared exponent that can represent the single largest of
// R/G/B without mantissa overflow, then rounds each channel to that
// exponent's own 9-bit mantissa -- the real "range reduction" this
// format's shared exponent needs, unlike `femeRTPackR11G11B10Float`'s
// three independent per-channel exponents above. Mirrors the Khronos
// `EXT_texture_shared_exponent` spec's own reference `FloatsToRGB9E5`
// algorithm (clamp to `[0, MAX_RGB9E5]`, choose `exp_shared = max(-bias -
// 1, FloorLog2(maxrgb)) + 1 + bias`, round each channel to that
// exponent's own mantissa, then bump the exponent once more and re-round
// if rounding overflowed the 9-bit mantissa).
__attribute__((always_inline)) static uint32_t
femeRTPackRGB9E5(FemeRTv4f32 Value) {
  const int32_t Bias = 15;
  const int32_t MantissaBits = 9;
  const int32_t MaxExp = 31; // 5-bit field, max biased exponent.
  const float MaxValue = (511.0f / 512.0f) * femeRTExp2(MaxExp - Bias);
  float R = __builtin_fminf(__builtin_fmaxf(Value[0], 0.0f), MaxValue);
  float G = __builtin_fminf(__builtin_fmaxf(Value[1], 0.0f), MaxValue);
  float B = __builtin_fminf(__builtin_fmaxf(Value[2], 0.0f), MaxValue);
  float MaxRGB = __builtin_fmaxf(__builtin_fmaxf(R, G), B);
  int32_t ExpShared;
  if (MaxRGB <= 0.0f) {
    ExpShared = 0;
  } else {
    int32_t Floor = femeRTFloorLog2(MaxRGB);
    if (Floor < -Bias - 1)
      Floor = -Bias - 1;
    ExpShared = Floor + 1 + Bias;
    if (ExpShared > MaxExp)
      ExpShared = MaxExp;
  }
  float Denom = femeRTExp2(ExpShared - Bias - MantissaBits);
  uint32_t Rm = (uint32_t)__builtin_roundf(R / Denom);
  uint32_t Gm = (uint32_t)__builtin_roundf(G / Denom);
  uint32_t Bm = (uint32_t)__builtin_roundf(B / Denom);
  uint32_t MaxM = Rm > Gm ? Rm : Gm;
  MaxM = MaxM > Bm ? MaxM : Bm;
  if (MaxM > 511u && ExpShared < MaxExp) {
    // Rounding pushed the mantissa one bit too wide for this exponent --
    // bump the shared exponent once more and re-round every channel, per
    // the reference algorithm's own overflow handling.
    ++ExpShared;
    Denom = femeRTExp2(ExpShared - Bias - MantissaBits);
    Rm = (uint32_t)__builtin_roundf(R / Denom);
    Gm = (uint32_t)__builtin_roundf(G / Denom);
    Bm = (uint32_t)__builtin_roundf(B / Denom);
  }
  if (Rm > 511u)
    Rm = 511u;
  if (Gm > 511u)
    Gm = 511u;
  if (Bm > 511u)
    Bm = 511u;
  return (Rm & 0x1FFu) | ((Gm & 0x1FFu) << 9) | ((Bm & 0x1FFu) << 18) |
         ((uint32_t)ExpShared << 27);
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

// (Roadmap H8e) Unpacks a `B4G4R4A4_UNORM` value
// (`VK_FORMAT_B4G4R4A4_UNORM_PACK16`: from the MSB down, 4 bits of B, 4
// bits of G, 4 bits of R, 4 bits of A) into a `<4 x float>` in
// `[0.0, 1.0]`.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackB4G4R4A4Unorm(uint16_t Raw) {
  FemeRTv4f32 V;
  V[3] = (float)(Raw & 0xFu) / 15.0f;
  V[0] = (float)((Raw >> 4) & 0xFu) / 15.0f;
  V[1] = (float)((Raw >> 8) & 0xFu) / 15.0f;
  V[2] = (float)((Raw >> 12) & 0xFu) / 15.0f;
  return V;
}

// (Roadmap H8e) Unpacks an `A1R5G5B5_UNORM` value
// (`VK_FORMAT_A1R5G5B5_UNORM_PACK16`: from the MSB down, 1 bit of A, 5
// bits of R, 5 bits of G, 5 bits of B) into a `<4 x float>` in
// `[0.0, 1.0]`.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackA1R5G5B5Unorm(uint16_t Raw) {
  FemeRTv4f32 V;
  V[2] = (float)(Raw & 0x1Fu) / 31.0f;
  V[1] = (float)((Raw >> 5) & 0x1Fu) / 31.0f;
  V[0] = (float)((Raw >> 10) & 0x1Fu) / 31.0f;
  V[3] = (float)((Raw >> 15) & 0x1u);
  return V;
}

// (Roadmap H8g) Unpacks an `R5G6B5_UNORM`/`B5G6R5_UNORM` value
// (`VK_FORMAT_R5G6B5_UNORM_PACK16`/`VK_FORMAT_B5G6R5_UNORM_PACK16`: from
// the MSB down, 5 bits of the first channel, 6 bits of G, 5 bits of the
// third channel, no alpha) into a `<4 x float>` in `[0.0, 1.0]`, alpha
// padding an implicit, unwritable `1.0` (this pair has no alpha channel
// at all, unlike every other packed 16-bit format above). `FirstIsRed`
// selects which channel order (`R5G6B5` vs `B5G6R5`) to decode into.
__attribute__((always_inline)) static FemeRTv4f32
femeRTUnpackR5G6B5Unorm(uint16_t Raw, int FirstIsRed) {
  FemeRTv4f32 V;
  float First = (float)((Raw >> 11) & 0x1Fu) / 31.0f;
  float G = (float)((Raw >> 5) & 0x3Fu) / 63.0f;
  float Third = (float)(Raw & 0x1Fu) / 31.0f;
  V[0] = FirstIsRed ? First : Third;
  V[1] = G;
  V[2] = FirstIsRed ? Third : First;
  V[3] = 1.0f;
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
  case 19: { // R16G16B16A16_UNORM (roadmap H19h)
    uint16_t Raw[4];
    __builtin_memcpy(Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR16G16B16A16Unorm(Raw);
  }
  case 20: { // R16G16B16A16_SNORM (roadmap H19h)
    uint16_t Raw[4];
    __builtin_memcpy(Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR16G16B16A16Snorm(Raw);
  }
  case 23: { // R11G11B10_FLOAT
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR11G11B10Float(Raw);
  }
  case 131: { // E5B9G9R9_UFLOAT (roadmap H8q)
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackRGB9E5(Raw);
  }
  case 24: { // R10G10B10A2_UNORM
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR10G10B10A2Unorm(Raw);
  }
  case 103: { // R10G10B10A2_SNORM (roadmap H19o)
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR10G10B10A2Snorm(Raw);
  }
  case 26: { // B8G8R8A8_UNORM
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackB8G8R8A8Unorm(Raw);
  }
  case 132: { // B8G8R8A8_UNORM_SRGB (roadmap H8r)
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    FemeRTv4f32 V = femeRTUnpackB8G8R8A8Unorm(Raw);
    V[0] = femeRTSRGBToLinear(V[0]);
    V[1] = femeRTSRGBToLinear(V[1]);
    V[2] = femeRTSRGBToLinear(V[2]);
    return V;
  }
  case 27: { // A8_UNORM
    return femeRTUnpackA8Unorm(*Ptr);
  }
  case 28: { // A1B5G5R5_UNORM
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackA1B5G5R5Unorm(Raw);
  }
  case 79: { // B4G4R4A4_UNORM (roadmap H8e)
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackB4G4R4A4Unorm(Raw);
  }
  case 84: { // A1R5G5B5_UNORM (roadmap H8e)
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackA1R5G5B5Unorm(Raw);
  }
  case 80: { // R5G6B5_UNORM (roadmap H8g)
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR5G6B5Unorm(Raw, /*FirstIsRed=*/1);
  }
  case 81: { // B5G6R5_UNORM (roadmap H8g)
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR5G6B5Unorm(Raw, /*FirstIsRed=*/0);
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
  case 85: // R8_UNORM (roadmap H19j)
    return femeRTUnpackR8Unorm(*Ptr);
  case 86: // R8_SNORM (roadmap H19j)
    return femeRTUnpackR8Snorm(*Ptr);
  case 89: { // R8G8_UNORM (roadmap H19n)
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR8G8Unorm(Raw);
  }
  case 90: { // R8G8_SNORM (roadmap H19n)
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR8G8Snorm(Raw);
  }
  case 93: { // R16_FLOAT (roadmap H19n)
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    FemeRTv4f32 V = {femeRTHalfToFloat(Raw), 0.0f, 0.0f, 1.0f};
    return V;
  }
  case 94: { // R16_UNORM (roadmap H19n)
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR16Unorm(Raw);
  }
  case 95: { // R16_SNORM (roadmap H19n)
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR16Snorm(Raw);
  }
  case 98: { // R16G16_FLOAT (roadmap H19n)
    uint16_t Raw[2];
    __builtin_memcpy(Raw, Ptr, sizeof(Raw));
    FemeRTv4f32 V = {femeRTHalfToFloat(Raw[0]), femeRTHalfToFloat(Raw[1]),
                     0.0f, 1.0f};
    return V;
  }
  case 99: { // R16G16_UNORM (roadmap H19n)
    uint16_t Raw[2];
    __builtin_memcpy(Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR16G16Unorm(Raw);
  }
  case 100: { // R16G16_SNORM (roadmap H19n)
    uint16_t Raw[2];
    __builtin_memcpy(Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR16G16Snorm(Raw);
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

// (Roadmap H19n) The inverse of `femeRTUnpackR10G10B10A2Uint` above:
// truncates R/G/B down to 10 bits and A down to 2 bits, packing the four
// fields into one `uint32_t` in the same MSB-down `A2B10G10R10` bit
// layout the unpack side reads.
__attribute__((always_inline)) static uint32_t
femeRTPackR10G10B10A2Uint(FemeRTv4i32 Texel) {
  uint32_t I0 = (uint32_t)Texel[0];
  uint32_t I1 = (uint32_t)Texel[1];
  uint32_t I2 = (uint32_t)Texel[2];
  uint32_t I3 = (uint32_t)Texel[3];
  return (I0 & 0x3FFu) | ((I1 & 0x3FFu) << 10) | ((I2 & 0x3FFu) << 20) |
         ((I3 & 0x3u) << 30);
}

// Unpacks a `R10G10B10A2_SINT` value
// (`VK_FORMAT_A2B10G10R10_SINT_PACK32`: same MSB-down `A2B10G10R10` bit
// layout as `R10G10B10A2_UINT` above, but each field is a signed integer)
// into a `<4 x i32>` by sign-extending each field -- roadmap H19o. Unlike
// `femeRTPackR10G10B10A2Uint`'s pack side below (shared as-is by both
// formats, since truncating a two's-complement value to N bits produces
// the same bit pattern regardless of signedness), the *unpack* side does
// need its own signed variant here: zero-extending a field whose top bit
// is set would silently produce the wrong (positive rather than
// negative) value, exactly the same `_UINT`/`_SINT` asymmetry
// `femeRTUnpackR8G8B8A8Uint`/`Sint` already show above (sign-extend using
// the same left-shift/arithmetic-right-shift technique
// `femeRTUnpackR10G10B10A2Snorm` uses, just without that function's own
// `[-1.0, 1.0]` float scaling).
__attribute__((always_inline)) static FemeRTv4i32
femeRTUnpackR10G10B10A2Sint(uint32_t Raw) {
  FemeRTv4i32 V;
  V[0] = ((int32_t)(Raw << 22)) >> 22;
  V[1] = ((int32_t)(Raw << 12)) >> 22;
  V[2] = ((int32_t)(Raw << 2)) >> 22;
  V[3] = ((int32_t)Raw) >> 30;
  return V;
}

// The inverse of `femeRTUnpackR10G10B10A2Sint`: truncating a two's-
// complement value to its field width produces the same bit pattern
// `femeRTPackR10G10B10A2Uint` already computes, so this shares that
// implementation exactly -- kept as a separate, identically named-per-
// format entry point for symmetry with the unpack side above, where the
// sign extension does differ (mirroring `femeRTPackR8G8B8A8Sint`'s own
// wrapper-around-`Uint` precedent).
__attribute__((always_inline)) static uint32_t
femeRTPackR10G10B10A2Sint(FemeRTv4i32 Texel) {
  return femeRTPackR10G10B10A2Uint(Texel);
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
  // (Roadmap H19n) `R32G32_UINT`/`R32G32_SINT`: the two-component
  // identity siblings of `R32_{UINT,SINT}` above; the unread B component
  // pads `0`, alpha pads `1`, matching `femeRTUnpackImageTexel`'s own
  // partial-component convention (`R32G32_FLOAT` et al.).
  case 6:  // R32G32_UINT
  case 10: { // R32G32_SINT
    int32_t RG[2];
    __builtin_memcpy(RG, Ptr, sizeof(RG));
    FemeRTv4i32 V = {RG[0], RG[1], 0, 1};
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
  case 104: { // R10G10B10A2_SINT (roadmap H19o): unlike the pack side
              // below, the unpack side needs real sign-extension, not a
              // reuse of `R10G10B10A2_UINT`'s own zero-extending unpack
              // (see `femeRTUnpackR10G10B10A2Sint`'s own comment for why).
    uint32_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR10G10B10A2Sint(Raw);
  }
  case 87: // R8_UINT (roadmap H19j)
    return femeRTUnpackR8Uint(*Ptr);
  case 88: // R8_SINT (roadmap H19j)
    return femeRTUnpackR8Sint(*Ptr);
  case 91: { // R8G8_UINT (roadmap H19n)
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR8G8Uint(Raw);
  }
  case 92: { // R8G8_SINT (roadmap H19n)
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR8G8Sint(Raw);
  }
  case 96: { // R16_UINT (roadmap H19n)
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR16Uint(Raw);
  }
  case 97: { // R16_SINT (roadmap H19n)
    uint16_t Raw;
    __builtin_memcpy(&Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR16Sint(Raw);
  }
  case 101: { // R16G16_UINT (roadmap H19n)
    uint16_t Raw[2];
    __builtin_memcpy(Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR16G16Uint(Raw);
  }
  case 102: { // R16G16_SINT (roadmap H19n)
    uint16_t Raw[2];
    __builtin_memcpy(Raw, Ptr, sizeof(Raw));
    return femeRTUnpackR16G16Sint(Raw);
  }
  default:
    return Zero;
  }
}

// Packs \p Texel back into `Format`'s own bytes at \p Ptr -- the write-side
// mirror of `femeRTUnpackImageTexel` above, for `feme.cpu.image.store.2d.
// v4f32` (roadmap H19a, widened by H19f/H19h). Originally scoped to
// exactly the two float-channel formats the Vulkan spec's own mandatory
// storage-image format floor requires: `R32_FLOAT` and
// `R32G32B32A32_FLOAT`, both the identity case (no scalar conversion).
// Roadmap H19f added `R16G16B16A16_FLOAT`, encoding each lane with
// `femeRTFloatToHalf`; roadmap H19h adds `R16G16B16A16_UNORM`/`_SNORM`,
// quantizing each lane with `femeRTPackR16G16B16A16Unorm`/`Snorm` -- still
// only a step towards the full `shaderStorageImageExtendedFormats` list
// (see that roadmap row and `Format.cpp`'s own updated scope comment for
// what remains). A write through any other format -- reachable only if a
// future row widens `Format.cpp`'s own `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT`
// gate beyond this floor without widening this switch to match -- is
// silently dropped, mirroring `femeRTUnpackImageTexel`'s own "all-zero for
// an unmodeled format" default rather than trapping.
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
  case 2: { // R32G32_FLOAT (roadmap H8d): the two-component identity
            // sibling of `R32G32B32A32_FLOAT` above, only the first two
            // components are stored -- newly needed so a
            // `VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT` texel-buffer
            // write through this format (Format.cpp's
            // `isStorageTexelBufferFormatSupported`) is real, not
            // silently dropped.
    float RG[2] = {Texel[0], Texel[1]};
    __builtin_memcpy(Ptr, RG, sizeof(RG));
    return;
  }
  case 18: { // R16G16B16A16_FLOAT (roadmap H19f).
    uint16_t Raw[4] = {femeRTFloatToHalf(Texel[0]), femeRTFloatToHalf(Texel[1]),
                       femeRTFloatToHalf(Texel[2]), femeRTFloatToHalf(Texel[3])};
    __builtin_memcpy(Ptr, Raw, sizeof(Raw));
    return;
  }
  case 19: { // R16G16B16A16_UNORM (roadmap H19h).
    uint16_t Raw[4];
    femeRTPackR16G16B16A16Unorm(Texel, Raw);
    __builtin_memcpy(Ptr, Raw, sizeof(Raw));
    return;
  }
  case 20: { // R16G16B16A16_SNORM (roadmap H19h).
    uint16_t Raw[4];
    femeRTPackR16G16B16A16Snorm(Texel, Raw);
    __builtin_memcpy(Ptr, Raw, sizeof(Raw));
    return;
  }
  case 85: // R8_UNORM (roadmap H19j).
    *Ptr = femeRTPackR8Unorm(Texel);
    return;
  case 86: // R8_SNORM (roadmap H19j).
    *Ptr = femeRTPackR8Snorm(Texel);
    return;
  case 89: { // R8G8_UNORM (roadmap H19n).
    uint16_t Raw = femeRTPackR8G8Unorm(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 90: { // R8G8_SNORM (roadmap H19n).
    uint16_t Raw = femeRTPackR8G8Snorm(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 93: { // R16_FLOAT (roadmap H19n).
    uint16_t Raw = femeRTFloatToHalf(Texel[0]);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 94: { // R16_UNORM (roadmap H19n).
    uint16_t Raw = femeRTPackR16Unorm(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 95: { // R16_SNORM (roadmap H19n).
    uint16_t Raw = femeRTPackR16Snorm(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 98: { // R16G16_FLOAT (roadmap H19n).
    uint16_t Raw[2] = {femeRTFloatToHalf(Texel[0]), femeRTFloatToHalf(Texel[1])};
    __builtin_memcpy(Ptr, Raw, sizeof(Raw));
    return;
  }
  case 99: { // R16G16_UNORM (roadmap H19n).
    uint16_t Raw[2];
    femeRTPackR16G16Unorm(Texel, Raw);
    __builtin_memcpy(Ptr, Raw, sizeof(Raw));
    return;
  }
  case 100: { // R16G16_SNORM (roadmap H19n).
    uint16_t Raw[2];
    femeRTPackR16G16Snorm(Texel, Raw);
    __builtin_memcpy(Ptr, Raw, sizeof(Raw));
    return;
  }
  case 23: { // R11G11B10_FLOAT (roadmap H19n, packed into one 4-byte word).
    uint32_t Raw = femeRTPackR11G11B10Float(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 131: { // E5B9G9R9_UFLOAT (roadmap H8q, packed into one 4-byte word).
    uint32_t Raw = femeRTPackRGB9E5(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 24: { // R10G10B10A2_UNORM (roadmap H19n, packed into one 4-byte
             // word).
    uint32_t Raw = femeRTPackR10G10B10A2Unorm(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 103: { // R10G10B10A2_SNORM (roadmap H19o, packed into one 4-byte
              // word).
    uint32_t Raw = femeRTPackR10G10B10A2Snorm(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 14: { // R8G8B8A8_SNORM (roadmap H19n): a real mandatory
             // `shaderStorageImageExtendedFormats` entry, not just
             // covered by this project's own texel-buffer conversion
             // path -- reuses `femeRTPackR8G8B8A8Snorm` (already defined
             // for that other path).
    uint32_t Raw = femeRTPackR8G8B8A8Snorm(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 13: { // R8G8B8A8_UNORM (roadmap H8d): needed so
             // `femeCpuResourceStoreTypedV4F32`'s new generic-table
             // dispatch keeps writing this format identically to its old
             // hard-coded special case -- reuses `femeRTPackR8G8B8A8Unorm`
             // (already defined for the texel-buffer path).
    uint32_t Raw = femeRTPackR8G8B8A8Unorm(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  default:
    return;
  }
}

// The integer counterpart of `femeRTPackImageTexel` above, originally
// covering the mandatory storage-image format floor's two integer-channel
// formats, `R32_UINT`/`R32_SINT` and `R32G32B32A32_UINT`/`_SINT` -- both
// identity formats (a signed/unsigned 32-bit scalar's bit pattern is
// stored as-is either way), mirroring `femeRTUnpackImageTexelI32`'s own
// read-side treatment of the same four formats. Roadmap H19f adds
// `R16G16B16A16_UINT`/`_SINT`, truncating each 32-bit lane down to its
// own 16-bit word (matching SPIR-V's own `OpUConvert`/`OpSConvert`-to-i16
// truncation an `imageStore` into this format implicitly performs).
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
  // (Roadmap H19n) `R32G32_UINT`/`R32G32_SINT`: the two-component
  // identity siblings of `R32_{UINT,SINT}` above; only the first two
  // components are stored.
  case 6:  // R32G32_UINT
  case 10: { // R32G32_SINT
    int32_t RG[2] = {Texel[0], Texel[1]};
    __builtin_memcpy(Ptr, RG, sizeof(RG));
    return;
  }
  case 8:  // R32G32B32A32_UINT
  case 12: // R32G32B32A32_SINT: identity format, no conversion.
    *(FemeRTv4i32Unaligned *)Ptr = (FemeRTv4i32Unaligned)Texel;
    return;
  case 21: // R16G16B16A16_UINT
  case 22: { // R16G16B16A16_SINT (roadmap H19f): truncate to 16 bits.
    uint16_t Raw[4] = {(uint16_t)Texel[0], (uint16_t)Texel[1],
                       (uint16_t)Texel[2], (uint16_t)Texel[3]};
    __builtin_memcpy(Ptr, Raw, sizeof(Raw));
    return;
  }
  case 87: // R8_UINT (roadmap H19j).
    *Ptr = femeRTPackR8Uint(Texel);
    return;
  case 88: // R8_SINT (roadmap H19j).
    *Ptr = femeRTPackR8Sint(Texel);
    return;
  case 91: { // R8G8_UINT (roadmap H19n).
    uint16_t Raw = femeRTPackR8G8Uint(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 92: { // R8G8_SINT (roadmap H19n).
    uint16_t Raw = femeRTPackR8G8Sint(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 96: { // R16_UINT (roadmap H19n).
    uint16_t Raw = femeRTPackR16Uint(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 97: { // R16_SINT (roadmap H19n).
    uint16_t Raw = femeRTPackR16Sint(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 101: { // R16G16_UINT (roadmap H19n).
    uint16_t Raw[2];
    femeRTPackR16G16Uint(Texel, Raw);
    __builtin_memcpy(Ptr, Raw, sizeof(Raw));
    return;
  }
  case 102: { // R16G16_SINT (roadmap H19n).
    uint16_t Raw[2];
    femeRTPackR16G16Sint(Texel, Raw);
    __builtin_memcpy(Ptr, Raw, sizeof(Raw));
    return;
  }
  case 25: { // R10G10B10A2_UINT (roadmap H19n, packed into one 4-byte
             // word).
    uint32_t Raw = femeRTPackR10G10B10A2Uint(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 104: { // R10G10B10A2_SINT (roadmap H19o): pack truncates to the
              // same bit pattern regardless of signedness, so this uses
              // `femeRTPackR10G10B10A2Sint` (itself a thin wrapper
              // around `Uint`'s implementation) -- see
              // `femeRTUnpackR10G10B10A2Sint`'s own comment for why the
              // *unpack* side above does need a real, separate helper.
    uint32_t Raw = femeRTPackR10G10B10A2Sint(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 16: { // R8G8B8A8_SINT (roadmap H19n): a real mandatory
             // `shaderStorageImageExtendedFormats` entry; reuses
             // `femeRTPackR8G8B8A8Sint` (already defined for the
             // texel-buffer conversion path).
    uint32_t Raw = femeRTPackR8G8B8A8Sint(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  case 15: { // R8G8B8A8_UINT (roadmap H8d): needed so
             // `femeCpuResourceStoreTypedV4I32`'s new generic-table
             // dispatch keeps writing this format identically to its old
             // hard-coded special case -- reuses `femeRTPackR8G8B8A8Uint`
             // (already defined for the texel-buffer path).
    uint32_t Raw = femeRTPackR8G8B8A8Uint(Texel);
    __builtin_memcpy(Ptr, &Raw, sizeof(Raw));
    return;
  }
  default:
    return;
  }
}

//--- Typed-buffer scalar (single-channel format) view -------------------

// `feme.cpu.resource.load.typed.f32` (roadmap L9): reads a scalar `float`
// element through a bindless typed-buffer descriptor whose real bound
// format has only one shader-visible channel (e.g. `R32_FLOAT`) -- the
// shape a single-channel `RWBuffer<float>`/`Buffer<float>` declares (see
// `isSupportedTexelElementType`'s own comment, SPIRVResourceLowering.cpp,
// for why this needs its own scalar entry point distinct from
// `femeCpuResourceLoadTypedV4F32` above). Reuses this file's own
// `femeRTImageFormatElementSize`/`femeRTUnpackImageTexel` per-format
// conversion tables (roadmap E25/E26) rather than duplicating a second,
// narrower one here, so this correctly decodes every format those tables
// already do, not just the `R32_FLOAT` identity case roadmap L9 was
// scoped from. Valid only when the bound format is genuinely
// single-channel, matching Vulkan's own format-compatibility requirement
// between a texel buffer view and the shader type that accesses it; a
// mismatched multi-channel format is not specially guarded against here
// any more than a real driver would specially guard against invalid API
// usage. `ResourceKind::Typed == 1`. An inactive lane, a failing
// bounds/kind check, or an unrecognized format (`ElemSize == 0`) reads as
// zero, matching every other typed-buffer load's own "reads zero" rule
// (see "Bounds checking").
float femeCpuResourceLoadTypedF32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex,
    _Bool Mask) asm("feme.cpu.resource.load.typed.f32");

__attribute__((always_inline)) float femeCpuResourceLoadTypedF32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  uint64_t ElemSize = femeRTImageFormatElementSize(Desc.Format);
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      ElemSize != 0 &&
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  if (!(AccessOK && Mask))
    return 0.0f;
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  return femeRTUnpackImageTexel(Desc.Format, Ptr)[0];
}

// `feme.cpu.resource.store.typed.f32` (roadmap L9): the store counterpart
// of `femeCpuResourceLoadTypedF32` above, with the same UAV check every
// typed-buffer store requires (see `femeCpuResourceStoreTypedV4F32`). Only
// the first component of the `<4 x float>` `femeRTPackImageTexel` expects
// is ever meaningful for a genuinely single-channel format -- see that
// function's own per-format "only the first component is stored" cases.
void femeCpuResourceStoreTypedF32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, float Value,
    _Bool Mask) asm("feme.cpu.resource.store.typed.f32");

__attribute__((always_inline)) void
femeCpuResourceStoreTypedF32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                             uint32_t DescriptorIndex, uint64_t ElementIndex,
                             float Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  uint64_t ElemSize = femeRTImageFormatElementSize(Desc.Format);
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      ElemSize != 0 &&
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!(AccessOK && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  FemeRTv4f32 Texel = {Value, 0.0f, 0.0f, 1.0f};
  femeRTPackImageTexel(Desc.Format, Ptr, Texel);
}

// `feme.cpu.resource.load.typed.i32` (roadmap L9): the integer counterpart
// of `femeCpuResourceLoadTypedF32` above, for a single-channel
// `RWBuffer<int>`/`RWBuffer<uint>`/`Buffer<int>`/`Buffer<uint>` (e.g.
// `R32_UINT`/`R32_SINT`) -- the exact shape this milestone's own
// `Basic/Matrix/*.test` reduction hit. Reuses
// `femeRTUnpackImageTexelI32` the same way the float view above reuses
// `femeRTUnpackImageTexel`.
int32_t femeCpuResourceLoadTypedI32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex,
    _Bool Mask) asm("feme.cpu.resource.load.typed.i32");

__attribute__((always_inline)) int32_t femeCpuResourceLoadTypedI32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  uint64_t ElemSize = femeRTImageFormatElementSize(Desc.Format);
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      ElemSize != 0 &&
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  if (!(AccessOK && Mask))
    return 0;
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  return femeRTUnpackImageTexelI32(Desc.Format, Ptr)[0];
}

// `feme.cpu.resource.store.typed.i32` (roadmap L9): the store counterpart
// of `femeCpuResourceLoadTypedI32` above, mirroring
// `femeCpuResourceStoreTypedF32`'s own UAV check and single-component
// scope.
void femeCpuResourceStoreTypedI32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.store.typed.i32");

__attribute__((always_inline)) void
femeCpuResourceStoreTypedI32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                             uint32_t DescriptorIndex, uint64_t ElementIndex,
                             int32_t Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  uint64_t ElemSize = femeRTImageFormatElementSize(Desc.Format);
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      ElemSize != 0 &&
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!(AccessOK && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  FemeRTv4i32 Texel = {Value, 0, 0, 1};
  femeRTPackImageTexelI32(Desc.Format, Ptr, Texel);
}

// `feme.cpu.resource.load.typed.v2f32` (roadmap L7a): reads a `<2 x
// float>` element through a bindless typed-buffer descriptor whose real
// bound format has exactly two shader-visible channels (e.g.
// `R32G32_FLOAT`) -- the shape a genuinely 2-channel `RWBuffer<float2>`/
// `Buffer<float2>` declares (see `isSupportedTexelElementType`'s own
// comment, SPIRVResourceLowering.cpp, for the real IR reduction --
// `Basic/Matrix/matrix_m-based_getter.test`'s own `RWBuffer<float2>
// OutVec2` -- that found this shape genuinely reachable, unlike this
// file's prior "never narrower than 4 or exactly 1" assumption). Reuses
// `femeRTImageFormatElementSize`/`femeRTUnpackImageTexel` the same way
// `femeCpuResourceLoadTypedV4F32` above does; only the first two lanes of
// the `<4 x float>` those tables always decode are kept. Like V4's own
// read side, `OpImageRead`/`OpImageFetch` never actually narrows to this
// shape in practice (SPIR-V's Image Instructions always return a full
// 4-component vector), so this load counterpart exists mainly for
// symmetry with the store below and for any narrower vector shape a
// future frontend might legitimately emit.
FemeRTv2f32 femeCpuResourceLoadTypedV2F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex,
    _Bool Mask) asm("feme.cpu.resource.load.typed.v2f32");

__attribute__((always_inline)) FemeRTv2f32 femeCpuResourceLoadTypedV2F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  uint64_t ElemSize = femeRTImageFormatElementSize(Desc.Format);
  if (ElemSize == 0)
    ElemSize = 16;
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  if (!(AccessOK && Mask))
    return (FemeRTv2f32){0.0f, 0.0f};
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  FemeRTv4f32 Texel = femeRTUnpackImageTexel(Desc.Format, Ptr);
  return (FemeRTv2f32){Texel[0], Texel[1]};
}

// `feme.cpu.resource.store.typed.v2f32` (roadmap L7a): the store
// counterpart of `femeCpuResourceLoadTypedV2F32` above, with the same UAV
// check every typed-buffer store requires (see
// `femeCpuResourceStoreTypedV4F32`). Widens \p Value to the full `<4 x
// float>` `femeRTPackImageTexel` expects, matching that function's own
// "only the bound format's real channel count is ever read back" padding
// convention (the padding B/A lanes here are never observed for a
// genuinely 2-channel format).
void femeCpuResourceStoreTypedV2F32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, FemeRTv2f32 Value,
    _Bool Mask) asm("feme.cpu.resource.store.typed.v2f32");

__attribute__((always_inline)) void
femeCpuResourceStoreTypedV2F32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                               uint32_t DescriptorIndex, uint64_t ElementIndex,
                               FemeRTv2f32 Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  uint64_t ElemSize = femeRTImageFormatElementSize(Desc.Format);
  if (ElemSize == 0)
    ElemSize = 16;
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!(AccessOK && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  FemeRTv4f32 Texel = {Value[0], Value[1], 0.0f, 1.0f};
  femeRTPackImageTexel(Desc.Format, Ptr, Texel);
}

// `feme.cpu.resource.load.typed.v2i32` (roadmap L7a): the integer
// counterpart of `femeCpuResourceLoadTypedV2F32` above, for a genuinely
// 2-channel `RWBuffer<int2>`/`RWBuffer<uint2>` (e.g. `R32G32_UINT`/
// `R32G32_SINT`). Reuses `femeRTUnpackImageTexelI32` the same way the
// float view above reuses `femeRTUnpackImageTexel`.
FemeRTv2i32 femeCpuResourceLoadTypedV2I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex,
    _Bool Mask) asm("feme.cpu.resource.load.typed.v2i32");

__attribute__((always_inline)) FemeRTv2i32 femeCpuResourceLoadTypedV2I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  uint64_t ElemSize = femeRTImageFormatElementSize(Desc.Format);
  if (ElemSize == 0)
    ElemSize = 16;
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  if (!(AccessOK && Mask))
    return (FemeRTv2i32){0, 0};
  const unsigned char *Ptr = (const unsigned char *)Desc.Data + ByteOffset;
  FemeRTv4i32 Texel = femeRTUnpackImageTexelI32(Desc.Format, Ptr);
  return (FemeRTv2i32){Texel[0], Texel[1]};
}

// `feme.cpu.resource.store.typed.v2i32` (roadmap L7a): the store
// counterpart of `femeCpuResourceLoadTypedV2I32` above, mirroring
// `femeCpuResourceStoreTypedV2F32`'s own UAV check and widening.
void femeCpuResourceStoreTypedV2I32(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, FemeRTv2i32 Value,
    _Bool Mask) asm("feme.cpu.resource.store.typed.v2i32");

__attribute__((always_inline)) void
femeCpuResourceStoreTypedV2I32(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                               uint32_t DescriptorIndex, uint64_t ElementIndex,
                               FemeRTv2i32 Value, _Bool Mask) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  uint64_t ElemSize = femeRTImageFormatElementSize(Desc.Format);
  if (ElemSize == 0)
    ElemSize = 16;
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!(AccessOK && Mask && IsUAV))
    return;
  unsigned char *Ptr = (unsigned char *)Desc.Data + ByteOffset;
  FemeRTv4i32 Texel = {Value[0], Value[1], 0, 1};
  femeRTPackImageTexelI32(Desc.Format, Ptr, Texel);
}

// Bounds-checked address helper for `feme.cpu.resource.atomic.*.typed.i32`
// (roadmap H8w): the storage-texel-buffer counterpart of
// `femeRTAtomicTexelAddress2D` above (`feme.cpu.image.atomic.*.2d.i32`,
// roadmap H8v) -- same real-hardware-atomic rationale (a real atomic
// needs a genuine pointer to operate on directly, unlike an ordinary
// store), same single-32-bit-scalar-format restriction (SPIR-V only ever
// allows a texel-buffer atomic against `R32_SINT`/`R32_UINT`, enforced
// upstream by `hasOnlySupportedPointerUses`, `SPIRVResourceLowering.cpp`),
// but addressed by descriptor index + element index like every other
// typed-buffer access (`femeCpuResourceLoadTypedI32`) rather than an
// image's own `(X, Y)` coordinate. Returns `NULL` on any failure
// (unbound/wrong-kind descriptor, non-UAV, out-of-bounds element, or an
// unexpected format), mirroring `femeRTAtomicTexelAddress2D`'s own
// fail-safe convention.
__attribute__((always_inline)) static int32_t *
femeRTAtomicResourceAddress(const FemeRTDescriptor *Heap, uint32_t HeapCount,
                           uint32_t DescriptorIndex, uint64_t ElementIndex) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  uint64_t ElemSize = femeRTImageFormatElementSize(Desc.Format);
  if (ElemSize != sizeof(int32_t))
    return (int32_t *)0;
  uint64_t ByteOffset = ElementIndex * ElemSize;
  _Bool AccessOK =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Typed=*/1, Desc.SizeInBytes,
                        Desc.Flags, ByteOffset, ElemSize);
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!(AccessOK && IsUAV))
    return (int32_t *)0;
  return (int32_t *)((unsigned char *)Desc.Data + ByteOffset);
}

// `feme.cpu.resource.atomic.add.typed.i32` (roadmap H8w): `OpAtomicIAdd`
// against a storage texel buffer (`R32_SINT`/`R32_UINT`) -- adds \p Value
// to the element at \p ElementIndex through descriptor \p DescriptorIndex,
// returning the pre-op value. See `femeCpuImageAtomicAdd2D`'s own doc
// (roadmap H8v) for the shared real-hardware-atomic/threading rationale;
// an unbound resource or an out-of-bounds/masked-off access returns 0,
// mirroring every other typed-buffer access's own "reads/no-ops as zero"
// rule.
int32_t femeCpuResourceAtomicAddTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.add.typed.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicAddTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr =
      femeRTAtomicResourceAddress(Heap, HeapCount, DescriptorIndex, ElementIndex);
  if (!Addr)
    return 0;
  return __atomic_fetch_add(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.sub.typed.i32` (roadmap H8w): `OpAtomicISub`'s
// counterpart to `feme.cpu.resource.atomic.add.typed.i32` above.
int32_t femeCpuResourceAtomicSubTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.sub.typed.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicSubTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr =
      femeRTAtomicResourceAddress(Heap, HeapCount, DescriptorIndex, ElementIndex);
  if (!Addr)
    return 0;
  return __atomic_fetch_sub(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.and.typed.i32` (roadmap H8w): `OpAtomicAnd`'s
// counterpart to `feme.cpu.resource.atomic.add.typed.i32` above.
int32_t femeCpuResourceAtomicAndTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.and.typed.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicAndTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr =
      femeRTAtomicResourceAddress(Heap, HeapCount, DescriptorIndex, ElementIndex);
  if (!Addr)
    return 0;
  return __atomic_fetch_and(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.or.typed.i32` (roadmap H8w): `OpAtomicOr`'s
// counterpart to `feme.cpu.resource.atomic.add.typed.i32` above.
int32_t femeCpuResourceAtomicOrTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.or.typed.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicOrTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr =
      femeRTAtomicResourceAddress(Heap, HeapCount, DescriptorIndex, ElementIndex);
  if (!Addr)
    return 0;
  return __atomic_fetch_or(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.xor.typed.i32` (roadmap H8w): `OpAtomicXor`'s
// counterpart to `feme.cpu.resource.atomic.add.typed.i32` above.
int32_t femeCpuResourceAtomicXorTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.xor.typed.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicXorTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr =
      femeRTAtomicResourceAddress(Heap, HeapCount, DescriptorIndex, ElementIndex);
  if (!Addr)
    return 0;
  return __atomic_fetch_xor(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.smax.typed.i32` (roadmap H8w): `OpAtomicSMax`'s
// counterpart to `feme.cpu.resource.atomic.add.typed.i32` above -- a
// signed maximum, using GCC/Clang's `__atomic_fetch_max` builtin on a
// signed `int32_t` operand (matching how `femeCpuImageAtomicSMax2D`,
// roadmap H8v, gets a signed max from the identical builtin).
int32_t femeCpuResourceAtomicSMaxTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.smax.typed.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicSMaxTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr =
      femeRTAtomicResourceAddress(Heap, HeapCount, DescriptorIndex, ElementIndex);
  if (!Addr)
    return 0;
  return __atomic_fetch_max(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.smin.typed.i32` (roadmap H8w): `OpAtomicSMin`'s
// counterpart to `feme.cpu.resource.atomic.add.typed.i32` above -- a
// signed minimum, using `__atomic_fetch_min` on a signed `int32_t`
// operand.
int32_t femeCpuResourceAtomicSMinTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.smin.typed.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicSMinTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr =
      femeRTAtomicResourceAddress(Heap, HeapCount, DescriptorIndex, ElementIndex);
  if (!Addr)
    return 0;
  return __atomic_fetch_min(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.umax.typed.i32` (roadmap H8w): `OpAtomicUMax`'s
// counterpart to `feme.cpu.resource.atomic.add.typed.i32` above -- an
// unsigned maximum, reinterpreting the same `int32_t` storage as
// `uint32_t` for the comparison (mirroring
// `femeCpuImageAtomicUMax2D`'s own identical reinterpretation, roadmap
// H8v).
int32_t femeCpuResourceAtomicUMaxTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.umax.typed.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicUMaxTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr =
      femeRTAtomicResourceAddress(Heap, HeapCount, DescriptorIndex, ElementIndex);
  if (!Addr)
    return 0;
  uint32_t *UAddr = (uint32_t *)Addr;
  return (int32_t)__atomic_fetch_max(UAddr, (uint32_t)Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.umin.typed.i32` (roadmap H8w): `OpAtomicUMin`'s
// counterpart to `feme.cpu.resource.atomic.add.typed.i32` above -- an
// unsigned minimum, see `femeCpuResourceAtomicUMaxTyped`'s own
// reinterpretation doc.
int32_t femeCpuResourceAtomicUMinTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.umin.typed.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicUMinTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr =
      femeRTAtomicResourceAddress(Heap, HeapCount, DescriptorIndex, ElementIndex);
  if (!Addr)
    return 0;
  uint32_t *UAddr = (uint32_t *)Addr;
  return (int32_t)__atomic_fetch_min(UAddr, (uint32_t)Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.exchange.typed.i32` (roadmap H8w):
// `OpAtomicExchange`'s counterpart to
// `feme.cpu.resource.atomic.add.typed.i32` above -- an unconditional
// swap.
int32_t femeCpuResourceAtomicExchangeTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.exchange.typed.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicExchangeTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr =
      femeRTAtomicResourceAddress(Heap, HeapCount, DescriptorIndex, ElementIndex);
  if (!Addr)
    return 0;
  return __atomic_exchange_n(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.compare_exchange.typed.i32` (roadmap H8w):
// `OpAtomicCompareExchange`'s counterpart to
// `feme.cpu.resource.atomic.add.typed.i32` above -- replaces the element
// at \p ElementIndex with \p Value only if it currently equals
// \p Comparator, always returning the pre-op value either way (matching
// `femeCpuImageAtomicCompareExchange2D`'s own identical semantics,
// roadmap H8v).
int32_t femeCpuResourceAtomicCompareExchangeTyped(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ElementIndex, int32_t Comparator, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.compare_exchange.typed.i32");

__attribute__((always_inline)) int32_t
femeCpuResourceAtomicCompareExchangeTyped(const FemeRTDescriptor *Heap,
                                         uint32_t HeapCount,
                                         uint32_t DescriptorIndex,
                                         uint64_t ElementIndex,
                                         int32_t Comparator, int32_t Value,
                                         _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr =
      femeRTAtomicResourceAddress(Heap, HeapCount, DescriptorIndex, ElementIndex);
  if (!Addr)
    return 0;
  int32_t Expected = Comparator;
  __atomic_compare_exchange_n(Addr, &Expected, Value, /*weak=*/0,
                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  return Expected;
}

// Bounds-checked address helper for `feme.cpu.resource.atomic.*.raw.i32`
// (roadmap H8x): the ordinary-storage-buffer counterpart of
// `femeRTAtomicResourceAddress` above -- same real-hardware-atomic
// rationale, but addressed by descriptor index + *byte offset* like every
// other raw/structured-buffer access (`femeCpuResourceLoadRawI32`) rather
// than an element index into a per-format size table (an ordinary
// storage buffer has no format at all). Both `ResourceKind::Raw == 3` and
// `ResourceKind::Structured == 2` are accepted, mirroring
// `femeCpuResourceLoadRawI32`'s own identical acceptance of either kind.
// Returns `NULL` on any failure (unbound/wrong-kind descriptor, non-UAV,
// or out-of-bounds byte offset), mirroring `femeRTAtomicResourceAddress`'s
// own fail-safe convention.
__attribute__((always_inline)) static int32_t *
femeRTAtomicResourceAddressRaw(const FemeRTDescriptor *Heap,
                               uint32_t HeapCount, uint32_t DescriptorIndex,
                               uint64_t ByteOffset) {
  FemeRTLoaded Desc = femeRTLoadDescriptor(Heap, HeapCount, DescriptorIndex);
  _Bool OkRaw = femeRTCheckAccess(Desc.Kind, /*ResourceKind::Raw=*/3,
                                  Desc.SizeInBytes, Desc.Flags, ByteOffset,
                                  sizeof(int32_t));
  _Bool OkStructured =
      femeRTCheckAccess(Desc.Kind, /*ResourceKind::Structured=*/2,
                        Desc.SizeInBytes, Desc.Flags, ByteOffset,
                        sizeof(int32_t));
  _Bool IsUAV = (Desc.Flags & 1u) != 0; // FEME_DESCRIPTOR_UAV.
  if (!((OkRaw || OkStructured) && IsUAV))
    return (int32_t *)0;
  return (int32_t *)((unsigned char *)Desc.Data + ByteOffset);
}

// `feme.cpu.resource.atomic.add.raw.i32` (roadmap H8x): `OpAtomicIAdd`
// against an ordinary storage buffer/direct-field storage block -- adds
// \p Value to the word at \p ByteOffset through descriptor
// \p DescriptorIndex, returning the pre-op value. See
// `femeCpuResourceAtomicAddTyped`'s own doc (roadmap H8w) for the shared
// real-hardware-atomic rationale; an unbound resource or an
// out-of-bounds/masked-off access returns 0, mirroring every other
// raw-buffer access's own "reads/no-ops as zero" rule.
int32_t femeCpuResourceAtomicAddRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.add.raw.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicAddRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr = femeRTAtomicResourceAddressRaw(Heap, HeapCount,
                                                 DescriptorIndex, ByteOffset);
  if (!Addr)
    return 0;
  return __atomic_fetch_add(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.sub.raw.i32` (roadmap H8x): `OpAtomicISub`'s
// counterpart to `feme.cpu.resource.atomic.add.raw.i32` above.
int32_t femeCpuResourceAtomicSubRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.sub.raw.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicSubRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr = femeRTAtomicResourceAddressRaw(Heap, HeapCount,
                                                 DescriptorIndex, ByteOffset);
  if (!Addr)
    return 0;
  return __atomic_fetch_sub(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.and.raw.i32` (roadmap H8x): `OpAtomicAnd`'s
// counterpart to `feme.cpu.resource.atomic.add.raw.i32` above.
int32_t femeCpuResourceAtomicAndRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.and.raw.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicAndRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr = femeRTAtomicResourceAddressRaw(Heap, HeapCount,
                                                 DescriptorIndex, ByteOffset);
  if (!Addr)
    return 0;
  return __atomic_fetch_and(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.or.raw.i32` (roadmap H8x): `OpAtomicOr`'s
// counterpart to `feme.cpu.resource.atomic.add.raw.i32` above.
int32_t femeCpuResourceAtomicOrRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.or.raw.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicOrRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr = femeRTAtomicResourceAddressRaw(Heap, HeapCount,
                                                 DescriptorIndex, ByteOffset);
  if (!Addr)
    return 0;
  return __atomic_fetch_or(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.xor.raw.i32` (roadmap H8x): `OpAtomicXor`'s
// counterpart to `feme.cpu.resource.atomic.add.raw.i32` above.
int32_t femeCpuResourceAtomicXorRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.xor.raw.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicXorRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr = femeRTAtomicResourceAddressRaw(Heap, HeapCount,
                                                 DescriptorIndex, ByteOffset);
  if (!Addr)
    return 0;
  return __atomic_fetch_xor(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.smax.raw.i32` (roadmap H8x): `OpAtomicSMax`'s
// counterpart to `feme.cpu.resource.atomic.add.raw.i32` above -- a signed
// maximum, using GCC/Clang's `__atomic_fetch_max` builtin on a signed
// `int32_t` operand (matching how `femeCpuResourceAtomicSMaxTyped`,
// roadmap H8w, gets a signed max from the identical builtin).
int32_t femeCpuResourceAtomicSMaxRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.smax.raw.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicSMaxRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr = femeRTAtomicResourceAddressRaw(Heap, HeapCount,
                                                 DescriptorIndex, ByteOffset);
  if (!Addr)
    return 0;
  return __atomic_fetch_max(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.smin.raw.i32` (roadmap H8x): `OpAtomicSMin`'s
// counterpart to `feme.cpu.resource.atomic.add.raw.i32` above -- a signed
// minimum, using `__atomic_fetch_min` on a signed `int32_t` operand.
int32_t femeCpuResourceAtomicSMinRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.smin.raw.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicSMinRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr = femeRTAtomicResourceAddressRaw(Heap, HeapCount,
                                                 DescriptorIndex, ByteOffset);
  if (!Addr)
    return 0;
  return __atomic_fetch_min(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.umax.raw.i32` (roadmap H8x): `OpAtomicUMax`'s
// counterpart to `feme.cpu.resource.atomic.add.raw.i32` above -- an
// unsigned maximum, reinterpreting the same `int32_t` storage as
// `uint32_t` for the comparison (mirroring
// `femeCpuResourceAtomicUMaxTyped`'s own identical reinterpretation,
// roadmap H8w).
int32_t femeCpuResourceAtomicUMaxRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.umax.raw.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicUMaxRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr = femeRTAtomicResourceAddressRaw(Heap, HeapCount,
                                                 DescriptorIndex, ByteOffset);
  if (!Addr)
    return 0;
  uint32_t *UAddr = (uint32_t *)Addr;
  return (int32_t)__atomic_fetch_max(UAddr, (uint32_t)Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.umin.raw.i32` (roadmap H8x): `OpAtomicUMin`'s
// counterpart to `feme.cpu.resource.atomic.add.raw.i32` above -- an
// unsigned minimum, see `femeCpuResourceAtomicUMaxRaw`'s own
// reinterpretation doc.
int32_t femeCpuResourceAtomicUMinRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.umin.raw.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicUMinRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr = femeRTAtomicResourceAddressRaw(Heap, HeapCount,
                                                 DescriptorIndex, ByteOffset);
  if (!Addr)
    return 0;
  uint32_t *UAddr = (uint32_t *)Addr;
  return (int32_t)__atomic_fetch_min(UAddr, (uint32_t)Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.exchange.raw.i32` (roadmap H8x):
// `OpAtomicExchange`'s counterpart to `feme.cpu.resource.atomic.add.raw.i32`
// above -- an unconditional swap.
int32_t femeCpuResourceAtomicExchangeRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.exchange.raw.i32");

__attribute__((always_inline)) int32_t femeCpuResourceAtomicExchangeRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr = femeRTAtomicResourceAddressRaw(Heap, HeapCount,
                                                 DescriptorIndex, ByteOffset);
  if (!Addr)
    return 0;
  return __atomic_exchange_n(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.resource.atomic.compare_exchange.raw.i32` (roadmap H8x):
// `OpAtomicCompareExchange`'s counterpart to
// `feme.cpu.resource.atomic.add.raw.i32` above -- replaces the word at
// \p ByteOffset with \p Value only if it currently equals \p Comparator,
// always returning the pre-op value either way (matching
// `femeCpuResourceAtomicCompareExchangeTyped`'s own identical semantics,
// roadmap H8w).
int32_t femeCpuResourceAtomicCompareExchangeRaw(
    const FemeRTDescriptor *Heap, uint32_t HeapCount, uint32_t DescriptorIndex,
    uint64_t ByteOffset, int32_t Comparator, int32_t Value,
    _Bool Mask) asm("feme.cpu.resource.atomic.compare_exchange.raw.i32");

__attribute__((always_inline)) int32_t
femeCpuResourceAtomicCompareExchangeRaw(const FemeRTDescriptor *Heap,
                                        uint32_t HeapCount,
                                        uint32_t DescriptorIndex,
                                        uint64_t ByteOffset,
                                        int32_t Comparator, int32_t Value,
                                        _Bool Mask) {
  if (!Mask)
    return 0;
  int32_t *Addr = femeRTAtomicResourceAddressRaw(Heap, HeapCount,
                                                 DescriptorIndex, ByteOffset);
  if (!Addr)
    return 0;
  int32_t Expected = Comparator;
  __atomic_compare_exchange_n(Addr, &Expected, Value, /*weak=*/0,
                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  return Expected;
}


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

// Applies a packed `FemeRTImageDescriptor::Swizzle` word (see its own
// comment) to `Color`, returning one output channel per byte of `Swizzle`
// in R/G/B/A order. `Identity` (0) reads the same-named input channel
// (e.g. output G reads input G), `Zero`/`One` (1/2) are constants, and
// `R`/`G`/`B`/`A` (3..6) read a specific input channel regardless of
// output position.
//
// Roadmap L125(d): used to apply an image view's own component swizzle to
// a synthesized border color (`SamplerAddressMode::ClampToBorder`
// resolving out of range) the same way core Vulkan requires it be applied
// to `VK_BORDER_COLOR_*_TRANSPARENT_BLACK`/`*_OPAQUE_WHITE` (a "well-known"
// border color, always well-defined regardless of
// `VK_EXT_border_color_swizzle` support -- unlike `*_OPAQUE_BLACK`/custom
// border colors, which need that extension for a non-identity swizzle,
// per `vktPipelineSamplerBorderSwizzleTests.cpp`'s own gating). Roadmap
// L125(f) widened this to also cover an ordinary in-bounds texel fetch
// (`femeRTFetchTexel2D`/`femeRTFetchTexel3D`'s own `ApplySwizzle`
// parameter) for the float-sampled path. The integer-sampled (`*I32`)
// path has its own counterpart, `femeRTApplyImageSwizzleI32` below
// (roadmap L125(h)) -- kept as a separate function rather than a shared
// one since `FemeRTv4f32`/`FemeRTv4i32` are distinct vector types with
// different "Zero"/"One" fill values (`0.0f`/`1.0f` vs. `0`/`1`).
__attribute__((always_inline)) static FemeRTv4f32
femeRTApplyImageSwizzle(FemeRTv4f32 Color, uint32_t Swizzle) {
  FemeRTv4f32 Result = {0.0f, 0.0f, 0.0f, 0.0f};
  for (int I = 0; I != 4; ++I) {
    uint32_t Channel = (Swizzle >> (I * 8)) & 0xffu;
    switch (Channel) {
    case 0: // Identity: this output channel reads its own same-named
            // input channel.
      Result[I] = Color[I];
      break;
    case 1: // Zero
      Result[I] = 0.0f;
      break;
    case 2: // One
      Result[I] = 1.0f;
      break;
    default: // R, G, B, A (3..6).
      Result[I] = Color[Channel - 3 < 4 ? Channel - 3 : 0];
      break;
    }
  }
  return Result;
}

// The integer counterpart of `femeRTApplyImageSwizzle` above, for
// `feme.cpu.image.sample.*.v4i32` (roadmap L125(h)). Identical channel
// semantics, just over `FemeRTv4i32` with integer `0`/`1` fill values
// instead of `0.0f`/`1.0f`.
__attribute__((always_inline)) static FemeRTv4i32
femeRTApplyImageSwizzleI32(FemeRTv4i32 Color, uint32_t Swizzle) {
  FemeRTv4i32 Result = {0, 0, 0, 0};
  for (int I = 0; I != 4; ++I) {
    uint32_t Channel = (Swizzle >> (I * 8)) & 0xffu;
    switch (Channel) {
    case 0: // Identity: this output channel reads its own same-named
            // input channel.
      Result[I] = Color[I];
      break;
    case 1: // Zero
      Result[I] = 0;
      break;
    case 2: // One
      Result[I] = 1;
      break;
    default: // R, G, B, A (3..6).
      Result[I] = Color[Channel - 3 < 4 ? Channel - 3 : 0];
      break;
    }
  }
  return Result;
}

// Returns a 4-bit mask (bit 0 = R, bit 1 = G, bit 2 = B, bit 3 = A) of
// which components of `Format` are actually backed by real texel data, as
// opposed to a fixed fill value `femeRTUnpackImageTexel` supplies for a
// component the format doesn't store (`0.0` for a missing R/G/B, `1.0`
// for a missing A -- mirror the exact per-case fill values that function
// already returns, one entry per case in its own switch). `A8_UNORM`
// (case 27) is the one format whose single real channel is A, not R --
// every other partial format's real channels start from R and extend
// rightward, matching `femeRTUnpackImageTexel`'s own per-case literal
// list. An unrecognized format conservatively returns "all four present"
// (`0xf`), the same as this function not existing, so it never narrows an
// already-correct border color for a format this switch doesn't know.
//
// Roadmap L125(e): used to re-expand a `Sampler`'s format-independent
// baked `BorderColor` per the *sampled image's own* format before
// `femeRTApplyImageSwizzle` runs, mirroring core Vulkan's border-color
// "conversion to RGBA" rule (a component the image format doesn't store
// is not read from the border-color value at all, and instead gets the
// same fixed fill an in-bounds texel of that format would get) --
// `VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK`'s nominal `(0,0,0,0)`
// therefore actually samples as `(0,0,0,1)` through an alpha-less format
// like `R32G32B32_FLOAT`, exactly like a real in-bounds texel of that
// format would.
__attribute__((always_inline)) static uint32_t
femeRTImageFormatComponentMask(uint32_t Format) {
  switch (Format) {
  case 1:  // R32_FLOAT
  case 31: // D16_UNORM
  case 32: // D32_FLOAT
  case 35: // S8_UINT
  case 85: // R8_UNORM
  case 86: // R8_SNORM
  case 93: // R16_FLOAT
  case 94: // R16_UNORM
  case 95: // R16_SNORM
    return 0x1u; // R only.
  case 2:  // R32G32_FLOAT
  case 89: // R8G8_UNORM
  case 90: // R8G8_SNORM
  case 98: // R16G16_FLOAT
  case 99: // R16G16_UNORM
  case 100: // R16G16_SNORM
    return 0x3u; // R, G.
  case 3:   // R32G32B32_FLOAT
  case 23:  // R11G11B10_FLOAT
  case 80:  // R5G6B5_UNORM
  case 81:  // B5G6R5_UNORM
  case 131: // E5B9G9R9_UFLOAT
    return 0x7u; // R, G, B.
  case 27: // A8_UNORM
    return 0x8u; // A only.
  case 4:   // R32G32B32A32_FLOAT
  case 13:  // R8G8B8A8_UNORM
  case 14:  // R8G8B8A8_SNORM
  case 17:  // R8G8B8A8_UNORM_SRGB
  case 18:  // R16G16B16A16_FLOAT
  case 19:  // R16G16B16A16_UNORM
  case 20:  // R16G16B16A16_SNORM
  case 24:  // R10G10B10A2_UNORM
  case 26:  // B8G8R8A8_UNORM
  case 28:  // A1B5G5R5_UNORM
  case 79:  // B4G4R4A4_UNORM
  case 84:  // A1R5G5B5_UNORM
  case 103: // R10G10B10A2_SNORM
  case 132: // B8G8R8A8_UNORM_SRGB
    return 0xfu; // R, G, B, A.
  default:
    return 0xfu;
  }
}

// Re-expands `BorderColor` per `femeRTImageFormatComponentMask(Format)`:
// a component the format doesn't store is replaced with the same fixed
// fill `femeRTUnpackImageTexel` would supply for it (`0.0` for R/G/B,
// `1.0` for A), rather than trusting `Sampler`'s own format-independent
// baked value for that component -- see `femeRTImageFormatComponentMask`'s
// own roadmap L125(e) comment above for why this is needed at fetch time,
// not sampler-creation time.
__attribute__((always_inline)) static FemeRTv4f32
femeRTExpandBorderColorForFormat(const float BorderColor[4], uint32_t Format) {
  uint32_t Mask = femeRTImageFormatComponentMask(Format);
  FemeRTv4f32 Result;
  for (int I = 0; I != 4; ++I)
    Result[I] = (Mask & (1u << I)) ? BorderColor[I] : (I == 3 ? 1.0f : 0.0f);
  return Result;
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
//
// Roadmap L125(f): `ApplySwizzle` distinguishes the two Vulkan semantics
// this one shared helper serves -- a sampled-image fetch (`OpImageSample*`/
// `OpImageFetch`, reached only from the `femeRT{Sample,SampleCmp}*`
// family) has its image view's own `VkComponentMapping` applied to the
// texel per spec, while a storage-image load (`OpImageRead`, reached only
// from `feme.cpu.image.load.*`'s own `femeCpuImageLoad*` wrappers) must
// not be swizzled at all. Every `Sample*`/`SampleCmp*` caller passes `1`;
// every `Load*`-family caller passes `0`. The border branch above already
// only ever executes for a `Sample*`-family caller (`UseBorder` requires a
// real `VkSampler`'s addressing mode, never present for a `Load*` access),
// so gating it by the same flag is safe and never observably changes its
// already-CTS-verified L125(d)/(e) behavior.
__attribute__((always_inline)) static FemeRTv4f32
femeRTFetchTexel2D(const FemeRTImageDescriptor *Img, uint32_t Level,
                   uint32_t Layer, int32_t X, int32_t Y, uint32_t Sample,
                   _Bool UseBorder, const float BorderColor[4],
                   _Bool ApplySwizzle) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (UseBorder) {
    FemeRTv4f32 Border =
        femeRTExpandBorderColorForFormat(BorderColor, Img->Format);
    return ApplySwizzle ? femeRTApplyImageSwizzle(Border, Img->Swizzle)
                        : Border;
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
  FemeRTv4f32 Texel = femeRTUnpackImageTexel(Img->Format, Ptr);
  return ApplySwizzle ? femeRTApplyImageSwizzle(Texel, Img->Swizzle) : Texel;
}

// The integer counterpart of `femeRTFetchTexel2D` above, for
// `feme.cpu.image.load.2d.v4i32` (roadmap E26). Takes no border color: an
// integer-channel image is only ever reached through `Load2D`/
// `OpImageFetch` (see ImageCalls.h's `Load2DI32` comment), which -- like
// its float counterpart -- addresses no sampler and therefore no address
// mode, so there is no `ClampToBorder` case to honor here either. Shares
// `femeRTFetchTexel2D`'s roadmap F8b multisample-stride fix and roadmap
// H7b-a array-layer widening (see that function's own comments). Takes a
// `Sample` operand (roadmap H19g), like `femeRTFetchTexel2D`'s own --
// `Sample` is `0` for every caller before this row (a single-sample image,
// or a caller with no per-sample index of its own). Roadmap L125(h) added
// the `ApplySwizzle` parameter, mirroring `femeRTFetchTexel2D`'s own
// L125(f) parameter of the same name and for the same reason: this helper
// is shared between `femeCpuImageSample*V4I32` (an `OpImageSample*`/
// `OpImageFetch`-lowered call, which must apply the image view's own
// `VkComponentMapping`) and `femeCpuImageLoad*V4I32` (an `OpImageRead`
// storage-image load, which must not) -- see `femeRTFetchTexel2D`'s own
// comment for the full rationale, identical here.
__attribute__((always_inline)) static FemeRTv4i32
femeRTFetchTexel2DI32(const FemeRTImageDescriptor *Img, uint32_t Level,
                      uint32_t Layer, int32_t X, int32_t Y, uint32_t Sample,
                      _Bool ApplySwizzle) {
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
  uint64_t SampleOffset = (uint64_t)Sample * Layout->SampleStride;
  uint64_t Offset = Layout->Offset + (uint64_t)Layer * Layout->SlicePitch +
                    (uint64_t)Y * Layout->RowPitch +
                    (uint64_t)X * TexelStride + SampleOffset;
  if (Offset + ElemSize > Img->SizeInBytes)
    return Zero;
  const unsigned char *Ptr = (const unsigned char *)Img->Data + Offset;
  FemeRTv4i32 Texel = femeRTUnpackImageTexelI32(Img->Format, Ptr);
  return ApplySwizzle ? femeRTApplyImageSwizzleI32(Texel, Img->Swizzle)
                      : Texel;
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

// Bounds-checked address helper for `feme.cpu.image.atomic.*.2d.i32`
// (roadmap H8v): shares `femeRTStoreTexel2DI32`'s own layer-0/mip-0/
// sample-0 addressing, but returns a raw `int32_t *` into the image's own
// backing store rather than packing a texel value through it, since a real
// atomic (`__atomic_fetch_add`/etc.) needs a genuine pointer to operate on
// directly -- unlike an ordinary store, which only ever needs to write
// through the address once. Additionally rejects any format whose element
// size isn't exactly 4 bytes: SPIR-V (and this project's own
// `hasOnlySupportedStorageImageUses`, `SPIRVResourceLowering.cpp`) only
// ever allows an image atomic against a single-32-bit-scalar storage
// image format (`R32_SINT`/`R32_UINT`), so every other format is
// unreachable here in practice, but the check is kept as defense in depth
// (mirroring every other `femeRT*` helper's own habit of failing safe on
// an unexpected shape rather than assuming the caller validated it).
// Returns `NULL` on any failure, exactly like every other `femeRT*` helper
// that can fail (see `femeRTFetchTexel2DI32`'s own `NULL`-descriptor
// check).
__attribute__((always_inline)) static int32_t *
femeRTAtomicTexelAddress2D(const FemeRTImageDescriptor *Img, int32_t X,
                          int32_t Y) {
  if (!Img->Data || Img->MipLayoutCount == 0 || Img->ArrayLayers == 0)
    return (int32_t *)0;
  if (X < 0 || Y < 0 || (uint32_t)X >= Img->Width || (uint32_t)Y >= Img->Height)
    return (int32_t *)0;
  uint64_t ElemSize = femeRTImageFormatElementSize(Img->Format);
  if (ElemSize != sizeof(int32_t))
    return (int32_t *)0;
  const FemeRTImageSubresourceLayout *Layout = &Img->MipLayouts[0];
  uint64_t TexelStride = Layout->SampleStride != 0
                             ? (uint64_t)Img->SampleCount * Layout->SampleStride
                             : ElemSize;
  uint64_t Offset =
      Layout->Offset + (uint64_t)Y * Layout->RowPitch + (uint64_t)X * TexelStride;
  if (Offset + ElemSize > Img->SizeInBytes)
    return (int32_t *)0;
  return (int32_t *)((unsigned char *)Img->Data + Offset);
}

// The arrayed counterpart of `femeRTStoreTexel2D` above, for
// `feme.cpu.image.store.2darray.v4f32` (roadmap H19b): writes \p Texel to
// the texel at integer coordinates `(X, Y)`, array layer \p Layer, mip
// level 0, sample 0 of \p Img, addressing the layer via
// `Layer * Layout->SlicePitch` -- the same per-layer addressing
// `femeRTFetchTexel2D`'s own roadmap H7b-a widening already uses on the
// read side. `Layer >= Img->ArrayLayers` is silently dropped, mirroring
// `femeRTStoreTexel2D`'s own out-of-bounds-X/Y handling.
__attribute__((always_inline)) static void
femeRTStoreTexel2DArray(const FemeRTImageDescriptor *Img, int32_t X, int32_t Y,
                        uint32_t Layer, FemeRTv4f32 Texel) {
  if (!Img->Data || Img->MipLayoutCount == 0 || Layer >= Img->ArrayLayers)
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
  uint64_t Offset = Layout->Offset + (uint64_t)Layer * Layout->SlicePitch +
                    (uint64_t)Y * Layout->RowPitch + (uint64_t)X * TexelStride;
  if (Offset + ElemSize > Img->SizeInBytes)
    return;
  unsigned char *Ptr = (unsigned char *)Img->Data + Offset;
  femeRTPackImageTexel(Img->Format, Ptr, Texel);
}

// The integer counterpart of `femeRTStoreTexel2DArray` above, for
// `feme.cpu.image.store.2darray.v4i32` (roadmap H19b).
__attribute__((always_inline)) static void
femeRTStoreTexel2DArrayI32(const FemeRTImageDescriptor *Img, int32_t X,
                          int32_t Y, uint32_t Layer, FemeRTv4i32 Texel) {
  if (!Img->Data || Img->MipLayoutCount == 0 || Layer >= Img->ArrayLayers)
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
  uint64_t Offset = Layout->Offset + (uint64_t)Layer * Layout->SlicePitch +
                    (uint64_t)Y * Layout->RowPitch + (uint64_t)X * TexelStride;
  if (Offset + ElemSize > Img->SizeInBytes)
    return;
  unsigned char *Ptr = (unsigned char *)Img->Data + Offset;
  femeRTPackImageTexelI32(Img->Format, Ptr, Texel);
}

// The multisampled counterpart of `femeRTStoreTexel2D` above, for
// `feme.cpu.image.store.2dms.v4f32` (roadmap H19g): writes \p Texel to the
// texel at integer coordinates `(X, Y)`, sample \p Sample, mip level 0 of
// \p Img -- a plain (non-arrayed) multisampled 2D storage image. Addresses
// the sample via `Sample * Layout->SampleStride`, the same per-sample
// addressing `femeRTFetchTexel2D`'s own roadmap F8c widening already uses
// on the read side (see that function's own comment for why
// `Layout->SampleStride` is populated for every multisampled image by
// `computeSubresourceLayouts`, `Image.cpp`). `Sample >= Img->SampleCount`
// is silently dropped, mirroring `femeRTStoreTexel2D`'s own
// out-of-bounds-X/Y handling.
__attribute__((always_inline)) static void
femeRTStoreTexel2DMS(const FemeRTImageDescriptor *Img, int32_t X, int32_t Y,
                    uint32_t Sample, FemeRTv4f32 Texel) {
  if (!Img->Data || Img->MipLayoutCount == 0 || Img->ArrayLayers == 0 ||
      Sample >= Img->SampleCount)
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
  uint64_t SampleOffset = (uint64_t)Sample * Layout->SampleStride;
  uint64_t Offset = Layout->Offset + (uint64_t)Y * Layout->RowPitch +
                    (uint64_t)X * TexelStride + SampleOffset;
  if (Offset + ElemSize > Img->SizeInBytes)
    return;
  unsigned char *Ptr = (unsigned char *)Img->Data + Offset;
  femeRTPackImageTexel(Img->Format, Ptr, Texel);
}

// The integer counterpart of `femeRTStoreTexel2DMS` above, for
// `feme.cpu.image.store.2dms.v4i32` (roadmap H19g).
__attribute__((always_inline)) static void
femeRTStoreTexel2DMSI32(const FemeRTImageDescriptor *Img, int32_t X, int32_t Y,
                       uint32_t Sample, FemeRTv4i32 Texel) {
  if (!Img->Data || Img->MipLayoutCount == 0 || Img->ArrayLayers == 0 ||
      Sample >= Img->SampleCount)
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
  uint64_t SampleOffset = (uint64_t)Sample * Layout->SampleStride;
  uint64_t Offset = Layout->Offset + (uint64_t)Y * Layout->RowPitch +
                    (uint64_t)X * TexelStride + SampleOffset;
  if (Offset + ElemSize > Img->SizeInBytes)
    return;
  unsigned char *Ptr = (unsigned char *)Img->Data + Offset;
  femeRTPackImageTexelI32(Img->Format, Ptr, Texel);
}

// The arrayed-*and*-multisampled counterpart of `femeRTStoreTexel2D`
// above, for `feme.cpu.image.store.2darrayms.v4f32` (roadmap H19m): writes
// \p Texel to the texel at integer coordinates `(X, Y)`, array layer
// \p Layer, sample \p Sample, mip level 0 of \p Img -- combining
// `femeRTStoreTexel2DArray`'s own per-layer addressing and
// `femeRTStoreTexel2DMS`'s own per-sample addressing in one offset, the
// same combined formula `femeRTFetchTexel2D`'s own independent `Layer`/
// `Sample` parameters already compute on the read side (see that
// function's own comment). `Layer >= Img->ArrayLayers` or
// `Sample >= Img->SampleCount` is silently dropped, mirroring
// `femeRTStoreTexel2DArray`/`femeRTStoreTexel2DMS`'s own out-of-bounds
// handling.
__attribute__((always_inline)) static void
femeRTStoreTexel2DArrayMS(const FemeRTImageDescriptor *Img, int32_t X,
                         int32_t Y, uint32_t Layer, uint32_t Sample,
                         FemeRTv4f32 Texel) {
  if (!Img->Data || Img->MipLayoutCount == 0 || Layer >= Img->ArrayLayers ||
      Sample >= Img->SampleCount)
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
  uint64_t SampleOffset = (uint64_t)Sample * Layout->SampleStride;
  uint64_t Offset = Layout->Offset + (uint64_t)Layer * Layout->SlicePitch +
                    (uint64_t)Y * Layout->RowPitch +
                    (uint64_t)X * TexelStride + SampleOffset;
  if (Offset + ElemSize > Img->SizeInBytes)
    return;
  unsigned char *Ptr = (unsigned char *)Img->Data + Offset;
  femeRTPackImageTexel(Img->Format, Ptr, Texel);
}

// The integer counterpart of `femeRTStoreTexel2DArrayMS` above, for
// `feme.cpu.image.store.2darrayms.v4i32` (roadmap H19m).
__attribute__((always_inline)) static void
femeRTStoreTexel2DArrayMSI32(const FemeRTImageDescriptor *Img, int32_t X,
                            int32_t Y, uint32_t Layer, uint32_t Sample,
                            FemeRTv4i32 Texel) {
  if (!Img->Data || Img->MipLayoutCount == 0 || Layer >= Img->ArrayLayers ||
      Sample >= Img->SampleCount)
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
  uint64_t SampleOffset = (uint64_t)Sample * Layout->SampleStride;
  uint64_t Offset = Layout->Offset + (uint64_t)Layer * Layout->SlicePitch +
                    (uint64_t)Y * Layout->RowPitch +
                    (uint64_t)X * TexelStride + SampleOffset;
  if (Offset + ElemSize > Img->SizeInBytes)
    return;
  unsigned char *Ptr = (unsigned char *)Img->Data + Offset;
  femeRTPackImageTexelI32(Img->Format, Ptr, Texel);
}

// The plain-1D counterpart of `femeRTFetchTexel2D` above, for
// `feme.cpu.image.load.1d.v4f32` (roadmap H19c). A 1D image's own
// `computeSubresourceLayouts` (Image.cpp) always produces `Height == 1`,
// so `RowPitch`/`SlicePitch` already collapse such that passing `Y == 0`
// straight through to the existing 2D fetch produces the correct byte
// offset -- no new addressing math is needed, just a thin wrapper with a
// narrower (X-only) coordinate.
__attribute__((always_inline)) static FemeRTv4f32
femeRTFetchTexel1D(const FemeRTImageDescriptor *Img, uint32_t Level,
                   int32_t X, uint32_t Sample, _Bool UseBorder,
                   const float BorderColor[4], _Bool ApplySwizzle) {
  return femeRTFetchTexel2D(Img, Level, /*Layer=*/0, X, /*Y=*/0, Sample,
                            UseBorder, BorderColor, ApplySwizzle);
}

// The integer counterpart of `femeRTFetchTexel1D` above, for
// `feme.cpu.image.load.1d.v4i32` (roadmap H19c). Roadmap L125(h) added
// the `ApplySwizzle` parameter, forwarded straight through to
// `femeRTFetchTexel2DI32` -- see that function's own comment.
__attribute__((always_inline)) static FemeRTv4i32
femeRTFetchTexel1DI32(const FemeRTImageDescriptor *Img, uint32_t Level,
                      int32_t X, _Bool ApplySwizzle) {
  return femeRTFetchTexel2DI32(Img, Level, /*Layer=*/0, X, /*Y=*/0,
                               /*Sample=*/0, ApplySwizzle);
}

// The plain-1D counterpart of `femeRTStoreTexel2D` above, for
// `feme.cpu.image.store.1d.v4f32` (roadmap H19c) -- see
// `femeRTFetchTexel1D`'s own comment for why `Y == 0` is a correct thin
// wrapper rather than a new addressing formula.
__attribute__((always_inline)) static void
femeRTStoreTexel1D(const FemeRTImageDescriptor *Img, int32_t X,
                   FemeRTv4f32 Texel) {
  femeRTStoreTexel2D(Img, X, /*Y=*/0, Texel);
}

// The integer counterpart of `femeRTStoreTexel1D` above, for
// `feme.cpu.image.store.1d.v4i32` (roadmap H19c).
__attribute__((always_inline)) static void
femeRTStoreTexel1DI32(const FemeRTImageDescriptor *Img, int32_t X,
                      FemeRTv4i32 Texel) {
  femeRTStoreTexel2DI32(Img, X, /*Y=*/0, Texel);
}

// The arrayed-1D counterpart of `femeRTFetchTexel1D` above, for
// `feme.cpu.image.load.1darray.v4f32` (roadmap H19e). Unlike `Plain1D`,
// this cannot wrap the non-arrayed `femeRTFetchTexel2D` call with
// `Layer == 0` -- it instead passes `Layer` straight through to
// `femeRTFetchTexel2D`'s own array-layer parameter (already present on
// every 2D fetch, plain or arrayed, since a plain 2D image is simply an
// arrayed one with `ArrayLayers == 1`), with `Y == 0` for the same reason
// `femeRTFetchTexel1D` passes it: a 1D(-array) image's own
// `computeSubresourceLayouts` (Image.cpp) always produces `Height == 1`.
__attribute__((always_inline)) static FemeRTv4f32
femeRTFetchTexel1DArray(const FemeRTImageDescriptor *Img, uint32_t Level,
                        int32_t X, uint32_t Layer, uint32_t Sample,
                        _Bool UseBorder, const float BorderColor[4],
                        _Bool ApplySwizzle) {
  return femeRTFetchTexel2D(Img, Level, Layer, X, /*Y=*/0, Sample, UseBorder,
                            BorderColor, ApplySwizzle);
}

// The integer counterpart of `femeRTFetchTexel1DArray` above, for
// `feme.cpu.image.load.1darray.v4i32` (roadmap H19e). Roadmap L125(h)
// added the `ApplySwizzle` parameter, forwarded straight through to
// `femeRTFetchTexel2DI32` -- see that function's own comment.
__attribute__((always_inline)) static FemeRTv4i32
femeRTFetchTexel1DArrayI32(const FemeRTImageDescriptor *Img, uint32_t Level,
                          int32_t X, uint32_t Layer, _Bool ApplySwizzle) {
  return femeRTFetchTexel2DI32(Img, Level, Layer, X, /*Y=*/0, /*Sample=*/0,
                               ApplySwizzle);
}

// The arrayed-1D counterpart of `femeRTStoreTexel1D` above, for
// `feme.cpu.image.store.1darray.v4f32` (roadmap H19e) -- a thin wrapper
// over the existing `femeRTStoreTexel2DArray` with `Y == 0`, mirroring
// `femeRTFetchTexel1DArray`'s own reuse of the 2D fetch's array-layer
// parameter.
__attribute__((always_inline)) static void
femeRTStoreTexel1DArray(const FemeRTImageDescriptor *Img, int32_t X,
                        uint32_t Layer, FemeRTv4f32 Texel) {
  femeRTStoreTexel2DArray(Img, X, /*Y=*/0, Layer, Texel);
}

// The integer counterpart of `femeRTStoreTexel1DArray` above, for
// `feme.cpu.image.store.1darray.v4i32` (roadmap H19e).
__attribute__((always_inline)) static void
femeRTStoreTexel1DArrayI32(const FemeRTImageDescriptor *Img, int32_t X,
                          uint32_t Layer, FemeRTv4i32 Texel) {
  femeRTStoreTexel2DArrayI32(Img, X, /*Y=*/0, Layer, Texel);
}

// The plain-3D counterpart of `femeRTFetchTexel2D` above, for
// `feme.cpu.image.load.3d.v4f32` (roadmap H19c). The byte-offset formula
// is identical to `femeRTFetchTexel2D`'s own array-layer addressing
// (`Z * Layout->SlicePitch`), but the bounds check is not: a real 3D
// image's own `ArrayLayers` is always `1` per the Vulkan spec
// (`VkImageCreateInfo.arrayLayers` must be 1 for `VK_IMAGE_TYPE_3D`), so
// the real per-mip depth extent to check `Z` against is instead
// `max(1, Img->Depth >> Level)` -- mirroring `Image.cpp`'s own
// `computeSubresourceLayouts`' identical `LevelDepth` calculation -- which
// is why this cannot be a thin wrapper around `femeRTFetchTexel2D` the way
// `femeRTFetchTexel1D` above is.
__attribute__((always_inline)) static FemeRTv4f32
femeRTFetchTexel3D(const FemeRTImageDescriptor *Img, uint32_t Level,
                   int32_t X, int32_t Y, int32_t Z, _Bool UseBorder,
                   const float BorderColor[4], _Bool ApplySwizzle) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (UseBorder) {
    FemeRTv4f32 Border =
        femeRTExpandBorderColorForFormat(BorderColor, Img->Format);
    return ApplySwizzle ? femeRTApplyImageSwizzle(Border, Img->Swizzle)
                        : Border;
  }
  if (!Img->Data || Level >= Img->MipLayoutCount || Z < 0)
    return Zero;
  uint32_t LevelDepth = Img->Depth >> Level;
  if (LevelDepth == 0)
    LevelDepth = 1;
  if ((uint32_t)Z >= LevelDepth)
    return Zero;
  uint64_t ElemSize = femeRTImageFormatElementSize(Img->Format);
  if (ElemSize == 0)
    return Zero;
  const FemeRTImageSubresourceLayout *Layout = &Img->MipLayouts[Level];
  uint64_t Offset = Layout->Offset + (uint64_t)Z * Layout->SlicePitch +
                    (uint64_t)Y * Layout->RowPitch + (uint64_t)X * ElemSize;
  if (Offset + ElemSize > Img->SizeInBytes)
    return Zero;
  const unsigned char *Ptr = (const unsigned char *)Img->Data + Offset;
  FemeRTv4f32 Texel = femeRTUnpackImageTexel(Img->Format, Ptr);
  return ApplySwizzle ? femeRTApplyImageSwizzle(Texel, Img->Swizzle) : Texel;
}

// The integer counterpart of `femeRTFetchTexel3D` above, for
// `feme.cpu.image.load.3d.v4i32` (roadmap H19c). Roadmap L125(h) added
// the `ApplySwizzle` parameter -- see `femeRTFetchTexel2DI32`'s own
// comment for the shared-helper rationale.
__attribute__((always_inline)) static FemeRTv4i32
femeRTFetchTexel3DI32(const FemeRTImageDescriptor *Img, uint32_t Level,
                      int32_t X, int32_t Y, int32_t Z, _Bool ApplySwizzle) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Img->Data || Level >= Img->MipLayoutCount || Z < 0)
    return Zero;
  uint32_t LevelDepth = Img->Depth >> Level;
  if (LevelDepth == 0)
    LevelDepth = 1;
  if ((uint32_t)Z >= LevelDepth)
    return Zero;
  uint64_t ElemSize = femeRTImageFormatElementSize(Img->Format);
  if (ElemSize == 0)
    return Zero;
  const FemeRTImageSubresourceLayout *Layout = &Img->MipLayouts[Level];
  uint64_t Offset = Layout->Offset + (uint64_t)Z * Layout->SlicePitch +
                    (uint64_t)Y * Layout->RowPitch + (uint64_t)X * ElemSize;
  if (Offset + ElemSize > Img->SizeInBytes)
    return Zero;
  const unsigned char *Ptr = (const unsigned char *)Img->Data + Offset;
  FemeRTv4i32 Texel = femeRTUnpackImageTexelI32(Img->Format, Ptr);
  return ApplySwizzle ? femeRTApplyImageSwizzleI32(Texel, Img->Swizzle)
                      : Texel;
}

// The plain-3D counterpart of `femeRTStoreTexel2D` above, for
// `feme.cpu.image.store.3d.v4f32` (roadmap H19c): always mip level 0, so
// the real depth extent to bound `Z` against is simply `Img->Depth`
// itself (`LevelDepth` at level 0 -- see `femeRTFetchTexel3D`'s own
// comment -- collapses to `Img->Depth` for any real, nonzero-depth image).
__attribute__((always_inline)) static void
femeRTStoreTexel3D(const FemeRTImageDescriptor *Img, int32_t X, int32_t Y,
                   int32_t Z, FemeRTv4f32 Texel) {
  if (!Img->Data || Img->MipLayoutCount == 0)
    return;
  if (X < 0 || Y < 0 || Z < 0 || (uint32_t)X >= Img->Width ||
      (uint32_t)Y >= Img->Height || (uint32_t)Z >= Img->Depth)
    return;
  uint64_t ElemSize = femeRTImageFormatElementSize(Img->Format);
  if (ElemSize == 0)
    return;
  const FemeRTImageSubresourceLayout *Layout = &Img->MipLayouts[0];
  uint64_t Offset = Layout->Offset + (uint64_t)Z * Layout->SlicePitch +
                    (uint64_t)Y * Layout->RowPitch + (uint64_t)X * ElemSize;
  if (Offset + ElemSize > Img->SizeInBytes)
    return;
  unsigned char *Ptr = (unsigned char *)Img->Data + Offset;
  femeRTPackImageTexel(Img->Format, Ptr, Texel);
}

// The integer counterpart of `femeRTStoreTexel3D` above, for
// `feme.cpu.image.store.3d.v4i32` (roadmap H19c).
__attribute__((always_inline)) static void
femeRTStoreTexel3DI32(const FemeRTImageDescriptor *Img, int32_t X, int32_t Y,
                      int32_t Z, FemeRTv4i32 Texel) {
  if (!Img->Data || Img->MipLayoutCount == 0)
    return;
  if (X < 0 || Y < 0 || Z < 0 || (uint32_t)X >= Img->Width ||
      (uint32_t)Y >= Img->Height || (uint32_t)Z >= Img->Depth)
    return;
  uint64_t ElemSize = femeRTImageFormatElementSize(Img->Format);
  if (ElemSize == 0)
    return;
  const FemeRTImageSubresourceLayout *Layout = &Img->MipLayouts[0];
  uint64_t Offset = Layout->Offset + (uint64_t)Z * Layout->SlicePitch +
                    (uint64_t)Y * Layout->RowPitch + (uint64_t)X * ElemSize;
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
// concept in the spec, not a separate one per source). `InstructionBias`
// (roadmap L58) is SPIR-V's own per-instruction `Bias` image operand
// (GLSL's `texture(sampler, coord, bias)`), summed alongside the
// sampler's own `Samp->LodBias` (`VkSamplerCreateInfo::mipLodBias`) per
// the Vulkan spec's own "total bias" formula -- `0.0f` for every sample
// with no `Bias` image operand of its own (SPIR-V forbids `Bias`
// alongside an explicit `Lod`, mirroring `InstructionMinLod`'s own
// `MinLod` restriction below). Shared by
// `femeRTSelectMipLevels` (which mip level(s) `lod'` selects) and
// `femeRTUseLinearFilter` below (whether `lod'` itself is a magnifying or
// minifying sample) so the two decisions can never disagree about which
// LOD they are looking at.
__attribute__((always_inline)) static float
femeRTComputeClampedLod(float Lod, _Bool UseExplicitLod,
                        const FemeRTSamplerDescriptor *Samp,
                        float InstructionMinLod, float InstructionBias) {
  float L = UseExplicitLod ? Lod : 0.0f;
  L += Samp->LodBias + InstructionBias;
  // Roadmap L26: SPIR-V's own per-instruction `MinLod` image operand
  // (HLSL's `Texture2D::Sample`'s trailing `clamp` argument) is an
  // additional floor alongside the sampler's own `minLod`
  // (`VkSamplerCreateInfo::minLod`) -- the greater of the two wins, same
  // as any other pair of independent minimums. `InstructionMinLod` is
  // `-INFINITY` (a no-op `fmaxf` operand) for every sample with no
  // `MinLod` image operand of its own.
  float MinLod = __builtin_fmaxf(Samp->MinLod, InstructionMinLod);
  return __builtin_fmaxf(MinLod, __builtin_fminf(L, Samp->MaxLod));
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
// approximation's own small mid-octave error shows up as a real, if
// minor, blend-weight inaccuracy rather than being rounded away. This
// file is compiled freestanding (see `femeRTHalfToFloat`'s own comment),
// so it reinterprets the value's own IEEE-754 bit pattern directly
// rather than call a transcendental libm routine this build cannot
// assume exists.
//
// (Roadmap L66(h)) The single-term linear "float-as-int" reinterpretation
// this function used before this fix (`Bits * (1/2^23) - 126.94269504`)
// has a real max error of ~0.057 log2 units mid-octave -- easily large
// enough to place `femeRTPlanImplicitLod`'s computed LOD on the wrong
// side of `femeRTNearestMipLevel`'s own `Frac < 0.5` mip-rounding
// boundary versus VK-GL-CTS's own exact-log2 reference oracle
// (`computeLodFromDerivates`'s `LODMODE_EXACT` mode, `deFloatLog2`) --
// confirmed via a real `dEQP-VK.glsl.texture_functions.texturegrad.
// samplercubeshadow_{fragment,vertex}` capture showing a small
// (90/16384-pixel) but genuine "Image mismatch" along several evenly-
// doubling-spaced screen-space diagonals (this test's own derivative
// sweeps continuously across several octaves within one 128x128 image,
// crossing several such rounding boundaries -- unlike the sibling
// `Plain2D`/`Array1D`/`Array2D` `Dref`+`Grad` shadow tests, whose own
// case-spec derivative magnitudes happen to stay within a single octave
// throughout, never exercising this same rounding sensitivity). An
// ordinary (non-`Dref`) sample tolerates this same error invisibly (a
// slightly-wrong LOD blends into a barely-different filtered color,
// safely within `deqp-vk`'s own image-comparison threshold), but a
// depth-comparison sample's boolean pass/fail result is not -- any
// mip-level disagreement flips the compare outright.
//
// Replaced with a degree-3 minimax polynomial correction of the
// mantissa's own fractional part (still just bit tricks and arithmetic,
// no libm call): extracts the unbiased exponent and the `[1, 2)`
// mantissa separately, then approximates `log2(1+f)` for the mantissa's
// own fractional part `f` with a cubic fit (coefficients least-squares
// minimax-fitted against a real `log2` table) instead of the old
// single-term linear fit -- cuts the max mid-octave error to ~0.0013 log2
// units (over 40x tighter), while preserving the exact-at-every-power-
// of-two property every existing caller already depends on.
__attribute__((always_inline)) static float femeRTFastLog2(float X) {
  uint32_t Bits;
  __builtin_memcpy(&Bits, &X, sizeof(Bits));
  int32_t Exp = (int32_t)(Bits >> 23) - 127;
  uint32_t MantissaBits = (Bits & 0x007FFFFFu) | (127u << 23);
  float M;
  __builtin_memcpy(&M, &MantissaBits, sizeof(M));
  float F = M - 1.0f; // Mantissa's own fractional part, in [0, 1).
  float Poly = F * (1.42349512f + F * (-0.58777299f + F * 0.16559316f));
  return (float)Exp + Poly;
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
                     float DUdY, float DVdX, float DVdY,
                     float InstructionMinLod, float InstructionBias) {
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

  Plan.ClampedLod = femeRTComputeClampedLod(
      Lod, /*UseExplicitLod=*/1, Samp, InstructionMinLod, InstructionBias);
  return Plan;
}

// (Roadmap L63) The single-axis counterpart of `femeRTPlanImplicitLod`
// above, for an implicit-LOD `Plain1D`/`Array1D` color sample: the same
// texel-space "scale factor" construction, narrowed to this shape's own
// single addressed coordinate component (there is no `V` axis, and no
// anisotropic multi-tap footprint -- unlike a 2D surface, a 1D texture
// has no second screen-space axis a footprint could meaningfully spread
// taps across, and no real CTS case exercises anisotropic filtering
// against this shape, so this deliberately stays the simpler, single-tap
// isotropic-only construction `femeRTPlanImplicitLod`'s own 2D case
// degenerates to whenever its `V`-axis derivatives are zero, but computed
// directly rather than reusing that function with zeroed `DVdX`/`DVdY`
// arguments -- doing so would incorrectly treat this shape's absent `V`
// axis as a maximally anisotropic 2D footprint whenever the sampler
// enables anisotropy, since `femeRTPlanImplicitLod`'s own zero-`Pmin`
// handling is written for a real, if degenerate, 2D surface "viewed edge
// on", not a shape with no second axis at all). Returns just the plain
// (unclamped) LOD; the caller still applies `femeRTComputeClampedLod`
// itself, mirroring every other implicit-LOD path's own division of
// labor.
__attribute__((always_inline)) static float
femeRTPlanImplicitLod1D(const FemeRTImageDescriptor *Img, float DUdX,
                        float DUdY) {
  float Ux = DUdX * (float)Img->Width, Uy = DUdY * (float)Img->Width;
  float AbsUx = Ux < 0.0f ? -Ux : Ux;
  float AbsUy = Uy < 0.0f ? -Uy : Uy;
  float Pmax = AbsUx > AbsUy ? AbsUx : AbsUy;
  return Pmax <= 0.0f ? 0.0f : femeRTFastLog2(Pmax);
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
                             uint32_t Level, int32_t OffsetX,
                             int32_t OffsetY) {
  uint32_t LevelWidth = femeRTMipExtent(Img->Width, Level);
  uint32_t LevelHeight = femeRTMipExtent(Img->Height, Level);
  float TexelU = U * (float)LevelWidth - 0.5f;
  float TexelV = V * (float)LevelHeight - 0.5f;
  float FloorU = __builtin_floorf(TexelU);
  float FloorV = __builtin_floorf(TexelV);
  int32_t BaseX = (int32_t)FloorU + OffsetX;
  int32_t BaseY = (int32_t)FloorV + OffsetY;

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
                    uint32_t Level, uint32_t Layer, int32_t OffsetX,
                    int32_t OffsetY) {
  uint32_t LevelWidth = femeRTMipExtent(Img->Width, Level);
  uint32_t LevelHeight = femeRTMipExtent(Img->Height, Level);
  int32_t X = (int32_t)__builtin_floorf(U * (float)LevelWidth) + OffsetX;
  int32_t Y = (int32_t)__builtin_floorf(V * (float)LevelHeight) + OffsetY;
  _Bool BorderX = 0, BorderY = 0;
  int32_t AddrX =
      femeRTApplyAddressMode(X, (int32_t)LevelWidth, Samp->AddressU, &BorderX);
  int32_t AddrY =
      femeRTApplyAddressMode(Y, (int32_t)LevelHeight, Samp->AddressV, &BorderY);
  return femeRTFetchTexel2D(Img, Level, Layer, AddrX, AddrY, /*Sample=*/0,
                            BorderX || BorderY, Samp->BorderColor,
                            /*ApplySwizzle=*/1);
}

// Bilinearly filters `Img` at `(U, V)`, array layer `Layer` (roadmap
// H7b-a; always `0` for a non-arrayed image), blending the four texels
// `femeRTComputeBilinearSupport` selects.
__attribute__((always_inline)) static FemeRTv4f32
femeRTSampleLinear2D(const FemeRTImageDescriptor *Img,
                     const FemeRTSamplerDescriptor *Samp, float U, float V,
                     uint32_t Level, uint32_t Layer, int32_t OffsetX,
                     int32_t OffsetY) {
  FemeRTBilinearSupport S =
      femeRTComputeBilinearSupport(Img, U, V, Samp, Level, OffsetX, OffsetY);
  FemeRTv4f32 T00 = femeRTFetchTexel2D(
      Img, Level, Layer, S.X0, S.Y0, /*Sample=*/0, S.BorderX0 || S.BorderY0,
      Samp->BorderColor, /*ApplySwizzle=*/1);
  FemeRTv4f32 T10 = femeRTFetchTexel2D(
      Img, Level, Layer, S.X1, S.Y0, /*Sample=*/0, S.BorderX1 || S.BorderY0,
      Samp->BorderColor, /*ApplySwizzle=*/1);
  FemeRTv4f32 T01 = femeRTFetchTexel2D(
      Img, Level, Layer, S.X0, S.Y1, /*Sample=*/0, S.BorderX0 || S.BorderY1,
      Samp->BorderColor, /*ApplySwizzle=*/1);
  FemeRTv4f32 T11 = femeRTFetchTexel2D(
      Img, Level, Layer, S.X1, S.Y1, /*Sample=*/0, S.BorderX1 || S.BorderY1,
      Samp->BorderColor, /*ApplySwizzle=*/1);
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
                       uint32_t Layer, float ClampedLod, int32_t OffsetX,
                       int32_t OffsetY) {
  _Bool UseLinear = femeRTUseLinearFilter(ClampedLod, Samp);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(Img, ClampedLod);
  _Bool Trilinear = Samp->MipFilter == 1 && MipPlan.Level0 != MipPlan.Level1;
  uint32_t Level0 = Trilinear ? MipPlan.Level0 : femeRTNearestMipLevel(MipPlan);
  FemeRTv4f32 Lo =
      UseLinear ? femeRTSampleLinear2D(Img, Samp, U, V, Level0, Layer,
                                       OffsetX, OffsetY)
               : femeRTSamplePoint2D(Img, Samp, U, V, Level0, Layer, OffsetX,
                                     OffsetY);
  if (!Trilinear)
    return Lo;
  FemeRTv4f32 Hi =
      UseLinear ? femeRTSampleLinear2D(Img, Samp, U, V, MipPlan.Level1, Layer,
                                       OffsetX, OffsetY)
               : femeRTSamplePoint2D(Img, Samp, U, V, MipPlan.Level1, Layer,
                                     OffsetX, OffsetY);
  return Lo + (Hi - Lo) * MipPlan.Frac;
}

// Point-samples (nearest texel) a 1D(-array) image at normalized
// coordinate `U`, array layer `Layer` (roadmap L52a; always `0` for a
// non-arrayed image) -- the plain-1D counterpart of `femeRTSamplePoint2D`
// above, using `femeRTFetchTexel1DArray`'s own reuse of the 2D fetch's
// array-layer parameter (see its own comment) instead of a real 1D fetch
// formula.
__attribute__((always_inline)) static FemeRTv4f32
femeRTSamplePoint1D(const FemeRTImageDescriptor *Img,
                    const FemeRTSamplerDescriptor *Samp, float U,
                    uint32_t Level, uint32_t Layer, int32_t OffsetX) {
  uint32_t LevelWidth = femeRTMipExtent(Img->Width, Level);
  int32_t X = (int32_t)__builtin_floorf(U * (float)LevelWidth) + OffsetX;
  _Bool BorderX = 0;
  int32_t AddrX =
      femeRTApplyAddressMode(X, (int32_t)LevelWidth, Samp->AddressU, &BorderX);
  return femeRTFetchTexel1DArray(Img, Level, AddrX, Layer, /*Sample=*/0,
                                 BorderX, Samp->BorderColor,
                                 /*ApplySwizzle=*/1);
}

// Linearly filters a 1D(-array) image at normalized coordinate `U`, array
// layer `Layer` -- the plain-1D counterpart of `femeRTSampleLinear2D`
// above, blending the two texel-center-offset taps `femeRTSamplePoint2D`'s
// own `TexelU`/`FloorU` formula (repeated here, minus the `V` axis)
// selects.
__attribute__((always_inline)) static FemeRTv4f32
femeRTSampleLinear1D(const FemeRTImageDescriptor *Img,
                     const FemeRTSamplerDescriptor *Samp, float U,
                     uint32_t Level, uint32_t Layer, int32_t OffsetX) {
  uint32_t LevelWidth = femeRTMipExtent(Img->Width, Level);
  float TexelU = U * (float)LevelWidth - 0.5f;
  float FloorU = __builtin_floorf(TexelU);
  int32_t BaseX = (int32_t)FloorU + OffsetX;
  float Wx = TexelU - FloorU;
  _Bool BorderX0 = 0, BorderX1 = 0;
  int32_t X0 = femeRTApplyAddressMode(BaseX, (int32_t)LevelWidth,
                                      Samp->AddressU, &BorderX0);
  int32_t X1 = femeRTApplyAddressMode(BaseX + 1, (int32_t)LevelWidth,
                                      Samp->AddressU, &BorderX1);
  FemeRTv4f32 T0 = femeRTFetchTexel1DArray(Img, Level, X0, Layer,
                                           /*Sample=*/0, BorderX0,
                                           Samp->BorderColor,
                                           /*ApplySwizzle=*/1);
  FemeRTv4f32 T1 = femeRTFetchTexel1DArray(Img, Level, X1, Layer,
                                           /*Sample=*/0, BorderX1,
                                           Samp->BorderColor,
                                           /*ApplySwizzle=*/1);
  return T0 + (T1 - T0) * Wx;
}

// (Roadmap L52a) Filters a 1D(-array) image at normalized coordinate `U`,
// array layer `Layer`, given an already biased-and-clamped level-of-detail
// value `ClampedLod` -- the plain-1D counterpart of `femeRTSampleFiltered2D`
// above, making the same mag/min-filter (`femeRTUseLinearFilter`) and
// trilinear-blend (`femeRTSelectMipLevels`) decisions, minus the `V` axis.
__attribute__((always_inline)) static FemeRTv4f32
femeRTSampleFiltered1D(const FemeRTImageDescriptor *Img,
                       const FemeRTSamplerDescriptor *Samp, float U,
                       uint32_t Layer, float ClampedLod, int32_t OffsetX) {
  _Bool UseLinear = femeRTUseLinearFilter(ClampedLod, Samp);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(Img, ClampedLod);
  _Bool Trilinear = Samp->MipFilter == 1 && MipPlan.Level0 != MipPlan.Level1;
  uint32_t Level0 = Trilinear ? MipPlan.Level0 : femeRTNearestMipLevel(MipPlan);
  FemeRTv4f32 Lo = UseLinear
                       ? femeRTSampleLinear1D(Img, Samp, U, Level0, Layer,
                                              OffsetX)
                       : femeRTSamplePoint1D(Img, Samp, U, Level0, Layer,
                                            OffsetX);
  if (!Trilinear)
    return Lo;
  FemeRTv4f32 Hi = UseLinear
                       ? femeRTSampleLinear1D(Img, Samp, U, MipPlan.Level1,
                                              Layer, OffsetX)
                       : femeRTSamplePoint1D(Img, Samp, U, MipPlan.Level1,
                                            Layer, OffsetX);
  return Lo + (Hi - Lo) * MipPlan.Frac;
}

// Point-samples (nearest texel) a `Plain3D` image at normalized
// coordinates `(U, V, W)` (roadmap L66(a)) -- the volumetric counterpart
// of `femeRTSamplePoint2D` above, using `femeRTFetchTexel3D`'s own real
// per-mip depth-extent formula (`Img->Depth`, never an array layer --
// SPIR-V forbids an arrayed `Dim::3D` image, see `femeRTFetchTexel3D`'s
// own comment) in place of `femeRTFetchTexel2D`'s array-layer parameter.
// `OffsetX`/`OffsetY`/`OffsetZ` (roadmap L67(c)) mirror
// `femeRTSamplePoint2D`'s own `OffsetX`/`OffsetY`, widened to a real
// third, depth-axis component.
__attribute__((always_inline)) static FemeRTv4f32
femeRTSamplePoint3D(const FemeRTImageDescriptor *Img,
                    const FemeRTSamplerDescriptor *Samp, float U, float V,
                    float W, uint32_t Level, int32_t OffsetX, int32_t OffsetY,
                    int32_t OffsetZ) {
  uint32_t LevelWidth = femeRTMipExtent(Img->Width, Level);
  uint32_t LevelHeight = femeRTMipExtent(Img->Height, Level);
  uint32_t LevelDepth = femeRTMipExtent(Img->Depth, Level);
  int32_t X = (int32_t)__builtin_floorf(U * (float)LevelWidth) + OffsetX;
  int32_t Y = (int32_t)__builtin_floorf(V * (float)LevelHeight) + OffsetY;
  int32_t Z = (int32_t)__builtin_floorf(W * (float)LevelDepth) + OffsetZ;
  _Bool BorderX = 0, BorderY = 0, BorderZ = 0;
  int32_t AddrX =
      femeRTApplyAddressMode(X, (int32_t)LevelWidth, Samp->AddressU, &BorderX);
  int32_t AddrY =
      femeRTApplyAddressMode(Y, (int32_t)LevelHeight, Samp->AddressV, &BorderY);
  int32_t AddrZ =
      femeRTApplyAddressMode(Z, (int32_t)LevelDepth, Samp->AddressW, &BorderZ);
  return femeRTFetchTexel3D(Img, Level, AddrX, AddrY, AddrZ,
                            BorderX || BorderY || BorderZ, Samp->BorderColor,
                            /*ApplySwizzle=*/1);
}

// Trilinearly filters a `Plain3D` image at normalized coordinates
// `(U, V, W)`, blending the eight texels `femeRTSampleLinear2D`'s own
// four-corner formula generalizes to once a third, depth axis is added --
// each pair of adjacent depth-slice corners first blended exactly like
// `femeRTSampleLinear2D`'s own 2D bilinear result, then those two
// intermediate results blended a final time along `W`. `OffsetX`/
// `OffsetY`/`OffsetZ` (roadmap L67(c)) mirror `femeRTSampleLinear2D`'s
// own `OffsetX`/`OffsetY`, widened the same way `femeRTSamplePoint3D`'s
// own are above.
__attribute__((always_inline)) static FemeRTv4f32
femeRTSampleLinear3D(const FemeRTImageDescriptor *Img,
                     const FemeRTSamplerDescriptor *Samp, float U, float V,
                     float W, uint32_t Level, int32_t OffsetX,
                     int32_t OffsetY, int32_t OffsetZ) {
  uint32_t LevelWidth = femeRTMipExtent(Img->Width, Level);
  uint32_t LevelHeight = femeRTMipExtent(Img->Height, Level);
  uint32_t LevelDepth = femeRTMipExtent(Img->Depth, Level);
  float TexelU = U * (float)LevelWidth - 0.5f;
  float TexelV = V * (float)LevelHeight - 0.5f;
  float TexelW = W * (float)LevelDepth - 0.5f;
  float FloorU = __builtin_floorf(TexelU);
  float FloorV = __builtin_floorf(TexelV);
  float FloorW = __builtin_floorf(TexelW);
  float Wx = TexelU - FloorU, Wy = TexelV - FloorV, Wz = TexelW - FloorW;
  int32_t BaseX = (int32_t)FloorU + OffsetX;
  int32_t BaseY = (int32_t)FloorV + OffsetY;
  int32_t BaseZ = (int32_t)FloorW + OffsetZ;
  _Bool BX0 = 0, BX1 = 0, BY0 = 0, BY1 = 0, BZ0 = 0, BZ1 = 0;
  int32_t X0 = femeRTApplyAddressMode(BaseX, (int32_t)LevelWidth,
                                      Samp->AddressU, &BX0);
  int32_t X1 = femeRTApplyAddressMode(BaseX + 1, (int32_t)LevelWidth,
                                      Samp->AddressU, &BX1);
  int32_t Y0 = femeRTApplyAddressMode(BaseY, (int32_t)LevelHeight,
                                      Samp->AddressV, &BY0);
  int32_t Y1 = femeRTApplyAddressMode(BaseY + 1, (int32_t)LevelHeight,
                                      Samp->AddressV, &BY1);
  int32_t Z0 = femeRTApplyAddressMode(BaseZ, (int32_t)LevelDepth,
                                      Samp->AddressW, &BZ0);
  int32_t Z1 = femeRTApplyAddressMode(BaseZ + 1, (int32_t)LevelDepth,
                                      Samp->AddressW, &BZ1);
  FemeRTv4f32 T000 = femeRTFetchTexel3D(Img, Level, X0, Y0, Z0,
                                        BX0 || BY0 || BZ0, Samp->BorderColor,
                                        /*ApplySwizzle=*/1);
  FemeRTv4f32 T100 = femeRTFetchTexel3D(Img, Level, X1, Y0, Z0,
                                        BX1 || BY0 || BZ0, Samp->BorderColor,
                                        /*ApplySwizzle=*/1);
  FemeRTv4f32 T010 = femeRTFetchTexel3D(Img, Level, X0, Y1, Z0,
                                        BX0 || BY1 || BZ0, Samp->BorderColor,
                                        /*ApplySwizzle=*/1);
  FemeRTv4f32 T110 = femeRTFetchTexel3D(Img, Level, X1, Y1, Z0,
                                        BX1 || BY1 || BZ0, Samp->BorderColor,
                                        /*ApplySwizzle=*/1);
  FemeRTv4f32 T001 = femeRTFetchTexel3D(Img, Level, X0, Y0, Z1,
                                        BX0 || BY0 || BZ1, Samp->BorderColor,
                                        /*ApplySwizzle=*/1);
  FemeRTv4f32 T101 = femeRTFetchTexel3D(Img, Level, X1, Y0, Z1,
                                        BX1 || BY0 || BZ1, Samp->BorderColor,
                                        /*ApplySwizzle=*/1);
  FemeRTv4f32 T011 = femeRTFetchTexel3D(Img, Level, X0, Y1, Z1,
                                        BX0 || BY1 || BZ1, Samp->BorderColor,
                                        /*ApplySwizzle=*/1);
  FemeRTv4f32 T111 = femeRTFetchTexel3D(Img, Level, X1, Y1, Z1,
                                        BX1 || BY1 || BZ1, Samp->BorderColor,
                                        /*ApplySwizzle=*/1);
  FemeRTv4f32 Top0 = T000 + (T100 - T000) * Wx;
  FemeRTv4f32 Bottom0 = T010 + (T110 - T010) * Wx;
  FemeRTv4f32 Slice0 = Top0 + (Bottom0 - Top0) * Wy;
  FemeRTv4f32 Top1 = T001 + (T101 - T001) * Wx;
  FemeRTv4f32 Bottom1 = T011 + (T111 - T011) * Wx;
  FemeRTv4f32 Slice1 = Top1 + (Bottom1 - Top1) * Wy;
  return Slice0 + (Slice1 - Slice0) * Wz;
}

// (Roadmap L66(a)) Filters a `Plain3D` image at normalized coordinates
// `(U, V, W)`, given an already biased-and-clamped level-of-detail value
// `ClampedLod` -- the volumetric counterpart of `femeRTSampleFiltered2D`
// above, making the same mag/min-filter (`femeRTUseLinearFilter`) and
// trilinear-mip-blend (`femeRTSelectMipLevels`) decisions; `OffsetX`/
// `OffsetY`/`OffsetZ` (roadmap L67(c)) mirror `femeRTSampleFiltered2D`'s
// own `OffsetX`/`OffsetY`, widened to a real third, depth-axis component
// (see `femeRTSamplePoint3D`'s own updated doc). `Bias`/`MinLodClamp`
// (roadmap L67(a)) are applied by the caller before `ClampedLod` is even
// computed (mirroring `femeRTSampleFiltered1D`'s own identical division
// of labor), so this function itself needs no operand of its own for
// either.
__attribute__((always_inline)) static FemeRTv4f32
femeRTSampleFiltered3D(const FemeRTImageDescriptor *Img,
                       const FemeRTSamplerDescriptor *Samp, float U, float V,
                       float W, float ClampedLod, int32_t OffsetX,
                       int32_t OffsetY, int32_t OffsetZ) {
  _Bool UseLinear = femeRTUseLinearFilter(ClampedLod, Samp);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(Img, ClampedLod);
  _Bool Trilinear = Samp->MipFilter == 1 && MipPlan.Level0 != MipPlan.Level1;
  uint32_t Level0 = Trilinear ? MipPlan.Level0 : femeRTNearestMipLevel(MipPlan);
  FemeRTv4f32 Lo =
      UseLinear ? femeRTSampleLinear3D(Img, Samp, U, V, W, Level0, OffsetX,
                                       OffsetY, OffsetZ)
               : femeRTSamplePoint3D(Img, Samp, U, V, W, Level0, OffsetX,
                                     OffsetY, OffsetZ);
  if (!Trilinear)
    return Lo;
  FemeRTv4f32 Hi =
      UseLinear ? femeRTSampleLinear3D(Img, Samp, U, V, W, MipPlan.Level1,
                                       OffsetX, OffsetY, OffsetZ)
               : femeRTSamplePoint3D(Img, Samp, U, V, W, MipPlan.Level1,
                                     OffsetX, OffsetY, OffsetZ);
  return Lo + (Hi - Lo) * MipPlan.Frac;
}

// (Roadmap L66(a)) The volumetric counterpart of `femeRTPlanImplicitLod1D`
// above, for an implicit-LOD `Plain3D` color sample: the same texel-space
// "scale factor" construction extended to a third, depth axis, still
// isotropic-only (no anisotropic multi-tap footprint) -- mirroring
// `femeRTPlanImplicitLod1D`'s own reasoning for staying isotropic-only
// (no real CTS case exercises anisotropic filtering against a volume
// texture, and the Vulkan spec itself never defines a meaningfully
// different anisotropic footprint for a third axis with no screen-space
// analogue). Returns just the plain (unclamped) LOD; the caller still
// applies `femeRTComputeClampedLod` itself, mirroring every other
// implicit-LOD path's own division of labor.
__attribute__((always_inline)) static float
femeRTPlanImplicitLod3D(const FemeRTImageDescriptor *Img, float DUdX,
                        float DUdY, float DVdX, float DVdY, float DWdX,
                        float DWdY) {
  float Ux = DUdX * (float)Img->Width, Uy = DUdY * (float)Img->Width;
  float Vx = DVdX * (float)Img->Height, Vy = DVdY * (float)Img->Height;
  float Wx = DWdX * (float)Img->Depth, Wy = DWdY * (float)Img->Depth;
  float Px = __builtin_sqrtf(Ux * Ux + Vx * Vx + Wx * Wx);
  float Py = __builtin_sqrtf(Uy * Uy + Vy * Vy + Wy * Wy);
  float Pmax = Px > Py ? Px : Py;
  return Pmax <= 0.0f ? 0.0f : femeRTFastLog2(Pmax);
}

// `feme.cpu.image.sample.3d.v4f32` (roadmap L66(a), extended with a real
// `Bias`/`MinLodClamp` pair by roadmap L67(a) and a real `ConstOffset`
// triple by roadmap L67(c)): samples a `Plain3D` sampled image at
// normalized coordinates `(U, V, W)`, the volumetric counterpart of
// `feme.cpu.image.sample.2d.v4f32` above -- same mag/min and mip
// filter-selection logic (`femeRTSampleFiltered3D`), same
// implicit-vs-explicit LOD split (`femeRTPlanImplicitLod3D` in place of
// `femeRTPlanImplicitLod`/`femeRTPlanImplicitLod1D`, since no real CTS
// case exercises anisotropic filtering against this shape -- see its own
// doc), and the same `Bias`/`MinLodClamp` pair `femeCpuImageSample1DV4F32`
// carries (roadmap L61(c)), threaded through to `femeRTComputeClampedLod`
// in place of the previous hardcoded `0.0f`/`-inf` no-op values.
// `OffsetX`/`OffsetY`/`OffsetZ` (roadmap L67(c)) mirror
// `femeCpuImageSample2DV4F32`'s own `OffsetX`/`OffsetY`, widened to a real
// third, depth-axis component (see `femeRTSamplePoint3D`'s own updated
// doc) -- threaded straight through to `femeRTSampleFiltered3D`, unlike
// `Bias`/`MinLodClamp` above, which are consumed by this function itself
// before `femeRTSampleFiltered3D` is even called. Still no `Grad`
// operand -- its own follow-on roadmap L67(b) sub-item.
FemeRTv4f32 femeCpuImageSample3DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float W,
    float DUdX, float DUdY, float DVdX, float DVdY, float DWdX, float DWdY,
    float Lod, _Bool UseExplicitLod, float Bias, int32_t OffsetX,
    int32_t OffsetY, int32_t OffsetZ, float MinLodClamp,
    _Bool Mask) asm("feme.cpu.image.sample.3d.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageSample3DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float W,
    float DUdX, float DUdY, float DVdX, float DVdY, float DWdX, float DWdY,
    float Lod, _Bool UseExplicitLod, float Bias, int32_t OffsetX,
    int32_t OffsetY, int32_t OffsetZ, float MinLodClamp, _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  float RawLod = UseExplicitLod
                     ? Lod
                     : femeRTPlanImplicitLod3D(&Img, DUdX, DUdY, DVdX, DVdY,
                                               DWdX, DWdY);
  // `UseExplicitLod=1` always -- see `femeCpuImageSample1DV4F32`'s own
  // identical comment above; `MinLodClamp`/`Bias` are now real
  // caller-supplied values (roadmap L67(a)), not hardcoded no-ops.
  float ClampedLod = femeRTComputeClampedLod(RawLod, /*UseExplicitLod=*/1,
                                            &Samp, /*InstructionMinLod=*/
                                            MinLodClamp,
                                            /*InstructionBias=*/Bias);
  return femeRTSampleFiltered3D(&Img, &Samp, U, V, W, ClampedLod, OffsetX,
                                OffsetY, OffsetZ);
}

// (Roadmap L55) Depth-comparison sampling against a normalized
// (fixed-point) depth format -- today, only `D16_UNORM` (format `31`) --
// must clamp both the shader-supplied compare reference and the fetched
// depth value to `[0, 1]` before comparing, matching VK-GL-CTS's own
// reference oracle (`tcuTexture.cpp`'s `execCompare`, gated on its own
// `isFixedPointDepth` flag) and the Vulkan spec's own depth-compare
// operation (16.5, "Depth Compare Operation"): a `D16_UNORM` texel can
// never itself represent a value outside `[0, 1]`, so an app-supplied
// reference value outside that range is defined to clamp, not to always
// trivially pass/fail every comparison against it. `D32_FLOAT` (format
// `32`) is the floating-point case VK-GL-CTS's own `isFixedPointDepth`
// leaves unclamped -- a float depth format can genuinely store (and
// compare against) an out-of-`[0,1]` value, so no clamping happens
// there. This mirrors `isFixedPointDepthTextureFormat`'s own
// `D`/`R`-order channel-class check, simplified to feme's own small,
// fixed set of supported depth formats.
__attribute__((always_inline)) static _Bool
femeRTIsFixedPointDepthFormat(uint32_t Format) {
  return Format == 31; // D16_UNORM.
}

// Applies `SamplerCompareFunc` `Func` as `Ref Func StoredTexel` (Direct3D's
// `SamplerComparisonFunc`/Vulkan's `VkCompareOp` convention), returning
// `1.0f` for pass and `0.0f` for fail. `IsFixedPointDepth` (roadmap L55,
// see `femeRTIsFixedPointDepthFormat` above) clamps both `Ref` and
// `Texel` to `[0, 1]` first when set, matching a normalized depth
// format's own real representable range.
__attribute__((always_inline)) static float
femeRTApplyCompare(uint32_t Func, float Ref, float Texel,
                   _Bool IsFixedPointDepth) {
  if (IsFixedPointDepth) {
    Ref = Ref < 0.0f ? 0.0f : (Ref > 1.0f ? 1.0f : Ref);
    Texel = Texel < 0.0f ? 0.0f : (Texel > 1.0f ? 1.0f : Texel);
  }
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
// level(s) directly (SPIR-V forbids a `Bias` operand alongside an
// explicit `Lod`, so `Bias` is always `0.0f` in that case); an
// implicit-LOD one instead derives its own level -- and, when `Samp`
// enables anisotropic filtering, a multi-tap anisotropic footprint --
// from the caller's own screen-space partial derivatives of `(U, V)`
// (`DUdX`/`DUdY`/`DVdX`/`DVdY`, roadmap H7i; see `femeRTPlanImplicitLod`),
// ignoring `Lod` entirely in that case; `Bias` (roadmap L58) is an
// additional per-instruction term (SPIR-V's own `Bias` image operand,
// GLSL's `texture(sampler, coord, bias)`) added to that derived LOD
// alongside the sampler's own `mipLodBias` (see `femeRTComputeClampedLod`'s
// own doc). An inactive lane, an unsampled or unwritten image, reads as
// zero (see "Bounds checking").
FemeRTv4f32 femeCpuImageSample2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float DUdX,
    float DUdY, float DVdX, float DVdY, float Lod, _Bool UseExplicitLod,
    float Bias, int32_t OffsetX, int32_t OffsetY, float MinLodClamp,
    _Bool Mask) asm("feme.cpu.image.sample.2d.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageSample2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float DUdX,
    float DUdY, float DVdX, float DVdY, float Lod, _Bool UseExplicitLod,
    float Bias, int32_t OffsetX, int32_t OffsetY, float MinLodClamp,
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
    // SPIR-V's `MinLod`/`Bias` image operands only ever combine with an
    // implicit-LOD sample (see `ImageSampleImplicitLodPattern`'s own
    // comment) -- `MinLodClamp` is always `-INFINITY` (a no-op) and
    // `Bias` is always `0.0f` whenever `UseExplicitLod` is set.
    float ClampedLod = femeRTComputeClampedLod(Lod, /*UseExplicitLod=*/1,
                                               &Samp, MinLodClamp, Bias);
    return femeRTSampleFiltered2D(&Img, &Samp, U, V, /*Layer=*/0, ClampedLod,
                                  OffsetX, OffsetY);
  }

  FemeRTImplicitLodPlan Plan = femeRTPlanImplicitLod(
      &Img, &Samp, DUdX, DUdY, DVdX, DVdY, MinLodClamp, Bias);
  if (Plan.TapCount <= 1)
    return femeRTSampleFiltered2D(&Img, &Samp, U, V, /*Layer=*/0,
                                  Plan.ClampedLod, OffsetX, OffsetY);

  // Anisotropic footprint: average `Plan.TapCount` same-level taps spread
  // symmetrically along the major axis, centered on `(U, V)` so the mean
  // sample point is exactly the original coordinate. The same
  // `(OffsetX, OffsetY)` constant texel offset (roadmap L26) applies to
  // every tap uniformly, exactly as it does to the single-tap case above.
  FemeRTv4f32 Sum = {0.0f, 0.0f, 0.0f, 0.0f};
  float FirstOffset = -0.5f * (float)(Plan.TapCount - 1);
  for (uint32_t Tap = 0; Tap != Plan.TapCount; ++Tap) {
    float Offset = FirstOffset + (float)Tap;
    float TapU = U + Offset * Plan.StepU;
    float TapV = V + Offset * Plan.StepV;
    Sum += femeRTSampleFiltered2D(&Img, &Samp, TapU, TapV, /*Layer=*/0,
                                  Plan.ClampedLod, OffsetX, OffsetY);
  }
  return Sum * (1.0f / (float)Plan.TapCount);
}

// (Roadmap H109) Nearest-filtered, explicit-LOD-only sample of an
// integer-channel (`_UINT`/`_SINT`) image, for `feme.cpu.image.sample.
// 2d.v4i32` -- the integer counterpart of `femeCpuImageSample2DV4F32`
// above. Unlike that function, this always reads a single mip level via
// `femeRTNearestMipLevel` and never bilinearly/trilinearly blends,
// regardless of `Samp`'s own filter/mipmap-mode fields: the Vulkan spec
// requires a `VkSampler` bound against an integer-format image to already
// use `VK_FILTER_NEAREST`/`VK_SAMPLER_MIPMAP_MODE_NEAREST` (enforced by
// validation before this driver ever sees the call), but this function is
// still defensive about it rather than trusting the caller, matching this
// file's general style elsewhere. There is no `Bias`/`Grad`/`MinLodClamp`
// operand -- see `ImageCallKind::Sample2DI32`'s own doc for why this is
// explicit-LOD only.
FemeRTv4i32 femeCpuImageSample2DV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float Lod,
    int32_t OffsetX, int32_t OffsetY,
    _Bool Mask) asm("feme.cpu.image.sample.2d.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuImageSample2DV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float Lod,
    int32_t OffsetX, int32_t OffsetY, _Bool Mask) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);

  // `MinLodClamp`/`Bias` are always the no-op values here (`-INFINITY`/
  // `0.0f`), mirroring `femeCpuImageSample2DV4F32`'s own explicit-LOD
  // case -- see this call kind's own doc for why no such operand exists.
  float ClampedLod = femeRTComputeClampedLod(
      Lod, /*UseExplicitLod=*/1, &Samp, -__builtin_inff(), 0.0f);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(&Img, ClampedLod);
  uint32_t Level = femeRTNearestMipLevel(MipPlan);
  uint32_t LevelWidth = femeRTMipExtent(Img.Width, Level);
  uint32_t LevelHeight = femeRTMipExtent(Img.Height, Level);
  int32_t X = (int32_t)__builtin_floorf(U * (float)LevelWidth) + OffsetX;
  int32_t Y = (int32_t)__builtin_floorf(V * (float)LevelHeight) + OffsetY;
  _Bool BorderX = 0, BorderY = 0;
  int32_t AddrX = femeRTApplyAddressMode(X, (int32_t)LevelWidth,
                                         Samp.AddressU, &BorderX);
  int32_t AddrY = femeRTApplyAddressMode(Y, (int32_t)LevelHeight,
                                         Samp.AddressV, &BorderY);
  if (BorderX || BorderY) {
    // Roadmap H109: `FemeRTSamplerDescriptor` has no integer border-color
    // storage (only a float `BorderColor[4]`) -- fall back to a fixed
    // `{0, 0, 0, 1}` default. Documented, narrow limitation: no real CTS
    // case is known to exercise `CLAMP_TO_BORDER` addressing against an
    // integer-sampled image yet (this call kind's own motivating case
    // always addresses an in-bounds coordinate).
    FemeRTv4i32 Border = {0, 0, 0, 1};
    return Border;
  }
  return femeRTFetchTexel2DI32(&Img, Level, /*Layer=*/0, AddrX, AddrY,
                               /*Sample=*/0, /*ApplySwizzle=*/1);
}

// (Roadmap L125(b)) The `Plain1D` counterpart of `femeCpuImageSample2DV4I32`
// above, for `feme.cpu.image.sample.1d.v4i32` -- mirrors that function's
// structure exactly, but addresses only a single (`X`) axis, matching
// `femeCpuImageSample1DV4F32`'s own relationship to
// `femeCpuImageSample2DV4F32`.
FemeRTv4i32 femeCpuImageSample1DV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float Lod,
    int32_t Offset, _Bool Mask) asm("feme.cpu.image.sample.1d.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuImageSample1DV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float Lod,
    int32_t Offset, _Bool Mask) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);

  // `MinLodClamp`/`Bias` are always the no-op values here (`-INFINITY`/
  // `0.0f`), mirroring `femeCpuImageSample2DV4I32`'s own choice above.
  float ClampedLod = femeRTComputeClampedLod(
      Lod, /*UseExplicitLod=*/1, &Samp, -__builtin_inff(), 0.0f);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(&Img, ClampedLod);
  uint32_t Level = femeRTNearestMipLevel(MipPlan);
  uint32_t LevelWidth = femeRTMipExtent(Img.Width, Level);
  int32_t X = (int32_t)__builtin_floorf(U * (float)LevelWidth) + Offset;
  _Bool BorderX = 0;
  int32_t AddrX = femeRTApplyAddressMode(X, (int32_t)LevelWidth,
                                         Samp.AddressU, &BorderX);
  if (BorderX) {
    // Roadmap L125(b): same documented, narrow limitation as
    // `femeCpuImageSample2DV4I32`'s own identical fallback above -- no
    // real CTS case is known to exercise `CLAMP_TO_BORDER` addressing
    // against an integer-sampled `Plain1D` image either.
    FemeRTv4i32 Border = {0, 0, 0, 1};
    return Border;
  }
  return femeRTFetchTexel1DI32(&Img, Level, AddrX, /*ApplySwizzle=*/1);
}

// (Roadmap L125(b)) The `Plain3D` counterpart of `femeCpuImageSample2DV4I32`
// above, for `feme.cpu.image.sample.3d.v4i32` -- mirrors that function's
// structure exactly, but addresses a third (`Z`, depth) axis too,
// matching `femeCpuImageSample3DV4F32`'s own relationship to
// `femeCpuImageSample2DV4F32`. No array layer to resolve (SPIR-V forbids
// an arrayed `Dim::3D` image), and `femeRTFetchTexel3DI32` already exists
// (reused by `feme.cpu.image.load.3d.v4i32`, roadmap H19c), so -- like
// `Array2D` before it -- no new low-level texel-fetch helper is needed
// here either.
FemeRTv4i32 femeCpuImageSample3DV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float W,
    float Lod, int32_t OffsetX, int32_t OffsetY, int32_t OffsetZ,
    _Bool Mask) asm("feme.cpu.image.sample.3d.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuImageSample3DV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float W,
    float Lod, int32_t OffsetX, int32_t OffsetY, int32_t OffsetZ,
    _Bool Mask) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);

  // `MinLodClamp`/`Bias` are always the no-op values here (`-INFINITY`/
  // `0.0f`), mirroring `femeCpuImageSample2DV4I32`'s own choice above.
  float ClampedLod = femeRTComputeClampedLod(
      Lod, /*UseExplicitLod=*/1, &Samp, -__builtin_inff(), 0.0f);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(&Img, ClampedLod);
  uint32_t Level = femeRTNearestMipLevel(MipPlan);
  uint32_t LevelWidth = femeRTMipExtent(Img.Width, Level);
  uint32_t LevelHeight = femeRTMipExtent(Img.Height, Level);
  uint32_t LevelDepth = femeRTMipExtent(Img.Depth, Level);
  int32_t X = (int32_t)__builtin_floorf(U * (float)LevelWidth) + OffsetX;
  int32_t Y = (int32_t)__builtin_floorf(V * (float)LevelHeight) + OffsetY;
  int32_t Z = (int32_t)__builtin_floorf(W * (float)LevelDepth) + OffsetZ;
  _Bool BorderX = 0, BorderY = 0, BorderZ = 0;
  int32_t AddrX = femeRTApplyAddressMode(X, (int32_t)LevelWidth,
                                         Samp.AddressU, &BorderX);
  int32_t AddrY = femeRTApplyAddressMode(Y, (int32_t)LevelHeight,
                                         Samp.AddressV, &BorderY);
  int32_t AddrZ = femeRTApplyAddressMode(Z, (int32_t)LevelDepth,
                                         Samp.AddressW, &BorderZ);
  if (BorderX || BorderY || BorderZ) {
    // Roadmap L125(b): same documented, narrow limitation as
    // `femeCpuImageSample2DV4I32`'s own identical fallback above -- no
    // real CTS case is known to exercise `CLAMP_TO_BORDER` addressing
    // against an integer-sampled `Plain3D` image either.
    FemeRTv4i32 Border = {0, 0, 0, 1};
    return Border;
  }
  return femeRTFetchTexel3DI32(&Img, Level, AddrX, AddrY, AddrZ,
                               /*ApplySwizzle=*/1);
}

// (Roadmap L52e) The raw, unclamped LOD `OpImageQueryLod`'s own second
// (`calculate.lod.unclamped`) lane reports, computed from the same
// texel-space "scale factor" construction `femeRTPlanImplicitLod` already
// uses for an ordinary implicit-LOD sample (`log2(Pmax)`, `Pmax` the
// larger of the footprint's two screen-axis extents) -- but, unlike that
// helper's own `Lod = 0.0f` convention for a zero-footprint (no
// measurable minification) input, `OpImageQueryLod`'s own reference
// semantics (`computeLodFromDerivates`,
// `vktShaderRenderTextureFunctionTests.cpp`) require `-infinity` here
// instead: a real sample query of a coordinate with no derivatives at all
// (e.g. every lane of a fragment-stage quad reading the exact same
// coordinate) is defined to report an unboundedly negative raw LOD, not
// the arbitrary `0.0` a real sample's own mip-level selection safely
// treats an all-zero footprint as (level 0 is a fine, if technically
// unjustified, choice for a case with nothing to minify; but a *query*
// caller is asking for the LOD value itself, so an equally-arbitrary
// `0.0` here would be an observably wrong answer, not just a merely
// suboptimal one). No anisotropy/multi-tap concept applies to a LOD query
// at all -- there is only ever one scalar LOD to report, unlike an actual
// multi-tap anisotropic sample's own per-tap levels.
__attribute__((always_inline)) static float
femeRTComputeUnclampedQueryLod(const FemeRTImageDescriptor *Img, float DUdX,
                               float DUdY, float DVdX, float DVdY) {
  float Ux = DUdX * (float)Img->Width, Uy = DUdY * (float)Img->Width;
  float Vx = DVdX * (float)Img->Height, Vy = DVdY * (float)Img->Height;
  float Px = __builtin_sqrtf(Ux * Ux + Vx * Vx);
  float Py = __builtin_sqrtf(Uy * Uy + Vy * Vy);
  float Pmax = Px > Py ? Px : Py;
  if (Pmax <= 0.0f)
    return -__builtin_inff();
  return femeRTFastLog2(Pmax);
}

// (Roadmap L52e) The clamped "level" `OpImageQueryLod`'s own first
// (`calculate.lod`) lane reports: \p UnclampedLod (already biased and
// min/max-clamped by `femeRTComputeClampedLod`, the same as an ordinary
// implicit-LOD sample's own mip level derivation) further clamped to
// `Img`'s own valid mip-level range `[0, MipLevels - 1]`, then --
// mirroring the CTS reference oracle's own `computeLevelFromLod`
// (`vktShaderRenderTextureFunctionTests.cpp`) -- either rounded to the
// nearest whole level (`Samp->MipFilter == 0`, `VK_SAMPLER_MIPMAP_MODE_
// NEAREST`, the same "round to nearest, ties up" convention
// `femeRTNearestMipLevel` already uses for an ordinary nearest-mipmap
// sample) or returned as the unrounded fractional value (`MipFilter ==
// 1`, `VK_SAMPLER_MIPMAP_MODE_LINEAR`, whose real per-pixel sample would
// itself blend two adjacent levels by this same fraction, so the query
// reports that continuous value rather than rounding it away). A
// non-mipmapped image (`MipLevels <= 1`, no second level to ever pick
// between) always reports exactly `0.0`, matching the reference oracle's
// own unconditional special case for that configuration.
__attribute__((always_inline)) static float
femeRTComputeClampedQueryLevel(const FemeRTImageDescriptor *Img,
                               const FemeRTSamplerDescriptor *Samp,
                               float ClampedLod) {
  if (Img->MipLevels <= 1)
    return 0.0f;
  float MaxLevel = (float)(Img->MipLevels - 1);
  float Level = __builtin_fmaxf(0.0f, __builtin_fminf(ClampedLod, MaxLevel));
  if (Samp->MipFilter != 0) // VK_SAMPLER_MIPMAP_MODE_LINEAR.
    return Level;
  // VK_SAMPLER_MIPMAP_MODE_NEAREST: round to the nearest whole level, the
  // same "round half up" convention `femeRTNearestMipLevel` uses (a
  // `Frac == 0.5` tie rounds up), reimplemented directly on the
  // continuous `Level` here rather than routing through
  // `femeRTSelectMipLevels`'s own two-adjacent-level `FemeRTMipTrilinear
  // Plan` shape, which this scalar-result query has no use for.
  float Rounded = __builtin_floorf(Level + 0.5f);
  return __builtin_fminf(Rounded, MaxLevel);
}

// `feme.cpu.image.querylod.2d.v2f32` (roadmap L52e): `Plain2D`'s own
// `OpImageQueryLod` runtime entry point -- see `ImageCallKind::QueryLod2D`
// (`ImageCalls.h`) for its `<2 x float>` result's own lane convention
// (lane 0 the clamped level, lane 1 the raw unclamped LOD). Unlike
// `femeCpuImageSample2DV4F32`, there is no `(U, V)` coordinate operand at
// all -- a LOD query's result depends only on the coordinate's own
// screen-space derivatives and the image's dimensions, never the
// coordinate value itself -- and no `Lod`/`UseExplicitLod`/offset/
// `MinLodClamp` operands either, since `OpImageQueryLod` always measures
// an implicit LOD from real derivatives, with no explicit-LOD form and no
// per-instruction `MinLod`/`ConstOffset` image operands of its own to
// thread through. An inactive lane, an unsampled image, or a null
// sampler reads as `{0.0, 0.0}` (see "Bounds checking"), the same
// convention `femeCpuImageSample2DV4F32` uses for its own all-zero
// `Zero` case.
FemeRTv2f32 femeCpuImageQueryLod2DV2F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DUdX, float DUdY,
    float DVdX, float DVdY,
    _Bool Mask) asm("feme.cpu.image.querylod.2d.v2f32");

__attribute__((always_inline)) FemeRTv2f32 femeCpuImageQueryLod2DV2F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DUdX, float DUdY,
    float DVdX, float DVdY, _Bool Mask) {
  FemeRTv2f32 Zero = {0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);

  float UnclampedLod =
      femeRTComputeUnclampedQueryLod(&Img, DUdX, DUdY, DVdX, DVdY);
  // Roadmap L52e design note: unlike an ordinary implicit-LOD sample
  // (`femeRTPlanImplicitLod`, whose `InstructionMinLod` models SPIR-V's
  // own per-instruction `MinLod` operand), `OpImageQueryLod` has no such
  // operand of its own -- `-infinity` (a no-op floor) is always passed
  // here. `femeRTComputeClampedLod` still applies here unchanged: its own
  // sampler-bias-plus-min/max-clamp logic is exactly what a real implicit
  // sample of this same coordinate would also apply before mip-level
  // selection, matching the CTS reference oracle's own
  // `computeLevelFromLod`.
  float ClampedLod = femeRTComputeClampedLod(UnclampedLod,
                                             /*UseExplicitLod=*/1, &Samp,
                                             /*InstructionMinLod=*/-__builtin_inff(),
                                             /*InstructionBias=*/0.0f);
  float ClampedLevel = femeRTComputeClampedQueryLevel(&Img, &Samp, ClampedLod);
  return (FemeRTv2f32){ClampedLevel, UnclampedLod};
}

// `feme.cpu.image.getdimensions.2d.v2i32` (roadmap L70): a plain 2D
// image's mip-0 `(Width, Height)` extent -- GLSL's own `imageSize()`/
// `textureSize()` against a `sampler2D`/`image2D` with no explicit LOD
// argument (SPIR-V `OpImageQuerySize`, `llvm.spv.resource.getdimensions.xy`).
// Unlike every sample/fetch call above, this needs neither a sampler heap
// (a plain size query, not a filtered access) nor a coordinate of its own
// -- only the bound image's own identity. An inactive lane or an unbound
// (`!Img.Data`) handle reads as `{0, 0}`, mirroring
// `femeCpuImageQueryLod2DV2F32`'s own all-zero `Zero` case for the same
// two conditions.
FemeRTv2i32 femeCpuImageGetDimensions2DV2I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex,
    _Bool Mask) asm("feme.cpu.image.getdimensions.2d.v2i32");

__attribute__((always_inline)) FemeRTv2i32 femeCpuImageGetDimensions2DV2I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, _Bool Mask) {
  FemeRTv2i32 Zero = {0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data)
    return Zero;
  return (FemeRTv2i32){(int32_t)Img.Width, (int32_t)Img.Height};
}

// `feme.cpu.image.getdimensions.lod.2d.v2i32` (roadmap L72(d)): a plain 2D
// image's own `(Width, Height)` extent at an explicit, possibly non-zero
// mip level -- GLSL's `textureSize(sampler, lod)` (SPIR-V
// `OpImageQuerySizeLod`), unlike `femeCpuImageGetDimensions2DV2I32`'s own
// always-mip-0 query. `Lod` is clamped into `[0, MipLevels - 1]`
// defensively (mirroring `femeRTSelectMipLevels`'s own clamping of a real
// sampled LOD) before halving `Width`/`Height` that many times, the same
// `max(1, Dim >> Level)` math `Image.cpp`'s own
// `computeSubresourceLayouts` already uses to compute a real mip level's
// subresource extent. An inactive lane or an unbound (`!Img.Data`) handle
// reads as `{0, 0}`, mirroring `femeCpuImageGetDimensions2DV2I32`'s own
// identical convention.
//
// (Roadmap L75) `femeRTClampQuerySizeLodMip` below factors this same
// `Lod`-clamping step out for every other-shape
// `femeCpuImageGetDimensionsLod*` sibling this function's own doc
// introduces, so it is not repeated five more times.
__attribute__((always_inline)) static uint32_t
femeRTClampQuerySizeLodMip(const FemeRTImageDescriptor *Img, int32_t Lod) {
  uint32_t ClampedLod = Lod < 0 ? 0 : (uint32_t)Lod;
  if (ClampedLod > Img->MipLevels - 1)
    ClampedLod = Img->MipLevels - 1;
  return ClampedLod;
}

FemeRTv2i32 femeCpuImageGetDimensionsLod2DV2I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t Lod,
    _Bool Mask) asm("feme.cpu.image.getdimensions.lod.2d.v2i32");

__attribute__((always_inline)) FemeRTv2i32 femeCpuImageGetDimensionsLod2DV2I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t Lod, _Bool Mask) {
  FemeRTv2i32 Zero = {0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || Img.MipLevels == 0)
    return Zero;
  uint32_t ClampedLod = femeRTClampQuerySizeLodMip(&Img, Lod);
  uint32_t Width = Img.Width >> ClampedLod;
  uint32_t Height = Img.Height >> ClampedLod;
  return (FemeRTv2i32){(int32_t)(Width ? Width : 1),
                       (int32_t)(Height ? Height : 1)};
}

// `feme.cpu.image.getdimensions.lod.1d.i32` (roadmap L75): a plain 1D
// image's own `Width` extent at an explicit mip level -- GLSL's
// `textureSize(sampler1D, lod)`, which returns a bare scalar `int` rather
// than any vector width, unlike every other shape
// `femeCpuImageGetDimensionsLod2DV2I32` above's own doc introduces. Same
// `max(1, Width >> ClampedLod)` formula as that function's own `Width`
// lane, just with no `Height` companion (a 1D image has no second
// spatial axis). An inactive lane or an unbound handle reads as `0`,
// mirroring `femeCpuImageGetDimensionsLod2DV2I32`'s own all-zero
// convention.
int32_t femeCpuImageGetDimensionsLod1DI32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t Lod,
    _Bool Mask) asm("feme.cpu.image.getdimensions.lod.1d.i32");

__attribute__((always_inline)) int32_t femeCpuImageGetDimensionsLod1DI32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t Lod, _Bool Mask) {
  if (!Mask)
    return 0;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || Img.MipLevels == 0)
    return 0;
  uint32_t ClampedLod = femeRTClampQuerySizeLodMip(&Img, Lod);
  uint32_t Width = Img.Width >> ClampedLod;
  return (int32_t)(Width ? Width : 1);
}

// `feme.cpu.image.getdimensions.lod.1darray.v2i32` (roadmap L75): an
// arrayed 1D image's own `(Width, ArrayLayers)` extent at an explicit mip
// level -- GLSL's `textureSize(sampler1DArray, lod)`, returning
// `ivec2(width, layers)`. Unlike
// `femeCpuImageGetDimensionsLod2DV2I32`'s own second (`Height`) lane, a
// layer count never shrinks with mip level (only the physical per-layer
// extent does), so `ArrayLayers` is read unclamped/unshifted here. An
// inactive lane or an unbound handle reads as `{0, 0}`, mirroring
// `femeCpuImageGetDimensionsLod2DV2I32`'s own identical convention.
FemeRTv2i32 femeCpuImageGetDimensionsLod1DArrayV2I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t Lod,
    _Bool Mask) asm("feme.cpu.image.getdimensions.lod.1darray.v2i32");

__attribute__((always_inline)) FemeRTv2i32
femeCpuImageGetDimensionsLod1DArrayV2I32(const FemeRTImageDescriptor *ImageHeap,
                                         uint32_t ImageHeapCount,
                                         uint32_t ImageIndex, int32_t Lod,
                                         _Bool Mask) {
  FemeRTv2i32 Zero = {0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || Img.MipLevels == 0)
    return Zero;
  uint32_t ClampedLod = femeRTClampQuerySizeLodMip(&Img, Lod);
  uint32_t Width = Img.Width >> ClampedLod;
  return (FemeRTv2i32){(int32_t)(Width ? Width : 1), (int32_t)Img.ArrayLayers};
}

// `feme.cpu.image.getdimensions.lod.2darray.v3i32` (roadmap L75): an
// arrayed 2D image's own `(Width, Height, ArrayLayers)` extent at an
// explicit mip level -- GLSL's `textureSize(sampler2DArray, lod)`,
// returning `ivec3(width, height, layers)`. Like
// `femeCpuImageGetDimensionsLod1DArrayV2I32` above, only the first two
// lanes shrink with `Lod`; the third (`ArrayLayers`) does not. An
// inactive lane or an unbound handle reads as `{0, 0, 0}`, mirroring
// `femeCpuImageGetDimensionsLod2DV2I32`'s own identical convention.
FemeRTv3i32 femeCpuImageGetDimensionsLod2DArrayV3I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t Lod,
    _Bool Mask) asm("feme.cpu.image.getdimensions.lod.2darray.v3i32");

__attribute__((always_inline)) FemeRTv3i32
femeCpuImageGetDimensionsLod2DArrayV3I32(const FemeRTImageDescriptor *ImageHeap,
                                         uint32_t ImageHeapCount,
                                         uint32_t ImageIndex, int32_t Lod,
                                         _Bool Mask) {
  FemeRTv3i32 Zero = {0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || Img.MipLevels == 0)
    return Zero;
  uint32_t ClampedLod = femeRTClampQuerySizeLodMip(&Img, Lod);
  uint32_t Width = Img.Width >> ClampedLod;
  uint32_t Height = Img.Height >> ClampedLod;
  return (FemeRTv3i32){(int32_t)(Width ? Width : 1),
                       (int32_t)(Height ? Height : 1),
                       (int32_t)Img.ArrayLayers};
}

// `feme.cpu.image.getdimensions.lod.3d.v3i32` (roadmap L75): a plain 3D
// (volume) image's own `(Width, Height, Depth)` extent at an explicit mip
// level -- GLSL's `textureSize(sampler3D, lod)`, returning `ivec3(width,
// height, depth)`. Unlike `femeCpuImageGetDimensionsLod2DArrayV3I32`'s own
// third (`ArrayLayers`) lane, a volume texture's `Depth` genuinely is a
// mip-chain dimension in its own right (SPIR-V/Vulkan halve a 3D image's
// depth alongside its width/height at each mip level, unlike an array's
// layer count, which never changes), so all three lanes shrink with
// `Lod` here. An inactive lane or an unbound handle reads as
// `{0, 0, 0}`, mirroring `femeCpuImageGetDimensionsLod2DV2I32`'s own
// identical convention.
FemeRTv3i32 femeCpuImageGetDimensionsLod3DV3I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t Lod,
    _Bool Mask) asm("feme.cpu.image.getdimensions.lod.3d.v3i32");

__attribute__((always_inline)) FemeRTv3i32 femeCpuImageGetDimensionsLod3DV3I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t Lod, _Bool Mask) {
  FemeRTv3i32 Zero = {0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || Img.MipLevels == 0)
    return Zero;
  uint32_t ClampedLod = femeRTClampQuerySizeLodMip(&Img, Lod);
  uint32_t Width = Img.Width >> ClampedLod;
  uint32_t Height = Img.Height >> ClampedLod;
  uint32_t Depth = Img.Depth >> ClampedLod;
  return (FemeRTv3i32){(int32_t)(Width ? Width : 1),
                       (int32_t)(Height ? Height : 1),
                       (int32_t)(Depth ? Depth : 1)};
}

// `feme.cpu.image.getdimensions.lod.cubearray.v3i32` (roadmap L75): a
// cube-array image's own `(Width, Height, NumCubeArrayElements)` extent
// at an explicit mip level -- GLSL's `textureSize(samplerCubeArray,
// lod)`, returning `ivec3(width, height, numCubeArrayElements)`, where
// the third component is the *element* count, i.e. `ArrayLayers / 6`,
// not the raw face-inclusive layer count
// `FemeRTImageDescriptor::ArrayLayers` itself tracks
// (`CommandBuffer.cpp`'s own `materializeImageDescriptor` treats a
// cube(array) view as a plain view-level convention over consecutive
// array layers, so `ArrayLayers` here is always a multiple of 6 -- one
// set of 6 consecutive faces per cube-array element). Otherwise
// identical in shape/formula to
// `femeCpuImageGetDimensionsLod2DArrayV3I32` (first two lanes shrink with
// `Lod`, third does not). An inactive lane or an unbound handle reads as
// `{0, 0, 0}`, mirroring `femeCpuImageGetDimensionsLod2DV2I32`'s own
// identical convention.
FemeRTv3i32 femeCpuImageGetDimensionsLodCubeArrayV3I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t Lod,
    _Bool Mask) asm("feme.cpu.image.getdimensions.lod.cubearray.v3i32");

__attribute__((always_inline)) FemeRTv3i32
femeCpuImageGetDimensionsLodCubeArrayV3I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t Lod, _Bool Mask) {
  FemeRTv3i32 Zero = {0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || Img.MipLevels == 0)
    return Zero;
  uint32_t ClampedLod = femeRTClampQuerySizeLodMip(&Img, Lod);
  uint32_t Width = Img.Width >> ClampedLod;
  uint32_t Height = Img.Height >> ClampedLod;
  return (FemeRTv3i32){(int32_t)(Width ? Width : 1),
                       (int32_t)(Height ? Height : 1),
                       (int32_t)(Img.ArrayLayers / 6)};
}

// `feme.cpu.image.querylevels.i32` (roadmap L72(d)): an image's own total
// mip-level count -- GLSL's `textureQueryLevels(sampler)` (SPIR-V
// `OpImageQueryLevels`). Unlike every other `feme.cpu.image.*` call above
// this needs neither a `Mask` (no per-invocation side effect to guard
// against) nor an explicit mip level/coordinate of its own -- only the
// bound image's own identity. An unbound (`!Img.Data`) handle reads as
// `0`, mirroring `femeCpuImageGetDimensions2DV2I32`'s own all-zero
// convention for the same condition.
int32_t femeCpuImageQueryLevelsI32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex) asm("feme.cpu.image.querylevels.i32");

__attribute__((always_inline)) int32_t
femeCpuImageQueryLevelsI32(const FemeRTImageDescriptor *ImageHeap,
                           uint32_t ImageHeapCount, uint32_t ImageIndex) {
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data)
    return 0;
  return (int32_t)Img.MipLevels;
}

// `feme.cpu.image.querysamples.i32` (roadmap L73): a multisampled image's
// own sample count -- GLSL's `textureSamples(sampler2DMS)` (SPIR-V
// `OpImageQuerySamples`). Structurally identical to
// `femeCpuImageQueryLevelsI32` immediately above (no `Mask`, no explicit
// mip level/coordinate), just reading `SampleCount` instead of
// `MipLevels`. An unbound (`!Img.Data`) handle reads as `0`, mirroring
// `femeCpuImageQueryLevelsI32`'s own identical convention.
int32_t femeCpuImageQuerySamplesI32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex) asm("feme.cpu.image.querysamples.i32");

__attribute__((always_inline)) int32_t
femeCpuImageQuerySamplesI32(const FemeRTImageDescriptor *ImageHeap,
                            uint32_t ImageHeapCount, uint32_t ImageIndex) {
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data)
    return 0;
  return (int32_t)Img.SampleCount;
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
// blends an ordinary color sample's two levels. (Roadmap L48) `Layer`
// generalizes what used to be a hard-coded 0, so `Array2D`/`Cube`/
// `CubeArray`'s own new depth-comparison entry points below can reuse this
// same per-level body, mirroring how `femeRTSampleFiltered2D`'s own
// `Layer` parameter is shared across every ordinary-sample shape.
// (Roadmap L50d) `OffsetX`/`OffsetY` generalize what used to be a
// hard-coded zero, mirroring `femeRTSampleFiltered2D`'s own
// `OffsetX`/`OffsetY` parameters (roadmap L26) for the same
// `ConstOffset` image operand, now threaded through for a depth-comparison
// sample against `Plain2D`/`Array2D` (SPIR-V forbids `ConstOffset` against
// `Cube`/`CubeArray`, so their own callers below still always pass zero).
__attribute__((always_inline)) static float
femeRTSampleCmp2DAtLevel(const FemeRTImageDescriptor *Img,
                         const FemeRTSamplerDescriptor *Samp, float U, float V,
                         uint32_t Layer, uint32_t Level, float Dref,
                         _Bool UseLinear, int32_t OffsetX, int32_t OffsetY) {
  _Bool IsFixedPointDepth = femeRTIsFixedPointDepthFormat(Img->Format);
  if (!UseLinear) { // Point (nearest).
    uint32_t LevelWidth = femeRTMipExtent(Img->Width, Level);
    uint32_t LevelHeight = femeRTMipExtent(Img->Height, Level);
    int32_t X = (int32_t)__builtin_floorf(U * (float)LevelWidth) + OffsetX;
    int32_t Y = (int32_t)__builtin_floorf(V * (float)LevelHeight) + OffsetY;
    _Bool BorderX = 0, BorderY = 0;
    int32_t AddrX = femeRTApplyAddressMode(X, (int32_t)LevelWidth,
                                           Samp->AddressU, &BorderX);
    int32_t AddrY = femeRTApplyAddressMode(Y, (int32_t)LevelHeight,
                                           Samp->AddressV, &BorderY);
    FemeRTv4f32 T =
        femeRTFetchTexel2D(Img, Level, Layer, AddrX, AddrY,
                           /*Sample=*/0, BorderX || BorderY, Samp->BorderColor,
                           /*ApplySwizzle=*/0);
    return femeRTApplyCompare(Samp->CompareFunc, Dref, T[0], IsFixedPointDepth);
  }

  FemeRTBilinearSupport S =
      femeRTComputeBilinearSupport(Img, U, V, Samp, Level, OffsetX, OffsetY);
  FemeRTv4f32 T00 =
      femeRTFetchTexel2D(Img, Level, Layer, S.X0, S.Y0, /*Sample=*/0,
                         S.BorderX0 || S.BorderY0, Samp->BorderColor,
                         /*ApplySwizzle=*/0);
  FemeRTv4f32 T10 =
      femeRTFetchTexel2D(Img, Level, Layer, S.X1, S.Y0, /*Sample=*/0,
                         S.BorderX1 || S.BorderY0, Samp->BorderColor,
                         /*ApplySwizzle=*/0);
  FemeRTv4f32 T01 =
      femeRTFetchTexel2D(Img, Level, Layer, S.X0, S.Y1, /*Sample=*/0,
                         S.BorderX0 || S.BorderY1, Samp->BorderColor,
                         /*ApplySwizzle=*/0);
  FemeRTv4f32 T11 =
      femeRTFetchTexel2D(Img, Level, Layer, S.X1, S.Y1, /*Sample=*/0,
                         S.BorderX1 || S.BorderY1, Samp->BorderColor,
                         /*ApplySwizzle=*/0);
  float C00 = femeRTApplyCompare(Samp->CompareFunc, Dref, T00[0], IsFixedPointDepth);
  float C10 = femeRTApplyCompare(Samp->CompareFunc, Dref, T10[0], IsFixedPointDepth);
  float C01 = femeRTApplyCompare(Samp->CompareFunc, Dref, T01[0], IsFixedPointDepth);
  float C11 = femeRTApplyCompare(Samp->CompareFunc, Dref, T11[0], IsFixedPointDepth);
  float Top = C00 + (C10 - C00) * S.Wx;
  float Bottom = C01 + (C11 - C01) * S.Wx;
  return Top + (Bottom - Top) * S.Wy;
}

// (Roadmap L52(c)) `MinLodClamp` generalizes what used to be a hard-coded
// `-infinity`, mirroring `femeCpuImageSample2DV4F32`'s own `MinLodClamp`
// parameter (roadmap L26) for the same `MinLod` image operand, now
// threaded through for a depth-comparison sample too (SPIR-V's own
// `MinLod` image operand is legal against a `samplecmp`/
// `samplecmplevelzero`/`samplecmp_clamp`/`samplecmpgrad{,_clamp}` alike,
// unlike `ConstOffset`, which forbids `Cube`/`CubeArray`). (Roadmap
// L52(b)) `Bias` likewise generalizes what used to be a hard-coded
// `0.0f`, threading SPIR-V's own `Bias` image operand
// (`spv_resource_samplecmpbias{,_clamp}`) through to the same
// `femeRTComputeClampedLod` parameter an ordinary sample's own bias
// already uses; the non-bias intrinsic forms still pass zero. (Roadmap
// L66(c)) `DUdX`/`DUdY`/`DVdX`/`DVdY` generalize what used to always be
// implicitly zero: for an implicit-LOD call (`UseExplicitLod` false),
// these now drive a real `femeRTPlanImplicitLod` derivative-based LOD
// calculation instead of the always-level-0 result a zero-derivative
// input would still give (mathematically identical for a caller with no
// real derivatives to give -- `femeRTPlanImplicitLod` computes `Pmax <=
// 0` -> `Lod = 0.0`, provably the same value `femeRTComputeClampedLod`
// alone always produced here before this row). `samplecmplevelzero`'s own
// `UseExplicitLod = 1` case is unaffected: it still calls
// `femeRTComputeClampedLod` directly, exactly as before.
float femeCpuImageSampleCmp2DF32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float DUdX,
    float DUdY, float DVdX, float DVdY, float Lod, _Bool UseExplicitLod,
    float Dref, float Bias, int32_t OffsetX, int32_t OffsetY, float MinLodClamp,
    _Bool Mask) asm("feme.cpu.image.samplecmp.2d.f32");

__attribute__((always_inline)) float femeCpuImageSampleCmp2DF32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float DUdX,
    float DUdY, float DVdX, float DVdY, float Lod, _Bool UseExplicitLod,
    float Dref, float Bias, int32_t OffsetX, int32_t OffsetY, float MinLodClamp,
    _Bool Mask) {
  if (!Mask)
    return 0.0f;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return 0.0f;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  float ClampedLod;
  if (UseExplicitLod) {
    ClampedLod = femeRTComputeClampedLod(Lod, UseExplicitLod, &Samp,
                                         /*InstructionMinLod=*/MinLodClamp,
                                         /*InstructionBias=*/Bias);
  } else {
    FemeRTImplicitLodPlan Plan =
        femeRTPlanImplicitLod(&Img, &Samp, DUdX, DUdY, DVdX, DVdY,
                              /*InstructionMinLod=*/MinLodClamp,
                              /*InstructionBias=*/Bias);
    ClampedLod = Plan.ClampedLod;
  }
  _Bool UseLinear = femeRTUseLinearFilter(ClampedLod, &Samp);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(&Img, ClampedLod);
  _Bool Trilinear = Samp.MipFilter == 1 && MipPlan.Level0 != MipPlan.Level1;
  uint32_t Level0 = Trilinear ? MipPlan.Level0 : femeRTNearestMipLevel(MipPlan);
  float Lo =
      femeRTSampleCmp2DAtLevel(&Img, &Samp, U, V, /*Layer=*/0, Level0, Dref,
                               UseLinear, OffsetX, OffsetY);
  if (!Trilinear)
    return Lo;
  float Hi = femeRTSampleCmp2DAtLevel(&Img, &Samp, U, V, /*Layer=*/0,
                                      MipPlan.Level1, Dref, UseLinear, OffsetX,
                                      OffsetY);
  return Lo + (Hi - Lo) * MipPlan.Frac;
}

// `feme.cpu.image.gathercmp.2d.v4f32` (roadmap L7d): `Plain2D`
// depth-comparison gather -- SPIR-V's `OpImageDrefGather`, HLSL's
// `Texture2D::GatherCmp()`. Unlike `femeCpuImageSampleCmp2DF32`'s own
// single bilinearly-*filtered* depth-comparison result, this returns a
// full `<4 x float>`: one 0/1 comparison result per each of the four
// texels the identical bilinear "footprint" `femeRTComputeBilinearSupport`
// already computes for an ordinary filtered sample at the same
// coordinate would blend between (SPIR-V/Vulkan's own fixed
// gather-footprint convention: a gather instruction always reads exactly
// the four texels a bilinear filter at the same coordinate would use,
// never actually blending them). A gather instruction always operates at
// mip level 0 -- unlike `femeRTSampleCmp2DAtLevel`'s own `Level`
// parameter, there is no explicit-LOD/implicit-LOD choice to make here at
// all, per the SPIR-V spec.
//
// The four components are packed in SPIR-V/Vulkan's own fixed gather
// result ordering (confirmed against a real `offload-test-suite` case's
// own documented expected values, `Vk.SampledTexture2D.GatherCmp.test.
// yaml`): result[0] is the texel at the *lower* X, *upper* Y corner of the
// footprint (`(X0, Y1)`); result[1] is `(X1, Y1)`; result[2] is `(X1,
// Y0)`; result[3] is `(X0, Y0)` -- i.e. counter-clockwise starting from
// the "upper-left" corner, matching HLSL's own documented `Gather`/
// `GatherCmp` component ordering.
FemeRTv4f32 femeCpuImageGatherCmp2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float Dref,
    int32_t OffsetX, int32_t OffsetY,
    _Bool Mask) asm("feme.cpu.image.gathercmp.2d.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageGatherCmp2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V, float Dref,
    int32_t OffsetX, int32_t OffsetY, _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  _Bool IsFixedPointDepth = femeRTIsFixedPointDepthFormat(Img.Format);
  FemeRTBilinearSupport S = femeRTComputeBilinearSupport(
      &Img, U, V, &Samp, /*Level=*/0, OffsetX, OffsetY);
  FemeRTv4f32 T00 =
      femeRTFetchTexel2D(&Img, /*Level=*/0, /*Layer=*/0, S.X0, S.Y0,
                        /*Sample=*/0, S.BorderX0 || S.BorderY0,
                        Samp.BorderColor, /*ApplySwizzle=*/0);
  FemeRTv4f32 T10 =
      femeRTFetchTexel2D(&Img, /*Level=*/0, /*Layer=*/0, S.X1, S.Y0,
                        /*Sample=*/0, S.BorderX1 || S.BorderY0,
                        Samp.BorderColor, /*ApplySwizzle=*/0);
  FemeRTv4f32 T01 =
      femeRTFetchTexel2D(&Img, /*Level=*/0, /*Layer=*/0, S.X0, S.Y1,
                        /*Sample=*/0, S.BorderX0 || S.BorderY1,
                        Samp.BorderColor, /*ApplySwizzle=*/0);
  FemeRTv4f32 T11 =
      femeRTFetchTexel2D(&Img, /*Level=*/0, /*Layer=*/0, S.X1, S.Y1,
                        /*Sample=*/0, S.BorderX1 || S.BorderY1,
                        Samp.BorderColor, /*ApplySwizzle=*/0);
  FemeRTv4f32 Result;
  Result[0] = femeRTApplyCompare(Samp.CompareFunc, Dref, T01[0], IsFixedPointDepth);
  Result[1] = femeRTApplyCompare(Samp.CompareFunc, Dref, T11[0], IsFixedPointDepth);
  Result[2] = femeRTApplyCompare(Samp.CompareFunc, Dref, T10[0], IsFixedPointDepth);
  Result[3] = femeRTApplyCompare(Samp.CompareFunc, Dref, T00[0], IsFixedPointDepth);
  return Result;
}

// `feme.cpu.image.gather.2d.v4f32` (roadmap L7g): `Plain2D`
// non-depth-comparison gather -- SPIR-V's `OpImageGather`, HLSL's
// `Texture2D::Gather{,Red,Green,Blue,Alpha}()`. Structurally identical to
// `femeCpuImageGatherCmp2DV4F32` above (same fixed bilinear "footprint"
// via `femeRTComputeBilinearSupport`, same fixed result ordering
// (`result[0]` is `(X0, Y1)`; `result[1]` is `(X1, Y1)`; `result[2]` is
// `(X1, Y0)`; `result[3]` is `(X0, Y0)`), same mip-level-0-only
// restriction), but each result component is one of the four sampled
// texel's own `Component` channel (0=R, 1=G, 2=B, 3=A -- `Component` is
// clamped into `[0, 3]` for an out-of-spec value, mirroring
// `femeRTFetchTexel2D`'s own out-of-range-reads-as-border/zero philosophy
// rather than reading undefined memory), never a depth comparison.
FemeRTv4f32 femeCpuImageGather2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V,
    int32_t Component, int32_t OffsetX, int32_t OffsetY,
    _Bool Mask) asm("feme.cpu.image.gather.2d.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageGather2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V,
    int32_t Component, int32_t OffsetX, int32_t OffsetY, _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  uint32_t Chan = (uint32_t)Component > 3u ? 3u : (uint32_t)Component;
  FemeRTBilinearSupport S = femeRTComputeBilinearSupport(
      &Img, U, V, &Samp, /*Level=*/0, OffsetX, OffsetY);
  // Roadmap L125(g): each gathered neighbor now goes through the same
  // per-texel component-substitution + swizzle pipeline a plain sample
  // would (`ApplySwizzle=1`), per the Vulkan spec's "Texel Gathering"
  // section ("Each texel is then converted to an RGBA value according to
  // component substitution and then swizzled") -- `Chan` (SPIR-V's
  // `Component` operand) selects a channel of that already-swizzled RGBA
  // result, not the raw unswizzled texel.
  FemeRTv4f32 T00 =
      femeRTFetchTexel2D(&Img, /*Level=*/0, /*Layer=*/0, S.X0, S.Y0,
                        /*Sample=*/0, S.BorderX0 || S.BorderY0,
                        Samp.BorderColor, /*ApplySwizzle=*/1);
  FemeRTv4f32 T10 =
      femeRTFetchTexel2D(&Img, /*Level=*/0, /*Layer=*/0, S.X1, S.Y0,
                        /*Sample=*/0, S.BorderX1 || S.BorderY0,
                        Samp.BorderColor, /*ApplySwizzle=*/1);
  FemeRTv4f32 T01 =
      femeRTFetchTexel2D(&Img, /*Level=*/0, /*Layer=*/0, S.X0, S.Y1,
                        /*Sample=*/0, S.BorderX0 || S.BorderY1,
                        Samp.BorderColor, /*ApplySwizzle=*/1);
  FemeRTv4f32 T11 =
      femeRTFetchTexel2D(&Img, /*Level=*/0, /*Layer=*/0, S.X1, S.Y1,
                        /*Sample=*/0, S.BorderX1 || S.BorderY1,
                        Samp.BorderColor, /*ApplySwizzle=*/1);
  FemeRTv4f32 Result;
  Result[0] = T01[Chan];
  Result[1] = T11[Chan];
  Result[2] = T10[Chan];
  Result[3] = T00[Chan];
  return Result;
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
                            /*UseBorder=*/0, NoBorder, /*ApplySwizzle=*/0);
}

// `feme.cpu.image.load.2d.v4i32` (roadmap E26): the integer-format
// counterpart of `feme.cpu.image.load.2d.v4f32` above -- same bounds
// checking and no-sampler/no-filtering semantics, decoded through
// `femeRTUnpackImageTexelI32`'s `_UINT`/`_SINT` table instead of
// `femeRTUnpackImageTexel`'s float one. Takes a `Sample` operand (roadmap
// H19g), mirroring `feme.cpu.image.load.2d.v4f32`'s own.
FemeRTv4i32
femeCpuImageLoad2DV4I32(const FemeRTImageDescriptor *ImageHeap,
                        uint32_t ImageHeapCount, uint32_t ImageIndex, int32_t X,
                        int32_t Y, uint32_t Mip, uint32_t Sample,
                        _Bool Mask) asm("feme.cpu.image.load.2d.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuImageLoad2DV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, uint32_t Mip, uint32_t Sample,
    _Bool Mask) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data)
    return Zero;
  if (X < 0 || Y < 0 || (uint32_t)X >= Img.Width || (uint32_t)Y >= Img.Height)
    return Zero;
  return femeRTFetchTexel2DI32(&Img, Mip, /*Layer=*/0, X, Y, Sample,
                               /*ApplySwizzle=*/0);
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

// `feme.cpu.image.atomic.add.2d.i32` (roadmap H8v): `OpAtomicIAdd` against
// a plain, non-arrayed, single-32-bit-scalar storage image (`R32_SINT`/
// `R32_UINT`) -- adds \p Value to the texel at `(X, Y)`, mip level 0,
// array layer 0, returning the pre-op value. Uses a real hardware atomic
// (`__atomic_fetch_add`, sequentially consistent) rather than a plain
// load-modify-store, since `Executor.cpp`'s own tiled dispatch (roadmap
// R33) runs separate shader invocations on separate host threads, exactly
// like a real GPU's own concurrent invocations. An unbound image or an
// out-of-bounds/masked-off access returns 0, mirroring
// `femeCpuImageLoad2DV4I32`'s own unbound-read-as-zero treatment (there is
// no well-defined "old value" to return in either case).
int32_t femeCpuImageAtomicAdd2D(const FemeRTImageDescriptor *ImageHeap,
                                uint32_t ImageHeapCount, uint32_t ImageIndex,
                                int32_t X, int32_t Y, int32_t Value,
                                _Bool Mask) asm("feme.cpu.image.atomic.add.2d.i32");

__attribute__((always_inline)) int32_t femeCpuImageAtomicAdd2D(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  int32_t *Addr = femeRTAtomicTexelAddress2D(&Img, X, Y);
  if (!Addr)
    return 0;
  return __atomic_fetch_add(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.image.atomic.sub.2d.i32` (roadmap H8v): `OpAtomicISub`'s
// counterpart to `feme.cpu.image.atomic.add.2d.i32` above. See its doc for
// the shared bounds/threading rationale.
int32_t femeCpuImageAtomicSub2D(const FemeRTImageDescriptor *ImageHeap,
                                uint32_t ImageHeapCount, uint32_t ImageIndex,
                                int32_t X, int32_t Y, int32_t Value,
                                _Bool Mask) asm("feme.cpu.image.atomic.sub.2d.i32");

__attribute__((always_inline)) int32_t femeCpuImageAtomicSub2D(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  int32_t *Addr = femeRTAtomicTexelAddress2D(&Img, X, Y);
  if (!Addr)
    return 0;
  return __atomic_fetch_sub(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.image.atomic.and.2d.i32` (roadmap H8v): `OpAtomicAnd`'s
// counterpart to `feme.cpu.image.atomic.add.2d.i32` above.
int32_t femeCpuImageAtomicAnd2D(const FemeRTImageDescriptor *ImageHeap,
                                uint32_t ImageHeapCount, uint32_t ImageIndex,
                                int32_t X, int32_t Y, int32_t Value,
                                _Bool Mask) asm("feme.cpu.image.atomic.and.2d.i32");

__attribute__((always_inline)) int32_t femeCpuImageAtomicAnd2D(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  int32_t *Addr = femeRTAtomicTexelAddress2D(&Img, X, Y);
  if (!Addr)
    return 0;
  return __atomic_fetch_and(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.image.atomic.or.2d.i32` (roadmap H8v): `OpAtomicOr`'s
// counterpart to `feme.cpu.image.atomic.add.2d.i32` above.
int32_t femeCpuImageAtomicOr2D(const FemeRTImageDescriptor *ImageHeap,
                               uint32_t ImageHeapCount, uint32_t ImageIndex,
                               int32_t X, int32_t Y, int32_t Value,
                               _Bool Mask) asm("feme.cpu.image.atomic.or.2d.i32");

__attribute__((always_inline)) int32_t femeCpuImageAtomicOr2D(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  int32_t *Addr = femeRTAtomicTexelAddress2D(&Img, X, Y);
  if (!Addr)
    return 0;
  return __atomic_fetch_or(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.image.atomic.xor.2d.i32` (roadmap H8v): `OpAtomicXor`'s
// counterpart to `feme.cpu.image.atomic.add.2d.i32` above.
int32_t femeCpuImageAtomicXor2D(const FemeRTImageDescriptor *ImageHeap,
                                uint32_t ImageHeapCount, uint32_t ImageIndex,
                                int32_t X, int32_t Y, int32_t Value,
                                _Bool Mask) asm("feme.cpu.image.atomic.xor.2d.i32");

__attribute__((always_inline)) int32_t femeCpuImageAtomicXor2D(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  int32_t *Addr = femeRTAtomicTexelAddress2D(&Img, X, Y);
  if (!Addr)
    return 0;
  return __atomic_fetch_xor(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.image.atomic.smax.2d.i32` (roadmap H8v): `OpAtomicSMax`'s
// counterpart to `feme.cpu.image.atomic.add.2d.i32` above -- a signed
// maximum, operating on the texel address's own `int32_t *` type directly.
int32_t
femeCpuImageAtomicSMax2D(const FemeRTImageDescriptor *ImageHeap,
                         uint32_t ImageHeapCount, uint32_t ImageIndex,
                         int32_t X, int32_t Y, int32_t Value,
                         _Bool Mask) asm("feme.cpu.image.atomic.smax.2d.i32");

__attribute__((always_inline)) int32_t femeCpuImageAtomicSMax2D(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  int32_t *Addr = femeRTAtomicTexelAddress2D(&Img, X, Y);
  if (!Addr)
    return 0;
  return __atomic_fetch_max(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.image.atomic.smin.2d.i32` (roadmap H8v): `OpAtomicSMin`'s
// counterpart to `feme.cpu.image.atomic.add.2d.i32` above -- a signed
// minimum, mirroring `femeCpuImageAtomicSMax2D`'s own signed-comparison
// treatment.
int32_t
femeCpuImageAtomicSMin2D(const FemeRTImageDescriptor *ImageHeap,
                         uint32_t ImageHeapCount, uint32_t ImageIndex,
                         int32_t X, int32_t Y, int32_t Value,
                         _Bool Mask) asm("feme.cpu.image.atomic.smin.2d.i32");

__attribute__((always_inline)) int32_t femeCpuImageAtomicSMin2D(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  int32_t *Addr = femeRTAtomicTexelAddress2D(&Img, X, Y);
  if (!Addr)
    return 0;
  return __atomic_fetch_min(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.image.atomic.umax.2d.i32` (roadmap H8v): `OpAtomicUMax`'s
// counterpart to `feme.cpu.image.atomic.add.2d.i32` above -- an *unsigned*
// maximum, requiring a cast to `uint32_t *` so `__atomic_fetch_max`
// compares \p Value bitwise-reinterpreted as unsigned rather than signed
// (mirroring the signed/unsigned split every other `femeRT*` integer
// helper already makes at the `_SINT`/`_UINT` format boundary).
int32_t
femeCpuImageAtomicUMax2D(const FemeRTImageDescriptor *ImageHeap,
                         uint32_t ImageHeapCount, uint32_t ImageIndex,
                         int32_t X, int32_t Y, int32_t Value,
                         _Bool Mask) asm("feme.cpu.image.atomic.umax.2d.i32");

__attribute__((always_inline)) int32_t femeCpuImageAtomicUMax2D(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  int32_t *Addr = femeRTAtomicTexelAddress2D(&Img, X, Y);
  if (!Addr)
    return 0;
  uint32_t UValue = (uint32_t)Value;
  return (int32_t)__atomic_fetch_max((uint32_t *)Addr, UValue,
                                     __ATOMIC_SEQ_CST);
}

// `feme.cpu.image.atomic.umin.2d.i32` (roadmap H8v): `OpAtomicUMin`'s
// counterpart to `feme.cpu.image.atomic.add.2d.i32` above -- an unsigned
// minimum, mirroring `femeCpuImageAtomicUMax2D`'s own unsigned-comparison
// treatment.
int32_t
femeCpuImageAtomicUMin2D(const FemeRTImageDescriptor *ImageHeap,
                         uint32_t ImageHeapCount, uint32_t ImageIndex,
                         int32_t X, int32_t Y, int32_t Value,
                         _Bool Mask) asm("feme.cpu.image.atomic.umin.2d.i32");

__attribute__((always_inline)) int32_t femeCpuImageAtomicUMin2D(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  int32_t *Addr = femeRTAtomicTexelAddress2D(&Img, X, Y);
  if (!Addr)
    return 0;
  uint32_t UValue = (uint32_t)Value;
  return (int32_t)__atomic_fetch_min((uint32_t *)Addr, UValue,
                                     __ATOMIC_SEQ_CST);
}

// `feme.cpu.image.atomic.exchange.2d.i32` (roadmap H8v):
// `OpAtomicExchange`'s counterpart to `feme.cpu.image.atomic.add.2d.i32`
// above -- an unconditional swap, returning the pre-op value.
int32_t femeCpuImageAtomicExchange2D(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Value,
    _Bool Mask) asm("feme.cpu.image.atomic.exchange.2d.i32");

__attribute__((always_inline)) int32_t femeCpuImageAtomicExchange2D(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  int32_t *Addr = femeRTAtomicTexelAddress2D(&Img, X, Y);
  if (!Addr)
    return 0;
  return __atomic_exchange_n(Addr, Value, __ATOMIC_SEQ_CST);
}

// `feme.cpu.image.atomic.compare_exchange.2d.i32` (roadmap H8v):
// `OpAtomicCompareExchange`'s counterpart to
// `feme.cpu.image.atomic.add.2d.i32` above -- the texel is only replaced
// with \p Value when it currently equals \p Comparator, but the value
// returned is always the pre-op value in memory either way, matching
// `OpAtomicCompareExchange`'s own result semantics (see
// `__atomic_compare_exchange_n`'s own identical "expected updated in
// place on failure" behavior, which is exactly what this needs).
int32_t femeCpuImageAtomicCompareExchange2D(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Comparator,
    int32_t Value,
    _Bool Mask) asm("feme.cpu.image.atomic.compare_exchange.2d.i32");

__attribute__((always_inline)) int32_t femeCpuImageAtomicCompareExchange2D(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Comparator,
    int32_t Value, _Bool Mask) {
  if (!Mask)
    return 0;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  int32_t *Addr = femeRTAtomicTexelAddress2D(&Img, X, Y);
  if (!Addr)
    return 0;
  int32_t Expected = Comparator;
  __atomic_compare_exchange_n(Addr, &Expected, Value, /*weak=*/0,
                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  return Expected;
}

// `feme.cpu.image.store.2darray.v4f32` (roadmap H19b): the arrayed
// counterpart of `feme.cpu.image.store.2d.v4f32` above, adding an integer
// `Layer` operand -- SPIR-V's `OpImageWrite` array-layer coordinate is
// always an integer, like `OpImageFetch`'s (see
// `femeCpuImageLoad2DArrayV4F32`'s own identical comment).
void femeCpuImageStore2DArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Layer,
    FemeRTv4f32 Texel, _Bool Mask) asm("feme.cpu.image.store.2darray.v4f32");

__attribute__((always_inline)) void femeCpuImageStore2DArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Layer,
    FemeRTv4f32 Texel, _Bool Mask) {
  if (!Mask || Layer < 0)
    return;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  femeRTStoreTexel2DArray(&Img, X, Y, (uint32_t)Layer, Texel);
}

// `feme.cpu.image.store.2darray.v4i32` (roadmap H19b): the integer-format
// counterpart of `feme.cpu.image.store.2darray.v4f32` above.
void femeCpuImageStore2DArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Layer,
    FemeRTv4i32 Texel, _Bool Mask) asm("feme.cpu.image.store.2darray.v4i32");

__attribute__((always_inline)) void femeCpuImageStore2DArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Layer,
    FemeRTv4i32 Texel, _Bool Mask) {
  if (!Mask || Layer < 0)
    return;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  femeRTStoreTexel2DArrayI32(&Img, X, Y, (uint32_t)Layer, Texel);
}

// `feme.cpu.image.store.2dms.v4f32` (roadmap H19g): the multisampled
// counterpart of `feme.cpu.image.store.2d.v4f32` above, adding an integer
// `Sample` operand -- SPIR-V's `OpImageWrite` sample-index coordinate is
// always an integer, like the array-layer coordinate
// `feme.cpu.image.store.2darray.v4f32` above takes.
void femeCpuImageStore2DMSV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, uint32_t Sample,
    FemeRTv4f32 Texel, _Bool Mask) asm("feme.cpu.image.store.2dms.v4f32");

__attribute__((always_inline)) void femeCpuImageStore2DMSV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, uint32_t Sample,
    FemeRTv4f32 Texel, _Bool Mask) {
  if (!Mask)
    return;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  femeRTStoreTexel2DMS(&Img, X, Y, Sample, Texel);
}

// `feme.cpu.image.store.2dms.v4i32` (roadmap H19g): the integer-format
// counterpart of `feme.cpu.image.store.2dms.v4f32` above.
void femeCpuImageStore2DMSV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, uint32_t Sample,
    FemeRTv4i32 Texel, _Bool Mask) asm("feme.cpu.image.store.2dms.v4i32");

__attribute__((always_inline)) void femeCpuImageStore2DMSV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, uint32_t Sample,
    FemeRTv4i32 Texel, _Bool Mask) {
  if (!Mask)
    return;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  femeRTStoreTexel2DMSI32(&Img, X, Y, Sample, Texel);
}

// `feme.cpu.image.store.2darrayms.v4f32` (roadmap H19m): the
// arrayed-*and*-multisampled counterpart of `feme.cpu.image.store.2d.v4f32`
// above, combining `feme.cpu.image.store.2darray.v4f32`'s own integer
// `Layer` operand and `feme.cpu.image.store.2dms.v4f32`'s own integer
// `Sample` operand -- a new, dedicated entry point rather than a widened
// `Store2DArray`/`Store2DMS`, since neither existing one had a spare
// operand slot for the other axis (see `ImageCalls.h`'s own
// `Store2DArrayMS` comment).
void femeCpuImageStore2DArrayMSV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Layer,
    uint32_t Sample, FemeRTv4f32 Texel,
    _Bool Mask) asm("feme.cpu.image.store.2darrayms.v4f32");

__attribute__((always_inline)) void femeCpuImageStore2DArrayMSV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Layer,
    uint32_t Sample, FemeRTv4f32 Texel, _Bool Mask) {
  if (!Mask || Layer < 0)
    return;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  femeRTStoreTexel2DArrayMS(&Img, X, Y, (uint32_t)Layer, Sample, Texel);
}

// `feme.cpu.image.store.2darrayms.v4i32` (roadmap H19m): the
// integer-format counterpart of `feme.cpu.image.store.2darrayms.v4f32`
// above.
void femeCpuImageStore2DArrayMSV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Layer,
    uint32_t Sample, FemeRTv4i32 Texel,
    _Bool Mask) asm("feme.cpu.image.store.2darrayms.v4i32");

__attribute__((always_inline)) void femeCpuImageStore2DArrayMSV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Layer,
    uint32_t Sample, FemeRTv4i32 Texel, _Bool Mask) {
  if (!Mask || Layer < 0)
    return;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  femeRTStoreTexel2DArrayMSI32(&Img, X, Y, (uint32_t)Layer, Sample, Texel);
}

// `feme.cpu.image.load.1d.v4f32` (roadmap H19c): the plain-1D counterpart
// of `feme.cpu.image.load.2d.v4f32` above, taking a single integer `X`
// texel coordinate instead of an `(X, Y)` pair -- see
// `femeRTFetchTexel1D`'s own comment for why `Y == 0` needs no new
// addressing math.
FemeRTv4f32
femeCpuImageLoad1DV4F32(const FemeRTImageDescriptor *ImageHeap,
                        uint32_t ImageHeapCount, uint32_t ImageIndex,
                        int32_t X, uint32_t Mip, uint32_t Sample,
                        _Bool Mask) asm("feme.cpu.image.load.1d.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageLoad1DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, uint32_t Mip, uint32_t Sample,
    _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data)
    return Zero;
  if (X < 0 || (uint32_t)X >= Img.Width)
    return Zero;
  static const float NoBorder[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  return femeRTFetchTexel1D(&Img, Mip, X, Sample, /*UseBorder=*/0, NoBorder,
                            /*ApplySwizzle=*/0);
}

// `feme.cpu.image.load.1d.v4i32` (roadmap H19c): the integer-format
// counterpart of `feme.cpu.image.load.1d.v4f32` above.
FemeRTv4i32
femeCpuImageLoad1DV4I32(const FemeRTImageDescriptor *ImageHeap,
                        uint32_t ImageHeapCount, uint32_t ImageIndex,
                        int32_t X, uint32_t Mip,
                        _Bool Mask) asm("feme.cpu.image.load.1d.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuImageLoad1DV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, uint32_t Mip, _Bool Mask) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data)
    return Zero;
  if (X < 0 || (uint32_t)X >= Img.Width)
    return Zero;
  return femeRTFetchTexel1DI32(&Img, Mip, X, /*ApplySwizzle=*/0);
}

// `feme.cpu.image.store.1d.v4f32` (roadmap H19c): writes one texel of a 1D
// storage image at integer coordinate `X`, mip level 0 (see
// `femeRTStoreTexel1D`'s own scope comment) -- Vulkan's `OpImageWrite`.
void femeCpuImageStore1DV4F32(const FemeRTImageDescriptor *ImageHeap,
                              uint32_t ImageHeapCount, uint32_t ImageIndex,
                              int32_t X, FemeRTv4f32 Texel,
                              _Bool Mask) asm("feme.cpu.image.store.1d.v4f32");

__attribute__((always_inline)) void femeCpuImageStore1DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, FemeRTv4f32 Texel, _Bool Mask) {
  if (!Mask)
    return;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  femeRTStoreTexel1D(&Img, X, Texel);
}

// `feme.cpu.image.store.1d.v4i32` (roadmap H19c): the integer-format
// counterpart of `feme.cpu.image.store.1d.v4f32` above.
void femeCpuImageStore1DV4I32(const FemeRTImageDescriptor *ImageHeap,
                              uint32_t ImageHeapCount, uint32_t ImageIndex,
                              int32_t X, FemeRTv4i32 Texel,
                              _Bool Mask) asm("feme.cpu.image.store.1d.v4i32");

__attribute__((always_inline)) void femeCpuImageStore1DV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, FemeRTv4i32 Texel, _Bool Mask) {
  if (!Mask)
    return;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  femeRTStoreTexel1DI32(&Img, X, Texel);
}

// `feme.cpu.image.load.1darray.v4f32` (roadmap H19e): the arrayed-1D
// counterpart of `feme.cpu.image.load.1d.v4f32` above, adding an integer
// `Layer` operand -- mirroring exactly how `feme.cpu.image.load.2darray.
// v4f32` extends `feme.cpu.image.load.2d.v4f32`.
FemeRTv4f32 femeCpuImageLoad1DArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Layer, uint32_t Mip,
    uint32_t Sample, _Bool Mask) asm("feme.cpu.image.load.1darray.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageLoad1DArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Layer, uint32_t Mip,
    uint32_t Sample, _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data)
    return Zero;
  if (X < 0 || Layer < 0 || (uint32_t)X >= Img.Width ||
      (uint32_t)Layer >= Img.ArrayLayers)
    return Zero;
  static const float NoBorder[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  return femeRTFetchTexel1DArray(&Img, Mip, X, (uint32_t)Layer, Sample,
                                 /*UseBorder=*/0, NoBorder,
                                 /*ApplySwizzle=*/0);
}

// `feme.cpu.image.load.1darray.v4i32` (roadmap H19e): the integer-format
// counterpart of `feme.cpu.image.load.1darray.v4f32` above.
FemeRTv4i32 femeCpuImageLoad1DArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Layer, uint32_t Mip,
    _Bool Mask) asm("feme.cpu.image.load.1darray.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuImageLoad1DArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Layer, uint32_t Mip,
    _Bool Mask) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data)
    return Zero;
  if (X < 0 || Layer < 0 || (uint32_t)X >= Img.Width ||
      (uint32_t)Layer >= Img.ArrayLayers)
    return Zero;
  return femeRTFetchTexel1DArrayI32(&Img, Mip, X, (uint32_t)Layer,
                                    /*ApplySwizzle=*/0);
}

// `feme.cpu.image.store.1darray.v4f32` (roadmap H19e): writes one texel of
// an arrayed 1D storage image at integer coordinate `X`, array layer
// `Layer`, mip level 0 -- Vulkan's `OpImageWrite`, mirroring
// `feme.cpu.image.store.2darray.v4f32`'s own shape.
void femeCpuImageStore1DArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Layer, FemeRTv4f32 Texel,
    _Bool Mask) asm("feme.cpu.image.store.1darray.v4f32");

__attribute__((always_inline)) void femeCpuImageStore1DArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Layer, FemeRTv4f32 Texel,
    _Bool Mask) {
  if (!Mask || Layer < 0)
    return;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  femeRTStoreTexel1DArray(&Img, X, (uint32_t)Layer, Texel);
}

// `feme.cpu.image.store.1darray.v4i32` (roadmap H19e): the integer-format
// counterpart of `feme.cpu.image.store.1darray.v4f32` above.
void femeCpuImageStore1DArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Layer, FemeRTv4i32 Texel,
    _Bool Mask) asm("feme.cpu.image.store.1darray.v4i32");

__attribute__((always_inline)) void femeCpuImageStore1DArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Layer, FemeRTv4i32 Texel,
    _Bool Mask) {
  if (!Mask || Layer < 0)
    return;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  femeRTStoreTexel1DArrayI32(&Img, X, (uint32_t)Layer, Texel);
}

// `feme.cpu.image.load.3d.v4f32` (roadmap H19c): the plain-3D counterpart
// of `feme.cpu.image.load.2d.v4f32` above, taking an `(X, Y, Z)` texel
// coordinate. Never arrayed -- SPIR-V disallows an arrayed `Dim::3D`
// image -- so, unlike `feme.cpu.image.load.2darray.v4f32`, there is no
// separate array-layer operand to carry.
FemeRTv4f32
femeCpuImageLoad3DV4F32(const FemeRTImageDescriptor *ImageHeap,
                        uint32_t ImageHeapCount, uint32_t ImageIndex,
                        int32_t X, int32_t Y, int32_t Z, uint32_t Mip,
                        uint32_t Sample,
                        _Bool Mask) asm("feme.cpu.image.load.3d.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageLoad3DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Z, uint32_t Mip,
    uint32_t Sample, _Bool Mask) {
  (void)Sample; // A real 3D image is never multisampled (Vulkan spec).
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
  return femeRTFetchTexel3D(&Img, Mip, X, Y, Z, /*UseBorder=*/0, NoBorder,
                            /*ApplySwizzle=*/0);
}

// `feme.cpu.image.load.3d.v4i32` (roadmap H19c): the integer-format
// counterpart of `feme.cpu.image.load.3d.v4f32` above.
FemeRTv4i32
femeCpuImageLoad3DV4I32(const FemeRTImageDescriptor *ImageHeap,
                        uint32_t ImageHeapCount, uint32_t ImageIndex,
                        int32_t X, int32_t Y, int32_t Z, uint32_t Mip,
                        _Bool Mask) asm("feme.cpu.image.load.3d.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuImageLoad3DV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Z, uint32_t Mip,
    _Bool Mask) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data)
    return Zero;
  if (X < 0 || Y < 0 || (uint32_t)X >= Img.Width || (uint32_t)Y >= Img.Height)
    return Zero;
  return femeRTFetchTexel3DI32(&Img, Mip, X, Y, Z, /*ApplySwizzle=*/0);
}

// `feme.cpu.image.store.3d.v4f32` (roadmap H19c): writes one texel of a 3D
// storage image at integer coordinates `(X, Y, Z)`, mip level 0 (see
// `femeRTStoreTexel3D`'s own scope comment) -- Vulkan's `OpImageWrite`.
void femeCpuImageStore3DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Z, FemeRTv4f32 Texel,
    _Bool Mask) asm("feme.cpu.image.store.3d.v4f32");

__attribute__((always_inline)) void femeCpuImageStore3DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Z, FemeRTv4f32 Texel,
    _Bool Mask) {
  if (!Mask)
    return;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  femeRTStoreTexel3D(&Img, X, Y, Z, Texel);
}

// `feme.cpu.image.store.3d.v4i32` (roadmap H19c): the integer-format
// counterpart of `feme.cpu.image.store.3d.v4f32` above.
void femeCpuImageStore3DV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Z, FemeRTv4i32 Texel,
    _Bool Mask) asm("feme.cpu.image.store.3d.v4i32");

__attribute__((always_inline)) void femeCpuImageStore3DV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Z, FemeRTv4i32 Texel,
    _Bool Mask) {
  if (!Mask)
    return;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  femeRTStoreTexel3DI32(&Img, X, Y, Z, Texel);
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

// `feme.cpu.image.gathercmp.array2d.v4f32` (roadmap H124q): the
// `Array2D` counterpart of `femeCpuImageGatherCmp2DV4F32` above --
// identical fixed bilinear footprint/result ordering/mip-level-0-only
// restriction, except `ArrayLayer` (SPIR-V's own arrayed-gather
// coordinate, a float rounded to nearest and clamped to a valid layer by
// `femeRTRoundClampLayer`, mirroring `femeCpuImageSample2DArrayV4F32`'s
// own precedent) is threaded into each `femeRTFetchTexel2D` call's own
// `Layer` argument in place of a hard-coded `0`.
FemeRTv4f32 femeCpuImageGatherCmpArray2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V,
    float ArrayLayer, float Dref, int32_t OffsetX, int32_t OffsetY,
    _Bool Mask) asm("feme.cpu.image.gathercmp.array2d.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageGatherCmpArray2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V,
    float ArrayLayer, float Dref, int32_t OffsetX, int32_t OffsetY,
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
  _Bool IsFixedPointDepth = femeRTIsFixedPointDepthFormat(Img.Format);
  uint32_t Layer = femeRTRoundClampLayer(Img.ArrayLayers, ArrayLayer);
  FemeRTBilinearSupport S = femeRTComputeBilinearSupport(
      &Img, U, V, &Samp, /*Level=*/0, OffsetX, OffsetY);
  FemeRTv4f32 T00 = femeRTFetchTexel2D(&Img, /*Level=*/0, Layer, S.X0, S.Y0,
                                       /*Sample=*/0, S.BorderX0 || S.BorderY0,
                                       Samp.BorderColor, /*ApplySwizzle=*/0);
  FemeRTv4f32 T10 = femeRTFetchTexel2D(&Img, /*Level=*/0, Layer, S.X1, S.Y0,
                                       /*Sample=*/0, S.BorderX1 || S.BorderY0,
                                       Samp.BorderColor, /*ApplySwizzle=*/0);
  FemeRTv4f32 T01 = femeRTFetchTexel2D(&Img, /*Level=*/0, Layer, S.X0, S.Y1,
                                       /*Sample=*/0, S.BorderX0 || S.BorderY1,
                                       Samp.BorderColor, /*ApplySwizzle=*/0);
  FemeRTv4f32 T11 = femeRTFetchTexel2D(&Img, /*Level=*/0, Layer, S.X1, S.Y1,
                                       /*Sample=*/0, S.BorderX1 || S.BorderY1,
                                       Samp.BorderColor, /*ApplySwizzle=*/0);
  FemeRTv4f32 Result;
  Result[0] =
      femeRTApplyCompare(Samp.CompareFunc, Dref, T01[0], IsFixedPointDepth);
  Result[1] =
      femeRTApplyCompare(Samp.CompareFunc, Dref, T11[0], IsFixedPointDepth);
  Result[2] =
      femeRTApplyCompare(Samp.CompareFunc, Dref, T10[0], IsFixedPointDepth);
  Result[3] =
      femeRTApplyCompare(Samp.CompareFunc, Dref, T00[0], IsFixedPointDepth);
  return Result;
}

// `feme.cpu.image.gather.array2d.v4f32` (roadmap H124q): the `Array2D`
// counterpart of `femeCpuImageGather2DV4F32` above, adding `ArrayLayer`
// the same way `femeCpuImageGatherCmpArray2DV4F32` does to
// `femeCpuImageGatherCmp2DV4F32`.
FemeRTv4f32 femeCpuImageGatherArray2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V,
    float ArrayLayer, int32_t Component, int32_t OffsetX, int32_t OffsetY,
    _Bool Mask) asm("feme.cpu.image.gather.array2d.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageGatherArray2DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V,
    float ArrayLayer, int32_t Component, int32_t OffsetX, int32_t OffsetY,
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
  uint32_t Chan = (uint32_t)Component > 3u ? 3u : (uint32_t)Component;
  uint32_t Layer = femeRTRoundClampLayer(Img.ArrayLayers, ArrayLayer);
  FemeRTBilinearSupport S = femeRTComputeBilinearSupport(
      &Img, U, V, &Samp, /*Level=*/0, OffsetX, OffsetY);
  // Roadmap L125(g): see femeCpuImageGather2DV4F32's own comment above --
  // each gathered neighbor is swizzled before `Chan` selects from it.
  FemeRTv4f32 T00 = femeRTFetchTexel2D(&Img, /*Level=*/0, Layer, S.X0, S.Y0,
                                       /*Sample=*/0, S.BorderX0 || S.BorderY0,
                                       Samp.BorderColor, /*ApplySwizzle=*/1);
  FemeRTv4f32 T10 = femeRTFetchTexel2D(&Img, /*Level=*/0, Layer, S.X1, S.Y0,
                                       /*Sample=*/0, S.BorderX1 || S.BorderY0,
                                       Samp.BorderColor, /*ApplySwizzle=*/1);
  FemeRTv4f32 T01 = femeRTFetchTexel2D(&Img, /*Level=*/0, Layer, S.X0, S.Y1,
                                       /*Sample=*/0, S.BorderX0 || S.BorderY1,
                                       Samp.BorderColor, /*ApplySwizzle=*/1);
  FemeRTv4f32 T11 = femeRTFetchTexel2D(&Img, /*Level=*/0, Layer, S.X1, S.Y1,
                                       /*Sample=*/0, S.BorderX1 || S.BorderY1,
                                       Samp.BorderColor, /*ApplySwizzle=*/1);
  FemeRTv4f32 Result;
  Result[0] = T01[Chan];
  Result[1] = T11[Chan];
  Result[2] = T10[Chan];
  Result[3] = T00[Chan];
  return Result;
}

// `feme.cpu.image.sample.2darray.v4f32` (roadmap H7b-a): the
// `Texture2DArray` counterpart of `feme.cpu.image.sample.2d.v4f32` above
// -- identical (U, V) filtering (roadmap L60(a): now including
// `femeCpuImageSample2DV4F32`'s own screen-space derivative/anisotropic-
// footprint math via `femeRTPlanImplicitLod`, and its `Bias`/`MinLodClamp`
// operands, unlike this function's own pre-L60(a) `femeRTComputeClampedLod`-
// only, always-single-tap implementation), plus `ArrayLayer` (SPIR-V's own
// arrayed-sample coordinate convention: a float, rounded to nearest and
// clamped to a valid layer by `femeRTRoundClampLayer` above). Roadmap L33:
// `OffsetX`/`OffsetY` are the same `ConstOffset` image operand
// `femeCpuImageSample2DV4F32` documents -- an ordinary Array2D sample can
// carry a real, possibly-nonzero one too now, applied to every tap
// uniformly the same way `femeCpuImageSample2DV4F32`'s own multi-tap
// anisotropic loop already does.
FemeRTv4f32 femeCpuImageSample2DArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V,
    float ArrayLayer, float DUdX, float DUdY, float DVdX, float DVdY, float Lod,
    _Bool UseExplicitLod, float Bias, int32_t OffsetX, int32_t OffsetY,
    float MinLodClamp, _Bool Mask) asm("feme.cpu.image.sample.2darray.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageSample2DArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V,
    float ArrayLayer, float DUdX, float DUdY, float DVdX, float DVdY, float Lod,
    _Bool UseExplicitLod, float Bias, int32_t OffsetX, int32_t OffsetY,
    float MinLodClamp, _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  uint32_t Layer = femeRTRoundClampLayer(Img.ArrayLayers, ArrayLayer);

  if (UseExplicitLod) {
    // Mirrors `femeCpuImageSample2DV4F32`'s own explicit-LOD case:
    // `MinLodClamp`/`Bias` are always no-ops (`-INFINITY`/`0.0f`)
    // whenever `UseExplicitLod` is set.
    float ClampedLod = femeRTComputeClampedLod(Lod, /*UseExplicitLod=*/1,
                                               &Samp, MinLodClamp, Bias);
    return femeRTSampleFiltered2D(&Img, &Samp, U, V, Layer, ClampedLod, OffsetX,
                                  OffsetY);
  }

  FemeRTImplicitLodPlan Plan = femeRTPlanImplicitLod(
      &Img, &Samp, DUdX, DUdY, DVdX, DVdY, MinLodClamp, Bias);
  if (Plan.TapCount <= 1)
    return femeRTSampleFiltered2D(&Img, &Samp, U, V, Layer, Plan.ClampedLod,
                                  OffsetX, OffsetY);

  // Anisotropic footprint: mirrors `femeCpuImageSample2DV4F32`'s own
  // multi-tap loop, but reading the same array `Layer` for every tap.
  FemeRTv4f32 Sum = {0.0f, 0.0f, 0.0f, 0.0f};
  float FirstOffset = -0.5f * (float)(Plan.TapCount - 1);
  for (uint32_t Tap = 0; Tap != Plan.TapCount; ++Tap) {
    float Offset = FirstOffset + (float)Tap;
    float TapU = U + Offset * Plan.StepU;
    float TapV = V + Offset * Plan.StepV;
    Sum += femeRTSampleFiltered2D(&Img, &Samp, TapU, TapV, Layer,
                                  Plan.ClampedLod, OffsetX, OffsetY);
  }
  return Sum * (1.0f / (float)Plan.TapCount);
}

// `feme.cpu.image.sample.1d.v4f32` (roadmap L52a): the `Texture1D`
// counterpart of `feme.cpu.image.sample.2d.v4f32`, minus its own
// screen-space-derivative/`ConstOffset` operands originally (roadmap L52a's
// own scope note: this shape's own richer additions -- mirroring
// `feme.cpu.image.sample.2d.v4f32`'s own -- deferred to keep that first
// pass minimal). Roadmap L63 now threads a real `DUdX`/`DUdY` screen-space
// partial-derivative pair through too, mirroring
// `feme.cpu.image.sample.2d.v4f32`'s own identical operands narrowed to
// this shape's single addressed coordinate component (see
// `femeRTPlanImplicitLod1D`'s own doc for why this is a dedicated helper
// rather than a reuse of `femeRTPlanImplicitLod` with zeroed `V`-axis
// arguments): an implicit-LOD sample's real mip level now reflects this
// sample's own real minification/magnification instead of always
// resolving to mip level 0 regardless of derivatives, exactly like
// `Plain2D`'s own implicit-LOD path (`UseExplicitLod` still short-circuits
// this entirely, mirroring `femeCpuImageSample2DV4F32`'s own explicit-LOD
// branch). `Bias`/`MinLodClamp` (roadmap L61(c)) thread a real
// `texture(sampler1D, u, bias)`/`MinLod`-clamp pair through to
// `femeRTComputeClampedLod`, mirroring
// `feme.cpu.image.sample.2d.v4f32`'s own identical parameters, in place
// of the previous hardcoded `0.0f`/`-inf` no-op values. `Offset` (roadmap
// L66(d)) threads a real `textureOffset(sampler1D, u, offset)` texel
// offset through to `femeRTSampleFiltered1D`'s own pre-existing `OffsetX`
// parameter (added ahead of this row but never wired up to a real
// operand until now), in place of the previous hardcoded `0` no-op
// value.
FemeRTv4f32 femeCpuImageSample1DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float DUdX, float DUdY,
    float Lod, _Bool UseExplicitLod, float Bias, int32_t Offset,
    float MinLodClamp, _Bool Mask) asm("feme.cpu.image.sample.1d.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageSample1DV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float DUdX, float DUdY,
    float Lod, _Bool UseExplicitLod, float Bias, int32_t Offset,
    float MinLodClamp, _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  float RawLod =
      UseExplicitLod ? Lod : femeRTPlanImplicitLod1D(&Img, DUdX, DUdY);
  // `UseExplicitLod=1` always, mirroring `femeRTPlanImplicitLod`'s own
  // call to `femeRTComputeClampedLod` (see its own doc comment): `RawLod`
  // is already this sample's own real starting-point LOD by this point
  // -- whether it came from an explicit `Lod` operand or was just derived
  // from real screen-space derivatives above -- so `false` here would
  // incorrectly discard it back to the hardcoded `0.0f`
  // `femeRTComputeClampedLod` itself falls back to for an actually-
  // implicit sample with no caller-derived LOD of its own to give.
  float ClampedLod =
      femeRTComputeClampedLod(RawLod, /*UseExplicitLod=*/1, &Samp,
                             /*InstructionMinLod=*/MinLodClamp,
                             /*InstructionBias=*/Bias);
  return femeRTSampleFiltered1D(&Img, &Samp, U, /*Layer=*/0, ClampedLod,
                                Offset);
}

// `feme.cpu.image.sample.1darray.v4f32` (roadmap L52a): the `Texture1DArray`
// counterpart of `feme.cpu.image.sample.1d.v4f32` above -- identical `U`
// filtering, plus `ArrayLayer` (SPIR-V's own arrayed-sample coordinate
// convention: a float, rounded to nearest and clamped to a valid layer by
// `femeRTRoundClampLayer` above), mirroring
// `feme.cpu.image.sample.2darray.v4f32`'s own precedent. `DUdX`/`DUdY`
// (roadmap L63) mirror `femeCpuImageSample1DV4F32`'s own new derivative
// parameters -- only `U` itself is ever differentiated, never
// `ArrayLayer`, mirroring `feme.cpu.image.sample.2darray.v4f32`'s own
// layer-agnostic derivative handling. `Bias`/`MinLodClamp` (roadmap
// L61(c)) mirror `femeCpuImageSample1DV4F32`'s own identical new
// parameters. `Offset` (roadmap L66(d)) mirrors
// `femeCpuImageSample1DV4F32`'s own identical new parameter -- `Array1D`'s
// own `ConstOffset` never touches `ArrayLayer`, matching `Plain1D`'s
// scalar (non-vector) offset shape.
FemeRTv4f32 femeCpuImageSample1DArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float ArrayLayer,
    float DUdX, float DUdY, float Lod, _Bool UseExplicitLod, float Bias,
    int32_t Offset, float MinLodClamp,
    _Bool Mask) asm("feme.cpu.image.sample.1darray.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageSample1DArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float ArrayLayer,
    float DUdX, float DUdY, float Lod, _Bool UseExplicitLod, float Bias,
    int32_t Offset, float MinLodClamp, _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  float RawLod =
      UseExplicitLod ? Lod : femeRTPlanImplicitLod1D(&Img, DUdX, DUdY);
  // `UseExplicitLod=1` always -- see `femeCpuImageSample1DV4F32`'s own
  // identical comment above.
  float ClampedLod =
      femeRTComputeClampedLod(RawLod, /*UseExplicitLod=*/1, &Samp,
                             /*InstructionMinLod=*/MinLodClamp,
                             /*InstructionBias=*/Bias);
  uint32_t Layer = femeRTRoundClampLayer(Img.ArrayLayers, ArrayLayer);
  return femeRTSampleFiltered1D(&Img, &Samp, U, Layer, ClampedLod, Offset);
}

// (Roadmap L125(b)) The `Array1D` counterpart of `femeCpuImageSample1DV4I32`
// (see above), for `feme.cpu.image.sample.1darray.v4i32` -- mirrors that
// function's structure exactly, but adds `ArrayLayer` (clamped/rounded via
// the same `femeRTRoundClampLayer` helper `femeCpuImageSample1DArrayV4F32`
// immediately above uses), matching `Sample1DArray`'s own relationship to
// `Sample1D`.
FemeRTv4i32 femeCpuImageSample1DArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float ArrayLayer,
    float Lod, int32_t Offset,
    _Bool Mask) asm("feme.cpu.image.sample.1darray.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuImageSample1DArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float ArrayLayer,
    float Lod, int32_t Offset, _Bool Mask) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);

  // `MinLodClamp`/`Bias` are always the no-op values here (`-INFINITY`/
  // `0.0f`), mirroring `femeCpuImageSample1DV4I32`'s own choice above.
  float ClampedLod = femeRTComputeClampedLod(
      Lod, /*UseExplicitLod=*/1, &Samp, -__builtin_inff(), 0.0f);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(&Img, ClampedLod);
  uint32_t Level = femeRTNearestMipLevel(MipPlan);
  uint32_t Layer = femeRTRoundClampLayer(Img.ArrayLayers, ArrayLayer);
  uint32_t LevelWidth = femeRTMipExtent(Img.Width, Level);
  int32_t X = (int32_t)__builtin_floorf(U * (float)LevelWidth) + Offset;
  _Bool BorderX = 0;
  int32_t AddrX = femeRTApplyAddressMode(X, (int32_t)LevelWidth,
                                         Samp.AddressU, &BorderX);
  if (BorderX) {
    // Roadmap L125(b): same documented, narrow limitation as
    // `femeCpuImageSample1DV4I32`'s own identical fallback above -- no
    // real CTS case is known to exercise `CLAMP_TO_BORDER` addressing
    // against an integer-sampled `Array1D` image either.
    FemeRTv4i32 Border = {0, 0, 0, 1};
    return Border;
  }
  return femeRTFetchTexel1DArrayI32(&Img, Level, AddrX, Layer,
                                    /*ApplySwizzle=*/1);
}

// (Roadmap L125(b)) The `Array2D` counterpart of `femeCpuImageSample2DV4I32`
// (see above), for `feme.cpu.image.sample.2darray.v4i32` -- mirrors that
// function's structure exactly, but adds `ArrayLayer` (clamped/rounded via
// the same `femeRTRoundClampLayer` helper `femeCpuImageSample2DArrayV4F32`
// uses), matching `Sample2DArray`'s own relationship to `Sample2D`.
// `femeRTFetchTexel2DI32` already takes a `Layer` parameter (it is the
// same helper `femeCpuImageSample2DV4I32` calls with `Layer=0`), so no new
// texel-fetch helper is needed here either.
FemeRTv4i32 femeCpuImageSample2DArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V,
    float ArrayLayer, float Lod, int32_t OffsetX, int32_t OffsetY,
    _Bool Mask) asm("feme.cpu.image.sample.2darray.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuImageSample2DArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V,
    float ArrayLayer, float Lod, int32_t OffsetX, int32_t OffsetY,
    _Bool Mask) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);

  // `MinLodClamp`/`Bias` are always the no-op values here (`-INFINITY`/
  // `0.0f`), mirroring `femeCpuImageSample2DV4I32`'s own choice above.
  float ClampedLod = femeRTComputeClampedLod(
      Lod, /*UseExplicitLod=*/1, &Samp, -__builtin_inff(), 0.0f);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(&Img, ClampedLod);
  uint32_t Level = femeRTNearestMipLevel(MipPlan);
  uint32_t Layer = femeRTRoundClampLayer(Img.ArrayLayers, ArrayLayer);
  uint32_t LevelWidth = femeRTMipExtent(Img.Width, Level);
  uint32_t LevelHeight = femeRTMipExtent(Img.Height, Level);
  int32_t X = (int32_t)__builtin_floorf(U * (float)LevelWidth) + OffsetX;
  int32_t Y = (int32_t)__builtin_floorf(V * (float)LevelHeight) + OffsetY;
  _Bool BorderX = 0, BorderY = 0;
  int32_t AddrX = femeRTApplyAddressMode(X, (int32_t)LevelWidth,
                                         Samp.AddressU, &BorderX);
  int32_t AddrY = femeRTApplyAddressMode(Y, (int32_t)LevelHeight,
                                         Samp.AddressV, &BorderY);
  if (BorderX || BorderY) {
    // Roadmap L125(b): same documented, narrow limitation as
    // `femeCpuImageSample2DV4I32`'s own identical fallback above -- no
    // real CTS case is known to exercise `CLAMP_TO_BORDER` addressing
    // against an integer-sampled `Array2D` image either.
    FemeRTv4i32 Border = {0, 0, 0, 1};
    return Border;
  }
  return femeRTFetchTexel2DI32(&Img, Level, Layer, AddrX, AddrY,
                               /*Sample=*/0, /*ApplySwizzle=*/1);
}

// (Roadmap L54) The single-level body of `femeCpuImageSampleCmp1DF32`/
// `femeCpuImageSampleCmpArray1DF32` below, mirroring
// `femeRTSampleCmp2DAtLevel`'s own point/bilinear comparison-filtering
// logic (percentage-closer filtering: compare each fetched texel first,
// then filter the 0/1 results) but reusing the 1D(-array) fetch/addressing
// helpers (`femeRTFetchTexel1DArray`, minus the `V` axis)
// `femeRTSamplePoint1D`/ `femeRTSampleLinear1D` above already established for
// an ordinary 1D color sample. `OffsetX` (roadmap L66(k)) mirrors those two
// helpers' own identically-named parameter: a real `deqp-vk` SPIR-V capture
// confirms a 1D shadow sampler's own `ConstOffset` is the same bare scalar
// `i32` an ordinary sample's is, so it is applied to `X`/`BaseX` the same way,
// before `femeRTApplyAddressMode`.
__attribute__((always_inline)) static float
femeRTSampleCmp1DAtLevel(const FemeRTImageDescriptor *Img,
                         const FemeRTSamplerDescriptor *Samp, float U,
                         uint32_t Layer, uint32_t Level, float Dref,
                         _Bool UseLinear, int32_t OffsetX) {
  _Bool IsFixedPointDepth = femeRTIsFixedPointDepthFormat(Img->Format);
  if (!UseLinear) { // Point (nearest).
    uint32_t LevelWidth = femeRTMipExtent(Img->Width, Level);
    int32_t X = (int32_t)__builtin_floorf(U * (float)LevelWidth) + OffsetX;
    _Bool BorderX = 0;
    int32_t AddrX = femeRTApplyAddressMode(X, (int32_t)LevelWidth,
                                           Samp->AddressU, &BorderX);
    FemeRTv4f32 T = femeRTFetchTexel1DArray(Img, Level, AddrX, Layer,
                                            /*Sample=*/0, BorderX,
                                            Samp->BorderColor,
                                            /*ApplySwizzle=*/0);
    return femeRTApplyCompare(Samp->CompareFunc, Dref, T[0], IsFixedPointDepth);
  }

  uint32_t LevelWidth = femeRTMipExtent(Img->Width, Level);
  float TexelU = U * (float)LevelWidth - 0.5f;
  float FloorU = __builtin_floorf(TexelU);
  int32_t BaseX = (int32_t)FloorU + OffsetX;
  float Wx = TexelU - FloorU;
  _Bool BorderX0 = 0, BorderX1 = 0;
  int32_t X0 = femeRTApplyAddressMode(BaseX, (int32_t)LevelWidth,
                                      Samp->AddressU, &BorderX0);
  int32_t X1 = femeRTApplyAddressMode(BaseX + 1, (int32_t)LevelWidth,
                                      Samp->AddressU, &BorderX1);
  FemeRTv4f32 T0 = femeRTFetchTexel1DArray(Img, Level, X0, Layer,
                                          /*Sample=*/0, BorderX0,
                                          Samp->BorderColor,
                                          /*ApplySwizzle=*/0);
  FemeRTv4f32 T1 = femeRTFetchTexel1DArray(Img, Level, X1, Layer,
                                          /*Sample=*/0, BorderX1,
                                          Samp->BorderColor,
                                          /*ApplySwizzle=*/0);
  float C0 = femeRTApplyCompare(Samp->CompareFunc, Dref, T0[0], IsFixedPointDepth);
  float C1 = femeRTApplyCompare(Samp->CompareFunc, Dref, T1[0], IsFixedPointDepth);
  return C0 + (C1 - C0) * Wx;
}

// `feme.cpu.image.samplecmp.1d.f32` (roadmap L54): the depth-comparison
// counterpart of `feme.cpu.image.sample.1d.v4f32`, mirroring
// `feme.cpu.image.samplecmp.2d.f32`'s own trilinear-blend body above
// (`femeRTSampleCmp2DAtLevel`) but reusing `femeRTSampleCmp1DAtLevel`
// instead -- a single `U` coordinate instead of `(U, V)`, a real
// `Bias`/`MinLodClamp` pair as of roadmap L62, and a real, bare-scalar
// `Offset` as of roadmap L66(k) (see `femeRTSampleCmp1DAtLevel`'s own
// comment). (Roadmap L66(f)) `DUdX`/`DUdY` generalize what used to always
// be implicitly zero, mirroring `femeCpuImageSampleCmp2DF32`'s own
// identical `Grad` generalization (roadmap L66(c)): for an implicit-LOD
// call (`UseExplicitLod` false), these now drive a real
// `femeRTPlanImplicitLod1D` derivative-based LOD calculation -- a bare
// scalar pair, not a `(DUdX, DVdX, DUdY, DVdY)` quartet, since `Plain1D`'s
// own addressing is itself already a bare scalar `U`, mirroring
// `femeCpuImageSample1DV4F32`'s own identical `DUdX`/`DUdY` parameters for
// an ordinary sample.
float femeCpuImageSampleCmp1DF32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float DUdX, float DUdY,
    float Lod, _Bool UseExplicitLod, float Dref, float Bias, int32_t Offset,
    float MinLodClamp, _Bool Mask) asm("feme.cpu.image.samplecmp.1d.f32");

__attribute__((always_inline)) float femeCpuImageSampleCmp1DF32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float DUdX, float DUdY,
    float Lod, _Bool UseExplicitLod, float Dref, float Bias, int32_t Offset,
    float MinLodClamp, _Bool Mask) {
  if (!Mask)
    return 0.0f;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return 0.0f;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  float ClampedLod;
  if (UseExplicitLod) {
    ClampedLod = femeRTComputeClampedLod(Lod, UseExplicitLod, &Samp,
                                         /*InstructionMinLod=*/MinLodClamp,
                                         /*InstructionBias=*/Bias);
  } else {
    float RawLod = femeRTPlanImplicitLod1D(&Img, DUdX, DUdY);
    ClampedLod = femeRTComputeClampedLod(RawLod, /*UseExplicitLod=*/1, &Samp,
                                         /*InstructionMinLod=*/MinLodClamp,
                                         /*InstructionBias=*/Bias);
  }
  _Bool UseLinear = femeRTUseLinearFilter(ClampedLod, &Samp);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(&Img, ClampedLod);
  _Bool Trilinear = Samp.MipFilter == 1 && MipPlan.Level0 != MipPlan.Level1;
  uint32_t Level0 = Trilinear ? MipPlan.Level0 : femeRTNearestMipLevel(MipPlan);
  float Lo = femeRTSampleCmp1DAtLevel(&Img, &Samp, U, /*Layer=*/0, Level0, Dref,
                                      UseLinear, Offset);
  if (!Trilinear)
    return Lo;
  float Hi = femeRTSampleCmp1DAtLevel(&Img, &Samp, U, /*Layer=*/0,
                                      MipPlan.Level1, Dref, UseLinear, Offset);
  return Lo + (Hi - Lo) * MipPlan.Frac;
}

// `feme.cpu.image.samplecmp.1darray.f32` (roadmap L54): the
// `Texture1DArray` counterpart of `feme.cpu.image.samplecmp.1d.f32` above,
// mirroring `feme.cpu.image.sample.1darray.v4f32`'s own relationship to
// `feme.cpu.image.sample.1d.v4f32`. (Roadmap L66(f)) `DUdX`/`DUdY` mirror
// `femeCpuImageSampleCmp1DF32`'s own identical new parameters -- only `U`,
// never `ArrayLayer`, is ever differentiated. (Roadmap L66(k)) `Offset`
// mirrors that function's own new, bare-scalar `Offset` parameter --
// `Array1D`'s own `ConstOffset` never touches `ArrayLayer` either.
float femeCpuImageSampleCmpArray1DF32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float ArrayLayer,
    float DUdX, float DUdY, float Lod, _Bool UseExplicitLod, float Dref,
    float Bias, int32_t Offset, float MinLodClamp,
    _Bool Mask) asm("feme.cpu.image.samplecmp.1darray.f32");

__attribute__((always_inline)) float femeCpuImageSampleCmpArray1DF32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float ArrayLayer,
    float DUdX, float DUdY, float Lod, _Bool UseExplicitLod, float Dref,
    float Bias, int32_t Offset, float MinLodClamp, _Bool Mask) {
  if (!Mask)
    return 0.0f;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return 0.0f;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  float ClampedLod;
  if (UseExplicitLod) {
    ClampedLod = femeRTComputeClampedLod(Lod, UseExplicitLod, &Samp,
                                         /*InstructionMinLod=*/MinLodClamp,
                                         /*InstructionBias=*/Bias);
  } else {
    float RawLod = femeRTPlanImplicitLod1D(&Img, DUdX, DUdY);
    ClampedLod = femeRTComputeClampedLod(RawLod, /*UseExplicitLod=*/1, &Samp,
                                         /*InstructionMinLod=*/MinLodClamp,
                                         /*InstructionBias=*/Bias);
  }
  uint32_t Layer = femeRTRoundClampLayer(Img.ArrayLayers, ArrayLayer);
  _Bool UseLinear = femeRTUseLinearFilter(ClampedLod, &Samp);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(&Img, ClampedLod);
  _Bool Trilinear = Samp.MipFilter == 1 && MipPlan.Level0 != MipPlan.Level1;
  uint32_t Level0 = Trilinear ? MipPlan.Level0 : femeRTNearestMipLevel(MipPlan);
  float Lo = femeRTSampleCmp1DAtLevel(&Img, &Samp, U, Layer, Level0, Dref,
                                      UseLinear, Offset);
  if (!Trilinear)
    return Lo;
  float Hi = femeRTSampleCmp1DAtLevel(&Img, &Samp, U, Layer, MipPlan.Level1,
                                      Dref, UseLinear, Offset);
  return Lo + (Hi - Lo) * MipPlan.Frac;
}

// `feme.cpu.image.samplecmp.2darray.f32` (roadmap L48): the
// `Texture2DArray` counterpart of `feme.cpu.image.samplecmp.2d.f32` above
// -- identical (U, V) depth-comparison filtering, plus `ArrayLayer`
// (SPIR-V's own arrayed-sample coordinate convention: a float, rounded to
// nearest and clamped to a valid layer by `femeRTRoundClampLayer`,
// mirroring `femeCpuImageSample2DArrayV4F32`'s own precedent). `OffsetX`/
// `OffsetY` (roadmap L50d) mirror `femeCpuImageSampleCmp2DF32`'s own new
// `ConstOffset` operand; `MinLodClamp` (roadmap L52(c)) mirrors its own
// new `MinLod` clamp operand. `DUdX`/`DUdY`/`DVdX`/`DVdY` (roadmap
// L66(g)) mirror `femeCpuImageSampleCmp2DF32`'s own identically-named
// `Grad` operands, driving the same real `femeRTPlanImplicitLod`
// derivative-based LOD calculation on the implicit-LOD path -- only
// `U`/`V`, never `ArrayLayer`, is ever differentiated, matching
// `femeCpuImageSample2DArrayV4F32`'s own identical precedent for an
// ordinary sample.
float femeCpuImageSampleCmpArray2DF32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V,
    float ArrayLayer, float DUdX, float DUdY, float DVdX, float DVdY, float Lod,
    _Bool UseExplicitLod, float Dref, float Bias, int32_t OffsetX,
    int32_t OffsetY, float MinLodClamp,
    _Bool Mask) asm("feme.cpu.image.samplecmp.2darray.f32");

__attribute__((always_inline)) float femeCpuImageSampleCmpArray2DF32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float U, float V,
    float ArrayLayer, float DUdX, float DUdY, float DVdX, float DVdY, float Lod,
    _Bool UseExplicitLod, float Dref, float Bias, int32_t OffsetX,
    int32_t OffsetY, float MinLodClamp, _Bool Mask) {
  if (!Mask)
    return 0.0f;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u)) // FEME_IMAGE_SAMPLED.
    return 0.0f;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  float ClampedLod;
  if (UseExplicitLod) {
    ClampedLod = femeRTComputeClampedLod(Lod, UseExplicitLod, &Samp,
                                         /*InstructionMinLod=*/MinLodClamp,
                                         /*InstructionBias=*/Bias);
  } else {
    FemeRTImplicitLodPlan Plan =
        femeRTPlanImplicitLod(&Img, &Samp, DUdX, DUdY, DVdX, DVdY,
                              /*InstructionMinLod=*/MinLodClamp,
                              /*InstructionBias=*/Bias);
    ClampedLod = Plan.ClampedLod;
  }
  _Bool UseLinear = femeRTUseLinearFilter(ClampedLod, &Samp);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(&Img, ClampedLod);
  _Bool Trilinear = Samp.MipFilter == 1 && MipPlan.Level0 != MipPlan.Level1;
  uint32_t Level0 = Trilinear ? MipPlan.Level0 : femeRTNearestMipLevel(MipPlan);
  uint32_t Layer = femeRTRoundClampLayer(Img.ArrayLayers, ArrayLayer);
  float Lo = femeRTSampleCmp2DAtLevel(&Img, &Samp, U, V, Layer, Level0, Dref,
                                     UseLinear, OffsetX, OffsetY);
  if (!Trilinear)
    return Lo;
  float Hi = femeRTSampleCmp2DAtLevel(&Img, &Samp, U, V, Layer,
                                      MipPlan.Level1, Dref, UseLinear, OffsetX,
                                      OffsetY);
  return Lo + (Hi - Lo) * MipPlan.Frac;
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
                            /*UseBorder=*/0, NoBorder, /*ApplySwizzle=*/0);
}

// `feme.cpu.image.load.2darray.v4i32` (roadmap H7b-a): the integer-format
// counterpart of `feme.cpu.image.load.2darray.v4f32` above, mirroring how
// `feme.cpu.image.load.2d.v4i32` relates to `feme.cpu.image.load.2d.v4f32`.
// Takes a `Sample` operand (roadmap H19m), mirroring
// `feme.cpu.image.load.2darray.v4f32`'s own -- widened in place the same
// way roadmap H19g widened `feme.cpu.image.load.2d.v4i32` to add the
// operand its float counterpart already had.
FemeRTv4i32 femeCpuImageLoad2DArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Layer, uint32_t Mip,
    uint32_t Sample, _Bool Mask) asm("feme.cpu.image.load.2darray.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuImageLoad2DArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    uint32_t ImageIndex, int32_t X, int32_t Y, int32_t Layer, uint32_t Mip,
    uint32_t Sample, _Bool Mask) {
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
  return femeRTFetchTexel2DI32(&Img, Mip, (uint32_t)Layer, X, Y, Sample,
                               /*ApplySwizzle=*/0);
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
  // (Roadmap L56) The un-normalized numerator/denominator this face's own
  // (U, V) division came from, before the final `0.5f * (./Major + 1.0f)`
  // remap below -- exposed only so `femeRTComputeCubeUVDerivatives` can
  // apply the same quotient-rule chain to a caller's own per-invocation
  // direction-vector derivatives that this function applies to the
  // direction vector's own raw (X, Y, Z) components. `RawMajor` is
  // already clamped away from zero the same way `Major` below is.
  float RawU, RawV, RawMajor;
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
  R.RawU = U;
  R.RawV = V;
  R.RawMajor = Major;
  R.U = 0.5f * (U / Major + 1.0f);
  R.V = 0.5f * (V / Major + 1.0f);
  return R;
}

// (Roadmap L56) The screen-space partial derivatives of the face-local
// `(U, V)` coordinate `femeRTSelectCubeFace` computed for `Face`, given
// the caller's own per-invocation screen-space partial derivatives of the
// direction vector's raw `(X, Y, Z)` components -- `DXdX`/`DYdX`/`DZdX`
// with respect to the screen-space X axis, `DXdY`/`DYdY`/`DZdY` with
// respect to Y (the same quad-based finite differences
// `getOrSynthesizeSample2DDerivatives` already synthesizes for a `Plain2D`
// sample's own `(DUdX, DUdY, DVdX, DVdY)`, applied here to the direction
// vector's three components instead of two already-face-local
// coordinates). This is what lets `femeRTPlanImplicitLod` -- otherwise
// unmodified, and already exhaustively tested against `Plain2D`'s own
// anisotropic-filtering CTS group -- compute a real, non-always-zero
// implicit LOD for a `Cube`/`CubeArray` sample: previously, every
// implicit-LOD cube sample resolved to mip level 0 unconditionally (no
// derivative of any kind was ever threaded through), which a real
// `dEQP-VK.texture.filtering.cube.combinations.linear_mipmap_linear.*`
// re-run confirmed was the actual root cause of that whole CTS group's
// 0/25 failure, not a bug in the trilinear blend arithmetic itself.
//
// Each face's own `(U, V) = (RawU, RawV) / RawMajor` is a ratio of two
// signed direction-vector components (or their negation, which the
// quotient rule handles identically since `d(-f)/dp == -df/dp`), so the
// same per-face sign/axis-selection table `femeRTSelectCubeFace` uses for
// `X`/`Y`/`Z` themselves applies unchanged to their derivatives; the
// ordinary calculus quotient rule, `d(N/M)/dp = (dN/dp*M - N*dM/dp)/M^2`,
// then turns those into `d(U)/dp`/`d(V)/dp` (the outer `0.5f*(./Major+1)`
// remap's own derivative is just that result scaled by 0.5, since the
// `+1.0f` term is constant).
typedef struct {
  float DUdX, DUdY, DVdX, DVdY;
} FemeRTCubeUVDerivatives;

__attribute__((always_inline)) static FemeRTCubeUVDerivatives
femeRTComputeCubeUVDerivatives(uint32_t Face, float RawMajor, float DXdX,
                               float DXdY, float DYdX, float DYdY, float DZdX,
                               float DZdY) {
  float DRawUdX, DRawUdY, DRawVdX, DRawVdY;
  switch (Face) {
  case 0: // +X: U=-Z, V=-Y, Major=X.
    DRawUdX = -DZdX;
    DRawUdY = -DZdY;
    DRawVdX = -DYdX;
    DRawVdY = -DYdY;
    break;
  case 1: // -X: U=Z, V=-Y, Major=-X.
    DRawUdX = DZdX;
    DRawUdY = DZdY;
    DRawVdX = -DYdX;
    DRawVdY = -DYdY;
    break;
  case 2: // +Y: U=X, V=Z, Major=Y.
    DRawUdX = DXdX;
    DRawUdY = DXdY;
    DRawVdX = DZdX;
    DRawVdY = DZdY;
    break;
  case 3: // -Y: U=X, V=-Z, Major=-Y.
    DRawUdX = DXdX;
    DRawUdY = DXdY;
    DRawVdX = -DZdX;
    DRawVdY = -DZdY;
    break;
  case 4: // +Z: U=X, V=-Y, Major=Z.
    DRawUdX = DXdX;
    DRawUdY = DXdY;
    DRawVdX = -DYdX;
    DRawVdY = -DYdY;
    break;
  default: // 5, -Z: U=-X, V=-Y, Major=-Z.
    DRawUdX = -DXdX;
    DRawUdY = -DXdY;
    DRawVdX = -DYdX;
    DRawVdY = -DYdY;
    break;
  }
  // (Roadmap L66(h)) VK-GL-CTS's own reference oracle for this LOD
  // (`vktShaderRenderTextureFunctionTests.cpp`'s `computeLodFromGradCube`)
  // deliberately treats the major-axis component as *locally constant*
  // across the derivative -- it scales the raw direction derivative by a
  // fixed `size / (2 * |majorAxis|)` factor and never differentiates
  // `|majorAxis|` itself, unlike a mathematically exact quotient-rule
  // derivative of `U = 0.5 * (rawU / rawMajor + 1)` (which this function
  // computed before this fix, via a now-removed `RawU * DRawMajordX`/
  // `RawV * DRawMajordY` correction term). That extra precision is
  // invisible to an ordinary color sample (any resulting sub-texel LOD
  // difference blends into a barely-different filtered color, safely
  // within `deqp-vk`'s own image-comparison threshold) but a
  // depth-comparison sample's boolean pass/fail result is far more
  // sensitive to exactly which mip level gets selected near a rounding
  // boundary -- confirmed via a real `dEQP-VK.glsl.texture_functions.
  // texturegrad.samplercubeshadow_{fragment,vertex}` capture showing a
  // sparse (90/16384-pixel, ~0.5%) diagonal-boundary "Image mismatch"
  // that vanished once this function's derivative matched the oracle's
  // own simplified formula instead of the exact one. Dropping the
  // correction term here (this function's only caller,
  // `femeRTComputeCubeClampedLod`, uses it purely for LOD estimation,
  // never for the actual sample's fetch coordinates) matches `deqp-vk`'s
  // own reference bit-for-bit for this LOD purpose.
  FemeRTCubeUVDerivatives D;
  D.DUdX = 0.5f * DRawUdX / RawMajor;
  D.DUdY = 0.5f * DRawUdY / RawMajor;
  D.DVdX = 0.5f * DRawVdX / RawMajor;
  D.DVdY = 0.5f * DRawVdY / RawMajor;
  return D;
}

// (Roadmap L53) Vulkan's own spec-mandated default cube-map filtering
// behaviour ("seamless cube map filtering," 16.3.3) blends a `LINEAR`
// bilinear tap that falls just outside a face's own `[0, size)` bounds
// with its true geometric neighbor -- the adjacent face across that
// shared cube edge -- rather than clamping it back onto the same face's
// own edge texel the way a plain 2D image's `ClampToEdge` address mode
// would. This project has no way to opt out (`VK_EXT_non_seamless_cube_map`,
// the only spec-defined opt-out, is `Not implemented` per
// `VulkanExtensionInventory.md`), so seamless filtering is applied
// unconditionally below, with no new per-sampler descriptor bit needed.
//
// `femeRTRemapCubeEdgeCoords` mirrors VK-GL-CTS's own reference oracle
// (`tcuTexture.cpp`'s `remapCubeEdgeCoords`), re-derived here against
// `femeRTSelectCubeFace`'s own face numbering (Vulkan's `+X,-X,+Y,-Y,
// +Z,-Z` cube-array-layer order, faces 0-5) and `U`/`V` sign convention,
// rather than reused verbatim (`tcuTexture.cpp`'s own internal
// `CubeFace` enum uses a different face order and per-face sign
// convention). Given a face and an integer texel coordinate that may
// fall outside `[0, Size)` in one or both axes (always by exactly one
// texel in each axis here, since a bilinear tap is never more than one
// texel beyond a face's own bounds and `ConstOffset` is never present
// against a `Cube`/`CubeArray` image -- SPIR-V forbids it outright, see
// roadmap L52's own review), this resolves the correct neighboring face
// and remapped in-bounds coordinate. If both axes are out of bounds --
// a tap that falls off the corner of the cube, shared by three faces
// with no single unique neighbor -- `Ambiguous` is set and `Face`/`X`/`Y`
// are left unspecified; the caller resolves that corner tap by averaging
// the other three, mirroring VK-GL-CTS's own recommended (not
// spec-required) behaviour.
//
// The face-to-canonical-3D-coordinate mapping below (`Cx`/`Cy`/`Cz`,
// each spanning `[0, Size)` when in-bounds, one axis always pinned to
// `0` or `Size - 1` per face) is derived directly from
// `femeRTSelectCubeFace`'s own forward formulas above (e.g. face `0`
// (`+X`)'s `U = -Z / Major`, `V = -Y / Major` inverts to `Cz = Size - 1
// - X`, `Cy = Size - 1 - Y`, with `Cx` pinned to `Size - 1`), then
// re-inverted for whichever neighboring face's own canonical formula the
// single out-of-bounds axis identifies.
typedef struct {
  uint32_t Face;
  int32_t X, Y;
  _Bool Ambiguous;
} FemeRTCubeEdgeCoords;

__attribute__((always_inline)) static FemeRTCubeEdgeCoords
femeRTRemapCubeEdgeCoords(uint32_t Face, int32_t X, int32_t Y, int32_t Size) {
  _Bool XIn = X >= 0 && X < Size;
  _Bool YIn = Y >= 0 && Y < Size;
  FemeRTCubeEdgeCoords R;
  if (XIn && YIn) {
    R.Face = Face;
    R.X = X;
    R.Y = Y;
    R.Ambiguous = 0;
    return R;
  }
  if (!XIn && !YIn) {
    R.Face = Face;
    R.X = 0;
    R.Y = 0;
    R.Ambiguous = 1;
    return R;
  }
  R.Ambiguous = 0;
  int32_t Cx = 0, Cy = 0, Cz = 0;
  switch (Face) {
  case 0: // +X.
    Cx = Size - 1;
    Cy = Size - 1 - Y;
    Cz = Size - 1 - X;
    break;
  case 1: // -X.
    Cx = 0;
    Cy = Size - 1 - Y;
    Cz = X;
    break;
  case 2: // +Y.
    Cy = Size - 1;
    Cx = X;
    Cz = Y;
    break;
  case 3: // -Y.
    Cy = 0;
    Cx = X;
    Cz = Size - 1 - Y;
    break;
  case 4: // +Z.
    Cz = Size - 1;
    Cx = X;
    Cy = Size - 1 - Y;
    break;
  default: // -Z (5).
    Cz = 0;
    Cx = Size - 1 - X;
    Cy = Size - 1 - Y;
    break;
  }
  if (Cx == -1) {
    R.Face = 1; // -X.
    R.Y = Size - 1 - Cy;
    R.X = Cz;
  } else if (Cx == Size) {
    R.Face = 0; // +X.
    R.Y = Size - 1 - Cy;
    R.X = Size - 1 - Cz;
  } else if (Cy == -1) {
    R.Face = 3; // -Y.
    R.X = Cx;
    R.Y = Size - 1 - Cz;
  } else if (Cy == Size) {
    R.Face = 2; // +Y.
    R.X = Cx;
    R.Y = Cz;
  } else if (Cz == -1) {
    R.Face = 5; // -Z.
    R.X = Size - 1 - Cx;
    R.Y = Size - 1 - Cy;
  } else { // Cz == Size (the only remaining case for a single
           // out-of-bounds axis).
    R.Face = 4; // +Z.
    R.X = Cx;
    R.Y = Size - 1 - Cy;
  }
  return R;
}

// Fetches one of the (up to) four texels a seamless cube bilinear tap
// blends, remapping across a face edge via `femeRTRemapCubeEdgeCoords`
// above when `(X, Y)` falls outside `BaseFace`'s own bounds. `LayerBase`
// is `0` for a plain `Cube` image, or `CubeIndex * 6` for a `CubeArray`
// element (mirroring `femeCpuImageSampleCubeArrayV4F32`'s own `Layer`
// computation), so the remapped face's own array layer stays within the
// same cube array element. Sets `*Ambiguous` and returns an unspecified
// value for the corner (both-axes-out-of-bounds) case; the caller
// resolves it afterward.
__attribute__((always_inline)) static FemeRTv4f32
femeRTFetchCubeSeamlessTexel(const FemeRTImageDescriptor *Img, uint32_t Level,
                             uint32_t LayerBase, uint32_t BaseFace, int32_t X,
                             int32_t Y, int32_t Size, _Bool *Ambiguous,
                             _Bool ApplySwizzle) {
  FemeRTCubeEdgeCoords C = femeRTRemapCubeEdgeCoords(BaseFace, X, Y, Size);
  if (C.Ambiguous) {
    *Ambiguous = 1;
    FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
    return Zero;
  }
  *Ambiguous = 0;
  static const float NoBorder[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  return femeRTFetchTexel2D(Img, Level, LayerBase + C.Face, C.X, C.Y,
                           /*Sample=*/0, /*UseBorder=*/0, NoBorder,
                           ApplySwizzle);
}

// The four raw (unblended) integer texel coordinates and fractional
// bilinear weights a seamless cube tap at normalized `(U, V)` needs --
// deliberately *not* reusing `femeRTComputeBilinearSupport` above, since
// that helper's own `X0`/`X1`/`Y0`/`Y1` are already address-mode-resolved
// (clamped/wrapped) by `femeRTApplyAddressMode`, leaving no way to tell
// whether a corner was originally out of `[0, Size)` bounds -- exactly
// the information `femeRTRemapCubeEdgeCoords` above needs.
typedef struct {
  int32_t X0, X1, Y0, Y1;
  float Wx, Wy;
} FemeRTCubeBilinearSupport;

__attribute__((always_inline)) static FemeRTCubeBilinearSupport
femeRTComputeCubeBilinearSupport(const FemeRTImageDescriptor *Img, float U,
                                 float V, uint32_t Level, int32_t *OutSize) {
  uint32_t LevelSize = femeRTMipExtent(Img->Width, Level); // Square faces.
  float TexelU = U * (float)LevelSize - 0.5f;
  float TexelV = V * (float)LevelSize - 0.5f;
  float FloorU = __builtin_floorf(TexelU);
  float FloorV = __builtin_floorf(TexelV);
  FemeRTCubeBilinearSupport S;
  S.Wx = TexelU - FloorU;
  S.Wy = TexelV - FloorV;
  S.X0 = (int32_t)FloorU;
  S.X1 = S.X0 + 1;
  S.Y0 = (int32_t)FloorV;
  S.Y1 = S.Y0 + 1;
  *OutSize = (int32_t)LevelSize;
  return S;
}

// Seamlessly bilinear-filters a cube(-array) image at `(U, V)` against a
// single mip level, the `LINEAR`-filter counterpart of
// `femeRTSamplePoint2D` used for `NEAREST` (which needs no seamless
// handling at all -- see `femeRTSampleFilteredCube`'s own comment).
// Mirrors `femeRTSampleLinear2D`'s own four-tap blend shape, but each tap
// remaps across a face edge via `femeRTFetchCubeSeamlessTexel` instead of
// clamping in place, and the (at most one) doubly-out-of-bounds corner
// tap is resolved by averaging the other three raw texel colors,
// mirroring VK-GL-CTS's own `getCubeLinearSamples`.
__attribute__((always_inline)) static FemeRTv4f32
femeRTSampleCubeLinearAtLevel(const FemeRTImageDescriptor *Img, uint32_t Level,
                             uint32_t LayerBase, uint32_t BaseFace, float U,
                             float V) {
  int32_t Size;
  FemeRTCubeBilinearSupport S =
      femeRTComputeCubeBilinearSupport(Img, U, V, Level, &Size);
  _Bool Amb00 = 0, Amb10 = 0, Amb01 = 0, Amb11 = 0;
  FemeRTv4f32 T00 = femeRTFetchCubeSeamlessTexel(Img, Level, LayerBase,
                                                BaseFace, S.X0, S.Y0, Size,
                                                &Amb00, /*ApplySwizzle=*/1);
  FemeRTv4f32 T10 = femeRTFetchCubeSeamlessTexel(Img, Level, LayerBase,
                                                BaseFace, S.X1, S.Y0, Size,
                                                &Amb10, /*ApplySwizzle=*/1);
  FemeRTv4f32 T01 = femeRTFetchCubeSeamlessTexel(Img, Level, LayerBase,
                                                BaseFace, S.X0, S.Y1, Size,
                                                &Amb01, /*ApplySwizzle=*/1);
  FemeRTv4f32 T11 = femeRTFetchCubeSeamlessTexel(Img, Level, LayerBase,
                                                BaseFace, S.X1, S.Y1, Size,
                                                &Amb11, /*ApplySwizzle=*/1);
  // At most one of the four taps can ever be the doubly-out-of-bounds
  // corner (a bilinear footprint spans at most a 2x2 texel square, which
  // can straddle at most one cube corner at a time).
  if (Amb00)
    T00 = (T10 + T01 + T11) * (1.0f / 3.0f);
  else if (Amb10)
    T10 = (T00 + T01 + T11) * (1.0f / 3.0f);
  else if (Amb01)
    T01 = (T00 + T10 + T11) * (1.0f / 3.0f);
  else if (Amb11)
    T11 = (T00 + T10 + T01) * (1.0f / 3.0f);
  FemeRTv4f32 Top = T00 + (T10 - T00) * S.Wx;
  FemeRTv4f32 Bottom = T01 + (T11 - T01) * S.Wx;
  return Top + (Bottom - Top) * S.Wy;
}

// The cube(-array) counterpart of `femeRTSampleFiltered2D` above,
// dispatching to the seamless `femeRTSampleCubeLinearAtLevel` for a
// `LINEAR` filter, or the ordinary (non-seamless) `femeRTSamplePoint2D`
// for `NEAREST` -- a single face's own nearest edge texel is already
// spec-correct with no cross-face blending needed (Vulkan's `NEAREST`
// filter never has a fractional footprint that could straddle a face
// edge), mirroring VK-GL-CTS's own `sampleCubeSeamlessNearest`
// short-circuit. `Samp` must already have `AddressU`/`AddressV` forced to
// `ClampToEdge` by the caller (see `femeCpuImageSampleCubeV4F32`'s own
// comment) for the `NEAREST` path's own `femeRTApplyAddressMode` call;
// the `LINEAR` path never consults `Samp->AddressU`/`AddressV` at all,
// since seamless filtering's own cross-face remapping entirely replaces
// ordinary address-mode clamping at a cube face's edge.
__attribute__((always_inline)) static FemeRTv4f32
femeRTSampleFilteredCube(const FemeRTImageDescriptor *Img,
                         const FemeRTSamplerDescriptor *Samp, float U, float V,
                         uint32_t LayerBase, uint32_t BaseFace,
                         float ClampedLod) {
  _Bool UseLinear = femeRTUseLinearFilter(ClampedLod, Samp);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(Img, ClampedLod);
  _Bool Trilinear = Samp->MipFilter == 1 && MipPlan.Level0 != MipPlan.Level1;
  uint32_t Level0 = Trilinear ? MipPlan.Level0 : femeRTNearestMipLevel(MipPlan);
  FemeRTv4f32 Lo =
      UseLinear
          ? femeRTSampleCubeLinearAtLevel(Img, Level0, LayerBase, BaseFace, U,
                                         V)
          : femeRTSamplePoint2D(Img, Samp, U, V, Level0, LayerBase + BaseFace,
                                /*OffsetX=*/0, /*OffsetY=*/0);
  if (!Trilinear)
    return Lo;
  FemeRTv4f32 Hi =
      UseLinear ? femeRTSampleCubeLinearAtLevel(Img, MipPlan.Level1, LayerBase,
                                              BaseFace, U, V)
               : femeRTSamplePoint2D(Img, Samp, U, V, MipPlan.Level1,
                                     LayerBase + BaseFace, /*OffsetX=*/0,
                                     /*OffsetY=*/0);
  return Lo + (Hi - Lo) * MipPlan.Frac;
}

// The depth-comparison counterpart of `femeRTSampleFilteredCube` above --
// mirrors `femeRTSampleCmp2DAtLevel`'s own point/bilinear split, but the
// `LINEAR` branch remaps each of the four taps across a face edge the
// same way `femeRTSampleCubeLinearAtLevel` does, applying `Samp`'s
// `CompareFunc` to each in-bounds tap *before* blending (matching
// VK-GL-CTS's own `sampleCubeSeamlessLinearCompare`, which compares each
// tap first and only then averages the three known per-tap compare
// results for a doubly-out-of-bounds corner -- not equivalent to
// averaging raw depth values and comparing once, since the compare
// function need not be linear).
__attribute__((always_inline)) static float
femeRTSampleCmpCubeAtLevel(const FemeRTImageDescriptor *Img,
                          const FemeRTSamplerDescriptor *Samp, float U,
                          float V, uint32_t LayerBase, uint32_t BaseFace,
                          uint32_t Level, float Dref, _Bool UseLinear) {
  _Bool IsFixedPointDepth = femeRTIsFixedPointDepthFormat(Img->Format);
  if (!UseLinear) { // Point (nearest): no seamless handling needed.
    uint32_t LevelSize = femeRTMipExtent(Img->Width, Level);
    int32_t X = (int32_t)__builtin_floorf(U * (float)LevelSize);
    int32_t Y = (int32_t)__builtin_floorf(V * (float)LevelSize);
    _Bool BorderX = 0, BorderY = 0;
    int32_t AddrX = femeRTApplyAddressMode(X, (int32_t)LevelSize,
                                           Samp->AddressU, &BorderX);
    int32_t AddrY = femeRTApplyAddressMode(Y, (int32_t)LevelSize,
                                           Samp->AddressV, &BorderY);
    FemeRTv4f32 T = femeRTFetchTexel2D(Img, Level, LayerBase + BaseFace,
                                      AddrX, AddrY, /*Sample=*/0,
                                      BorderX || BorderY, Samp->BorderColor,
                                      /*ApplySwizzle=*/0);
    return femeRTApplyCompare(Samp->CompareFunc, Dref, T[0], IsFixedPointDepth);
  }

  int32_t Size;
  FemeRTCubeBilinearSupport S =
      femeRTComputeCubeBilinearSupport(Img, U, V, Level, &Size);
  _Bool Amb00 = 0, Amb10 = 0, Amb01 = 0, Amb11 = 0;
  FemeRTv4f32 T00 = femeRTFetchCubeSeamlessTexel(Img, Level, LayerBase,
                                                BaseFace, S.X0, S.Y0, Size,
                                                &Amb00, /*ApplySwizzle=*/0);
  FemeRTv4f32 T10 = femeRTFetchCubeSeamlessTexel(Img, Level, LayerBase,
                                                BaseFace, S.X1, S.Y0, Size,
                                                &Amb10, /*ApplySwizzle=*/0);
  FemeRTv4f32 T01 = femeRTFetchCubeSeamlessTexel(Img, Level, LayerBase,
                                                BaseFace, S.X0, S.Y1, Size,
                                                &Amb01, /*ApplySwizzle=*/0);
  FemeRTv4f32 T11 = femeRTFetchCubeSeamlessTexel(Img, Level, LayerBase,
                                                BaseFace, S.X1, S.Y1, Size,
                                                &Amb11, /*ApplySwizzle=*/0);
  float C00 = femeRTApplyCompare(Samp->CompareFunc, Dref, T00[0], IsFixedPointDepth);
  float C10 = femeRTApplyCompare(Samp->CompareFunc, Dref, T10[0], IsFixedPointDepth);
  float C01 = femeRTApplyCompare(Samp->CompareFunc, Dref, T01[0], IsFixedPointDepth);
  float C11 = femeRTApplyCompare(Samp->CompareFunc, Dref, T11[0], IsFixedPointDepth);
  if (Amb00)
    C00 = (C10 + C01 + C11) * (1.0f / 3.0f);
  else if (Amb10)
    C10 = (C00 + C01 + C11) * (1.0f / 3.0f);
  else if (Amb01)
    C01 = (C00 + C10 + C11) * (1.0f / 3.0f);
  else if (Amb11)
    C11 = (C00 + C10 + C01) * (1.0f / 3.0f);
  float Top = C00 + (C10 - C00) * S.Wx;
  float Bottom = C01 + (C11 - C01) * S.Wx;
  return Top + (Bottom - Top) * S.Wy;
}

// (Roadmap L56) Shared by both `femeCpuImageSampleCubeV4F32` and
// `femeCpuImageSampleCubeArrayV4F32` below: an explicit-LOD sample's
// `ClampedLod` is the same simple bias-and-clamp `femeRTComputeClampedLod`
// already computes for every other shape; an implicit-LOD sample instead
// needs a real, non-always-zero LOD derived from this sample's own
// screen-space footprint -- computed by turning the caller's own raw
// direction-vector derivatives into face-local `(U, V)` derivatives
// (`femeRTComputeCubeUVDerivatives`) and feeding those into the same
// `femeRTPlanImplicitLod` a `Plain2D` implicit sample already uses
// unmodified. Before this fix, every implicit-LOD cube(-array) sample
// used the `UseExplicitLod=0` branch of `femeRTComputeClampedLod` alone,
// which always resolves to `Lod=0` (mip level 0) regardless of any real
// minification -- the actual root cause of
// `dEQP-VK.texture.filtering.cube.combinations.linear_mipmap_linear.*`'s
// 0/25 failure (confirmed via a real `deqp-vk` capture showing this
// sample path never receives the fragment stage's own screen-space
// derivatives at all, unlike `Plain2D`'s roadmap H7i path).
__attribute__((always_inline)) static float
femeRTComputeCubeClampedLod(const FemeRTImageDescriptor *Img,
                           const FemeRTSamplerDescriptor *Samp,
                           FemeRTCubeFace CF, float Lod, _Bool UseExplicitLod,
                           float DDirXdX, float DDirXdY, float DDirYdX,
                           float DDirYdY, float DDirZdX, float DDirZdY,
                           float MinLodClamp, float Bias) {
  if (UseExplicitLod)
    return femeRTComputeClampedLod(Lod, /*UseExplicitLod=*/1, Samp,
                                   MinLodClamp, Bias);
  FemeRTCubeUVDerivatives D =
      femeRTComputeCubeUVDerivatives(CF.Face, CF.RawMajor, DDirXdX, DDirXdY,
                                     DDirYdX, DDirYdY, DDirZdX, DDirZdY);
  return femeRTPlanImplicitLod(Img, Samp, D.DUdX, D.DUdY, D.DVdX, D.DVdY,
                              MinLodClamp, Bias)
      .ClampedLod;
}

// `feme.cpu.image.querylod.cube.v2f32` (roadmap H124u): `Cube`'s own
// counterpart of `femeCpuImageQueryLod2DV2F32` above -- HLSL's
// `TextureCube::CalculateLevelOfDetail`/`CalculateLevelOfDetailUnclamped`
// (`OpImageQueryLod` against a `Cube`-shaped handle). Unlike `QueryLod2D`,
// there is no face-local `(U, V)` derivative pair to consult directly:
// the caller only has the direction vector `(DirX, DirY, DirZ)` and its
// own raw screen-space derivatives, so this first selects the cube face
// the direction vector lands on (`femeRTSelectCubeFace`, the same helper
// `femeCpuImageSampleCubeV4F32` uses), then remaps those raw derivatives
// into that face's own `(U, V)` derivative pair
// (`femeRTComputeCubeUVDerivatives`, mirroring
// `femeRTComputeCubeClampedLod`'s own implicit-LOD branch above) before
// handing them to the same `femeRTComputeUnclampedQueryLod`/
// `femeRTComputeClampedQueryLevel` pair `QueryLod2D` already uses
// unmodified -- a cube face's own UV derivatives measure minification
// exactly the same way a plain 2D image's do once face selection has
// picked out which single face's texel grid is actually being sampled.
// The `<2 x float>` result's own lane convention (lane 0 clamped level,
// lane 1 raw unclamped LOD) is identical to `QueryLod2D`'s.
FemeRTv2f32 femeCpuImageQueryLodCubeV2F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float DDirXdX, float DDirXdY, float DDirYdX, float DDirYdY,
    float DDirZdX, float DDirZdY,
    _Bool Mask) asm("feme.cpu.image.querylod.cube.v2f32");

__attribute__((always_inline)) FemeRTv2f32 femeCpuImageQueryLodCubeV2F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float DDirXdX, float DDirXdY, float DDirYdX, float DDirYdY,
    float DDirZdX, float DDirZdY, _Bool Mask) {
  FemeRTv2f32 Zero = {0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u) ||
      Img.ArrayLayers < 6) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);

  FemeRTCubeFace CF = femeRTSelectCubeFace(DirX, DirY, DirZ);
  FemeRTCubeUVDerivatives D =
      femeRTComputeCubeUVDerivatives(CF.Face, CF.RawMajor, DDirXdX, DDirXdY,
                                     DDirYdX, DDirYdY, DDirZdX, DDirZdY);
  float UnclampedLod =
      femeRTComputeUnclampedQueryLod(&Img, D.DUdX, D.DUdY, D.DVdX, D.DVdY);
  // Same `-infinity` no-op `InstructionMinLod`/zero `InstructionBias`
  // convention `femeCpuImageQueryLod2DV2F32` uses -- see its own doc.
  float ClampedLod =
      femeRTComputeClampedLod(UnclampedLod,
                              /*UseExplicitLod=*/1, &Samp,
                              /*InstructionMinLod=*/-__builtin_inff(),
                              /*InstructionBias=*/0.0f);
  float ClampedLevel = femeRTComputeClampedQueryLevel(&Img, &Samp, ClampedLod);
  return (FemeRTv2f32){ClampedLevel, UnclampedLod};
}

// `feme.cpu.image.sample.cube.v4f32` (roadmap H7b-a): samples a
// `TextureCube` sampled image at direction vector `(DirX, DirY, DirZ)`,
// converted to a face index (addressed as `femeRTSamplePoint2D`/
// `femeRTSampleLinear2D`'s own `Layer` parameter) and 2D UV by
// `femeRTSelectCubeFace` above. A cube face's own edges never wrap or
// mirror across a face boundary the way a plain 2D image's row/column
// wraps -- there is no "next" face along a U/V axis -- but (roadmap L53)
// a `LINEAR`-filtered tap that falls just outside a face's own bounds
// does *not* simply clamp back onto that face's own edge texel either:
// `femeRTSampleFilteredCube` blends it with its true geometric neighbor
// across the shared cube edge instead, matching Vulkan's own
// spec-mandated default seamless cube-map filtering behaviour (see that
// function's own comment, and `femeRTRemapCubeEdgeCoords` above it).
// `DDirXdX`/`DDirXdY`/`DDirYdX`/`DDirYdY`/`DDirZdX`/`DDirZdY` (roadmap
// L56) are the caller's own screen-space partial derivatives of the
// direction vector's three components, consulted only for an
// implicit-LOD sample (`femeRTComputeCubeClampedLod` above); a caller
// with none to give (a non-fragment stage, or an explicit-LOD sample)
// passes zero constants. `Bias` (roadmap L58) is the same per-instruction
// `Bias` image operand `femeCpuImageSample2DV4F32` documents.
FemeRTv4f32 femeCpuImageSampleCubeV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float DDirXdX, float DDirXdY, float DDirYdX, float DDirYdY,
    float DDirZdX, float DDirZdY, float Lod, _Bool UseExplicitLod, float Bias,
    float MinLodClamp, _Bool Mask) asm("feme.cpu.image.sample.cube.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageSampleCubeV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float DDirXdX, float DDirXdY, float DDirYdX, float DDirYdY,
    float DDirZdX, float DDirZdY, float Lod, _Bool UseExplicitLod, float Bias,
    float MinLodClamp, _Bool Mask) {
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
  FemeRTCubeFace CF = femeRTSelectCubeFace(DirX, DirY, DirZ);
  float ClampedLod = femeRTComputeCubeClampedLod(
      &Img, &Samp, CF, Lod, UseExplicitLod, DDirXdX, DDirXdY, DDirYdX,
      DDirYdY, DDirZdX, DDirZdY, MinLodClamp, Bias);
  return femeRTSampleFilteredCube(&Img, &Samp, CF.U, CF.V, /*LayerBase=*/0,
                                CF.Face, ClampedLod);
}

// (Roadmap L125(b)) The `Cube` counterpart of `femeCpuImageSample2DV4I32`
// above, for `feme.cpu.image.sample.cube.v4i32` -- mirrors
// `femeCpuImageSampleCubeV4F32`'s own face-selection/address-forcing
// structure immediately above (hence its placement here, after
// `femeRTSelectCubeFace`/`femeRTComputeCubeClampedLod`'s own definitions,
// both `static` and required by C to precede any caller), but simplified
// to `NEAREST`-only fetch, no `Bias`/`MinLodClamp`/derivative operands
// (mirroring every other `*I32` kind's own restriction -- `Lod` already
// defaults to a constant `0.0` at the call site for an implicit-LOD
// caller, so `femeRTComputeCubeClampedLod`'s own derivative-based
// implicit-LOD branch is never reached; `UseExplicitLod=1` unconditionally
// selects its simple bias-and-clamp branch instead, exactly like every
// prior `*I32` kind's identical choice). Reuses `femeRTFetchTexel2DI32`
// directly (with `CF.Face` as `Layer`) rather than
// `femeRTSampleFilteredCube`, since a `NEAREST` cube fetch needs no
// seamless cross-face blending at all (a single nearest texel is already
// spec-correct, mirroring `femeRTSampleFilteredCube`'s own `NEAREST`
// short-circuit to the non-seamless `femeRTSamplePoint2D` path) -- so, as
// with `Array2D`/`Plain3D` before it, no new low-level texel-fetch helper
// is needed here either.
FemeRTv4i32 femeCpuImageSampleCubeV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float Lod,
    _Bool Mask) asm("feme.cpu.image.sample.cube.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuImageSampleCubeV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float Lod, _Bool Mask) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u) || Img.ArrayLayers < 6) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  Samp.AddressU = 2; // ClampToEdge -- see femeCpuImageSampleCubeV4F32.
  Samp.AddressV = 2;
  FemeRTCubeFace CF = femeRTSelectCubeFace(DirX, DirY, DirZ);
  // `MinLodClamp`/`Bias` are always the no-op values here (`-INFINITY`/
  // `0.0f`), mirroring `femeCpuImageSample2DV4I32`'s own choice above;
  // `UseExplicitLod=1` makes `femeRTComputeCubeClampedLod` ignore every
  // derivative argument, so zero constants are passed for those too.
  float ClampedLod = femeRTComputeCubeClampedLod(
      &Img, &Samp, CF, Lod, /*UseExplicitLod=*/1, 0.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 0.0f, -__builtin_inff(), 0.0f);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(&Img, ClampedLod);
  uint32_t Level = femeRTNearestMipLevel(MipPlan);
  uint32_t LevelWidth = femeRTMipExtent(Img.Width, Level);
  uint32_t LevelHeight = femeRTMipExtent(Img.Height, Level);
  int32_t X = (int32_t)__builtin_floorf(CF.U * (float)LevelWidth);
  int32_t Y = (int32_t)__builtin_floorf(CF.V * (float)LevelHeight);
  _Bool BorderX = 0, BorderY = 0;
  // `Samp.AddressU`/`AddressV` are forced to `ClampToEdge` (mode 2) just
  // above, which `femeRTApplyAddressMode` never treats as
  // out-of-bounds -- only `ClampToBorder` (mode 3) ever sets
  // `BorderX`/`BorderY`, so both stay `0` here unconditionally; no
  // integer border-color fallback is needed the way `Plain2D`'s/
  // `Array2D`'s/`Plain1D`'s/`Array1D`'s/`Plain3D`'s own sampler-controlled
  // address mode requires.
  int32_t AddrX = femeRTApplyAddressMode(X, (int32_t)LevelWidth,
                                         Samp.AddressU, &BorderX);
  int32_t AddrY = femeRTApplyAddressMode(Y, (int32_t)LevelHeight,
                                         Samp.AddressV, &BorderY);
  return femeRTFetchTexel2DI32(&Img, Level, /*Layer=*/CF.Face, AddrX, AddrY,
                               /*Sample=*/0, /*ApplySwizzle=*/1);
}

// `feme.cpu.image.gathercmp.cube.v4f32` (roadmap H124r): `TextureCube`
// depth-comparison gather -- SPIR-V's `OpImageDrefGather`, HLSL's
// `TextureCube::GatherCmp()`. Structurally identical to
// `femeCpuImageGatherCmp2DV4F32` (same fixed result ordering, same
// mip-level-0-only restriction), except the direction vector
// `(DirX, DirY, DirZ)` is first resolved to a face and face-local
// `(U, V)` coordinate by `femeRTSelectCubeFace` (the same helper
// `femeCpuImageSampleCubeV4F32` above uses). Roadmap L125(j): a gather
// footprint that straddles a cube face edge must remap into the
// adjacent face exactly like a seamless blended sample does -- Vulkan
// (unlike desktop GL) makes cube-map seamless filtering mandatory, and
// `dEQP-VK.glsl.texture_gather.graphics.basic.cube.*` genuinely
// exercises footprints that straddle a face edge (confirmed via a
// `filter_mode` sanity case, identity-swizzle, that failed before this
// fix and passes after). Fixed by switching from
// `femeRTComputeBilinearSupport`/`femeRTFetchTexel2D` (this row's own
// prior "clamped in place" approximation, documented above as
// unmotivated by any known CTS case -- now known to be wrong) to
// `femeRTComputeCubeBilinearSupport`/`femeRTFetchCubeSeamlessTexel`,
// the exact same seamless cross-face remap
// `femeRTSampleCubeLinearAtLevel` already uses for a blended sample,
// including its doubly-out-of-bounds corner-averaging rule -- mirroring
// VK-GL-CTS's own `TextureCubeView::gather`, which reuses its sampling
// path's own `getCubeLinearSamples` verbatim rather than a separate
// gather-specific footprint. A cube face has no sampler-controlled
// address mode to force to `ClampToEdge` any more, since
// `femeRTFetchCubeSeamlessTexel` never consults one at all (a cube
// gather footprint remaps across faces unconditionally, the same way a
// sample does).
FemeRTv4f32 femeCpuImageGatherCmpCubeV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float Dref,
    _Bool Mask) asm("feme.cpu.image.gathercmp.cube.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageGatherCmpCubeV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float Dref, _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u) ||
      Img.ArrayLayers < 6) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  FemeRTCubeFace CF = femeRTSelectCubeFace(DirX, DirY, DirZ);
  _Bool IsFixedPointDepth = femeRTIsFixedPointDepthFormat(Img.Format);
  int32_t Size;
  FemeRTCubeBilinearSupport S =
      femeRTComputeCubeBilinearSupport(&Img, CF.U, CF.V, /*Level=*/0, &Size);
  _Bool Amb00 = 0, Amb10 = 0, Amb01 = 0, Amb11 = 0;
  FemeRTv4f32 T00 = femeRTFetchCubeSeamlessTexel(
      &Img, /*Level=*/0, /*LayerBase=*/0, CF.Face, S.X0, S.Y0, Size, &Amb00,
      /*ApplySwizzle=*/0);
  FemeRTv4f32 T10 = femeRTFetchCubeSeamlessTexel(
      &Img, /*Level=*/0, /*LayerBase=*/0, CF.Face, S.X1, S.Y0, Size, &Amb10,
      /*ApplySwizzle=*/0);
  FemeRTv4f32 T01 = femeRTFetchCubeSeamlessTexel(
      &Img, /*Level=*/0, /*LayerBase=*/0, CF.Face, S.X0, S.Y1, Size, &Amb01,
      /*ApplySwizzle=*/0);
  FemeRTv4f32 T11 = femeRTFetchCubeSeamlessTexel(
      &Img, /*Level=*/0, /*LayerBase=*/0, CF.Face, S.X1, S.Y1, Size, &Amb11,
      /*ApplySwizzle=*/0);
  // At most one of the four taps can ever be the doubly-out-of-bounds
  // corner -- see femeRTSampleCubeLinearAtLevel's own identical comment.
  if (Amb00)
    T00 = (T10 + T01 + T11) * (1.0f / 3.0f);
  else if (Amb10)
    T10 = (T00 + T01 + T11) * (1.0f / 3.0f);
  else if (Amb01)
    T01 = (T00 + T10 + T11) * (1.0f / 3.0f);
  else if (Amb11)
    T11 = (T00 + T10 + T01) * (1.0f / 3.0f);
  FemeRTv4f32 Result;
  Result[0] =
      femeRTApplyCompare(Samp.CompareFunc, Dref, T01[0], IsFixedPointDepth);
  Result[1] =
      femeRTApplyCompare(Samp.CompareFunc, Dref, T11[0], IsFixedPointDepth);
  Result[2] =
      femeRTApplyCompare(Samp.CompareFunc, Dref, T10[0], IsFixedPointDepth);
  Result[3] =
      femeRTApplyCompare(Samp.CompareFunc, Dref, T00[0], IsFixedPointDepth);
  return Result;
}

// `feme.cpu.image.gather.cube.v4f32` (roadmap H124r): `TextureCube`
// non-depth-comparison gather -- SPIR-V's `OpImageGather`, HLSL's
// `TextureCube::Gather{,Red,Green,Blue,Alpha}()`. Structurally identical
// to `femeCpuImageGatherCmpCubeV4F32` above (same face-selection/
// footprint/result ordering/mip-level-0-only restriction), but each
// result component is one of the four sampled texel's own `Component`
// channel (0=R, 1=G, 2=B, 3=A), never a depth comparison -- mirroring
// `femeCpuImageGather2DV4F32`'s own relationship to
// `femeCpuImageGatherCmp2DV4F32`.
FemeRTv4f32 femeCpuImageGatherCubeV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, int32_t Component,
    _Bool Mask) asm("feme.cpu.image.gather.cube.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageGatherCubeV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, int32_t Component, _Bool Mask) {
  FemeRTv4f32 Zero = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u) ||
      Img.ArrayLayers < 6) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  Samp.AddressU = 2; // ClampToEdge -- see femeCpuImageGatherCmpCubeV4F32.
  Samp.AddressV = 2;
  FemeRTCubeFace CF = femeRTSelectCubeFace(DirX, DirY, DirZ);
  uint32_t Chan = (uint32_t)Component > 3u ? 3u : (uint32_t)Component;
  FemeRTBilinearSupport S = femeRTComputeBilinearSupport(
      &Img, CF.U, CF.V, &Samp, /*Level=*/0, /*OffsetX=*/0, /*OffsetY=*/0);
  // Roadmap L125(g): see femeCpuImageGather2DV4F32's own comment above --
  // each gathered neighbor is swizzled before `Chan` selects from it.
  FemeRTv4f32 T00 = femeRTFetchTexel2D(&Img, /*Level=*/0, CF.Face, S.X0, S.Y0,
                                       /*Sample=*/0, S.BorderX0 || S.BorderY0,
                                       Samp.BorderColor, /*ApplySwizzle=*/1);
  FemeRTv4f32 T10 = femeRTFetchTexel2D(&Img, /*Level=*/0, CF.Face, S.X1, S.Y0,
                                       /*Sample=*/0, S.BorderX1 || S.BorderY0,
                                       Samp.BorderColor, /*ApplySwizzle=*/1);
  FemeRTv4f32 T01 = femeRTFetchTexel2D(&Img, /*Level=*/0, CF.Face, S.X0, S.Y1,
                                       /*Sample=*/0, S.BorderX0 || S.BorderY1,
                                       Samp.BorderColor, /*ApplySwizzle=*/1);
  FemeRTv4f32 T11 = femeRTFetchTexel2D(&Img, /*Level=*/0, CF.Face, S.X1, S.Y1,
                                       /*Sample=*/0, S.BorderX1 || S.BorderY1,
                                       Samp.BorderColor, /*ApplySwizzle=*/1);
  FemeRTv4f32 Result;
  Result[0] = T01[Chan];
  Result[1] = T11[Chan];
  Result[2] = T10[Chan];
  Result[3] = T00[Chan];
  return Result;
}

// `feme.cpu.image.sample.cubearray.v4f32` (roadmap H7b-a): the
// `TextureCubeArray` counterpart of `feme.cpu.image.sample.cube.v4f32`
// above, adding `ArrayLayer` (SPIR-V's own arrayed-cube coordinate
// convention: a float selecting which six-layer cube element of the
// array, rounded to nearest and clamped by `femeRTRoundClampLayer`).
// `Img.ArrayLayers` is the whole array's own total layer count (roadmap
// H7b's own descriptor materialization already reports this, e.g. 12 for
// a two-element cube array); dividing by 6 recovers the number of
// selectable cube elements. `Bias`/`MinLodClamp` (roadmap L60(a)) mirror
// `femeCpuImageSampleCubeV4F32`'s own `Bias`/`MinLodClamp` parameters --
// before this fix, this entry point always passed a hardcoded
// `MinLodClamp=-inf`/`Bias=0.0f` no-op pair to
// `femeRTComputeCubeClampedLod`, silently dropping any real bias/clamp a
// caller supplied.
FemeRTv4f32 femeCpuImageSampleCubeArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float DDirXdX, float DDirXdY, float DDirYdX, float DDirYdY,
    float DDirZdX, float DDirZdY, float ArrayLayer, float Lod,
    _Bool UseExplicitLod, float Bias, float MinLodClamp,
    _Bool Mask) asm("feme.cpu.image.sample.cubearray.v4f32");

__attribute__((always_inline)) FemeRTv4f32 femeCpuImageSampleCubeArrayV4F32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float DDirXdX, float DDirXdY, float DDirYdX, float DDirYdY,
    float DDirZdX, float DDirZdY, float ArrayLayer, float Lod,
    _Bool UseExplicitLod, float Bias, float MinLodClamp, _Bool Mask) {
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
  FemeRTCubeFace CF = femeRTSelectCubeFace(DirX, DirY, DirZ);
  float ClampedLod = femeRTComputeCubeClampedLod(
      &Img, &Samp, CF, Lod, UseExplicitLod, DDirXdX, DDirXdY, DDirYdX,
      DDirYdY, DDirZdX, DDirZdY, MinLodClamp, Bias);
  uint32_t NumCubes = Img.ArrayLayers / 6;
  uint32_t CubeIndex = femeRTRoundClampLayer(NumCubes, ArrayLayer);
  return femeRTSampleFilteredCube(&Img, &Samp, CF.U, CF.V,
                                /*LayerBase=*/CubeIndex * 6, CF.Face,
                                ClampedLod);
}

// (Roadmap L125(b)) The `CubeArray` counterpart of
// `femeCpuImageSampleCubeV4I32` above, for
// `feme.cpu.image.sample.cubearray.v4i32` -- mirrors
// `femeCpuImageSampleCubeArrayV4F32`'s own relationship to
// `femeCpuImageSampleCubeV4F32` immediately above: adds a float
// `ArrayLayer` operand, rounded to nearest and clamped to a valid cube
// element via the same `femeRTRoundClampLayer` helper, then folded into
// `femeRTFetchTexel2DI32`'s own `Layer` as `CubeIndex * 6 + CF.Face`
// (mirroring `femeRTSampleFilteredCube`'s own identical `LayerBase +
// BaseFace` addition for its `NEAREST` path). Otherwise identical to
// `femeCpuImageSampleCubeV4I32`: `UseExplicitLod=1`/zero derivatives (no
// implicit-LOD derivative logic needed), forced `ClampToEdge` addressing
// (so no border-color fallback branch), and a direct
// `femeRTFetchTexel2DI32` point-sample (no seamless cross-face blending
// needed for `NEAREST`).
FemeRTv4i32 femeCpuImageSampleCubeArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float ArrayLayer, float Lod,
    _Bool Mask) asm("feme.cpu.image.sample.cubearray.v4i32");

__attribute__((always_inline)) FemeRTv4i32 femeCpuImageSampleCubeArrayV4I32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float ArrayLayer, float Lod, _Bool Mask) {
  FemeRTv4i32 Zero = {0, 0, 0, 0};
  if (!Mask)
    return Zero;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u) || Img.ArrayLayers < 6) // FEME_IMAGE_SAMPLED.
    return Zero;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  Samp.AddressU = 2; // ClampToEdge -- see femeCpuImageSampleCubeV4F32.
  Samp.AddressV = 2;
  FemeRTCubeFace CF = femeRTSelectCubeFace(DirX, DirY, DirZ);
  float ClampedLod = femeRTComputeCubeClampedLod(
      &Img, &Samp, CF, Lod, /*UseExplicitLod=*/1, 0.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 0.0f, -__builtin_inff(), 0.0f);
  uint32_t NumCubes = Img.ArrayLayers / 6;
  uint32_t CubeIndex = femeRTRoundClampLayer(NumCubes, ArrayLayer);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(&Img, ClampedLod);
  uint32_t Level = femeRTNearestMipLevel(MipPlan);
  uint32_t LevelWidth = femeRTMipExtent(Img.Width, Level);
  uint32_t LevelHeight = femeRTMipExtent(Img.Height, Level);
  int32_t X = (int32_t)__builtin_floorf(CF.U * (float)LevelWidth);
  int32_t Y = (int32_t)__builtin_floorf(CF.V * (float)LevelHeight);
  _Bool BorderX = 0, BorderY = 0;
  // `Samp.AddressU`/`AddressV` are forced to `ClampToEdge` (mode 2) just
  // above, which `femeRTApplyAddressMode` never treats as
  // out-of-bounds -- see `femeCpuImageSampleCubeV4I32`'s own identical
  // comment.
  int32_t AddrX = femeRTApplyAddressMode(X, (int32_t)LevelWidth,
                                         Samp.AddressU, &BorderX);
  int32_t AddrY = femeRTApplyAddressMode(Y, (int32_t)LevelHeight,
                                         Samp.AddressV, &BorderY);
  return femeRTFetchTexel2DI32(&Img, Level, /*Layer=*/CubeIndex * 6 + CF.Face,
                               AddrX, AddrY, /*Sample=*/0,
                               /*ApplySwizzle=*/1);
}

// `feme.cpu.image.samplecmp.cube.f32` (roadmap L48): the `TextureCube`
// counterpart of `feme.cpu.image.samplecmp.2d.f32` above, converting the
// direction vector `(DirX, DirY, DirZ)` to a face index and 2D UV via
// `femeRTSelectCubeFace` (below `femeCpuImageSampleCubeV4F32`, reused
// verbatim -- a shadow sampler's own cube-face selection has no
// depth-comparison-specific twist). `femeRTSampleCmpCubeAtLevel` (roadmap
// L53) applies the same seamless cross-face blending to a `LINEAR`
// comparison tap that `femeRTSampleFilteredCube` applies to an ordinary
// color one -- see that function's own comment. `MinLodClamp` (roadmap
// L52(c)) mirrors `femeCpuImageSampleCmp2DF32`'s own new `MinLod` clamp
// operand. `DDirXdX`/`DDirXdY`/`DDirYdX`/`DDirYdY`/`DDirZdX`/`DDirZdY`
// (roadmap L66(h)) mirror `femeCpuImageSampleCubeV4F32`'s own identically-
// named screen-space direction-vector derivative operands, fed through
// the same `femeRTComputeCubeClampedLod` helper that already turns them
// into a real, derivative-driven implicit LOD for an ordinary cube
// sample -- before this fix, an implicit-LOD `Cube` depth-comparison
// sample always called `femeRTComputeClampedLod` directly, which always
// resolves to `Lod=0` (mip level 0) regardless of any real minification,
// the same root cause `femeRTComputeCubeClampedLod`'s own doc describes
// for `femeCpuImageSampleCubeV4F32`'s pre-L56 bug. A caller with no real
// derivatives to give (a non-fragment stage, or an explicit-LOD sample)
// passes six zero constants, which `femeRTComputeCubeClampedLod`
// provably still resolves to `Lod=0` for the same reason a Plain2D
// all-zero `Grad` degenerates to the same always-level-0 result.
float femeCpuImageSampleCmpCubeF32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float DDirXdX, float DDirXdY, float DDirYdX, float DDirYdY,
    float DDirZdX, float DDirZdY, float Lod, _Bool UseExplicitLod, float Dref,
    float Bias, float MinLodClamp,
    _Bool Mask) asm("feme.cpu.image.samplecmp.cube.f32");

__attribute__((always_inline)) float femeCpuImageSampleCmpCubeF32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float DDirXdX, float DDirXdY, float DDirYdX, float DDirYdY,
    float DDirZdX, float DDirZdY, float Lod, _Bool UseExplicitLod, float Dref,
    float Bias, float MinLodClamp, _Bool Mask) {
  if (!Mask)
    return 0.0f;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u) || Img.ArrayLayers < 6)
    return 0.0f;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  Samp.AddressU = 2; // ClampToEdge -- see femeCpuImageSampleCubeV4F32.
  Samp.AddressV = 2;
  FemeRTCubeFace CF = femeRTSelectCubeFace(DirX, DirY, DirZ);
  float ClampedLod = femeRTComputeCubeClampedLod(
      &Img, &Samp, CF, Lod, UseExplicitLod, DDirXdX, DDirXdY, DDirYdX, DDirYdY,
      DDirZdX, DDirZdY, MinLodClamp, Bias);
  _Bool UseLinear = femeRTUseLinearFilter(ClampedLod, &Samp);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(&Img, ClampedLod);
  _Bool Trilinear = Samp.MipFilter == 1 && MipPlan.Level0 != MipPlan.Level1;
  uint32_t Level0 = Trilinear ? MipPlan.Level0 : femeRTNearestMipLevel(MipPlan);
  float Lo = femeRTSampleCmpCubeAtLevel(&Img, &Samp, CF.U, CF.V,
                                       /*LayerBase=*/0, CF.Face, Level0, Dref,
                                       UseLinear);
  if (!Trilinear)
    return Lo;
  float Hi = femeRTSampleCmpCubeAtLevel(&Img, &Samp, CF.U, CF.V,
                                       /*LayerBase=*/0, CF.Face,
                                       MipPlan.Level1, Dref, UseLinear);
  return Lo + (Hi - Lo) * MipPlan.Frac;
}

// `feme.cpu.image.samplecmp.cubearray.f32` (roadmap L48): the
// `TextureCubeArray` counterpart of `feme.cpu.image.samplecmp.cube.f32`
// above, adding `ArrayLayer` the same way `femeCpuImageSampleCubeArrayV4F32`
// adds it to `femeCpuImageSampleCubeV4F32`. `MinLodClamp` (roadmap
// L52(c)) mirrors `femeCpuImageSampleCmpCubeF32`'s own new `MinLod` clamp
// operand. `DDirXdX`/`DDirXdY`/`DDirYdX`/`DDirYdY`/`DDirZdX`/`DDirZdY`
// (roadmap L66(i)) mirror `femeCpuImageSampleCmpCubeF32`'s own
// identically-named screen-space direction-vector derivative operands
// (roadmap L66(h)), fed through the same `femeRTComputeCubeClampedLod`
// helper `femeCpuImageSampleCubeArrayV4F32` already uses for an ordinary
// cube-array sample -- before this fix, an implicit-LOD `CubeArray`
// depth-comparison sample always called `femeRTComputeClampedLod`
// directly, the same always-level-0 bug `femeCpuImageSampleCmpCubeF32`'s
// own doc describes for its pre-L66(h) state. A caller with no real
// derivatives to give (a non-fragment stage, or an explicit-LOD sample)
// passes six zero constants, which `femeRTComputeCubeClampedLod` provably
// still resolves to `Lod=0` for the same reason a Cube all-zero `Grad`
// degenerates to the same always-level-0 result.
float femeCpuImageSampleCmpCubeArrayF32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float DDirXdX, float DDirXdY, float DDirYdX, float DDirYdY,
    float DDirZdX, float DDirZdY, float ArrayLayer, float Lod,
    _Bool UseExplicitLod, float Dref, float Bias, float MinLodClamp,
    _Bool Mask) asm("feme.cpu.image.samplecmp.cubearray.f32");

__attribute__((always_inline)) float femeCpuImageSampleCmpCubeArrayF32(
    const FemeRTImageDescriptor *ImageHeap, uint32_t ImageHeapCount,
    const FemeRTSamplerDescriptor *SamplerHeap, uint32_t SamplerHeapCount,
    uint32_t ImageIndex, uint32_t SamplerIndex, float DirX, float DirY,
    float DirZ, float DDirXdX, float DDirXdY, float DDirYdX, float DDirYdY,
    float DDirZdX, float DDirZdY, float ArrayLayer, float Lod,
    _Bool UseExplicitLod, float Dref, float Bias, float MinLodClamp,
    _Bool Mask) {
  if (!Mask)
    return 0.0f;
  FemeRTImageDescriptor Img =
      femeRTLoadImageDescriptor(ImageHeap, ImageHeapCount, ImageIndex);
  if (!Img.Data || !(Img.Flags & 1u) || Img.ArrayLayers < 6)
    return 0.0f;
  FemeRTSamplerDescriptor Samp =
      femeRTLoadSamplerDescriptor(SamplerHeap, SamplerHeapCount, SamplerIndex);
  Samp.AddressU = 2; // ClampToEdge -- see femeCpuImageSampleCubeV4F32.
  Samp.AddressV = 2;
  FemeRTCubeFace CF = femeRTSelectCubeFace(DirX, DirY, DirZ);
  float ClampedLod = femeRTComputeCubeClampedLod(
      &Img, &Samp, CF, Lod, UseExplicitLod, DDirXdX, DDirXdY, DDirYdX, DDirYdY,
      DDirZdX, DDirZdY, MinLodClamp, Bias);
  _Bool UseLinear = femeRTUseLinearFilter(ClampedLod, &Samp);
  FemeRTMipTrilinearPlan MipPlan = femeRTSelectMipLevels(&Img, ClampedLod);
  _Bool Trilinear = Samp.MipFilter == 1 && MipPlan.Level0 != MipPlan.Level1;
  uint32_t Level0 = Trilinear ? MipPlan.Level0 : femeRTNearestMipLevel(MipPlan);
  uint32_t NumCubes = Img.ArrayLayers / 6;
  uint32_t CubeIndex = femeRTRoundClampLayer(NumCubes, ArrayLayer);
  float Lo = femeRTSampleCmpCubeAtLevel(&Img, &Samp, CF.U, CF.V,
                                       /*LayerBase=*/CubeIndex * 6, CF.Face,
                                       Level0, Dref, UseLinear);
  if (!Trilinear)
    return Lo;
  float Hi = femeRTSampleCmpCubeAtLevel(&Img, &Samp, CF.U, CF.V,
                                       /*LayerBase=*/CubeIndex * 6, CF.Face,
                                       MipPlan.Level1, Dref, UseLinear);
  return Lo + (Hi - Lo) * MipPlan.Frac;
}
