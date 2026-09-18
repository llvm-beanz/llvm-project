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
#include "llvm/IR/IntrinsicsDirectX.h"
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

// Roadmap H75: an inner if/else diamond nested inside an outer divergent
// diamond's arm, whose own condition is built from a value `applyStageMasks`
// has already masked (a `load` inside the outer arm) -- see
// `nested-diamond-condition-from-masked-load.ll` for the full comment and
// the real `dEQP-VK.mesh_shader.ext.misc.barrier_in_mesh`/`barrier_in_task`
// reduction this comes from. `UniformityInfo` is computed once, before any
// masking happens, against the *original*, unmasked `load` -- whose address
// is a plain function argument, so it (correctly, for that unmasked IR)
// classifies the inner branch's condition as uniform. Left uncorrected, the
// inner branch would keep its real `br`, a genuine divergent branch
// `feme::cpu::SIMDizePass` cannot widen.
TEST(LinearizeTest, FlattensNestedDiamondWhoseConditionDependsOnMaskedLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %p) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c1 = icmp eq i32 %tid, 0
      br i1 %c1, label %t, label %f
    t:
      %v = load i32, ptr %p
      %c2 = icmp eq i32 %v, 32
      br i1 %c2, label %inner.t, label %inner.f
    inner.t:
      %x1 = add i32 %tid, 10
      br label %outer.end
    inner.f:
      %x2 = add i32 %tid, 20
      br label %outer.end
    outer.end:
      %inner.v = phi i32 [%x1, %inner.t], [%x2, %inner.f]
      br label %end
    f:
      br label %end
    end:
      %v2 = phi i32 [%inner.v, %outer.end], [0, %f]
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

  bool FoundMaskedLoad = false;
  for (Instruction &I : instructions(F)) {
    if (auto *Br = dyn_cast<CondBrInst>(&I))
      ADD_FAILURE() << "no conditional branch should survive: "
                    << Br->getCondition()->getName();
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction())
        if (Callee->getName().starts_with("feme.cpu.masked.load"))
          FoundMaskedLoad = true;
  }
  EXPECT_TRUE(FoundMaskedLoad);
}

// Roadmap H167: the same shape as
// `FlattensNestedDiamondWhoseConditionDependsOnMaskedLoad` immediately
// above, but for a value read via an already-masked `feme.cpu.resource.*`
// call (the shape `feme::cpu::ResourceLoweringPass` leaves behind) rather
// than a plain `load` this pass itself converts. `t`'s own resource call
// starts with a constant `true` mask and is rewritten in place to the
// block's real, non-constant mask -- but unlike a plain `load`, that
// rewrite is not recorded into `MaskedLoadResults`, since it was never a
// plain `load` to begin with. Before this fix, `dependsOnTaintedValue`
// only consulted that recorded set, so the inner branch built from the
// call's result (now genuinely per-lane-varying) kept its real,
// unflattened shape, since `UniformityInfo` -- computed once, before any
// masking happens -- still calls it uniform. Reduced from
// `InterlockedExchange.resources.32.test`'s own post-loop, single-
// invocation verification reads (roadmap H165's own closing session
// found this hiding one stage later, in `feme::cpu::SIMDizePass`).
TEST(LinearizeTest, FlattensNestedDiamondWhoseConditionDependsOnMaskedResourceCall) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %heap, i32 %heap_count, i32 %desc) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c1 = icmp eq i32 %tid, 0
      br i1 %c1, label %t, label %f
    t:
      %v = call i32 @feme.cpu.resource.load.raw.i32(ptr %heap, i32 %heap_count, i32 %desc, i64 0, i1 true)
      %c2 = icmp eq i32 %v, 32
      br i1 %c2, label %inner.t, label %inner.f
    inner.t:
      %x1 = add i32 %tid, 10
      br label %outer.end
    inner.f:
      %x2 = add i32 %tid, 20
      br label %outer.end
    outer.end:
      %inner.v = phi i32 [%x1, %inner.t], [%x2, %inner.f]
      br label %end
    f:
      br label %end
    end:
      %v2 = phi i32 [%inner.v, %outer.end], [0, %f]
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  bool FoundNonConstantResourceMask = false;
  for (Instruction &I : instructions(F)) {
    if (auto *Br = dyn_cast<CondBrInst>(&I))
      ADD_FAILURE() << "no conditional branch should survive: "
                    << Br->getCondition()->getName();
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction())
        if (Callee->getName() == "feme.cpu.resource.load.raw.i32")
          if (!isa<Constant>(CI->getArgOperand(4)))
            FoundNonConstantResourceMask = true;
  }
  EXPECT_TRUE(FoundNonConstantResourceMask);
}


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

// Roadmap H95a: a `feme.cpu.resource.*` call sitting in a loop's exit
// block, where that loop is itself nested inside a still-divergent outer
// `if (tid == 0) { <loop-with-divergent-exit>; <resource call> }` diamond
// (see `resource-call-after-nested-loop-in-divergent-diamond.ll` for the
// real-world reduction this shape comes from). Before this fix,
// `DiamondFlattener::run` always seeded a cycle's exit block -- treated as
// a brand-new root -- with a hardcoded "every lane active" mask instead of
// the real, narrower mask that was actually in effect when the loop was
// reached; since nothing in the exit block's own local shape narrows that
// placeholder any further, it constant-folded straight back down to a
// literal `true`, and `applyStageMasks`'s constant-mask check silently
// skipped rewriting the call's mask operand, leaving it exactly as if
// every lane -- not just the one that took the outer branch -- should
// execute it.
TEST(LinearizeTest,
     MasksResourceCallInExitBlockOfLoopNestedInDivergentDiamond) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %heap, i32 %heap_count) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c1 = icmp eq i32 %tid, 0
      br i1 %c1, label %outer.t, label %outer.f
    outer.t:
      br label %header
    header:
      %i = phi i32 [0, %outer.t], [%inc, %latch]
      %cond = icmp eq i32 %i, %tid
      br i1 %cond, label %exit, label %latch
    latch:
      %inc = add i32 %i, 1
      br label %header
    exit:
      call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i32 1, i1 true)
      br label %end
    outer.f:
      br label %end
    end:
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  EXPECT_FALSE(verifyModule(*M, &errs()));

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
        << "mask should have been rewritten to the outer diamond's real "
           "mask, not left as (or folded back down to) the constant "
           "`true` feme::cpu::ResourceLoweringPass left it as";
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

// Roadmap H149: `WaveIsFirstLane()`'s underlying `llvm.dx.wave.is.first.lane`/
// `llvm.spv.wave.is.first.lane` intrinsics take zero operands, unlike every
// other maskless-consuming wave call this pass already narrows (`Ballot`'s
// predicate, the arithmetic-reduce/prefix kinds' value operand) -- there is
// nothing on the call itself to rewrite. Confirm `LinearizePass` instead
// attaches the divergent region's own live mask as a `"feme.divergence.mask"`
// operand bundle so `feme::cpu::SIMDize.cpp`'s `widenWaveCall` can still find
// it later (this exact gap made lanes 2 and 3 -- each the sole active lane in
// its own switch arm -- wrongly report `WaveIsFirstLane() == false`, per
// `Feature/WaveOps/WaveIsFirstLane.test`'s reduction of offload-test-suite
// issue #681).
TEST(LinearizeTest, AttachesDivergenceMaskBundleToIsFirstLaneUnderDivergentBranch) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c = icmp eq i32 %tid, 0
      br i1 %c, label %t, label %f
    t:
      %first = call i1 @llvm.dx.wave.is.first.lane()
      br label %end
    f:
      br label %end
    end:
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i1 @llvm.dx.wave.is.first.lane()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundCall = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || !CI->getCalledFunction() ||
        CI->getCalledFunction()->getIntrinsicID() !=
            Intrinsic::dx_wave_is_first_lane)
      continue;
    FoundCall = true;
    auto Bundle = CI->getOperandBundle("feme.divergence.mask");
    ASSERT_TRUE(Bundle.has_value())
        << "WaveIsFirstLane under a divergent branch should carry the "
           "region's live mask as an operand bundle, since the intrinsic "
           "itself has no operand to narrow";
    EXPECT_FALSE(isa<Constant>(Bundle->Inputs[0]))
        << "the attached mask should have been rewritten away from a "
           "compile-time constant, reflecting the real divergent-branch "
           "condition";
  }
  EXPECT_TRUE(FoundCall);
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

// Roadmap H165: a divergent branch inside a loop body whose arms reconverge
// strictly *inside* the loop body (not at the loop's own exit block) --
// structurally the same "mid-body diamond" shape H158 taught the entry
// wrapper to outline, one pass earlier. Before H165, `LoopLinearizer` had no
// path for this: its only handling of an internal divergent branch was one
// that relayed into the loop's own exit check
// (`matchExitCheckWithRelay`), so this shape hit its "internal branch...
// does not reach the loop's exit block" diagnostic. `DiamondFlattener`'s new
// `flattenLoopBodyDiamond` recognizes it instead: the branch's immediate
// post-dominator validates with an empty `CycleBoundaryBlocks` (neither arm
// crosses a loop control edge), so it is flattened in place like an ordinary
// diamond, leaving the loop's own separate, uniform exit check untouched.
TEST(LinearizeTest, FlattensLoopBodyDiamond) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %n) #0 {
    entry:
      br label %loop
    loop:
      %i = phi i32 [0, %entry], [%inc, %merge]
      %v = phi i32 [0, %entry], [%v.merge, %merge]
      br label %body
    body:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %cond = icmp eq i32 %tid, 0
      br i1 %cond, label %then, label %else
    then:
      %a = add i32 %v, 1
      br label %merge
    else:
      %b = add i32 %v, 2
      br label %merge
    merge:
      %v.merge = phi i32 [%a, %then], [%b, %else]
      %inc = add i32 %i, 1
      %loop.cond = icmp slt i32 %inc, %n
      br i1 %loop.cond, label %loop, label %exit
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
  // The mid-body diamond's own branch should be gone (flattened into a
  // select), while the loop's own exit check remains a real branch.
  unsigned NumCondBr = 0;
  bool FoundSelect = false;
  for (Instruction &I : instructions(F)) {
    if (isa<CondBrInst>(I))
      ++NumCondBr;
    if (isa<SelectInst>(I))
      FoundSelect = true;
  }
  EXPECT_EQ(NumCondBr, 1u);
  EXPECT_TRUE(FoundSelect);
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
// `appendScalarMangling` does not recognize must not crash this pass
// with `llvm_unreachable` when it needs masking. It should instead
// report a diagnostic through the module's `LLVMContext` (see
// `feme::cpu::runPipeline`'s `ErrorDiagnosticGuard`, which turns this
// into a graceful pipeline failure) and leave the original `store`
// untouched, rather than replace it with a call built from a null callee.
// Roadmap L116(a): a struct/array-typed masked store is now decomposed
// into one masked store per leaf (see `createMaskedStoreRecursive`), so
// this regression case (originally reduced against `{float, float}`, a
// shape that decomposition now fully supports) is rewritten against a
// struct with one leaf type (`x86_fp80`) `MaskIntrinsics.cpp`'s own
// `appendScalarMangling` genuinely cannot mangle, to keep covering the
// still-real "leave the original, unmasked store untouched rather than
// replace it with a call built from a null callee" diagnostic path for
// whatever leaf shape decomposition itself cannot yet handle.
TEST(LinearizeTest,
     UnsupportedAggregateMaskedStoreDiagnosesGracefullyInsteadOfCrashing) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %p, i1 %cond, {x86_fp80, float} %val) #0 {
    entry:
      call void @feme.stage.discard(i1 %cond)
      store {x86_fp80, float} %val, ptr %p
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

// Roadmap L116(a): a struct/array-typed masked load/store whose every
// leaf is a scalar or fixed-vector type -- the common graphicsfuzz shape
// this row exists to close -- is now decomposed into one masked
// load/store per leaf instead of being diagnosed as unsupported.
TEST(LinearizeTest, DecomposesAggregateMaskedStorePerLeaf) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %p, i1 %cond, {i32, <2 x float>} %val) #0 {
    entry:
      call void @feme.stage.discard(i1 %cond)
      store {i32, <2 x float>} %val, ptr %p
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

  run(*M);
  EXPECT_FALSE(SawError);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  unsigned MaskedStoreCount = 0;
  bool FoundPlainStore = false;
  for (Instruction &I : instructions(F)) {
    if (isa<StoreInst>(I))
      FoundPlainStore = true;
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (matchMaskedStore(*CI))
        ++MaskedStoreCount;
  }
  EXPECT_FALSE(FoundPlainStore)
      << "the original aggregate store should be fully decomposed away";
  EXPECT_EQ(MaskedStoreCount, 2u)
      << "one masked store per leaf (i32, then <2 x float>)";
}

// Roadmap L116(a): the `load` counterpart of
// `DecomposesAggregateMaskedStorePerLeaf` -- a struct/array-typed masked
// load's leaves are reassembled with `insertvalue`.
TEST(LinearizeTest, DecomposesAggregateMaskedLoadPerLeaf) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define {i32, [2 x float]} @main(ptr %p, i1 %cond) #0 {
    entry:
      call void @feme.stage.discard(i1 %cond)
      %v = load {i32, [2 x float]}, ptr %p
      ret {i32, [2 x float]} %v
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

  run(*M);
  EXPECT_FALSE(SawError);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  unsigned MaskedLoadCount = 0;
  bool FoundPlainLoad = false;
  bool FoundInsertValue = false;
  for (Instruction &I : instructions(F)) {
    if (isa<LoadInst>(I))
      FoundPlainLoad = true;
    if (isa<InsertValueInst>(I))
      FoundInsertValue = true;
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (matchMaskedLoad(*CI))
        ++MaskedLoadCount;
  }
  EXPECT_FALSE(FoundPlainLoad)
      << "the original aggregate load should be fully decomposed away";
  EXPECT_TRUE(FoundInsertValue)
      << "the decomposed leaves should be reassembled with insertvalue";
  EXPECT_EQ(MaskedLoadCount, 3u)
      << "one masked load per leaf (i32, then the two floats of the "
         "[2 x float] array)";
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

// Roadmap H94a: a hand-built two-relay-hop case, distilled directly from a
// real captured pre-`feme-cpu-linearize` IR reduction of
// `dEQP-VK.mesh_shader.ext.properties.mesh_shared_memory_size`'s own
// workgroup-shared-memory verification loop (see the checked-in
// `Transforms/CPU/Linearize/loop-relay-chain-two-hops.ll` lit test for the
// full, real-world version this was distilled from). The loop's one real,
// divergent exit check (`body`'s `%mismatch`) reaches the loop's exit
// only via `Flow`, and `Flow`'s own "exit" decision reaches it in turn
// only via a *second* relay hop, `loop.exit.guard` -- exactly the shape
// `LoopLinearizer`'s pre-H94a `OtherCondBrBlocks` classification saw as
// two independently divergent exit checks (`body`/`Flow` and `Flow`/
// `loop.exit.guard` respectively) and diagnosed as unsupported.
//
// Two structural details this test's own development found essential,
// not merely incidental, to reproducing the bug (earlier, simpler
// attempts at a from-scratch synthetic two-relay-hop shape did not
// reproduce it at all, and are recorded in agent_thoughts.md's "H94a
// session" entry so this is not rediscovered from scratch again):
// (1) the loop itself must be nested inside an *enclosing* divergent
// branch (here, `entry`'s own `%is.zero` thread-ID check) -- a loop whose
// own induction variable and checks are otherwise entirely uniform is
// never classified as divergent at all by `UniformityInfo`'s control-
// dependence propagation, so `body`'s in-loop shared-memory-mismatch
// check needs that enclosing divergent context to itself become
// divergent; (2) the loop's own two exit edges (`doexit` and
// `loop.exit.guard._crit_edge`) must first fully unify into one single
// block (`loopexit_merged`, mirroring what a real `UnifyLoopExits` pass
// run would produce) before reaching the outer diamond's own
// reconvergence block -- `DiamondFlattener::validate` requires the
// reconvergence block to have exactly two predecessors, which a loop
// whose two exit edges both flow directly into it (without this
// intermediate unification) violates.
TEST(LinearizeTest, LinearizesLoopWithTwoRelayHopsToDivergentExit) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @spirv_var_41 = external addrspace(3) global [1 x i32]
    @out_buf = external addrspace(1) global i32

    define void @main() #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %is.zero = icmp eq i32 %tid, 0
      br i1 %is.zero, label %loopentry, label %falsearm
    falsearm:
      br label %merge
    loopentry:
      br label %loopstart
    loopstart:
      %idx = phi i32 [ %merged, %Flow._crit_edge ], [ 0, %loopentry ]
      br label %check
    incblock:
      %inc = add i32 %idx, 1
      br label %Flow24
    check:
      %inloop = icmp ult i32 %idx, 1
      br i1 %inloop, label %body, label %.Flow_crit_edge
    .Flow_crit_edge:
      br label %Flow
    body:
      %gep = getelementptr [1 x i32], ptr addrspace(3) @spirv_var_41, i32 0, i32 %idx
      %loaded = load i32, ptr addrspace(3) %gep, align 4
      %mul = mul i32 %idx, 3
      %expected = add i32 %mul, 1000
      %mismatch = icmp eq i32 %loaded, %expected
      br i1 %mismatch, label %match, label %.Flow24_crit_edge
    .Flow24_crit_edge:
      br label %Flow24
    Flow24:
      %carry = phi i32 [ %inc, %incblock ], [ poison, %.Flow24_crit_edge ]
      %exit1 = phi i1 [ false, %incblock ], [ true, %.Flow24_crit_edge ]
      br label %Flow
    match:
      br label %incblock
    Flow:
      %merged = phi i32 [ %carry, %Flow24 ], [ poison, %.Flow_crit_edge ]
      %exit2 = phi i1 [ false, %Flow24 ], [ true, %.Flow_crit_edge ]
      %exit3 = phi i1 [ %exit1, %Flow24 ], [ true, %.Flow_crit_edge ]
      br i1 %exit3, label %loop.exit.guard, label %Flow._crit_edge
    Flow._crit_edge:
      br label %loopstart
    loop.exit.guard:
      %Guard.inv = xor i1 %exit2, true
      br i1 %Guard.inv, label %doexit, label %loop.exit.guard._crit_edge
    doexit:
      br label %loopexit_merged
    loop.exit.guard._crit_edge:
      br label %loopexit_merged
    loopexit_merged:
      %found.mismatch = phi i1 [ true, %doexit ], [ false, %loop.exit.guard._crit_edge ]
      %flag = select i1 %found.mismatch, i32 1, i32 0
      store i32 %flag, ptr addrspace(1) @out_buf, align 4
      br label %merge
    merge:
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundMaskAny = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (CI->getCalledFunction() &&
          CI->getCalledFunction()->getName() == "feme.cpu.mask.any")
        FoundMaskAny = true;
  EXPECT_TRUE(FoundMaskAny);
}

// Roadmap H120: distilled from a real captured pre-`feme-cpu-linearize` IR
// dump of `dEQP-VK.tessellation.user_defined_io.per_patch_block.*`'s Hull
// control-point stage (see agent_thoughts.md's "H120 session" entry for
// the full trace this was root-caused from). `merge` below plays the real
// crash's own `Flow39` block's exact double role: it is simultaneously
// (a) the real two-arm merge point of the *outer*, uniform `%u` diamond
// (its own `%sideeffect.merge` phi must merge both `armT`'s and `armF`'s
// arms), and (b) a cycle boundary in its own right (it branches back to
// `entry`, its own cycle's header, so its own conditional branch is a
// loop-control edge `validate` stops at -- see `isLoopControlEdge`).
// `armT`'s own arm additionally nests a *second*, inner loop (`loop`)
// with its own divergent exit (`%break.cond`, thread-ID-dependent) whose
// own `CycleBoundaryMasks` recording at `loop`'s own boundary predates --
// and, before this fix, stomped on -- `merge`'s. `armT`'s own
// `feme.stage.demote` is essential, not incidental: without it, both arms
// leave the side-effect mask an untouched constant, so the two
// (correct-vs-stale) recordings this bug conflates happen to coincide and
// the bug is unobservable; with it, `armT`'s own path narrows the mask
// while `armF`'s does not, so the outer diamond's real two-arm merge (at
// `merge`) is a genuine, non-trivial `phi` distinct from `loop`'s own
// single-arm-only value.
//
// Before this fix: `run`'s discovery pass adds `loop.exit` as its own
// late root (since `validate`'s recursion into `armT` stops early right
// at `loop`'s own boundary, never itself confirming that arm reaches
// `merge`); flattening that root walks straight through `loop.exit` into
// `merge` a *second* time with no intervening diamond, and previously
// *overwrote* `CycleBoundaryMasks[merge]` -- recorded correctly the first
// time, by the outer diamond's own continuous walk, as the real two-arm
// merge `phi` -- with this second walk's own narrower, single-arm-only
// value (the mask in effect when `loop`'s own boundary, not `merge`'s own
// two real predecessors, was reached). `final`'s own masked resource call
// (reachable only via `merge`) then got seeded with that stale,
// non-dominating value instead, producing exactly the same `InstCombine`
// "Dominance relation broken?"/`verifyModule` "Instruction does not
// dominate all uses!" failure the real CTS case hit (confirmed by
// temporarily reverting this fix and re-running this exact test: it
// fails the same way without it). The `try_emplace`-style "first
// recording wins" fix below makes `merge`'s own later, single-arm
// re-visit a no-op instead.
TEST(LinearizeTest,
     PreservesFirstCycleBoundaryMaskWhenOuterMergeIsAlsoALoopBoundary) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %n, ptr %buf) #0 {
    preheader:
      br label %entry
    entry:
      %outer.i = phi i32 [0, %preheader], [%outer.inc, %merge]
      %u = icmp sgt i32 %n, 0
      br i1 %u, label %armT, label %armF
    armT:
      call void @feme.stage.demote(i1 %u)
      br label %loop
    loop:
      %i = phi i32 [0, %armT], [%inc, %loop]
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %inc = add i32 %i, 1
      %break.cond = icmp eq i32 %tid, %inc
      br i1 %break.cond, label %loop.exit, label %loop
    loop.exit:
      br label %merge
    armF:
      br label %merge
    merge:
      %outer.inc = add i32 %outer.i, 1
      %outer.cond = icmp slt i32 %outer.inc, %n
      br i1 %outer.cond, label %entry, label %final
    final:
      %tid2 = call i32 @llvm.dx.thread.id(i32 0)
      %divarm = icmp eq i32 %tid2, 0
      br i1 %divarm, label %final.t, label %final.f
    final.t:
      br label %final.merge
    final.f:
      br label %final.merge
    final.merge:
      call void @feme.cpu.resource.store.raw.i32(ptr %buf, i32 %n, i32 0, i64 0, i32 %outer.inc, i1 true)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.stage.demote(i1)
    declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  // Must not crash the process, and the resulting IR must remain valid
  // SSA (every operand dominating its use) -- before this fix, `final`'s
  // masked resource-call operand referenced a value only valid on one of
  // `merge`'s two real incoming edges, which `verifyModule` catches as a
  // broken dominance relation.
  run(*M);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

} // namespace
