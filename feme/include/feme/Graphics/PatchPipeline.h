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
// control points and the three compiled stages a real hull shader's two
// phases and a domain shader compile into
// (`feme::cpu::CompiledStage::invokePatch`/`invokePatchConstant`/
// `invokeDomain`), it runs the control-point phase, the patch-constant
// phase, the fixed-function tessellator (`feme::graphics::tessellate`), and
// the domain stage, in that order, and returns the domain stage's
// per-domain-point vertex outputs alongside the tessellator's connectivity.
//
// This closes the tessellation half of that deferred item (hull, patch
// constant, domain); chaining the geometry stage on top of this function's
// result, and wiring either into `feme::graphics::Executor`/`feme-render`,
// remain the documented follow-up (see this file's own unit test and
// roadmap R34's row in feme/docs/Roadmap.md for the up-to-date status).
//
// Cross-stage layout linking: the hull control-point phase, patch-constant
// phase, and domain stage are three independently compiled entry points,
// each with its own `EntrySignature`/`ElementID` numbering. A real pipeline
// would link "the completed patch's control points" and "the patch
// constants" between them by `SignatureElement::Location` the way
// `feme::graphics::Executor` already links a vertex stage's outputs to a
// fragment stage's inputs -- that linking step is not yet generalized to
// these three stages (documented follow-up). This function instead requires
// its caller to build one shared `feme::cpu::FemeStageLayout` for "the
// completed patch's control points" (used as the hull's own output layout,
// the patch-constant phase's own input layout, and the domain stage's own
// input layout) and another for "the patch constants" (the patch-constant
// phase's own output layout and the domain stage's own patch-constant input
// layout), so the three stages agree on where that shared data lives
// without this function performing any linking of its own.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_PATCHPIPELINE_H
#define FEME_GRAPHICS_PATCHPIPELINE_H

#include "feme/Graphics/Tessellator.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <vector>

namespace feme::cpu {
class CompiledStage;
struct FemeStageLayout;
} // namespace feme::cpu

namespace feme::graphics {

/// The three compiled stages one patch pipeline chains, in the order they
/// run.
struct PatchPipelineStages {
  const cpu::CompiledStage &Hull;
  const cpu::CompiledStage &PatchConstant;
  const cpu::CompiledStage &Domain;
};

/// The stage-storage layouts each chained invocation reads/writes, built by
/// the caller the same way `feme::graphics::Executor` builds them for the
/// vertex/fragment stages. See the file comment above for why `OutputPatch`
/// and `PatchConstants` are each a single layout shared by two stages
/// rather than one per stage.
struct PatchPipelineLayouts {
  /// The hull control-point phase's own input: the original, pre-control-
  /// stage input control points (plus any system-value elements it reads,
  /// e.g. `StageLayoutSystemValue::OutputControlPointID`).
  const cpu::FemeStageLayout &HullInput;
  /// The completed patch's control points: the hull phase's own output
  /// layout, the patch-constant phase's own input layout, and the domain
  /// stage's own input layout, all at once.
  const cpu::FemeStageLayout &OutputPatch;
  /// The patch-constant phase's own `InputPatch` parameter layout, or null
  /// if it declares none.
  const cpu::FemeStageLayout *PatchConstantInputPatch = nullptr;
  /// The per-patch tessellation factors/patch constants: the patch-constant
  /// phase's own output layout and the domain stage's own patch-constant
  /// input layout, both at once.
  const cpu::FemeStageLayout &PatchConstants;
  /// The domain stage's own output: one vertex's worth of attributes per
  /// generated domain point.
  const cpu::FemeStageLayout &DomainOutput;
};

/// The result of chaining one patch through `runPatchPipeline`.
struct PatchPipelineResult {
  /// The completed `OutputPatch`'s control points, in `OutputPatch`'s own
  /// layout: `OutputControlPointCount * OutputControlPointScalarCount`
  /// scalars.
  std::vector<float> OutputControlPoints;
  /// The patch-constant phase's output, in `PatchConstants`'s own layout:
  /// `PatchConstantScalarCount` scalars.
  std::vector<float> PatchConstants;
  /// The fixed-function tessellator's generated domain coordinates and
  /// primitive connectivity for this patch.
  TessellatedPatch Tessellated;
  /// The domain stage's output: one `DomainOutputScalarsPerVertex`-scalar
  /// row per `Tessellated.Points` entry, in the same order, in
  /// `DomainOutput`'s own layout.
  std::vector<float> DomainOutputs;
};

/// Runs \p InputControlPoints (\p InputControlPointCount control points,
/// `InputControlPoints.size() / InputControlPointCount` scalars each)
/// through \p Stages' hull control-point phase, patch-constant phase, the
/// fixed-function tessellator, and domain stage, in that order:
///
///  1. `Stages.Hull.invokePatch`, batched over \p OutputControlPointCount
///     control points, producing the completed `OutputPatch`.
///  2. `Stages.PatchConstant.invokePatchConstant` against that `OutputPatch`
///     (and \p InputControlPoints again, if \p Layouts declares an
///     `InputPatch` block), producing the per-patch tessellation
///     factors/constants.
///  3. Reading `feme::SignatureSystemValue::TessFactorEdge`/
///     `TessFactorInside` out of that patch-constant output (by scanning
///     `Stages.PatchConstant`'s own attached signature -- see
///     `feme::cpu::CompiledStage::getArtifactInfo`) and feeding them, with
///     \p TessState, to `feme::graphics::tessellate`.
///  4. `Stages.Domain.invokeDomain`, batched one invocation per generated
///     domain point (`feme::graphics::buildDomainInvocations`), evaluating
///     the completed `OutputPatch` and per-patch constants at each.
///
/// Returns an `Error` if \p InputControlPointCount/
/// \p OutputControlPointCount violate
/// `feme::graphics::validatePatchControlPointCounts`, or if any chained
/// `invoke*` call fails.
llvm::Expected<PatchPipelineResult> runPatchPipeline(
    const PatchPipelineStages &Stages, const PatchPipelineLayouts &Layouts,
    const TessellatorDomain Domain, const TessPartitioning Partitioning,
    const TessOutputPrimitive OutputPrimitive,
    llvm::ArrayRef<float> InputControlPoints, uint32_t InputControlPointCount,
    uint32_t OutputControlPointCount, uint32_t OutputControlPointScalarCount,
    uint32_t PatchConstantScalarCount, uint32_t DomainOutputScalarsPerVertex,
    uint32_t MaxTessFactor = DefaultMaxTessFactor);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_PATCHPIPELINE_H
