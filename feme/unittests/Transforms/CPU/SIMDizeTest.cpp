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

// Roadmap E29: a divergent `store` -- like `atomicrmw` above, void-typed,
// and reaching the same generic `widenScalarizedFallback` since it is
// neither a masked-store intrinsic call nor a groupshared address -- must
// not be named: `Builder.Insert` used to pass `I.getName() + ".lane"`
// unconditionally, asserting (`Value::setNameImpl`'s "Cannot assign a name
// to void values!") once a real divergent store reached it
// (dEQP-VK.renderpasses.dynamic_rendering...low_resolution_z.blend.
// color_masked_after_color_depth's own crash, through a masked color write
// that lowers to a plain divergent store rather than the masked-output-
// store intrinsic).
TEST(SIMDizeTest, ScalarizesDivergentStoreFallbackWithoutCrashing) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %buf = alloca i32
      store i32 %tid, ptr %buf
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

  unsigned StoreCount = 0;
  for (Instruction &I : instructions(F))
    if (isa<StoreInst>(&I))
      ++StoreCount;
  EXPECT_EQ(StoreCount, 4u);
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

TEST(SIMDizeTest, WidensNonConstantIndexExtractElementIntoSelectChain) {
  // Roadmap step C3: a non-constant-index `extractelement` out of a
  // decomposed vector is no longer diagnosed -- "a shuffle or a dynamic
  // index becomes selects across the components" (FeMeCPUDesign.md's
  // "Phase 4: Widening") is now implemented as a `select` chain over the
  // widened index (see `FunctionWidener::widenExtractElement`).
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
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  unsigned WideSelectCount = 0;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(I.getType()->isVectorTy() &&
                 cast<VectorType>(I.getType())->getElementType()->isVectorTy());
    if (auto *SI = dyn_cast<SelectInst>(&I))
      if (SI->getType() == FixedVectorType::get(Type::getFloatTy(Ctx), 4))
        ++WideSelectCount;
  }
  // One select per component, chaining the match against each compile-time
  // position (0..3).
  EXPECT_EQ(WideSelectCount, 4u);
}

TEST(SIMDizeTest, DecomposesVectorPHIAcrossUniformDiamond) {
  // Roadmap step C3: a divergent `phi` of vector type -- the shape
  // `feme::cpu::LinearizePass` leaves at a uniform diamond's merge block
  // reconciling two divergent vector values -- decomposes into one
  // per-component `phi` instead of being diagnosed (see
  // `FunctionWidener::createWidenedVectorPHIStub`/
  // `fillWidenedVectorPHIIncoming`).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i1 %cond) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = sitofp i32 %tid to float
      br i1 %cond, label %a, label %b
    a:
      %va = insertelement <4 x float> poison, float %tidf, i32 0
      br label %end
    b:
      %vb = insertelement <4 x float> poison, float 1.000000e+00, i32 0
      br label %end
    end:
      %v = phi <4 x float> [ %va, %a ], [ %vb, %b ]
      %e0 = extractelement <4 x float> %v, i32 0
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

  unsigned WidePHICount = 0;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(I.getType()->isVectorTy() &&
                 cast<VectorType>(I.getType())->getElementType()->isVectorTy());
    if (auto *PN = dyn_cast<PHINode>(&I))
      if (PN->getType() == FixedVectorType::get(Type::getFloatTy(Ctx), 4))
        ++WidePHICount;
  }
  // One per-component `phi` for each of the vector's four components.
  EXPECT_EQ(WidePHICount, 4u);
}

TEST(SIMDizeTest, DecomposesScalarConditionVectorSelect) {
  // Roadmap step C3: a vector-typed `select` with a scalar `i1` condition
  // decomposes into one `select` per component sharing that condition (see
  // `FunctionWidener::widenVectorSelect`).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i1 %cond) #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = sitofp i32 %tid to float
      %va = insertelement <4 x float> poison, float %tidf, i32 0
      %vb = insertelement <4 x float> poison, float 1.000000e+00, i32 0
      %v = select i1 %cond, <4 x float> %va, <4 x float> %vb
      %e0 = extractelement <4 x float> %v, i32 0
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

  unsigned WideSelectCount = 0;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(I.getType()->isVectorTy() &&
                 cast<VectorType>(I.getType())->getElementType()->isVectorTy());
    if (auto *SI = dyn_cast<SelectInst>(&I))
      if (SI->getType() == FixedVectorType::get(Type::getFloatTy(Ctx), 4))
        ++WideSelectCount;
  }
  EXPECT_EQ(WideSelectCount, 4u);
}

TEST(SIMDizeTest, DecomposesShuffleVectorAtCompileTime) {
  // Roadmap step C3: a `shufflevector`'s mask is always a compile-time
  // constant, so it decomposes with no runtime `select` at all -- each
  // output component is simply one of the two operands' already-widened
  // components (see `FunctionWidener::widenShuffleVector`).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = sitofp i32 %tid to float
      %v = insertelement <4 x float> poison, float %tidf, i32 0
      %swz = shufflevector <4 x float> %v, <4 x float> poison,
                            <4 x i32> <i32 1, i32 0, i32 2, i32 3>
      %e0 = extractelement <4 x float> %swz, i32 1
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

  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(isa<ShuffleVectorInst>(&I));
    EXPECT_FALSE(I.getType()->isVectorTy() &&
                 cast<VectorType>(I.getType())->getElementType()->isVectorTy());
  }
}

TEST(SIMDizeTest, DecomposesElementwiseBinaryOpOnTwoDivergentVectors) {
  // Roadmap step C3: ordinary elementwise arithmetic over two divergent
  // vectors -- the "color = a + b" shape every shader is full of --
  // decomposes into one scalar-element op per component instead of being
  // diagnosed (see `FunctionWidener::widenVectorElementwise`).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = sitofp i32 %tid to float
      %va0 = insertelement <4 x float> poison, float %tidf, i32 0
      %va1 = insertelement <4 x float> %va0, float 1.000000e+00, i32 1
      %va2 = insertelement <4 x float> %va1, float 2.000000e+00, i32 2
      %va3 = insertelement <4 x float> %va2, float 3.000000e+00, i32 3
      %vb0 = insertelement <4 x float> poison, float 4.000000e+00, i32 0
      %vb1 = insertelement <4 x float> %vb0, float 5.000000e+00, i32 1
      %vb2 = insertelement <4 x float> %vb1, float 6.000000e+00, i32 2
      %vb3 = insertelement <4 x float> %vb2, float 7.000000e+00, i32 3
      %sum = fadd <4 x float> %va3, %vb3
      %e0 = extractelement <4 x float> %sum, i32 0
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

  unsigned WideFAddCount = 0;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(I.getType()->isVectorTy() &&
                 cast<VectorType>(I.getType())->getElementType()->isVectorTy());
    if (auto *BO = dyn_cast<BinaryOperator>(&I))
      if (BO->getOpcode() == Instruction::FAdd &&
          BO->getType() == FixedVectorType::get(Type::getFloatTy(Ctx), 4))
        ++WideFAddCount;
  }
  // One `fadd` per decomposed component.
  EXPECT_EQ(WideFAddCount, 4u);
}

// Roadmap step R23's "divergent index" shape: `FunctionWidener::
// widenGroupSharedGEP` must widen the divergent `getelementptr` into a
// real vector-of-pointers access (rather than `widenScalarizedFallback`'s
// per-lane clone-and-reassemble via `insertelement`), which
// `widenGroupSharedLoad` then turns into a real `llvm.masked.gather` --
// and `feme::cpu::rewriteGroupSharedGlobals` (run at the end of the same
// pass) must retarget both into the flat, address-space-0 form without
// diagnosing them. See test/Transforms/CPU/simdize-groupshared-
// divergent-index.ll for the full end-to-end IR shape this produces.
TEST(SIMDizeTest, WidensGroupSharedDivergentIndexToVectorGEPAndGather) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
      %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 %tid
      %val = load i32, ptr addrspace(3) %ptr
      ret void
    }
    @shared = internal addrspace(3) global [4 x i32] undef
    declare i32 @llvm.dx.thread.id.in.group(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  bool FoundVectorGEP = false;
  bool FoundGather = false;
  for (Instruction &I : instructions(F)) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(&I))
      FoundVectorGEP |= GEP->getType()->isVectorTy();
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (CI->getCalledFunction() &&
          CI->getCalledFunction()->getIntrinsicID() == Intrinsic::masked_gather)
        FoundGather = true;
    // `@shared`'s address space must be canonicalized away entirely, not
    // just left divergent.
    EXPECT_FALSE(I.getType()->isPointerTy() &&
                 I.getType()->getPointerAddressSpace() == 3);
  }
  EXPECT_TRUE(FoundVectorGEP);
  EXPECT_TRUE(FoundGather);
}

// Roadmap step R23's "access through a getelementptr" shape: an
// `atomicrmw` always scalarizes (see `ScalarizesAtomicRMWFallback` above),
// even when its groupshared address is uniform (a compile-time-constant
// array index here) -- `FunctionWidener::widenGroupSharedAtomicRMW` must
// reuse that uniform `getelementptr` directly for every lane's clone
// instead of broadcasting it, so `rewriteGroupSharedGlobals` sees a single
// real `getelementptr` with several ordinary `atomicrmw` users, exactly
// as it already does for a direct (non-indexed) global.
TEST(SIMDizeTest, WidensGroupSharedAtomicRMWThroughUniformGEP) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 2
      %old = atomicrmw add ptr addrspace(3) %ptr, i32 1 monotonic
      ret void
    }
    @shared = internal addrspace(3) global [4 x i32] undef
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  unsigned AtomicRMWCount = 0;
  GetElementPtrInst *SharedGEP = nullptr;
  for (Instruction &I : instructions(F)) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(&I))
      if (GEP->getSourceElementType()->isArrayTy())
        SharedGEP = GEP;
    if (isa<AtomicRMWInst>(&I))
      ++AtomicRMWCount;
  }
  EXPECT_EQ(AtomicRMWCount, 4u);
  // Every widened `atomicrmw` shares the *same* `getelementptr` -- the
  // broadcast this test guards against would instead have left each one
  // reading a separate `extractelement` of a splat.
  ASSERT_TRUE(SharedGEP);
  for (User *U : SharedGEP->users())
    EXPECT_TRUE(isa<AtomicRMWInst>(U));
}

// Roadmap step R23's "masked store at a uniform address" shape: a `store`
// masked by `feme::cpu::LinearizePass` into a `feme.cpu.masked.store` call
// widens (`FunctionWidener::widenMaskedStore`) into a real
// `llvm.masked.scatter` even when the address never varies by lane, which
// `rewriteGroupSharedGlobals` must still retarget by recognizing the
// resulting same-value broadcast (`matchPointerBroadcast`) instead of
// diagnosing it.
TEST(SIMDizeTest, RewritesGroupSharedMaskedStoreAtUniformAddress) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
      %cond = icmp eq i32 %tid, 0
      br i1 %cond, label %t, label %f
    t:
      store i32 42, ptr addrspace(3) @shared
      br label %end
    f:
      br label %end
    end:
      ret void
    }
    @shared = internal addrspace(3) global i32 undef
    declare i32 @llvm.dx.thread.id.in.group(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  feme::cpu::LinearizePass().run(*M, MAM);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  bool FoundScatter = false;
  for (Instruction &I : instructions(F)) {
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (CI->getCalledFunction() &&
          CI->getCalledFunction()->getIntrinsicID() ==
              Intrinsic::masked_scatter)
        FoundScatter = true;
    EXPECT_FALSE(I.getType()->isPointerTy() &&
                 I.getType()->getPointerAddressSpace() == 3);
  }
  EXPECT_TRUE(FoundScatter);
}

// A *nested* `getelementptr` (one level deeper than a single index --
// e.g. a groupshared array of arrays) remains outside roadmap step R23's
// scope and must still be diagnosed, not silently miscompiled.
TEST(SIMDizeTest, DiagnosesNestedGroupSharedGetElementPtr) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %p1 = getelementptr inbounds [2 x [4 x i32]], ptr addrspace(3) @shared, i32 0, i32 0
      %p2 = getelementptr inbounds [4 x i32], ptr addrspace(3) %p1, i32 0, i32 2
      %val = load i32, ptr addrspace(3) %p2
      ret void
    }
    @shared = internal addrspace(3) global [2 x [4 x i32]] undef
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
