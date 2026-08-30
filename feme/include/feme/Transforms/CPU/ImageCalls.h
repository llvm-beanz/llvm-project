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
  /// `Load2DArray`/`Load2DArrayI32` only: the integer array-layer texel
  /// coordinate; null for every other kind.
  llvm::Value *Layer = nullptr;
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
  /// `Load2D`/`Load2DArray` only (roadmap F8c): the multisample index a
  /// `subpassLoad`'s explicit-sample form threads through; null for every
  /// sampled kind and for `Load2DI32`/`Load2DArrayI32`, which never carry
  /// one.
  llvm::Value *Sample = nullptr;
  /// `Store2D`/`Store2DI32` only: the `<4 x float>`/`<4 x i32>` texel value
  /// being written; null for every read-only kind.
  llvm::Value *Texel = nullptr;
  llvm::Value *Mask = nullptr;
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

/// Builds a `feme.cpu.image.load.2d.v4i32` call (roadmap E26).
llvm::CallInst *createLoad2DI32(llvm::IRBuilderBase &Builder,
                                const ImageCallEnv &Env,
                                llvm::Value *ImageIndex, llvm::Value *X,
                                llvm::Value *Y, llvm::Value *Mip,
                                llvm::Value *Mask,
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

/// Builds a `feme.cpu.image.load.2darray.v4i32` call (roadmap H7b-a).
llvm::CallInst *createLoad2DArrayI32(llvm::IRBuilderBase &Builder,
                                    const ImageCallEnv &Env,
                                    llvm::Value *ImageIndex, llvm::Value *X,
                                    llvm::Value *Y, llvm::Value *Layer,
                                    llvm::Value *Mip, llvm::Value *Mask,
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
