//===- Prepare.cpp - CPU target Phase 1: preparation ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/Prepare.h"

#include "feme/Core/ShaderStage.h"
#include "feme/Transforms/CPU/VerifyStructured.h"

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
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Scalar/StructurizeCFG.h"
#include "llvm/Transforms/Utils/BreakCriticalEdges.h"
#include "llvm/Transforms/Utils/FixIrreducible.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Utils/UnifyLoopExits.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Runs the function-local half of Phase 1 on \p F: `SROA` first (roadmap
/// H19a: splits a `Function`-storage-class SPIR-V local variable lowered as
/// a vector-typed `alloca` -- e.g. a `gl_GlobalInvocationID.xy` scratch
/// temporary GEP-indexed one element at a time -- into per-element scalar
/// allocas, since plain `mem2reg` alone refuses to promote any `alloca`
/// with a `getelementptr` use at all, `isAllocaPromotable`'s documented
/// precondition; reduced from a real failing
/// `dEQP-VK.image.load_store.with_format.2d.r32_sfloat` case, whose
/// compute shader's own coordinate computation takes exactly this shape),
/// then `mem2reg` (so the structurizer sees as few `alloca`s live across
/// blocks as possible -- `SROAPass` already promotes most of what it
/// splits on its own, but a residual whole-value `alloca` `SROA` did not
/// need to split still needs this pass), then `LowerSwitch` (the linearizer
/// only understands two-way branches), then `FixIrreducible` +
/// `UnifyLoopExits` + `StructurizeCFG` -- in that order, matching
/// `StructurizeCFG`'s own documented precondition that irreducible control
/// flow and multi-exit loops are already gone -- and finally
/// `BreakCriticalEdges`: `StructurizeCFG`'s own "Flow" blocks (built to
/// merge a divergent branch's two arms back together, see its
/// documentation) can themselves leave a critical edge behind (a branch
/// with more than one successor into a block with more than one
/// predecessor), which the linearizer's mask-merging at a branch's targets
/// cannot be built on top of -- see `feme::cpu::verifyStructured`'s "no
/// critical edges" postcondition.
void prepareFunction(Function &F, FunctionAnalysisManager &FAM) {
  FunctionPassManager FPM;
  FPM.addPass(SROAPass(SROAOptions()));
  FPM.addPass(PromotePass());
  FPM.addPass(LowerSwitchPass());
  FPM.addPass(FixIrreduciblePass());
  FPM.addPass(UnifyLoopExitsPass());
  FPM.addPass(StructurizeCFGPass());
  FPM.addPass(BreakCriticalEdgesPass());
  FPM.run(F, FAM);
}

/// Selects the single \p Stage entry point Phase 1 keeps: \p EntryPoint by
/// name if given, else the module's only one. Every other entry point of
/// that stage is a diagnosable ambiguity (`EntryPoint` empty, more than one
/// candidate) or an outright miss (`EntryPoint` names nothing), rather than
/// an arbitrary pick -- "Canonicalize entry points" in
/// feme/docs/FeMeCPUDesign.md requires selection, not guessing.
///
/// The stage is the checked `feme::ShaderStage` an entry point declares (see
/// `feme::getShaderStage`), not a string comparison against one attribute's
/// value, so that selecting a non-compute stage is an argument rather than a
/// second rule.
Expected<Function *> selectEntryPoint(Module &M, StringRef EntryPoint,
                                      feme::ShaderStage Stage) {
  StringRef StageName = feme::getShaderStageName(Stage);
  auto declaresStage = [Stage](const Function &F) {
    return feme::getShaderStage(F) == Stage;
  };

  if (!EntryPoint.empty()) {
    Function *F = M.getFunction(EntryPoint);
    if (!F || !declaresStage(*F))
      return createStringError(
          errc::invalid_argument, "no %s entry point named '%s' in this module",
          StageName.str().c_str(), EntryPoint.str().c_str());
    return F;
  }

  Function *Found = nullptr;
  for (Function &F : M) {
    if (!declaresStage(F))
      continue;
    if (Found)
      return createStringError(
          errc::invalid_argument,
          "module has more than one %s entry point; select one with "
          "the entry-point option",
          StageName.str().c_str());
    Found = &F;
  }
  if (!Found)
    return createStringError(errc::invalid_argument,
                             "module has no %s entry point",
                             StageName.str().c_str());
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
  Expected<Function *> Entry = selectEntryPoint(M, EntryPoint, Stage);
  if (!Entry) {
    // Selecting an entry point is a precondition every later phase
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

  // Phase 1's postconditions (see "CFG restructurization test suite" in
  // feme/docs/FeMeCPUDesign.md) hold by construction if `StructurizeCFG` and
  // friends did their job; checking them here, once, on every run rather
  // than only in the dedicated test suite catches a regression as close to
  // its cause as possible. Assertions-only: a release build trusts the
  // upstream passes rather than paying for the check on every compile.
  assert(verifyStructured(**Entry) &&
         "feme-cpu-prepare: output function is not structured; see "
         "-verify-structured for details");

  return PreservedAnalyses::none();
}
