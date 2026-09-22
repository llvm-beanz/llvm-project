//===- UnrollConstantTripCountLoops.cpp - Force-unroll small loops -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See UnrollConstantTripCountLoops.h for the full rationale (roadmap
// L128/L128(a)/L128(b)).
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/Graphics/UnrollConstantTripCountLoops.h"

#include "feme/Core/ShaderStage.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Scalar/LoopUnrollPass.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Utils/LCSSA.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

using namespace llvm;

namespace feme {
namespace graphics {

namespace {

/// A conservative upper bound on the trip counts this pass will force-
/// unroll: comfortably above every vertex-input-array shape this pass was
/// written for (`dEQP-VK`'s own `max_attributes` shader tops out at
/// `RowCount == 15`, `maxVertexInputAttributes - 1`), while still small
/// enough that force-unrolling any loop this size can never meaningfully
/// bloat compiled shader code.
constexpr unsigned MaxForcedFullUnrollTripCount = 64;

/// Attaches `!llvm.loop.unroll.full` metadata (the same forced-unroll
/// marker `#pragma unroll`/`[[unroll]]` produce) to every loop in the
/// current function whose exact trip count `ScalarEvolution` can prove is
/// a compile-time constant no larger than `MaxForcedFullUnrollTripCount`.
/// A plain (non-forced) `LoopFullUnrollPass` declines to unroll a loop
/// whose real trip count looks unprofitable by its own cost-model
/// heuristics; marking it explicitly here sidesteps that heuristic for
/// exactly the small, compile-time-bounded loops this pass targets,
/// without changing the cost model itself (which could affect unrelated
/// unrolling decisions elsewhere in the compiler).
struct MarkLoopsForForcedFullUnroll
    : llvm::OptionalPassInfoMixin<MarkLoopsForForcedFullUnroll> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
    ScalarEvolution &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);

    SmallVector<Loop *, 8> WorkList(LI.begin(), LI.end());
    bool Changed = false;
    while (!WorkList.empty()) {
      Loop *L = WorkList.pop_back_val();
      WorkList.append(L->begin(), L->end());

      unsigned TripCount = SE.getSmallConstantTripCount(L);
      if (TripCount != 0 && TripCount <= MaxForcedFullUnrollTripCount) {
        // Explicitly the `StringRef` overload, not
        // `addStringMetadataToLoop(Loop *, const char *, unsigned V = 0)`:
        // that overload's default `V == 0` produces a *false*-valued
        // "key = value" metadata node
        // (`llvm::getOptionalBoolLoopAttribute`'s own `case 2` reads the
        // operand as the actual boolean value), the opposite of what a
        // bare, name-only "attribute set" node (this overload's own
        // shape, `case 1`, always `true`) means -- a `const char*` string
        // literal argument binds to the former overload by default
        // (an exact match beats the latter's implicit `StringRef`
        // conversion), so this cast is required, not stylistic.
        addStringMetadataToLoop(L, StringRef("llvm.loop.unroll.full"));
        Changed = true;
      }
    }
    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

} // namespace

PreservedAnalyses
UnrollConstantTripCountStageLoopsPass::run(Module &M, ModuleAnalysisManager &) {
  // A self-contained `PassBuilder` setup (module/CGSCC/function/loop
  // analysis managers, all cross-registered) rather than relying on the
  // caller's own `ModuleAnalysisManager` -- this pass needs real function-
  // and loop-level analyses (`LoopInfo`, `ScalarEvolution`, the `AAManager`
  // `LoopFullUnrollPass`'s own adaptor requires) that a bare, minimally-
  // registered caller-supplied manager is not guaranteed to provide.
  PassBuilder PB;
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager LocalMAM;
  PB.registerModuleAnalyses(LocalMAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, LocalMAM);

  FunctionPassManager FPM;
  // `SROAPass` first: promotes the `alloca`s glslang's own SPIR-V-to-LLVM
  // translation leaves behind for a loop's induction variable and any
  // scalar loop-carried accumulator into SSA registers, which
  // `ScalarEvolution` needs to recognize the loop's trip count as a
  // provable constant in the first place.
  FPM.addPass(SROAPass(SROAOptions::PreserveCFG));
  FPM.addPass(LoopSimplifyPass());
  FPM.addPass(LCSSAPass());
  FPM.addPass(MarkLoopsForForcedFullUnroll());
  FPM.addPass(createFunctionToLoopPassAdaptor(
      LoopFullUnrollPass(/*OptLevel=*/2, /*OnlyWhenForced=*/true)));
  // Cleans up the fully-unrolled loop's now-redundant control flow and
  // per-iteration copies of loop-invariant instructions, so
  // `CanonicalizeStagePass` (run immediately after this pass, see this
  // pass's own header comment) sees `K` separate, simplified, constant-
  // indexed stage-IO accesses rather than `K` copies of a more convoluted
  // loop body.
  FPM.addPass(InstCombinePass());
  FPM.addPass(SimplifyCFGPass());

  // `SROAPass`/`InstCombinePass` above turn a whole-aggregate `alloca` of
  // e.g. a `matNx3` stage-IO temporary (`[K x <3 x float>]`, one `<3 x
  // float>` per matrix column) into `K` separate, literal-byte-offset
  // `getelementptr`s -- offsets `InstCombine` computes, and permanently
  // bakes in as plain integer constants with no further type information,
  // from \p M's *current* `DataLayout`. At this point in the pipeline that
  // is still `importShaderModule`'s own SPIR-V-execution-model one (see
  // this function's own header comment, and `CanonicalizeStagePass`'s
  // preserved-until-`Pipeline.cpp`'s-host-substitution rationale, roadmap
  // H82) -- which, for `feme-vulkan`'s in-process MLIR SPIR-V-to-LLVM
  // translation, is MLIR's own generic placeholder
  // (`e-ve-i64:64-n8:16:32:64-G10`), *not* a real target's. That
  // placeholder has no explicit vector-alignment spec at all, so it
  // resolves a `<3 x float>` array element's ABI size/alignment to its
  // tightly-packed 12-byte store size, not the 16-byte-rounded-up-to-a-
  // power-of-two size every real target (and LLVM's own default,
  // built-in rule for a vector type the data layout string does not
  // otherwise override, see `DataLayout::getAlignment`) actually gives
  // it. Every *other* place this same `[K x <3 x float>]` value's layout
  // matters -- SPIR-V's own `Offset`-decoration-derived struct layout
  // `CanonicalizeStagePass` resolves against (std140/std430, which pads
  // a 3-vector to a 4-vector's size for exactly this reason), and the
  // real host `DataLayout` substituted later in `Pipeline.cpp` -- agrees
  // on the padded, 16-byte stride. Baking in the *placeholder*'s
  // tightly-packed 12-byte stride here instead is a genuine, silent
  // byte-offset mismatch against every one of those (this pass's own
  // roadmap L150 miscompile). Temporarily substituting a plain, default-
  // constructed `DataLayout` (LLVM's built-in defaults; deliberately
  // *not* a real target's, so as not to reintroduce a dependency this
  // library does not otherwise have on `FeMeTargetCPU`/a `TargetMachine`)
  // for the ordinary LLVM-IR-optimization work this `FunctionPassManager`
  // does resolves the mismatch: its vector-alignment rule already agrees
  // with every real target's on this shape, and the module's actual
  // `DataLayout` is restored immediately afterward, so nothing else in
  // the pipeline (in particular `CanonicalizeStagePass`, run immediately
  // after this pass returns) is affected.
  DataLayout OriginalDL = M.getDataLayout();
  M.setDataLayout(DataLayout());

  bool Changed = false;
  for (Function &F : M) {
    // Scoped to Vertex/Fragment entry points only -- see this pass's own
    // header comment for why every other recognized stage's own arrayed
    // stage-IO global is a different (always-dynamic) shape this pass
    // cannot help with.
    std::optional<ShaderStage> Stage = getShaderStage(F);
    if (!Stage ||
        (*Stage != ShaderStage::Vertex && *Stage != ShaderStage::Fragment))
      continue;
    if (F.isDeclaration())
      continue;
    PreservedAnalyses PA = FPM.run(F, FAM);
    Changed |= !PA.areAllPreserved();
  }

  M.setDataLayout(OriginalDL);

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace graphics
} // namespace feme
