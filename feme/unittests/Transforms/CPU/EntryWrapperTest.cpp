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
