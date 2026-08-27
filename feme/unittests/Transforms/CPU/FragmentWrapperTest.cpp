//===- FragmentWrapperTest.cpp - Tests for FragmentWrapperPass ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/FragmentWrapper.h"

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
    Err.print("FragmentWrapperTest", errs());
  return M;
}

TEST(FragmentWrapperTest, LowersStageIOAndWritesReturnMasks) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ps_main() #0 {
      %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %dx = call float @feme.stage.derivative.x.fine.f32(float %in)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %dx, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare float @feme.stage.derivative.x.fine.f32(float)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::Float;
  SignatureElement Out = In;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Sig.Elements = {In, Out};
  dxil::setEntrySignature(*M->getFunction("ps_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  FragmentWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_ps_main"));
  for (const Instruction &I : instructions(*M->getFunction("ps_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Regression test for roadmap H3a: gl_ViewportIndex read back as a fragment
// input (e.g. `out_color = color[gl_ViewportIndex]`) requires
// loadFragmentSystemValue() to handle SignatureSystemValue::
// ViewportArrayIndex. Verify FragmentWrapperPass lowers a system-value input
// element bound to ViewportArrayIndex without hitting the "unsupported
// fragment system value" error path.
TEST(FragmentWrapperTest, LowersViewportArrayIndexSystemValueInput) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ps_main() #0 {
      %vpidx = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
      call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 %vpidx, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
    attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::SInt;
  In.SystemValue = SignatureSystemValue::ViewportArrayIndex;
  SignatureElement Out;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Out.ComponentType = SignatureComponentType::SInt;
  Sig.Elements = {In, Out};
  dxil::setEntrySignature(*M->getFunction("ps_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  // Prior to the H3a fix, lowering a ViewportArrayIndex-bound input element
  // hit loadFragmentSystemValue()'s `default:` case, which calls
  // LLVMContext::emitError() -- rather than crash-testing that error path
  // directly, install a diagnostic handler and assert it is never invoked.
  bool SawError = false;
  Ctx.setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Ctx) {
        (void)DI;
        *reinterpret_cast<bool *>(Ctx) = true;
      },
      &SawError);

  FragmentWrapperPass().run(*M, MAM);

  EXPECT_FALSE(SawError) << "loadFragmentSystemValue() reported an "
                             "\"unsupported fragment system value\" error "
                             "for SignatureSystemValue::ViewportArrayIndex";
  EXPECT_TRUE(M->getFunction("feme_cpu_entry_ps_main"));
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

} // namespace
