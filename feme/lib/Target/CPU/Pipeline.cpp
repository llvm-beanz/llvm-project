//===- Pipeline.cpp - FeMe CPU target lowering pipeline ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/Pipeline.h"

#include "feme/Target/CPU/RuntimeCPU.h"
#include "feme/Transforms/CPU/EntryWrapper.h"
#include "feme/Transforms/CPU/Linearize.h"
#include "feme/Transforms/CPU/Prepare.h"
#include "feme/Transforms/CPU/ResourceLowering.h"
#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/CPU/WaveLowering.h"

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Error.h"

using namespace llvm;

namespace {

/// Finds the sole (or named) `hlsl.shader` function in \p M -- see
/// `feme::cpu::PreparePass`'s own (equivalent, but `Error`-reporting-via-
/// diagnostic-handler rather than `Expected`-returning) selection rule,
/// which this mirrors so `runPipeline` can name-check its own precondition
/// before handing \p EntryPoint to that pass.
Expected<Function *> selectEntryPoint(Module &M, StringRef EntryPoint) {
  if (!EntryPoint.empty()) {
    Function *F = M.getFunction(EntryPoint);
    if (!F || !F->hasFnAttribute("hlsl.shader"))
      return createStringError(inconvertibleErrorCode(),
                               "no compute entry point named '%s'",
                               EntryPoint.str().c_str());
    return F;
  }
  Function *Found = nullptr;
  for (Function &F : M) {
    if (!F.hasFnAttribute("hlsl.shader"))
      continue;
    if (Found)
      return createStringError(
          inconvertibleErrorCode(),
          "module has more than one compute entry point; select one by "
          "name");
    Found = &F;
  }
  if (!Found)
    return createStringError(inconvertibleErrorCode(),
                             "module has no compute entry point");
  return Found;
}

/// `FeMeRuntimeCPU.c`'s externally-visible helpers are given their
/// canonical dotted `feme.cpu.resource.*`/`feme.cpu.rt.*` names via a GNU
/// `asm` label (see that file's top comment), since a dotted name is not a
/// valid C identifier. On Mach-O targets, Clang spells an `asm`-labeled
/// symbol's LLVM IR name with a leading `'\1'` (SOH) byte, a convention the
/// AsmPrinter recognizes as "emit this name verbatim, without the
/// platform's usual global-symbol mangling" (i.e. without Mach-O's leading
/// underscore) -- see `Mangler::getNameWithPrefix`. That byte is part of
/// the `GlobalValue`'s actual name, though, so it also defeats the
/// exact-name matching `Linker::linkInModule(..., LinkOnlyNeeded)` uses
/// below: the plain (unescaped) declaration a raised shader module creates
/// never matches the runtime module's `'\1'`-prefixed definition, so the
/// helper never gets linked in, leaving the declaration to fail symbol
/// resolution instead. Strip that leading byte from every global in the
/// freshly-parsed runtime module so its names line up with the plain
/// canonical names regardless of host object format.
void stripAsmLabelManglingEscape(Module &M) {
  for (GlobalValue &GV : M.global_values()) {
    StringRef Name = GV.getName();
    if (Name.starts_with('\1'))
      GV.setName(Name.drop_front());
  }
}

/// `FeMeRuntimeCPU.c` is compiled to bitcode by an unadorned `clang -c
/// -emit-llvm` invocation (see feme/runtime/CPU/CMakeLists.txt), with no
/// explicit `-target`, so its module carries whichever triple Clang treats
/// as its default for the build host -- which need not be textually
/// identical to the (already normalized) triple `feme::Driver::run`
/// resolved from `--target`/`%feme_host_triple` for \p M, even when both
/// name the very same target (e.g. Clang's Mach-O default spells its OS
/// component "macosx<ver>" where an explicit "--target=...-darwin<ver>"
/// triple spells it "darwin<ver>"). `RuntimeMod` is plain freestanding C
/// with no target-specific codegen of its own, so it is always safe to
/// retarget to \p M's triple -- doing so before linking avoids
/// `Linker::linkInModule` emitting a spurious "Linking two modules of
/// different target triples" warning for what is, in truth, the same
/// target.
void alignRuntimeModuleTriple(Module &RuntimeMod, const Module &M) {
  RuntimeMod.setTargetTriple(M.getTargetTriple());
}

} // namespace

namespace feme::cpu {

Expected<PipelineResult> runPipeline(Module &M, StringRef EntryPoint,
                                     unsigned WaveSize) {
  Expected<Function *> Entry = selectEntryPoint(M, EntryPoint);
  if (!Entry)
    return Entry.takeError();
  std::string EntryName = (*Entry)->getName().str();

  {
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

    ModulePassManager MPM;
    MPM.addPass(PreparePass(EntryPoint));
    MPM.addPass(ResourceLoweringPass());
    MPM.addPass(LinearizePass());
    MPM.addPass(SIMDizePass(WaveSize));
    MPM.addPass(WaveLoweringPass());
    MPM.addPass(EntryWrapperPass());
    MPM.run(M, MAM);
  }

  std::string WrapperName = getEntrySymbolName(EntryName);
  if (!M.getFunction(WrapperName))
    return createStringError(
        inconvertibleErrorCode(),
        "feme-cpu-wrap-entry did not produce '%s'; the shader is likely not "
        "acyclic, uniform control flow (see feme::cpu::SIMDizePass, "
        "roadmap milestone 4)",
        WrapperName.c_str());

  // Link in only the referenced `libFeMeRuntimeCPU` helper definitions (see
  // "Runtime Support Library" in feme/docs/FeMeCPUDesign.md).
  Expected<std::unique_ptr<Module>> RuntimeMod =
      parseBitcodeFile(getRuntimeCPUBitcode(), M.getContext());
  if (!RuntimeMod)
    return RuntimeMod.takeError();
  stripAsmLabelManglingEscape(**RuntimeMod);
  alignRuntimeModuleTriple(**RuntimeMod, M);
  Linker L(M);
  if (L.linkInModule(std::move(*RuntimeMod), Linker::Flags::LinkOnlyNeeded))
    return createStringError(inconvertibleErrorCode(),
                             "failed to link libFeMeRuntimeCPU");

  return PipelineResult{std::move(EntryName), std::move(WrapperName)};
}

} // namespace feme::cpu
