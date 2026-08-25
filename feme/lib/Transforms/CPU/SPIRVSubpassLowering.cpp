//===- SPIRVSubpassLowering.cpp - SPIR-V subpassInput ABI plumbing ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SPIRVSubpassLowering.h"

#include "feme/Core/StageOps.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Whether \p F contains at least one `feme.stage.subpass.load` call.
bool usesSubpassLoad(Function &F) {
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    feme::StageOpKind Kind;
    if (CI && feme::isStageOpCall(*CI, &Kind) &&
        Kind == feme::StageOpKind::SubpassLoad)
      return true;
  }
  return false;
}

/// Replaces \p F with an identical function that additionally takes
/// `subpass_input_heap` (a `ptr`) and `subpass_input_heap_count` (an
/// `i32`) as its two new trailing parameters -- the same
/// Function-replacement shape `feme::cpu::SPIRVResourceLoweringPass::
/// addResourceEnvParams` uses for its own eight trailing parameters (see
/// that function's comment for why a whole new `Function` is needed rather
/// than mutating \p F's type in place).
Function *addSubpassInputHeapParams(Function &F) {
  LLVMContext &Ctx = F.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);

  SmallVector<Type *, 8> ParamTypes(F.getFunctionType()->params());
  ParamTypes.append({PtrTy, I32Ty});

  FunctionType *NewTy = FunctionType::get(F.getReturnType(), ParamTypes,
                                          F.getFunctionType()->isVarArg());
  Function *NewF = Function::Create(NewTy, F.getLinkage(), F.getAddressSpace(),
                                    "", F.getParent());
  NewF->copyAttributesFrom(&F);
  NewF->setComdat(F.getComdat());
  // `feme.signature` (the entry's `EntrySignature`, attached by
  // `feme::graphics::CanonicalizeStagePass` before this pass ever runs)
  // and every other function-level metadata node must survive this
  // Function-replacement, exactly like `addResourceEnvParams` and every
  // other stage-wrapper pass already copies for its own trailing-parameter
  // replacement -- `feme::cpu::FragmentWrapperPass`'s `lowerFragmentStageOps`
  // reads that metadata back much later, after `feme::cpu::SIMDizePass`
  // widening.
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  F.getAllMetadata(MDs);
  for (auto [Kind, Node] : MDs)
    NewF->setMetadata(Kind, Node);
  NewF->splice(NewF->begin(), &F);

  for (auto [OldArg, NewArg] : zip(F.args(), NewF->args())) {
    NewArg.takeName(&OldArg);
    OldArg.replaceAllUsesWith(&NewArg);
  }

  auto ArgIt = NewF->arg_begin() + F.arg_size();
  (&*ArgIt++)->setName("subpass_input_heap");
  (&*ArgIt++)->setName("subpass_input_heap_count");

  NewF->takeName(&F);
  F.replaceAllUsesWith(NewF);
  F.eraseFromParent();
  return NewF;
}

} // namespace

PreservedAnalyses SPIRVSubpassLoweringPass::run(Module &M,
                                                ModuleAnalysisManager &) {
  // Snapshot every function needing the fix before mutating anything:
  // `addSubpassInputHeapParams` replaces `F` with a new `Function` appended
  // to `M`'s function list, which -- unlike iterating a pre-built
  // collection -- a live `M.functions()` walk (even an early-inc-range one)
  // can still reach later in the same loop, adding a second, colliding set
  // of trailing parameters to it.
  SmallVector<Function *, 4> Candidates;
  for (Function &F : M)
    if (!F.isDeclaration() && usesSubpassLoad(F))
      Candidates.push_back(&F);
  for (Function *F : Candidates)
    addSubpassInputHeapParams(*F);
  return Candidates.empty() ? PreservedAnalyses::all()
                            : PreservedAnalyses::none();
}
