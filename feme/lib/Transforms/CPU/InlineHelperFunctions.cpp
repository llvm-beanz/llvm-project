//===- InlineHelperFunctions.cpp - Inline non-entry-point functions ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/InlineHelperFunctions.h"

#include "feme/Core/ShaderStage.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"

using namespace llvm;
using namespace feme::cpu;

PreservedAnalyses InlineHelperFunctionsPass::run(Module &M,
                                                 ModuleAnalysisManager &AM) {
  // Mark every non-declaration, non-entry-point function `alwaysinline` and
  // `internal` -- the two properties `llvm::AlwaysInlinerPass` needs to
  // guarantee it inlines every call to it (an external-linkage function
  // could still be called from outside this module, so the inliner
  // otherwise has to leave at least one definition behind) -- so the whole
  // module ends up with exactly one non-declaration function per shader
  // stage, the invariant every later CPU-pipeline pass already assumes (see
  // this pass's header comment).
  bool SawHelper = false;
  for (Function &F : M) {
    if (F.isDeclaration() || feme::isShaderEntryPoint(F))
      continue;
    F.addFnAttr(Attribute::AlwaysInline);
    F.setLinkage(GlobalValue::InternalLinkage);
    SawHelper = true;
  }

  // The common case (an HLSL/DXIL-sourced module, or a GLSL/glslang-sourced
  // one with no surviving helper function) has nothing to inline; skip the
  // two nested module passes below entirely rather than pay their cost (and
  // report spurious "changed" analyses invalidation) for a no-op.
  if (!SawHelper)
    return PreservedAnalyses::all();

  AlwaysInlinerPass().run(M, AM);
  // Removes the now-callerless helper function bodies `AlwaysInlinerPass`
  // leaves behind (it deletes a callee only once every call to it has been
  // inlined away, which erases the calls but not the orphaned definition
  // itself).
  GlobalDCEPass().run(M, AM);

  return PreservedAnalyses::none();
}
