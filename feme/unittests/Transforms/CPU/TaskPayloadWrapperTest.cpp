//===- TaskPayloadWrapperTest.cpp - Tests for TaskPayloadWrapperPass -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/TaskPayloadWrapper.h"

#include "feme/Core/Signature.h"
#include "feme/Core/StageOps.h"
#include "feme/Transforms/CPU/EntryWrapper.h"
#include "feme/Transforms/CPU/Linearize.h"
#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/CPU/WaveLowering.h"
#include "feme/Transforms/DXIL/SignatureImport.h"
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

SignatureElement makeInputElement(uint32_t ElementID,
                                  SignatureSystemValue SystemValue) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.ComponentType = SignatureComponentType::UInt;
  Elt.SystemValue = SystemValue;
  return Elt;
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
  bool SawPayload = false, SawMeshGroupCount = false;
  for (const Argument &Arg : Body->args()) {
    SawPayload |= Arg.getName() == "task_payload";
    SawMeshGroupCount |= Arg.getName() == "task_mesh_group_count";
  }
  EXPECT_TRUE(SawPayload);
  EXPECT_TRUE(SawMeshGroupCount);

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

// (Roadmap H6s) A task entry's `EmitMeshTasksEXT` request (canonicalized
// as `feme.stage.emit_mesh_tasks`, no signature element or per-lane
// addressing of its own -- see `StageOpKind::EmitMeshTasks`'s comment)
// lowers into three stores through `task_mesh_group_count`, and the wave
// body gains this pass's own trailing param for it, mirroring
// `LowersPayloadStore`'s own shape exactly.
TEST(TaskPayloadWrapperTest, LowersEmitMeshTasks) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @as_main() #0 {
      call void @feme.stage.emit_mesh_tasks(i32 4, i32 5, i32 6)
      ret void
    }
    declare void @feme.stage.emit_mesh_tasks(i32, i32, i32)
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
  bool SawMeshGroupCount = false;
  for (const Argument &Arg : Body->args())
    SawMeshGroupCount |= Arg.getName() == "task_mesh_group_count";
  EXPECT_TRUE(SawMeshGroupCount);

  for (const Instruction &I : instructions(*Body))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  unsigned NumStores = 0;
  for (const Instruction &I : instructions(*Body))
    if (isa<StoreInst>(I))
      ++NumStores;
  EXPECT_GE(NumStores, 3u);

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// A task entry using both `feme.stage.task.payload.store` and
// `feme.stage.emit_mesh_tasks` together (the real shape every
// `with_task_shader` CTS case's own task stage takes -- write the payload,
// then request the mesh dispatch) lowers both, pinning down that
// `TaskPayloadWrapperPass` handles a function using more than one of the
// ops it supports.
TEST(TaskPayloadWrapperTest, LowersPayloadStoreAndEmitMeshTasksTogether) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @as_main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      call void @feme.stage.task.payload.store.i32(i32 0, i32 %tid)
      call void @feme.stage.emit_mesh_tasks(i32 4, i32 5, i32 6)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.stage.task.payload.store.i32(i32, i32)
    declare void @feme.stage.emit_mesh_tasks(i32, i32, i32)
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
  EXPECT_GE(NumStores, 4u);

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// (Roadmap H6s) A real `with_task_shader` CTS case's own task-stage shader
// computes its `EmitMeshTasksEXT` group-count operands from an ordinary
// resource load, leaving an unrelated `feme.cpu.resource.load.raw.*` call
// still in `F` alongside the (by then already-masked) `emit_mesh_tasks`
// call once `Linearize`/`SIMDize` are done -- `lowerTaskPayloadStageOps`'s
// own catch-all must not reject that unrelated call outright the same way
// `MeshOutputWrapperPass`'s own catch-all once wrongly did (roadmap
// H6g-b-d), since the `UsesStageOps` gate only establishes that *some*
// call in `F` needs this pass's attention, not that *every* call does.
TEST(TaskPayloadWrapperTest, LeavesUnrelatedResourceLoadCallAlone) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @as_main() #0 {
      %loaded = call i32 @feme.cpu.resource.load.raw.i32(ptr null, i32 0, i32 0, i64 0, i1 true)
      call void @feme.stage.emit_mesh_tasks(i32 %loaded, i32 1, i32 1)
      ret void
    }
    declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)
    declare void @feme.stage.emit_mesh_tasks(i32, i32, i32)
    attributes #0 = { "feme.shader.stage"="amplification" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Handle) = true;
      },
      &SawError);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  TaskPayloadWrapperPass().run(*M, MAM);
  EXPECT_FALSE(SawError);

  Function *Body = M->getFunction("as_main");
  ASSERT_TRUE(Body);
  bool SawResourceLoad = false;
  for (const Instruction &I : instructions(*Body)) {
    const auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    EXPECT_FALSE(isStageOpCall(*CI)) << *CI;
    if (const Function *Callee = CI->getCalledFunction())
      SawResourceLoad |=
          Callee->getName().starts_with("feme.cpu.resource.load.raw.");
  }
  EXPECT_TRUE(SawResourceLoad);

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// (Roadmap H6t) A task entry's `gl_DrawID` input load (canonicalized as
// `feme.stage.input.load` with `SignatureSystemValue::DrawID`, found while
// re-running H6s's own fix against a real `with_task_shader` CTS case)
// lowers into a per-lane broadcast of `task_draw_id`, mirroring
// `MeshOutputWrapperTest.LowersDrawIDInputLoad`'s own coverage of the same
// builtin on the mesh-stage side exactly.
TEST(TaskPayloadWrapperTest, LowersDrawIDInputLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @as_main() #0 {
      %draw_id = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
      call void @feme.stage.emit_mesh_tasks(i32 %draw_id, i32 1, i32 1)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare void @feme.stage.emit_mesh_tasks(i32, i32, i32)
    attributes #0 = { "feme.shader.stage"="amplification" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {
      makeInputElement(0, SignatureSystemValue::DrawID),
  };
  dxil::setEntrySignature(*M->getFunction("as_main"), Sig);

  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Handle) = true;
      },
      &SawError);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  TaskPayloadWrapperPass().run(*M, MAM);
  EXPECT_FALSE(SawError);

  Function *Body = M->getFunction("as_main");
  ASSERT_TRUE(Body);
  bool SawDrawID = false;
  for (const Argument &Arg : Body->args())
    SawDrawID |= Arg.getName() == "task_draw_id";
  EXPECT_TRUE(SawDrawID);

  for (const Instruction &I : instructions(*Body))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// A task entry reading an input system value other than `gl_DrawID`
// (mirroring `MeshOutputWrapperTest.RejectsUnsupportedInputSystemValue`'s
// own coverage) is diagnosed cleanly, not silently mislowered.
TEST(TaskPayloadWrapperTest, RejectsUnsupportedInputSystemValue) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @as_main() #0 {
      %v = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
      call void @feme.stage.emit_mesh_tasks(i32 %v, i32 1, i32 1)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare void @feme.stage.emit_mesh_tasks(i32, i32, i32)
    attributes #0 = { "feme.shader.stage"="amplification" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {
      makeInputElement(0, SignatureSystemValue::VertexID),
  };
  dxil::setEntrySignature(*M->getFunction("as_main"), Sig);

  std::string LastError;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() != DS_Error)
          return;
        *reinterpret_cast<std::string *>(Handle) = "error";
      },
      &LastError);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  TaskPayloadWrapperPass().run(*M, MAM);
  EXPECT_EQ(LastError, "error");
}

} // namespace
