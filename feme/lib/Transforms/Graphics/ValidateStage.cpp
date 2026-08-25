//===- ValidateStage.cpp - Validate canonical stage operations -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/Graphics/ValidateStage.h"

#include "feme/Core/ShaderStage.h"
#include "feme/Core/Signature.h"
#include "feme/Core/StageOps.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme;
using namespace feme::graphics;

namespace {

/// Whether \p Kind is legal for \p Stage, per "Canonical stage operations"
/// in feme/docs/FeMeGraphicsDesign.md: "The initial vertex path needs input
/// loads and output stores. The initial fragment path adds input loads,
/// output stores, discard/demote, derivatives, and quad reads" (pull-model
/// interpolation is fragment-only for the same reason: it reads a fragment
/// input's interpolation planes).
bool isStageOpLegalForStage(StageOpKind Kind, ShaderStage Stage) {
  switch (Kind) {
  case StageOpKind::InputLoad:
  case StageOpKind::OutputStore:
    return Stage == ShaderStage::Vertex || Stage == ShaderStage::Fragment;
  case StageOpKind::Discard:
  case StageOpKind::Demote:
  case StageOpKind::IsHelper:
  case StageOpKind::DerivativeXFine:
  case StageOpKind::DerivativeYFine:
  case StageOpKind::DerivativeXCoarse:
  case StageOpKind::DerivativeYCoarse:
  case StageOpKind::QuadRead:
  case StageOpKind::InterpolateAtCentroid:
  case StageOpKind::InterpolateAtSample:
  case StageOpKind::InterpolateAtOffset:
    return Stage == ShaderStage::Fragment;
  case StageOpKind::SubpassLoad:
    // Roadmap F8a: `subpassInput` local reads only exist in a fragment
    // shader, per the Vulkan/SPIR-V spec's own restriction.
    return Stage == ShaderStage::Fragment;
  case StageOpKind::StreamEmit:
  case StageOpKind::StreamCut:
    // Not yet reachable: `ValidateStagePass` only runs for Vertex/Fragment
    // today (see its `run` below). Recorded now so this switch stays
    // exhaustive once the geometry stage is validated here too.
    return Stage == ShaderStage::Geometry;
  case StageOpKind::NumStageOpKinds:
    break;
  }
  llvm_unreachable("unhandled StageOpKind");
}

/// The signature element \p CI's \p ElementIDOperand refers to, checking
/// both that the operand is a constant and that it names an element of
/// \p Sig with direction \p ExpectedDir, or `nullptr` (having already
/// diagnosed through \p F's context) if either check fails.
const SignatureElement *validateElement(CallInst &CI, unsigned ElementOperand,
                                        const EntrySignature &Sig,
                                        SignatureDirection ExpectedDir,
                                        StringRef OpName) {
  Function &F = *CI.getFunction();
  std::optional<uint64_t> ElementID =
      getStageOpConstantOperand(CI, ElementOperand);
  if (!ElementID) {
    F.getContext().emitError(&CI, "feme-graphics-validate-stage: '" + OpName +
                                      "' in function '" + F.getName() +
                                      "' has a non-constant element ID");
    return nullptr;
  }
  const auto It = llvm::find_if(Sig.Elements, [&](const SignatureElement &E) {
    return E.ElementID == *ElementID;
  });
  if (It == Sig.Elements.end()) {
    F.getContext().emitError(&CI, "feme-graphics-validate-stage: '" + OpName +
                                      "' in function '" + F.getName() +
                                      "' refers to unknown element " +
                                      Twine(*ElementID));
    return nullptr;
  }
  if (It->Direction != ExpectedDir) {
    F.getContext().emitError(
        &CI, "feme-graphics-validate-stage: '" + OpName + "' in function '" +
                 F.getName() + "' refers to element " + Twine(*ElementID) +
                 " with the wrong direction");
    return nullptr;
  }
  return &*It;
}

/// Checks that \p CI's \p ComponentOperand, if constant, lies within
/// \p Elt's declared `[FirstComponent, FirstComponent + ComponentCount)`
/// range. A non-constant component is not flagged (see the file comment:
/// only constant operands are checked), since some pull-model uses may
/// select a component dynamically.
void validateComponent(CallInst &CI, unsigned ComponentOperand,
                       const SignatureElement &Elt, StringRef OpName) {
  std::optional<uint64_t> Component =
      getStageOpConstantOperand(CI, ComponentOperand);
  if (!Component)
    return;
  if (*Component < Elt.FirstComponent ||
      *Component >= Elt.FirstComponent + Elt.ComponentCount) {
    Function &F = *CI.getFunction();
    F.getContext().emitError(
        &CI, "feme-graphics-validate-stage: '" + OpName + "' in function '" +
                 F.getName() + "' component " + Twine(*Component) +
                 " is out of range for element " + Twine(Elt.ElementID));
  }
}

/// Checks that \p CI's \p RowOperand, if constant, is within \p Elt's
/// declared `RowCount`.
void validateRow(CallInst &CI, unsigned RowOperand, const SignatureElement &Elt,
                 StringRef OpName) {
  std::optional<uint64_t> Row = getStageOpConstantOperand(CI, RowOperand);
  if (!Row)
    return;
  if (*Row >= Elt.RowCount) {
    Function &F = *CI.getFunction();
    F.getContext().emitError(
        &CI, "feme-graphics-validate-stage: '" + OpName + "' in function '" +
                 F.getName() + "' row " + Twine(*Row) +
                 " is out of range for element " + Twine(Elt.ElementID));
  }
}

void validateCall(CallInst &CI, StageOpKind Kind, ShaderStage Stage,
                  const EntrySignature &Sig) {
  StringRef OpName = getStageOpName(Kind);
  if (!isStageOpLegalForStage(Kind, Stage)) {
    Function &F = *CI.getFunction();
    F.getContext().emitError(&CI, "feme-graphics-validate-stage: '" + OpName +
                                      "' is not legal in function '" +
                                      F.getName() + "' (stage '" +
                                      getShaderStageName(Stage) + "')");
    return;
  }

  switch (Kind) {
  case StageOpKind::InputLoad:
  case StageOpKind::InterpolateAtCentroid:
  case StageOpKind::InterpolateAtSample:
  case StageOpKind::InterpolateAtOffset: {
    const SignatureElement *Elt = validateElement(
        CI, /*ElementOperand=*/0, Sig, SignatureDirection::Input, OpName);
    if (!Elt)
      return;
    if (Kind == StageOpKind::InputLoad) {
      validateRow(CI, /*RowOperand=*/1, *Elt, OpName);
      validateComponent(CI, /*ComponentOperand=*/2, *Elt, OpName);
    } else {
      validateComponent(CI, /*ComponentOperand=*/1, *Elt, OpName);
    }
    break;
  }
  case StageOpKind::OutputStore: {
    const SignatureElement *Elt = validateElement(
        CI, /*ElementOperand=*/0, Sig, SignatureDirection::Output, OpName);
    if (!Elt)
      return;
    validateRow(CI, /*RowOperand=*/1, *Elt, OpName);
    validateComponent(CI, /*ComponentOperand=*/2, *Elt, OpName);
    break;
  }
  case StageOpKind::Discard:
  case StageOpKind::Demote:
  case StageOpKind::IsHelper:
  case StageOpKind::DerivativeXFine:
  case StageOpKind::DerivativeYFine:
  case StageOpKind::DerivativeXCoarse:
  case StageOpKind::DerivativeYCoarse:
  case StageOpKind::QuadRead:
  case StageOpKind::StreamEmit:
  case StageOpKind::StreamCut:
  case StageOpKind::SubpassLoad:
    // No element/row/component operands to validate.
    break;
  case StageOpKind::NumStageOpKinds:
    llvm_unreachable("not a real StageOpKind");
  }
}

} // namespace

PreservedAnalyses ValidateStagePass::run(Module &M, ModuleAnalysisManager &AM) {
  for (Function &F : M) {
    std::optional<ShaderStage> Stage = getShaderStage(F);
    if (!Stage ||
        (*Stage != ShaderStage::Vertex && *Stage != ShaderStage::Fragment))
      continue;

    EntrySignature Sig = dxil::getEntrySignature(F).value_or(EntrySignature{});
    for (Instruction &I : instructions(F)) {
      auto *CI = dyn_cast<CallInst>(&I);
      StageOpKind Kind;
      if (!CI || !isStageOpCall(*CI, &Kind))
        continue;
      validateCall(*CI, Kind, *Stage, Sig);
    }
  }
  // Diagnostics only; this pass never rewrites IR.
  return PreservedAnalyses::all();
}
