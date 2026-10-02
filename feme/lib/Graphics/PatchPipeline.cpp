//===- PatchPipeline.cpp - Chained hull/tessellator/domain pipeline -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/PatchPipeline.h"

#include "feme/Core/Signature.h"
#include "feme/Graphics/DomainInvocations.h"
#include "feme/Graphics/Patch.h"
#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/CPU/ResourceHeap.h"
#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>

using namespace llvm;

namespace feme::graphics {

namespace {

/// Whether \p Elt is one of the patch-constant phase's `InputPatch`
/// elements -- the original, pre-control-stage control points -- rather
/// than one of its completed-`OutputPatch` elements. Both are
/// `SignatureDirection::Input`; only `FromInputPatch` tells them apart.
bool isInputPatchElement(const SignatureElement &Elt) {
  return Elt.FromInputPatch;
}

bool isOutputPatchElement(const SignatureElement &Elt) {
  return !Elt.FromInputPatch;
}

/// Builds \p Sig's \p Direction storage, filtered to the elements
/// \p Filter accepts. Only the patch-constant phase's own `Input`
/// direction needs this: its two halves (`OutputPatch` and `InputPatch`)
/// are separate ABI blocks with separate layouts.
Expected<StageStorage>
buildFilteredStorage(const EntrySignature &Sig, SignatureDirection Direction,
                     uint32_t InvocationCount,
                     function_ref<bool(const SignatureElement &)> Filter) {
  EntrySignature Filtered;
  Filtered.Elements.reserve(Sig.Elements.size());
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.Direction != Direction || Filter(Elt))
      Filtered.Elements.push_back(Elt);
  return buildStageStorage(Filtered, Direction, InvocationCount);
}

/// Scans \p Sig for its `TessFactorEdge`/`TessFactorInside` patch outputs
/// and reads their values out of \p PatchConstants, producing the
/// `TessFactors` `feme::graphics::tessellate` consumes. An absent factor
/// keeps `TessFactors`'s own default of 1.0.
TessFactors extractTessFactors(const EntrySignature &Sig,
                               const StageStorage &PatchConstants) {
  TessFactors Factors;
  for (const SignatureElement &Elt : Sig.Elements) {
    if (Elt.Direction != SignatureDirection::PatchOutput)
      continue;
    if (Elt.SystemValue != SignatureSystemValue::TessFactorEdge &&
        Elt.SystemValue != SignatureSystemValue::TessFactorInside)
      continue;
    bool IsEdge = Elt.SystemValue == SignatureSystemValue::TessFactorEdge;
    for (uint32_t Row = 0; Row != Elt.RowCount; ++Row)
      for (uint32_t C = 0; C != Elt.ComponentCount; ++C) {
        uint32_t Index = Row * Elt.ComponentCount + C;
        float Value = PatchConstants.readFloat(
            Elt.ElementID, Elt.FirstComponent + C, /*Invocation=*/0, Row);
        if (IsEdge) {
          if (Index < Factors.Edges.size())
            Factors.Edges[Index] = Value;
          continue;
        }
        if (Index < Factors.Inside.size())
          Factors.Inside[Index] = Value;
      }
  }
  return Factors;
}

/// Whether \p Sig declares any element in \p Direction at all.
bool hasDirection(const EntrySignature &Sig, SignatureDirection Direction) {
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.Direction == Direction)
      return true;
  return false;
}

/// Copies the shared descriptor/root-constant environment into whichever
/// of the three stage-specific resource structs \p To is.
template <typename ResourcesT>
void applyResources(ResourcesT &To, const cpu::DispatchResources *From) {
  if (!From)
    return;
  To.ResourceHeap = From->ResourceHeap;
  To.BoundResources = From->BoundResources;
  To.BoundImages = From->BoundImages;
  To.BoundSamplers = From->BoundSamplers;
  To.ImageHeap = From->ImageHeap;
  To.SamplerHeap = From->SamplerHeap;
  To.RootConstants = From->RootConstants;
}

/// Whether \p Elt is eligible to be linked to a producer stage's matching
/// output at all, as opposed to being sourced from the consuming stage's
/// own invocation record by its compiled wrapper.
///
/// (Roadmap H112) `VertexToHull`/`VertexToInputPatch`/`HullToPatchConstant`/
/// `HullToDomain` below used to filter with `isNotSystemValue` above --
/// which excludes *every* system-value input, not just the ones a hull or
/// domain stage's own wrapper actually synthesizes itself
/// (`OutputControlPointID`/`PatchVertices`/`PrimitiveID` for the hull
/// phase, `DomainLocation`/`PatchVertices`/`PrimitiveID` for the domain
/// stage -- see `HullWrapper.cpp`'s `lowerHullInputLoad` and
/// `DomainWrapper.cpp`'s `lowerDomainInputLoad`, whose own `default` case
/// comments already document that every *other* system value (`Position`,
/// `ClipDistance`, `CullDistance`, `PointSize`) is a per-control-point
/// attribute merely *forwarded* from the previous stage's matching output,
/// exactly like an ordinary user varying, and needs the same producer
/// linkage one gets). Since `isNotSystemValue` excluded those too, no
/// `LinkedStageElement` was ever created for them, so `copyLinkedElements`
/// never copied the real value into the consumer's own input storage --
/// silently leaving it zero-initialized instead. This went unnoticed for
/// `Position` only because every existing tessellation test (and, per
/// `CanonicalizeStage.cpp`'s own convention, every real SPIR-V-derived
/// shader up to this point) forwards it through an ordinary `Location`-
/// based varying between the vertex/hull/domain stages, only marking it
/// `SystemValue::Position` at the domain stage's own final output -- so it
/// was never actually filtered out by `isNotSystemValue`. `gl_ClipDistance`/
/// `gl_CullDistance` have no such non-system-value spelling: SPIR-V/GLSL
/// only ever expose them as `gl_ClipDistance`/`gl_CullDistance`, a system
/// value at every stage that touches them, so this bug was reachable only
/// once a real per-control-point `ClipDistance`/`CullDistance` passthrough
/// shape existed to trigger it -- `dEQP-VK.clipping.user_defined.
/// {clip_distance,clip_cull_distance}.vert_tess*`, this milestone's own
/// root cause.
bool isForwardedFromProducerStage(const SignatureElement &Elt) {
  switch (Elt.SystemValue) {
  case SignatureSystemValue::OutputControlPointID:
  case SignatureSystemValue::PatchVertices:
  case SignatureSystemValue::PrimitiveID:
  case SignatureSystemValue::DomainLocation:
  case SignatureSystemValue::ViewIndex:
    // (Roadmap H51/L109) Like `PrimitiveID` above, `gl_ViewIndex` is a
    // pipeline-supplied, per-draw scalar with no real cross-stage
    // producer to link against -- see `FemePatchArgs::ViewIndex`'s own
    // comment.
    return false;
  default:
    return true;
  }
}

} // namespace

Expected<PatchPipelineLinkage>
linkPatchPipeline(const EntrySignature &VertexOutputSig,
                  const PatchPipelineStages &Stages) {
  PatchPipelineLinkage Link;
  Expected<EntrySignature> HullSig = getStageSignature(Stages.Hull);
  if (!HullSig)
    return HullSig.takeError();
  Expected<EntrySignature> PatchConstantSig =
      getStageSignature(Stages.PatchConstant);
  if (!PatchConstantSig)
    return PatchConstantSig.takeError();
  Expected<EntrySignature> DomainSig = getStageSignature(Stages.Domain);
  if (!DomainSig)
    return DomainSig.takeError();
  Link.HullSig = std::move(*HullSig);
  Link.PatchConstantSig = std::move(*PatchConstantSig);
  Link.DomainSig = std::move(*DomainSig);

  // A stage's own wrapper-synthesized system-value inputs (the hull
  // phase's `OutputControlPointID`/`PatchVertices`/`PrimitiveID`, the
  // domain stage's `DomainLocation`/`PatchVertices`/`PrimitiveID`) are
  // sourced from its invocation record by the compiled wrapper, not from
  // the previous stage's output, so those are excluded here; every other
  // per-control-point attribute (ordinary varyings, and forwarded system
  // values like `Position`/`ClipDistance`/`CullDistance`/`PointSize` --
  // see `isForwardedFromProducerStage`'s own comment) is linked normally.
  Expected<SmallVector<LinkedStageElement, 4>> VertexToHull = linkStageElements(
      VertexOutputSig, SignatureDirection::Output, Link.HullSig,
      SignatureDirection::Input, "vertex stage output -> hull stage input",
      isForwardedFromProducerStage);
  if (!VertexToHull)
    return VertexToHull.takeError();
  Link.VertexToHull = std::move(*VertexToHull);

  for (const SignatureElement &Elt : Link.PatchConstantSig.Elements)
    if (Elt.Direction == SignatureDirection::Input && Elt.FromInputPatch)
      Link.HasInputPatch = true;
  if (Link.HasInputPatch) {
    Expected<SmallVector<LinkedStageElement, 4>> VertexToInputPatch =
        linkStageElements(VertexOutputSig, SignatureDirection::Output,
                          Link.PatchConstantSig, SignatureDirection::Input,
                          "vertex stage output -> patch-constant InputPatch",
                          [](const SignatureElement &Elt) {
                            return isInputPatchElement(Elt) &&
                                   isForwardedFromProducerStage(Elt);
                          });
    if (!VertexToInputPatch)
      return VertexToInputPatch.takeError();
    Link.VertexToInputPatch = std::move(*VertexToInputPatch);
  }

  Expected<SmallVector<LinkedStageElement, 4>> HullToPatchConstant =
      linkStageElements(Link.HullSig, SignatureDirection::Output,
                        Link.PatchConstantSig, SignatureDirection::Input,
                        "hull stage output -> patch-constant OutputPatch",
                        [](const SignatureElement &Elt) {
                          return isOutputPatchElement(Elt) &&
                                 isForwardedFromProducerStage(Elt) &&
                                 Elt.Frequency != SignatureFrequency::PerPatch;
                        });
  if (!HullToPatchConstant)
    return HullToPatchConstant.takeError();
  Link.HullToPatchConstant = std::move(*HullToPatchConstant);

  // (Roadmap L344) A `patch`-frequency `OutputPatch` consumer element
  // (e.g. `cross_invocation_per_patch`'s own `in_te_data0`, classified
  // `Input`/`PerPatch` rather than `PatchOutput` here precisely because
  // `!HasStoreInPhase` -- see `classifySPIRVElement`'s own comment) is
  // *not* an ordinary per-control-point value `HullToPatchConstant`
  // above's generic per-invocation `copyLinkedElements` call already
  // handles correctly: the hull control-point phase's own compiled body
  // writes such a `patch`-qualified array one index at a time, from
  // every invocation (`HullWrapper.cpp`'s `lowerHullOutputStore` always
  // addresses a store by *this invocation's own* flat index, regardless
  // of the element's `Frequency`), each invocation writing only its own
  // *diagonal* `(Invocation == Row)` entry of `Result.OutputPatch`'s
  // per-invocation storage, leaving every other `Row` in that same
  // invocation's own copy unwritten. Reading that storage back the same
  // (generic, single-invocation-indexed) way `HullToPatchConstant` does
  // would see only one real element (wherever `Invocation == Row`) and
  // garbage everywhere else -- exactly the systematic, mostly-wrong
  // readback this roadmap entry's own CTS image diff (`--deqp-log-
  // images=enable`) first showed. `runPatchPipeline`'s own
  // `copyLinkedPatchFrequencyElements` call gathers this diagonal into
  // one real, complete array instead, replicated across every
  // destination `OutputPatch` invocation slot (since `PatchConstantWrapper
  // .cpp`'s own `lowerPatchConstantInputLoad` always reads such a
  // `PerPatch`-frequency element at `ControlPoint == 0`, any slot works,
  // as long as every slot agrees).
  Expected<SmallVector<LinkedStageElement, 4>> HullToPatchConstantDiagonal =
      linkStageElements(
          Link.HullSig, SignatureDirection::Output, Link.PatchConstantSig,
          SignatureDirection::Input,
          "hull stage output -> patch-constant OutputPatch (patch-frequency)",
          [](const SignatureElement &Elt) {
            return isOutputPatchElement(Elt) &&
                   isForwardedFromProducerStage(Elt) &&
                   Elt.Frequency == SignatureFrequency::PerPatch;
          });
  if (!HullToPatchConstantDiagonal)
    return HullToPatchConstantDiagonal.takeError();
  Link.HullToPatchConstantDiagonal = std::move(*HullToPatchConstantDiagonal);

  // (Roadmap L339) A domain-stage `Input` consumer element this phase's
  // own `Output`-direction (genuine per-vertex, post-barrier) signature
  // elements cover is linked by `PatchConstantToDomainInput` below
  // instead -- `HullToDomain` must skip it here, since the hull
  // control-point phase's own output never wrote it (that's precisely
  // what makes it a `cross_invocation_per_vertex`-shaped varying rather
  // than an ordinary one) and would otherwise make this an unconditional
  // hard "no matching producer element" link failure.
  auto IsCoveredByPatchConstantVertexOutput =
      [&Link](const SignatureElement &Elt) {
        if (Elt.SystemValue != SignatureSystemValue::None)
          return findElement(Link.PatchConstantSig,
                             SignatureDirection::Output,
                             Elt.SystemValue) != nullptr;
        if (!Elt.Location)
          return false;
        return findElementByLocation(Link.PatchConstantSig,
                                     SignatureDirection::Output,
                                     *Elt.Location, Elt.Index,
                                     Elt.FirstComponent) != nullptr;
      };
  Expected<SmallVector<LinkedStageElement, 4>> HullToDomain = linkStageElements(
      Link.HullSig, SignatureDirection::Output, Link.DomainSig,
      SignatureDirection::Input, "hull stage output -> domain stage input",
      [&](const SignatureElement &Elt) {
        return isForwardedFromProducerStage(Elt) &&
               !IsCoveredByPatchConstantVertexOutput(Elt);
      });
  if (!HullToDomain)
    return HullToDomain.takeError();
  Link.HullToDomain = std::move(*HullToDomain);

  Link.HasDomainPatchConstants =
      hasDirection(Link.DomainSig, SignatureDirection::PatchInput);
  if (Link.HasDomainPatchConstants) {
    // (Roadmap L344) A domain `patch in` consumer this phase's own
    // `PatchOutput` elements cover (`HasPatchConstantOutputProducer`)
    // goes through the ordinary link below; any other one is, instead,
    // a `patch`-qualified varying the hull control-point phase itself
    // wrote, with the patch-constant phase's own body only ever reading
    // it back (`classifySPIRVElement`'s own `isPatchOutputDecoration`
    // comment) -- `HullToPatchConstantDomain` below forwards it straight
    // from the hull stage's own genuine `Output` producer so it is not
    // left as an unconditional hard "no matching producer element"
    // error merely because this phase's own signature has no
    // `PatchOutput`-direction element for it.
    auto HasPatchConstantOutputProducer =
        [&Link](const SignatureElement &Elt) {
          if (Elt.SystemValue != SignatureSystemValue::None)
            return findElement(Link.PatchConstantSig,
                               SignatureDirection::PatchOutput,
                               Elt.SystemValue) != nullptr;
          if (!Elt.Location)
            return false;
          return findElementByLocation(Link.PatchConstantSig,
                                       SignatureDirection::PatchOutput,
                                       *Elt.Location, Elt.Index,
                                       Elt.FirstComponent) != nullptr;
        };
    Expected<SmallVector<LinkedStageElement, 4>> PatchConstantToDomain =
        linkStageElements(Link.PatchConstantSig,
                          SignatureDirection::PatchOutput, Link.DomainSig,
                          SignatureDirection::PatchInput,
                          "patch-constant output -> domain stage patch input",
                          HasPatchConstantOutputProducer);
    if (!PatchConstantToDomain)
      return PatchConstantToDomain.takeError();
    Link.PatchConstantToDomain = std::move(*PatchConstantToDomain);

    Expected<SmallVector<LinkedStageElement, 4>> HullToPatchConstantDomain =
        linkStageElements(
            Link.HullSig, SignatureDirection::Output, Link.DomainSig,
            SignatureDirection::PatchInput,
            "hull stage output -> domain stage patch input (forwarded)",
            [&](const SignatureElement &Elt) {
              return !HasPatchConstantOutputProducer(Elt);
            });
    if (!HullToPatchConstantDomain)
      return HullToPatchConstantDomain.takeError();
    Link.HullToPatchConstantDomain = std::move(*HullToPatchConstantDomain);
  }

  // (Roadmap L339) A genuine per-control-point output the patch-constant
  // phase's own body writes after a barrier (e.g. GLSL's
  // `cross_invocation_per_vertex` shape) -- unlike `PatchOutput` above,
  // this is `SignatureDirection::Output`, structure-of-arrays over
  // `OutputControlPointCount`, and links into the domain stage's
  // *ordinary* per-vertex `Input` (the same direction `HullToDomain`
  // feeds, see `runPatchPipeline`'s own comment on why copying into that
  // same `DomainInput` storage from two different `Src` blocks is safe).
  // Mirrors `HullToDomain`'s own `IsCoveredByPatchConstantVertexOutput`
  // filter above: only a domain-stage `Input` consumer this phase's own
  // `Output`-direction elements actually cover belongs to this link --
  // every other ordinary varying (already linked by `HullToDomain`
  // instead) must be excluded here, or this would unconditionally
  // require *every* domain input to find a patch-constant producer,
  // hard-failing on any one `HullToDomain` already covers.
  Link.HasPatchConstantVertexOutputs =
      hasDirection(Link.PatchConstantSig, SignatureDirection::Output);
  if (Link.HasPatchConstantVertexOutputs) {
    Expected<SmallVector<LinkedStageElement, 4>> PatchConstantToDomainInput =
        linkStageElements(
            Link.PatchConstantSig, SignatureDirection::Output, Link.DomainSig,
            SignatureDirection::Input,
            "patch-constant per-vertex output -> domain stage input",
            [&](const SignatureElement &Elt) {
              return isForwardedFromProducerStage(Elt) &&
                     IsCoveredByPatchConstantVertexOutput(Elt);
            });
    if (!PatchConstantToDomainInput)
      return PatchConstantToDomainInput.takeError();
    Link.PatchConstantToDomainInput = std::move(*PatchConstantToDomainInput);
  }

  return Link;
}

Expected<PatchPipelineResult> runPatchPipeline(
    const PatchPipelineStages &Stages, const PatchPipelineLinkage &Link,
    const TessellationState &Tess, const StageStorage &VertexOutputs,
    ArrayRef<uint32_t> ControlPointInvocations,
    const cpu::DispatchResources *Resources, uint32_t PrimitiveID,
    uint32_t ViewIndex) {
  std::string ValidationErr;
  {
    raw_string_ostream OS(ValidationErr);
    if (!validatePatchControlPointCounts(Tess.InputControlPointCount,
                                         Tess.OutputControlPointCount, &OS))
      return createStringError(inconvertibleErrorCode(), "%s",
                               OS.str().c_str());
  }
  if (ControlPointInvocations.size() != Tess.InputControlPointCount)
    return createStringError(inconvertibleErrorCode(),
                             "a patch was given %zu control-point invocation "
                             "index(es) but declares %u input control points",
                             ControlPointInvocations.size(),
                             Tess.InputControlPointCount);

  PatchPipelineResult Result;

  // 1. Hull control-point phase: gather this patch's input control points
  //    out of the vertex stage's own output block, then produce the
  //    completed `OutputPatch`.
  Expected<StageStorage> HullInput = buildStageStorage(
      Link.HullSig, SignatureDirection::Input, Tess.InputControlPointCount);
  if (!HullInput)
    return HullInput.takeError();
  copyLinkedElements(VertexOutputs, *HullInput, Link.VertexToHull,
                     Tess.InputControlPointCount, ControlPointInvocations);

  Expected<StageStorage> HullOutput = buildStageStorage(
      Link.HullSig, SignatureDirection::Output, Tess.OutputControlPointCount);
  if (!HullOutput)
    return HullOutput.takeError();
  Result.OutputPatch = std::move(*HullOutput);
  {
    cpu::FemeStageLayout InLayout = HullInput->layout();
    cpu::FemeStageLayout OutLayout = Result.OutputPatch.layout();
    cpu::PatchResources Res;
    applyResources(Res, Resources);
    Res.InputLayout = &InLayout;
    Res.Inputs = HullInput->Data.data();
    Res.OutputLayout = &OutLayout;
    Res.Outputs = Result.OutputPatch.Data.data();
    Res.OutputControlPointCount = Tess.OutputControlPointCount;
    Res.InputPatchControlPointCount = Tess.InputControlPointCount;
    Res.PrimitiveID = PrimitiveID;
    Res.ViewIndex = ViewIndex;
    cpu::PreparedPatchBatch Prepared =
        cpu::PreparedPatchBatch::create(Stages.Hull.getResourceInfo(), Res);
    if (Error E = Stages.Hull.invokePatch(Prepared))
      return std::move(E);
  }

  // 2. Patch-constant phase: produces tessellation factors/patch constants.
  Expected<StageStorage> PatchConstantInput =
      buildFilteredStorage(Link.PatchConstantSig, SignatureDirection::Input,
                           Tess.OutputControlPointCount, isOutputPatchElement);
  if (!PatchConstantInput)
    return PatchConstantInput.takeError();
  copyLinkedElements(Result.OutputPatch, *PatchConstantInput,
                     Link.HullToPatchConstant, Tess.OutputControlPointCount);
  copyLinkedPatchFrequencyElements(
      Result.OutputPatch, *PatchConstantInput, Link.HullToPatchConstantDiagonal,
      /*SourceInvocationCount=*/Tess.OutputControlPointCount,
      /*DestInvocationCount=*/Tess.OutputControlPointCount);

  StageStorage InputPatch;
  if (Link.HasInputPatch) {
    Expected<StageStorage> Built =
        buildFilteredStorage(Link.PatchConstantSig, SignatureDirection::Input,
                             Tess.InputControlPointCount, isInputPatchElement);
    if (!Built)
      return Built.takeError();
    InputPatch = std::move(*Built);
    copyLinkedElements(VertexOutputs, InputPatch, Link.VertexToInputPatch,
                       Tess.InputControlPointCount, ControlPointInvocations);
  }

  Expected<StageStorage> PatchConstantOutput =
      buildStageStorage(Link.PatchConstantSig, SignatureDirection::PatchOutput,
                        /*InvocationCount=*/1);
  if (!PatchConstantOutput)
    return PatchConstantOutput.takeError();
  Result.PatchConstants = std::move(*PatchConstantOutput);

  // (Roadmap L339) A genuine per-control-point output is
  // structure-of-arrays over `OutputControlPointCount`, exactly like
  // `Result.OutputPatch` above, unlike `Result.PatchConstants`'s single
  // per-patch slot.
  if (Link.HasPatchConstantVertexOutputs) {
    Expected<StageStorage> PatchConstantVertexOutput = buildStageStorage(
        Link.PatchConstantSig, SignatureDirection::Output,
        Tess.OutputControlPointCount);
    if (!PatchConstantVertexOutput)
      return PatchConstantVertexOutput.takeError();
    Result.PatchConstantVertexOutputs = std::move(*PatchConstantVertexOutput);
  }
  {
    cpu::FemeStageLayout InLayout = PatchConstantInput->layout();
    cpu::FemeStageLayout InPatchLayout = InputPatch.layout();
    cpu::FemeStageLayout OutLayout = Result.PatchConstants.layout();
    cpu::FemeStageLayout PerVertexOutLayout =
        Result.PatchConstantVertexOutputs.layout();
    cpu::PatchConstantResources Res;
    applyResources(Res, Resources);
    Res.InputLayout = &InLayout;
    Res.Inputs = PatchConstantInput->Data.data();
    if (Link.HasInputPatch) {
      Res.InputPatchLayout = &InPatchLayout;
      Res.InputPatch = InputPatch.Data.data();
      Res.InputPatchControlPointCount = Tess.InputControlPointCount;
    }
    Res.OutputLayout = &OutLayout;
    Res.Outputs = Result.PatchConstants.Data.data();
    if (Link.HasPatchConstantVertexOutputs) {
      Res.PerVertexOutputLayout = &PerVertexOutLayout;
      Res.PerVertexOutputs = Result.PatchConstantVertexOutputs.Data.data();
    }
    Res.OutputControlPointCount = Tess.OutputControlPointCount;
    Res.PrimitiveID = PrimitiveID;
    Res.ViewIndex = ViewIndex;
    cpu::PreparedPatchConstantBatch Prepared =
        cpu::PreparedPatchConstantBatch::create(
            Stages.PatchConstant.getResourceInfo(), Res);
    if (Error E = Stages.PatchConstant.invokePatchConstant(Prepared))
      return std::move(E);
  }

  // 3. Fixed-function tessellator.
  TessFactors Factors =
      extractTessFactors(Link.PatchConstantSig, Result.PatchConstants);
  Result.Tessellated =
      tessellate(Tess.Domain, Tess.Partitioning, Tess.OutputPrimitive, Factors,
                 Tess.MaxTessFactor);

  // 4. Domain/evaluation stage, one invocation per generated domain point.
  //    A patch the tessellator culled entirely (a non-positive factor)
  //    still gets an empty, correctly-shaped output block rather than an
  //    error: it simply contributes no vertices.
  uint32_t PointCount = static_cast<uint32_t>(Result.Tessellated.Points.size());
  Expected<StageStorage> DomainOutput = buildStageStorage(
      Link.DomainSig, SignatureDirection::Output, std::max(PointCount, 1u));
  if (!DomainOutput)
    return DomainOutput.takeError();
  Result.DomainOutputs = std::move(*DomainOutput);
  if (PointCount == 0)
    return Result;

  Expected<StageStorage> DomainInput = buildStageStorage(
      Link.DomainSig, SignatureDirection::Input, Tess.OutputControlPointCount);
  if (!DomainInput)
    return DomainInput.takeError();
  copyLinkedElements(Result.OutputPatch, *DomainInput, Link.HullToDomain,
                     Tess.OutputControlPointCount);
  // (Roadmap L339) A second, independent source feeding the same
  // `DomainInput` storage: safe because `Link.HullToDomain` and
  // `Link.PatchConstantToDomainInput` address disjoint `DomainSig`
  // element IDs (one stage's `Output`-direction signature never shares an
  // element ID with another's, per `linkPatchPipeline`'s own element-ID
  // bookkeeping).
  if (Link.HasPatchConstantVertexOutputs)
    copyLinkedElements(Result.PatchConstantVertexOutputs, *DomainInput,
                       Link.PatchConstantToDomainInput,
                       Tess.OutputControlPointCount);

  StageStorage DomainPatchConstants;
  if (Link.HasDomainPatchConstants) {
    Expected<StageStorage> Built =
        buildStageStorage(Link.DomainSig, SignatureDirection::PatchInput,
                          /*InvocationCount=*/1);
    if (!Built)
      return Built.takeError();
    DomainPatchConstants = std::move(*Built);
    copyLinkedElements(Result.PatchConstants, DomainPatchConstants,
                       Link.PatchConstantToDomain, /*InvocationCount=*/1);
    // (Roadmap L344) `Result.OutputPatch`'s own `PerPatch`-frequency
    // elements (e.g. `cross_invocation_per_patch`'s own `in_te_data0`,
    // written per-index by the control-point phase's own per-invocation
    // body but into one shared, patch-wide array -- see
    // `classifySPIRVElement`'s own comment) are addressed the same way
    // regardless of which control-point invocation wrote them, so
    // `InvocationCount=1` here reads the one shared copy correctly, the
    // same as the ordinary `PatchConstantToDomain` copy just above.
    copyLinkedElements(Result.OutputPatch, DomainPatchConstants,
                       Link.HullToPatchConstantDomain, /*InvocationCount=*/1);
  }

  std::vector<cpu::FemeDomainInvocation> Invocations =
      buildDomainInvocations(Result.Tessellated, PrimitiveID, ViewIndex);
  {
    cpu::FemeStageLayout InLayout = DomainInput->layout();
    cpu::FemeStageLayout PatchLayout = DomainPatchConstants.layout();
    cpu::FemeStageLayout OutLayout = Result.DomainOutputs.layout();
    cpu::DomainResources Res;
    applyResources(Res, Resources);
    Res.InputLayout = &InLayout;
    Res.Inputs = DomainInput->Data.data();
    if (Link.HasDomainPatchConstants) {
      Res.PatchConstantLayout = &PatchLayout;
      Res.PatchConstants = DomainPatchConstants.Data.data();
    }
    Res.OutputLayout = &OutLayout;
    Res.Outputs = Result.DomainOutputs.Data.data();
    Res.Invocations = Invocations;
    Res.OutputControlPointCount = Tess.OutputControlPointCount;
    cpu::PreparedDomainBatch Prepared =
        cpu::PreparedDomainBatch::create(Stages.Domain.getResourceInfo(), Res);
    if (Error E = Stages.Domain.invokeDomain(Prepared))
      return std::move(E);
  }

  return Result;
}

} // namespace feme::graphics
