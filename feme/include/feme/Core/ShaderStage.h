//===- ShaderStage.h - Source-independent shader stage identity -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::ShaderStage, the source-independent pipeline
// stage enumeration described by the "Stage identity" section of
// feme/docs/FeMeGraphicsDesign.md, and the `feme.shader.stage` entry-point
// attribute that records it on a raised llvm::Function.
//
// The enumeration is reflection and pipeline data, not a replacement for
// LLVM target triples: a raised module keeps its
// `dxil-unknown-shadermodelX.Y-<stage>`/`spirv-unknown-vulkan-<stage>`
// triple, and `feme::ShaderStage` is a validated projection of the same
// information onto each entry point. Import derives it from the
// source-format stage and diagnoses any disagreement with the module
// triple's environment; consumers (stage selection, reflection, pipeline
// creation) then work with a checked enumeration rather than a string
// comparison.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_CORE_SHADERSTAGE_H
#define FEME_CORE_SHADERSTAGE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/TargetParser/Triple.h"

#include <cstdint>
#include <optional>

namespace llvm {
class Function;
} // namespace llvm

namespace feme {

/// A shader pipeline stage, independent of the source format an entry point
/// was imported from.
///
/// The enumerator names follow Direct3D where the two APIs disagree on
/// spelling for the pre-raster stages (`Hull`, `Domain`, `Amplification`)
/// and Vulkan where Direct3D's spelling would be ambiguous in a software
/// rasterizer (`Fragment`, not `Pixel`). Each enumerator has exactly one
/// spelling in reflection, diagnostics and serialized artifacts, which is
/// what `getShaderStageName` returns.
enum class ShaderStage : uint8_t {
  Vertex,
  Hull,
  Domain,
  Geometry,
  Fragment,
  Compute,
  Amplification,
  Mesh,
  Library,
  RayGeneration,
  Intersection,
  AnyHit,
  ClosestHit,
  Miss,
  Callable,
  // Keep last: the number of stages, for range checks and array sizing.
  NumStages,
};

/// The name of the function attribute recording an entry point's
/// ShaderStage, spelled with `getShaderStageName`'s canonical name as its
/// value.
llvm::StringRef getShaderStageAttrName();

/// The canonical spelling of \p Stage, used by the `feme.shader.stage`
/// attribute, reflection and diagnostics.
llvm::StringRef getShaderStageName(ShaderStage Stage);

/// Parses \p Name as a stage name, accepting both `getShaderStageName`'s
/// canonical spellings and the target triple environment spellings the
/// existing `hlsl.shader` attribute uses (which differ for exactly one
/// stage: Direct3D's `pixel` names `ShaderStage::Fragment`). Returns
/// `std::nullopt` if \p Name is not a stage.
std::optional<ShaderStage> parseShaderStage(llvm::StringRef Name);

/// The stage a target triple environment names, or `std::nullopt` for an
/// environment that is not a pipeline stage at all (including
/// `rootsignature`, which is a container profile rather than a stage).
std::optional<ShaderStage>
getShaderStageForEnvironment(llvm::Triple::EnvironmentType Env);

/// The target triple environment naming \p Stage. This is the inverse of
/// `getShaderStageForEnvironment`.
llvm::Triple::EnvironmentType getEnvironmentForShaderStage(ShaderStage Stage);

/// Whether an entry point declaring \p Stage is consistent with a module
/// whose triple environment is \p Env.
///
/// A `library` environment carries no single stage of its own -- every entry
/// point in it declares its own -- so it accepts any stage. An environment
/// that names no stage at all (an unknown or non-shader triple) constrains
/// nothing and likewise accepts any stage. Everything else must match
/// exactly.
bool isShaderStageCompatibleWithEnvironment(ShaderStage Stage,
                                            llvm::Triple::EnvironmentType Env);

/// Records \p Stage on \p F as its `feme.shader.stage` attribute, replacing
/// any stage already recorded there.
void setShaderStage(llvm::Function &F, ShaderStage Stage);

/// The stage \p F declares, or `std::nullopt` if it declares none (i.e. \p F
/// is not an entry point) or spells one this build does not know.
///
/// `feme.shader.stage` wins when present; a function carrying only the
/// pre-R16 `hlsl.shader` attribute is still accepted, so that modules raised
/// before the attribute existed -- and hand-written IR in tests -- keep
/// selecting correctly.
std::optional<ShaderStage> getShaderStage(const llvm::Function &F);

} // namespace feme

#endif // FEME_CORE_SHADERSTAGE_H
