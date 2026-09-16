//===- EntryWrapperTest.cpp - Tests for EntryWrapperPass ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/EntryWrapper.h"

#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/CPU/WaveLowering.h"
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
    Err.print("EntryWrapperTest", errs());
  return M;
}

TEST(EntryWrapperTest, GetEntrySymbolNameAddsPrefix) {
  EXPECT_EQ(getEntrySymbolName("main"), "feme_cpu_entry_main");
}

TEST(EntryWrapperTest, WrapsWidenedFunction) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %doubled = mul i32 %tid, 2
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);
  EXPECT_EQ(Wrapper->arg_size(), 1u);
  EXPECT_TRUE(Wrapper->getArg(0)->getType()->isPointerTy());

  Function *Body = M->getFunction("main");
  ASSERT_TRUE(Body);
  EXPECT_EQ(Body->getLinkage(), GlobalValue::InternalLinkage);

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(EntryWrapperTest, LeavesUnwidenedFunctionUnwrapped) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  EntryWrapperPass().run(*M, MAM);

  EXPECT_FALSE(M->getFunction("feme_cpu_entry_main"));
  EXPECT_TRUE(M->getFunction("main"));
}

// Roadmap milestone 9: groupshared allocation (see EntryWrapper.cpp's file
// comment). A small `addrspace(3)` global fits on the wrapper's own stack.
TEST(EntryWrapperTest, AllocatesSmallGroupSharedOnStack) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @shared = internal addrspace(3) global [4 x i32] undef
    define void @main() #0 {
      %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 0
      %val = load i32, ptr addrspace(3) %ptr
      %doubled = mul i32 %val, 2
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  EXPECT_FALSE(M->getGlobalVariable("shared"));
  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);
  bool FoundAlloca = false;
  for (Instruction &I : instructions(Wrapper))
    if (auto *AI = dyn_cast<AllocaInst>(&I)) {
      FoundAlloca = true;
      EXPECT_EQ(AI->getAllocatedType(),
                ArrayType::get(Type::getInt8Ty(Ctx), 16));
    }
  EXPECT_TRUE(FoundAlloca);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap milestone 9: a `..._with_group_sync` barrier splits the wave
// body into two regions, each wrapped in its own wave loop.
TEST(EntryWrapperTest, SplitsAtGroupSyncBarrier) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @shared = internal addrspace(3) global [4 x i32] undef
    define void @main() #0 {
      %gid = call i32 @llvm.dx.group.id(i32 0)
      %ptr0 = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 0
      store i32 %gid, ptr addrspace(3) %ptr0
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %ptr1 = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 1
      %val = load i32, ptr addrspace(3) %ptr1
      %doubled = mul i32 %val, 2
      ret void
    }
    declare i32 @llvm.dx.group.id(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("main.region0"));
  EXPECT_TRUE(M->getFunction("main"));

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);
  unsigned NumWaveLoopHeaders = 0;
  bool FoundFence = false;
  for (BasicBlock &BB : *Wrapper) {
    if (BB.getName().starts_with("wave.loop.header"))
      ++NumWaveLoopHeaders;
    for (Instruction &I : BB)
      if (isa<FenceInst>(&I))
        FoundFence = true;
  }
  EXPECT_EQ(NumWaveLoopHeaders, 2u);
  EXPECT_TRUE(FoundFence);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap milestone 9: a barrier with no group-sync requirement becomes an
// in-place `fence`, needing no region split.
TEST(EntryWrapperTest, MemoryOnlyBarrierBecomesFenceWithoutSplitting) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      call void @llvm.dx.device.memory.barrier()
      %doubled = mul i32 %tid, 2
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @llvm.dx.device.memory.barrier()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  EXPECT_FALSE(M->getFunction("main.region0"));
  Function *Body = M->getFunction("main");
  ASSERT_TRUE(Body);
  bool FoundFence = false;
  for (Instruction &I : instructions(Body))
    if (isa<FenceInst>(&I))
      FoundFence = true;
  EXPECT_TRUE(FoundFence);

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);
  unsigned NumWaveLoopHeaders = 0;
  for (BasicBlock &BB : *Wrapper)
    if (BB.getName().starts_with("wave.loop.header"))
      ++NumWaveLoopHeaders;
  EXPECT_EQ(NumWaveLoopHeaders, 1u);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap step R24 (feme/docs/Roadmap.md): a `..._with_group_sync` barrier
// inside a uniform (group-id-derived) surviving branch is split rather
// than diagnosed -- see "Barrier inside a surviving branch" in
// EntryWrapper.cpp's file comment. The branch's own uniform condition is
// cloned into the wrapper as an ordinary scalar `br`; only the arm with a
// barrier (`a`) is split into regions, the other (`b`) keeps one.
TEST(EntryWrapperTest, SplitsBarrierInsideUniformBranch) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      %gid = call i32 @llvm.dx.group.id(i32 0)
      %cond = icmp eq i32 %gid, 0
      br i1 %cond, label %a, label %b
    a:
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      br label %exit
    b:
      br label %exit
    exit:
      ret void
    }
    declare i32 @llvm.dx.group.id(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);
  EXPECT_TRUE(M->getFunction("main.true.body0"));
  EXPECT_TRUE(M->getFunction("main.true.body1"));
  EXPECT_TRUE(M->getFunction("main.false.body0"));

  bool FoundCondBr = false, FoundFence = false;
  for (BasicBlock &BB : *Wrapper) {
    if (BB.getName() == "branch.cond")
      if (isa<CondBrInst>(BB.getTerminator()))
        FoundCondBr = true;
    for (Instruction &I : BB)
      if (isa<FenceInst>(&I))
        FoundFence = true;
  }
  EXPECT_TRUE(FoundCondBr);
  EXPECT_TRUE(FoundFence);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap step R24's remaining narrowing: a merge block with a phi (a
// value one arm of the branch computes differently from the other) is
// still diagnosed -- threading it would mean spilling across the
// wrapper's own scalar branch choice, which this milestone's spilling
// does not support.
TEST(EntryWrapperTest, BranchMergePhiIsDiagnosed) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      %gid = call i32 @llvm.dx.group.id(i32 0)
      %cond = icmp eq i32 %gid, 0
      br i1 %cond, label %a, label %b
    a:
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      br label %exit
    b:
      br label %exit
    exit:
      %val = phi i32 [ 1, %a ], [ 2, %b ]
      %doubled = mul i32 %val, 2
      ret void
    }
    declare i32 @llvm.dx.group.id(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  EXPECT_FALSE(M->getFunction("feme_cpu_entry_main"));
}

// Roadmap H155 (feme/docs/Roadmap.md, real-world case
// `WaveOps/GroupMemoryBarrierWithGroupSync.test`): a barrier sitting in the
// loop's own prefix chain, before the loop even starts, is now split into
// its own region(s) exactly like `Shape.BodyOrder`, rather than declined --
// `main.prefix0`/`main.prefix1` are each run as their own per-wave loop,
// with a fence between them, before the wrapper's scalar loop begins.
TEST(EntryWrapperTest, LoopWithBarrierInPrefixIsSplit) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      br label %header
    header:
      %i = phi i32 [ 0, %entry ], [ %i.next, %flow ]
      %cmp = icmp ult i32 %i, 4
      br i1 %cmp, label %flow, label %after
    flow:
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %i.next = add i32 %i, 1
      br label %header
    after:
      ret void
    }
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  ASSERT_TRUE(M->getFunction("feme_cpu_entry_main"));
  EXPECT_TRUE(M->getFunction("main.prefix0"));
  EXPECT_TRUE(M->getFunction("main.prefix1"));
  EXPECT_TRUE(M->getFunction("main.body0"));
  EXPECT_TRUE(M->getFunction("main.suffix0"));
}

// Roadmap H155: a barrier sitting in the loop's own suffix chain, after the
// loop's own exit, is likewise split into its own region(s) rather than
// declined -- `main.suffix0`/`main.suffix1` are each run as their own
// per-wave loop, with a fence between them, after the wrapper's scalar
// loop ends.
TEST(EntryWrapperTest, LoopWithBarrierInSuffixIsSplit) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      br label %header
    header:
      %i = phi i32 [ 0, %entry ], [ %i.next, %flow ]
      %cmp = icmp ult i32 %i, 4
      br i1 %cmp, label %flow, label %after
    flow:
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %i.next = add i32 %i, 1
      br label %header
    after:
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %gid = call i32 @llvm.dx.group.id(i32 0)
      %doubled = mul i32 %gid, 2
      ret void
    }
    declare i32 @llvm.dx.group.id(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  ASSERT_TRUE(M->getFunction("feme_cpu_entry_main"));
  EXPECT_TRUE(M->getFunction("main.prefix0"));
  EXPECT_TRUE(M->getFunction("main.body0"));
  EXPECT_TRUE(M->getFunction("main.body1"));
  EXPECT_TRUE(M->getFunction("main.suffix0"));
  EXPECT_TRUE(M->getFunction("main.suffix1"));
}

// Roadmap L45 (feme/docs/Roadmap.md): a uniform two-way branch whose arms
// are each barrier-free and reconverge at a merge block with its own phi
// -- entirely outside every `..._with_group_sync` barrier's own region --
// is kept intact inside whichever region contains it rather than being
// diagnosed by `isLinearChain`'s straight-chain check, since it can never
// itself need a region split. Here the single barrier sits before the
// branch, so the whole diamond (condition, both arms, and the merge phi)
// lands in the function's own final region (`main` itself, reused for the
// last region -- see `splitAtGroupSyncBarriers`'s doc comment).
TEST(EntryWrapperTest, SplitsAroundSafeDiamondAfterBarrier) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      %gid = call i32 @llvm.dx.group.id(i32 0)
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %cond = icmp eq i32 %gid, 0
      br i1 %cond, label %a, label %b
    a:
      %vala = add i32 %gid, 10
      br label %exit
    b:
      br label %exit
    exit:
      %val = phi i32 [ %vala, %a ], [ 0, %b ]
      %doubled = mul i32 %val, 2
      ret void
    }
    declare i32 @llvm.dx.group.id(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("main.region0"));
  Function *Body = M->getFunction("main");
  ASSERT_TRUE(Body);
  bool FoundCondBr = false, FoundPhi = false;
  for (Instruction &I : instructions(Body)) {
    if (isa<CondBrInst>(&I))
      FoundCondBr = true;
    if (isa<PHINode>(&I))
      FoundPhi = true;
  }
  EXPECT_TRUE(FoundCondBr);
  EXPECT_TRUE(FoundPhi);

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);
  unsigned NumWaveLoopHeaders = 0;
  bool FoundFence = false;
  for (BasicBlock &BB : *Wrapper) {
    if (BB.getName().starts_with("wave.loop.header"))
      ++NumWaveLoopHeaders;
    for (Instruction &I : BB)
      if (isa<FenceInst>(&I))
        FoundFence = true;
  }
  EXPECT_EQ(NumWaveLoopHeaders, 2u);
  EXPECT_TRUE(FoundFence);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap L7m: the same "safe diamond" shape as
// `SplitsAroundSafeDiamondAfterBarrier` above, but with the single barrier
// *after* the diamond instead of before it (so the diamond's blocks must
// be spliced into the function's *first* region, not its last) -- and,
// crucially, with the diamond's true-arm block placed *textually last* in
// the source (so it is also last in the parsed function's *physical*
// block-list order), after both the merge block and the block containing
// the barrier. `splitAtGroupSyncBarriers`'s region-bucketing previously
// walked the function's raw physical block-list order to decide which
// blocks belong to which region, silently assuming that order always
// matched the diamond's actual logical/execution order; here it does not,
// and the true-arm block (physically last) used to get bucketed into the
// *second* region (alongside the barrier's own boundary block) instead of
// the first, leaving `main.region0`'s branch to the true arm dangling --
// a real, previously-uncaught `llvm::DeleteDeadBlocks` assertion crash
// once the standard LLVM optimizer pipeline ran over the resulting
// module (see this row's own citation in feme/docs/Roadmap.md).
TEST(EntryWrapperTest, SplitsSafeDiamondWithOutOfOrderTrueArmBeforeBarrier) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      %gid = call i32 @llvm.dx.group.id(i32 0)
      %cond = icmp eq i32 %gid, 0
      br i1 %cond, label %a, label %exit
    exit:
      %val = phi i32 [ %vala, %a ], [ 0, %entry ]
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %doubled = mul i32 %val, 2
      ret void
    a:
      %vala = add i32 %gid, 10
      br label %exit
    }
    declare i32 @llvm.dx.group.id(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  Function *Region0 = M->getFunction("main.region0");
  ASSERT_TRUE(Region0);
  Function *Region1 = M->getFunction("main");
  ASSERT_TRUE(Region1);

  // The diamond (condition, both arms, and the merge phi) must all have
  // landed together in the *first* region, ahead of the barrier -- not
  // split apart by a stale, physical-block-list-order assumption.
  bool FoundCondBr = false, FoundPhi = false, FoundAdd = false;
  for (Instruction &I : instructions(Region0)) {
    if (isa<CondBrInst>(&I))
      FoundCondBr = true;
    if (isa<PHINode>(&I))
      FoundPhi = true;
    if (auto *BO = dyn_cast<BinaryOperator>(&I);
        BO && BO->getOpcode() == Instruction::Add)
      FoundAdd = true;
  }
  EXPECT_TRUE(FoundCondBr);
  EXPECT_TRUE(FoundPhi);
  EXPECT_TRUE(FoundAdd);

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);
  unsigned NumWaveLoopHeaders = 0;
  bool FoundFence = false;
  for (BasicBlock &BB : *Wrapper) {
    if (BB.getName().starts_with("wave.loop.header"))
      ++NumWaveLoopHeaders;
    for (Instruction &I : BB)
      if (isa<FenceInst>(&I))
        FoundFence = true;
  }
  EXPECT_EQ(NumWaveLoopHeaders, 2u);
  EXPECT_TRUE(FoundFence);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap L45: a genuinely unsafe diamond -- one whose arm itself
// contains a `..._with_group_sync` barrier, *and* whose merge block has a
// phi -- is still diagnosed: `matchBranchShape` declines it (a merge phi
// needs threading a value across the wrapper's own scalar branch choice,
// see `BranchMergePhiIsDiagnosed` above), and `isLinearChain`'s own new
// "safe diamond" case declines it too, since the arm containing the
// barrier is not itself barrier-free (`walkBarrierFreeArm`) -- so this
// case correctly falls through to the pre-existing diagnostic rather
// than being (unsoundly) accepted as if it were the safe shape above.
TEST(EntryWrapperTest, BarrierInsideDiamondArmWithMergePhiStillDiagnosed) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      %gid = call i32 @llvm.dx.group.id(i32 0)
      %cond = icmp eq i32 %gid, 0
      br i1 %cond, label %a, label %b
    a:
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %vala = add i32 %gid, 10
      br label %exit
    b:
      br label %exit
    exit:
      %val = phi i32 [ %vala, %a ], [ 0, %b ]
      %doubled = mul i32 %val, 2
      ret void
    }
    declare i32 @llvm.dx.group.id(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  EXPECT_FALSE(M->getFunction("feme_cpu_entry_main"));
}

// Roadmap step R5 (feme/docs/Roadmap.md): a divergent (per-lane) value
// computed before a `..._with_group_sync` barrier and used after it is
// spilled to a per-wave context array rather than being diagnosed -- see
// "Values live across a barrier" in EntryWrapper.cpp's file comment.
TEST(EntryWrapperTest, SpillsValueLiveAcrossGroupSyncBarrier) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %doubled = mul i32 %tid, 2
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);

  Function *Region0 = M->getFunction("main.region0");
  Function *Region1 = M->getFunction("main");
  ASSERT_TRUE(Region0);
  ASSERT_TRUE(Region1);
  EXPECT_TRUE(Region0->getArg(Region0->arg_size() - 1)->getName() ==
              "barrier_spill");
  EXPECT_TRUE(Region1->getArg(Region1->arg_size() - 1)->getName() ==
              "barrier_spill");

  bool FoundStore = false, FoundLoad = false;
  for (Instruction &I : instructions(Region0))
    FoundStore |= isa<StoreInst>(&I);
  for (Instruction &I : instructions(Region1))
    FoundLoad |= isa<LoadInst>(&I);
  EXPECT_TRUE(FoundStore);
  EXPECT_TRUE(FoundLoad);

  bool FoundSpillAlloca = false;
  for (Instruction &I : instructions(Wrapper))
    if (auto *AI = dyn_cast<AllocaInst>(&I))
      if (AI->getName() == "barrier.spill")
        FoundSpillAlloca = true;
  EXPECT_TRUE(FoundSpillAlloca);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap step R5: a `..._with_group_sync` barrier inside a uniform loop
// (a stride-halving reduction loop's shape) is split, not diagnosed -- see
// "Barriers inside a uniform loop" in EntryWrapper.cpp's file comment. The
// loop's own induction variable (`stride`) is hoisted into the wrapper as
// an ordinary scalar loop; the barrier-split body regions run once per
// wave, once per iteration.
TEST(EntryWrapperTest, SplitsBarrierInsideUniformLoop) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      br label %loop.header
    loop.header:
      %stride = phi i32 [ 2, %entry ], [ %stride.next, %loop.latch ]
      %cond = icmp ugt i32 %stride, 0
      br i1 %cond, label %loop.body, label %loop.exit
    loop.body:
      %gid = call i32 @llvm.dx.group.id(i32 0)
      %sum = add i32 %gid, %stride
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %doubled = mul i32 %sum, 2
      br label %loop.latch
    loop.latch:
      %stride.next = lshr i32 %stride, 1
      br label %loop.header
    loop.exit:
      ret void
    }
    declare i32 @llvm.dx.group.id(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);
  EXPECT_FALSE(M->getFunction("main"));
  EXPECT_TRUE(M->getFunction("main.body0"));

  bool FoundWrapperLoop = false, FoundWrapperPhi = false, FoundFence = false;
  unsigned NumWaveLoopHeaders = 0;
  for (BasicBlock &BB : *Wrapper) {
    if (BB.getName() == "loop.header")
      FoundWrapperLoop = true;
    if (BB.getName().starts_with("wave.loop.header"))
      ++NumWaveLoopHeaders;
    for (Instruction &I : BB) {
      if (auto *PN = dyn_cast<PHINode>(&I))
        if (PN->getName().starts_with("loopvar"))
          FoundWrapperPhi = true;
      if (isa<FenceInst>(&I))
        FoundFence = true;
    }
  }
  EXPECT_TRUE(FoundWrapperLoop);
  EXPECT_TRUE(FoundWrapperPhi);
  // A single barrier splits the loop body into 2 regions (before/after);
  // the (trivial) prefix and suffix chains each get their own wave loop
  // too, for 4 total.
  EXPECT_EQ(NumWaveLoopHeaders, 4u);
  EXPECT_TRUE(FoundFence);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap H124e(a) (feme/docs/Roadmap.md): a "Flow-merge loop" whose
// structured-control-flow latch collapsed into the same physical block as
// its own barrier region (no separate `BodyOrder` block survives a
// preceding `JumpThreadingPass` run) -- `matchLoopShape` must split that
// one block right after its barrier's last occurrence into a `BodyOrder`
// entry (`flow`'s own barrier-and-earlier half) and a fresh pure-
// recurrence `Latch` (its tail), rather than declining the shape
// outright. `%hv` (computed in `header`, used only in the barrier-and-
// earlier half of `flow`) exercises `HeaderDerivedValues` threading: it
// must be passed through as its own `loopvarN` trailing parameter, not
// just the genuine induction `%i`.
TEST(EntryWrapperTest, SplitsFlowMergeLoopWithHeaderDerivedValue) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      br label %header
    header:
      %i = phi i32 [ 0, %entry ], [ %i.next, %flow ]
      %hv = xor i32 %i, 1
      %cmp = icmp ult i32 %i, 4
      br i1 %cmp, label %flow, label %after
    flow:
      %gid = call i32 @llvm.dx.group.id(i32 0)
      %use = add i32 %hv, %gid
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %i.next = add i32 %i, 1
      br label %header
    after:
      ret void
    }
    declare i32 @llvm.dx.group.id(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);
  EXPECT_FALSE(M->getFunction("main"));
  Function *Body0 = M->getFunction("main.body0");
  ASSERT_TRUE(Body0);

  // `%i` (loopvar0) is the genuine induction; `%hv` (loopvar1) is the
  // extra `HeaderDerivedValues` entry this shape specifically exercises
  // -- it must show up as its own trailing `loopvarN` parameter on the
  // outlined barrier region, not just the induction.
  bool FoundHeaderDerivedParam = false;
  for (Argument &A : Body0->args())
    if (A.getName() == "loopvar1")
      FoundHeaderDerivedParam = true;
  EXPECT_TRUE(FoundHeaderDerivedParam);

  bool FoundFence = false;
  unsigned NumWaveLoopHeaders = 0;
  for (BasicBlock &BB : *Wrapper) {
    if (BB.getName().starts_with("wave.loop.header"))
      ++NumWaveLoopHeaders;
    for (Instruction &I : BB) {
      if (isa<FenceInst>(&I))
        FoundFence = true;
    }
  }
  // `%i` (loopvar0) is the genuine induction; `%hv` (loopvar1) is the
  // extra `HeaderDerivedValues` entry this shape specifically exercises.
  // The collapsed block splits into a `BodyOrder` entry (before the
  // barrier) and a fresh `Latch` (the pure-recurrence tail); together
  // with the (trivial) prefix and suffix chains, that's 4 wave loops.
  EXPECT_EQ(NumWaveLoopHeaders, 4u);
  EXPECT_TRUE(FoundFence);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap H154 (feme/docs/Roadmap.md): like
// `SplitsFlowMergeLoopWithHeaderDerivedValue` above, but with a genuine
// (non-collapsed) extra body block (`mid`) preceding the barrier-
// containing latch block (`flow`) -- i.e. `Shape.BodyOrder` is already
// non-empty for an ordinary reason before the latch-splitting logic even
// runs. `matchLoopShape`'s `LatchSplitAfter` gating must key off "does
// `Shape.Latch` contain a barrier", not "is `Shape.BodyOrder` empty" (the
// latter would previously not even attempt a split here, and decline via
// `isPureClosedChain` finding the barrier's own side effect), so this
// must still succeed, appending `flow`'s own barrier-and-earlier half as
// a second `BodyOrder` entry alongside `mid`.
TEST(EntryWrapperTest, SplitsFlowMergeLoopWithNonEmptyBodyBeforeLatchBarrier) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      br label %header
    header:
      %i = phi i32 [ 0, %entry ], [ %i.next, %flow ]
      %cmp = icmp ult i32 %i, 4
      br i1 %cmp, label %mid, label %after
    mid:
      %gid = call i32 @llvm.dx.group.id(i32 0)
      br label %flow
    flow:
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %i.next = add i32 %i, 1
      br label %header
    after:
      ret void
    }
    declare i32 @llvm.dx.group.id(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);
  EXPECT_FALSE(M->getFunction("main"));
  // `mid` and `flow`'s own barrier-and-earlier half are two separate
  // `BodyOrder` entries.
  EXPECT_TRUE(M->getFunction("main.body0"));
  EXPECT_TRUE(M->getFunction("main.body1"));

  bool FoundFence = false;
  unsigned NumWaveLoopHeaders = 0;
  for (BasicBlock &BB : *Wrapper) {
    if (BB.getName().starts_with("wave.loop.header"))
      ++NumWaveLoopHeaders;
    for (Instruction &I : BB)
      if (isa<FenceInst>(&I))
        FoundFence = true;
  }
  // 4 wave loops total: `mid` and `flow`'s barrier-and-earlier half are
  // two `BodyOrder` regions, plus the fresh post-barrier `Latch` tail;
  // the (trivial) prefix and suffix chains don't need their own wave
  // loop since `SIMDizePass` already fully widens their simple scalar
  // content without further per-wave iteration.
  EXPECT_EQ(NumWaveLoopHeaders, 4u);
  EXPECT_TRUE(FoundFence);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap H158 (feme/docs/Roadmap.md): a "Flow-merge loop" whose body
// block ends in a uniform `CondBr` forming a "safe diamond" (both arms
// barrier-free, reconverging at the collapsed barrier+latch block)
// instead of an unconditional branch straight into it. Such a diamond
// can never need a region split of its own -- it always lands entirely
// inside whichever single region contains it -- but outlining that
// region used to assert-fail, because
// `outlineChainAtBarriers`/`rebuildSplitChainOrder` assumed every block
// of a chain but the last ends in an `UncondBrInst`. Both the chain
// rebuild and `matchLoopShape`'s own body walk now route a `CondBr`
// through `matchSafeDiamond`, exactly as `isLinearChain` already did for
// the identical shape on the non-loop straight-line path, so this shape
// is accepted and its whole diamond outlined into one region function.
TEST(EntryWrapperTest, SplitsFlowMergeLoopWithMidBodySafeDiamond) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      br label %header
    header:
      %i = phi i32 [ 0, %entry ], [ %i.next, %flow ]
      %cmp = icmp ult i32 %i, 4
      br i1 %cmp, label %body, label %after
    body:
      %gid = call i32 @llvm.dx.group.id(i32 0)
      %cond2 = icmp ugt i32 %i, 0
      br i1 %cond2, label %then, label %flow
    then:
      %tmp = add i32 %gid, 1
      br label %flow
    flow:
      %merged = phi i32 [ %gid, %body ], [ %tmp, %then ]
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %i.next = add i32 %i, 1
      br label %header
    after:
      ret void
    }
    declare i32 @llvm.dx.group.id(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  ASSERT_TRUE(M->getFunction("feme_cpu_entry_main"));
  EXPECT_FALSE(M->getFunction("main"));

  // The whole diamond -- header, both arms and the merge point -- lands
  // in one region function, with its own branch intact.
  Function *Body0 = M->getFunction("main.body0");
  ASSERT_TRUE(Body0);
  EXPECT_EQ(Body0->size(), 3u);
  bool FoundInternalBranch = false;
  for (BasicBlock &BB : *Body0)
    if (BB.getTerminator()->getNumSuccessors() == 2)
      FoundInternalBranch = true;
  EXPECT_TRUE(FoundInternalBranch);

  // The barrier still splits the loop body after the diamond.
  EXPECT_TRUE(M->getFunction("main.body1"));
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap H159(b) (feme/docs/Roadmap.md): a "Flow-merge loop" (see
// `SplitsFlowMergeLoopWithHeaderDerivedValue` above) with a *second*
// header phi (`%acc`) whose own recurrence (`%acc.next`) is computed in
// the collapsed block's barrier-and-earlier half -- i.e. it ends up
// inside the outlined `BodyOrder` region, not the fresh post-barrier
// `Latch` tail, so its value is never available to the wrapper's own
// cloned scalar loop at all. `%acc` must therefore be recognized as a
// wave-persistent induction: seeded into its own per-wave spill slot once
// by the loop's prefix region, reloaded at its use inside the body
// region, and stored back right after its recurrence -- the spill array
// is allocated outside the wrapper's loop, so the slot carries the value
// across the loop's own backedge. `%i` stays an ordinary scalar
// induction driving the wrapper's trip count.
TEST(EntryWrapperTest, SplitsFlowMergeLoopWithWavePersistentRecurrence) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      br label %header
    header:
      %i = phi i32 [ 0, %entry ], [ %i.next, %flow ]
      %acc = phi i32 [ 0, %entry ], [ %acc.next, %flow ]
      %cmp = icmp ult i32 %i, 4
      br i1 %cmp, label %flow, label %after
    flow:
      %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
      %acc.next = add i32 %acc, %tid
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %i.next = add i32 %i, 1
      br label %header
    after:
      ret void
    }
    declare i32 @llvm.dx.thread.id.in.group(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);
  EXPECT_FALSE(M->getFunction("main"));

  // Only `%i` is an ordinary induction, so exactly one `loopvarN`
  // parameter is threaded through: `%acc` needs none.
  Function *Body0 = M->getFunction("main.body0");
  ASSERT_TRUE(Body0);
  unsigned NumLoopVars = 0;
  for (Argument &A : Body0->args())
    if (A.getName().starts_with("loopvar"))
      ++NumLoopVars;
  EXPECT_EQ(NumLoopVars, 1u);

  // The prefix region seeds the slot once per wave; the body region
  // reloads it and stores the recurrence back into it. `%acc` is per-lane
  // once widened, so the slot's own type is the widened vector type.
  Function *Prefix0 = M->getFunction("main.prefix0");
  ASSERT_TRUE(Prefix0);
  unsigned NumSeedStores = 0;
  for (Instruction &I : instructions(*Prefix0))
    if (isa<StoreInst>(&I))
      ++NumSeedStores;
  EXPECT_EQ(NumSeedStores, 1u);

  bool FoundReload = false;
  bool FoundCarryStore = false;
  for (Instruction &I : instructions(*Body0)) {
    if (auto *LI = dyn_cast<LoadInst>(&I);
        LI && LI->getName().starts_with("acc."))
      FoundReload = true;
    if (auto *SI = dyn_cast<StoreInst>(&I);
        SI && SI->getPointerOperand()->getName().starts_with("acc."))
      FoundCarryStore = true;
  }
  EXPECT_TRUE(FoundReload);
  EXPECT_TRUE(FoundCarryStore);

  // The wrapper's own scalar loop keeps a single phi, for `%i` alone.
  BasicBlock *HeaderBB = nullptr;
  for (BasicBlock &BB : *Wrapper)
    if (BB.getName() == "loop.header")
      HeaderBB = &BB;
  ASSERT_TRUE(HeaderBB);
  EXPECT_EQ(std::distance(HeaderBB->phis().begin(), HeaderBB->phis().end()), 1);

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap H159(b) (feme/docs/Roadmap.md): the wave-persistent induction
// path threads a per-lane loop-carried value through one per-wave slot,
// which holds exactly one value at a time -- so a use of the induction
// positioned *after* its own recurrence has been stored back would read
// the next iteration's value instead of this one's. That ordering
// requirement is checked, so this shape (`%late` reads `%acc` after
// `%acc.next` computes it) must be declined rather than miscompiled.
TEST(EntryWrapperTest, FlowMergeLoopWithLateWavePersistentUseIsDiagnosed) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      br label %header
    header:
      %i = phi i32 [ 0, %entry ], [ %i.next, %flow ]
      %acc = phi i32 [ 0, %entry ], [ %acc.next, %flow ]
      %cmp = icmp ult i32 %i, 4
      br i1 %cmp, label %flow, label %after
    flow:
      %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
      %acc.next = add i32 %acc, %tid
      %late = mul i32 %acc, 3
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %i.next = add i32 %i, 1
      br label %header
    after:
      ret void
    }
    declare i32 @llvm.dx.thread.id.in.group(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  EXPECT_FALSE(M->getFunction("feme_cpu_entry_main"));
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap H159(a) (feme/docs/Roadmap.md): a "Flow-merge loop" whose latch
// tail is *not* a pure scalar recurrence -- it references `%gid`, a value
// defined in the latch's own pre-barrier (per-wave, SIMD-widened) half,
// which simply does not exist in the wrapper's group-wide scalar latch
// block. Rather than declining the shape, `matchLoopShape` must mark the
// latch `LatchIsWaveRegion` and `buildWrapperForLoop` must outline it as
// the loop body's own last per-wave region instead of cloning it. The
// induction's recurrence (`%i.next`) lives in the header here, so it is
// still available to the wrapper's own scalar loop without any
// wave-persistent spilling (that is H159(b)).
TEST(EntryWrapperTest, SplitsFlowMergeLoopWithWaveSpecificLatch) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      br label %header
    header:
      %i = phi i32 [ 0, %entry ], [ %i.next, %flow ]
      %i.next = add i32 %i, 1
      %cmp = icmp ult i32 %i, 4
      br i1 %cmp, label %flow, label %after
    flow:
      %gid = call i32 @llvm.dx.thread.id.in.group(i32 0)
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 0
      %val = load i32, ptr addrspace(3) %ptr
      %use = add i32 %val, %gid
      %use2 = mul i32 %use, %i
      br label %header
    after:
      ret void
    }
    @shared = internal addrspace(3) global [4 x i32] undef
    declare i32 @llvm.dx.thread.id.in.group(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);
  EXPECT_FALSE(M->getFunction("main"));
  // The latch is outlined as this loop's own second body region, right
  // alongside its pre-barrier half -- not cloned into the wrapper.
  Function *Body0 = M->getFunction("main.body0");
  Function *Body1 = M->getFunction("main.body1");
  ASSERT_TRUE(Body0);
  ASSERT_TRUE(Body1);

  // The outlined latch still reads the induction (directly, or via the
  // header-local splat `feme::cpu::SIMDizePass` derives from it), so
  // those uses must have been rewritten to one of the region's own
  // trailing `loopvarN` parameters -- a value defined in a function it no
  // longer belongs to would not survive verification.
  bool UsesLoopVar = false;
  for (Argument &A : Body1->args())
    if (A.getName().starts_with("loopvar") && !A.use_empty())
      UsesLoopVar = true;
  EXPECT_TRUE(UsesLoopVar);

  // Nothing of the latch survives in the wrapper itself: its own
  // `loop.latch` block holds only the backedge branch.
  BasicBlock *LatchBB = nullptr;
  for (BasicBlock &BB : *Wrapper)
    if (BB.getName() == "loop.latch")
      LatchBB = &BB;
  ASSERT_TRUE(LatchBB);
  EXPECT_EQ(LatchBB->size(), 1u);

  bool FoundFence = false;
  for (Instruction &I : instructions(*Wrapper))
    if (isa<FenceInst>(&I))
      FoundFence = true;
  EXPECT_TRUE(FoundFence);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap H94b (feme/docs/Roadmap.md): a barrier-free loop (roadmap H72)
// whose real closing decision is reached one level deeper than the arm's
// own final branch -- the shape `feme::cpu::SIMDizePass`'s widening of a
// divergent-trip-count loop (roadmap milestone 4) produces, with an
// outer, uniform trip-count check (`header`) whose body arm passes
// through an extra block (`mid`) before that block's own `CondBr` closes
// the loop back to `header`, and the arm's remaining block (`latch`) only
// reached on the other, fresh side of that branch. `walkBarrierFreeArm`
// must tolerate a `CondBr` mid-arm, not just at the arm's own final
// terminator, when exactly one of its two successors is already an
// established block. The whole loop is barrier-free (the single barrier
// sits in `entry`, before it), so it must be kept intact -- not
// diagnosed -- exactly like `SplitsAroundSafeDiamondAfterBarrier`'s own
// diamond case above.
TEST(EntryWrapperTest, SplitsBarrierFreeLoopWithNestedCondBr) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      %gid = call i32 @llvm.dx.group.id(i32 0)
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      br label %header
    header:
      %i = phi i32 [ 0, %entry ], [ %i, %mid ], [ %i.next, %latch ]
      %cmp = icmp ult i32 %i, 4
      br i1 %cmp, label %body, label %after
    body:
      br label %mid
    mid:
      %any = icmp ne i32 %gid, %i
      br i1 %any, label %header, label %latch
    latch:
      %i.next = add i32 %i, 1
      br label %header
    after:
      ret void
    }
    declare i32 @llvm.dx.group.id(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  // Not diagnosed: the loop (all of `header`/`body`/`mid`/`latch`) is
  // barrier-free, so it stays intact in `main` (the function reused for
  // the last region), same as a safe diamond would.
  Function *Body = M->getFunction("main");
  ASSERT_TRUE(Body);
  EXPECT_TRUE(M->getFunction("main.region0"));
  bool FoundLoopHeader = false, FoundMidCondBr = false;
  for (BasicBlock &BB : *Body) {
    if (BB.getName() == "header")
      FoundLoopHeader = true;
    if (BB.getName() == "mid" && isa<CondBrInst>(BB.getTerminator()))
      FoundMidCondBr = true;
  }
  EXPECT_TRUE(FoundLoopHeader);
  EXPECT_TRUE(FoundMidCondBr);

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);
  unsigned NumWaveLoopHeaders = 0;
  bool FoundFence = false;
  for (BasicBlock &BB : *Wrapper) {
    if (BB.getName().starts_with("wave.loop.header"))
      ++NumWaveLoopHeaders;
    for (Instruction &I : BB)
      if (isa<FenceInst>(&I))
        FoundFence = true;
  }
  EXPECT_EQ(NumWaveLoopHeaders, 2u);
  EXPECT_TRUE(FoundFence);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap step R24 (feme/docs/Roadmap.md): a `phi` live across a
// `..._with_group_sync` barrier is spilled exactly like any other value
// (see "A `phi` live across a barrier" in EntryWrapper.cpp's file
// comment), rather than being diagnosed. Its spill store goes after the
// block's own last phi rather than immediately after itself.
TEST(EntryWrapperTest, SpillsPhiLiveAcrossGroupSyncBarrier) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      br label %next
    next:
      %val = phi i32 [ %tid, %entry ]
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %doubled = mul i32 %val, 2
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);

  Function *Region0 = M->getFunction("main.region0");
  Function *Region1 = M->getFunction("main");
  ASSERT_TRUE(Region0);
  ASSERT_TRUE(Region1);

  bool FoundPhi = false, FoundStoreAfterPhi = false, FoundLoad = false;
  for (BasicBlock &BB : *Region0) {
    bool SeenPhi = false;
    for (Instruction &I : BB) {
      if (isa<PHINode>(&I)) {
        FoundPhi = true;
        SeenPhi = true;
        continue;
      }
      if (isa<StoreInst>(&I) && SeenPhi)
        FoundStoreAfterPhi = true;
    }
  }
  for (Instruction &I : instructions(Region1))
    FoundLoad |= isa<LoadInst>(&I);
  EXPECT_TRUE(FoundPhi);
  EXPECT_TRUE(FoundStoreAfterPhi);
  EXPECT_TRUE(FoundLoad);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Roadmap H95a: a value live across a barrier that is used directly as a
// `phi`'s incoming value in the barrier's own reload region (rather than
// by some later, ordinary instruction, as `SpillsValueLiveAcrossGroupSyncBarrier`
// above covers) needs its reload placed at the end of the specific
// incoming block that operand corresponds to, not immediately before the
// `phi` itself -- inserting it in the `phi`'s own block instead would
// both violate "every phi in a block precedes every non-phi instruction"
// (if another phi follows) and, regardless of that, produce a value that
// does not dominate the very predecessor edge it is meant to feed (this
// exact shape -- a value live across a barrier merging into a post-
// barrier `phi` -- is what a real `dEQP-VK.mesh_shader.ext.properties.
// *shared_memory_size` CTS reduction exposed once roadmap H95a's own
// `DiamondFlattener` fix started threading a real (non-constant) mask
// value through a shape like this one).
TEST(EntryWrapperTest, SpillsValueUsedAsPhiIncomingValueAfterBarrier) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      %gid = call i32 @llvm.dx.group.id(i32 0)
      %gplus = add i32 %gid, 7
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      %cond = icmp eq i32 %gid, 0
      br i1 %cond, label %a, label %b
    a:
      br label %exit
    b:
      br label %exit
    exit:
      %val = phi i32 [ %gplus, %a ], [ 0, %b ]
      %doubled = mul i32 %val, 2
      ret void
    }
    declare i32 @llvm.dx.group.id(i32)
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);

  Function *Region1 = M->getFunction("main");
  ASSERT_TRUE(Region1);

  // The reload feeding `exit`'s phi must live in `a` (the incoming block
  // for that operand), not in `exit` itself alongside the phi.
  bool FoundLoadInA = false, FoundLoadInExit = false;
  for (BasicBlock &BB : *Region1) {
    for (Instruction &I : BB) {
      if (!isa<LoadInst>(&I))
        continue;
      if (BB.getName() == "a")
        FoundLoadInA = true;
      if (BB.getName() == "exit")
        FoundLoadInExit = true;
    }
  }
  EXPECT_TRUE(FoundLoadInA);
  EXPECT_FALSE(FoundLoadInExit);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// An entry point that reached `SIMDizePass` still carrying a parameter of
// its own (a shader entry point takes none -- its inputs arrive through
// stage-IO or resource accesses) leaves that parameter ahead of the
// `WaveBodyEnv` ABI ones on the widened wave body. This pass has no
// argument to supply for it, and must diagnose that through the module's
// `LLVMContext` -- which `feme::cpu::runPipeline`'s `ErrorDiagnosticGuard`
// turns into a clean pipeline failure -- rather than crashing on the
// `llvm_unreachable` in its own wave-body call dispatch.
TEST(EntryWrapperTest, UnsupportedEntryParameterDiagnosesInsteadOfCrashing) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %n) #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %sum = add i32 %tid, %n
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Handle) = true;
      },
      &SawError);

  // Must not crash the process (before this check existed, the wave-body
  // call dispatch `llvm_unreachable`'d on '%n' and `SIGABRT`ed here).
  EntryWrapperPass().run(*M, MAM);
  EXPECT_TRUE(SawError);

  // No half-built wrapper left behind for a later phase to trip over.
  EXPECT_FALSE(M->getFunction("feme_cpu_entry_main"));
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

} // namespace
