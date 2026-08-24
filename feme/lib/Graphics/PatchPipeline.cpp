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
#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace feme::graphics {

namespace {

/// Reads the scalar at \p ElementID's row \p Row, component \p Component,
/// invocation \p Invocation out of \p Data, per \p Layout's byte-addressing
/// recipe for that element (see `FemeStageElement`'s own comment). \p Data
/// is treated as a tightly-packed `float` array, matching every stage
/// layout this file's caller builds (see PatchPipeline.h's file comment).
float readScalar(const cpu::FemeStageLayout &Layout, ArrayRef<float> Data,
                 uint32_t ElementID, uint32_t Row, uint32_t Component,
                 uint32_t Invocation) {
  const cpu::FemeStageElement &E = Layout.Elements[ElementID];
  uint64_t ByteOffset =
      E.DataOffset + (uint64_t)Row * E.RowStride +
      (uint64_t)(Component - E.FirstComponent) * E.ComponentStride +
      (uint64_t)Invocation * E.InvocationStride;
  assert(ByteOffset % sizeof(float) == 0 &&
         "PatchPipeline requires tightly-packed float stage storage");
  return Data[ByteOffset / sizeof(float)];
}

/// Scans \p Stage's attached signature for its `TessFactorEdge`/
/// `TessFactorInside` output elements and reads their values out of
/// \p PatchConstants (in \p Layout), producing the `TessFactors`
/// `feme::graphics::tessellate` consumes.
Expected<TessFactors> extractTessFactors(const cpu::CompiledStage &Stage,
                                         const cpu::FemeStageLayout &Layout,
                                         ArrayRef<float> PatchConstants) {
  std::vector<uint8_t> Bytes = Stage.getArtifactInfo().Signature;
  if (Bytes.empty())
    return createStringError(inconvertibleErrorCode(),
                             "patch-constant stage has no attached "
                             "signature metadata");
  Expected<EntrySignature> Sig = parseSignature(Bytes);
  if (!Sig)
    return Sig.takeError();

  TessFactors Factors;
  for (const SignatureElement &Elt : Sig->Elements) {
    if (Elt.SystemValue != SignatureSystemValue::TessFactorEdge &&
        Elt.SystemValue != SignatureSystemValue::TessFactorInside)
      continue;
    bool IsEdge = Elt.SystemValue == SignatureSystemValue::TessFactorEdge;
    for (uint32_t Row = 0; Row < Elt.RowCount; ++Row) {
      for (uint32_t Component = Elt.FirstComponent;
           Component < Elt.FirstComponent + Elt.ComponentCount; ++Component) {
        uint32_t Index =
            Row * Elt.ComponentCount + (Component - Elt.FirstComponent);
        float Value = readScalar(Layout, PatchConstants, Elt.ElementID, Row,
                                 Component, /*Invocation=*/0);
        if (IsEdge) {
          if (Index < Factors.Edges.size())
            Factors.Edges[Index] = Value;
          continue;
        }
        if (Index < Factors.Inside.size())
          Factors.Inside[Index] = Value;
      }
    }
  }
  return Factors;
}

} // namespace

Expected<PatchPipelineResult> runPatchPipeline(
    const PatchPipelineStages &Stages, const PatchPipelineLayouts &Layouts,
    const TessellatorDomain Domain, const TessPartitioning Partitioning,
    const TessOutputPrimitive OutputPrimitive,
    ArrayRef<float> InputControlPoints, uint32_t InputControlPointCount,
    uint32_t OutputControlPointCount, uint32_t OutputControlPointScalarCount,
    uint32_t PatchConstantScalarCount, uint32_t DomainOutputScalarsPerVertex,
    uint32_t MaxTessFactor) {
  std::string ValidationErr;
  {
    raw_string_ostream OS(ValidationErr);
    if (!validatePatchControlPointCounts(InputControlPointCount,
                                         OutputControlPointCount, &OS))
      return createStringError(inconvertibleErrorCode(), "%s",
                               OS.str().c_str());
  }

  PatchPipelineResult Result;

  // 1. Hull control-point phase: produces the completed `OutputPatch`.
  Result.OutputControlPoints.assign(
      (size_t)OutputControlPointCount * OutputControlPointScalarCount, 0.0f);
  {
    cpu::PatchResources Resources;
    Resources.InputLayout = &Layouts.HullInput;
    Resources.Inputs = InputControlPoints.data();
    Resources.OutputLayout = &Layouts.OutputPatch;
    Resources.Outputs = Result.OutputControlPoints.data();
    Resources.OutputControlPointCount = OutputControlPointCount;
    cpu::PreparedPatchBatch Prepared = cpu::PreparedPatchBatch::create(
        Stages.Hull.getResourceInfo(), Resources);
    if (Error E = Stages.Hull.invokePatch(Prepared))
      return std::move(E);
  }

  // 2. Patch-constant phase: produces tessellation factors/patch constants.
  Result.PatchConstants.assign(PatchConstantScalarCount, 0.0f);
  {
    cpu::PatchConstantResources Resources;
    Resources.InputLayout = &Layouts.OutputPatch;
    Resources.Inputs = Result.OutputControlPoints.data();
    if (Layouts.PatchConstantInputPatch) {
      Resources.InputPatchLayout = Layouts.PatchConstantInputPatch;
      Resources.InputPatch = InputControlPoints.data();
      Resources.InputPatchControlPointCount = InputControlPointCount;
    }
    Resources.OutputLayout = &Layouts.PatchConstants;
    Resources.Outputs = Result.PatchConstants.data();
    Resources.OutputControlPointCount = OutputControlPointCount;
    cpu::PreparedPatchConstantBatch Prepared =
        cpu::PreparedPatchConstantBatch::create(
            Stages.PatchConstant.getResourceInfo(), Resources);
    if (Error E = Stages.PatchConstant.invokePatchConstant(Prepared))
      return std::move(E);
  }

  // 3. Fixed-function tessellator.
  Expected<TessFactors> Factors = extractTessFactors(
      Stages.PatchConstant, Layouts.PatchConstants, Result.PatchConstants);
  if (!Factors)
    return Factors.takeError();
  Result.Tessellated = tessellate(Domain, Partitioning, OutputPrimitive,
                                  *Factors, MaxTessFactor);

  // 4. Domain/evaluation stage, one invocation per generated domain point.
  std::vector<cpu::FemeDomainInvocation> Invocations =
      buildDomainInvocations(Result.Tessellated);
  Result.DomainOutputs.assign((size_t)Result.Tessellated.Points.size() *
                                  DomainOutputScalarsPerVertex,
                              0.0f);
  if (!Result.Tessellated.Points.empty()) {
    cpu::DomainResources Resources;
    Resources.InputLayout = &Layouts.OutputPatch;
    Resources.Inputs = Result.OutputControlPoints.data();
    Resources.PatchConstantLayout = &Layouts.PatchConstants;
    Resources.PatchConstants = Result.PatchConstants.data();
    Resources.OutputLayout = &Layouts.DomainOutput;
    Resources.Outputs = Result.DomainOutputs.data();
    Resources.Invocations = Invocations;
    Resources.OutputControlPointCount = OutputControlPointCount;
    cpu::PreparedDomainBatch Prepared = cpu::PreparedDomainBatch::create(
        Stages.Domain.getResourceInfo(), Resources);
    if (Error E = Stages.Domain.invokeDomain(Prepared))
      return std::move(E);
  }

  return Result;
}

} // namespace feme::graphics
