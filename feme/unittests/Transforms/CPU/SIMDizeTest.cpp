//===- SIMDizeTest.cpp - Tests for SIMDizePass ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SIMDize.h"

#include "feme/Transforms/CPU/BuiltinCalls.h"
#include "feme/Transforms/CPU/Linearize.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("SIMDizeTest", errs());
  return M;
}

void runPass(Module &M, unsigned WaveSize = 4) {
  ModuleAnalysisManager MAM;
  SIMDizePass(WaveSize).run(M, MAM);
}

TEST(SIMDizeTest, AppendsWaveBodyInterfaceParams) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  std::optional<WaveBodyEnv> Env = getWaveBodyEnv(*F);
  ASSERT_TRUE(Env);
  EXPECT_TRUE(Env->GroupIDX);
  EXPECT_TRUE(Env->GroupIDY);
  EXPECT_TRUE(Env->GroupIDZ);
  EXPECT_TRUE(Env->WaveIndex);
  EXPECT_TRUE(Env->EntryMask);
  EXPECT_TRUE(Env->GroupShared);
  EXPECT_EQ(cast<FixedVectorType>(Env->EntryMask->getType())->getNumElements(),
            4u);
}

TEST(SIMDizeTest, WidensDivergentThreadId) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %doubled = mul i32 %tid, 2
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundBuiltinCall = false;
  bool FoundWideMul = false;
  for (Instruction &I : instructions(F)) {
    if (auto *CI = dyn_cast<CallInst>(&I)) {
      if (std::optional<MatchedBuiltinCall> Matched = matchBuiltinCall(*CI)) {
        FoundBuiltinCall = true;
        EXPECT_EQ(Matched->Kind, BuiltinCallKind::ThreadId);
        EXPECT_EQ(Matched->WaveSize, 4u);
      }
    }
    if (auto *BO = dyn_cast<BinaryOperator>(&I))
      if (BO->getOpcode() == Instruction::Mul && BO->getType()->isVectorTy())
        FoundWideMul = true;
  }
  EXPECT_TRUE(FoundBuiltinCall);
  EXPECT_TRUE(FoundWideMul);
}

TEST(SIMDizeTest, LeavesUniformFunctionUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %v) #0 {
      %r = add i32 %v, 1
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  for (Instruction &I : instructions(F))
    if (auto *BO = dyn_cast<BinaryOperator>(&I))
      EXPECT_FALSE(BO->getType()->isVectorTy());
}

TEST(SIMDizeTest, WidensMaskedLoopBackedge) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %n) #0 {
    entry:
      br label %loop
    loop:
      %i = phi i32 [0, %entry], [%inc, %latch]
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %break.cond = icmp eq i32 %tid, %i
      br i1 %break.cond, label %exit, label %latch
    latch:
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

  ModuleAnalysisManager MAM;
  feme::cpu::LinearizePass().run(*M, MAM);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  bool FoundWidePHI = false;
  bool FoundReduceOr = false;
  for (Instruction &I : instructions(F)) {
    if (auto *PN = dyn_cast<PHINode>(&I))
      if (PN->getType()->isVectorTy())
        FoundWidePHI = true;
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (CI->getCalledFunction() &&
          CI->getCalledFunction()->getIntrinsicID() ==
              Intrinsic::vector_reduce_or)
        FoundReduceOr = true;
  }
  EXPECT_TRUE(FoundWidePHI);
  EXPECT_TRUE(FoundReduceOr);
}

TEST(SIMDizeTest, ScalarizesAtomicRMWFallback) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %p) #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tid64 = zext i32 %tid to i64
      %ptr = getelementptr i32, ptr %p, i64 %tid64
      %old = atomicrmw add ptr %ptr, i32 1 monotonic
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  unsigned AtomicRMWCount = 0;
  for (Instruction &I : instructions(F))
    if (isa<AtomicRMWInst>(&I))
      ++AtomicRMWCount;
  EXPECT_EQ(AtomicRMWCount, 4u);
}

// A divergent `atomicrmw nand` (unlike `add`/`and`/... -- see
// `ScalarizesAtomicRMWFallback` above and `getAtomicRMWIdentity`'s comment
// in SIMDize.cpp) has no identity element a masked-off lane's operand can
// be replaced with, so `widenMaskedAtomicRMW` diagnoses it via `emitError`
// instead of masking it. This regression-tests that `widen()` bails out
// cleanly right there instead of continuing to widen the rest of the
// function with a value that was never given its usual `Widened` entry
// (previously a null-pointer dereference/crash later in the same pass).
TEST(SIMDizeTest, DiagnosesUnmaskableAtomicRMWWithoutCrashing) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c = icmp eq i32 %tid, 0
      br i1 %c, label %t, label %f
    t:
      %old = atomicrmw nand ptr @g, i32 1 monotonic
      br label %end
    f:
      br label %end
    end:
      ret void
    }
    @g = global i32 0
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Ctx) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Ctx) = true;
      },
      &SawError);

  ModuleAnalysisManager MAM;
  feme::cpu::LinearizePass().run(*M, MAM);
  runPass(*M);

  EXPECT_TRUE(SawError);
}

// A divergent call to an ordinary function has no widened form yet, so
// `widenElementwise` diagnoses it via `emitError`. Like every other
// mid-widening diagnostic, it is emitted after `buildWidenedFunction` has
// erased the pre-widening function, which is why the widener must not
// reach through that function for the context (previously a use-after-free
// that crashed on macOS arm64).
TEST(SIMDizeTest, DiagnosesUnsupportedDivergentCall) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %r = call i32 @foo(i32 %tid)
      ret void
    }
    declare i32 @foo(i32)
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  std::string ErrorMessage;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Ctx) {
        if (DI->getSeverity() != DS_Error)
          return;
        std::string &Out = *reinterpret_cast<std::string *>(Ctx);
        raw_string_ostream OS(Out);
        DiagnosticPrinterRawOStream Printer(OS);
        DI->print(Printer);
      },
      &ErrorMessage);

  runPass(*M);

  EXPECT_NE(ErrorMessage.find("unsupported divergent call to 'foo'"),
            std::string::npos)
      << "actual diagnostic: " << ErrorMessage;
}

TEST(SIMDizeTest, WidensMaskedLoadStoreToGatherScatter) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %p) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c = icmp eq i32 %tid, 0
      br i1 %c, label %t, label %f
    t:
      %off = zext i32 %tid to i64
      %addr = getelementptr i32, ptr %p, i64 %off
      %loaded = load i32, ptr %addr
      %added = add i32 %loaded, 1
      store i32 %added, ptr %addr
      br label %end
    f:
      br label %end
    end:
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  feme::cpu::LinearizePass().run(*M, MAM);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  bool FoundGather = false;
  bool FoundScatter = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || !CI->getCalledFunction())
      continue;
    switch (CI->getCalledFunction()->getIntrinsicID()) {
    case Intrinsic::masked_gather:
      FoundGather = true;
      break;
    case Intrinsic::masked_scatter:
      FoundScatter = true;
      break;
    default:
      break;
    }
  }
  EXPECT_TRUE(FoundGather);
  EXPECT_TRUE(FoundScatter);
}

TEST(SIMDizeTest, DecomposesInsertElementChainIntoResourceStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %resource_heap, i32 %resource_heap_count) #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = uitofp i32 %tid to float
      %v0 = insertelement <4 x float> poison, float %tidf, i32 0
      %v1 = insertelement <4 x float> %v0, float %tidf, i32 1
      %v2 = insertelement <4 x float> %v1, float %tidf, i32 2
      %v3 = insertelement <4 x float> %v2, float 1.000000e+00, i32 3
      %off = zext i32 %tid to i64
      call void @feme.cpu.resource.store.typed.v4f32(
          ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off,
          <4 x float> %v3, i1 true)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.cpu.resource.store.typed.v4f32(ptr, i32, i32, i64, <4 x float>, i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  // Decomposition never builds an illegal `<4 x <4 x float>>`, and the
  // scalarized call keeps its original, whole-vector `<4 x float>` argument
  // type, reassembled per lane from the widened components.
  unsigned StoreCallCount = 0;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(I.getType()->isVectorTy() &&
                 cast<VectorType>(I.getType())->getElementType()->isVectorTy());
    auto *CI = dyn_cast<CallInst>(&I);
    if (CI && CI->getCalledFunction() &&
        CI->getCalledFunction()->getName() ==
            "feme.cpu.resource.store.typed.v4f32") {
      ++StoreCallCount;
      EXPECT_TRUE(CI->getArgOperand(4)->getType()->isVectorTy());
    }
  }
  EXPECT_EQ(StoreCallCount, 4u);
}

TEST(SIMDizeTest, DecomposesResourceLoadIntoExtractElement) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %resource_heap, i32 %resource_heap_count) #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %off = zext i32 %tid to i64
      %v = call <4 x float> @feme.cpu.resource.load.typed.v4f32(
          ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off, i1 true)
      %e0 = extractelement <4 x float> %v, i32 0
      %e2 = extractelement <4 x float> %v, i32 2
      %sum = fadd float %e0, %e2
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare <4 x float> @feme.cpu.resource.load.typed.v4f32(ptr, i32, i32, i64, i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  // A vector-typed load never builds an illegal `<4 x <4 x float>>` either
  // (the dual of `DecomposesInsertElementChainIntoResourceStore` above),
  // and the original `fadd` widens directly over the two decomposed,
  // per-lane `<4 x float>` components `extractelement` reads back.
  bool FoundWideAdd = false;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(I.getType()->isVectorTy() &&
                 cast<VectorType>(I.getType())->getElementType()->isVectorTy());
    if (auto *BO = dyn_cast<BinaryOperator>(&I))
      if (BO->getOpcode() == Instruction::FAdd &&
          BO->getType() == FixedVectorType::get(Type::getFloatTy(Ctx), 4))
        FoundWideAdd = true;
  }
  EXPECT_TRUE(FoundWideAdd);
}

TEST(SIMDizeTest, DiagnosesNonConstantIndexExtractElement) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = sitofp i32 %tid to float
      %v = insertelement <4 x float> poison, float %tidf, i32 0
      %e = extractelement <4 x float> %v, i32 %tid
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  bool SawError = false;
  LLVMContext &MCtx = M->getContext();
  MCtx.setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Ctx) {
        *static_cast<bool *>(Ctx) = DI->getSeverity() == DS_Error;
      },
      &SawError);
  runPass(*M);
  EXPECT_TRUE(SawError);
}

} // namespace
