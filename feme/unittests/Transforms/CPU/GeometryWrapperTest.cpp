//===- GeometryWrapperTest.cpp - Tests for GeometryWrapperPass -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/GeometryWrapper.h"

#include "feme/Core/Signature.h"
#include "feme/Core/StageOps.h"
#include "feme/Transforms/CPU/Linearize.h"
#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/CPU/WaveLowering.h"
#include "feme/Transforms/DXIL/SignatureImport.h"
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
    Err.print("GeometryWrapperTest", errs());
  return M;
}

SignatureElement makeFloatElement(uint32_t ElementID,
                                  SignatureDirection Direction) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = Direction;
  Elt.ComponentType = SignatureComponentType::Float;
  return Elt;
}

SignatureElement makePrimitiveIDInput(uint32_t ElementID) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.SystemValue = SignatureSystemValue::PrimitiveID;
  Elt.ComponentType = SignatureComponentType::UInt;
  return Elt;
}

SignatureElement makeInvocationIDInput(uint32_t ElementID) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.SystemValue = SignatureSystemValue::InvocationID;
  Elt.ComponentType = SignatureComponentType::UInt;
  return Elt;
}

TEST(GeometryWrapperTest, LowersInputEmitAndCutAndBuildsWrapper) {
  LLVMContext Ctx;
  // Reads two of the input triangle's three vertices (element 0), scales by
  // this invocation's `SV_PrimitiveID` (element 1), and emits two vertices
  // onto stream 0 before cutting: the canonical "emit a two-vertex strip per
  // primitive" shape.
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @gs_main() #0 {
      %pid = call i32 @feme.stage.input.load.i32(i32 1, i32 0, i32 0, i32 0)
      %pidf = uitofp i32 %pid to float
      %v0 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %v1 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 1)
      %r0 = fmul float %v0, %pidf
      %r1 = fmul float %v1, %pidf
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %r0, i32 0)
      call void @feme.stage.stream.emit(i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %r1, i32 0)
      call void @feme.stage.stream.emit(i32 0)
      call void @feme.stage.stream.cut(i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    declare void @feme.stage.stream.emit(i32)
    declare void @feme.stage.stream.cut(i32)
    attributes #0 = { "feme.shader.stage"="geometry" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {makeFloatElement(0, SignatureDirection::Input),
                  makePrimitiveIDInput(1),
                  makeFloatElement(2, SignatureDirection::Output)};
  dxil::setEntrySignature(*M->getFunction("gs_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  GeometryWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_gs_main"));
  for (const Instruction &I : instructions(*M->getFunction("gs_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// (Roadmap H5d-a) Reads this invocation's own `gl_InvocationID` (element 1,
// `SignatureSystemValue::InvocationID`) alongside its assembled vertex
// (element 0), scales by it, and emits/cuts one vertex -- exercises
// `lowerGeometryInvocationID`, distinct from `SV_PrimitiveID`'s own
// `lowerGeometryPrimitiveID` the test above already covers.
TEST(GeometryWrapperTest, LowersInvocationIDInputLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @gs_main() #0 {
      %iid = call i32 @feme.stage.input.load.i32(i32 1, i32 0, i32 0, i32 0)
      %iidf = uitofp i32 %iid to float
      %v0 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %r0 = fmul float %v0, %iidf
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %r0, i32 0)
      call void @feme.stage.stream.emit(i32 0)
      call void @feme.stage.stream.cut(i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    declare void @feme.stage.stream.emit(i32)
    declare void @feme.stage.stream.cut(i32)
    attributes #0 = { "feme.shader.stage"="geometry" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {makeFloatElement(0, SignatureDirection::Input),
                  makeInvocationIDInput(1),
                  makeFloatElement(2, SignatureDirection::Output)};
  dxil::setEntrySignature(*M->getFunction("gs_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  GeometryWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_gs_main"));
  for (const Instruction &I : instructions(*M->getFunction("gs_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(GeometryWrapperTest, DiagnosesAnUnsupportedOutputStream) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @gs_main() #0 {
      call void @feme.stage.stream.emit(i32 1)
      ret void
    }
    declare void @feme.stage.stream.emit(i32)
    attributes #0 = { "feme.shader.stage"="geometry" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  dxil::setEntrySignature(*M->getFunction("gs_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  GeometryWrapperPass().run(*M, MAM);

  EXPECT_FALSE(M->getFunction("feme_cpu_entry_gs_main"));
}

TEST(GeometryWrapperTest, DiagnosesGroupSyncBarrier) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @gs_main() #0 {
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      ret void
    }
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "feme.shader.stage"="geometry" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  GeometryWrapperPass().run(*M, MAM);

  EXPECT_FALSE(M->getFunction("feme_cpu_entry_gs_main"));
}

} // namespace
