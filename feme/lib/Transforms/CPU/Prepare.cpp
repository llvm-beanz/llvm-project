//===- Prepare.cpp - CPU target Phase 1: preparation ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/Prepare.h"

#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar/StructurizeCFG.h"
#include "llvm/Transforms/Utils/FixIrreducible.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Utils/UnifyLoopExits.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Runs the function-local half of Phase 1 on \p F: `mem2reg` first (so the
/// structurizer sees as few `alloca`s live across blocks as possible), then
/// `LowerSwitch` (the linearizer only understands two-way branches), then
/// `FixIrreducible` + `UnifyLoopExits` + `StructurizeCFG` -- in that order,
/// matching `StructurizeCFG`'s own documented precondition that irreducible
/// control flow and multi-exit loops are already gone.
void prepareFunction(Function &F, FunctionAnalysisManager &FAM) {
  FunctionPassManager FPM;
  FPM.addPass(PromotePass());
  FPM.addPass(LowerSwitchPass());
  FPM.addPass(FixIrreduciblePass());
  FPM.addPass(UnifyLoopExitsPass());
  FPM.addPass(StructurizeCFGPass());
  FPM.run(F, FAM);
}

/// Whether \p F is a compute entry point (the only kind FeMe's front ends
/// raise today, see "Format-Agnostic Operation" in
/// feme/docs/FeMeCPUDesign.md).
bool isComputeEntryPoint(const Function &F) {
  return F.hasFnAttribute("hlsl.shader") &&
         F.getFnAttribute("hlsl.shader").getValueAsString() == "compute";
}

/// Selects the single compute entry point Phase 1 keeps: \p EntryPoint by
/// name if given, else the module's only one. Every other
/// `hlsl.shader="compute"` function is a diagnosable ambiguity (`EntryPoint`
/// empty, more than one candidate) or an outright miss (`EntryPoint` names
/// nothing), rather than an arbitrary pick -- "Canonicalize entry points" in
/// feme/docs/FeMeCPUDesign.md requires selection, not guessing.
Expected<Function *> selectEntryPoint(Module &M, StringRef EntryPoint) {
  if (!EntryPoint.empty()) {
    Function *F = M.getFunction(EntryPoint);
    if (!F || !isComputeEntryPoint(*F))
      return createStringError(
          errc::invalid_argument,
          "no compute entry point named '%s' in this module",
          EntryPoint.str().c_str());
    return F;
  }

  Function *Found = nullptr;
  for (Function &F : M) {
    if (!isComputeEntryPoint(F))
      continue;
    if (Found)
      return createStringError(
          errc::invalid_argument,
          "module has more than one compute entry point; select one with "
          "the entry-point option");
    Found = &F;
  }
  if (!Found)
    return createStringError(errc::invalid_argument,
                             "module has no compute entry point");
  return Found;
}

/// Removes every function definition not reachable from \p Entry's call
/// graph (a plain call-instruction walk suffices: raised shaders have no
/// indirect calls), leaving only the single root Phase 6's wrapper needs.
/// Function declarations (intrinsics, not-yet-lowered raised ops) are left
/// alone -- they carry no body to be unreachable from.
void removeUnreachableDefinitions(Module &M, Function &Entry) {
  SetVector<Function *> Reachable;
  Reachable.insert(&Entry);
  for (unsigned I = 0; I != Reachable.size(); ++I) {
    Function *F = Reachable[I];
    if (F->isDeclaration())
      continue;
    for (Instruction &Inst : instructions(F))
      if (auto *CI = dyn_cast<CallInst>(&Inst))
        if (Function *Callee = CI->getCalledFunction())
          Reachable.insert(Callee);
  }

  for (Function &F : make_early_inc_range(M.functions())) {
    if (F.isDeclaration() || Reachable.contains(&F))
      continue;
    F.replaceAllUsesWith(PoisonValue::get(F.getType()));
    F.eraseFromParent();
  }
}

} // namespace

PreservedAnalyses PreparePass::run(Module &M, ModuleAnalysisManager &) {
  Expected<Function *> Entry = selectEntryPoint(M, EntryPoint);
  if (!Entry) {
    // Selecting a compute entry point is a precondition every later phase
    // relies on (Phase 6 needs a single wrapper root); there is no
    // meaningful IR to hand onward if it fails. `ModulePassManager::run` has
    // no `Error`-returning path of its own, so this reports the failure
    // through the module's diagnostic handler instead -- the driver
    // (`feme`/`feme-opt`) installs one that turns a `DS_Error`-severity
    // diagnostic into an ordinary tool failure.
    M.getContext().emitError("feme-cpu-prepare: " +
                             toString(Entry.takeError()));
    return PreservedAnalyses::all();
  }

  removeUnreachableDefinitions(M, **Entry);

  PassBuilder PB;
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  prepareFunction(**Entry, FAM);

  return PreservedAnalyses::none();
}
