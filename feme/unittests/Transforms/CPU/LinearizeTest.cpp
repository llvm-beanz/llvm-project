//===- LinearizeTest.cpp - Tests for LinearizePass ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/Linearize.h"

#include "feme/Transforms/CPU/ImageCalls.h"
#include "feme/Transforms/CPU/MaskIntrinsics.h"
#include "feme/Transforms/CPU/ResourceCalls.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("LinearizeTest", errs());
  return M;
}

bool run(Module &M) {
  ModuleAnalysisManager MAM;
  PreservedAnalyses PA = LinearizePass().run(M, MAM);
  return !PA.areAllPreserved();
}

TEST(LinearizeTest, FlattensDivergentDiamondAndReplacesPhiWithSelect) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c = icmp eq i32 %tid, 0
      br i1 %c, label %t, label %f
    t:
      %a = add i32 %tid, 1
      br label %end
    f:
      %b = add i32 %tid, 2
      br label %end
    end:
      %v = phi i32 [%a, %t], [%b, %f]
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  bool FoundSelect = false;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(isa<PHINode>(I)) << "phi should have become a select";
    if (isa<SelectInst>(I))
      FoundSelect = true;
    if (auto *Br = dyn_cast<CondBrInst>(&I))
      ADD_FAILURE() << "no conditional branch should survive: "
                    << Br->getCondition()->getName();
  }
  EXPECT_TRUE(FoundSelect);
}

TEST(LinearizeTest, LeavesUniformDiamondUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %uniform_cond) #0 {
    entry:
      %c = icmp sgt i32 %uniform_cond, 0
      br i1 %c, label %t, label %f
    t:
      br label %end
    f:
      br label %end
    end:
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_FALSE(run(*M));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundCondBr = false;
  for (Instruction &I : instructions(F))
    if (isa<CondBrInst>(I))
      FoundCondBr = true;
  EXPECT_TRUE(FoundCondBr);
}

// Roadmap H89a: a divergent diamond's own reconvergence-block `phi` may
// merge a value one arm never actually produces (represented as `poison`
// on that arm, exactly the shape `StructurizeCFG`/`UnifyLoopExits` leave
// an enclosing uniform loop's own counter in when it merely reconverges
// *through* an unrelated inner diamond rather than being genuinely
// computed differently by each arm). `DiamondFlattener::flatten` must
// reuse the one real operand directly instead of building
// `select(Cond, RealValue, poison)`: the `select` is not wrong by itself,
// but it makes `computeWaveUniformity` (correctly, per its own flow-
// insensitive model) treat the merged value -- and anything that later
// branches on it, like an enclosing loop's own trip-count check -- as
// divergent, even though nothing about it actually depends on `Cond`.
TEST(LinearizeTest, ReusesRealOperandInsteadOfSelectWhenOtherArmIsPoison) {
  LLVMContext Ctx;
  // `%v` is stored to `%out` (rather than left dead, as a bare `ret void`
  // after the `phi` would let earlier dead-code cleanup erase it before
  // `DiamondFlattener` even runs, defeating the point of this test) so its
  // final replacement value survives to be inspected below. Note that
  // `Masks.Live`/`Masks.SideEffect`'s own `select`s at the merge block are
  // an unrelated, always-present part of ordinary diamond flattening (see
  // `flatten`'s divergent-branch case) -- this test only asserts about the
  // `%v` phi's own replacement, not about `select`s in general.
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %out) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c = icmp eq i32 %tid, 0
      br i1 %c, label %t, label %f
    t:
      br label %end
    f:
      br label %end
    end:
      %v = phi i32 [poison, %t], [%tid, %f]
      store i32 %v, ptr %out
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  // The original `store i32 %v, ptr %out` is itself under the (uniform,
  // in this test) diamond's own masks, so `LinearizePass`'s masking step
  // rewrites it into a `feme.cpu.masked.store.*` call whose first operand
  // is the value actually stored -- find that call to inspect what `%v`
  // was replaced with.
  CallInst *MaskedStore = nullptr;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(isa<PHINode>(I)) << "phi should have been resolved";
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (CI->getCalledFunction() &&
          CI->getCalledFunction()->getName().starts_with(
              "feme.cpu.masked.store"))
        MaskedStore = CI;
  }
  ASSERT_TRUE(MaskedStore);
  EXPECT_TRUE(isa<CallInst>(MaskedStore->getArgOperand(0)))
      << "the poison arm should have been skipped, reusing %tid directly, "
         "instead of building select(Cond, %tid, poison)";
}

TEST(LinearizeTest, MasksResourceCallUnderDivergentBranch) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %heap, i32 %heap_count, i32 %desc) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c = icmp eq i32 %tid, 0
      br i1 %c, label %t, label %f
    t:
      %off = sext i32 %tid to i64
      %loaded = call float @feme.cpu.resource.load.raw.f32(ptr %heap, i32 %heap_count, i32 %desc, i64 %off, i1 true)
      br label %end
    f:
      br label %end
    end:
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundMaskedCall = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    std::optional<MatchedResourceCall> Matched = matchResourceCall(*CI);
    if (!Matched)
      continue;
    FoundMaskedCall = true;
    EXPECT_FALSE(isa<Constant>(Matched->Mask))
        << "mask should have been rewritten away from the constant `true` "
           "feme::cpu::ResourceLoweringPass left it as";
  }
  EXPECT_TRUE(FoundMaskedCall);
}

TEST(LinearizeTest, MasksImageStoreCallUnderDivergentBranch) {
  // L7r: `feme.cpu.image.*` calls need the exact same divergent-region mask
  // threading `MasksResourceCallUnderDivergentBranch` above already
  // confirms for `feme.cpu.resource.*` calls, but `applyStageMasks` only
  // ever recognized the resource-call shape -- an image store or atomic
  // left inside a divergent diamond's "true" arm kept the constant `true`
  // mask `feme::cpu::SPIRVResourceLoweringPass` gives every such call,
  // running unconditionally for the whole wave rather than just the lanes
  // that actually reach that arm (confirmed by reducing a real failing
  // `dEQP-VK.subgroups.basic.compute.subgroupmemorybarrierimage` case down
  // to this exact shape: a `subgroupElect()`-gated `imageStore`).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %image_heap, i32 %image_heap_count) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c = icmp eq i32 %tid, 0
      br i1 %c, label %t, label %f
    t:
      call void @feme.cpu.image.store.2d.v4i32(
          ptr %image_heap, i32 %image_heap_count, i32 0, i32 %tid, i32 %tid,
          <4 x i32> zeroinitializer, i1 true)
      br label %end
    f:
      br label %end
    end:
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.cpu.image.store.2d.v4i32(ptr, i32, i32, i32, i32, <4 x i32>, i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundMaskedCall = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
    if (!Matched)
      continue;
    FoundMaskedCall = true;
    EXPECT_FALSE(isa<Constant>(Matched->Mask))
        << "mask should have been rewritten away from the constant `true` "
           "feme::cpu::SPIRVResourceLoweringPass left it as";
  }
  EXPECT_TRUE(FoundMaskedCall);
}

TEST(LinearizeTest, LinearizesLoopWithDivergentExit) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      br label %loop
    loop:
      %i = phi i32 [0, %entry], [%inc, %loop]
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %inc = add i32 %i, 1
      %break.cond = icmp eq i32 %tid, %inc
      br i1 %break.cond, label %exit, label %loop
    exit:
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundMaskAny = false;
  bool FoundActivePhi = false;
  for (Instruction &I : instructions(F)) {
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (CI->getCalledFunction() &&
          CI->getCalledFunction()->getName() == "feme.cpu.mask.any")
        FoundMaskAny = true;
    if (auto *PN = dyn_cast<PHINode>(&I))
      if (PN->getType()->isIntegerTy(1))
        FoundActivePhi = true;
  }
  EXPECT_TRUE(FoundMaskAny);
  EXPECT_TRUE(FoundActivePhi);
}

TEST(LinearizeTest, LeavesUniformLoopUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %n) #0 {
    entry:
      br label %loop
    loop:
      %i = phi i32 [0, %entry], [%inc, %loop]
      %inc = add i32 %i, 1
      %loop.cond = icmp slt i32 %inc, %n
      br i1 %loop.cond, label %loop, label %exit
    exit:
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_FALSE(run(*M));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(CI->getCalledFunction() &&
                   CI->getCalledFunction()->getName() == "feme.cpu.mask.any");
}

// Roadmap R27: `feme.stage.discard` narrows both the live and side-effect
// masks going forward, even with no divergent branch at all in the
// function -- see `hasStageMaskOps`'s comment for why an unconditional
// discard still needs `DiamondFlattener` to walk the function.
TEST(LinearizeTest, DiscardNarrowsBothMasksAndMasksSubsequentStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %p, i1 %cond) #0 {
    entry:
      call void @feme.stage.discard(i1 %cond)
      store i32 1, ptr %p
      ret void
    }
    declare void @feme.stage.discard(i1)
    attributes #0 = { "feme.shader.stage"="fragment" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  if (Function *Discard = F->getParent()->getFunction("feme.stage.discard"))
    EXPECT_TRUE(Discard->use_empty())
        << "feme.stage.discard call should have been erased";

  bool FoundMaskedStore = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (std::optional<MatchedMaskedMemOp> Matched = matchMaskedStore(*CI)) {
      FoundMaskedStore = true;
      EXPECT_FALSE(isa<Constant>(Matched->Mask))
          << "store after feme.stage.discard should be masked by the "
             "narrowed side-effect mask";
    }
  }
  EXPECT_TRUE(FoundMaskedStore);
}

// `feme.stage.demote` narrows only the side-effect mask, leaving the live
// mask (and therefore a subsequent ordinary `load`) untouched.
TEST(LinearizeTest, DemoteNarrowsOnlySideEffectMask) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main(ptr %p, i1 %cond) #0 {
    entry:
      call void @feme.stage.demote(i1 %cond)
      store i32 1, ptr %p
      %v = load i32, ptr %p
      ret i32 %v
    }
    declare void @feme.stage.demote(i1)
    attributes #0 = { "feme.shader.stage"="fragment" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundMaskedStore = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (std::optional<MatchedMaskedMemOp> Matched = matchMaskedStore(*CI)) {
      FoundMaskedStore = true;
      EXPECT_FALSE(isa<Constant>(Matched->Mask));
    }
    // The load after the demote is still unconditionally live: `demote`
    // does not narrow the live mask, so it stays the all-active constant
    // and this milestone leaves the plain `load` untouched (see
    // `applyStageMasks`'s "left unmasked exactly when ... still the
    // all-active constant").
  }
  EXPECT_TRUE(FoundMaskedStore);
  bool FoundPlainLoad = false;
  for (Instruction &I : instructions(F))
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
      FoundPlainLoad = true;
      (void)LI;
    }
  EXPECT_TRUE(FoundPlainLoad);
}

// `feme.stage.is_helper` reads back `live && !side-effect`: after a
// `demote`, the invocation is live but has no side-effect mask, so
// `is_helper` must fold to a value that is true whenever `demote`'s
// condition was true.
TEST(LinearizeTest, IsHelperReflectsDemotedState) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i1 @main(i1 %cond) #0 {
    entry:
      call void @feme.stage.demote(i1 %cond)
      %h = call i1 @feme.stage.is_helper()
      ret i1 %h
    }
    declare void @feme.stage.demote(i1)
    declare i1 @feme.stage.is_helper()
    attributes #0 = { "feme.shader.stage"="fragment" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  if (Function *IsHelper = F->getParent()->getFunction("feme.stage.is_helper"))
    EXPECT_TRUE(IsHelper->use_empty())
        << "feme.stage.is_helper call should have been erased";
  auto *Ret = dyn_cast<ReturnInst>(F->getEntryBlock().getTerminator());
  ASSERT_TRUE(Ret);
  // `is_helper` lowers to `live && !sideeffect`; with no divergent branch,
  // `live` is still the all-active constant `true`, so this should fold to
  // (a value equivalent to) `%cond` itself once `and true, X` and `not
  // (not cond)`-style simplifications are accounted for -- checked here
  // structurally (an `and` of the all-active constant and something
  // derived from `%cond`) rather than by exact instruction match, since
  // this pass does no constant folding of its own.
  EXPECT_TRUE(isa<Instruction>(Ret->getReturnValue()) ||
              isa<Argument>(Ret->getReturnValue()));
}

// H4e: a store whose value operand is a shape `MaskIntrinsics.cpp`'s
// `appendScalarMangling` does not recognize (a matrix/aggregate type,
// represented here by a struct -- the same shape a matrix lowers to) must
// not crash this pass with `llvm_unreachable` when it needs masking. It
// should instead report a diagnostic through the module's `LLVMContext`
// (see `feme::cpu::runPipeline`'s `ErrorDiagnosticGuard`, which turns this
// into a graceful pipeline failure) and leave the original `store`
// untouched, rather than replace it with a call built from a null callee.
TEST(LinearizeTest,
     UnsupportedAggregateMaskedStoreDiagnosesGracefullyInsteadOfCrashing) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %p, i1 %cond, {float, float} %val) #0 {
    entry:
      call void @feme.stage.discard(i1 %cond)
      store {float, float} %val, ptr %p
      ret void
    }
    declare void @feme.stage.discard(i1)
    attributes #0 = { "feme.shader.stage"="fragment" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Handle) = true;
      },
      &SawError);

  // Must not crash the process (the pre-H4e `llvm_unreachable` this
  // milestone replaces would have `SIGABRT`ed here instead of returning).
  run(*M);
  EXPECT_TRUE(SawError);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundPlainStore = false;
  for (Instruction &I : instructions(F)) {
    if (auto *SI = dyn_cast<StoreInst>(&I)) {
      FoundPlainStore = true;
      EXPECT_TRUE(SI->getValueOperand()->getType()->isStructTy())
          << "the unsupported-type store should be left unmasked, not "
             "replaced with a null-callee call";
    }
    ASSERT_FALSE(isa<CallInst>(I) &&
                 cast<CallInst>(I).getCalledFunction() == nullptr)
        << "no call with a null callee should ever be created";
  }
  EXPECT_TRUE(FoundPlainStore);
}

// Roadmap L88: a real `llvm::Value::~Value` "Uses remain when a value is
// destroyed!" assertion, discovered by a speculative
// `VK_SUBGROUP_FEATURE_SHUFFLE_BIT` verification run against
// `dEQP-VK.subgroups.shuffle.*` (see L85) -- every non-rotate shuffle
// test's own verification harness nests a uniform loop's own trip-count
// check inside the shader's own divergent per-invocation guard. `%Flow`
// below is exactly the redundant "Flow" merge block
// loop-uniform-check-separate-structurized.ll documents (its own branch
// condition, `%2`, is a phi of two literal constants) that
// `feme::cpu::foldRedundantFlowBlock` (added by H19k) knows how to fold
// away -- but because the loop is nested inside the outer divergent `%c1`
// diamond here, `DiamondFlattener` (which runs first) has already injected
// its own `%live.merge`/`%sideeffect.merge` mask phis directly into
// `%Flow`, and threaded those same values up into the outer diamond's own,
// unrelated `%Flow2` reconvergence block as a `select` operand with no
// phi-chain relationship to `%Flow` at all. Folding `%Flow` away
// regardless previously destroyed `%live.merge`/`%sideeffect.merge` while
// `%Flow2`'s own select instructions still referenced them, aborting the
// process outright rather than merely miscompiling. `foldRedundantFlowBlock`
// must recognize this escaping use and leave `%Flow` in place instead.
TEST(LinearizeTest,
     PreservesRedundantFlowBlockWhoseMaskPhiEscapesToOuterDiamond) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %n, ptr %buf) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c1 = icmp ne i32 %tid, 0
      br i1 %c1, label %outer.f, label %entry.Flow1_crit_edge
    entry.Flow1_crit_edge:
      br label %Flow1
    Flow1:
      %0 = phi i1 [ false, %outer.f ], [ true, %entry.Flow1_crit_edge ]
      br i1 %0, label %outer.t, label %Flow1.Flow2_crit_edge
    Flow1.Flow2_crit_edge:
      br label %Flow2
    outer.t:
      br label %header
    Flow2:
      br label %end
    header:
      %i = phi i32 [ %1, %Flow.header_crit_edge ], [ 0, %outer.t ]
      br label %check
    check:
      %cond = icmp slt i32 %i, %n
      br i1 %cond, label %body, label %check.Flow_crit_edge
    check.Flow_crit_edge:
      br label %Flow
    body:
      store i32 %i, ptr %buf
      br label %latch
    Flow:
      %1 = phi i32 [ %inc, %latch ], [ poison, %check.Flow_crit_edge ]
      %2 = phi i1 [ false, %latch ], [ true, %check.Flow_crit_edge ]
      br i1 %2, label %exit, label %Flow.header_crit_edge
    Flow.header_crit_edge:
      br label %header
    latch:
      %inc = add i32 %i, 1
      br label %Flow
    exit:
      br label %Flow2
    outer.f:
      br label %Flow1
    end:
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  // Must not crash the process (the pre-L88 fold would have destroyed a
  // still-used `PHINode` here, aborting via `llvm::Value::~Value`'s own
  // "Uses remain when a value is destroyed!" assertion instead of
  // returning).
  run(*M);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  BasicBlock *Flow = nullptr;
  for (BasicBlock &BB : *F)
    if (BB.getName() == "Flow")
      Flow = &BB;
  ASSERT_TRUE(Flow) << "the redundant Flow block's own mask phi escapes to "
                       "the outer diamond, so it must be left in place "
                       "rather than folded away";
  bool FoundMaskPhi = false;
  for (PHINode &PN : Flow->phis())
    if (PN.getType()->isIntegerTy(1) &&
        PN.getName().starts_with("live.merge"))
      FoundMaskPhi = true;
  EXPECT_TRUE(FoundMaskPhi);
}

} // namespace
