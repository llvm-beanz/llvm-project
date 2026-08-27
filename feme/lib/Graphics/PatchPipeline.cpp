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

bool isNotSystemValue(const SignatureElement &Elt) {
  return Elt.SystemValue == SignatureSystemValue::None;
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

  // A stage's system-value inputs (the hull phase's `OutputControlPointID`,
  // the domain stage's `DomainLocation`) are sourced from its invocation
  // record by the compiled wrapper, not from the previous stage's output,
  // so only ordinary per-control-point attributes are linked here.
  Expected<SmallVector<LinkedStageElement, 4>> VertexToHull = linkStageElements(
      VertexOutputSig, SignatureDirection::Output, Link.HullSig,
      SignatureDirection::Input, "vertex stage output -> hull stage input",
      isNotSystemValue);
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
                                   isNotSystemValue(Elt);
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
                                 isNotSystemValue(Elt);
                        });
  if (!HullToPatchConstant)
    return HullToPatchConstant.takeError();
  Link.HullToPatchConstant = std::move(*HullToPatchConstant);

  Expected<SmallVector<LinkedStageElement, 4>> HullToDomain = linkStageElements(
      Link.HullSig, SignatureDirection::Output, Link.DomainSig,
      SignatureDirection::Input, "hull stage output -> domain stage input",
      isNotSystemValue);
  if (!HullToDomain)
    return HullToDomain.takeError();
  Link.HullToDomain = std::move(*HullToDomain);

  Link.HasDomainPatchConstants =
      hasDirection(Link.DomainSig, SignatureDirection::PatchInput);
  if (Link.HasDomainPatchConstants) {
    Expected<SmallVector<LinkedStageElement, 4>> PatchConstantToDomain =
        linkStageElements(Link.PatchConstantSig,
                          SignatureDirection::PatchOutput, Link.DomainSig,
                          SignatureDirection::PatchInput,
                          "patch-constant output -> domain stage patch input");
    if (!PatchConstantToDomain)
      return PatchConstantToDomain.takeError();
    Link.PatchConstantToDomain = std::move(*PatchConstantToDomain);
  }

  return Link;
}

Expected<PatchPipelineResult> runPatchPipeline(
    const PatchPipelineStages &Stages, const PatchPipelineLinkage &Link,
    const TessellationState &Tess, const StageStorage &VertexOutputs,
    ArrayRef<uint32_t> ControlPointInvocations,
    const cpu::DispatchResources *Resources) {
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
  {
    cpu::FemeStageLayout InLayout = PatchConstantInput->layout();
    cpu::FemeStageLayout InPatchLayout = InputPatch.layout();
    cpu::FemeStageLayout OutLayout = Result.PatchConstants.layout();
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
    Res.OutputControlPointCount = Tess.OutputControlPointCount;
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
  }

  std::vector<cpu::FemeDomainInvocation> Invocations =
      buildDomainInvocations(Result.Tessellated);
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
