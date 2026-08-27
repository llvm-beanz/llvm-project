//===- PatchPipeline.h - Chained hull/tessellator/domain pipeline -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares `feme::graphics::runPatchPipeline`, roadmap R34's
// continuation on its remaining "actually chaining the ... compiled stage
// invocations together per patch" deferred item: given one patch's input
// control points (as a slice of a vertex stage's own output) and the three
// compiled stages a real hull shader's two phases and a domain shader
// compile into (`feme::cpu::CompiledStage::invokePatch`/
// `invokePatchConstant`/`invokeDomain`), it runs the control-point phase,
// the patch-constant phase, the fixed-function tessellator
// (`feme::graphics::tessellate`), and the domain stage, in that order, and
// returns the domain stage's per-domain-point vertex outputs alongside the
// tessellator's connectivity.
//
// Cross-stage layout linking: the vertex stage, hull control-point phase,
// patch-constant phase, and domain stage are four independently compiled
// entry points, each with its own `EntrySignature`/`ElementID` numbering.
// This function's first version required its caller to hand-build one
// shared `feme::cpu::FemeStageLayout` per shared data block, a documented
// stand-in for the missing linker; roadmap H4 replaces that with the real
// thing. `linkPatchPipeline` matches each consumer stage's inputs to its
// producer stage's outputs by `Location`/system value
// (`feme::graphics::linkStageElements`), and `runPatchPipeline` gives each
// stage its own `StageStorage` block, copying between them per the linked
// plan -- the same relationship `executeDraws` already has between a vertex
// stage's outputs and a fragment stage's inputs.
//
// Chaining the geometry stage on top of this function's result remains the
// documented follow-up (see roadmap R34/H5 in feme/docs/Roadmap.md).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_PATCHPIPELINE_H
#define FEME_GRAPHICS_PATCHPIPELINE_H

#include "feme/Core/Signature.h"
#include "feme/Graphics/StageLink.h"
#include "feme/Graphics/StageStorage.h"
#include "feme/Graphics/Tessellator.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"

#include <cstdint>

namespace feme::cpu {
class CompiledStage;
struct DispatchResources;
} // namespace feme::cpu

namespace feme::graphics {

/// The three compiled stages one patch pipeline chains, in the order they
/// run.
struct PatchPipelineStages {
  const cpu::CompiledStage &Hull;
  const cpu::CompiledStage &PatchConstant;
  const cpu::CompiledStage &Domain;
};

/// The fixed-function tessellator state a patch pipeline runs with: what a
/// hull/domain shader pair declares through its `ExecutionMode`s (SPIR-V)
/// or `hlsl.tessellation.*` attributes (DXIL), plus the patch sizes the
/// pipeline's own `patchControlPoints`/`OutputControlPointID` range fix.
struct TessellationState {
  TessellatorDomain Domain = TessellatorDomain::Quad;
  TessPartitioning Partitioning = TessPartitioning::Integer;
  TessOutputPrimitive OutputPrimitive = TessOutputPrimitive::TriangleCcw;
  /// Control points per input patch, i.e. how many vertex-stage outputs one
  /// patch consumes (`VkPipelineTessellationStateCreateInfo::
  /// patchControlPoints`).
  uint32_t InputControlPointCount = 3;
  /// Control points the hull control-point phase produces, i.e. how many
  /// times it is invoked per patch.
  uint32_t OutputControlPointCount = 3;
  /// The device's own cap on a rounded segment count
  /// (`maxTessellationGenerationLevel`).
  uint32_t MaxTessFactor = DefaultMaxTessFactor;
};

/// The linked cross-stage attribute plan for one hull/patch-constant/domain
/// triple, built once per pipeline by `linkPatchPipeline` and reused by
/// every patch of every draw through it.
struct PatchPipelineLinkage {
  /// Each stage's own signature, kept so `runPatchPipeline` can size that
  /// stage's storage without re-parsing the compiled artifact per patch.
  EntrySignature HullSig;
  EntrySignature PatchConstantSig;
  EntrySignature DomainSig;

  /// Vertex-stage output -> hull control-point phase input.
  llvm::SmallVector<LinkedStageElement, 4> VertexToHull;
  /// Vertex-stage output -> the patch-constant phase's `InputPatch`
  /// parameter (`SignatureElement::FromInputPatch`). Empty, and
  /// `HasInputPatch` false, when the phase declares no such parameter.
  llvm::SmallVector<LinkedStageElement, 4> VertexToInputPatch;
  bool HasInputPatch = false;
  /// Hull control-point phase output -> patch-constant phase `OutputPatch`
  /// input.
  llvm::SmallVector<LinkedStageElement, 4> HullToPatchConstant;
  /// Hull control-point phase output -> domain stage control-point input.
  llvm::SmallVector<LinkedStageElement, 4> HullToDomain;
  /// Patch-constant phase `PatchOutput` -> domain stage `PatchInput`.
  /// Empty, and `HasDomainPatchConstants` false, when the domain stage
  /// reads no patch constant or tessellation factor.
  llvm::SmallVector<LinkedStageElement, 4> PatchConstantToDomain;
  bool HasDomainPatchConstants = false;
};

/// Links \p VertexOutputSig's outputs and the three compiled stages'
/// signatures into a reusable `PatchPipelineLinkage`, per this file's
/// comment. Returns an `Error` naming the offending element if any stage's
/// input has no producer counterpart or the two disagree on shape.
llvm::Expected<PatchPipelineLinkage>
linkPatchPipeline(const EntrySignature &VertexOutputSig,
                  const PatchPipelineStages &Stages);

/// The result of chaining one patch through `runPatchPipeline`. Each block
/// is in its *producing* stage's own layout, so a caller reading one back
/// addresses it with that stage's own `ElementID`s.
struct PatchPipelineResult {
  /// The completed `OutputPatch`'s control points, in the hull
  /// control-point phase's own output layout.
  StageStorage OutputPatch;
  /// The patch-constant phase's per-patch output (tessellation factors and
  /// patch constants), in that phase's own output layout.
  StageStorage PatchConstants;
  /// The fixed-function tessellator's generated domain coordinates and
  /// primitive connectivity for this patch.
  TessellatedPatch Tessellated;
  /// The domain stage's per-vertex output, one invocation per
  /// `Tessellated.Points` entry in the same order, in the domain stage's
  /// own output layout.
  StageStorage DomainOutputs;
};

/// Runs one patch through \p Stages' hull control-point phase,
/// patch-constant phase, the fixed-function tessellator, and domain stage,
/// in that order:
///
///  1. `Stages.Hull.invokePatch`, batched over
///     `Tess.OutputControlPointCount` control points, reading the patch's
///     input control points gathered out of \p VertexOutputs per
///     \p ControlPointInvocations (which names, for each of the patch's
///     `Tess.InputControlPointCount` control points, its vertex-stage
///     invocation index).
///  2. `Stages.PatchConstant.invokePatchConstant` against that completed
///     `OutputPatch` (and, if the phase declares an `InputPatch`
///     parameter, the same gathered input control points again).
///  3. Reading `feme::SignatureSystemValue::TessFactorEdge`/
///     `TessFactorInside` out of that patch-constant output and feeding
///     them, with \p Tess, to `feme::graphics::tessellate`.
///  4. `Stages.Domain.invokeDomain`, batched one invocation per generated
///     domain point (`feme::graphics::buildDomainInvocations`).
///
/// \p Resources is the descriptor/root-constant environment all three
/// stages share, or null for a patch pipeline that binds none.
///
/// Returns an `Error` if \p Tess's control-point counts violate
/// `feme::graphics::validatePatchControlPointCounts`,
/// \p ControlPointInvocations does not have exactly
/// `Tess.InputControlPointCount` entries, or any chained `invoke*` call
/// fails.
llvm::Expected<PatchPipelineResult> runPatchPipeline(
    const PatchPipelineStages &Stages, const PatchPipelineLinkage &Link,
    const TessellationState &Tess, const StageStorage &VertexOutputs,
    llvm::ArrayRef<uint32_t> ControlPointInvocations,
    const cpu::DispatchResources *Resources = nullptr);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_PATCHPIPELINE_H
