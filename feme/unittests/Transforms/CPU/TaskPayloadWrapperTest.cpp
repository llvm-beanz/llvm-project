//===- TaskPayloadWrapperTest.cpp - Tests for TaskPayloadWrapperPass -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/TaskPayloadWrapper.h"

#include "feme/Core/StageOps.h"
#include "feme/Transforms/CPU/EntryWrapper.h"
#include "feme/Transforms/CPU/Linearize.h"
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

using namespace feme;
using namespace feme::cpu;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("TaskPayloadWrapperTest", errs());
  return M;
}

// A task entry's payload store (roadmap H6i's canonicalized shape, a
// compile-time-constant `offset` operand) lowers into a store addressed
// off `task_payload`, and the wave body gains this pass's own trailing
// params.
TEST(TaskPayloadWrapperTest, LowersPayloadStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @as_main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = uitofp i32 %tid to float
      call void @feme.stage.task.payload.store.f32(i32 4, float %tidf)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.stage.task.payload.store.f32(i32, float)
    attributes #0 = { "feme.shader.stage"="amplification" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  TaskPayloadWrapperPass().run(*M, MAM);

  Function *Body = M->getFunction("as_main");
  ASSERT_TRUE(Body);
  bool SawPayload = false, SawMaxPayloadBytes = false;
  for (const Argument &Arg : Body->args()) {
    SawPayload |= Arg.getName() == "task_payload";
    SawMaxPayloadBytes |= Arg.getName() == "task_max_payload_bytes";
  }
  EXPECT_TRUE(SawPayload);
  EXPECT_TRUE(SawMaxPayloadBytes);

  for (const Instruction &I : instructions(*Body))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// A task entry with no payload store at all is left completely alone
// besides this pass's own unconditional trailing params -- mirroring
// `MeshOutputWrapperTest.AppendsParamsEvenWithNoOutputStore`'s own
// "always append params, conditionally lower" convention every stage
// wrapper follows (see VertexWrapper.cpp).
TEST(TaskPayloadWrapperTest, AppendsParamsEvenWithNoPayloadStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @as_main() #0 {
      ret void
    }
    attributes #0 = { "feme.shader.stage"="amplification" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  TaskPayloadWrapperPass().run(*M, MAM);

  Function *Body = M->getFunction("as_main");
  ASSERT_TRUE(Body);
  bool SawPayload = false;
  for (const Argument &Arg : Body->args())
    SawPayload |= Arg.getName() == "task_payload";
  EXPECT_TRUE(SawPayload);

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Two payload stores at different constant offsets both lower, each
// against its own byte offset off the same `task_payload` base pointer --
// pinning down that this pass does not require (or assume) a single store
// per entry.
TEST(TaskPayloadWrapperTest, LowersMultiplePayloadStoresAtDistinctOffsets) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @as_main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = uitofp i32 %tid to float
      call void @feme.stage.task.payload.store.i32(i32 0, i32 %tid)
      call void @feme.stage.task.payload.store.f32(i32 4, float %tidf)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.stage.task.payload.store.i32(i32, i32)
    declare void @feme.stage.task.payload.store.f32(i32, float)
    attributes #0 = { "feme.shader.stage"="amplification" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  TaskPayloadWrapperPass().run(*M, MAM);

  Function *Body = M->getFunction("as_main");
  ASSERT_TRUE(Body);
  for (const Instruction &I : instructions(*Body))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  unsigned NumStores = 0;
  for (const Instruction &I : instructions(*Body))
    if (isa<StoreInst>(I))
      ++NumStores;
  EXPECT_GE(NumStores, 2u);

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// End-to-end: `TaskPayloadWrapperPass` followed by `EntryWrapperPass` (the
// same order `feme::cpu::buildPipeline` -- Pipeline.cpp -- chains them in
// for `ShaderStage::Amplification`) builds a real `feme_cpu_entry_as_main`
// wrapper whose single argument reads as `getTaskArgsType`'s longer
// struct.
TEST(TaskPayloadWrapperTest, ChainsIntoEntryWrapperPass) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @as_main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = uitofp i32 %tid to float
      call void @feme.stage.task.payload.store.f32(i32 4, float %tidf)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.stage.task.payload.store.f32(i32, float)
    attributes #0 = { "feme.shader.stage"="amplification" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  TaskPayloadWrapperPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  Function *Wrapper = M->getFunction("feme_cpu_entry_as_main");
  ASSERT_TRUE(Wrapper);
  EXPECT_EQ(Wrapper->arg_size(), 1u);
  EXPECT_TRUE(Wrapper->getArg(0)->getType()->isPointerTy());

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

} // namespace
