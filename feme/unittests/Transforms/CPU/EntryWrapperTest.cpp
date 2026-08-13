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

// Roadmap milestone 9's narrowing: a barrier inside a surviving (uniform)
// branch is diagnosed rather than mis-split.
TEST(EntryWrapperTest, NonLinearControlFlowWithBarrierIsDiagnosed) {
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

  EXPECT_FALSE(M->getFunction("feme_cpu_entry_main"));
}

} // namespace
