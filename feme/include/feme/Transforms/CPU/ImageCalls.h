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
  llvm::Value *U = nullptr;
  llvm::Value *V = nullptr;
  /// The LOD/mip operand: `Sample2D`/`SampleCmp2D`'s explicit-or-ignored LOD
  /// float, or `Load2D`/`Load2DI32`'s integer mip level.
  llvm::Value *Lod = nullptr;
  /// `Sample2D`/`SampleCmp2D` only: whether `Lod` is an explicit LOD (true)
  /// or should be ignored in favor of implicit level 0 (false); null for
  /// `Load2D`/`Load2DI32`, which always name their mip explicitly.
  llvm::Value *UseExplicitLod = nullptr;
  /// `SampleCmp2D` only: the depth-comparison reference value.
  llvm::Value *Dref = nullptr;
  /// `Load2D` only (roadmap F8c): the multisample index a `subpassLoad`'s
  /// explicit-sample form threads through; null for `Sample2D`/
  /// `SampleCmp2D`/`Load2DI32`, which never carry one.
  llvm::Value *Sample = nullptr;
  llvm::Value *Mask = nullptr;
};

/// Returns the canonical `feme.cpu.image.*` name for \p Kind, e.g.
/// `feme.cpu.image.sample.2d.v4f32`.
llvm::StringRef getImageCallName(ImageCallKind Kind);

/// Gets (inserting if absent) the `feme.cpu.image.*` declaration for
/// \p Kind in \p M.
llvm::Function *getOrInsertImageCall(llvm::Module &M, ImageCallKind Kind);

/// Builds a `feme.cpu.image.sample.2d.v4f32` call.
llvm::CallInst *createSample2D(llvm::IRBuilderBase &Builder,
                               const ImageCallEnv &Env, llvm::Value *ImageIndex,
                               llvm::Value *SamplerIndex, llvm::Value *U,
                               llvm::Value *V, llvm::Value *Lod,
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

/// Recognizes \p CI as one of the canonical `feme.cpu.image.*` calls,
/// returning its decoded operands, or `std::nullopt` if \p CI's callee isn't
/// one.
std::optional<MatchedImageCall> matchImageCall(const llvm::CallInst &CI);

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_IMAGECALLS_H
