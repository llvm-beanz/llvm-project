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
  /// `<4 x float>`. No filtered-sample counterpart exists: SPIR-V only
  /// legalizes `OpImageFetch` (never `OpImageSample*`) against an integer-
  /// sampled image, so there is nothing for a `SampleImage2DI32` to mean.
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
  llvm::Value *U = nullptr;
  llvm::Value *V = nullptr;
  /// `Sample2D` only (roadmap H7i): the caller's own screen-space partial
  /// derivatives of `U`/`V`, consulted only for an implicit-LOD sample
  /// (see `createSample2D`'s doc); null for every other kind.
  llvm::Value *DUdX = nullptr;
  llvm::Value *DUdY = nullptr;
  llvm::Value *DVdX = nullptr;
  llvm::Value *DVdY = nullptr;
  /// `SampleCube`/`SampleCubeArray` only: the direction vector's Z
  /// component; null for every other kind.
  llvm::Value *W = nullptr;
  /// `Sample2DArray`/`SampleCubeArray` only: the float array-layer
  /// coordinate (rounded to nearest, clamped, at the runtime); null for
  /// every other kind, including the integer-coordinate `Load2DArray`/
  /// `Load2DArrayI32`, which instead use `Layer` below.
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
  /// `SampleCmp2D` only: the depth-comparison reference value.
  llvm::Value *Dref = nullptr;
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
/// ignored either way).
llvm::CallInst *createSample2D(llvm::IRBuilderBase &Builder,
                               const ImageCallEnv &Env, llvm::Value *ImageIndex,
                               llvm::Value *SamplerIndex, llvm::Value *U,
                               llvm::Value *V, llvm::Value *DUdX,
                               llvm::Value *DUdY, llvm::Value *DVdX,
                               llvm::Value *DVdY, llvm::Value *Lod,
                               llvm::Value *UseExplicitLod, llvm::Value *Mask,
                               const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.samplecmp.2d.f32` call.
llvm::CallInst *
createSampleCmp2D(llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
                  llvm::Value *ImageIndex, llvm::Value *SamplerIndex,
                  llvm::Value *U, llvm::Value *V, llvm::Value *Lod,
                  llvm::Value *UseExplicitLod, llvm::Value *Dref,
                  llvm::Value *Mask, const llvm::Twine &Name = "");

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

/// Builds a `feme.cpu.image.sample.2darray.v4f32` call (roadmap H7b-a).
llvm::CallInst *
createSample2DArray(llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
                    llvm::Value *ImageIndex, llvm::Value *SamplerIndex,
                    llvm::Value *U, llvm::Value *V, llvm::Value *ArrayLayer,
                    llvm::Value *Lod, llvm::Value *UseExplicitLod,
                    llvm::Value *Mask, const llvm::Twine &Name = "");

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
/// DirX/\p DirY/\p DirZ are the sample direction vector's components.
llvm::CallInst *createSampleCube(llvm::IRBuilderBase &Builder,
                                 const ImageCallEnv &Env,
                                 llvm::Value *ImageIndex,
                                 llvm::Value *SamplerIndex, llvm::Value *DirX,
                                 llvm::Value *DirY, llvm::Value *DirZ,
                                 llvm::Value *Lod, llvm::Value *UseExplicitLod,
                                 llvm::Value *Mask,
                                 const llvm::Twine &Name = "");

/// Builds a `feme.cpu.image.sample.cubearray.v4f32` call (roadmap H7b-a).
llvm::CallInst *createSampleCubeArray(
    llvm::IRBuilderBase &Builder, const ImageCallEnv &Env,
    llvm::Value *ImageIndex, llvm::Value *SamplerIndex, llvm::Value *DirX,
    llvm::Value *DirY, llvm::Value *DirZ, llvm::Value *ArrayLayer,
    llvm::Value *Lod, llvm::Value *UseExplicitLod, llvm::Value *Mask,
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

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_IMAGECALLS_H
