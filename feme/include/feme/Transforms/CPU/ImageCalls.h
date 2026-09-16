//===- ImageCalls.h - `feme.cpu.image.*` call helpers ------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the creation and recognition helpers for the canonical
// `feme.cpu.image.*` calls `feme::cpu::ResourceLoweringPass` lowers a raised
// texture/sampler access (`llvm.dx.resource.sample`/`samplelevel`/
// `load_level`) into, and `runtime/CPU/FeMeRuntimeCPU.c` implements (see
// "Canonical image operations"/"Texture layout and formats" in
// feme/docs/FeMeGraphicsDesign.md). This mirrors `feme::cpu::ResourceCalls`'
// role for buffers, but is a separate module rather than an extension of
// `ResourceCallKind`: a sample call's operand shape (two heaps, two
// descriptor indices, multiple coordinate operands) does not fit
// `MatchedResourceCall`'s fixed (heap, index, offset, [value], mask) shape,
// so forcing it in would either break that shape's invariants for every
// existing caller or require every caller to handle a shape it never
// produces.
//
// Scope (roadmap R30): 2D only, matching `runtime/CPU`'s own scope note.
//
// Update (roadmap H7b-a): widened beyond plain 2D to also cover
// `Texture2DArray`/`TextureCube`/`TextureCubeArray` shapes, reusing
// `runtime/CPU`'s own newly-widened `feme.cpu.image.sample.2darray.v4f32`/
// `.load.2darray.v4f32`/`.v4i32`/`.sample.cube.v4f32`/
// `.sample.cubearray.v4f32` entry points (FeMeRuntimeCPU.c). No
// `SampleCmp` counterpart is added for any of these new shapes: neither
// `SPIRVResourceLowering.cpp` nor `ResourceLowering.cpp` lowers a
// depth-comparison sample for *any* dimension yet, 2D included -- a
// pre-existing, unrelated gap.
//
// Update (roadmap H7i): `Sample2D`'s own implicit-LOD path (`Lod`'s
// `UseExplicitLod` operand false) now consults four extra operands,
// `DUdX`/`DUdY`/`DVdX`/`DVdY` -- the caller's own screen-space partial
// derivatives of `U`/`V` -- instead of always resolving to mip level 0
// (`runtime/CPU`'s own former scope note). A fragment-stage caller (the
// only stage GLSL/HLSL's own implicit `texture()`/`Sample()` is ever
// legal from) synthesizes them via `feme::createStageDerivative`
// (`feme.stage.derivative.*`, `feme::cpu::WaveLoweringPass`'s existing
// quad-lane machinery); any other caller passes zero constants, leaving
// its own sample at mip level 0 exactly as before. Scoped to `Sample2D`
// only -- the one shape a real anisotropic-filtering CTS case needs
// (`dEQP-VK.texture.filtering.2d.*anisotropy*`); `Sample2DArray`/
// `SampleCube`/`SampleCubeArray` still resolve every implicit sample to
// mip level 0, a pre-existing limitation this update does not change.
//
// Update (roadmap L56): `SampleCube`/`SampleCubeArray` gain their own
// six extra operands, `DDirXdX`/`DDirXdY`/`DDirYdX`/`DDirYdY`/`DDirZdX`/
// `DDirZdY` (see `CubeDirectionDerivatives`'s own doc) -- the always-mip-0
// limitation the H7i update above left in place for these two shapes was
// confirmed (via a real `dEQP-VK.texture.filtering.cube.combinations.
// linear_mipmap_linear.*` re-run, roadmap L56) to be an outright
// correctness bug, not just missing anisotropic-filtering polish the way
// it is for `Sample2DArray`: a real trilinear-filtering CTS case exercises
// these two shapes and needs a real, non-always-zero implicit LOD to pass
// at all. `Sample2DArray` still resolves every implicit sample to mip
// level 0 -- no real CTS case has yet motivated extending it too.
//
// Update (roadmap L58): `Sample2D`/`SampleCube` gain a new `Bias` operand,
// SPIR-V's own `Bias` image operand (GLSL's `texture(sampler, coord,
// bias)`/HLSL's `Texture2D::SampleBias`'s explicit bias argument) -- an
// additional term added to the implicit-LOD footprint's own raw LOD
// before the sampler's `mipLodBias`/`minLod`/`maxLod` clamp runs (see
// `femeRTComputeClampedLod`'s own updated doc), never combined with an
// explicit LOD (SPIR-V forbids `Bias` alongside `Lod`, mirroring `MinLod`'s
// own identical restriction). Scoped to `Sample2D`/`SampleCube` only, the
// two shapes with a real, non-always-zero implicit-LOD footprint to add a
// bias to (`Sample2DArray`/`SampleCubeArray` still resolve every implicit
// sample to a hardcoded mip level 0 regardless of any real footprint or
// bias, an existing limitation this update does not change; `Plain1D`/
// `Array1D`/`Plain3D` are left for a future row too). A caller with no
// `Bias` operand of its own (e.g. an explicit-LOD sample, or a shape whose
// intrinsic form has no `Bias` bit set) passes a zero constant.
//
// Update (roadmap H19a): two new, write-only kinds, `Store2D`/`Store2DI32`,
// give a storage image (a `spirv.Image`/`spirv.SignedImage` handle used
// without a sampler, `Sampled == 2`) somewhere to lower `OpImageWrite` to
// -- previously only `Load2D`/`Load2DI32` existed, covering `OpImageRead`/
// `OpImageFetch` but never a write. Scoped, like every other kind here, to
// a plain, non-arrayed 2D image (`ImageShape::Plain2D`); no
// `Store2DArray`/cube counterpart exists yet.
//
// Update (roadmap H19b): two more write-only kinds, `Store2DArray`/
// `Store2DArrayI32`, extend the above to an arrayed (`ImageShape::Array2D`)
// storage image, adding an integer array-layer operand before the texel
// value -- mirroring exactly how `Load2DArray`/`Load2DArrayI32` extend
// `Load2D`/`Load2DI32`'s own non-arrayed shape. Cube/cube-array storage
// images remain unstarted follow-on work (roadmap H19d), as does an arrayed
// 1D storage image (roadmap H19e).
//
// Update (roadmap H19c): eight more kinds, `Load1D`/`Load1DI32`/`Store1D`/
// `Store1DI32`/`Load3D`/`Load3DI32`/`Store3D`/`Store3DI32`, cover a plain
// (non-arrayed) 1D and 3D storage image's own read/write, mirroring
// `Load2D`/`Store2D`'s shape but with the coordinate arity narrowed to one
// component (`X` only, `Plain1D`) or widened to three (`X`/`Y`/`Z`,
// `Plain3D`; never arrayed -- SPIR-V disallows an arrayed `Dim::3D` image
// outright). A 1D image's own coordinate is scoped separately (see
// `MatchedImageCall::U`'s doc): it is a single value, not a two-component
// `(U, V)` pair like every other kind here.
//
// Update (roadmap H19d): a storage cube/cube-array handle now maps to
// the pre-existing `ImageShape::Array2D` (no new call vocabulary needed --
// `Store2DArray`/`Load2DArray` above already cover it, since a storage
// cube's own `(x, y, face)` addressing is structurally identical to an
// ordinary 2D array's `(x, y, layer)`).
//
// Update (roadmap H19e): four more kinds, `Load1DArray`/`Load1DArrayI32`/
// `Store1DArray`/`Store1DArrayI32`, cover an arrayed 1D storage image's own
// read/write -- the one dimension left out of both H19b's array scope
// (`Texture2DArray` only) and H19c's non-arrayed scope (`Plain1D`/`Plain3D`
// only). Unlike `Load2DArray`'s 3-component `(X, Y, Layer)` coordinate, a
// 1D array has only one spatial coordinate to begin with, giving a
// 2-component `(X, Layer)` shape -- mirroring `Load1D`'s single `X` plus
// `Load2DArray`'s integer array-layer operand.
//
// Update (roadmap H19m): an arrayed multisampled 2D storage image
// (`ImageShape::Array2DMS`) needs a 4-component `(X, Y, Layer, Sample)`
// coordinate. The read side needed no new call vocabulary at all:
// `Load2DArray` already carries both a `Layer` and a `Sample` operand (the
// latter added for a different reason, `subpassLoad`'s explicit-sample
// array form), and `Load2DArrayI32` is simply widened in place to add the
// `Sample` operand it was missing -- mirroring exactly how roadmap H19g
// widened `Load2DI32` to add the same operand `Load2D` already had. The
// write side has no such existing operand to widen: two new kinds,
// `Store2DArrayMS`/`Store2DArrayMSI32`, add both a `Layer` and a `Sample`
// operand to `Store2D`'s own shape, mirroring `Store2DMS`/`Store2DMSI32`'s
// relationship to `Store2D` but for an arrayed image.
//
// Update (roadmap L48): three new kinds, `SampleCmpArray2D`/`SampleCmpCube`/
// `SampleCmpCubeArray`, extend `SampleCmp2D`'s depth-comparison sample
// (roadmap L46, `Plain2D` only) to the same three non-`Plain2D` shapes
// `Sample2DArray`/`SampleCube`/`SampleCubeArray` already cover for an
// ordinary filtered sample -- an arrayed and/or cube shadow sampler, per
// SPIR-V's own `OpImageSampleDrefImplicitLod`/`ExplicitLod`. Confirmed via
// a real `deqp-vk` SPIR-V capture (`--deqp-log-decompiled-spirv=enable`)
// of `dEQP-VK.glsl.texture_functions.texture.{sampler2darrayshadow,
// samplercube{,array}shadow}_fragment`: glslang always pads a
// depth-comparison sample's own `Coordinate` operand with one extra,
// redundant trailing component (echoing `Dref`, itself always read from
// its own separate operand, never this echo) beyond the shape's ordinary
// addressing width -- `SampleCmp2D`'s own precedent (roadmap L46) already
// established this for `Plain2D` (2 -> 3); `Array2D`/`Cube` (3 -> 4)
// follow identically. `CubeArray`'s own ordinary width (4, a direction
// vector plus a float array layer) is already SPIR-V's per-instruction
// vector width ceiling, so its own dref coordinate stays 4-wide with no
// further padding -- `Dref` is read as a genuinely independent value in
// this case, not an echo of the coordinate's own last component (both
// happen to share one ultimate GLSL source expression in the CTS case
// this was confirmed against, `texture(sampler, coord, coord.w)`, but that
// is user-shader coincidence, not a property this pass may rely on).
// `sampler1d{,array}shadow` (a genuinely 1D shadow sampler) remain
// unstarted follow-on work: unlike every shape here, no ordinary
// (non-comparison) `Sample1D`/`Sample1DArray` sampled-image infrastructure
// exists yet at all (`Load1D`/`Load1DArray`, roadmap H19c/H19e, are
// storage-image-only kinds) -- filed as its own roadmap row rather than
// folded into this update, since it needs new ordinary-sample plumbing
// built first, not just a depth-comparison counterpart of existing
// plumbing the way this update's three new kinds are.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_IMAGECALLS_H
#define FEME_TRANSFORMS_CPU_IMAGECALLS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Value.h"

#include <optional>

namespace llvm {
class CallInst;
class Function;
class IRBuilderBase;
class Module;
} // namespace llvm

namespace feme::cpu {

/// Which canonical `feme.cpu.image.*` operation a call performs.
enum class ImageCallKind : uint8_t {
  /// `feme.cpu.image.sample.2d.v4f32`: filtered color sample.
  Sample2D,
  /// `feme.cpu.image.samplecmp.2d.f32`: filtered depth-comparison sample.
  SampleCmp2D,
  /// `feme.cpu.image.load.2d.v4f32`: explicit-mip, no-sampler texel fetch.
  Load2D,
  /// `feme.cpu.image.load.2d.v4i32` (roadmap E26): the integer-format
  /// counterpart of `Load2D` -- an explicit-mip, no-sampler texel fetch of
  /// a `_UINT`/`_SINT` image, returning `<4 x i32>` instead of
  /// `<4 x float>`. Roadmap H109: a prior version of this comment claimed
  /// no filtered-sample counterpart could exist, reasoning that SPIR-V
  /// only legalizes `OpImageFetch` (never `OpImageSample*`) against an
  /// integer-sampled image -- this was wrong: `OpImageSampleExplicitLod`
  /// against an integer-channel sampled image is legal SPIR-V (just
  /// restricted, per the Vulkan spec, to `NEAREST` filtering), and is
  /// exactly what `Sample2DI32` below now covers.
  Load2DI32,
  /// `feme.cpu.image.sample.2darray.v4f32` (roadmap H7b-a): the
  /// `Texture2DArray` counterpart of `Sample2D`, adding a float array-layer
  /// coordinate (rounded to nearest, clamped, per SPIR-V's own arrayed-
  /// sample convention).
  Sample2DArray,
  /// `feme.cpu.image.load.2darray.v4f32` (roadmap H7b-a): the
  /// `Texture2DArray` counterpart of `Load2D`, adding an integer
  /// array-layer coordinate.
  Load2DArray,
  /// `feme.cpu.image.load.2darray.v4i32` (roadmap H7b-a): the integer-format
  /// counterpart of `Load2DArray`, mirroring `Load2DI32`'s relationship to
  /// `Load2D`.
  Load2DArrayI32,
  /// `feme.cpu.image.sample.cube.v4f32` (roadmap H7b-a): the `TextureCube`
  /// counterpart of `Sample2D` -- a 3-component direction-vector coordinate
  /// (`U`/`V`/`W` here standing for the vector's X/Y/Z) resolved to a face
  /// plus 2D UV by the runtime's own "major axis" algorithm.
  SampleCube,
  /// `feme.cpu.image.sample.cubearray.v4f32` (roadmap H7b-a): the
  /// `TextureCubeArray` counterpart of `SampleCube`, adding a float
  /// array-layer coordinate selecting which six-layer cube element of the
  /// array to sample.
  SampleCubeArray,
  /// `feme.cpu.image.store.2d.v4f32` (roadmap H19a): a plain, non-arrayed
  /// storage-image write (`OpImageWrite`), the write-side counterpart of
  /// `Load2D`.
  Store2D,
  /// `feme.cpu.image.store.2d.v4i32` (roadmap H19a): the integer-format
  /// counterpart of `Store2D`, mirroring `Load2DI32`'s relationship to
  /// `Load2D`.
  Store2DI32,
  /// `feme.cpu.image.store.2darray.v4f32` (roadmap H19b): the arrayed
  /// counterpart of `Store2D`, adding an integer array-layer operand,
  /// mirroring `Load2DArray`'s relationship to `Load2D`.
  Store2DArray,
  /// `feme.cpu.image.store.2darray.v4i32` (roadmap H19b): the integer-format
  /// counterpart of `Store2DArray`, mirroring `Load2DArrayI32`'s
  /// relationship to `Load2DArray`.
  Store2DArrayI32,
  /// `feme.cpu.image.load.1d.v4f32` (roadmap H19c): the plain-1D
  /// counterpart of `Load2D`, taking a single integer `X` texel coordinate
  /// instead of an `(X, Y)` pair.
  Load1D,
  /// `feme.cpu.image.load.1d.v4i32` (roadmap H19c): the integer-format
  /// counterpart of `Load1D`, mirroring `Load2DI32`'s relationship to
  /// `Load2D`.
  Load1DI32,
  /// `feme.cpu.image.store.1d.v4f32` (roadmap H19c): the write-side
  /// counterpart of `Load1D`, mirroring `Store2D`'s relationship to
  /// `Load2D`.
  Store1D,
  /// `feme.cpu.image.store.1d.v4i32` (roadmap H19c): the integer-format
  /// counterpart of `Store1D`, mirroring `Store2DI32`'s relationship to
  /// `Store2D`.
  Store1DI32,
  /// `feme.cpu.image.load.3d.v4f32` (roadmap H19c): the plain-3D
  /// counterpart of `Load2D`, taking an `(X, Y, Z)` texel coordinate.
  /// Never arrayed: SPIR-V disallows an arrayed `Dim::3D` image.
  Load3D,
  /// `feme.cpu.image.load.3d.v4i32` (roadmap H19c): the integer-format
  /// counterpart of `Load3D`, mirroring `Load2DI32`'s relationship to
  /// `Load2D`.
  Load3DI32,
  /// `feme.cpu.image.store.3d.v4f32` (roadmap H19c): the write-side
  /// counterpart of `Load3D`, mirroring `Store2D`'s relationship to
  /// `Load2D`.
  Store3D,
  /// `feme.cpu.image.store.3d.v4i32` (roadmap H19c): the integer-format
  /// counterpart of `Store3D`, mirroring `Store2DI32`'s relationship to
  /// `Store2D`.
  Store3DI32,
  /// `feme.cpu.image.load.1darray.v4f32` (roadmap H19e): the arrayed-1D
  /// counterpart of `Load1D`, adding an integer array-layer coordinate --
  /// mirroring exactly how `Load2DArray` extends `Load2D`.
  Load1DArray,
  /// `feme.cpu.image.load.1darray.v4i32` (roadmap H19e): the integer-format
  /// counterpart of `Load1DArray`, mirroring `Load2DArrayI32`'s
  /// relationship to `Load2DArray`.
  Load1DArrayI32,
  /// `feme.cpu.image.store.1darray.v4f32` (roadmap H19e): the write-side
  /// counterpart of `Load1DArray`, mirroring `Store2DArray`'s relationship
  /// to `Load2DArray`.
  Store1DArray,
  /// `feme.cpu.image.store.1darray.v4i32` (roadmap H19e): the
  /// integer-format counterpart of `Store1DArray`, mirroring
  /// `Store2DArrayI32`'s relationship to `Store2DArray`.
  Store1DArrayI32,
  /// `feme.cpu.image.store.2dms.v4f32` (roadmap H19g): the write-side
  /// counterpart of `Load2D`'s own multisampled use, adding an integer
  /// sample-index operand -- a plain, non-arrayed multisampled storage
  /// image's `OpImageWrite`, distinct from `Store2DArray`'s array-layer
  /// operand (never both at once: `classifyStorageImage2DHandle` never
  /// returns a shape that is both arrayed and multisampled today).
  Store2DMS,
  /// `feme.cpu.image.store.2dms.v4i32` (roadmap H19g): the integer-format
  /// counterpart of `Store2DMS`, mirroring `Store2DArrayI32`'s
  /// relationship to `Store2DArray`.
  Store2DMSI32,
  /// `feme.cpu.image.store.2darrayms.v4f32` (roadmap H19m): the arrayed
  /// multisampled counterpart of `Store2D`, adding both an integer array-
  /// layer operand (like `Store2DArray`'s own) and an integer sample-index
  /// operand (like `Store2DMS`'s own) -- unlike either of those two kinds,
  /// never both at once.
  Store2DArrayMS,
  /// `feme.cpu.image.store.2darrayms.v4i32` (roadmap H19m): the
  /// integer-format counterpart of `Store2DArrayMS`, mirroring
  /// `Store2DMSI32`'s relationship to `Store2DMS`.
  Store2DArrayMSI32,
  /// `feme.cpu.image.atomic.add.2d.i32` (roadmap H8v): a plain, non-arrayed
  /// storage-image RMW atomic (`OpAtomicIAdd` against an
  /// `OpImageTexelPointer`), scoped -- unlike every `Load*`/`Store*` kind
  /// above -- to a single 32-bit *scalar* component rather than a
  /// `<4 x i32>`/`<4 x float>` texel: SPIR-V only permits an image atomic
  /// against a single-component `R32i`/`R32ui` image format. Returns the
  /// value that was in memory immediately before the add, matching
  /// `OpAtomicIAdd`'s own result semantics.
  AtomicAdd2D,
  /// `feme.cpu.image.atomic.sub.2d.i32` (roadmap H8v): `OpAtomicISub`'s
  /// counterpart to `AtomicAdd2D`.
  AtomicSub2D,
  /// `feme.cpu.image.atomic.and.2d.i32` (roadmap H8v): `OpAtomicAnd`'s
  /// counterpart to `AtomicAdd2D`.
  AtomicAnd2D,
  /// `feme.cpu.image.atomic.or.2d.i32` (roadmap H8v): `OpAtomicOr`'s
  /// counterpart to `AtomicAdd2D`.
  AtomicOr2D,
  /// `feme.cpu.image.atomic.xor.2d.i32` (roadmap H8v): `OpAtomicXor`'s
  /// counterpart to `AtomicAdd2D`.
  AtomicXor2D,
  /// `feme.cpu.image.atomic.smax.2d.i32` (roadmap H8v): `OpAtomicSMax`'s
  /// counterpart to `AtomicAdd2D` -- a signed maximum.
  AtomicSMax2D,
  /// `feme.cpu.image.atomic.smin.2d.i32` (roadmap H8v): `OpAtomicSMin`'s
  /// counterpart to `AtomicAdd2D` -- a signed minimum.
  AtomicSMin2D,
  /// `feme.cpu.image.atomic.umax.2d.i32` (roadmap H8v): `OpAtomicUMax`'s
  /// counterpart to `AtomicAdd2D` -- an unsigned maximum.
  AtomicUMax2D,
  /// `feme.cpu.image.atomic.umin.2d.i32` (roadmap H8v): `OpAtomicUMin`'s
  /// counterpart to `AtomicAdd2D` -- an unsigned minimum.
  AtomicUMin2D,
  /// `feme.cpu.image.atomic.exchange.2d.i32` (roadmap H8v):
  /// `OpAtomicExchange`'s counterpart to `AtomicAdd2D` -- an unconditional
  /// swap.
  AtomicExchange2D,
  /// `feme.cpu.image.atomic.compare_exchange.2d.i32` (roadmap H8v):
  /// `OpAtomicCompareExchange`'s counterpart to `AtomicAdd2D`, taking an
  /// extra `Comparator` operand before `Value` -- the memory word is only
  /// replaced with `Value` when it currently equals `Comparator`, but the
  /// value returned is always the pre-op value either way, matching
  /// `OpAtomicCompareExchange`'s own result semantics.
  AtomicCompareExchange2D,
  /// `feme.cpu.image.samplecmp.2darray.f32` (roadmap L48): the
  /// `Texture2DArray` counterpart of `SampleCmp2D`, adding the same float
  /// array-layer coordinate `Sample2DArray` adds to `Sample2D`.
  SampleCmpArray2D,
  /// `feme.cpu.image.samplecmp.cube.f32` (roadmap L48): the `TextureCube`
  /// counterpart of `SampleCmp2D`, taking the same 3-component direction
  /// vector `SampleCube` does.
  SampleCmpCube,
  /// `feme.cpu.image.samplecmp.cubearray.f32` (roadmap L48): the
  /// `TextureCubeArray` counterpart of `SampleCmp2D`, adding the same
  /// float array-layer coordinate `SampleCubeArray` adds to `SampleCube`.
  SampleCmpCubeArray,
  /// `feme.cpu.image.sample.1d.v4f32` (roadmap L52a): the ordinary
  /// (non-comparison) `Texture1D` counterpart of `Sample2D` -- a single
  /// `U` coordinate instead of `(U, V)`, no screen-space
  /// derivatives/`ConstOffset`/`MinLod` clamp (mirroring `Sample2DArray`'s
  /// own simpler scope rather than `Sample2D`'s richer roadmap H7i/L26
  /// additions, deferred here to keep this first pass minimal). The
  /// depth-comparison counterpart (`SampleCmp1D`) does not exist yet --
  /// filed as its own follow-on roadmap row.
  Sample1D,
  /// `feme.cpu.image.sample.1darray.v4f32` (roadmap L52a): the
  /// `Texture1DArray` counterpart of `Sample1D`, adding the same float
  /// array-layer coordinate `Sample2DArray` adds to `Sample2D`.
  Sample1DArray,
  /// `feme.cpu.image.samplecmp.1d.f32` (roadmap L54): the depth-comparison
  /// counterpart of `Sample1D`, mirroring `SampleCmp2D`'s own relationship
  /// to `Sample2D`. Carries a real `Bias`/`MinLod` clamp pair (roadmap
  /// L62), like every other dref-sample shape, but -- like
  /// `Sample1D`/`Sample1DArray` themselves -- still no `ConstOffset`
  /// (SPIR-V's own `ConstOffset` image operand is legal against `Dim::1D`,
  /// but no real CTS case exercises it yet, matching `Sample1D`'s own
  /// scope decision). Roadmap L66(f) added a real `DUdX`/`DUdY` scalar
  /// derivative pair too, mirroring `SampleCmp2D`'s own `Grad` support
  /// (roadmap L66(c)) -- a bare scalar here rather than a 2-wide pair,
  /// since `Plain1D`'s own addressing is itself already a bare scalar
  /// `U` with no `V`.
  SampleCmp1D,
  /// `feme.cpu.image.samplecmp.1darray.f32` (roadmap L54): the
  /// `Texture1DArray` counterpart of `SampleCmp1D`, adding the same float
  /// array-layer coordinate `Sample1DArray` adds to `Sample1D`. Roadmap
  /// L66(f) added the same `DUdX`/`DUdY` scalar derivative pair
  /// `SampleCmp1D` gained -- only `U`, never `ArrayLayer`, is ever
  /// differentiated, mirroring `Sample1DArray`'s own identical precedent.
  SampleCmpArray1D,
  /// `feme.cpu.image.querylod.2d.v2f32` (roadmap L52e): `Plain2D`'s own
  /// counterpart of `OpImageQueryLod` (HLSL's
  /// `Texture2D::CalculateLevelOfDetail`/`CalculateLevelOfDetailUnclamped`,
  /// GLSL's `textureQueryLod`) -- given the caller's own screen-space
  /// partial derivatives of the sampled coordinate (the same
  /// `SampleDerivatives` shape `Sample2D`'s own implicit-LOD path
  /// consults, synthesized the same way via
  /// `getOrSynthesizeSample2DDerivatives`), returns a `<2 x float>` whose
  /// lane 0 is the clamped level (the mip level/blend an ordinary
  /// implicit-LOD sample of this same coordinate would actually use,
  /// after the sampler's own `minLod`/`maxLod`/bias and the image's own
  /// valid mip range are applied) and lane 1 is the raw, unclamped LOD
  /// (before any of that) -- mirroring
  /// `llvm.spv.resource.calculate.lod`/`.calculate.lod.unclamped`'s own
  /// lane convention (see `ImageQueryLodPattern`,
  /// `SPIRVToLLVMPatterns.cpp`). Unlike `Sample2D`, there is no
  /// explicit-vs-implicit-LOD distinction at all -- `OpImageQueryLod`
  /// always measures the implicit LOD a coordinate's own derivatives
  /// would produce, so real derivatives are always required (never zero
  /// constants, unlike `Sample2D`'s `ExplicitLod`-gated case) for every
  /// caller in the one stage (`Fragment`) this instruction is ever legal
  /// from. Scoped to `Plain2D`/`Array2D` (roadmap H124t reuses this same
  /// entry point for `Array2D`, whose own `CalculateLevelOfDetail` has an
  /// identical 2-component coordinate and formula) -- `Cube`/`CubeArray`
  /// (see `QueryLodCube` immediately below)/`Plain1D`/`Array1D`/
  /// `Plain3D` counterparts remain unstarted follow-on work.
  QueryLod2D,
  /// `feme.cpu.image.querylod.cube.v2f32` (roadmap H124u): `Cube`'s own
  /// counterpart of `OpImageQueryLod` (HLSL's `TextureCube::
  /// CalculateLevelOfDetail`/`CalculateLevelOfDetailUnclamped`). Unlike
  /// `QueryLod2D`'s own `(DUdX, DUdY, DVdX, DVdY)` operand pair, a cube's
  /// own direction-vector coordinate has no `(U, V)` of its own until a
  /// face is selected -- this entry point instead takes the caller's raw
  /// screen-space derivatives of the direction vector's three components
  /// (`DDirXdX`/`DDirXdY`/`DDirYdX`/`DDirYdY`/`DDirZdX`/`DDirZdY`,
  /// synthesized the same way `getOrSynthesizeSampleCubeDerivatives`
  /// already does for an ordinary implicit-LOD cube sample) plus the
  /// direction vector itself (`DirX`/`DirY`/`DirZ`, needed to select
  /// which face's own per-face sign/axis convention
  /// `femeRTComputeCubeUVDerivatives` should remap those raw derivatives
  /// through) -- see `femeCpuImageQueryLodCubeV2F32`'s own doc
  /// (`FeMeRuntimeCPU.c`) for the exact face-selection-then-remap
  /// pipeline. The `<2 x float>` result's own lane convention is
  /// otherwise identical to `QueryLod2D`'s (lane 0 clamped level, lane 1
  /// raw unclamped LOD).
  QueryLodCube,
  /// `feme.cpu.image.sample.3d.v4f32` (roadmap L66(a), extended with a
  /// real `Bias`/`MinLodClamp` pair by roadmap L67(a) and real `Grad`
  /// support by roadmap L67(b)): the volumetric counterpart of
  /// `Sample2D`, a `Plain3D` ordinary sample -- a real `(U, V, W)`
  /// coordinate, its own screen-space partial derivatives
  /// (`DUdX`/`DUdY`/`DVdX`/`DVdY`/`DWdX`/`DWdY`, either synthesized for an
  /// implicit-LOD sample or the caller's own real `Grad` derivative
  /// triple, mirroring `Sample1D`'s own derivative pair) for
  /// `Lod`/`UseExplicitLod`'s own implicit-vs-explicit split, and a real
  /// `Bias`/`MinLodClamp` pair (mirroring `Sample1D`'s own roadmap L61(c)
  /// extension). Still no `ConstOffset` operand -- that remains its own
  /// follow-on roadmap L67(c)/L66(d) sub-item, blocked on the same
  /// pre-existing `Plain2D`-only `isSupportedOffset` restriction roadmap
  /// L33 already scopes.
  Sample3D,
  /// `feme.cpu.image.getdimensions.2d.v2i32` (roadmap L70): a plain 2D
  /// image's mip-0 extent (`OpImageQuerySize` against a non-arrayed,
  /// non-multisampled `Dim2D` image, `llvm.spv.resource.getdimensions.xy`
  /// -- GLSL's `imageSize()`/`textureSize()` against a `sampler2D`/
  /// `image2D` with no explicit LOD argument), returning the `(Width,
  /// Height)` pair `FemeImageDescriptor` already carries for its bound
  /// mip-0 subresource, or `(0, 0)` for an unbound handle or a masked-off
  /// invocation. Unlike every other kind above this needs neither a
  /// sampler heap (a plain size query, not a filtered access) nor a
  /// coordinate of its own -- only the bound image's own identity, hence
  /// the smaller operand list `createGetDimensions2D` documents. Scoped to
  /// `Plain2D` only, whether the underlying handle is a sampled or storage
  /// image (`hasOnlySupportedImageUses`/`hasOnlySupportedStorageImageUses`
  /// both accept this same call shape) -- every other `ImageShape`'s own
  /// `GetDimensions` counterpart remains unstarted follow-on work.
  GetDimensions2D,
  /// `feme.cpu.image.getdimensions.lod.2d.v2i32` (roadmap L72(d)): a plain
  /// 2D image's own extent at an explicit, possibly non-zero mip level
  /// (`OpImageQuerySizeLod` -- GLSL's `textureSize(sampler, lod)` against
  /// a `sampler2D`, unlike `GetDimensions2D`'s own always-mip-0
  /// `imageSize()`/no-argument `textureSize()`), returning the
  /// `(max(1, Width >> Lod), max(1, Height >> Lod))` pair, clamped the
  /// same way `Image.cpp`'s own `computeSubresourceLayouts` already
  /// computes a mip level's real extent. Takes the same operands as
  /// `GetDimensions2D` (`ImageIndex`/`Mask`) plus one more: the explicit
  /// mip level itself (`Lod` in `MatchedImageCall`, reusing that same
  /// field other sampling kinds already use for an explicit-LOD operand).
  /// Scoped to `Plain2D`/`Cube` (roadmap L75 widened this to also accept
  /// `Cube`: a cube face's own extent query uses the identical
  /// `(max(1, Width >> Lod), max(1, Height >> Lod))` formula, since a
  /// cube face is exactly as square as a `Plain2D` mip level, needing no
  /// new builder or runtime function at all) -- every other
  /// `ImageShape`'s own distinct result width still needs its own builder
  /// (`QuerySizeLod1D`/`QuerySizeLod1DArray`/`QuerySizeLod2DArray`/
  /// `QuerySizeLod3D`/`QuerySizeLodCubeArray` below, roadmap L75).
  QuerySizeLod2D,
  /// `feme.cpu.image.getdimensions.lod.1d.i32` (roadmap L75): a plain 1D
  /// image's own extent at an explicit, possibly non-zero mip level
  /// (`OpImageQuerySizeLod` against a `sampler1D` -- GLSL's
  /// `textureSize(sampler1D, lod)`, which returns a bare scalar `int`,
  /// unlike every wider shape above/below). Returns
  /// `max(1, Width >> Lod)`, the same clamped-halving formula
  /// `QuerySizeLod2D` already uses for its own `Width` component, just
  /// without a `Height` companion (a 1D image has no second spatial
  /// axis). Same operand list as `QuerySizeLod2D` (`ImageIndex`/`Lod`/
  /// `Mask`), only the result type differs.
  QuerySizeLod1D,
  /// `feme.cpu.image.getdimensions.lod.1darray.v2i32` (roadmap L75): an
  /// arrayed 1D image's own extent at an explicit mip level (`sampler1DArray`
  /// -- GLSL's `textureSize(sampler1DArray, lod)`, returning `ivec2(width,
  /// layers)`). Unlike `QuerySizeLod2D`'s own `v2i32` result (whose second
  /// lane is a `Height` that *does* shrink with `Lod`, the same as its
  /// first lane), this shape's second lane is `ArrayLayers` -- a layer
  /// count never shrinks with mip level, only the physical per-layer
  /// extent does -- so this needs its own distinct builder rather than
  /// reusing `QuerySizeLod2D`'s. Same operand list as `QuerySizeLod2D`.
  QuerySizeLod1DArray,
  /// `feme.cpu.image.getdimensions.lod.2darray.v3i32` (roadmap L75): an
  /// arrayed 2D image's own extent at an explicit mip level
  /// (`sampler2DArray` -- GLSL's `textureSize(sampler2DArray, lod)`,
  /// returning `ivec3(width, height, layers)`). Like
  /// `QuerySizeLod1DArray` immediately above, only the first two lanes
  /// (`Width`/`Height`) shrink with `Lod`; the third (`ArrayLayers`) does
  /// not. Same operand list as `QuerySizeLod2D`.
  QuerySizeLod2DArray,
  /// `feme.cpu.image.getdimensions.lod.3d.v3i32` (roadmap L75): a plain 3D
  /// (volume) image's own extent at an explicit mip level (`sampler3D` --
  /// GLSL's `textureSize(sampler3D, lod)`, returning `ivec3(width, height,
  /// depth)`). Unlike `QuerySizeLod2DArray`'s own third lane, a volume
  /// texture's `Depth` genuinely is a mip-chain dimension in its own
  /// right (SPIR-V/Vulkan halve a 3D image's depth alongside its width/
  /// height at each mip level, unlike an array's layer count, which never
  /// changes), so all three lanes shrink with `Lod` here. Same operand
  /// list as `QuerySizeLod2D`.
  QuerySizeLod3D,
  /// `feme.cpu.image.getdimensions.lod.cubearray.v3i32` (roadmap L75): a
  /// cube-array image's own extent at an explicit mip level
  /// (`samplerCubeArray` -- GLSL's `textureSize(samplerCubeArray, lod)`,
  /// returning `ivec3(width, height, numCubeArrayElements)`, where the
  /// third component is the *element* count, i.e. `ArrayLayers / 6`, not
  /// the raw face-inclusive layer count `FemeImageDescriptor::ArrayLayers`
  /// itself tracks (`CommandBuffer.cpp`'s own `materializeImageDescriptor`
  /// treats a cube(array) view as a plain view-level convention over
  /// consecutive array layers, so `ArrayLayers` here is always a multiple
  /// of 6 -- one set of 6 consecutive faces per cube-array element).
  /// Otherwise identical in shape/formula to `QuerySizeLod2DArray` (first
  /// two lanes shrink with `Lod`, third does not). Same operand list as
  /// `QuerySizeLod2D`.
  QuerySizeLodCubeArray,
  /// `feme.cpu.image.querylevels.i32` (roadmap L72(d)/L74): an image's own
  /// total mip-level count (`OpImageQueryLevels` -- GLSL's
  /// `textureQueryLevels(sampler)`), returning the scalar
  /// `FemeImageDescriptor::MipLevels` already tracks for its bound
  /// subresource, or `0` for an unbound handle. Unlike `QuerySizeLod2D`
  /// this needs neither a `Mask` (this query has no per-invocation side
  /// effect to guard) nor an explicit mip level of its own -- only
  /// `ImageIndex` -- so its own operand list is the smallest of any kind
  /// above. Unlike `QuerySizeLod2D`'s own `Plain2D`-only scope, this
  /// result never varies by shape (`MipLevels` is tracked identically
  /// regardless of dimensionality/arrayed-ness), so roadmap L74 widened
  /// `SPIRVResourceLowering.cpp`'s own shape gate to accept every
  /// classifiable non-multisampled shape (`Plain1D`/`Array1D`/`Plain2D`/
  /// `Array2D`/`Plain3D`/`Cube`/`CubeArray`) with no change needed to this
  /// builder itself -- `Plain2DMS`/`Array2DMS` are still rejected, since
  /// GLSL has no `textureQueryLevels()` overload for a multisampled
  /// sampler in the first place.
  QueryLevels,
  /// `feme.cpu.image.querysamples.i32` (roadmap L73): a multisampled
  /// image's own sample count (`OpImageQuerySamples` -- GLSL's
  /// `textureSamples(sampler2DMS)`), returning the scalar
  /// `FemeImageDescriptor::SampleCount` already tracks for its bound
  /// subresource, or `0` for an unbound handle. Structurally identical to
  /// `QueryLevels` above (same operand list -- no `Mask`, no explicit mip
  /// level -- just a different runtime field read), but scoped to
  /// `Plain2DMS`/`Array2DMS` only: `OpImageQuerySamples` is spec-legal
  /// only against a multisampled image, the one shape pair
  /// `QuerySizeLod2D`/`QueryLevels` above deliberately do not cover.
  QuerySamples,
  /// `feme.cpu.image.gathercmp.2d.v4f32` (roadmap L7d): `Plain2D`
  /// depth-comparison gather (SPIR-V `OpImageDrefGather` -- HLSL's
  /// `Texture2D::GatherCmp()`). Unlike `SampleCmp2D`'s own single
  /// filtered depth-comparison result, this returns a full `<4 x float>`:
  /// one 0/1 comparison result per one of the four texels the same
  /// bilinear "footprint" `SampleCmp2D` blends between would use (SPIR-V/
  /// Vulkan's own fixed gather-footprint-and-ordering convention), never
  /// blended together. Takes no `Lod`/`Bias`/`Grad`/`MinLod` operand at
  /// all -- a gather instruction always operates at mip level 0 per the
  /// SPIR-V spec, with no way to request otherwise -- so this kind's own
  /// operand list is narrower than `SampleCmp2D`'s.
  GatherCmp2D,
  /// `feme.cpu.image.gather.2d.v4f32` (roadmap L7g): `Plain2D`
  /// non-depth-comparison gather (SPIR-V `OpImageGather` -- HLSL's
  /// `Texture2D::Gather{,Red,Green,Blue,Alpha}()`). Structurally
  /// identical to `GatherCmp2D` above (same fixed gather footprint and
  /// result ordering, same mip-level-0-only restriction, no `Lod`/`Bias`/
  /// `Grad`/`MinLod` operand), but selects one of the four sampled
  /// *components* (`Component`, a 32-bit integer 0-3 selecting R/G/B/A)
  /// from each of the four texels, rather than comparing each texel's
  /// depth component against a `Dref` reference value.
  Gather2D,
  /// `feme.cpu.image.gathercmp.array2d.v4f32` (roadmap H124q): the
  /// `Texture2DArray` counterpart of `GatherCmp2D` above -- identical
  /// fixed bilinear footprint/result ordering/mip-level-0-only
  /// restriction, plus `ArrayLayer` (SPIR-V's own arrayed-gather
  /// coordinate convention: a float, rounded to nearest and clamped to a
  /// valid layer, mirroring `Sample2DArray`'s own identical `ArrayLayer`
  /// operand).
  GatherCmpArray2D,
  /// `feme.cpu.image.gather.array2d.v4f32` (roadmap H124q): the
  /// `Texture2DArray` counterpart of `Gather2D` above, adding
  /// `ArrayLayer` the same way `GatherCmpArray2D` does to `GatherCmp2D`.
  GatherArray2D,
  /// `feme.cpu.image.sample.2d.v4i32` (roadmap H109): a `Plain2D` nearest-
  /// filtered sample against an integer-channel (`usampler2D`/
  /// `isampler2D`) sampled image, returning `<4 x i32>` instead of
  /// `Sample2D`'s `<4 x float>`. Per the Vulkan/SPIR-V spec, sampling an
  /// integer-format image is legal but only with `VK_FILTER_NEAREST`
  /// mag/min filtering and `VK_SAMPLER_MIPMAP_MODE_NEAREST` -- unlike
  /// `Sample2D`, this kind's own runtime entry point always point-samples
  /// a single mip level, regardless of the bound sampler's own filter
  /// state, and never blends between two adjacent levels. Scoped, for
  /// now, to an explicit-LOD sample only (`UseExplicitLod` always
  /// `true`) -- no `Bias`/`Grad`/`MinLod` operand exists here, unlike
  /// `Sample2D`'s own: no real CTS case has yet motivated an implicit-LOD
  /// integer sample, and SPIR-V forbids `Bias`/`Grad`/`MinLod` alongside
  /// an explicit `Lod` operand regardless. `OffsetX`/`OffsetY` (SPIR-V's
  /// own `ConstOffset` image operand) are still threaded through, exactly
  /// like `Sample2D`'s own, since a real CTS case (`dEQP-VK.mesh_shader.
  /// ext.synchronization.transfer_to_{mesh,task}.sampled_image.*`, this
  /// kind's own motivating case) needs no offset but a future one might.
  Sample2DI32,
  /// `feme.cpu.image.gathercmp.cube.v4f32` (roadmap H124r): the
  /// `TextureCube` counterpart of `GatherCmp2D` above -- a 3-component
  /// direction-vector coordinate (`U`/`V`/`W` standing for the vector's
  /// X/Y/Z, mirroring `SampleCube`'s own convention) resolved to a face
  /// plus 2D UV by the runtime's own "major axis" algorithm
  /// (`femeRTSelectCubeFace`), then gathered from that face using the
  /// identical fixed bilinear footprint/result ordering `GatherCmp2D`
  /// uses. No `OffsetX`/`OffsetY` operand at all, unlike `GatherCmp2D`:
  /// SPIR-V forbids `ConstOffset` against `Dim::Cube` outright (see
  /// `isSupportedOffset`'s own comment), and HLSL's own
  /// `TextureCube::GatherCmp()` has no offset overload to begin with.
  GatherCmpCube,
  /// `feme.cpu.image.gather.cube.v4f32` (roadmap H124r): the
  /// `TextureCube` counterpart of `Gather2D` above, mirroring
  /// `GatherCmpCube`'s own direction-vector coordinate and lack of an
  /// offset operand.
  GatherCube,
};

/// The image/sampler heap operands every `feme.cpu.image.*` call carries.
/// `SamplerHeap`/`SamplerHeapCount` are unused (and passed as null/poison by
/// callers) for `Load2D`, which takes no sampler.
struct ImageCallEnv {
  llvm::Value *ImageHeap = nullptr;
  llvm::Value *ImageHeapCount = nullptr;
  llvm::Value *SamplerHeap = nullptr;
  llvm::Value *SamplerHeapCount = nullptr;
};

/// The result of successfully matching a call against the canonical
/// `feme.cpu.image.*` shape (see `matchImageCall`).
struct MatchedImageCall {
  ImageCallKind Kind;
  llvm::CallInst *Call = nullptr;
  ImageCallEnv Env;
  llvm::Value *ImageIndex = nullptr;
  /// The sampler descriptor index, for `Sample2D`/`SampleCmp2D`; null for
  /// `Load2D`/`Load2DI32`.
  llvm::Value *SamplerIndex = nullptr;
  /// `Sample2D`/`SampleCmp2D`: normalized U/V coordinates.
  /// `Load2D`/`Load2DI32`: integer X/Y texel coordinates.
  /// `Sample2DArray`/`Load2DArray`/`Load2DArrayI32`: same as their plain
  /// counterparts' `U`/`V` (the array layer is carried separately, in
  /// `ArrayLayer`/`Layer` below).
  /// `SampleCube`/`SampleCubeArray`: the direction vector's X/Y component
  /// (`W` below carries the Z component).
  /// `Load1D`/`Load1DI32`/`Store1D`/`Store1DI32` (roadmap H19c): the
  /// single integer `X` texel coordinate; `V` is null for these four
  /// kinds, unlike every other kind above.
  /// `Load3D`/`Load3DI32`/`Store3D`/`Store3DI32` (roadmap H19c): the
  /// integer `X`/`Y` texel coordinates (`Z` below carries the third).
  /// `Load1DArray`/`Load1DArrayI32`/`Store1DArray`/`Store1DArrayI32`
  /// (roadmap H19e): the single integer `X` texel coordinate, same as
  /// `Load1D`'s own `U` -- `V` is likewise null for these four kinds (the
  /// array layer is carried separately, in `Layer` below, same as
  /// `Load2DArray`'s own convention).
  /// `Sample1D`/`Sample1DArray` (roadmap L52a): the single normalized `U`
  /// coordinate, same shape as `Load1D`'s own `U` -- `V` is null for these
  /// two kinds too. `SampleCmp1D`/`SampleCmpArray1D` (roadmap L54) use
  /// this the same way.
  llvm::Value *U = nullptr;
  llvm::Value *V = nullptr;
  /// `Sample2D` only (roadmap H7i): the caller's own screen-space partial
  /// derivatives of `U`/`V`, consulted only for an implicit-LOD sample
  /// (see `createSample2D`'s doc); null for every other kind, except
  /// `QueryLod2D` (roadmap L52e), which uses these the same way but
  /// always requires real ones (see `createQueryLod2D`'s doc) -- `U`/`V`
  /// themselves stay null for `QueryLod2D`, unlike `Sample2D`, since its
  /// own runtime entry point never needs the coordinate itself, only its
  /// derivatives.
  llvm::Value *DUdX = nullptr;
  llvm::Value *DUdY = nullptr;
  llvm::Value *DVdX = nullptr;
  llvm::Value *DVdY = nullptr;
  /// `Sample3D` only (roadmap L66(a)): the caller's own screen-space
  /// partial derivatives of the third, depth-axis `W` coordinate below,
  /// consulted only for an implicit-LOD sample, mirroring `DUdX`/`DUdY`/
  /// `DVdX`/`DVdY` immediately above; null for every other kind.
  llvm::Value *DWdX = nullptr;
  llvm::Value *DWdY = nullptr;
  /// `SampleCube`/`SampleCubeArray`: the direction vector's Z component.
  /// `Sample3D` (roadmap L66(a)): the third, depth-axis normalized `W`
  /// coordinate, same convention as `U`/`V` above. Null for every other
  /// kind.
  llvm::Value *W = nullptr;
  /// `SampleCube`/`SampleCubeArray` only (roadmap L56): the caller's own
  /// screen-space partial derivatives of the direction vector's `X`/`Y`/
  /// `Z` components (`U`/`V`/`W` above), consulted only for an
  /// implicit-LOD sample -- see `getOrSynthesizeSampleCubeDerivatives`'s
  /// doc; null for every other kind.
  llvm::Value *DDirXdX = nullptr;
  llvm::Value *DDirXdY = nullptr;
  llvm::Value *DDirYdX = nullptr;
  llvm::Value *DDirYdY = nullptr;
  llvm::Value *DDirZdX = nullptr;
  llvm::Value *DDirZdY = nullptr;
  /// `Sample2DArray`/`SampleCubeArray` only: the float array-layer
  /// coordinate (rounded to nearest, clamped, at the runtime); null for
  /// every other kind, including the integer-coordinate `Load2DArray`/
  /// `Load2DArrayI32`, which instead use `Layer` below.
  /// `Sample1DArray` (roadmap L52a) also uses this, same convention as
  /// `Sample2DArray`. `SampleCmpArray1D` (roadmap L54) uses this the same
  /// way too.
  llvm::Value *ArrayLayer = nullptr;
  /// `Load2DArray`/`Load2DArrayI32`/`Store2DArray`/`Store2DArrayI32`
  /// (roadmap H19b), `Load1DArray`/`Load1DArrayI32`/`Store1DArray`/
  /// `Store1DArrayI32` (roadmap H19e), and `Store2DArrayMS`/
  /// `Store2DArrayMSI32` (roadmap H19m) only: the integer array-layer
  /// texel coordinate; null for every other kind.
  llvm::Value *Layer = nullptr;
  /// `Load3D`/`Load3DI32`/`Store3D`/`Store3DI32` (roadmap H19c) only: the
  /// integer `Z` texel coordinate (a real depth-slice index, not an array
  /// layer -- a 3D image is never arrayed); null for every other kind.
  llvm::Value *Z = nullptr;
  /// The LOD/mip operand: `Sample2D`/`SampleCmp2D`/`Sample2DArray`/
  /// `SampleCube`/`SampleCubeArray`'s explicit-or-ignored LOD float, or
  /// `Load2D`/`Load2DI32`/`Load2DArray`/`Load2DArrayI32`'s integer mip
  /// level.
  llvm::Value *Lod = nullptr;
  /// Whether `Lod` is an explicit LOD (true) or should be ignored in favor
  /// of implicit level 0 (false), for every sampled (non-`Load*`) kind;
  /// null for `Load2D`/`Load2DI32`/`Load2DArray`/`Load2DArrayI32`, which
  /// always name their mip explicitly.
  llvm::Value *UseExplicitLod = nullptr;
  /// `Sample2D`/`SampleCube`/`SampleCubeArray`/`Sample2DArray` (roadmap
  /// L58/L60(a)), `Sample1D`/`Sample1DArray` (roadmap L61(c)), and
  /// `Sample3D` (roadmap L67(a)): SPIR-V's own `Bias` image operand, an
  /// additional term added to the implicit-LOD footprint's own raw LOD
  /// before the sampler's own bias/clamp runs (see `createSample2D`'s
  /// doc); null for every other kind.
  llvm::Value *Bias = nullptr;
  /// `SampleCmp2D`/`SampleCmpArray2D`/`SampleCmpCube`/`SampleCmpCubeArray`/
  /// `SampleCmp1D`/`SampleCmpArray1D` (roadmap L54) and `GatherCmp2D`
  /// (roadmap L7d) only: the depth-comparison reference value.
  llvm::Value *Dref = nullptr;
  /// `Gather2D` (roadmap L7g) only: the 32-bit integer 0-3 selecting
  /// which of the four sampled texel components (R/G/B/A) to gather.
  llvm::Value *Component = nullptr;
  /// `Sample2D`/`Sample2DArray` (roadmap L26/L33), `SampleCmp2D`/
  /// `SampleCmpArray2D` (roadmap L50d), and `Sample3D` (roadmap L67(c))
  /// only: the integer `<ConstOffset>` texel offset's X/Y components (see
  /// `createSample2D`'s doc); null for every other kind, including every
  /// non-`Plain2D`/`Array2D`/`Plain3D` sampled kind (SPIR-V forbids a real
  /// `ConstOffset` against any of those shapes -- see
  /// `isSupportedOffset`'s comment in `SPIRVResourceLowering.cpp`).
  /// `Sample1D`/`Sample1DArray` (roadmap L66(d)) also populate `OffsetX`
  /// alone (never `OffsetY`) with their own bare scalar `ConstOffset`,
  /// rather than a vector's first component -- see `createSample1D`'s own
  /// updated doc for why this shape's offset is a scalar, not a vector.
  /// `SampleCmp1D`/`SampleCmpArray1D` (roadmap L66(k)) populate `OffsetX`
  /// the same scalar way, now that a real `deqp-vk` SPIR-V capture of
  /// `sampler1d{,array}shadow_bias_fragment` confirms a depth-comparison
  /// sample's own `ConstOffset` is the same bare scalar `i32` an ordinary
  /// sample's is, despite this shape's own dref-widened `Coordinate`
  /// staying a genuine vector (see `createSampleCmp1D`'s own updated doc).
  llvm::Value *OffsetX = nullptr;
  llvm::Value *OffsetY = nullptr;
  /// `Sample3D` only (roadmap L67(c)): the same `<ConstOffset>` texel
  /// offset's own third, depth-axis Z component -- `Plain3D`'s own
  /// `ConstOffset` is a genuine `<3 x i32>`, unlike `Plain2D`'s/
  /// `Array2D`'s own 2-component one; null for every other kind.
  llvm::Value *OffsetZ = nullptr;
  /// `Sample2D`/`SampleCube`/`SampleCubeArray`/`Sample2DArray`/
  /// `SampleCmp2D`/`SampleCmpArray2D`/`SampleCmpCube`/`SampleCmpCubeArray`
  /// (roadmap L26/L60(a)/L52(c)), `Sample1D`/`Sample1DArray`/
  /// `SampleCmp1D`/`SampleCmpArray1D` (roadmap L61(c)/L62), and `Sample3D`
  /// (roadmap L67(a)): the `MinLod` clamp floor on the implicit LOD (see
  /// `createSample2D`'s doc); null for every other kind.
  llvm::Value *MinLodClamp = nullptr;
  /// `Load2D`/`Load2DI32`/`Load2DArray`/`Load2DArrayI32` only (roadmap
  /// F8c/H19g/H19m): the multisample index a `subpassLoad`'s
  /// explicit-sample form (`Load2D`) or a plain or arrayed multisampled
  /// storage image's own `OpImageRead` (`Load2DI32`, roadmap H19g;
  /// `Load2DArrayI32`, roadmap H19m) threads through; null for every
  /// sampled kind.
  llvm::Value *Sample = nullptr;
  /// `Store2D`/`Store2DI32`/`Store2DArray`/`Store2DArrayI32`/`Store1D`/
  /// `Store1DI32`/`Store3D`/`Store3DI32`/`Store1DArray`/`Store1DArrayI32`/
  /// `Store2DMS`/`Store2DMSI32`/`Store2DArrayMS`/`Store2DArrayMSI32`
  /// only: the `<4 x float>`/`<4 x i32>` texel value being written; null
  /// for every read-only kind.
  llvm::Value *Texel = nullptr;
  llvm::Value *Mask = nullptr;
  /// `AtomicAdd2D`/`AtomicSub2D`/`AtomicAnd2D`/`AtomicOr2D`/`AtomicXor2D`/
  /// `AtomicSMax2D`/`AtomicSMin2D`/`AtomicUMax2D`/`AtomicUMin2D`/
  /// `AtomicExchange2D`/`AtomicCompareExchange2D` (roadmap H8v) only: the
  /// scalar `i32` value operand every atomic call carries (the value
  /// added/compared-against and, for every kind but a failed
  /// compare-exchange, written); null for every other kind. Unlike
  /// `Texel`, this is always scalar, never vector -- but non-null exactly
  /// when this call has the same real, must-be-mask-gated memory side
  /// effect `Texel`'s own non-null-ness signals for a store (see
  /// `FunctionWidener::widenImageCall`'s own `LaneMaskBase` choice).
  llvm::Value *AtomicValue = nullptr;
  /// `AtomicCompareExchange2D` (roadmap H8v) only: the scalar `i32`
  /// comparator operand; null for every other kind, including every other
  /// atomic kind.
  llvm::Value *Comparator = nullptr;
};

/// Returns the canonical `feme.cpu.image.*` name for \p Kind, e.g.
/// `feme.cpu.image.sample.2d.v4f32`.
llvm::StringRef getImageCallName(ImageCallKind Kind);

/// Gets (inserting if absent) the `feme.cpu.image.*` declaration for
/// \p Kind in \p M.
llvm::Function *getOrInsertImageCall(llvm::Module &M, ImageCallKind Kind);

/// Builds a `feme.cpu.image.sample.2d.v4f32` call. \p DUdX/\p DUdY/\p DVdX/
/// \p DVdY (roadmap H7i) are the caller's own screen-space partial
/// derivatives of \p U/\p V, used only when \p UseExplicitLod is false to
/// compute a real implicit mip level (and, when the sampler enables
/// anisotropic filtering, a multi-tap anisotropic footprint) instead of
/// always reading mip level 0 -- pass zero constants for a caller with none
/// to give (a non-fragment stage, or an explicit-LOD sample, where they are
/// ignored either way). \p Bias (roadmap L58) is SPIR-V's own `Bias` image
/// operand, added to the raw implicit LOD before the sampler's own
/// bias/clamp runs, never combined with an explicit LOD -- pass a zero
/// constant for a caller with none to give (including every explicit-LOD
/// sample, where SPIR-V forbids `Bias` outright). \p OffsetX/\p OffsetY
/// (roadmap L26) are SPIR-V's
/// own compile-time-constant `ConstOffset` image operand, an integer texel
/// offset added to every fetched texel's own address before the sampler's
/// addressing mode is applied -- pass zero constants for a caller with
/// none to give (DXIL's own `Texture2D::Sample`, which does not thread a
/// real offset through this pass yet). \p MinLodClamp (roadmap L26) is
/// SPIR-V's own `MinLod` image operand (HLSL's `Texture2D::Sample`'s
/// trailing `clamp` argument), an additional floor on the implicit LOD
/// alongside the sampler's own `minLod` -- pass negative infinity (a
/// no-op floor) for a caller with none to give.
llvm::CallInst *createSample2D(llvm::IRBuilderBase &Builder,
                               const ImageCallEnv &Env, llvm::Value *ImageIndex,
                               llvm::Value *SamplerIndex, llvm::Value *U,
                               llvm::Value *V, llvm::Value *DUdX,
                               llvm::Value *DUdY, llvm::Value *DVdX,
                               llvm::Value *DVdY, llvm::Value *Lod,
                               llvm::Value *UseExplicitLod, llvm::Value *Bias,
                               llvm::Value *OffsetX, llvm::Value *OffsetY,
                               llvm::Value *MinLodClamp, llvm::Value *Mask,
                               const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.sample.2d.v4i32` call (roadmap H109): the
/// integer-channel, always-nearest-filtered counterpart of `createSample2D`.
/// Unlike `createSample2D`, there are no `DUdX`/`DUdY`/`DVdX`/`DVdY`, `Bias`,
/// or `MinLodClamp` operands -- \p Lod is always an explicit LOD (SPIR-V
/// forbids combining `Bias`/implicit LOD with an integer-channel image's
/// mandatory `NEAREST` filtering in any case this pass has needed to
/// support yet). \p OffsetX/\p OffsetY mirror `createSample2D`'s own
/// `ConstOffset` image operand.
llvm::CallInst *createSample2DI32(llvm::IRBuilderBase &Builder,
                                  const ImageCallEnv &Env,
                                  llvm::Value *ImageIndex,
                                  llvm::Value *SamplerIndex, llvm::Value *U,
                                  llvm::Value *V, llvm::Value *Lod,
                                  llvm::Value *OffsetX, llvm::Value *OffsetY,
                                  llvm::Value *Mask,
                                  const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.samplecmp.2d.f32` call. \p OffsetX/\p OffsetY
/// (roadmap L50d) are the same `ConstOffset` image operand
/// `createSample2D` documents -- a depth-comparison sample against
/// `Plain2D` can carry a real, possibly-nonzero one too (SPIR-V's
/// `ConstOffset` image operand is equally legal against
/// `OpImageSampleDrefImplicitLod`/`OpImageSampleDrefExplicitLod`) -- pass
/// zero constants for a caller with none to give. \p MinLodClamp (roadmap
/// L52(c)) is the same `MinLod` image operand `createSample2D` documents
/// (`spv_resource_samplecmp_clamp`'s own trailing `clamp` operand,
/// legalized upstream by `ImageSampleDrefImplicitLodPattern` alongside
/// `ConstOffset` today) -- pass negative infinity (a no-op floor) for a
/// caller with none to give. \p Bias (roadmap L52(b)) is the same `Bias`
/// image operand `createSample2D` documents
/// (`spv_resource_samplecmpbias`/`.samplecmpbias.clamp`'s own bias
/// operand, which GLSL's `texture(sampler2DShadow, coord, bias)` emits
/// and HLSL has no spelling for) -- pass a zero constant (a no-op LOD
/// shift) for a caller with none to give. \p DUdX/\p DUdY/\p DVdX/\p DVdY
/// (roadmap L66(c)) are the same `Grad` image operand pair
/// `createSample2D` documents (`spv_resource_samplecmpgrad`/
/// `.samplecmpgrad.clamp`'s own `dPdx`/`dPdy` pair) -- pass zero
/// constants for a caller with none to give, which
/// `femeCpuImageSampleCmp2DF32` provably degenerates to the exact same
/// level-0 implicit-LOD result its own narrower pre-L66(c) implementation
/// always computed.
llvm::CallInst *
createSampleCmp2D(llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
                  llvm::Value *ImageIndex, llvm::Value *SamplerIndex,
                  llvm::Value *U, llvm::Value *V, llvm::Value *DUdX,
                  llvm::Value *DUdY, llvm::Value *DVdX, llvm::Value *DVdY,
                  llvm::Value *Lod, llvm::Value *UseExplicitLod,
                  llvm::Value *Dref, llvm::Value *Bias, llvm::Value *OffsetX,
                  llvm::Value *OffsetY, llvm::Value *MinLodClamp,
                  llvm::Value *Mask, const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.gathercmp.2d.v4f32` call (roadmap L7d):
/// `Plain2D` depth-comparison gather. \p OffsetX/\p OffsetY are the same
/// `ConstOffset` image operand `createSample2D`/`createSampleCmp2D`
/// document -- pass zero constants for a caller with none to give (the
/// no-offset `GatherCmp()` overload). Unlike `createSampleCmp2D`, there is
/// no `Lod`/`UseExplicitLod`/`Bias`/`DUdX`/`DUdY`/`DVdX`/`DVdY`/
/// `MinLodClamp` parameter at all: a gather instruction always operates
/// at mip level 0 per the SPIR-V spec, with no way to request otherwise,
/// so none of those concepts apply here.
llvm::CallInst *
createGatherCmp2D(llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
                  llvm::Value *ImageIndex, llvm::Value *SamplerIndex,
                  llvm::Value *U, llvm::Value *V, llvm::Value *Dref,
                  llvm::Value *OffsetX, llvm::Value *OffsetY,
                  llvm::Value *Mask, const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.gather.2d.v4f32` call (roadmap L7g): `Plain2D`
/// non-depth-comparison gather. Structurally identical to
/// `createGatherCmp2D` above (same `OffsetX`/`OffsetY`, no `Lod`/
/// `UseExplicitLod`/`Bias`/`DUdX`/`DUdY`/`DVdX`/`DVdY`/`MinLodClamp`
/// parameter), but takes \p Component (a 32-bit integer 0-3 selecting
/// which of the four sampled texel components -- R/G/B/A -- to gather)
/// in place of \p Dref.
llvm::CallInst *
createGather2D(llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
              llvm::Value *ImageIndex, llvm::Value *SamplerIndex,
              llvm::Value *U, llvm::Value *V, llvm::Value *Component,
              llvm::Value *OffsetX, llvm::Value *OffsetY, llvm::Value *Mask,
              const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.gathercmp.array2d.v4f32` call (roadmap
/// H124q): the `Array2D` counterpart of `createGatherCmp2D` above, adding
/// \p ArrayLayer (a float array-layer coordinate, mirroring
/// `createSample2DArray`'s own identical operand) right after \p V.
llvm::CallInst *
createGatherCmpArray2D(llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
                       llvm::Value *ImageIndex, llvm::Value *SamplerIndex,
                       llvm::Value *U, llvm::Value *V, llvm::Value *ArrayLayer,
                       llvm::Value *Dref, llvm::Value *OffsetX,
                       llvm::Value *OffsetY, llvm::Value *Mask,
                       const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.gather.array2d.v4f32` call (roadmap H124q):
/// the `Array2D` counterpart of `createGather2D` above, adding
/// \p ArrayLayer the same way `createGatherCmpArray2D` does to
/// `createGatherCmp2D`.
llvm::CallInst *
createGatherArray2D(llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
                    llvm::Value *ImageIndex, llvm::Value *SamplerIndex,
                    llvm::Value *U, llvm::Value *V, llvm::Value *ArrayLayer,
                    llvm::Value *Component, llvm::Value *OffsetX,
                    llvm::Value *OffsetY, llvm::Value *Mask,
                    const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.gathercmp.cube.v4f32` call (roadmap H124r):
/// the `TextureCube` counterpart of `createGatherCmp2D` above, taking a
/// 3-component direction vector \p DirX/\p DirY/\p DirZ (mirroring
/// `createSampleCube`'s own convention) in place of \p U/\p V, and no
/// \p OffsetX/\p OffsetY parameter at all -- SPIR-V forbids `ConstOffset`
/// against `Dim::Cube` outright, so there is no offset overload to
/// support.
llvm::CallInst *
createGatherCmpCube(llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
                    llvm::Value *ImageIndex, llvm::Value *SamplerIndex,
                    llvm::Value *DirX, llvm::Value *DirY, llvm::Value *DirZ,
                    llvm::Value *Dref, llvm::Value *Mask,
                    const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.gather.cube.v4f32` call (roadmap H124r): the
/// `TextureCube` counterpart of `createGather2D` above, mirroring
/// `createGatherCmpCube`'s own direction-vector coordinate and lack of an
/// offset parameter.
llvm::CallInst *createGatherCube(llvm::IRBuilderBase &Builder,
                                 const ImageCallEnv &Env,
                                 llvm::Value *ImageIndex,
                                 llvm::Value *SamplerIndex, llvm::Value *DirX,
                                 llvm::Value *DirY, llvm::Value *DirZ,
                                 llvm::Value *Component, llvm::Value *Mask,
                                 const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.load.2d.v4f32` call. \p Sample (roadmap F8c)
/// selects which sample of a multisampled image to read; pass a constant
/// `0` for a single-sample image or a caller with no sample of its own to
/// name (every caller except `FragmentWrapper.cpp`'s
/// `lowerFragmentSubpassLoad` does this today).
llvm::CallInst *createLoad2D(llvm::IRBuilderBase &Builder,
                             const ImageCallEnv &Env, llvm::Value *ImageIndex,
                             llvm::Value *X, llvm::Value *Y, llvm::Value *Mip,
                             llvm::Value *Sample, llvm::Value *Mask,
                             const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.load.2d.v4i32` call (roadmap E26). \p Sample
/// (roadmap H19g) selects which sample of a multisampled storage image to
/// read; pass a constant `0` for a single-sample image, mirroring
/// `createLoad2D`'s own `Sample` doc.
llvm::CallInst *createLoad2DI32(llvm::IRBuilderBase &Builder,
                                const ImageCallEnv &Env,
                                llvm::Value *ImageIndex, llvm::Value *X,
                                llvm::Value *Y, llvm::Value *Mip,
                                llvm::Value *Sample, llvm::Value *Mask,
                                const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.store.2d.v4f32` call (roadmap H19a): writes
/// \p Texel to a plain, non-arrayed storage image at integer coordinates
/// (\p X, \p Y), mip level 0.
llvm::CallInst *createStore2D(llvm::IRBuilderBase &Builder,
                              const ImageCallEnv &Env, llvm::Value *ImageIndex,
                              llvm::Value *X, llvm::Value *Y,
                              llvm::Value *Texel, llvm::Value *Mask,
                              const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.store.2d.v4i32` call (roadmap H19a).
llvm::CallInst *createStore2DI32(llvm::IRBuilderBase &Builder,
                                 const ImageCallEnv &Env,
                                 llvm::Value *ImageIndex, llvm::Value *X,
                                 llvm::Value *Y, llvm::Value *Texel,
                                 llvm::Value *Mask,
                                 const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.store.2darray.v4f32` call (roadmap H19b):
/// writes \p Texel to an arrayed storage image at integer coordinates
/// (\p X, \p Y), array layer \p Layer, mip level 0.
llvm::CallInst *createStore2DArray(llvm::IRBuilderBase &Builder,
                                   const ImageCallEnv &Env,
                                   llvm::Value *ImageIndex, llvm::Value *X,
                                   llvm::Value *Y, llvm::Value *Layer,
                                   llvm::Value *Texel, llvm::Value *Mask,
                                   const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.store.2darray.v4i32` call (roadmap H19b).
llvm::CallInst *createStore2DArrayI32(llvm::IRBuilderBase &Builder,
                                      const ImageCallEnv &Env,
                                      llvm::Value *ImageIndex,
                                      llvm::Value *X, llvm::Value *Y,
                                      llvm::Value *Layer, llvm::Value *Texel,
                                      llvm::Value *Mask,
                                      const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.store.2dms.v4f32` call (roadmap H19g): writes
/// \p Texel to a plain, non-arrayed multisampled storage image at integer
/// coordinates (\p X, \p Y), mip level 0, sample \p Sample.
llvm::CallInst *createStore2DMS(llvm::IRBuilderBase &Builder,
                                const ImageCallEnv &Env,
                                llvm::Value *ImageIndex, llvm::Value *X,
                                llvm::Value *Y, llvm::Value *Sample,
                                llvm::Value *Texel, llvm::Value *Mask,
                                const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.store.2dms.v4i32` call (roadmap H19g).
llvm::CallInst *createStore2DMSI32(llvm::IRBuilderBase &Builder,
                                   const ImageCallEnv &Env,
                                   llvm::Value *ImageIndex, llvm::Value *X,
                                   llvm::Value *Y, llvm::Value *Sample,
                                   llvm::Value *Texel, llvm::Value *Mask,
                                   const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.store.2darrayms.v4f32` call (roadmap H19m):
/// writes \p Texel to an arrayed multisampled storage image at integer
/// coordinates (\p X, \p Y), array layer \p Layer, sample \p Sample, mip
/// level 0 -- combining `createStore2DArray`'s own `Layer` operand with
/// `createStore2DMS`'s own `Sample` operand.
llvm::CallInst *createStore2DArrayMS(llvm::IRBuilderBase &Builder,
                                     const ImageCallEnv &Env,
                                     llvm::Value *ImageIndex, llvm::Value *X,
                                     llvm::Value *Y, llvm::Value *Layer,
                                     llvm::Value *Sample, llvm::Value *Texel,
                                     llvm::Value *Mask,
                                     const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.store.2darrayms.v4i32` call (roadmap H19m).
llvm::CallInst *createStore2DArrayMSI32(llvm::IRBuilderBase &Builder,
                                        const ImageCallEnv &Env,
                                        llvm::Value *ImageIndex,
                                        llvm::Value *X, llvm::Value *Y,
                                        llvm::Value *Layer, llvm::Value *Sample,
                                        llvm::Value *Texel, llvm::Value *Mask,
                                        const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.sample.2darray.v4f32` call (roadmap H7b-a). \p
/// DUdX/\p DUdY/\p DVdX/\p DVdY (roadmap L60(a)) mirror `createSample2D`'s
/// own screen-space partial-derivative operands -- pass zero constants for
/// a caller with none to give (a non-fragment stage, or an explicit-LOD
/// sample). \p Bias/\p MinLodClamp (roadmap L60(a)) mirror
/// `createSample2D`'s own `Bias`/`MinLodClamp` parameters -- pass a zero
/// constant/negative infinity, respectively, for a caller with neither to
/// give. \p OffsetX/\p OffsetY (roadmap L33) are the same `ConstOffset`
/// image operand `createSample2D` documents -- an ordinary (non-comparison)
/// `Array2D` sample can carry a real, possibly-nonzero one too (SPIR-V's
/// `ConstOffset` image operand is equally legal against an arrayed
/// `OpImageSampleImplicitLod`/`OpImageSampleExplicitLod`, the same way it
/// already is against `Array2D`'s own depth-comparison sample, see
/// `createSampleCmpArray2D`'s doc) -- pass zero constants for a caller with
/// none to give.
llvm::CallInst *createSample2DArray(
    llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
    llvm::Value *ImageIndex, llvm::Value *SamplerIndex, llvm::Value *U,
    llvm::Value *V, llvm::Value *ArrayLayer, llvm::Value *DUdX,
    llvm::Value *DUdY, llvm::Value *DVdX, llvm::Value *DVdY, llvm::Value *Lod,
    llvm::Value *UseExplicitLod, llvm::Value *Bias, llvm::Value *OffsetX,
    llvm::Value *OffsetY, llvm::Value *MinLodClamp, llvm::Value *Mask,
    const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.load.2darray.v4f32` call (roadmap H7b-a). See
/// `createLoad2D`'s `Sample` doc for its meaning here.
llvm::CallInst *createLoad2DArray(llvm::IRBuilderBase &Builder,
                                  const ImageCallEnv &Env,
                                  llvm::Value *ImageIndex, llvm::Value *X,
                                  llvm::Value *Y, llvm::Value *Layer,
                                  llvm::Value *Mip, llvm::Value *Sample,
                                  llvm::Value *Mask,
                                  const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.load.2darray.v4i32` call (roadmap H7b-a). \p
/// Sample (roadmap H19m) is the multisample index a plain multisampled
/// storage image's own `OpImageRead` (`Load2DI32`, roadmap H19g) threads
/// through -- see `createLoad2D`'s own `Sample` doc; pass a constant `0`
/// for a single-sample image.
llvm::CallInst *createLoad2DArrayI32(llvm::IRBuilderBase &Builder,
                                    const ImageCallEnv &Env,
                                    llvm::Value *ImageIndex, llvm::Value *X,
                                    llvm::Value *Y, llvm::Value *Layer,
                                    llvm::Value *Mip, llvm::Value *Sample,
                                    llvm::Value *Mask,
                                    const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.sample.cube.v4f32` call (roadmap H7b-a). \p
/// DirX/\p DirY/\p DirZ are the sample direction vector's components. \p
/// DDirXdX/\p DDirXdY/\p DDirYdX/\p DDirYdY/\p DDirZdX/\p DDirZdY (roadmap
/// L56) are the caller's own screen-space partial derivatives of \p DirX/
/// \p DirY/\p DirZ, consulted only for an implicit-LOD sample (see
/// `getOrSynthesizeSampleCubeDerivatives`'s doc); a caller with none to
/// give (a non-fragment stage, or an explicit-LOD sample) passes six zero
/// constants. \p Bias (roadmap L58) is the same `Bias` image operand
/// `createSample2D` documents -- pass a zero constant for a caller with
/// none to give. \p MinLodClamp (roadmap L26) is the same `MinLod` clamp
/// `createSample2D` documents -- a cube sample can carry one too (SPIR-V's
/// `MinLod` image operand is legal against any dimensionality, unlike
/// `ConstOffset`, which `Dim::Cube` forbids) -- pass negative infinity (a
/// no-op floor) for a caller with none to give.
llvm::CallInst *createSampleCube(llvm::IRBuilderBase &Builder,
                                 const ImageCallEnv &Env,
                                 llvm::Value *ImageIndex,
                                 llvm::Value *SamplerIndex, llvm::Value *DirX,
                                 llvm::Value *DirY, llvm::Value *DirZ,
                                 llvm::Value *DDirXdX, llvm::Value *DDirXdY,
                                 llvm::Value *DDirYdX, llvm::Value *DDirYdY,
                                 llvm::Value *DDirZdX, llvm::Value *DDirZdY,
                                 llvm::Value *Lod, llvm::Value *UseExplicitLod,
                                 llvm::Value *Bias, llvm::Value *MinLodClamp,
                                 llvm::Value *Mask,
                                 const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.sample.cubearray.v4f32` call (roadmap H7b-a).
/// \p DDirXdX/\p DDirXdY/\p DDirYdX/\p DDirYdY/\p DDirZdX/\p DDirZdY
/// (roadmap L56) mirror `createSampleCube`'s own new derivative operands.
/// \p Bias/\p MinLodClamp (roadmap L60(a)) mirror `createSampleCube`'s own
/// `Bias`/`MinLodClamp` parameters -- pass a zero constant/negative
/// infinity, respectively, for a caller with neither to give.
llvm::CallInst *createSampleCubeArray(
    llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
    llvm::Value *ImageIndex, llvm::Value *SamplerIndex, llvm::Value *DirX,
    llvm::Value *DirY, llvm::Value *DirZ, llvm::Value *DDirXdX,
    llvm::Value *DDirXdY, llvm::Value *DDirYdX, llvm::Value *DDirYdY,
    llvm::Value *DDirZdX, llvm::Value *DDirZdY, llvm::Value *ArrayLayer,
    llvm::Value *Lod, llvm::Value *UseExplicitLod, llvm::Value *Bias,
    llvm::Value *MinLodClamp, llvm::Value *Mask,
    const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.samplecmp.2darray.f32` call (roadmap L48), the
/// `Texture2DArray` counterpart of `createSampleCmp2D`. \p OffsetX/\p
/// OffsetY (roadmap L50d) mirror `createSampleCmp2D`'s own new
/// `ConstOffset` parameters. \p MinLodClamp (roadmap L52(c)) mirrors
/// `createSampleCmp2D`'s own new `MinLod` clamp parameter, and \p Bias
/// (roadmap L52(b)) its own new `Bias` parameter. \p DUdX/\p DUdY/\p
/// DVdX/\p DVdY (roadmap L66(g)) mirror `createSampleCmp2D`'s own
/// identically-named `Grad` operands -- only `U`/`V`, never `ArrayLayer`,
/// are ever differentiated, matching `createSample2DArray`'s own
/// identical precedent for an ordinary sample; pass zero constants for a
/// caller with none to give.
llvm::CallInst *createSampleCmpArray2D(
    llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
    llvm::Value *ImageIndex, llvm::Value *SamplerIndex, llvm::Value *U,
    llvm::Value *V, llvm::Value *ArrayLayer, llvm::Value *DUdX,
    llvm::Value *DUdY, llvm::Value *DVdX, llvm::Value *DVdY, llvm::Value *Lod,
    llvm::Value *UseExplicitLod, llvm::Value *Dref, llvm::Value *Bias,
    llvm::Value *OffsetX, llvm::Value *OffsetY, llvm::Value *MinLodClamp,
    llvm::Value *Mask, const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.samplecmp.cube.f32` call (roadmap L48), the
/// `TextureCube` counterpart of `createSampleCmp2D`. \p MinLodClamp
/// (roadmap L52(c)) mirrors `createSampleCmp2D`'s own new `MinLod` clamp
/// parameter -- a cube depth-comparison sample can carry one too (SPIR-V's
/// `MinLod` image operand is legal against any dimensionality, unlike
/// `ConstOffset`, which `Dim::Cube` forbids). \p Bias (roadmap L52(b))
/// mirrors `createSampleCmp2D`'s own new `Bias` parameter, equally legal
/// against `Dim::Cube` for the same reason. \p DDirXdX/\p DDirXdY/\p
/// DDirYdX/\p DDirYdY/\p DDirZdX/\p DDirZdY (roadmap L66(h)) mirror
/// `createSampleCube`'s own identically-named screen-space
/// direction-vector derivative operands, consulted only for an
/// implicit-LOD `Grad` sample (see `femeRTComputeCubeClampedLod`'s own
/// doc in `FeMeRuntimeCPU.c`) -- pass six zero constants for a caller with
/// none to give.
llvm::CallInst *createSampleCmpCube(
    llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
    llvm::Value *ImageIndex, llvm::Value *SamplerIndex, llvm::Value *DirX,
    llvm::Value *DirY, llvm::Value *DirZ, llvm::Value *DDirXdX,
    llvm::Value *DDirXdY, llvm::Value *DDirYdX, llvm::Value *DDirYdY,
    llvm::Value *DDirZdX, llvm::Value *DDirZdY, llvm::Value *Lod,
    llvm::Value *UseExplicitLod, llvm::Value *Dref, llvm::Value *Bias,
    llvm::Value *MinLodClamp, llvm::Value *Mask, const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.samplecmp.cubearray.f32` call (roadmap L48),
/// the `TextureCubeArray` counterpart of `createSampleCmp2D`. \p
/// MinLodClamp (roadmap L52(c)) mirrors `createSampleCmpCube`'s own new
/// `MinLod` clamp parameter, and \p Bias (roadmap L52(b)) its own new
/// `Bias` parameter. \p DDirXdX/\p DDirXdY/\p DDirYdX/\p DDirYdY/\p
/// DDirZdX/\p DDirZdY (roadmap L66(i)) mirror `createSampleCmpCube`'s own
/// identically-named screen-space direction-vector derivative operands
/// (roadmap L66(h)), consulted only for an implicit-LOD `Grad` sample --
/// pass six zero constants for a caller with none to give.
llvm::CallInst *createSampleCmpCubeArray(
    llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
    llvm::Value *ImageIndex, llvm::Value *SamplerIndex, llvm::Value *DirX,
    llvm::Value *DirY, llvm::Value *DirZ, llvm::Value *DDirXdX,
    llvm::Value *DDirXdY, llvm::Value *DDirYdX, llvm::Value *DDirYdY,
    llvm::Value *DDirZdX, llvm::Value *DDirZdY, llvm::Value *ArrayLayer,
    llvm::Value *Lod, llvm::Value *UseExplicitLod, llvm::Value *Dref,
    llvm::Value *Bias, llvm::Value *MinLodClamp, llvm::Value *Mask,
    const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.sample.1d.v4f32` call (roadmap L52a). \p U is
/// the single normalized coordinate. \p DUdX/\p DUdY (roadmap L63) mirror
/// `createSample2D`'s own screen-space partial-derivative operands,
/// narrowed to this shape's single addressed component -- pass zero
/// constants for a caller with none to give (a non-fragment stage, or an
/// explicit-LOD sample); see `getOrSynthesizeSample1DDerivatives`'s own
/// doc. \p Bias/\p MinLodClamp (roadmap L61(c)) mirror `createSample2D`'s
/// own identically-named parameters -- a `Plain1D` sample can carry a real
/// `Bias`/`MinLod` clamp too. \p Offset (roadmap L66(d)) is the same
/// `ConstOffset` image operand `createSample2D`'s own `OffsetX`/`OffsetY`
/// document, narrowed to a single scalar `i32` -- unlike `Plain2D`'s/
/// `Array2D`'s/`Plain3D`'s own vector-typed offset, a real `deqp-vk` SPIR-V
/// capture confirms glslang always emits a bare scalar `ConstOffset`
/// against a 1D (possibly arrayed) sampler, matching `Plain1D`'s own bare
/// scalar `U` coordinate rather than a vector the array layer would
/// otherwise widen (see `isSupportedOffset`'s own updated comment in
/// `SPIRVResourceLowering.cpp`) -- pass a zero constant for a caller with
/// none to give.
llvm::CallInst *createSample1D(llvm::IRBuilderBase &Builder,
                               const ImageCallEnv &Env, llvm::Value *ImageIndex,
                               llvm::Value *SamplerIndex, llvm::Value *U,
                               llvm::Value *DUdX, llvm::Value *DUdY,
                               llvm::Value *Lod, llvm::Value *UseExplicitLod,
                               llvm::Value *Bias, llvm::Value *Offset,
                               llvm::Value *MinLodClamp, llvm::Value *Mask,
                               const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.sample.1darray.v4f32` call (roadmap L52a), the
/// `Texture1DArray` counterpart of `createSample1D`. \p DUdX/\p DUdY
/// (roadmap L63) mirror `createSample1D`'s own new derivative parameters --
/// only \p U itself is ever differentiated, never \p ArrayLayer, mirroring
/// `createSample2DArray`'s own layer-agnostic derivative handling. \p
/// Bias/\p MinLodClamp (roadmap L61(c)) mirror `createSample1D`'s own
/// identically new parameters. \p Offset (roadmap L66(d)) mirrors
/// `createSample1D`'s own new, bare-scalar `Offset` parameter -- confirmed
/// via a real `deqp-vk` SPIR-V capture that `Array1D`'s own `ConstOffset`
/// stays a scalar too, despite its own 2-component `(U, ArrayLayer)`
/// coordinate: SPIR-V's own `ConstOffset` dimensionality excludes the
/// array layer, the same "+1" carve-out `GradDerivativeWidth`
/// (roadmap L64) already applies to a `Grad` derivative.
llvm::CallInst *
createSample1DArray(llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
                    llvm::Value *ImageIndex, llvm::Value *SamplerIndex,
                    llvm::Value *U, llvm::Value *ArrayLayer, llvm::Value *DUdX,
                    llvm::Value *DUdY, llvm::Value *Lod,
                    llvm::Value *UseExplicitLod, llvm::Value *Bias,
                    llvm::Value *Offset, llvm::Value *MinLodClamp,
                    llvm::Value *Mask, const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.samplecmp.1d.f32` call (roadmap L54), the
/// depth-comparison counterpart of `createSample1D`. \p Bias/\p MinLodClamp
/// (roadmap L62) mirror `createSampleCmpCube`'s own identically-named
/// parameters. \p DUdX/\p DUdY (roadmap L66(f)) are a real screen-space
/// derivative pair for a `Grad` sample -- bare scalars, mirroring
/// `createSample1D`'s own identically-named parameters, since `Plain1D`'s
/// addressing is itself already a bare scalar; zero constants for every
/// non-`Grad` form. \p Offset (roadmap L66(k)) mirrors `createSample1D`'s
/// own bare-scalar `Offset` parameter -- a real `deqp-vk` SPIR-V capture
/// of `sampler1dshadow_bias_fragment` confirms a depth-comparison
/// sample's own `ConstOffset` is a scalar `i32` too, despite this shape's
/// own dref-widened `Coordinate` staying a genuine vector (see
/// `hasOnlySupportedImageUses`'s own updated comment in
/// `SPIRVResourceLowering.cpp`); a zero constant for the trivial
/// always-zero case.
llvm::CallInst *
createSampleCmp1D(llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
                  llvm::Value *ImageIndex, llvm::Value *SamplerIndex,
                  llvm::Value *U, llvm::Value *DUdX, llvm::Value *DUdY,
                  llvm::Value *Lod, llvm::Value *UseExplicitLod,
                  llvm::Value *Dref, llvm::Value *Bias, llvm::Value *Offset,
                  llvm::Value *MinLodClamp, llvm::Value *Mask,
                  const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.samplecmp.1darray.f32` call (roadmap L54), the
/// `Texture1DArray` counterpart of `createSampleCmp1D`. \p Bias/\p
/// MinLodClamp (roadmap L62) mirror that function's own new parameters.
/// \p DUdX/\p DUdY (roadmap L66(f)) mirror `createSampleCmp1D`'s own
/// identically-named new parameters -- only `U`, never `ArrayLayer`, is
/// ever differentiated. \p Offset (roadmap L66(k)) mirrors
/// `createSampleCmp1D`'s own new, bare-scalar `Offset` parameter --
/// `Array1D`'s own `ConstOffset` never touches `ArrayLayer` either,
/// matching `createSample1DArray`'s own identical precedent for an
/// ordinary sample.
llvm::CallInst *createSampleCmpArray1D(
    llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
    llvm::Value *ImageIndex, llvm::Value *SamplerIndex, llvm::Value *U,
    llvm::Value *ArrayLayer, llvm::Value *DUdX, llvm::Value *DUdY,
    llvm::Value *Lod, llvm::Value *UseExplicitLod, llvm::Value *Dref,
    llvm::Value *Bias, llvm::Value *Offset, llvm::Value *MinLodClamp,
    llvm::Value *Mask, const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.querylod.2d.v2f32` call (roadmap L52e): see
/// `ImageCallKind::QueryLod2D`'s own doc for its `<2 x float>` result
/// shape. \p DUdX/\p DUdY/\p DVdX/\p DVdY are the caller's own screen-space
/// partial derivatives of the sampled coordinate -- unlike `createSample2D`,
/// always real ones (see `getOrSynthesizeSample2DDerivatives`), never zero
/// constants, since `OpImageQueryLod` has no explicit-LOD form to fall
/// back to.
llvm::CallInst *createQueryLod2D(llvm::IRBuilderBase &Builder,
                                 const ImageCallEnv &Env,
                                 llvm::Value *ImageIndex,
                                 llvm::Value *SamplerIndex, llvm::Value *DUdX,
                                 llvm::Value *DUdY, llvm::Value *DVdX,
                                 llvm::Value *DVdY, llvm::Value *Mask,
                                 const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.querylod.cube.v2f32` call (roadmap H124u):
/// see `ImageCallKind::QueryLodCube`'s own doc for its `<2 x float>`
/// result shape and its operand list's own rationale (a direction vector
/// plus its own raw screen-space derivatives, rather than `QueryLod2D`'s
/// already-face-local `(DUdX, DUdY, DVdX, DVdY)` pair).
llvm::CallInst *
createQueryLodCube(llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
                   llvm::Value *ImageIndex, llvm::Value *SamplerIndex,
                   llvm::Value *DirX, llvm::Value *DirY, llvm::Value *DirZ,
                   llvm::Value *DDirXdX, llvm::Value *DDirXdY,
                   llvm::Value *DDirYdX, llvm::Value *DDirYdY,
                   llvm::Value *DDirZdX, llvm::Value *DDirZdY,
                   llvm::Value *Mask, const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.sample.3d.v4f32` call (roadmap L66(a),
/// extended with a real \p Bias/\p MinLodClamp pair by roadmap L67(a) and
/// a real \p OffsetX/\p OffsetY/\p OffsetZ triple by roadmap L67(c)): a
/// `Plain3D` ordinary sample, mirroring `createSample1D`'s own operand
/// order (coordinate, then its own screen-space derivative pair(s), then
/// `Lod`/`UseExplicitLod`/`Bias`/`OffsetX`/`OffsetY`/`OffsetZ`/
/// `MinLodClamp`/`Mask`), extended to a real `(U, V, W)` coordinate and
/// its own `DUdX`/`DUdY`/`DVdX`/`DVdY`/`DWdX`/`DWdY` derivative triple --
/// either synthesized for an implicit-LOD sample or the caller's own real
/// `Grad` derivative (roadmap L67(b); this builder itself is agnostic to
/// which, same as `createSample1D`'s own precedent). \p Bias/\p
/// MinLodClamp mirror `createSample1D`'s own identically-named
/// parameters -- a `Plain3D` sample can carry a real `Bias`/`MinLod`
/// clamp too. \p OffsetX/\p OffsetY/\p OffsetZ (roadmap L67(c)) are the
/// same `ConstOffset` image operand `createSample2D`'s own `OffsetX`/
/// `OffsetY` document, widened to a real third, depth-axis component --
/// `Plain3D`'s own `ConstOffset` is a genuine `<3 x i32>` (mirroring its
/// own 3-component `(U, V, W)` coordinate width), unlike `Plain2D`'s/
/// `Array2D`'s own 2-component one -- pass zero constants for a caller
/// with none to give.
llvm::CallInst *createSample3D(
    llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
    llvm::Value *ImageIndex, llvm::Value *SamplerIndex, llvm::Value *U,
    llvm::Value *V, llvm::Value *W, llvm::Value *DUdX, llvm::Value *DUdY,
    llvm::Value *DVdX, llvm::Value *DVdY, llvm::Value *DWdX, llvm::Value *DWdY,
    llvm::Value *Lod, llvm::Value *UseExplicitLod, llvm::Value *Bias,
    llvm::Value *OffsetX, llvm::Value *OffsetY, llvm::Value *OffsetZ,
    llvm::Value *MinLodClamp, llvm::Value *Mask,
    const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.getdimensions.2d.v2i32` call (roadmap L70):
/// see `ImageCallKind::GetDimensions2D`'s own doc for its `<2 x i32>`
/// result shape (lane 0 width, lane 1 height). Takes only \p ImageIndex
/// and \p Mask from \p Env's image heap -- no sampler heap, and no
/// coordinate operand of any kind, unlike every sample/fetch builder
/// above.
llvm::CallInst *createGetDimensions2D(llvm::IRBuilderBase &Builder,
                                      const ImageCallEnv &Env,
                                      llvm::Value *ImageIndex,
                                      llvm::Value *Mask,
                                      const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.getdimensions.lod.2d.v2i32` call (roadmap
/// L72(d)): see `ImageCallKind::QuerySizeLod2D`'s own doc for its
/// `<2 x i32>` result shape. Same operand list as `createGetDimensions2D`
/// plus \p Lod, the explicit mip level to query.
llvm::CallInst *createQuerySizeLod2D(llvm::IRBuilderBase &Builder,
                                     const ImageCallEnv &Env,
                                     llvm::Value *ImageIndex, llvm::Value *Lod,
                                     llvm::Value *Mask,
                                     const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.getdimensions.lod.1d.i32` call (roadmap L75):
/// see `ImageCallKind::QuerySizeLod1D`'s own doc for its scalar `i32`
/// result. Same operand list as `createQuerySizeLod2D`.
llvm::CallInst *createQuerySizeLod1D(llvm::IRBuilderBase &Builder,
                                     const ImageCallEnv &Env,
                                     llvm::Value *ImageIndex, llvm::Value *Lod,
                                     llvm::Value *Mask,
                                     const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.getdimensions.lod.1darray.v2i32` call
/// (roadmap L75): see `ImageCallKind::QuerySizeLod1DArray`'s own doc for
/// its `<2 x i32>` result shape (lane 0 width, lane 1 array-layer count).
/// Same operand list as `createQuerySizeLod2D`.
llvm::CallInst *createQuerySizeLod1DArray(llvm::IRBuilderBase &Builder,
                                          const ImageCallEnv &Env,
                                          llvm::Value *ImageIndex,
                                          llvm::Value *Lod, llvm::Value *Mask,
                                          const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.getdimensions.lod.2darray.v3i32` call
/// (roadmap L75): see `ImageCallKind::QuerySizeLod2DArray`'s own doc for
/// its `<3 x i32>` result shape (lane 0 width, lane 1 height, lane 2
/// array-layer count). Same operand list as `createQuerySizeLod2D`.
llvm::CallInst *createQuerySizeLod2DArray(llvm::IRBuilderBase &Builder,
                                          const ImageCallEnv &Env,
                                          llvm::Value *ImageIndex,
                                          llvm::Value *Lod, llvm::Value *Mask,
                                          const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.getdimensions.lod.3d.v3i32` call (roadmap
/// L75): see `ImageCallKind::QuerySizeLod3D`'s own doc for its
/// `<3 x i32>` result shape (lane 0 width, lane 1 height, lane 2 depth,
/// all three shrinking with \p Lod). Same operand list as
/// `createQuerySizeLod2D`.
llvm::CallInst *createQuerySizeLod3D(llvm::IRBuilderBase &Builder,
                                     const ImageCallEnv &Env,
                                     llvm::Value *ImageIndex, llvm::Value *Lod,
                                     llvm::Value *Mask,
                                     const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.getdimensions.lod.cubearray.v3i32` call
/// (roadmap L75): see `ImageCallKind::QuerySizeLodCubeArray`'s own doc
/// for its `<3 x i32>` result shape (lane 0 width, lane 1 height, lane 2
/// cube-array *element* count, i.e. `ArrayLayers / 6`). Same operand list
/// as `createQuerySizeLod2D`.
llvm::CallInst *createQuerySizeLodCubeArray(llvm::IRBuilderBase &Builder,
                                            const ImageCallEnv &Env,
                                            llvm::Value *ImageIndex,
                                            llvm::Value *Lod, llvm::Value *Mask,
                                            const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.querylevels.i32` call (roadmap L72(d)): see
/// `ImageCallKind::QueryLevels`'s own doc for its scalar `i32` result.
/// Takes only \p ImageIndex from \p Env's image heap -- no `Mask`, no
/// sampler heap, and no coordinate/mip-level operand of any kind, the
/// smallest operand list of any `feme.cpu.image.*` builder.
llvm::CallInst *createQueryLevels(llvm::IRBuilderBase &Builder,
                                  const ImageCallEnv &Env,
                                  llvm::Value *ImageIndex,
                                  const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.querysamples.i32` call (roadmap L73): see
/// `ImageCallKind::QuerySamples`'s own doc for its scalar `i32` result.
/// Same operand list as `createQueryLevels` -- only \p ImageIndex from
/// \p Env's image heap, no `Mask`/sampler heap/coordinate operand of any
/// kind.
llvm::CallInst *createQuerySamples(llvm::IRBuilderBase &Builder,
                                   const ImageCallEnv &Env,
                                   llvm::Value *ImageIndex,
                                   const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.load.1d.v4f32` call (roadmap H19c). See
/// `createLoad2D`'s `Sample` doc for its meaning here.
llvm::CallInst *createLoad1D(llvm::IRBuilderBase &Builder,
                             const ImageCallEnv &Env, llvm::Value *ImageIndex,
                             llvm::Value *X, llvm::Value *Mip,
                             llvm::Value *Sample, llvm::Value *Mask,
                             const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.load.1d.v4i32` call (roadmap H19c).
llvm::CallInst *createLoad1DI32(llvm::IRBuilderBase &Builder,
                                const ImageCallEnv &Env,
                                llvm::Value *ImageIndex, llvm::Value *X,
                                llvm::Value *Mip, llvm::Value *Mask,
                                const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.store.1d.v4f32` call (roadmap H19c): writes
/// \p Texel to a plain 1D storage image at integer coordinate \p X, mip
/// level 0.
llvm::CallInst *createStore1D(llvm::IRBuilderBase &Builder,
                              const ImageCallEnv &Env, llvm::Value *ImageIndex,
                              llvm::Value *X, llvm::Value *Texel,
                              llvm::Value *Mask, const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.store.1d.v4i32` call (roadmap H19c).
llvm::CallInst *createStore1DI32(llvm::IRBuilderBase &Builder,
                                 const ImageCallEnv &Env,
                                 llvm::Value *ImageIndex, llvm::Value *X,
                                 llvm::Value *Texel, llvm::Value *Mask,
                                 const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.load.3d.v4f32` call (roadmap H19c). See
/// `createLoad2D`'s `Sample` doc for its meaning here.
llvm::CallInst *createLoad3D(llvm::IRBuilderBase &Builder,
                             const ImageCallEnv &Env, llvm::Value *ImageIndex,
                             llvm::Value *X, llvm::Value *Y, llvm::Value *Z,
                             llvm::Value *Mip, llvm::Value *Sample,
                             llvm::Value *Mask, const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.load.3d.v4i32` call (roadmap H19c).
llvm::CallInst *createLoad3DI32(llvm::IRBuilderBase &Builder,
                                const ImageCallEnv &Env,
                                llvm::Value *ImageIndex, llvm::Value *X,
                                llvm::Value *Y, llvm::Value *Z,
                                llvm::Value *Mip, llvm::Value *Mask,
                                const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.store.3d.v4f32` call (roadmap H19c): writes
/// \p Texel to a plain 3D storage image at integer coordinates
/// (\p X, \p Y, \p Z), mip level 0.
llvm::CallInst *createStore3D(llvm::IRBuilderBase &Builder,
                              const ImageCallEnv &Env, llvm::Value *ImageIndex,
                              llvm::Value *X, llvm::Value *Y, llvm::Value *Z,
                              llvm::Value *Texel, llvm::Value *Mask,
                              const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.store.3d.v4i32` call (roadmap H19c).
llvm::CallInst *createStore3DI32(llvm::IRBuilderBase &Builder,
                                 const ImageCallEnv &Env,
                                 llvm::Value *ImageIndex, llvm::Value *X,
                                 llvm::Value *Y, llvm::Value *Z,
                                 llvm::Value *Texel, llvm::Value *Mask,
                                 const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.load.1darray.v4f32` call (roadmap H19e). See
/// `createLoad2D`'s `Sample` doc for its meaning here.
llvm::CallInst *createLoad1DArray(llvm::IRBuilderBase &Builder,
                                  const ImageCallEnv &Env,
                                  llvm::Value *ImageIndex, llvm::Value *X,
                                  llvm::Value *Layer, llvm::Value *Mip,
                                  llvm::Value *Sample, llvm::Value *Mask,
                                  const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.load.1darray.v4i32` call (roadmap H19e).
llvm::CallInst *createLoad1DArrayI32(llvm::IRBuilderBase &Builder,
                                     const ImageCallEnv &Env,
                                     llvm::Value *ImageIndex, llvm::Value *X,
                                     llvm::Value *Layer, llvm::Value *Mip,
                                     llvm::Value *Mask,
                                     const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.store.1darray.v4f32` call (roadmap H19e):
/// writes \p Texel to an arrayed 1D storage image at integer coordinate
/// \p X, array layer \p Layer, mip level 0.
llvm::CallInst *createStore1DArray(llvm::IRBuilderBase &Builder,
                                   const ImageCallEnv &Env,
                                   llvm::Value *ImageIndex, llvm::Value *X,
                                   llvm::Value *Layer, llvm::Value *Texel,
                                   llvm::Value *Mask,
                                   const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.store.1darray.v4i32` call (roadmap H19e).
llvm::CallInst *createStore1DArrayI32(llvm::IRBuilderBase &Builder,
                                      const ImageCallEnv &Env,
                                      llvm::Value *ImageIndex,
                                      llvm::Value *X, llvm::Value *Layer,
                                      llvm::Value *Texel, llvm::Value *Mask,
                                      const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.atomic.add.2d.i32` call (roadmap H8v): performs
/// `*texel += Value` at integer coordinates (\p X, \p Y), mip level 0, of a
/// plain, non-arrayed, single-32-bit-scalar-format storage image, returning
/// the pre-op value.
llvm::CallInst *createAtomicAdd2D(llvm::IRBuilderBase &Builder,
                                  const ImageCallEnv &Env,
                                  llvm::Value *ImageIndex, llvm::Value *X,
                                  llvm::Value *Y, llvm::Value *Value,
                                  llvm::Value *Mask,
                                  const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.atomic.sub.2d.i32` call (roadmap H8v). See
/// `createAtomicAdd2D`'s own doc.
llvm::CallInst *createAtomicSub2D(llvm::IRBuilderBase &Builder,
                                  const ImageCallEnv &Env,
                                  llvm::Value *ImageIndex, llvm::Value *X,
                                  llvm::Value *Y, llvm::Value *Value,
                                  llvm::Value *Mask,
                                  const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.atomic.and.2d.i32` call (roadmap H8v). See
/// `createAtomicAdd2D`'s own doc.
llvm::CallInst *createAtomicAnd2D(llvm::IRBuilderBase &Builder,
                                  const ImageCallEnv &Env,
                                  llvm::Value *ImageIndex, llvm::Value *X,
                                  llvm::Value *Y, llvm::Value *Value,
                                  llvm::Value *Mask,
                                  const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.atomic.or.2d.i32` call (roadmap H8v). See
/// `createAtomicAdd2D`'s own doc.
llvm::CallInst *createAtomicOr2D(llvm::IRBuilderBase &Builder,
                                 const ImageCallEnv &Env,
                                 llvm::Value *ImageIndex, llvm::Value *X,
                                 llvm::Value *Y, llvm::Value *Value,
                                 llvm::Value *Mask,
                                 const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.atomic.xor.2d.i32` call (roadmap H8v). See
/// `createAtomicAdd2D`'s own doc.
llvm::CallInst *createAtomicXor2D(llvm::IRBuilderBase &Builder,
                                  const ImageCallEnv &Env,
                                  llvm::Value *ImageIndex, llvm::Value *X,
                                  llvm::Value *Y, llvm::Value *Value,
                                  llvm::Value *Mask,
                                  const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.atomic.smax.2d.i32` call (roadmap H8v). See
/// `createAtomicAdd2D`'s own doc.
llvm::CallInst *createAtomicSMax2D(llvm::IRBuilderBase &Builder,
                                   const ImageCallEnv &Env,
                                   llvm::Value *ImageIndex, llvm::Value *X,
                                   llvm::Value *Y, llvm::Value *Value,
                                   llvm::Value *Mask,
                                   const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.atomic.smin.2d.i32` call (roadmap H8v). See
/// `createAtomicAdd2D`'s own doc.
llvm::CallInst *createAtomicSMin2D(llvm::IRBuilderBase &Builder,
                                   const ImageCallEnv &Env,
                                   llvm::Value *ImageIndex, llvm::Value *X,
                                   llvm::Value *Y, llvm::Value *Value,
                                   llvm::Value *Mask,
                                   const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.atomic.umax.2d.i32` call (roadmap H8v). See
/// `createAtomicAdd2D`'s own doc.
llvm::CallInst *createAtomicUMax2D(llvm::IRBuilderBase &Builder,
                                   const ImageCallEnv &Env,
                                   llvm::Value *ImageIndex, llvm::Value *X,
                                   llvm::Value *Y, llvm::Value *Value,
                                   llvm::Value *Mask,
                                   const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.atomic.umin.2d.i32` call (roadmap H8v). See
/// `createAtomicAdd2D`'s own doc.
llvm::CallInst *createAtomicUMin2D(llvm::IRBuilderBase &Builder,
                                   const ImageCallEnv &Env,
                                   llvm::Value *ImageIndex, llvm::Value *X,
                                   llvm::Value *Y, llvm::Value *Value,
                                   llvm::Value *Mask,
                                   const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.atomic.exchange.2d.i32` call (roadmap H8v). See
/// `createAtomicAdd2D`'s own doc.
llvm::CallInst *createAtomicExchange2D(llvm::IRBuilderBase &Builder,
                                       const ImageCallEnv &Env,
                                       llvm::Value *ImageIndex,
                                       llvm::Value *X, llvm::Value *Y,
                                       llvm::Value *Value, llvm::Value *Mask,
                                       const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.atomic.compare_exchange.2d.i32` call (roadmap
/// H8v): like `createAtomicAdd2D`, but only replaces `*texel` with \p Value
/// when it currently equals \p Comparator -- either way, returns the
/// pre-op value.
llvm::CallInst *createAtomicCompareExchange2D(
    llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
    llvm::Value *ImageIndex, llvm::Value *X, llvm::Value *Y,
    llvm::Value *Comparator, llvm::Value *Value, llvm::Value *Mask,
    const llvm::Twine &Name = "");

/// Recognizes \p CI as one of the canonical `feme.cpu.image.*` calls,
/// returning its decoded operands, or `std::nullopt` if \p CI's callee isn't
/// one.
std::optional<MatchedImageCall> matchImageCall(const llvm::CallInst &CI);

/// The four screen-space partial-derivative operands `createSample2D`'s
/// implicit-LOD path consults (roadmap H7i): `DUdX`, `DUdY`, `DVdX`, `DVdY`,
/// in that order.
struct SampleDerivatives {
  llvm::Value *DUdX;
  llvm::Value *DUdY;
  llvm::Value *DVdX;
  llvm::Value *DVdY;
};

/// Returns the four screen-space partial derivatives of \p U/\p V an
/// implicit-LOD `Sample2D` call should pass to `createSample2D`, given that
/// \p Caller (the function \p Builder is inserting into) declares
/// \p RequiredStage (its own entry-point `ShaderStage`, `feme::ShaderStage`,
/// via `feme::getShaderStage`): real derivatives, synthesized via
/// `feme::createStageDerivative` (`feme.stage.derivative.x.coarse`/
/// `.y.coarse`, later lowered by `feme::cpu::WaveLoweringPass`'s existing
/// quad-lane machinery) when \p Caller's own stage is `Fragment` -- the
/// only stage GLSL/HLSL's own implicit `texture()`/`Sample()` is ever legal
/// from -- or four zero constants otherwise (an explicit-LOD sample, or
/// (defensively) any other stage), leaving that sample's own implicit level
/// resolved to mip 0 exactly as before this row.
SampleDerivatives getOrSynthesizeSample2DDerivatives(llvm::IRBuilderBase &B,
                                                     llvm::Function &Caller,
                                                     llvm::Value *U,
                                                     llvm::Value *V);

/// (Roadmap L63) The two screen-space partial-derivative operands
/// `createSample1D`/`createSample1DArray`'s implicit-LOD path consults:
/// `DUdX`, `DUdY` -- the `Plain1D`/`Array1D` counterpart of
/// `SampleDerivatives` above, narrowed to a single addressed coordinate
/// component (this shape's own `U` is the only one ever differentiated;
/// `Array1D`'s own array layer, like `Array2D`'s, is never
/// differentiated).
struct SampleDerivatives1D {
  llvm::Value *DUdX;
  llvm::Value *DUdY;
};

/// Returns the two screen-space partial derivatives of \p U an
/// implicit-LOD `Sample1D`/`Sample1DArray` call should pass to
/// `createSample1D`/`createSample1DArray`, mirroring
/// `getOrSynthesizeSample2DDerivatives`'s own `Caller`-stage-gated
/// real-derivatives-or-zero-constants behavior: real derivatives
/// (synthesized via `feme::createStageDerivative`) only when \p Caller's
/// own stage is `Fragment`, else two zero constants, leaving that
/// sample's own implicit level resolved to mip 0 exactly as before this
/// row (roadmap L52a's original, deliberately-narrower `Plain1D`/
/// `Array1D` scope) -- not a regression for any caller not in a position
/// to synthesize a real derivative.
SampleDerivatives1D getOrSynthesizeSample1DDerivatives(llvm::IRBuilderBase &B,
                                                       llvm::Function &Caller,
                                                       llvm::Value *U);

/// (Roadmap L56) The six screen-space partial-derivative operands
/// `createSampleCube`/`createSampleCubeArray`'s implicit-LOD path
/// consults: the direction vector's own `X`/`Y`/`Z` components' partial
/// derivatives with respect to the screen-space X axis
/// (`DDirXdX`/`DDirYdX`/`DDirZdX`), then with respect to Y
/// (`DDirXdY`/`DDirYdY`/`DDirZdY`) -- the `Cube`/`CubeArray` counterpart
/// of `SampleDerivatives` above, differentiating the whole 3-component
/// direction vector itself rather than an already-face-local 2D
/// coordinate, since which face is selected (and therefore what the
/// face-local coordinate even means) isn't known until the runtime sees
/// concrete `(DirX, DirY, DirZ)` values -- see
/// `femeRTComputeCubeUVDerivatives` (`FeMeRuntimeCPU.c`), which turns
/// these back into a face-local `(DUdX, DUdY, DVdX, DVdY)` once a face
/// has actually been selected.
struct CubeDirectionDerivatives {
  llvm::Value *DDirXdX;
  llvm::Value *DDirXdY;
  llvm::Value *DDirYdX;
  llvm::Value *DDirYdY;
  llvm::Value *DDirZdX;
  llvm::Value *DDirZdY;
};

/// Returns the six screen-space partial derivatives of \p DirX/\p DirY/
/// \p DirZ an implicit-LOD `SampleCube`/`SampleCubeArray` call should pass
/// to `createSampleCube`/`createSampleCubeArray`, mirroring
/// `getOrSynthesizeSample2DDerivatives`'s own `Caller`-stage-gated
/// real-derivatives-or-zero-constants behavior (real derivatives only ever
/// apply in the `Fragment` stage; every other caller gets six zero
/// constants, leaving that sample's own implicit level resolved to mip 0
/// exactly as before this row -- the same pre-L56 behavior, not a
/// regression, for any caller not in a position to synthesize a real
/// derivative).
CubeDirectionDerivatives
getOrSynthesizeSampleCubeDerivatives(llvm::IRBuilderBase &B,
                                    llvm::Function &Caller, llvm::Value *DirX,
                                    llvm::Value *DirY, llvm::Value *DirZ);

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_IMAGECALLS_H
