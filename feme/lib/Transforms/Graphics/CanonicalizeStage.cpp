//===- CanonicalizeStage.cpp - Canonicalize vertex/fragment stage IR ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/Graphics/CanonicalizeStage.h"

#include "feme/Core/ShaderStage.h"
#include "feme/Core/Signature.h"
#include "feme/Core/StageOps.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme;
using namespace feme::graphics;

namespace {

/// `feme::dxil::convertEntrySignature` numbers each of DXIL's input, output
/// and patch-constant lists from 0 upward, in the same order the source
/// list's rows appear (see SignatureImport.cpp's `convertSignature`), so
/// the Nth `Sig.Elements` entry (0-based) with direction \p Dir is exactly
/// what DXIL's own per-list signature ID N names. This reconstructs that
/// per-direction-index -> combined `ElementID` table so a `loadInput`/
/// `storeOutput` call's signature-ID operand (DXIL's own per-list ID, not
/// feme's combined one) can be resolved back through it.
SmallVector<uint32_t> collectElementIDsByDirection(const EntrySignature &Sig,
                                                   SignatureDirection Dir) {
  SmallVector<uint32_t> IDs;
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.Direction == Dir)
      IDs.push_back(Elt.ElementID);
  return IDs;
}

std::optional<uint32_t> resolveElementID(ArrayRef<uint32_t> IDsByDirection,
                                         uint64_t DXILID) {
  if (DXILID >= IDsByDirection.size())
    return std::nullopt;
  return IDsByDirection[DXILID];
}

std::optional<uint64_t> getConstInt(const Value *V) {
  if (const auto *CI = dyn_cast<ConstantInt>(V))
    return CI->getZExtValue();
  return std::nullopt;
}

/// Calls \p Raise on every `CallInst` that calls a `dx.op.*` function whose
/// opcode operand (its first argument) is \p Opcode, snapshotting each
/// function's user list first so \p Raise may erase/replace calls freely.
bool forEachDXOpCall(Function &F, unsigned Opcode,
                     function_ref<bool(CallInst &)> Raise) {
  bool Changed = false;
  Module &M = *F.getParent();
  for (Function &Callee : llvm::make_early_inc_range(M.functions())) {
    if (!Callee.isDeclaration() || !Callee.getName().starts_with("dx.op."))
      continue;
    for (User *U : llvm::make_early_inc_range(Callee.users())) {
      auto *CI = dyn_cast<CallInst>(U);
      if (!CI || CI->getFunction() != &F || CI->getCalledFunction() != &Callee)
        continue;
      if (CI->arg_size() == 0 || getConstInt(CI->getArgOperand(0)) != Opcode)
        continue;
      Changed |= Raise(*CI);
    }
  }
  return Changed;
}

/// Calls \p Raise on every `CallInst` in \p F calling the LLVM intrinsic
/// \p ID (already raised out of DXIL's `dx.op.*`/SPIR-V's `llvm.spv.*`
/// calling convention, by `feme::dxil::OpRaisingPass` or MLIR's SPIR-V ->
/// LLVM conversion respectively).
bool forEachIntrinsicCall(Function &F, Intrinsic::ID ID,
                          function_ref<bool(CallInst &)> Raise) {
  bool Changed = false;
  Module &M = *F.getParent();
  for (Function &Callee : llvm::make_early_inc_range(M.functions())) {
    if (Callee.getIntrinsicID() != ID)
      continue;
    for (User *U : llvm::make_early_inc_range(Callee.users())) {
      auto *CI = dyn_cast<CallInst>(U);
      if (!CI || CI->getFunction() != &F)
        continue;
      Changed |= Raise(*CI);
    }
  }
  return Changed;
}

/// Casts \p V to `i32` if it is not already, for the `i8` column operand
/// `dx.op.loadInput`/`storeOutput`/the pull-model interpolation family use,
/// so every `feme.stage.*` builder can uniformly take `i32` row/component
/// operands (see StageOps.h).
Value *toI32(IRBuilderBase &B, Value *V) {
  if (V->getType()->isIntegerTy(32))
    return V;
  return B.CreateZExt(V, B.getInt32Ty());
}

/// Rewrites \p F's `dx.op.loadInput`/`storeOutput` calls (opcodes 4 and 5;
/// unraised by `feme::dxil::OpRaisingPass`, since they need signature
/// context that pass does not have) into `feme.stage.input.load`/
/// `output.store`, and its already-raised `llvm.dx.discard`/derivative/
/// quad-read intrinsic calls into their `feme.stage.*` peers. Also raises
/// `IsHelperLane` (221) and the pull-model interpolation family
/// (`EvalCentroid`/`EvalSampleIndex`/`EvalSnapped`, opcodes 89/88/87)
/// directly, since neither has an LLVM intrinsic form to raise through
/// first.
bool canonicalizeDXILStage(Function &F, const EntrySignature &Sig) {
  bool Changed = false;
  SmallVector<uint32_t> InputIDs =
      collectElementIDsByDirection(Sig, SignatureDirection::Input);
  SmallVector<uint32_t> OutputIDs =
      collectElementIDsByDirection(Sig, SignatureDirection::Output);

  Changed |= forEachDXOpCall(F, 4, [&](CallInst &CI) { // LoadInput
    if (CI.arg_size() != 5)
      return false;
    std::optional<uint64_t> DXILID = getConstInt(CI.getArgOperand(1));
    if (!DXILID)
      return false;
    std::optional<uint32_t> ElementID = resolveElementID(InputIDs, *DXILID);
    if (!ElementID)
      return false;
    IRBuilder<> B(&CI);
    Value *Row = toI32(B, CI.getArgOperand(2));
    Value *Col = toI32(B, CI.getArgOperand(3));
    Value *Vertex = toI32(B, CI.getArgOperand(4));
    CallInst *New = createStageInputLoad(B, CI.getType(), *ElementID, Row, Col,
                                         Vertex, CI.getName());
    CI.replaceAllUsesWith(New);
    CI.eraseFromParent();
    return true;
  });

  Changed |= forEachDXOpCall(F, 5, [&](CallInst &CI) { // StoreOutput
    if (CI.arg_size() != 5)
      return false;
    std::optional<uint64_t> DXILID = getConstInt(CI.getArgOperand(1));
    if (!DXILID)
      return false;
    std::optional<uint32_t> ElementID = resolveElementID(OutputIDs, *DXILID);
    if (!ElementID)
      return false;
    IRBuilder<> B(&CI);
    Value *Row = toI32(B, CI.getArgOperand(2));
    Value *Col = toI32(B, CI.getArgOperand(3));
    Value *Val = CI.getArgOperand(4);
    Value *Vertex = B.getInt32(0);
    createStageOutputStore(B, *ElementID, Row, Col, Val, Vertex);
    CI.eraseFromParent();
    return true;
  });

  Changed |= forEachDXOpCall(F, 221, [](CallInst &CI) { // IsHelperLane
    IRBuilder<> B(&CI);
    CallInst *New = createStageIsHelper(B);
    CI.replaceAllUsesWith(New);
    CI.eraseFromParent();
    return true;
  });

  auto raiseEval = [&](CallInst &CI, StageOpKind Kind, unsigned ExpectedArgs) {
    if (CI.arg_size() != ExpectedArgs)
      return false;
    std::optional<uint64_t> DXILID = getConstInt(CI.getArgOperand(1));
    if (!DXILID)
      return false;
    std::optional<uint32_t> ElementID = resolveElementID(InputIDs, *DXILID);
    if (!ElementID)
      return false;
    IRBuilder<> B(&CI);
    // Operand 2 (row) always selects the same row an ordinary input load
    // of this element would; the pull model still evaluates one signature
    // element, just at a different location than its declared
    // interpolation, so it does not appear as a separate `feme.stage.*`
    // operand (see StageOpKind::InterpolateAt*'s comment).
    Value *Col = toI32(B, CI.getArgOperand(3));
    CallInst *New = nullptr;
    switch (Kind) {
    case StageOpKind::InterpolateAtCentroid:
      New = createStageInterpolateAtCentroid(B, CI.getType(), *ElementID, Col);
      break;
    case StageOpKind::InterpolateAtSample:
      New = createStageInterpolateAtSample(B, CI.getType(), *ElementID, Col,
                                           toI32(B, CI.getArgOperand(4)));
      break;
    case StageOpKind::InterpolateAtOffset:
      New = createStageInterpolateAtOffset(B, CI.getType(), *ElementID, Col,
                                           toI32(B, CI.getArgOperand(4)),
                                           toI32(B, CI.getArgOperand(5)));
      break;
    default:
      llvm_unreachable("not an interpolate-at StageOpKind");
    }
    CI.replaceAllUsesWith(New);
    CI.eraseFromParent();
    return true;
  };
  Changed |= forEachDXOpCall(F, 89, [&](CallInst &CI) { // EvalCentroid
    return raiseEval(CI, StageOpKind::InterpolateAtCentroid, 4);
  });
  Changed |= forEachDXOpCall(F, 88, [&](CallInst &CI) { // EvalSampleIndex
    return raiseEval(CI, StageOpKind::InterpolateAtSample, 5);
  });
  Changed |= forEachDXOpCall(F, 87, [&](CallInst &CI) { // EvalSnapped
    return raiseEval(CI, StageOpKind::InterpolateAtOffset, 6);
  });

  // The remaining ops are already raised to generic `llvm.dx.*` intrinsics
  // by `feme::dxil::OpRaisingPass` (context-free, so it does not need to
  // know this is a fragment entry point); this pass only needs to rename
  // them into the `feme.stage.*` family, since they're already legal LLVM
  // IR shaped exactly like their `feme.stage.*` peer.
  Changed |= forEachIntrinsicCall(F, Intrinsic::dx_discard, [](CallInst &CI) {
    IRBuilder<> B(&CI);
    createStageDiscard(B, CI.getArgOperand(0));
    CI.eraseFromParent();
    return true;
  });
  static const std::pair<Intrinsic::ID, StageOpKind> DerivativeMappings[] = {
      {Intrinsic::dx_ddx_fine, StageOpKind::DerivativeXFine},
      {Intrinsic::dx_ddy_fine, StageOpKind::DerivativeYFine},
      {Intrinsic::dx_ddx_coarse, StageOpKind::DerivativeXCoarse},
      {Intrinsic::dx_ddy_coarse, StageOpKind::DerivativeYCoarse},
  };
  for (const auto &Mapping : DerivativeMappings) {
    Intrinsic::ID ID = Mapping.first;
    StageOpKind Kind = Mapping.second;
    Changed |= forEachIntrinsicCall(F, ID, [&](CallInst &CI) {
      IRBuilder<> B(&CI);
      CallInst *New = createStageDerivative(B, Kind, CI.getArgOperand(0));
      CI.replaceAllUsesWith(New);
      CI.eraseFromParent();
      return true;
    });
  }
  static const std::pair<Intrinsic::ID, uint8_t> QuadReadMappings[] = {
      {Intrinsic::dx_quad_read_across_x, 0},
      {Intrinsic::dx_quad_read_across_y, 1},
      {Intrinsic::dx_quad_read_across_diagonal, 2},
  };
  for (const auto &Mapping : QuadReadMappings) {
    Intrinsic::ID ID = Mapping.first;
    uint8_t Direction = Mapping.second;
    Changed |= forEachIntrinsicCall(F, ID, [&](CallInst &CI) {
      IRBuilder<> B(&CI);
      CallInst *New = createStageQuadRead(B, CI.getArgOperand(0), Direction);
      CI.replaceAllUsesWith(New);
      CI.eraseFromParent();
      return true;
    });
  }
  return Changed;
}

} // namespace

PreservedAnalyses CanonicalizeStagePass::run(Module &M,
                                             ModuleAnalysisManager &AM) {
  bool Changed = false;
  for (Function &F : M) {
    std::optional<ShaderStage> Stage = getShaderStage(F);
    // G0 covers the vertex and fragment stages only (see the design's
    // "Canonical stage operations": "only operations required by
    // implemented stages are legal").
    if (!Stage ||
        (*Stage != ShaderStage::Vertex && *Stage != ShaderStage::Fragment))
      continue;

    // An absent signature (e.g. a hand-written test exercising only the
    // signature-independent rewrites below) is treated as an empty one:
    // `loadInput`/`storeOutput` then simply fail to resolve (left
    // unmodified, for `feme::graphics::ValidateStagePass` to diagnose),
    // while discard/derivative/quad-read/helper-lane rewriting -- which
    // needs no signature at all -- still proceeds.
    EntrySignature Sig = dxil::getEntrySignature(F).value_or(EntrySignature{});
    Changed |= canonicalizeDXILStage(F, Sig);
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
