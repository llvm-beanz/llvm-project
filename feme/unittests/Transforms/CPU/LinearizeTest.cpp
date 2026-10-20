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
#include "llvm/IR/Constants.h"
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

TEST(LinearizeTest,
     LeavesQueryLevelsCallImageIndexUntouchedUnderDivergentBranch) {
  // Roadmap L267: `feme.cpu.image.querylevels.i32`/
  // `feme.cpu.image.querysamples.i32` (`createQueryLevels`/
  // `createQuerySamples`) are the sole `feme.cpu.image.*` kinds with no
  // trailing mask operand at all (see `MatchedImageCall::Mask`'s own
  // doc) -- before this fix, `applyStageMasks` unconditionally rewrote a
  // matched image call's own last operand (`Call->arg_size() - 1`) into
  // the divergent region's live mask, which for these two kinds is their
  // own last *real* operand (`%image_index` here), not a mask to narrow.
  // That silently replaced a real per-invocation resource-descriptor
  // index with an `i1` live-mask value, corrupting the call outright (a
  // real `dEQP-VK.glsl.texture_functions.query.texturequerylevels.*`
  // repro). `%image_index` must survive this pass completely unchanged.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main(ptr %image_heap, i32 %image_heap_count, i32 %image_index) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c = icmp eq i32 %tid, 0
      br i1 %c, label %t, label %f
    t:
      %levels = call i32 @feme.cpu.image.querylevels.i32(
          ptr %image_heap, i32 %image_heap_count, i32 %image_index)
      br label %end
    f:
      br label %end
    end:
      %r = phi i32 [ %levels, %t ], [ 0, %f ]
      ret i32 %r
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i32 @feme.cpu.image.querylevels.i32(ptr, i32, i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundQueryLevelsCall = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || !CI->getCalledFunction() ||
        CI->getCalledFunction()->getName() != "feme.cpu.image.querylevels.i32")
      continue;
    FoundQueryLevelsCall = true;
    ASSERT_EQ(CI->arg_size(), 3u);
    EXPECT_EQ(CI->getArgOperand(2), F->getArg(2))
        << "the call's own real `%image_index` operand must not be "
           "overwritten with a live mask";
  }
  EXPECT_TRUE(FoundQueryLevelsCall);
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

// Roadmap H125(a): `%i`, read after the loop exits (via `exit`'s own
// `%i.result` merge), also feeds a second, genuinely uniform in-loop exit
// check (`%trip`, comparing `%i.next` against the compile-time constant
// `10`) that reaches the cycle's single `exit` block directly, alongside
// `header`'s own divergent exit check -- reduced from Clang's own
// `Basic/Mandelbrot.test`, whose `for (; Iteration < MaxIteration;
// ++Iteration) { if (Diverged) break; }` loop has exactly this shape.
// Before this fix, `freezeLoopCarriedValues` froze `%i` itself in place
// (needed for its own `exit`-block use), which also retroactively made
// `%trip`'s condition divergent per `UniformityInfo` (a `phi` with any
// divergent incoming value is divergent for all its uses), corrupting
// `body`'s own, otherwise untouched branch and eventually tripping
// `feme-cpu-simdize`'s "has a divergent branch" diagnostic. `%i`'s own
// `header` phi must stay completely untouched here -- still backed
// directly by `%i.next` -- with a new, separate shadow `phi` instead
// carrying the frozen value `exit`'s own merge actually needs.
TEST(LinearizeTest, FreezesSharedLoopCarriedValueViaShadowPhiWhenItFeedsUniformCheck) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %out) #0 {
    entry:
      br label %header
    header:
      %i = phi i32 [0, %entry], [%i.next, %latch]
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %break.cond = icmp eq i32 %tid, %i
      br i1 %break.cond, label %exit, label %body
    body:
      %i.next = add i32 %i, 1
      %trip = icmp slt i32 %i.next, 10
      br i1 %trip, label %latch, label %exit
    latch:
      br label %header
    exit:
      %i.result = phi i32 [ %i, %header ], [ %i.next, %body ]
      store i32 %i.result, ptr %out
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
  BasicBlock *Body = nullptr;
  for (BasicBlock &BB : *F)
    if (BB.getName() == "body")
      Body = &BB;
  ASSERT_TRUE(Body);

  // `body`'s own `%trip` branch must still be a plain, untouched CondBr:
  // the fix must not have widened or masked it in any way.
  auto *Term = dyn_cast<CondBrInst>(Body->getTerminator());
  ASSERT_TRUE(Term);
  auto *Trip = dyn_cast<CmpInst>(Term->getCondition());
  ASSERT_TRUE(Trip);
  EXPECT_EQ(Trip->getName(), "trip");
  auto *TripNext = dyn_cast<BinaryOperator>(Trip->getOperand(0));
  ASSERT_TRUE(TripNext);
  EXPECT_EQ(TripNext->getName(), "i.next");

  // `header`'s own `%i` phi must still be backed directly by `%i.next` on
  // the latch edge -- not by any `select`/frozen value.
  BasicBlock *Header = F->getEntryBlock().getSingleSuccessor();
  PHINode *IPhi = nullptr;
  for (Instruction &I : *Header)
    if (auto *PN = dyn_cast<PHINode>(&I))
      if (PN->getName() == "i")
        IPhi = PN;
  ASSERT_TRUE(IPhi);
  for (unsigned Idx = 0, End = IPhi->getNumIncomingValues(); Idx != End;
       ++Idx)
    if (IPhi->getIncomingBlock(Idx)->getName() == "latch")
      EXPECT_EQ(IPhi->getIncomingValue(Idx), TripNext);

  // A separate shadow phi must exist, feeding `exit`'s own merge.
  bool FoundShadowPhi = false;
  for (Instruction &I : instructions(F))
    if (auto *PN = dyn_cast<PHINode>(&I))
      if (PN->getName().contains("frozen.shadow"))
        FoundShadowPhi = true;
  EXPECT_TRUE(FoundShadowPhi);
}

// Roadmap ENV3(c)(9): like
// `FreezesSharedLoopCarriedValueViaShadowPhiWhenItFeedsUniformCheck`
// above, but checks a deeper property of the very same reproducer: the
// shadow phi's own Header-resident identity is appended to, and
// therefore later re-visited by, the same `Header->phis()` scan that
// created it -- that second visit's own self-select
// (`%i.frozen.shadow.frozen`) is what actually finishes computing the
// shadow's correct, cumulative "frozen at first divergence" value for a
// masked SIMD group whose lanes share a late, common final active
// iteration. Before this fix, `exit`'s own `i.result` merge read the raw
// `header`-resident shadow phi directly on the `latch` edge -- valid SSA
// (`header` dominates `exit`), but one iteration stale: the raw phi's
// value, read from a dominated block, is only ever "as of entering the
// current iteration," not the freshly-recomputed freeze this exact,
// possibly-final iteration's own divergence may have just produced.
// Reduced from Clang's own `Basic/Mandelbrot.test`, whose `Iteration`
// counter is exactly this shape (read after the loop to compute the
// palette color index).
TEST(LinearizeTest,
     FreezesLoopCarriedValueExitUseWithFullyResolvedShadowNotStaleOne) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %out) #0 {
    entry:
      br label %header
    header:
      %i = phi i32 [0, %entry], [%i.next, %latch]
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %break.cond = icmp eq i32 %tid, %i
      br i1 %break.cond, label %exit, label %body
    body:
      %i.next = add i32 %i, 1
      %trip = icmp slt i32 %i.next, 10
      br i1 %trip, label %latch, label %exit
    latch:
      br label %header
    exit:
      %i.result = phi i32 [ %i, %header ], [ %i.next, %body ]
      store i32 %i.result, ptr %out
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
  BasicBlock *Exit = nullptr, *Latch = nullptr;
  for (BasicBlock &BB : *F) {
    if (BB.getName() == "exit")
      Exit = &BB;
    if (BB.getName() == "latch")
      Latch = &BB;
  }
  ASSERT_TRUE(Exit);
  ASSERT_TRUE(Latch);

  PHINode *IResult = nullptr;
  for (Instruction &I : *Exit)
    if (auto *PN = dyn_cast<PHINode>(&I))
      if (PN->getName() == "i.result")
        IResult = PN;
  ASSERT_TRUE(IResult);

  Value *LatchValue = IResult->getIncomingValueForBlock(Latch);
  auto *LatchValueI = dyn_cast<Instruction>(LatchValue);
  ASSERT_TRUE(LatchValueI);
  // Must not be the raw, Header-resident shadow phi itself (the stale
  // value this fix removes); must be defined in `latch` (the
  // self-reprocessed select).
  EXPECT_FALSE(isa<PHINode>(LatchValueI));
  EXPECT_EQ(LatchValueI->getParent(), Latch);
  EXPECT_TRUE(LatchValueI->getName().contains("frozen.shadow.frozen"));
}

// Roadmap L420: like
// `FreezesSharedLoopCarriedValueViaShadowPhiWhenItFeedsUniformCheck`
// above, but the value read after the loop (`%diverged.result`) is *not*
// a reference to one of `header`'s own loop-carried phis -- it is a
// second `exit`-block merge whose two incoming values are themselves
// simple, fixed per-edge constants (`true` for "exited via the
// divergent `break`", `false` for "exited via the loop's own separate,
// genuinely uniform trip-count check"). Reduced from `Basic/
// Mandelbrot.test`'s own per-pixel escape-iteration loop, whose post-loop
// `if (Diverged)` color selection reads exactly this shape of merge.
// Before this fix, this merge's `header`-path value (the literal `true`)
// was carried onto `latch`'s new edge completely unfrozen (`V` is a
// `ConstantInt`, not a `header` phi, so `freezeLoopCarriedValues`'s own
// use-rewriting scan never touches it): every lane that took the
// divergent exit was made to read whichever constant the group's
// *final* round happened to carry, not its own -- confirmed to
// eventually SIMDize into a group-wide broadcast scalar instead of a
// genuine per-lane vector.
TEST(LinearizeTest, FreezesNonPhiExitRelayValueViaDedicatedShadowPhi) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %out, ptr %out2) #0 {
    entry:
      br label %header
    header:
      %i = phi i32 [0, %entry], [%i.next, %latch]
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %break.cond = icmp eq i32 %tid, %i
      br i1 %break.cond, label %exit, label %body
    body:
      %i.next = add i32 %i, 1
      %trip = icmp slt i32 %i.next, 10
      br i1 %trip, label %latch, label %exit
    latch:
      br label %header
    exit:
      %i.result = phi i32 [ %i, %header ], [ %i.next, %body ]
      %diverged.result = phi i1 [ true, %header ], [ false, %body ]
      store i32 %i.result, ptr %out
      %diverged.ext = zext i1 %diverged.result to i32
      store i32 %diverged.ext, ptr %out2
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

  // `exit`'s own `%diverged.result` merge must no longer read the raw
  // literal `true` directly off `latch`'s edge -- it must instead read a
  // dedicated shadow phi this fix introduces.
  PHINode *DivergedResult = nullptr;
  for (Instruction &I : instructions(F))
    if (auto *PN = dyn_cast<PHINode>(&I))
      if (PN->getName() == "diverged.result")
        DivergedResult = PN;
  ASSERT_TRUE(DivergedResult);
  bool FoundRawConstantOnLatchEdge = false;
  bool FoundShadowOnLatchEdge = false;
  for (unsigned Idx = 0, End = DivergedResult->getNumIncomingValues();
       Idx != End; ++Idx) {
    if (DivergedResult->getIncomingBlock(Idx)->getName() != "latch")
      continue;
    Value *V = DivergedResult->getIncomingValue(Idx);
    if (isa<ConstantInt>(V))
      FoundRawConstantOnLatchEdge = true;
    if (V->getName().contains("exit.relay.shadow"))
      FoundShadowOnLatchEdge = true;
  }
  EXPECT_FALSE(FoundRawConstantOnLatchEdge);
  EXPECT_TRUE(FoundShadowOnLatchEdge);

  // The shadow phi itself must live in `header`, seeded `poison` on
  // entry (never observably read by a lane that has not yet taken the
  // divergent exit) and updated via a `select` keyed on a per-iteration
  // "did this lane just exit via header's own check" mask.
  bool FoundShadowPhiInHeader = false;
  for (Instruction &I : instructions(F))
    if (auto *PN = dyn_cast<PHINode>(&I))
      if (PN->getName().contains("exit.relay.shadow") &&
          PN->getParent()->getName() == "header")
        FoundShadowPhiInHeader = true;
  EXPECT_TRUE(FoundShadowPhiInHeader);
}

// Roadmap L466: like `FreezesNonPhiExitRelayValueViaDedicatedShadowPhi`
// above, but the loop's own separate, genuinely uniform exit check is
// `latch`'s *own* terminator (a real, direct edge to `exit`) rather than
// some other, non-`latch` block's. Reduced from Clang's own
// `Basic/Mandelbrot.test`, whose
// `for (; Iteration < MaxIteration; ++Iteration) { if (Diverged) break; }`
// loop has exactly this shape once `StructurizeCFG` relays both exits
// through the loop's own latch block.
//
// Before this fix, `latch` already had its *own* pre-existing incoming
// value on `exit`'s merge phi (the literal `false`), so the restore loop
// that re-attaches `header`'s own frozen relay value onto `latch`'s edge
// -- guarded by "does `latch` not yet have an entry on this phi" --
// skipped this case entirely, silently discarding the frozen value and
// leaving every lane to read the stale, scalar-only constant
// unconditionally.
TEST(LinearizeTest, BlendsExitRelayValueWithPreexistingUniformLatchExit) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %out) #0 {
    entry:
      br label %header
    header:
      %i = phi i32 [0, %entry], [%i.next, %latch]
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %break.cond = icmp eq i32 %tid, %i
      br i1 %break.cond, label %exit, label %body
    body:
      br label %latch
    latch:
      %i.next = add i32 %i, 1
      %trip = icmp slt i32 %i.next, 10
      br i1 %trip, label %header, label %exit
    exit:
      %result = phi i1 [ true, %header ], [ false, %latch ]
      store i1 %result, ptr %out
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

  // `exit`'s own `%result` merge must no longer read the raw literal
  // `false` directly off `latch`'s edge -- it must instead read a
  // `select` that blends in the frozen relay value for any lane that
  // already diverged via `header`'s own check.
  PHINode *Result = nullptr;
  for (Instruction &I : instructions(F))
    if (auto *PN = dyn_cast<PHINode>(&I))
      if (PN->getName() == "result")
        Result = PN;
  ASSERT_TRUE(Result);
  bool FoundRawConstantOnLatchEdge = false;
  bool FoundBlendSelectOnLatchEdge = false;
  for (unsigned Idx = 0, End = Result->getNumIncomingValues(); Idx != End;
       ++Idx) {
    if (Result->getIncomingBlock(Idx)->getName() != "latch")
      continue;
    Value *V = Result->getIncomingValue(Idx);
    if (isa<ConstantInt>(V))
      FoundRawConstantOnLatchEdge = true;
    if (auto *SI = dyn_cast<SelectInst>(V))
      if (SI->getName().contains("exit.relay.latch.blend"))
        FoundBlendSelectOnLatchEdge = true;
  }
  EXPECT_FALSE(FoundRawConstantOnLatchEdge);
  EXPECT_TRUE(FoundBlendSelectOnLatchEdge);

  // The blend's own "already diverged" branch must read the frozen
  // shadow phi (not some other, un-frozen value), and its "still active"
  // branch must preserve the edge's own original constant.
  bool FoundShadowOperand = false;
  bool FoundOriginalConstantOperand = false;
  for (Instruction &I : instructions(F))
    if (auto *SI = dyn_cast<SelectInst>(&I))
      if (SI->getName().contains("exit.relay.latch.blend")) {
        if (SI->getTrueValue()->getName().contains("exit.relay.shadow"))
          FoundShadowOperand = true;
        if (auto *CI = dyn_cast<ConstantInt>(SI->getFalseValue()))
          FoundOriginalConstantOperand = CI->isZero();
      }
  EXPECT_TRUE(FoundShadowOperand);
  EXPECT_TRUE(FoundOriginalConstantOperand);
}

// Roadmap L466: like
// `BlendsExitRelayValueWithPreexistingUniformLatchExit` above, but the
// loop's own separate, genuinely uniform exit check sits in some *other*
// block strictly between `header`'s own divergent exit check and
// `latch` (inside `collectUniformPassThroughRegion`'s own uniform
// pass-through region, which tolerates a walk ending at `exit` directly,
// bypassing `latch` entirely) rather than being `latch`'s own
// terminator. Reduced from Clang's own `Basic/Mandelbrot.test`, whose
// `MaxIteration` hard iteration-count cap check is `StructurizeCFG`-
// relayed through exactly such a block, distinct from that same loop's
// own `latch`.
//
// `header`'s own `Frozen` relay value (computed at `latch`) does *not*
// dominate `body`'s own direct edge to `exit` here, since `body` is
// reached without ever passing through `latch` at all -- the fix must
// build an equivalent blend from `header`-resident values instead.
TEST(LinearizeTest, BlendsExitRelayValueWithPreexistingUniformBypassExit) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %out) #0 {
    entry:
      br label %header
    header:
      %i = phi i32 [0, %entry], [%i.next, %latch]
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %break.cond = icmp eq i32 %tid, %i
      br i1 %break.cond, label %exit, label %body
    body:
      %i.next = add i32 %i, 1
      %trip = icmp slt i32 %i.next, 10
      br i1 %trip, label %cont, label %exit
    cont:
      br label %latch
    latch:
      br label %header
    exit:
      %result = phi i1 [ true, %header ], [ false, %body ]
      store i1 %result, ptr %out
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

  // `exit`'s own `%result` merge must no longer read the raw literal
  // `false` directly off `body`'s edge -- it must instead read a
  // `select` that blends in the frozen relay value for any lane that
  // already diverged via `header`'s own check.
  PHINode *Result = nullptr;
  for (Instruction &I : instructions(F))
    if (auto *PN = dyn_cast<PHINode>(&I))
      if (PN->getName() == "result")
        Result = PN;
  ASSERT_TRUE(Result);
  bool FoundRawConstantOnBodyEdge = false;
  bool FoundBlendSelectOnBodyEdge = false;
  for (unsigned Idx = 0, End = Result->getNumIncomingValues(); Idx != End;
       ++Idx) {
    if (Result->getIncomingBlock(Idx)->getName() != "body")
      continue;
    Value *V = Result->getIncomingValue(Idx);
    if (isa<ConstantInt>(V))
      FoundRawConstantOnBodyEdge = true;
    if (auto *SI = dyn_cast<SelectInst>(V))
      if (SI->getName().contains("exit.relay.bypass.blend"))
        FoundBlendSelectOnBodyEdge = true;
  }
  EXPECT_FALSE(FoundRawConstantOnBodyEdge);
  EXPECT_TRUE(FoundBlendSelectOnBodyEdge);
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

// Roadmap K6s: a uniform-trip-count loop (its own header/latch exit check
// is entirely uniform, exactly the shape `LeavesUniformLoopUnchanged`
// above is correctly left alone) nested inside a *divergent* `if`, whose
// body's only mask-affecting operation is a plain `feme.cpu.resource.*`
// call -- no `feme.stage.discard`/`.demote`/other `StageOpKind` op
// anywhere. `LoopLinearizer::linearizeCycle`'s `CycleHasMaskOps` scan
// previously only recognized `StageOpKind` ops as a reason to invoke the
// entry-mask-threading machinery (`makeActivePNPair`/`applyStageMasks`/
// `DiamondFlattener::flattenLoopBodyRegion`) for an otherwise-uniform
// loop reached through a divergent diamond arm, so this exact shape fell
// through to "no divergence, no mask ops: a real uniform loop, left
// alone" and the resource call's own mask operand was left at its
// placeholder constant `true` forever -- letting the loop's body execute
// unconditionally for every invocation regardless of the enclosing `if`,
// reproducing `dEQP-VK.spirv_assembly.instruction.graphics.
// variable_pointers.graphics.writes_single_buffer_frag`'s uniformly
// wrong buffer readback (every invocation, not just the selected one,
// wrote the loop's 100 iterations' worth of stores).
TEST(LinearizeTest, MasksResourceCallInUniformLoopNestedInDivergentDiamond) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %n, ptr %buf, i32 %desc) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %cond = icmp eq i32 %tid, 0
      br i1 %cond, label %loop.ph, label %else
    else:
      br label %merge
    loop.ph:
      br label %loop.header
    loop.header:
      %i = phi i32 [0, %loop.ph], [%inc, %loop.latch]
      %loop.cond = icmp slt i32 %i, %n
      br i1 %loop.cond, label %loop.body, label %loop.exit
    loop.body:
      call void @feme.cpu.resource.store.raw.i32(ptr %buf, i32 1, i32 %desc, i64 0, i32 %i, i1 true)
      %inc = add i32 %i, 1
      br label %loop.latch
    loop.latch:
      br label %loop.header
    loop.exit:
      br label %merge
    merge:
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
    attributes #0 = { "feme.shader.stage"="fragment" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundResourceStore = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (std::optional<MatchedResourceCall> Matched = matchResourceCall(*CI)) {
      FoundResourceStore = true;
      Value *Mask = CI->getArgOperand(CI->arg_size() - 1);
      EXPECT_FALSE(isa<Constant>(Mask))
          << "the loop's resource store should now be masked by the "
             "enclosing divergent diamond's own narrowed mask, not left "
             "at its placeholder constant `true`";
    }
  }
  EXPECT_TRUE(FoundResourceStore);
}

// Roadmap K6v: a uniform, top-level loop (no enclosing divergence at all)
// whose own real trip-count check is neither its header's nor its latch's
// own terminator -- `StructurizeCFG` instead left it one block upstream of
// a trivial, phi-less relay ("%loop.latch", doing nothing but `br label
// %loop.header`) that the header's/latch's own plain `matchExitCheck`
// cannot see past. `%loop.header` itself ends in an unconditional branch
// straight into the body, and `%loop.check`'s own exit arm reaches
// `%loop.exit` directly. Nested inside a divergent `if` (exactly
// `MasksResourceCallInUniformLoopNestedInDivergentDiamond`'s own shape
// above, so a real, non-constant mask is actually expected on success),
// to additionally confirm the cycle's own `feme.cpu.resource.*` call gets
// masked by the *enclosing* diamond's narrowed mask once the relocated
// uniform latch check is recognized, rather than only confirming the
// diagnostic itself is gone. Before this fix, neither the direct
// `HeaderExit`/`LatchExit` match nor `OtherCondBrBlocks`'s divergent-only
// relocated-check recovery recognized `%loop.check` as the loop's real
// (merely uniform) exit, so this cycle hit the "no recognized uniform
// exit check" diagnostic. Reproduces the control-flow shape found while
// root-causing `dEQP-VK.spirv_assembly.instruction.compute.
// variable_pointers.vars_ssbo_2_diff`'s own `feme-cpu-linearize` failure.
TEST(LinearizeTest, PromotesUniformLatchCheckBehindTrivialRelay) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %n, ptr %buf, i32 %desc) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %cond = icmp eq i32 %tid, 0
      br i1 %cond, label %loop.ph, label %else
    else:
      br label %merge
    loop.ph:
      br label %loop.header
    loop.header:
      %i = phi i32 [0, %loop.ph], [%inc, %loop.latch]
      call void @feme.cpu.resource.store.raw.i32(ptr %buf, i32 1, i32 %desc, i64 0, i32 %i, i1 true)
      %inc = add i32 %i, 1
      br label %loop.check
    loop.check:
      %loop.cond = icmp slt i32 %inc, %n
      br i1 %loop.cond, label %loop.latch, label %loop.exit
    loop.latch:
      br label %loop.header
    loop.exit:
      br label %merge
    merge:
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
    attributes #0 = { "feme.shader.stage"="fragment" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundResourceStore = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (std::optional<MatchedResourceCall> Matched = matchResourceCall(*CI)) {
      FoundResourceStore = true;
      Value *Mask = CI->getArgOperand(CI->arg_size() - 1);
      EXPECT_FALSE(isa<Constant>(Mask))
          << "the loop's resource store should now be masked by the "
             "enclosing divergent diamond's own narrowed mask, not left "
             "at its placeholder constant `true`";
    }
  }
  EXPECT_TRUE(FoundResourceStore);
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

// Roadmap L425(a) (2nd/3rd bugs): mirrors `FlattensLoopBodyDiamond` above,
// but with the mid-body diamond's `then` arm containing a
// `feme.cpu.resource.store.raw.i32` call instead of a plain `add` --
// exactly the shape `AllMemoryBarrierWithGroupSync.test`'s own `for
// (uint I = 0; I < 512; ++I) { if (GTID.x == I) { ...; UavCounter[0]++;
// } }` reduces to. Unlike the plain-arithmetic diamond above, this one
// must *not* be flattened by `DiamondFlattener::flattenLoopBodyDiamond`'s
// own always-active `AllActive` guess (see `regionHasStageMaskOps`'s own
// comment) -- that call's mask operand needs this loop's *real*,
// loop-carried per-iteration mask, which only `LoopLinearizer::
// linearizeCycle`'s own `flattenLoopBodyRegion` computes. Before the 2nd
// bug's fix (teaching `regionHasStageMaskOps` to recognize a
// `feme.cpu.resource.*`/`feme.cpu.image.*` *write* the same way it
// already recognized `feme.stage.*` ops), the call's mask was left
// wrong (first stamped with a bogus always-true guess, then clobbered by
// the real pass's own outer, not-yet-diamond-narrowed mask). Before the
// 3rd bug's own fix (rolling back `MergeVisited`'s insertions from this
// method's own discarded `validate` trial once `regionHasStageMaskOps`
// says to defer), the *discarded* trial's leftover `MergeVisited` entries
// made the real, deferred-to `flattenLoopBodyRegion` call's own `validate`
// immediately (and incorrectly) fail with a "reached by more than one
// divergent root" diagnostic instead of ever running at all.
TEST(LinearizeTest, MasksResourceStoreCallInLoopBodyDiamondViaLoopLinearizer) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %n, ptr %buf, i32 %desc) #0 {
    entry:
      br label %loop.header
    loop.header:
      %i = phi i32 [0, %entry], [%inc, %loop.latch]
      %loop.cond = icmp slt i32 %i, %n
      br i1 %loop.cond, label %loop.body, label %loop.exit
    loop.body:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %cond = icmp eq i32 %tid, 0
      br i1 %cond, label %then, label %else
    then:
      call void @feme.cpu.resource.store.raw.i32(ptr %buf, i32 1, i32 %desc, i64 0, i32 %i, i1 true)
      br label %diamond.merge
    else:
      br label %diamond.merge
    diamond.merge:
      br label %loop.latch
    loop.latch:
      %inc = add i32 %i, 1
      br label %loop.header
    loop.exit:
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Handle) = true;
      },
      &SawError);

  EXPECT_TRUE(run(*M));
  EXPECT_FALSE(SawError)
      << "the loop's own mask-threading machinery should recognize this "
         "shape's uniform exit check instead of diagnosing it as "
         "unsupported";
  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundResourceStore = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (std::optional<MatchedResourceCall> Matched = matchResourceCall(*CI)) {
      FoundResourceStore = true;
      Value *Mask = CI->getArgOperand(CI->arg_size() - 1);
      EXPECT_FALSE(isa<Constant>(Mask))
          << "the loop body diamond's resource store should be masked by "
             "the real, loop-carried per-iteration mask, not left at its "
             "placeholder constant `true`, and not left unresolved by a "
             "spurious `MergeVisited` conflict";
    }
  }
  EXPECT_TRUE(FoundResourceStore);
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
    @out_buf_two_hop = external addrspace(1) global i32

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
      store i32 %flag, ptr addrspace(1) @out_buf_two_hop, align 4
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

// Roadmap L374: distilled from a real captured pre-linearize IR dump of
// `dEQP-VK.spirv_assembly.instruction.graphics.loop.break_frag` (see
// agent_thoughts.md's "L374 session" entry for the full trace this was
// root-caused from). `.flow_crit_edge` below plays the real crash's own
// `.Flow_crit_edge` block's role: a one-hop pure relay stub that is
// `header`'s own *sole* false-arm target, feeding `flow`'s condition phi
// a compile-time-constant `true` value. Because that value is constant,
// `peelConstantFlowPredecessors` (which runs early, before `header`'s own
// exit is even classified) retargets `.flow_crit_edge`'s own branch to go
// directly to `exit` instead of through `flow` -- exercising the same
// peel-then-reclassify shape the real crash's `HeaderExit->RelayBlock`
// fix (see `Linearize.cpp`'s own comment at the `HeaderDivergent`
// handling code) depends on. Note: this particular reduction's own
// control-flow shape (confirmed via `FEME_DEBUG_LINEARIZE_TRACE`) ends up
// fully mask-converting `header`'s own branch rather than reaching the
// specific relayed-`HeaderExit` code path the real crash hit -- it does
// not, on its own, reproduce the original assertion failure pre-fix.
// It is kept anyway as a general regression check that peeling a
// constant-flow predecessor inside a cycle headed toward a genuine exit
// block continues to produce a verifier-clean module with fully
// consistent phi/predecessor bookkeeping (the same class of invariant
// the real bug violated), while the real crash's own, much more reliable
// regression coverage remains the literal CTS case named above (reproduced
// via `gdb` pre-fix, confirmed passing post-fix).
TEST(LinearizeTest, LinearizesLoopWhoseHeaderRelayIsPeeledDirectlyToExit) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @out_buf = external addrspace(1) global float

    define void @main() #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %bound = add i32 %tid, 2
      br label %header

    header:
      %i = phi i32 [ 4, %entry ], [ %dec, %latch ]
      %acc = phi float [ 0.000000e+00, %entry ], [ %frozen, %latch ]
      %cont = icmp sgt i32 %i, %bound
      br i1 %cont, label %body, label %.flow_crit_edge

    .flow_crit_edge:
      br label %flow

    body:
      %fi = uitofp nneg i32 %i to float
      %acc2 = fadd float %acc, %fi
      %dec = add nsw i32 %i, -1
      %break = icmp sle i32 %i, 1
      br label %flow

    flow:
      %acc3 = phi float [ %acc2, %body ], [ %acc, %.flow_crit_edge ]
      %exit3 = phi i1 [ %break, %body ], [ true, %.flow_crit_edge ]
      br i1 %exit3, label %exit, label %latch

    latch:
      %frozen = freeze float %acc3
      br label %header

    exit:
      %final = phi float [ %acc3, %flow ], [ %acc, %latch ]
      store float %final, ptr addrspace(1) @out_buf, align 4
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  EXPECT_FALSE(verifyModule(*M, &errs()));

  // The real crash was a mismatch between a phi's listed incoming blocks
  // and its own parent block's real, structural predecessor list -- so
  // directly re-check that invariant holds for every surviving phi,
  // rather than only trusting `verifyModule` (which does check this, but
  // a regression here is worth asserting on explicitly and clearly).
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  for (BasicBlock &BB : *F) {
    SmallPtrSet<BasicBlock *, 4> Preds(llvm::from_range, predecessors(&BB));
    for (PHINode &PN : BB.phis()) {
      SmallPtrSet<BasicBlock *, 4> Incoming(llvm::from_range, PN.blocks());
      EXPECT_EQ(Preds, Incoming) << "phi " << PN << " in " << BB.getName();
    }
  }
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
// Roadmap L190: distilled from a real captured pre-`feme-cpu-linearize` IR
// dump of `dEQP-VK.graphicsfuzz.complex-nested-loops-and-call`'s fragment
// stage (see agent_thoughts.md's "L190 session" entry for the full trace
// this was root-caused from). `Flow8`'s own condition `phi` (`%44`) has a
// literal-constant contribution from its critical-edge relay predecessor
// `.Flow8_crit_edge`, which `peelConstantFlowPredecessors` peels first,
// bypassing that edge directly into `loop.exit.guard3` and using
// `SSAUpdater` to seed a brand-new reconciling `phi` there for each of
// `Flow8`'s own externally-used values. `collapseTriviallyRedundantPhisInCycle`
// and `mergeTrivialRelayBlocksInCycle` then fold `Flow8` (now phi-less)
// into its sole remaining predecessor `Flow9`, relabeling those new
// `phi`s' `Flow8` entries to `Flow9`. `Flow9`'s own condition `phi` (`%60`
// after that merge) *also* has a literal-constant contribution, from its
// own critical-edge relay `.Flow9_crit_edge` -- so the very same peel
// runs again, this time on `Flow9`, needing to give `loop.exit.guard3`'s
// *already-existing* `phi`s (created by the first peel) a *third*
// incoming entry for this newly-direct edge too.
//
// Before this fix: `SSAUpdater::RewriteUse` only ever replaces the value
// of a single *existing* `Use` in place -- it cannot split an existing
// incoming slot into two, so an existing downstream `phi`'s entry
// labeled `Flow9` never gained a matching entry for `.Flow9_crit_edge`,
// leaving `loop.exit.guard3` with `phi`s that list only 2 incoming values
// despite the block now having 3 real predecessors. That invalid IR
// verified successfully at the time (nothing downstream in the pass
// itself calls `verifyModule`), but crashed deep inside upstream LLVM's
// `JumpThreadingPass` (via `TryToSimplifyUncondBranchFromEmptyBlock`'s
// `PHINode::getIncomingValueForBlock`, whose own assertion this row is
// named for) many stages later, once that pass tried to look up one of
// `loop.exit.guard3`'s `phi`s' value for a predecessor it did not
// actually have an entry for. Confirmed by temporarily reverting this
// fix and re-running this exact test: `verifyModule` now catches the
// same missing-predecessor-entry shape directly, immediately after this
// pass runs, instead of only much later and by assertion crash.
TEST(LinearizeTest,
     ExtendsExistingReconvergencePhiWhenASecondPeelAddsAThirdPredecessor) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"
    
    @spirv_var_38.str = private constant [13 x i8] c"spirv_var_38\00"
    @gl_FragCoord = external addrspace(7) constant <4 x float>
    @_GLF_color = external addrspace(8) global <4 x float>
    
    declare void @llvm.spv.discard()
    
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    
    declare void @feme.stage.discard(i1)
    
    define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #1 {
      %1 = alloca [16 x <3 x float>], align 4
      br label %2
    
    2:                                                ; preds = %Flow12._crit_edge, %0
      %3 = phi i32 [ 0, %0 ], [ %55, %Flow12._crit_edge ]
      %4 = phi <3 x float> [ undef, %0 ], [ %54, %Flow12._crit_edge ]
      %5 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 4, i1 true)
      %6 = fptosi float %5 to i32
      %7 = icmp slt i32 %3, %6
      br i1 %7, label %._crit_edge, label %.Flow12_crit_edge
    
    .Flow12_crit_edge:                                ; preds = %2
      br label %Flow12
    
    ._crit_edge:                                      ; preds = %2
      br label %16
    
    common.ret:                                       ; preds = %Flow.common.ret_crit_edge, %9
      ret void
    
    Flow:                                             ; preds = %loop.exit.guard.Flow_crit_edge, %48
      %8 = phi i1 [ false, %48 ], [ true, %loop.exit.guard.Flow_crit_edge ]
      br i1 %8, label %9, label %Flow.common.ret_crit_edge
    
    Flow.common.ret_crit_edge:                        ; preds = %Flow
      br label %common.ret
    
    9:                                                ; preds = %Flow
      %10 = load <3 x float>, ptr %1, align 4
      %11 = shufflevector <3 x float> %10, <3 x float> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 poison>
      %12 = insertelement <4 x float> %11, float 1.000000e+00, i64 3
      %13 = extractelement <4 x float> %12, i64 0
      call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %13, i32 0)
      %14 = extractelement <4 x float> %12, i64 1
      call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float %14, i32 0)
      %15 = extractelement <4 x float> %12, i64 2
      call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float %15, i32 0)
      call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.000000e+00, i32 0)
      br label %common.ret
    
    16:                                               ; preds = %Flow10._crit_edge, %._crit_edge
      %17 = phi <3 x float> [ %27, %Flow10._crit_edge ], [ %4, %._crit_edge ]
      %18 = phi i32 [ %25, %Flow10._crit_edge ], [ 0, %._crit_edge ]
      %19 = icmp slt i32 %18, 2
      br i1 %19, label %22, label %.Flow10_crit_edge
    
    .Flow10_crit_edge:                                ; preds = %16
      br label %Flow10
    
    20:                                               ; preds = %loop.exit.guard1
      %21 = add i32 %3, 1
      br label %Flow13
    
    22:                                               ; preds = %16
      %23 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
      %24 = fptosi float %23 to i32
      br label %29
    
    Flow10:                                           ; preds = %.Flow10_crit_edge, %Flow11
      %25 = phi i32 [ %51, %Flow11 ], [ poison, %.Flow10_crit_edge ]
      %26 = phi i1 [ false, %Flow11 ], [ true, %.Flow10_crit_edge ]
      %27 = phi <3 x float> [ %52, %Flow11 ], [ %17, %.Flow10_crit_edge ]
      %28 = phi i1 [ %53, %Flow11 ], [ true, %.Flow10_crit_edge ]
      br i1 %28, label %loop.exit.guard1, label %Flow10._crit_edge
    
    Flow10._crit_edge:                                ; preds = %Flow10
      br label %16
    
    29:                                               ; preds = %Flow8._crit_edge, %22
      %30 = phi i32 [ %43, %Flow8._crit_edge ], [ 1, %22 ]
      %31 = fcmp uge float %23, 1.000000e+01
      br i1 %31, label %39, label %.Flow8_crit_edge
    
    .Flow8_crit_edge:                                 ; preds = %29
      br label %Flow8
    
    32:                                               ; preds = %loop.exit.guard3
      %33 = sitofp i32 %43 to float
      %34 = insertelement <3 x float> <float poison, float 0.000000e+00, float 0.000000e+00>, float %33, i64 0
      %35 = select i1 %31, <3 x float> %42, <3 x float> %34
      %36 = sext i32 %24 to i64
      %37 = getelementptr [16 x i8], ptr %1, i64 %36
      store <3 x float> %35, ptr %37, align 4
      %38 = add i32 %18, 1
      br label %Flow11
    
    39:                                               ; preds = %29
      %40 = fcmp uge float %23, 2.000000e+01
      br i1 %40, label %45, label %.Flow9_crit_edge
    
    .Flow9_crit_edge:                                 ; preds = %39
      br label %Flow9
    
    Flow8:                                            ; preds = %.Flow8_crit_edge, %Flow9
      %41 = phi i1 [ %58, %Flow9 ], [ true, %.Flow8_crit_edge ]
      %42 = phi <3 x float> [ splat (float 1.000000e+00), %Flow9 ], [ %17, %.Flow8_crit_edge ]
      %43 = phi i32 [ %59, %Flow9 ], [ %30, %.Flow8_crit_edge ]
      %44 = phi i1 [ %60, %Flow9 ], [ true, %.Flow8_crit_edge ]
      br i1 %44, label %loop.exit.guard3, label %Flow8._crit_edge
    
    Flow8._crit_edge:                                 ; preds = %Flow8
      br label %29
    
    45:                                               ; preds = %39
      %46 = add i32 %30, 1
      %47 = fcmp olt float %23, 3.000000e+01
      br label %Flow9
    
    48:                                               ; preds = %loop.exit.guard
      call void @feme.stage.discard(i1 true)
      br label %Flow
    
    Flow13:                                           ; preds = %loop.exit.guard1.Flow13_crit_edge, %20
      %49 = phi i32 [ %21, %20 ], [ poison, %loop.exit.guard1.Flow13_crit_edge ]
      %50 = phi i1 [ false, %20 ], [ true, %loop.exit.guard1.Flow13_crit_edge ]
      br label %Flow12
    
    loop.exit.guard:                                  ; preds = %Flow12
      %Guard..inv = xor i1 %56, true
      br i1 %Guard..inv, label %48, label %loop.exit.guard.Flow_crit_edge
    
    loop.exit.guard.Flow_crit_edge:                   ; preds = %loop.exit.guard
      br label %Flow
    
    Flow11:                                           ; preds = %loop.exit.guard3.Flow11_crit_edge, %32
      %51 = phi i32 [ %38, %32 ], [ poison, %loop.exit.guard3.Flow11_crit_edge ]
      %52 = phi <3 x float> [ %35, %32 ], [ %17, %loop.exit.guard3.Flow11_crit_edge ]
      %53 = phi i1 [ false, %32 ], [ true, %loop.exit.guard3.Flow11_crit_edge ]
      br label %Flow10
    
    Flow12:                                           ; preds = %.Flow12_crit_edge, %Flow13
      %54 = phi <3 x float> [ %27, %Flow13 ], [ %4, %.Flow12_crit_edge ]
      %55 = phi i32 [ %49, %Flow13 ], [ poison, %.Flow12_crit_edge ]
      %56 = phi i1 [ false, %Flow13 ], [ true, %.Flow12_crit_edge ]
      %57 = phi i1 [ %50, %Flow13 ], [ true, %.Flow12_crit_edge ]
      br i1 %57, label %loop.exit.guard, label %Flow12._crit_edge
    
    Flow12._crit_edge:                                ; preds = %Flow12
      br label %2
    
    loop.exit.guard1:                                 ; preds = %Flow10
      br i1 %26, label %20, label %loop.exit.guard1.Flow13_crit_edge
    
    loop.exit.guard1.Flow13_crit_edge:                ; preds = %loop.exit.guard1
      br label %Flow13
    
    Flow9:                                            ; preds = %.Flow9_crit_edge, %45
      %58 = phi i1 [ true, %45 ], [ false, %.Flow9_crit_edge ]
      %59 = phi i32 [ %46, %45 ], [ poison, %.Flow9_crit_edge ]
      %60 = phi i1 [ %47, %45 ], [ true, %.Flow9_crit_edge ]
      br label %Flow8
    
    loop.exit.guard3:                                 ; preds = %Flow8
      br i1 %41, label %32, label %loop.exit.guard3.Flow11_crit_edge
    
    loop.exit.guard3.Flow11_crit_edge:                ; preds = %loop.exit.guard3
      br label %Flow11
    }
    
    declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1)
    
    attributes #1 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="fragment" "hlsl.shader"="pixel" }
  )");
  ASSERT_TRUE(M);

  // Must not crash the process, and the resulting IR must remain valid
  // SSA -- before this fix, `loop.exit.guard3`'s own reconciling `phi`s
  // (created by the first of two, same-cycle constant-flow-predecessor
  // peels) were left with only 2 incoming entries despite the block
  // ending up with 3 real predecessors once the second peel ran, which
  // `verifyModule` catches as a malformed `phi` node.
  run(*M);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap L196: distilled from a real captured pre-`feme-cpu-linearize` IR
// dump of the `stable-colorgrid-modulo-double-always-false-discard`
// graphicsfuzz Vulkan CTS case's own `variant_fragment_shader` (see
// agent_thoughts.md's "L196(b) session" entry for the full derivation:
// `spirv-as` -> `feme-translate --import-spirv` -> `feme-translate
// --no-implicit-module --spirv-to-llvmir` -> the real `feme-cpu` target's
// own `feme-graphics-canonicalize-stage`/`feme-cpu-prepare`/... normalize
// pipeline, run via `feme-opt -passes=...` up to (but not including)
// `feme-cpu-linearize` -- then `llvm-reduce`, in two passes (first over
// the raw pre-normalize IR with an interestingness test comparing this
// pass's pre-L196-fix and post-L196-fix `feme.cpu.mask.any` counts on the
// *whole* normalize-then-linearize pipeline, then again directly on the
// captured pre-linearize IR with the same comparison run through this
// pass alone) to reach the minimal shape below.
//
// The real shader's own "two sibling top-level loops" shape that
// motivated this reduction (see `agent_thoughts.md`'s L196 session
// entry) did not survive either reduction pass: both `llvm-reduce` runs
// independently converged on a *single* loop whose one exit check is
// itself composed, via an inner one-arm-diamond's own merge `phi`
// (`%4` below), from a genuinely divergent value (`%3`, a stage input
// load) and a constant. This is a different concrete trigger for the
// exact same underlying hazard `KnownUniformValues` fixes, not the
// original sibling-cycle shape itself: `DiamondFlattener` first
// flattens the one-arm diamond (`1`/`._crit_edge.Flow5_crit_edge`) that
// computes `%3`, replacing `Flow5`'s own original terminator (whatever
// `UniformityInfo` actually analyzed it as, before this pass ran) with a
// brand new conditional branch on the freshly created merge `phi` `%4`
// -- itself divergent, since one of its two incoming values (`%3`) is.
// Before this fix, `isDivergentBranch`'s predecessor,
// `UI.isDivergentTerminator`, is keyed on `Flow5`'s own *block* and
// therefore still reports whatever verdict `UniformityInfo` gave
// `Flow5`'s *original* terminator, not this pass's own newly created
// one -- exactly the same stale-block-keyed-cache hazard the real
// sibling-cycle CTS case hit, just via a diamond-then-loop-exit
// composition within a single cycle rather than across two.
//
// Confirmed by temporarily reverting the `KnownUniformValues` mechanism
// (`Linearize.cpp`'s own L196 commit) and re-running this exact test
// via a standalone `feme-opt -passes=feme-cpu-linearize` invocation on
// this same IR: without the fix, `Flow5`'s own backedge condition is
// misclassified as uniform (no `feme.cpu.mask.any` reduction is
// created, and the loop's continuation branches directly on `%4`'s own
// genuinely divergent, unreduced per-lane value instead) -- the same
// silently-wrong-render class of bug (not a crash or verifier failure)
// the real CTS sweep observed.
TEST(LinearizeTest, TracksUniformityOfOwnFlattenedDiamondMergeAcrossLoopExit) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      br label %._crit_edge

    ._crit_edge:                                      ; preds = %Flow5.._crit_edge_crit_edge, %0
      br i1 false, label %1, label %._crit_edge.Flow5_crit_edge

    ._crit_edge.Flow5_crit_edge:                       ; preds = %._crit_edge
      br label %Flow5

    1:                                                  ; preds = %._crit_edge
      %2 = call float @feme.stage.input.load.f32()
      %3 = fcmp ult float 0.000000e+00, %2
      br label %Flow7

    Flow5:                                              ; preds = %Flow7, %._crit_edge.Flow5_crit_edge
      %4 = phi i1 [ %3, %Flow7 ], [ true, %._crit_edge.Flow5_crit_edge ]
      br i1 %4, label %loop.exit.guard, label %Flow5.._crit_edge_crit_edge

    Flow5.._crit_edge_crit_edge:                       ; preds = %Flow5
      br label %._crit_edge

    Flow7:                                              ; preds = %1
      br label %Flow5

    loop.exit.guard:                                   ; preds = %Flow5
      ret void
    }
    declare float @feme.stage.input.load.f32()
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

// Roadmap L197/L198/L200/L282: genuinely nested cycles -- an outer loop
// whose body contains its own, separate inner loop, each with its own
// divergent exit check. `LoopLinearizer::run()` traverses post-order
// (`linearizeCyclePostOrder`, recursing into every child before
// considering its parent), precomputing every cycle's own exit-block
// list up front (`precomputeExitBlocks`/`getExitBlocks`, sidestepping a
// genuine use-after-free `CI.getExitBlocks` would otherwise hit against
// an ancestor cycle whose descendant already erased some of its own
// blocks -- see `ExitBlocksByCycle`'s own comment for the full hazard
// writeup, and historical commit b9cba5d890f3 for the closely related,
// previously-hit `UniformityInfo`-recomputation crash this shares its
// root cause with) and refreshing `DT`/`PDT` after every cycle (a second,
// independent staleness hazard this same investigation found, affecting
// even leaf-only traversal across *sibling* leaf cycles). With all six
// bugs found via real Vulkan CTS shaders once a non-leaf cycle was
// actually attempted now fixed (see `linearizeCyclePostOrder`'s own
// comment for the full writeup), attempting `linearizeCycle` on the
// *outer*, non-leaf cycle itself is unconditional: both the inner leaf
// cycle and the outer non-leaf cycle are now linearized.
TEST(LinearizeTest, LinearizesBothInnerLeafLoopAndOuterNonLeafLoop) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      br label %outer.header
    outer.header:
      %i = phi i32 [0, %entry], [%i.inc, %outer.latch]
      br label %inner.header
    inner.header:
      %j = phi i32 [0, %outer.header], [%j.inc, %inner.header]
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %j.inc = add i32 %j, 1
      %inner.break = icmp eq i32 %tid, %j.inc
      br i1 %inner.break, label %outer.latch, label %inner.header
    outer.latch:
      %i.inc = add i32 %i, 1
      %outer.break = icmp eq i32 %tid, %i.inc
      br i1 %outer.break, label %exit, label %outer.header
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
  unsigned MaskAnyCount = 0;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (CI->getCalledFunction() &&
          CI->getCalledFunction()->getName() == "feme.cpu.mask.any")
        ++MaskAnyCount;
  // Both the inner loop's own divergent exit AND the outer loop's own
  // divergent exit now get their own reduction, now that non-leaf
  // traversal is unconditional (see the comment above).
  EXPECT_EQ(MaskAnyCount, 2u);
  BasicBlock *OuterLatch = nullptr;
  for (BasicBlock &BB : *F)
    if (BB.getName() == "outer.latch")
      OuterLatch = &BB;
  ASSERT_TRUE(OuterLatch);
  auto *OuterBr = dyn_cast<CondBrInst>(OuterLatch->getTerminator());
  ASSERT_TRUE(OuterBr);
  // The outer loop's own plain `outer.break` icmp/br has been replaced by
  // a reduced `loop.continue` mask check, just like the inner loop's own
  // exit already was.
  EXPECT_EQ(OuterBr->getCondition()->getName(), "loop.continue3");
}

// Roadmap L282 bug 5: `DiamondFlattener::validate`'s own recursive descent
// used to have no bound at all, so a genuinely deep chain of nested
// divergent diamonds (the shape a deeply nested chain of `if`s without
// `else`s produces) could stack-overflow the process (a real `SIGSEGV`,
// confirmed via `gdb`, on `dEQP-VK.graphicsfuzz.increment-value-in-nested-
// for-loop`) instead of being diagnosed as an ordinary "not this shape"
// validation failure. Build a synthetic, non-loop repro of the same
// "deeply nested diamond" shape directly (300 levels, comfortably past
// the 256-deep bound) and confirm it is cleanly rejected rather than
// crashing the test process. Each level `I` is an `if`-without-`else`
// nested inside the previous level's own true arm (`tI` branches to
// `t(I+1)` on true, `fI` on false), with every level's own merge block
// `mI` falling through to the enclosing level's merge (`m(I-1)`), down
// to a single shared `exit` that returns -- exactly the "deeply nested
// `if`" shape a real shader's own deeply nested control flow produces.
TEST(LinearizeTest,
     DeeplyNestedDivergentDiamondsDiagnoseInsteadOfStackOverflowing) {
  constexpr unsigned NumLevels = 300;
  std::string Asm = R"(
    define void @main() #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c = icmp eq i32 %tid, 0
      br i1 %c, label %t0, label %f0
  )";
  for (unsigned I = 0; I != NumLevels; ++I) {
    Asm += "    t" + std::to_string(I) + ":\n";
    if (I + 1 != NumLevels)
      Asm += "      br i1 %c, label %t" + std::to_string(I + 1) +
             ", label %f" + std::to_string(I) + "\n";
    else
      Asm += "      br label %m" + std::to_string(I) + "\n";
    Asm += "    f" + std::to_string(I) + ":\n      br label %m" +
           std::to_string(I) + "\n";
    Asm += "    m" + std::to_string(I) + ":\n      br label %" +
           (I == 0 ? "exit" : ("m" + std::to_string(I - 1))) + "\n";
  }
  Asm += R"(
    exit:
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )";

  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, Asm);
  ASSERT_TRUE(M);

  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Handle) = true;
      },
      &SawError);

  // Must not crash the process (a stack-overflowing SIGSEGV pre-fix would
  // abort the test binary outright instead of returning).
  EXPECT_FALSE(run(*M));
  EXPECT_TRUE(SawError);
}

// Roadmap L286: root-caused via a live `gdb` repro of `dEQP-VK.glsl.loops.
// special.{for,while}_dynamic_iterations.dowhile_trap_{fragment,vertex}`
// (see this session's own `agent_thoughts.md` entry, and
// `DiamondFlattener::getCycleBoundaryMasks`/`LoopLinearizer::
// DiamondFlattenedEntryMasks`'s own member comments for the full
// writeup). `DiamondFlattener::run()` -- a wholly separate pass that runs
// *before* `LoopLinearizer`, against the pristine, not-yet-linearized IR
// -- flattens an outer divergent diamond (`%cond` below) whose "true" arm
// contains an entire nested cycle it has not linearized yet, tolerated via
// its own `CycleBoundaryBlocks` mechanism (see `validate`'s own early
// return "stop here; LoopLinearizer's problem, not an error"). Before this
// fix, `LoopLinearizer`'s own `makeActivePNPair` had no way to learn what
// mask `DiamondFlattener` had already computed reaching that boundary, and
// always seeded the nested cycle's own entry mask with a bare,
// unconditional `true` instead -- so a lane `%cond` already decided should
// skip the nested cycle entirely still entered it "live". Here, exactly
// like the real `dowhile_trap` shape, the nested cycle's own exit
// condition (`%inner.cond`) depends on the very same, now-frozen `%i`/
// `%bound` pair `%cond` does, so such a lane's own reduction would spin
// forever once actually executed -- this test instead just confirms the
// nested cycle's own entry mask phi is no longer a bare literal `true` for
// its non-backedge incoming edge, i.e. that it has genuinely inherited
// `%cond`'s own narrowing rather than reproducing the hang via a slow,
// real JIT execution.
TEST(LinearizeTest,
     NestedCycleInsideFlattenedDiamondArmInheritsOuterDiamondsMask) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      %bound = call i32 @llvm.dx.thread.id(i32 0)
      br label %outer.header
    outer.header:
      %i = phi i32 [0, %entry], [%i.next, %outer.latch]
      %cond = icmp slt i32 %i, %bound
      br i1 %cond, label %inner.preheader, label %skip
    inner.preheader:
      br label %inner.header
    inner.header:
      %j = phi i32 [0, %inner.preheader], [%j.next, %inner.header]
      %j.next = add i32 %j, 1
      %inner.cond = icmp slt i32 %i, %bound
      br i1 %inner.cond, label %inner.header, label %inner.exit
    inner.exit:
      br label %merge
    skip:
      br label %merge
    merge:
      br label %outer.latch
    outer.latch:
      %i.next = add i32 %i, 1
      ; Deliberately a *uniform* trip count (a constant bound), unlike
      ; %cond above -- so the outer loop itself is an ordinary, uniform
      ; loop LoopLinearizer leaves untouched (see `LeavesUniformLoopUnchanged`
      ; above), and only the divergent mid-body diamond (%cond) plus the
      ; nested cycle it contains need any masking at all. This isolates the
      ; bug to the DiamondFlattener-to-LoopLinearizer cross-pass gap instead
      ; of also exercising LoopLinearizer's own, already-fixed, same-pass
      ; cycle-to-cycle `rethreadNestedEntryMasks` mechanism, which would
      ; mask this exact failure if the outer loop were divergent too.
      %outer.cond = icmp slt i32 %i.next, 4
      br i1 %outer.cond, label %outer.header, label %exit
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
  BasicBlock *InnerHeader = nullptr;
  for (BasicBlock &BB : *F)
    if (BB.getName().starts_with("inner.header"))
      InnerHeader = &BB;
  ASSERT_TRUE(InnerHeader);

  PHINode *InnerLive = nullptr;
  for (PHINode &PN : InnerHeader->phis())
    if (PN.getName().starts_with("active.live")) {
      InnerLive = &PN;
      break;
    }
  ASSERT_TRUE(InnerLive);

  // Every incoming value entering the inner cycle from *outside* it (not
  // its own backedge) must not be a bare literal `true` -- it must be
  // narrowed by the enclosing diamond's own condition, otherwise a lane
  // the enclosing diamond's own condition excluded would still enter the
  // inner cycle "live", reproducing the hang this test guards against.
  bool SawNonBackedgeIncoming = false;
  for (unsigned I = 0, E = InnerLive->getNumIncomingValues(); I != E; ++I) {
    BasicBlock *Pred = InnerLive->getIncomingBlock(I);
    if (Pred == InnerHeader)
      continue; // The cycle's own backedge -- not an entry edge.
    SawNonBackedgeIncoming = true;
    Value *V = InnerLive->getIncomingValue(I);
    EXPECT_FALSE(isa<ConstantInt>(V))
        << "inner cycle's own entry mask was not narrowed by the "
           "enclosing diamond's condition (Pred='"
        << Pred->getName() << "')";
  }
  EXPECT_TRUE(SawNonBackedgeIncoming);
}

// Roadmap L377(a): a real, captured pre-`feme-cpu-linearize` IR reduction
// of `dEQP-VK.pipeline.monolithic.image.sampler.view_type.1d.format.*.
// image_size.127x1_compute`'s own barycentric-sampling loop (see the
// checked-in `Transforms/CPU/Linearize/
// checkblock-chained-divergent-check-bypass.ll` lit test for the full
// FileCheck-driven version of this same case). The loop's body has a
// second, genuinely divergent branch (`Flow72`) immediately upstream of
// the loop's real exit check (`Flow72.Flow74_crit_edge`) whose own "stay
// in loop" continuation (ultimately block `26`) is *also* reachable
// directly from `Flow72`'s own sibling successor `86` (via block
// `29`/`Flow75`/`Flow71`), entirely bypassing `Flow72.Flow74_crit_edge`'s
// own check -- i.e. that check did not dominate its own continuation.
// `Flow72` is not itself a second exit check (neither of its arms reaches
// `ExitBlock` on its own) -- it is an ordinary "if (c1) continue; else if
// (c2) continue; else exit;" shape. Before roadmap `L377`'s guard, this
// silently produced IR failing `verifyModule` ("Instruction does not
// dominate all uses!" on `active.check.live`/`exited.via.check.next` and
// their phi/select consumers); `L377(a)` recognizes this specific,
// common shape and folds `Flow72`'s own divergent branch into a plain
// `select` feeding the exit check's own single-entry machinery (see
// `linearizeCycle`'s own comment) -- deleting the now wholly dead bypass
// chain outright so no divergent branch (which `feme-cpu-simdize` would
// otherwise reject) is left behind -- so it now linearizes successfully
// instead of being diagnosed.
TEST(LinearizeTest,
     LinearizesCheckBlockChainedDivergentCheckBypassToStayInLoop) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    declare i32 @llvm.spv.thread.id.i32(i32)
    declare i32 @llvm.spv.num.workgroups.i32(i32)
    declare float @llvm.fabs.f32(float)
    declare i32 @feme.cpu.resource.getdimensions.raw.i32(ptr, i32, i32, i64, i64, i1)
    declare <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr, i32, i32, i64, i1)
    declare <4 x float> @feme.cpu.image.sample.1d.v4f32(ptr, i32, ptr, i32, i32, i32, float, float, float, float, i1, float, i32, float, i1)
    declare void @feme.cpu.image.store.2d.v4f32(ptr, i32, i32, i32, i32, <4 x float>, i1)

    define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #0 {
      %1 = call i32 @llvm.spv.thread.id.i32(i32 0)
      %2 = insertelement <3 x i32> poison, i32 %1, i32 0
      %3 = call i32 @llvm.spv.thread.id.i32(i32 1)
      %4 = insertelement <3 x i32> %2, i32 %3, i32 1
      %5 = call i32 @llvm.spv.thread.id.i32(i32 2)
      %6 = insertelement <3 x i32> %4, i32 %5, i32 2
      %7 = shufflevector <3 x i32> %6, <3 x i32> %6, <2 x i32> <i32 0, i32 1>
      %8 = uitofp <2 x i32> %7 to <2 x float>
      %9 = fadd <2 x float> %8, splat (float 5.000000e-01)
      %10 = call i32 @llvm.spv.num.workgroups.i32(i32 0)
      %11 = insertelement <3 x i32> poison, i32 %10, i32 0
      %12 = call i32 @llvm.spv.num.workgroups.i32(i32 1)
      %13 = insertelement <3 x i32> %11, i32 %12, i32 1
      %14 = call i32 @llvm.spv.num.workgroups.i32(i32 2)
      %15 = insertelement <3 x i32> %13, i32 %14, i32 2
      %16 = shufflevector <3 x i32> %15, <3 x i32> %15, <2 x i32> <i32 0, i32 1>
      %17 = uitofp <2 x i32> %16 to <2 x float>
      %18 = fdiv <2 x float> %9, %17
      %19 = fmul <2 x float> %18, splat (float 2.000000e+00)
      %20 = fsub <2 x float> %19, splat (float 1.000000e+00)
      br label %21

    21:
      %.0 = phi i32 [ 0, %0 ], [ %56, %Flow71._crit_edge ]
      %22 = call i32 @feme.cpu.resource.getdimensions.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 32, i64 0, i1 true)
      %23 = icmp slt i32 %.0, %22
      br i1 %23, label %31, label %.Flow71_crit_edge

    .Flow71_crit_edge:
      br label %Flow71

    Flow:
      %24 = phi <4 x float> [ %124, %93 ], [ poison, %loop.exit.guard.Flow_crit_edge ]
      %25 = phi i1 [ false, %93 ], [ true, %loop.exit.guard.Flow_crit_edge ]
      br i1 %25, label %26, label %Flow.spirv_fn_12.exit_crit_edge

    Flow.spirv_fn_12.exit_crit_edge:
      br label %spirv_fn_12.exit

    26:
      %27 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 16, i1 true)
      br label %spirv_fn_12.exit

    Flow74:
      %28 = phi i1 [ true, %86 ], [ %90, %Flow72.Flow74_crit_edge ]
      br i1 %28, label %29, label %Flow74.Flow75_crit_edge

    Flow74.Flow75_crit_edge:
      br label %Flow75

    29:
      %30 = add i32 %.0, 3
      br label %Flow75

    31:
      %32 = zext i32 %.0 to i64
      %33 = mul i64 %32, 32
      %34 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %33, i1 true)
      %35 = shufflevector <4 x float> %34, <4 x float> %34, <2 x i32> <i32 0, i32 1>
      %36 = add i32 %.0, 1
      %37 = zext i32 %36 to i64
      %38 = mul i64 %37, 32
      %39 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %38, i1 true)
      %40 = shufflevector <4 x float> %39, <4 x float> %39, <2 x i32> <i32 0, i32 1>
      %41 = add i32 %.0, 2
      %42 = zext i32 %41 to i64
      %43 = mul i64 %42, 32
      %44 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %43, i1 true)
      %45 = shufflevector <4 x float> %44, <4 x float> %44, <2 x i32> <i32 0, i32 1>
      %.0.vec.extract37 = extractelement <2 x float> %40, i64 0
      %.0.vec.extract49 = extractelement <2 x float> %35, i64 0
      %46 = fsub float %.0.vec.extract37, %.0.vec.extract49
      %.4.vec.extract29 = extractelement <2 x float> %45, i64 1
      %.4.vec.extract55 = extractelement <2 x float> %35, i64 1
      %47 = fsub float %.4.vec.extract29, %.4.vec.extract55
      %48 = fmul float %46, %47
      %.4.vec.extract43 = extractelement <2 x float> %40, i64 1
      %.4.vec.extract57 = extractelement <2 x float> %35, i64 1
      %49 = fsub float %.4.vec.extract43, %.4.vec.extract57
      %.0.vec.extract21 = extractelement <2 x float> %45, i64 0
      %.0.vec.extract51 = extractelement <2 x float> %35, i64 0
      %50 = fsub float %.0.vec.extract21, %.0.vec.extract51
      %51 = fmul float %49, %50
      %52 = fsub float %48, %51
      %53 = call float @llvm.fabs.f32(float %52)
      %54 = fcmp uge float %53, f0x3727C5AC
      br i1 %54, label %61, label %.Flow72_crit_edge

    .Flow72_crit_edge:
      br label %Flow72

    Flow71:
      %55 = phi i1 [ %91, %Flow75 ], [ true, %.Flow71_crit_edge ]
      %56 = phi i32 [ %138, %Flow75 ], [ %.0, %.Flow71_crit_edge ]
      %57 = phi float [ %87, %Flow75 ], [ poison, %.Flow71_crit_edge ]
      %58 = phi float [ %88, %Flow75 ], [ poison, %.Flow71_crit_edge ]
      %59 = phi float [ %89, %Flow75 ], [ poison, %.Flow71_crit_edge ]
      %60 = phi i1 [ %139, %Flow75 ], [ true, %.Flow71_crit_edge ]
      br i1 %60, label %loop.exit.guard, label %Flow71._crit_edge

    Flow71._crit_edge:
      br label %21

    61:
      %.0.vec.extract23 = extractelement <2 x float> %45, i64 0
      %.0.vec.extract39 = extractelement <2 x float> %40, i64 0
      %62 = fsub float %.0.vec.extract23, %.0.vec.extract39
      %.4.vec.extract = extractelement <2 x float> %20, i64 1
      %.4.vec.extract45 = extractelement <2 x float> %40, i64 1
      %63 = fsub float %.4.vec.extract, %.4.vec.extract45
      %64 = fmul float %62, %63
      %.4.vec.extract31 = extractelement <2 x float> %45, i64 1
      %.4.vec.extract47 = extractelement <2 x float> %40, i64 1
      %65 = fsub float %.4.vec.extract31, %.4.vec.extract47
      %.0.vec.extract = extractelement <2 x float> %20, i64 0
      %.0.vec.extract41 = extractelement <2 x float> %40, i64 0
      %66 = fsub float %.0.vec.extract, %.0.vec.extract41
      %67 = fmul float %65, %66
      %68 = fsub float %64, %67
      %69 = fdiv float %68, %52
      %.0.vec.extract53 = extractelement <2 x float> %35, i64 0
      %.0.vec.extract25 = extractelement <2 x float> %45, i64 0
      %70 = fsub float %.0.vec.extract53, %.0.vec.extract25
      %.4.vec.extract5 = extractelement <2 x float> %20, i64 1
      %.4.vec.extract33 = extractelement <2 x float> %45, i64 1
      %71 = fsub float %.4.vec.extract5, %.4.vec.extract33
      %72 = fmul float %70, %71
      %.4.vec.extract59 = extractelement <2 x float> %35, i64 1
      %.4.vec.extract35 = extractelement <2 x float> %45, i64 1
      %73 = fsub float %.4.vec.extract59, %.4.vec.extract35
      %.0.vec.extract3 = extractelement <2 x float> %20, i64 0
      %.0.vec.extract27 = extractelement <2 x float> %45, i64 0
      %74 = fsub float %.0.vec.extract3, %.0.vec.extract27
      %75 = fmul float %73, %74
      %76 = fsub float %72, %75
      %77 = fdiv float %76, %52
      %78 = fsub float 1.000000e+00, %69
      %79 = fsub float %78, %77
      %80 = fcmp oge float %69, f0xB727C5AC
      %81 = fcmp oge float %77, f0xB727C5AC
      %82 = and i1 %80, %81
      %83 = fcmp oge float %79, f0xB727C5AC
      %84 = and i1 %82, %83
      %.inv70 = xor i1 %84, true
      br i1 %.inv70, label %92, label %.Flow73_crit_edge

    .Flow73_crit_edge:
      br label %Flow73

    Flow73:
      %85 = phi i1 [ true, %92 ], [ false, %.Flow73_crit_edge ]
      br label %Flow72

    86:
      br label %Flow74

    Flow72:
      %87 = phi float [ %69, %Flow73 ], [ poison, %.Flow72_crit_edge ]
      %88 = phi float [ %77, %Flow73 ], [ poison, %.Flow72_crit_edge ]
      %89 = phi float [ %79, %Flow73 ], [ poison, %.Flow72_crit_edge ]
      %90 = phi i1 [ %85, %Flow73 ], [ false, %.Flow72_crit_edge ]
      %91 = phi i1 [ false, %Flow73 ], [ true, %.Flow72_crit_edge ]
      br i1 %91, label %86, label %Flow72.Flow74_crit_edge

    Flow72.Flow74_crit_edge:
      br label %Flow74

    92:
      br label %Flow73

    93:
      %94 = zext i32 %56 to i64
      %95 = mul i64 %94, 32
      %96 = add i64 %95, 16
      %97 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %96, i1 true)
      %98 = insertelement <4 x float> poison, float %57, i32 0
      %99 = insertelement <4 x float> %98, float %57, i32 1
      %100 = insertelement <4 x float> %99, float %57, i32 2
      %101 = insertelement <4 x float> %100, float %57, i32 3
      %102 = fmul <4 x float> %97, %101
      %103 = add i32 %56, 1
      %104 = zext i32 %103 to i64
      %105 = mul i64 %104, 32
      %106 = add i64 %105, 16
      %107 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %106, i1 true)
      %108 = insertelement <4 x float> poison, float %58, i32 0
      %109 = insertelement <4 x float> %108, float %58, i32 1
      %110 = insertelement <4 x float> %109, float %58, i32 2
      %111 = insertelement <4 x float> %110, float %58, i32 3
      %112 = fmul <4 x float> %107, %111
      %113 = fadd <4 x float> %102, %112
      %114 = add i32 %56, 2
      %115 = zext i32 %114 to i64
      %116 = mul i64 %115, 32
      %117 = add i64 %116, 16
      %118 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %117, i1 true)
      %119 = insertelement <4 x float> poison, float %59, i32 0
      %120 = insertelement <4 x float> %119, float %59, i32 1
      %121 = insertelement <4 x float> %120, float %59, i32 2
      %122 = insertelement <4 x float> %121, float %59, i32 3
      %123 = fmul <4 x float> %118, %122
      %124 = fadd <4 x float> %113, %123
      br label %Flow

    spirv_fn_12.exit:
      %125 = phi <4 x float> [ %24, %Flow.spirv_fn_12.exit_crit_edge ], [ %27, %26 ]
      %.0.vec.extract7 = extractelement <4 x float> %125, i64 0
      %126 = call <4 x float> @feme.cpu.image.sample.1d.v4f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %.0.vec.extract7, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 true, float 0.000000e+00, i32 0, float -inf, i1 true)
      %127 = fmul <4 x float> %126, <float 5.000000e-04, float 1.000000e+00, float 1.000000e+00, float 1.000000e+00>
      %128 = fadd <4 x float> %127, <float 5.000000e-01, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00>
      %129 = call i32 @llvm.spv.thread.id.i32(i32 0)
      %130 = insertelement <3 x i32> poison, i32 %129, i32 0
      %131 = call i32 @llvm.spv.thread.id.i32(i32 1)
      %132 = insertelement <3 x i32> %130, i32 %131, i32 1
      %133 = call i32 @llvm.spv.thread.id.i32(i32 2)
      %134 = insertelement <3 x i32> %132, i32 %133, i32 2
      %135 = shufflevector <3 x i32> %134, <3 x i32> %134, <2 x i32> <i32 0, i32 1>
      %136 = extractelement <2 x i32> %135, i64 0
      %137 = extractelement <2 x i32> %135, i64 1
      call void @feme.cpu.image.store.2d.v4f32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 %136, i32 %137, <4 x float> %128, i1 true)
      ret void

    Flow75:
      %138 = phi i32 [ %30, %29 ], [ %.0, %Flow74.Flow75_crit_edge ]
      %139 = phi i1 [ false, %29 ], [ true, %Flow74.Flow75_crit_edge ]
      br label %Flow71

    loop.exit.guard:
      %Guard..inv = xor i1 %55, true
      br i1 %Guard..inv, label %93, label %loop.exit.guard.Flow_crit_edge

    loop.exit.guard.Flow_crit_edge:
      br label %Flow
    }
    attributes #0 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="compute" "hlsl.numthreads"="1,1,1" "hlsl.shader"="compute" }
  )");
  ASSERT_TRUE(M) << "the IR text itself must still parse";

  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Handle) = true;
      },
      &SawError);

  // Roadmap L377(a): this shape is now linearized successfully instead of
  // being diagnosed -- confirm no error is raised and the resulting IR
  // still verifies cleanly (not just "doesn't crash", the bar the
  // pre-`L377(a)` diagnostic-only guard only ever had to clear).
  run(*M);
  EXPECT_FALSE(SawError) << "roadmap L377(a) should linearize this shape "
                            "instead of diagnosing it as unsupported";
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap L432(b): `DiamondFlattener::run()` -- the whole-function pass
// that runs *before* `LoopLinearizer` -- shares its own `MergeVisited`/
// `ResolvedBlocks` bookkeeping with `LoopLinearizer` for the rest of the
// function's lifetime (see `MergeVisited`'s own comment). Its entry-root
// validation walk passes straight through a uniform loop's header, body,
// and latch purely to discover the loop's own cycle-boundary exit (never
// to flatten anything inside it, since the loop's own exit check is
// uniform here, not a divergent diamond). When that walk finds no
// divergent branch anywhere else in the function either, `run()` used to
// bail out (`return false`, "nothing to flatten") while still leaving
// every one of those blocks claimed in the shared `MergeVisited` with no
// matching `ResolvedBlocks` entry -- so `LoopLinearizer`'s own, later,
// legitimate `flattenLoopBodyRegion` walk over the very same blocks (this
// loop's body has a `feme.cpu.resource.store.raw.i32` call, so its mask
// must be threaded through that call, not `DiamondFlattener`'s own
// simpler per-diamond flattening) immediately hit the `L375` "reached by
// more than one divergent root" diagnostic as a false positive. This
// reproduces `dEQP-VK.synchronization.op.multi_queue.binary_semaphore.
// write_blit_image_read_image_compute.image_128_r32_uint_concurrent`'s
// own shape: a uniform `for` loop whose header holds only the induction
// `phi` plus an unconditional branch to the real (uniform) trip-count
// check, whose "exit" arm is routed through a trivial relay block before
// reaching the function's single `ret`.
TEST(LinearizeTest, DoesNotPoisonMergeVisitedWhenWholeFunctionPassDeclines) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %n, ptr %buf, i32 %desc) #0 {
    entry:
      br label %header
    header:
      %i = phi i32 [0, %entry], [%i.next, %latch]
      br label %check
    check:
      %cond = icmp slt i32 %i, %n
      br i1 %cond, label %body, label %flowcritedge
    body:
      call void @feme.cpu.resource.store.raw.i32(ptr %buf, i32 1, i32 %desc, i64 0, i32 %i, i1 true)
      %inc = add i32 %i, 1
      br label %flow
    flowcritedge:
      br label %flow
    flow:
      %i.next = phi i32 [ %inc, %body ], [ %i, %flowcritedge ]
      %flowcond = phi i1 [ false, %body ], [ true, %flowcritedge ]
      br i1 %flowcond, label %exit, label %latch
    latch:
      br label %header
    exit:
      ret void
    }
    declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
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
  EXPECT_FALSE(SawError)
      << "roadmap L432(b): DiamondFlattener::run()'s own whole-function "
         "validation walk must roll back its MergeVisited claims when it "
         "declines to flatten anything, instead of permanently poisoning "
         "LoopLinearizer's later, legitimate walk over the same blocks";
  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundResourceStore = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (CI && matchResourceCall(*CI))
      FoundResourceStore = true;
  }
  EXPECT_TRUE(FoundResourceStore);
}

// Roadmap L419(ac): distilled from `dEQP-VK.glsl.switch.loop_in_switch.
// while_loop_in_switch_dynamic_fragment`'s own reduction (the `switch`
// dispatch scaffolding this real case wraps the loop in is irrelevant to
// this specific bug and omitted entirely here -- `header`'s own divergent
// `CondBr` relaying through `Flow`, a shared merge block owning real,
// multi-valued phis, straight to `exit` is the whole repro). `Flow` plays
// the real case's own `Flow` block's role: `%merged`/`%flag` distinguish
// "this lane just took `header`'s own exit edge" from "this lane is
// still looping", exactly the shape `recoverRelayExitCheck`'s own
// generalized chase (roadmap L419(ab)) newly admits into
// `HeaderDivergent` handling. Before `freezeRelayChainExitValues`
// (roadmap L419(ac) proper), erasing `header`'s own `CondBr` orphaned
// `Flow`'s phis' `.Flow_crit_edge` contribution, silently collapsing
// `%merged` to whatever `body`'s own still-executing `%bodyval` computed
// on a later iteration once `EliminateUnreachableBlocks` ran, corrupting
// `exit`'s own stored value for a lane whose real per-lane trip count
// was already exhausted.
TEST(LinearizeTest, FreezesRelayChainExitValueThroughSharedDispatchBlock) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr addrspace(1) %out) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %bound = add i32 %tid, 1
      br label %header
    header:
      %val = phi <3 x float> [ %merged, %Flow._crit_edge ], [ zeroinitializer, %entry ]
      %iv = phi i32 [ %inc, %Flow._crit_edge ], [ 0, %entry ]
      %cond = icmp slt i32 %iv, %bound
      br i1 %cond, label %body, label %.Flow_crit_edge
    .Flow_crit_edge:
      br label %Flow
    body:
      %inc = add i32 %iv, 1
      %bodyval = fadd <3 x float> %val, <float 1.000000e+00, float 1.000000e+00, float 1.000000e+00>
      br label %Flow
    Flow:
      %merged = phi <3 x float> [ %bodyval, %body ], [ %val, %.Flow_crit_edge ]
      %flag = phi i1 [ false, %body ], [ true, %.Flow_crit_edge ]
      br i1 %flag, label %exit, label %Flow._crit_edge
    Flow._crit_edge:
      br label %header
    exit:
      %x = extractelement <3 x float> %merged, i64 0
      store float %x, ptr addrspace(1) %out, align 4
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,1,1" }
  )");
  ASSERT_TRUE(M);

  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Handle) = true;
      },
      &SawError);

  EXPECT_TRUE(run(*M));
  EXPECT_FALSE(SawError)
      << "header's own divergent branch, relayed through Flow straight to "
         "exit, should now be admitted into HeaderDivergent handling "
         "instead of being diagnosed as unsupported";
  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  BasicBlock *Flow = nullptr;
  BasicBlock *Header = nullptr;
  for (BasicBlock &BB : *F) {
    if (BB.getName() == "Flow")
      Flow = &BB;
    if (BB.getName() == "header")
      Header = &BB;
  }
  ASSERT_TRUE(Flow);
  ASSERT_TRUE(Header);

  // Roadmap L419(ac), fix (A): `Flow`'s own data phi must have been
  // replaced with a `select` on the per-lane "still looping" guard mask,
  // preserving the orphaned `.Flow_crit_edge` edge's own value (`%val`,
  // `header`'s raw loop-carried phi) for any lane that already exited,
  // instead of silently discarding it.
  bool FoundRelayFreezeSelect = false;
  for (Instruction &I : *Flow)
    if (auto *SI = dyn_cast<SelectInst>(&I))
      if (SI->getType()->isVectorTy() &&
          SI->getFalseValue() == &*Header->begin())
        FoundRelayFreezeSelect = true;
  EXPECT_TRUE(FoundRelayFreezeSelect)
      << "Flow's own data phi should be select-frozen against header's "
         "raw loop-carried phi, not left to silently collapse once its "
         "only predecessor from header is erased";

  // Roadmap L419(ac), fix (B): because the orphaned value above is itself
  // header's own raw phi (not a derived instruction), that phi's own
  // backedge must *also* be sticky/self-referential now (the same
  // pattern `freezeLoopCarriedValues` already applies elsewhere) -- a
  // one-shot select in `Flow` alone is not sound, since header's own phi
  // keeps mutating for every lane, every iteration, unless its own
  // backedge is frozen too.
  PHINode *ValPhi = cast<PHINode>(&*Header->begin());
  ASSERT_EQ(ValPhi->getName(), "val");
  bool FoundSelfReferentialFreeze = false;
  for (unsigned I = 0, E = ValPhi->getNumIncomingValues(); I != E; ++I) {
    auto *SI = dyn_cast<SelectInst>(ValPhi->getIncomingValue(I));
    if (SI && (SI->getTrueValue() == ValPhi || SI->getFalseValue() == ValPhi))
      FoundSelfReferentialFreeze = true;
  }
  EXPECT_TRUE(FoundSelfReferentialFreeze)
      << "header's own %val phi should be frozen via a self-referential "
         "select on its own backedge, not left to keep mutating for "
         "already-exited lanes";
}

// Roadmap L462: `header` is a genuinely trivial, single-successor
// pass-through block (only the induction `phi`, no `CondBr` of its own),
// with its real, uniform-looking trip-count check one hop forward in
// `check` -- the shape `findForwardExitCheckBlock`'s own "Roadmap
// L432(a)" fallback exists for. `check`'s condition reads a resource
// load whose mask operand is still the literal, compile-time constant
// `i1 true` `UniformityInfo`'s one-time, pre-masking snapshot sees (see
// "Roadmap L419(y)"'s own comment), so the forward-match guard
// ("Roadmap L432(c)", restricted to `!isDivergentBranch`) previously let
// it through as `HeaderExit` -- but `check` is also this cycle's *only*
// `OtherCondBrBlocks` member, so the "Roadmap L419(h)" widening
// (`cycleHasMaskOps(C) && OtherCondBrBlocks.size() == 1`) independently
// recognized the very same block as `CheckBlock` too, and both code
// paths erased and replaced the same, already-detached `CondBr`,
// asserting ("Instruction to insert before is not in a basic block!").
// Reduced from the real crash in `dEQP-VK.spirv_assembly.instruction.
// spirv1p4.loop_control.{iteration_multiple,max_iterations,
// min_iterations,partial_count,peel_count}`. See
// `loop-header-forward-check-is-sole-conditional-branch.ll` for the
// equivalent lit-level coverage.
TEST(LinearizeTest,
     DoesNotDoubleClaimSoleConditionalBranchAsBothHeaderAndCheckExit) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %heap, i32 %heap_count) #0 {
    entry:
      br label %header
    header:
      %i = phi i32 [0, %entry], [%inc, %latch]
      br label %check
    check:
      %val = call i32 @feme.cpu.resource.load.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i1 true)
      %cond = icmp slt i32 %i, %val
      br i1 %cond, label %body, label %exit
    body:
      %inc = add i32 %i, 1
      br label %latch
    latch:
      br label %header
    exit:
      ret void
    }
    declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  // The bug was an assertion failure (an aborted process), not a
  // diagnosable error, so the real regression coverage here is simply
  // that `run` returns without crashing and produces verifiable IR;
  // still check for an unexpected diagnostic as a secondary signal.
  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Handle) = true;
      },
      &SawError);

  EXPECT_TRUE(run(*M));
  EXPECT_FALSE(SawError) << "the sole forward exit check should be "
                             "claimed exactly once, not diagnosed";
  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  BasicBlock *Check = nullptr;
  for (BasicBlock &BB : *F)
    if (BB.getName() == "check")
      Check = &BB;
  ASSERT_TRUE(Check);
  // `check`'s own conditional branch is the cycle's single, genuine exit
  // check, so `CheckBlock`'s own handling (not the `HeaderExit`-specific
  // one) must be the sole owner of its rewrite: its terminator is now an
  // unconditional branch into the loop body (the real exit decision
  // lives in the `active.check.live` mask threaded through `latch`
  // instead), and its own resource load's mask operand must have been
  // narrowed to a genuine per-lane value rather than left as the
  // original constant `true` -- both only possible if the block's
  // original `CondBr` was mutated exactly once, not twice (crashing).
  EXPECT_TRUE(isa<UncondBrInst>(Check->getTerminator()));
  auto *Call = dyn_cast<CallInst>(&*Check->begin());
  ASSERT_TRUE(Call);
  EXPECT_FALSE(isa<ConstantInt>(Call->getArgOperand(4)))
      << "check's resource load should have been narrowed to a real "
         "per-lane mask, not left with its original constant-true operand";
}

// Roadmap L463: a minimized reduction of
// `dEQP-VK.graphicsfuzz.cov-loop-condition-divide-by-uniform-always-false`'s
// fragment shader -- two nested `for` loops where the inner loop's own
// body has a genuinely divergent `if` (guarding an `fdiv`-based
// comparison) that `DiamondFlattener::validate` cannot yet lower, so the
// inner loop is left alone with a clean "unsupported" diagnostic. The
// *outer* loop's own fold helpers (`foldRedundantFlowBlocksInCycle`,
// `peelConstantFlowPredecessorsInCycle`, `mergeTrivialRelayBlocksInCycle`)
// are scoped via `CycleInfo::contains`, which also matches every block of
// the still-nested inner cycle, and one of them merges the inner loop's
// own cached exit block (`LoopLinearizer::ExitBlocksByCycle`, "Roadmap
// L197") away as collateral damage. The outer loop's own `validate` walk
// then reaches the inner loop's header and, recognizing it as an
// already-attempted child cycle, reads that now-stale cache entry via
// `GetAlreadyLinearizedCycleExit` -- before this fix, a raw, dangling
// `BasicBlock *` into freed memory, asserting on `getTerminator()`. See
// `nested-loop-abandoned-child-exit-block-merged.ll` for the equivalent
// lit-level coverage.
TEST(LinearizeTest, DoesNotUseFreedChildCycleExitBlockAfterOuterLoopFold) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    declare i1 @llvm.is.fpclass.f32(float, i32 immarg)
    declare float @llvm.copysign.f32(float, float)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)
    declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1)

    define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #0 {
      %1 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
      %2 = sitofp i32 %1 to float
      call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %2, i32 0)
      br label %5
    5:
      %.05 = phi i32 [ 1, %0 ], [ %53, %Flow6._crit_edge ]
      %6 = icmp slt i32 %.05, 9
      br i1 %6, label %._crit_edge, label %.Flow6_crit_edge
    .Flow6_crit_edge:
      br label %Flow6
    ._crit_edge:
      br label %20
    common.ret:
      ret void
    7:
      %8 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
      %9 = sitofp i32 %8 to float
      call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %9, i32 0)
      br label %common.ret
    20:
      %.0 = phi i32 [ %31, %Flow._crit_edge ], [ 1, %._crit_edge ]
      %21 = icmp slt i32 %.0, 10
      br i1 %21, label %28, label %.Flow_crit_edge
    .Flow_crit_edge:
      br label %Flow
    22:
      %23 = add i32 %.05, 1
      br label %Flow7
    Flow4:
      %24 = phi i1 [ false, %34 ], [ true, %.Flow4_crit_edge ]
      %25 = phi i1 [ %48, %34 ], [ %30, %.Flow4_crit_edge ]
      br i1 %25, label %26, label %Flow4.Flow5_crit_edge
    Flow4.Flow5_crit_edge:
      br label %Flow5
    26:
      %27 = add i32 %.0, 1
      br label %Flow5
    28:
      %29 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
      %30 = icmp sgt i32 %.05, %29
      %.inv = xor i1 %30, true
      br i1 %.inv, label %34, label %.Flow4_crit_edge
    .Flow4_crit_edge:
      br label %Flow4
    Flow:
      %31 = phi i32 [ %51, %Flow5 ], [ poison, %.Flow_crit_edge ]
      %32 = phi i1 [ %24, %Flow5 ], [ true, %.Flow_crit_edge ]
      %33 = phi i1 [ %52, %Flow5 ], [ true, %.Flow_crit_edge ]
      br i1 %33, label %loop.exit.guard1, label %Flow._crit_edge
    Flow._crit_edge:
      br label %20
    34:
      %35 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
      %36 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 2, i64 0, i1 true)
      %37 = call i1 @llvm.is.fpclass.f32(float %35, i32 144)
      %38 = call float @llvm.copysign.f32(float 0.000000e+00, float %35)
      %39 = select i1 %37, float %38, float %35
      %40 = call i1 @llvm.is.fpclass.f32(float %36, i32 144)
      %41 = call float @llvm.copysign.f32(float 0.000000e+00, float %36)
      %42 = select i1 %40, float %41, float %36
      %43 = fdiv float %39, %42
      %44 = call i1 @llvm.is.fpclass.f32(float %43, i32 144)
      %45 = call float @llvm.copysign.f32(float 0.000000e+00, float %43)
      %46 = select i1 %44, float %45, float %43
      %47 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
      %48 = fcmp uge float %46, %47
      br label %Flow4
    Flow7:
      %49 = phi i32 [ %23, %22 ], [ poison, %loop.exit.guard1.Flow7_crit_edge ]
      %50 = phi i1 [ false, %22 ], [ true, %loop.exit.guard1.Flow7_crit_edge ]
      br label %Flow6
    loop.exit.guard:
      br i1 %54, label %7, label %loop.exit.guard.common.ret_crit_edge
    loop.exit.guard.common.ret_crit_edge:
      br label %common.ret
    Flow5:
      %51 = phi i32 [ %27, %26 ], [ poison, %Flow4.Flow5_crit_edge ]
      %52 = phi i1 [ false, %26 ], [ true, %Flow4.Flow5_crit_edge ]
      br label %Flow
    Flow6:
      %53 = phi i32 [ %49, %Flow7 ], [ poison, %.Flow6_crit_edge ]
      %54 = phi i1 [ false, %Flow7 ], [ true, %.Flow6_crit_edge ]
      %55 = phi i1 [ %50, %Flow7 ], [ true, %.Flow6_crit_edge ]
      br i1 %55, label %loop.exit.guard, label %Flow6._crit_edge
    Flow6._crit_edge:
      br label %5
    loop.exit.guard1:
      br i1 %32, label %22, label %loop.exit.guard1.Flow7_crit_edge
    loop.exit.guard1.Flow7_crit_edge:
      br label %Flow7
    }
    attributes #0 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="fragment" "hlsl.shader"="pixel" }
  )");
  ASSERT_TRUE(M);

  // The bug was an assertion failure (an aborted process) reached from
  // inside the inner loop's own clean, intentional "unsupported"
  // diagnostic path, so the regression coverage here is simply that
  // `run` returns without crashing, having emitted that one diagnostic
  // (not zero, not a crash) and otherwise valid IR.
  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Handle) = true;
      },
      &SawError);

  run(*M);
  EXPECT_TRUE(SawError) << "the inner loop's divergent-if body shape "
                            "should still be diagnosed as unsupported";
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

} // namespace
