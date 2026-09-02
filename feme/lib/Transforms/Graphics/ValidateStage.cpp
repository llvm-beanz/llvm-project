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
#include "feme/Transforms/Graphics/StageIOGlobal.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"

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
    // (Roadmap H6g-b-c) A mesh entry's own per-vertex/per-primitive output
    // writes reuse `OutputStore` rather than a dedicated op (roadmap H6b's
    // own comment on `StageOpKind::OutputStore`), now reachable here since
    // `ValidateStagePass::run` below validates `ShaderStage::Mesh` too.
    return Stage == ShaderStage::Vertex || Stage == ShaderStage::Fragment ||
           Stage == ShaderStage::Mesh;
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
    // Not yet reachable: `ValidateStagePass` only runs for
    // Vertex/Fragment/Mesh today (see its `run` below). Recorded now so
    // this switch stays exhaustive once the geometry stage is validated
    // here too.
    return Stage == ShaderStage::Geometry;
  case StageOpKind::TaskPayloadStore:
    // (Roadmap H6i) Likewise not yet reachable: `ValidateStagePass` does
    // not validate the amplification (task) stage yet, mirroring how
    // `StreamEmit`/`StreamCut` above are still unreachable until the
    // geometry stage is validated.
    return Stage == ShaderStage::Amplification;
  case StageOpKind::SetMeshOutputs:
    // (Roadmap H6c-a-a-i) Reachable since roadmap H6g-b-c's own fix wired
    // `ShaderStage::Mesh` into `ValidateStagePass::run` below.
    return Stage == ShaderStage::Mesh;
  case StageOpKind::EmitMeshTasks:
    // (Roadmap H6s) Not yet reachable, mirroring `TaskPayloadStore` above:
    // `ValidateStagePass` does not validate the amplification (task)
    // stage yet.
    return Stage == ShaderStage::Amplification;
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

/// (Roadmap H5b/H6b) Checks that \p CI's \p VertexOperand, if non-constant,
/// is legal for \p Stage. Every stage's own `feme.stage.input.load`/
/// `.output.store` recursion (`loadStageIOValue`/`storeStageIOValue` in
/// CanonicalizeStage.cpp) seeds this operand with an ordinary constant
/// `i32 0` by default; the exceptions are a geometry entry's own
/// dynamically-indexed `gl_in[i]`-shaped per-vertex input, and a mesh
/// entry's own dynamically-indexed `gl_MeshVerticesEXT[i]`/
/// `gl_MeshPrimitivesEXT[i]`-shaped per-vertex/per-primitive output
/// (both via `getDynamicVertexIndexedAccess`), threaded through as a
/// genuine non-constant `Value*` -- legal only there because
/// `FemeGeometryArgs`'s own primitive-major `Inputs` layout (see
/// GeometryWrapper.cpp's file comment: "the vertex-in-primitive operand
/// ... may be any value in `[0, VerticesPerPrimitive)`") and its mesh
/// equivalent (roadmap H6c) are the only stage ABIs actually built to
/// address one at runtime. A non-constant `Vertex` operand anywhere else
/// can only mean a malformed/miscompiled access no valid input should ever
/// produce.
void validateVertex(CallInst &CI, unsigned VertexOperand, ShaderStage Stage,
                    StringRef OpName) {
  if (getStageOpConstantOperand(CI, VertexOperand) ||
      Stage == ShaderStage::Geometry || Stage == ShaderStage::Mesh)
    return;
  Function &F = *CI.getFunction();
  F.getContext().emitError(
      &CI, "feme-graphics-validate-stage: '" + OpName + "' in function '" +
               F.getName() +
               "' has a non-constant vertex operand, illegal outside the "
               "geometry/mesh stages");
}

/// The `GlobalVariable` \p Ptr ultimately addresses by walking back through
/// any chain of `getelementptr`s (instruction or constant-expression), or
/// `nullptr` if it does not trace back to one directly. Unlike
/// `CanonicalizeStagePass`'s own `getStageIOGlobal`, this does not need to
/// resolve a byte offset -- only whether \p Ptr's underlying object is a
/// stage-IO global at all -- so a plain GEP walk is enough.
GlobalVariable *findUnderlyingGlobal(Value *Ptr) {
  while (auto *GEP = dyn_cast<GEPOperator>(Ptr))
    Ptr = GEP->getPointerOperand();
  return dyn_cast<GlobalVariable>(Ptr);
}

/// (Roadmap H6g-b-c) Diagnoses \p I (a load or store) if \p Ptr -- its
/// pointer operand -- still addresses a raw, un-canonicalized SPIR-V
/// stage-IO global variable: the shape `CanonicalizeStagePass`'s own
/// `resolveOffsetWithinElement` leaves behind (returning `std::nullopt`,
/// "leave for `ValidateStagePass` to diagnose") when an access does not
/// yet match a shape it knows how to rewrite into a `feme.stage.*` call --
/// e.g. a mesh entry's arrayed `PerPrimitiveEXT`/`PerVertexEXT` builtin
/// interface-block access (roadmap H6c-a-a-iii). Left unrewritten, such an
/// access previously survived as an ordinary load/store of a
/// `GlobalVariable` with no definition of its own all the way to
/// `feme::cpu`'s JIT, which failed there with a raw, undiagnosable
/// "Symbols not found" link error instead of a clean compile-time
/// rejection.
void validateStageIOGlobalAccess(Instruction &I, Value *Ptr) {
  GlobalVariable *GV = findUnderlyingGlobal(Ptr);
  unsigned AddrSpace;
  if (!isSPIRVStageIOGlobal(GV, AddrSpace))
    return;
  Function &F = *I.getFunction();
  F.getContext().emitError(
      &I, "feme-graphics-validate-stage: function '" + F.getName() +
              "' has an unresolved stage-IO global-variable access to '" +
              GV->getName() +
              "', a shape CanonicalizeStagePass does not yet canonicalize "
              "into a 'feme.stage.*' call");
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
      validateVertex(CI, /*VertexOperand=*/3, Stage, OpName);
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
    validateVertex(CI, /*VertexOperand=*/4, Stage, OpName);
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
  case StageOpKind::TaskPayloadStore:
  case StageOpKind::SetMeshOutputs:
  case StageOpKind::EmitMeshTasks:
    // No element/row/component operands to validate: a task payload
    // write (roadmap H6i) addresses raw memory by byte offset, not a
    // `SignatureElement`, so it has nothing to look up here either; a
    // mesh entry's `SetMeshOutputsEXT` (roadmap H6c-a-a-i) is likewise a
    // workgroup-uniform count pair, not a signature element access, and
    // neither is a task entry's `EmitMeshTasksEXT` (roadmap H6s) --
    // workgroup-uniform group-count triple, no signature element of its
    // own either.
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
        (*Stage != ShaderStage::Vertex && *Stage != ShaderStage::Fragment &&
         *Stage != ShaderStage::Mesh))
      continue;

    EntrySignature Sig = dxil::getEntrySignature(F).value_or(EntrySignature{});
    for (Instruction &I : instructions(F)) {
      if (auto *CI = dyn_cast<CallInst>(&I)) {
        StageOpKind Kind;
        if (isStageOpCall(*CI, &Kind))
          validateCall(*CI, Kind, *Stage, Sig);
        continue;
      }
      // (Roadmap H6g-b-c) Every stage validated here is routed through
      // `CanonicalizeStagePass`'s own SPIR-V path, so any load/store still
      // standing after it ran that addresses a recognized stage-IO global
      // is one it failed to canonicalize, not ordinary unrelated program
      // memory access.
      if (auto *LI = dyn_cast<LoadInst>(&I))
        validateStageIOGlobalAccess(I, LI->getPointerOperand());
      else if (auto *SI = dyn_cast<StoreInst>(&I))
        validateStageIOGlobalAccess(I, SI->getPointerOperand());
    }
  }
  // Diagnostics only; this pass never rewrites IR.
  return PreservedAnalyses::all();
}
