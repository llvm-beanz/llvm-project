//===- DomainWrapperTest.cpp - Tests for DomainWrapperPass ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/DomainWrapper.h"

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
    Err.print("DomainWrapperTest", errs());
  return M;
}

SignatureElement makeFloatElement(uint32_t ElementID,
                                  SignatureDirection Direction) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = Direction;
  Elt.ComponentType = SignatureComponentType::Float;
  if (Direction == SignatureDirection::PatchInput ||
      Direction == SignatureDirection::PatchOutput)
    Elt.Frequency = SignatureFrequency::PerPatch;
  return Elt;
}

SignatureElement makeDomainLocationInput(uint32_t ElementID) {
  SignatureElement Elt = makeFloatElement(ElementID, SignatureDirection::Input);
  Elt.SystemValue = SignatureSystemValue::DomainLocation;
  Elt.ComponentCount = 3;
  return Elt;
}

TEST(DomainWrapperTest, LowersAllThreeInputSourcesAndBuildsWrapper) {
  LLVMContext Ctx;
  // The canonical evaluation shape: blend two control points of the
  // completed patch (element 0, any control-point index) using this
  // invocation's own domain coordinate (element 1, `SV_DomainLocation`),
  // scaled by a patch constant (element 2, `PatchInput`).
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ds_main() #0 {
      %u = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
      %p0 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %p1 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 1)
      %k = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 0)
      %d = fsub float %p1, %p0
      %s = fmul float %d, %u
      %b = fadd float %p0, %s
      %r = fmul float %b, %k
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %r, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="domain" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {makeFloatElement(0, SignatureDirection::Input),
                  makeDomainLocationInput(1),
                  makeFloatElement(2, SignatureDirection::PatchInput),
                  makeFloatElement(3, SignatureDirection::Output)};
  dxil::setEntrySignature(*M->getFunction("ds_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  DomainWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_ds_main"));
  for (const Instruction &I : instructions(*M->getFunction("ds_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(DomainWrapperTest, DiagnosesDynamicDomainLocationComponent) {
  LLVMContext Ctx;
  // A domain-location component chosen at runtime: the record is a
  // fixed-size ABI struct, so this wrapper requires a constant component
  // (see DomainWrapper.cpp's file comment).
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ds_main(i32 %c) #0 {
      %u = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 %c, i32 0)
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %u, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="domain" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {makeDomainLocationInput(1),
                  makeFloatElement(3, SignatureDirection::Output)};
  dxil::setEntrySignature(*M->getFunction("ds_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  DomainWrapperPass().run(*M, MAM);

  // The wrapper is not built for a diagnosed shader.
  EXPECT_FALSE(M->getFunction("feme_cpu_entry_ds_main"));
}

TEST(DomainWrapperTest, DiagnosesGroupSyncBarrier) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ds_main() #0 {
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      ret void
    }
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "feme.shader.stage"="domain" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  DomainWrapperPass().run(*M, MAM);

  EXPECT_FALSE(M->getFunction("feme_cpu_entry_ds_main"));
}

} // namespace
