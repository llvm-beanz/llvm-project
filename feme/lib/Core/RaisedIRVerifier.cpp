//===- RaisedIRVerifier.cpp - Diagnose leftover raised-IR conventions ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Core/RaisedIRVerifier.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace feme {
namespace {

/// Whether \p Ty is one of the raised resource handle type families this
/// check rejects: DXIL's `target("dx.")` or SPIR-V's `target("spirv.")`.
bool isRaisedHandleType(const Type *Ty) {
  const auto *TargetExtTy = dyn_cast<TargetExtType>(Ty);
  if (!TargetExtTy)
    return false;
  StringRef Name = TargetExtTy->getName();
  return Name.starts_with("dx.") || Name.starts_with("spirv.");
}

/// Whether \p Name is a format-specific op this pipeline should have
/// already rewritten into ordinary LLVM IR by the time a real
/// `llvm::TargetMachine` sees it: either one of `feme::dxil::OpRaisingPass`'s
/// `llvm.dx.*` outputs (or a SPIR-V `Translator`'s `llvm.spv.*` one), or a
/// raw, not-yet-raised DXIL calling-convention op (`dx.op.*`, e.g.
/// `dx.op.textureLoad.f16` -- see `test/Transforms/DXIL/
/// dxil-raise-texture-ops.ll`'s own declarations for the shape) that
/// `OpRaisingPass` itself does not yet cover for every resource kind (its
/// legacy, non-bindless texture/sampler path -- see Design.md's "Decision:
/// texture and sampler handle kinds"). Neither is a real target's own
/// intrinsic (`llvm.amdgcn.*`, ...).
bool isRaisedOpName(StringRef Name) {
  return Name.starts_with("llvm.dx.") || Name.starts_with("llvm.spv.") ||
         Name.starts_with("dx.op.");
}

/// Returns the first call to \p F -- one of the raised intrinsic
/// declarations \p F names -- found in the module, or nullptr if \p F has
/// no callers left (e.g. its declaration merely survived a lowering pass
/// that already rewrote every use, see `ResourceLoweringPass::run`'s own
/// dead-declaration cleanup).
const CallInst *findRemainingCall(const Function &F) {
  for (const User *U : F.users()) {
    const auto *CI = dyn_cast<CallInst>(U);
    if (CI && CI->getCalledFunction() == &F)
      return CI;
  }
  return nullptr;
}

} // namespace

llvm::Error verifyNoRaisedIRRemains(const Module &M,
                                    StringRef TargetDescription) {
  for (const Function &F : M) {
    if (isRaisedOpName(F.getName())) {
      if (const CallInst *CI = findRemainingCall(F)) {
        const Function *Caller = CI->getFunction();
        return createStringError(
            inconvertibleErrorCode(),
            "'%s' is not supported when targeting '%s' (used in function "
            "'%s')",
            F.getName().str().c_str(), TargetDescription.str().c_str(),
            Caller ? Caller->getName().str().c_str() : "<unknown>");
      }
    }

    if (F.isDeclaration())
      continue;

    for (const Argument &Arg : F.args())
      if (isRaisedHandleType(Arg.getType()))
        return createStringError(
            inconvertibleErrorCode(),
            "resource handle type '%s' is not supported when targeting "
            "'%s' (parameter of function '%s')",
            cast<TargetExtType>(Arg.getType())->getName().str().c_str(),
            TargetDescription.str().c_str(), F.getName().str().c_str());

    for (const Instruction &I : instructions(F))
      if (isRaisedHandleType(I.getType()))
        return createStringError(
            inconvertibleErrorCode(),
            "resource handle type '%s' is not supported when targeting "
            "'%s' (produced in function '%s')",
            cast<TargetExtType>(I.getType())->getName().str().c_str(),
            TargetDescription.str().c_str(), F.getName().str().c_str());
  }
  return Error::success();
}

} // namespace feme
