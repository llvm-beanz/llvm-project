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
#include "llvm/IR/IntrinsicsSPIRV.h"
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

// (Roadmap H7z) A divergent (per-lane) value of aggregate type -- e.g. an
// ordinary array-typed `load` through a divergent address -- has no
// per-lane component-decomposition support in this pass at all (unlike a
// divergent vector, which this file's own many `Decomposes*` cases already
// widen into `N` per-lane scalars). This is exactly the shape H7x's own
// stage-IO fragment-input consumer used to produce for a
// `gl_ClipDistance`/`gl_CullDistance` read, before roadmap H7y's own,
// unrelated fix (StageIOAddressOfPattern keeping an array-typed `Input`
// variable as a real pointer, so only a scalar element is ever loaded, not
// the whole array) incidentally stopped that particular case from ever
// reaching this pass as a divergent aggregate. The generic protection
// itself -- diagnosing rather than miscompiling/asserting on a divergent
// aggregate this pass cannot yet decompose -- remains load-bearing for any
// other producer of one (this test's own synthetic array load, a struct
// load, etc.), so it needs its own direct test independent of H7y's fix.
TEST(SIMDizeTest, DiagnosesUnsupportedDivergentAggregate) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %p) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %off = zext i32 %tid to i64
      %addr = getelementptr [2 x float], ptr %p, i64 %off
      %loaded = load [2 x float], ptr %addr
      %v0 = extractvalue [2 x float] %loaded, 0
      ret void
    }
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

  EXPECT_NE(ErrorMessage.find("has a divergent value 'loaded' of aggregate "
                              "type; component decomposition is not yet "
                              "supported"),
            std::string::npos)
      << "actual diagnostic: " << ErrorMessage;
}

// (Roadmap L21) A divergent struct value assembled by a chain of
// `insertvalue`s (mixing a whole sub-array inserted at once with genuine
// scalar leaves) and read back apart by a chain of `extractvalue`s (again
// mixing a whole sub-array extraction with individual scalar-leaf
// extractions) -- the exact shape `feme::cpu::SPIRVResourceLoweringPass`'s
// own whole-aggregate resource load/store decomposition (roadmap L20)
// produces once reassembled through `feme::cpu::LinearizePass`, reduced
// from a real `Feature/StructuredBuffer/packed.test` failure -- now
// decomposes into one `<W x T>` per flattened scalar leaf
// (`WidenedAggregateComponents`) instead of `checkVectorDecompositionSupported`'s
// former unconditional aggregate bail.
TEST(SIMDizeTest, DecomposesInsertValueChainIntoResourceStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %resource_heap, i32 %resource_heap_count) #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %off = zext i32 %tid to i64
      %e0 = call i32 @feme.cpu.resource.load.raw.i32(
          ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off, i1 true)
      %off1 = add i64 %off, 4
      %e1 = call i32 @feme.cpu.resource.load.raw.i32(
          ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off1, i1 true)
      %off2 = add i64 %off, 8
      %scalar = call i32 @feme.cpu.resource.load.raw.i32(
          ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off2, i1 true)

      %a0 = insertvalue [2 x i32] poison, i32 %e0, 0
      %a1 = insertvalue [2 x i32] %a0, i32 %e1, 1
      %s0 = insertvalue { [2 x i32], i32 } poison, [2 x i32] %a1, 0
      %s1 = insertvalue { [2 x i32], i32 } %s0, i32 %scalar, 1

      %arr = extractvalue { [2 x i32], i32 } %s1, 0
      %r0 = extractvalue [2 x i32] %arr, 0
      %r1 = extractvalue [2 x i32] %arr, 1
      %r2 = extractvalue { [2 x i32], i32 } %s1, 1

      call void @feme.cpu.resource.store.raw.i32(
          ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off, i32 %r0, i1 true)
      call void @feme.cpu.resource.store.raw.i32(
          ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off1, i32 %r1, i1 true)
      call void @feme.cpu.resource.store.raw.i32(
          ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off2, i32 %r2, i1 true)
      ret void
    }
    declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)
    declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  // Decomposition never builds an illegal `<4 x {[2 x i32], i32}>`, and
  // every per-lane scalarized store's own value operand stays a genuine
  // scalar `i32` (never a nested vector or an aggregate).
  unsigned StoreCallCount = 0, LoadCallCount = 0;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(I.getType()->isAggregateType());
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || !CI->getCalledFunction())
      continue;
    StringRef Name = CI->getCalledFunction()->getName();
    if (Name == "feme.cpu.resource.store.raw.i32") {
      ++StoreCallCount;
      EXPECT_TRUE(CI->getArgOperand(4)->getType()->isIntegerTy(32));
    } else if (Name == "feme.cpu.resource.load.raw.i32") {
      ++LoadCallCount;
    }
  }
  EXPECT_EQ(LoadCallCount, 3u * 4u);
  EXPECT_EQ(StoreCallCount, 3u * 4u);
}

// Roadmap L27: a divergent, whole *vector*-typed value inserted as a single
// struct-field leaf via `insertvalue` (rather than each scalar component
// individually, the only shape roadmap L21 supported), then read back out
// via a constant-index `extractvalue` and stored through an ordinary,
// non-groupshared alloca -- the exact shape reduced from a real
// `Feature/Semantics/HullSystemValues.test` failure, whose `PatchConstants`
// function builds a local per-control-point `float4 Position` scratch array
// (one per-control-point struct field holding a whole position vector)
// before indexing back into it by its own `SV_OutputControlPointID`. Two
// gaps closed together: `isSupportedAggregateLeafType` now accepts a whole
// `FixedVectorType` leaf (flattened into its own `N` component slots rather
// than a single flat slot), and `checkVectorDecomposition Supported`'s own
// consumer-shape check now accepts both an `insertvalue`'s inserted-value
// operand and an ordinary (non-groupshared) `store`'s value operand as
// valid users of a divergent vector value.
TEST(SIMDizeTest, DecomposesInsertValueVectorLeafIntoOrdinaryStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %scratch) #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %off = zext i32 %tid to i64
      %off1 = add i64 %off, 4

      %e0 = call float @feme.cpu.resource.load.raw.f32(
          ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off, i1 true)
      %e1 = call float @feme.cpu.resource.load.raw.f32(
          ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off1, i1 true)
      %v.0 = insertelement <2 x float> poison, float %e0, i64 0
      %v = insertelement <2 x float> %v.0, float %e1, i64 1

      %s = insertvalue { <2 x float> } poison, <2 x float> %v, 0

      %leaf = extractvalue { <2 x float> } %s, 0
      store <2 x float> %leaf, ptr %scratch, align 8

      %r = load <2 x float>, ptr %scratch, align 8
      %r0 = extractelement <2 x float> %r, i64 0
      %r1 = extractelement <2 x float> %r, i64 1
      call void @feme.cpu.resource.store.raw.f32(
          ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off, float %r0, i1 true)
      call void @feme.cpu.resource.store.raw.f32(
          ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off1, float %r1, i1 true)
      ret void
    }
    declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1)
    declare void @feme.cpu.resource.store.raw.f32(ptr, i32, i32, i64, float, i1)
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  // Decomposition never builds an illegal `<4 x <2 x float>>` nested
  // vector: every widened `store`'s own value operand stays a genuine
  // `<2 x float>`, one per lane, and every aggregate-typed intermediate
  // (`%s`) is erased rather than surviving into the widened function.
  unsigned StoreInstCount = 0, ResourceStoreCallCount = 0;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(I.getType()->isAggregateType());
    if (auto *SI = dyn_cast<StoreInst>(&I)) {
      ++StoreInstCount;
      EXPECT_TRUE(SI->getValueOperand()->getType()->isVectorTy());
      EXPECT_FALSE(
          cast<VectorType>(SI->getValueOperand()->getType())->getElementType()->isVectorTy());
      continue;
    }
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || !CI->getCalledFunction())
      continue;
    if (CI->getCalledFunction()->getName() == "feme.cpu.resource.store.raw.f32")
      ++ResourceStoreCallCount;
  }
  EXPECT_EQ(StoreInstCount, 4u);
  EXPECT_EQ(ResourceStoreCallCount, 2u * 4u);
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

TEST(SIMDizeTest, DecomposesPrivateMemoryDivergentLoadIntoExtractElement) {
  // Roadmap H7o: an ordinary, non-groupshared divergent-address `load` of
  // vector type -- the "local constant lookup table indexed by a
  // per-invocation builtin" shape, reduced from a real
  // `dEQP-VK.pipeline.monolithic.multisample.min_sample_shading_*` vertex
  // shader's own `positions[gl_VertexIndex]` -- used to hit
  // `checkVectorDecompositionSupported`'s "has a divergent value of vector
  // type" diagnostic unconditionally: unlike a resource-call load (see
  // `DecomposesResourceLoadIntoExtractElement` above) or a groupshared
  // load (`WidensGroupSharedDivergentIndexToVectorGEPAndGather` below), no
  // producer case at all covered a plain `LoadInst`. It is now decomposed
  // the same way, into `N` widened per-component values via
  // `widenScalarizedFallback`'s per-lane clone, rather than building one
  // illegal `<4 x <4 x float>>` result.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %table = alloca [4 x <4 x float>], align 4
      store [4 x <4 x float>] [
          <4 x float> <float -1.0, float -1.0, float 0.0, float 1.0>,
          <4 x float> <float -1.0, float  1.0, float 0.0, float 1.0>,
          <4 x float> <float  1.0, float -1.0, float 0.0, float 1.0>,
          <4 x float> <float  1.0, float  1.0, float 0.0, float 1.0>],
          ptr %table, align 4
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %elt = getelementptr [4 x <4 x float>], ptr %table, i32 0, i32 %tid
      %v = load <4 x float>, ptr %elt, align 4
      %e0 = extractelement <4 x float> %v, i32 0
      %e2 = extractelement <4 x float> %v, i32 2
      %sum = fadd float %e0, %e2
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

  bool FoundWideAdd = false;
  bool FoundWideLoad = false;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(I.getType()->isVectorTy() &&
                 cast<VectorType>(I.getType())->getElementType()->isVectorTy());
    if (auto *LI = dyn_cast<LoadInst>(&I))
      if (LI->getType() == FixedVectorType::get(Type::getFloatTy(Ctx), 4))
        FoundWideLoad = true;
    if (auto *BO = dyn_cast<BinaryOperator>(&I))
      if (BO->getOpcode() == Instruction::FAdd &&
          BO->getType() == FixedVectorType::get(Type::getFloatTy(Ctx), 4))
        FoundWideAdd = true;
  }
  // The per-lane load itself keeps the original `<4 x float>` element
  // type (one real load through one lane's own extracted `ptr`, not a
  // widened `<4 x <4 x float>>`, which the `EXPECT_FALSE` above already
  // rules out for every instruction), while the reassembled components
  // downstream (the `fadd` over two of them) widen exactly like a
  // resource load's would.
  EXPECT_TRUE(FoundWideLoad);
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

// Roadmap H6g-b-a-i-a-i-a: a mesh entry point's own
// `gl_PrimitiveTriangleIndicesEXT[...] = uvec3(...)` write has no
// canonicalized `feme.stage.*` op of its own to become a
// `feme.cpu.resource.*`/masked-output-store call instead (see
// MeshOutputWrapper.h's file comment), so it survives as an ordinary
// `store` of a divergent vector value that `feme::cpu::LinearizePass`
// masks into a `feme.cpu.masked.store.*` call exactly like a scalar one.
// `checkVectorDecompositionSupported` must accept that call's stored-value
// operand as a supported consumer of the decomposed vector (mirroring a
// matched resource-store call's), and `FunctionWidener::widenMaskedStore`
// must reassemble each lane's own vector from the decomposed components
// rather than try to broadcast the whole vector into an illegal
// `<W x <3 x i32>>` -- `llvm.masked.scatter` has no vector-of-vector-
// element form to lower that to anyway, so each lane is written
// individually, guarded by a load-select-store idiom.
TEST(SIMDizeTest, DecomposesInsertElementChainIntoMaskedVectorStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %p) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %base = mul i32 %tid, 3
      %e1 = add i32 %base, 1
      %e2 = add i32 %base, 2
      %cond = icmp eq i32 %tid, 0
      br i1 %cond, label %t, label %f
    t:
      %off = zext i32 %tid to i64
      %addr = getelementptr <3 x i32>, ptr %p, i64 %off
      %v0 = insertelement <3 x i32> poison, i32 %base, i32 0
      %v1 = insertelement <3 x i32> %v0, i32 %e1, i32 1
      %v2 = insertelement <3 x i32> %v1, i32 %e2, i32 2
      store <3 x i32> %v2, ptr %addr
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
  LinearizePass().run(*M, MAM);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  // Decomposition never builds an illegal `<4 x <3 x i32>>`, and each lane
  // gets its own real `<3 x i32>` store, reassembled from the widened
  // per-component values.
  unsigned VectorStoreCount = 0;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(I.getType()->isVectorTy() &&
                 cast<VectorType>(I.getType())->getElementType()->isVectorTy());
    if (auto *SI = dyn_cast<StoreInst>(&I))
      if (SI->getValueOperand()->getType() ==
          FixedVectorType::get(Type::getInt32Ty(Ctx), 3))
        ++VectorStoreCount;
  }
  EXPECT_EQ(VectorStoreCount, 4u);
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

// Roadmap H6g-b-a-i-a-i-b: a divergent vector comparison (`fcmp`/`icmp`)
// producing a `<N x i1>` result must decompose into `N` per-component
// `<W x i1>` comparisons instead of the illegal `<W x <N x i1>>` a naive
// broadcast would build, and that `<N x i1>` result must itself be a
// supported *consumer* shape for a `select`'s now-per-lane vector
// condition (`FunctionWidener::widenVectorSelect`'s per-component
// condition decomposition), mirroring the exact
// `dEQP-VK.mesh_shader.ext.in_out.32_bits_only` shape this row's own
// investigation reduced a real failing shader down to: a component-wise
// `lessThanEqual`-style comparison feeding a per-lane `select`/`mix`.
TEST(SIMDizeTest, DecomposesVectorComparisonIntoPerLaneSelectCondition) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = sitofp i32 %tid to float
      %a0 = insertelement <4 x float> poison, float %tidf, i32 0
      %b0 = insertelement <4 x float> poison, float 1.000000e+00, i32 0
      %t0 = insertelement <4 x float> poison, float 2.000000e+00, i32 0
      %cond = fcmp ole <4 x float> %a0, %b0
      %v = select <4 x i1> %cond, <4 x float> %t0, <4 x float> %a0
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

  unsigned FCmpCount = 0;
  unsigned SelectCount = 0;
  for (Instruction &I : instructions(F)) {
    // Never build an illegal vector-of-vector type (a `<W x <N x T>>`
    // this fix must avoid, for either the comparison's own boolean-vector
    // result or the select's true/false vector operands).
    EXPECT_FALSE(I.getType()->isVectorTy() &&
                 cast<VectorType>(I.getType())->getElementType()->isVectorTy());
    if (auto *Cmp = dyn_cast<FCmpInst>(&I)) {
      EXPECT_TRUE(Cmp->getType() == FixedVectorType::get(
                                        Type::getInt1Ty(Ctx), 4));
      ++FCmpCount;
    }
    if (auto *Sel = dyn_cast<SelectInst>(&I)) {
      // Each per-lane `select`'s own condition is one of the decomposed
      // `fcmp`'s per-component results, not a single shared scalar.
      EXPECT_TRUE(isa<FCmpInst>(Sel->getCondition()));
      ++SelectCount;
    }
  }
  EXPECT_EQ(FCmpCount, 4u);
  EXPECT_EQ(SelectCount, 4u);
}

TEST(SIMDizeTest, DecomposesVectorComparisonIntoReduceAnd) {
  // Roadmap H6g-b-a-i-a-i-b: a `llvm.vector.reduce.and` call folding a
  // divergent, per-lane-decomposed `fcmp`'s components together is a
  // supported consumer shape -- the shape glslang's `all(lessThanEqual(...))`
  // GLSL builtin takes over a component-wise vector comparison (see
  // `isSupportedVectorReduceIntrinsic`/`widenVectorReduce` in SIMDize.cpp,
  // and `simdize-vector-reduce.ll`).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = sitofp i32 %tid to float
      %a0 = insertelement <4 x float> poison, float %tidf, i32 0
      %b0 = insertelement <4 x float> poison, float 1.000000e+00, i32 0
      %cond = fcmp ole <4 x float> %a0, %b0
      %all = call i1 @llvm.vector.reduce.and.v4i1(<4 x i1> %cond)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i1 @llvm.vector.reduce.and.v4i1(<4 x i1>)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  unsigned FCmpCount = 0;
  unsigned AndCount = 0;
  for (Instruction &I : instructions(F)) {
    // Never build an illegal vector-of-vector type.
    EXPECT_FALSE(I.getType()->isVectorTy() &&
                 cast<VectorType>(I.getType())->getElementType()->isVectorTy());
    if (isa<FCmpInst>(&I))
      ++FCmpCount;
    // The reduce call's own result folds back down to a lane-wise
    // `<W x i1>` scalar-shaped value, not a vector: a plain `and`
    // `BinaryOperator` combining the decomposed components pairwise, not
    // another `llvm.vector.reduce.*` call.
    if (auto *BO = dyn_cast<BinaryOperator>(&I))
      if (BO->getOpcode() == Instruction::And &&
          BO->getType() == FixedVectorType::get(Type::getInt1Ty(Ctx), 4))
        ++AndCount;
    if (auto *RCI = dyn_cast<CallInst>(&I)) {
      Function *Callee = RCI->getCalledFunction();
      EXPECT_FALSE(Callee &&
                   Callee->getIntrinsicID() == Intrinsic::vector_reduce_and);
    }
  }
  EXPECT_EQ(FCmpCount, 4u);
  EXPECT_EQ(AndCount, 3u);
}

TEST(SIMDizeTest, DecomposesHomogeneousVectorizableIntrinsicCall) {
  // Roadmap H6g-b-a-i-a-i-b: `llvm.maxnum` (and `llvm.minnum`/`llvm.smin`/
  // `llvm.smax`/...) over an already-decomposed divergent vector operand --
  // the shape a GLSL `min`/`max`/`clamp` builtin over a vec-typed value
  // takes -- is the shape that actually dominates this row's own cited
  // `dEQP-VK.mesh_shader.ext.in_out.*` bucket once the row's own initial
  // `fcmp`/`icmp`/`select` fix lets those cases progress far enough to
  // reach it (see `simdize-vector-intrinsic.ll` and `agent_thoughts.md`).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = sitofp i32 %tid to float
      %va = insertelement <4 x float> poison, float %tidf, i32 0
      %vb = insertelement <4 x float> poison, float 1.000000e+00, i32 0
      %clamped = call <4 x float> @llvm.maxnum.v4f32(<4 x float> %va, <4 x float> %vb)
      %e0 = extractelement <4 x float> %clamped, i32 0
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare <4 x float> @llvm.maxnum.v4f32(<4 x float>, <4 x float>)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  unsigned MaxNumCount = 0;
  for (Instruction &I : instructions(F)) {
    // Never build an illegal vector-of-vector type.
    EXPECT_FALSE(I.getType()->isVectorTy() &&
                 cast<VectorType>(I.getType())->getElementType()->isVectorTy());
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    Function *Callee = CI->getCalledFunction();
    if (Callee && Callee->getIntrinsicID() == Intrinsic::maxnum) {
      // Each per-component call keeps the widened `<W x float>` overload,
      // not a scalar `float` one: "vectors become components, not nested
      // vectors" widens each original vector *lane* into a whole
      // `<W x elemT>` component, it does not scalarize per-SIMD-lane.
      EXPECT_EQ(CI->getType(), FixedVectorType::get(Type::getFloatTy(Ctx), 4));
      ++MaxNumCount;
    }
  }
  EXPECT_EQ(MaxNumCount, 4u);
}

TEST(SIMDizeTest, LeavesUniformVectorizableIntrinsicCallUnchanged) {
  // Roadmap H6m: a homogeneous "trivially vectorizable" intrinsic call
  // (`llvm.fabs.v3f32`) whose operand is *uniform* (not divergent) must be
  // left exactly as it is, exactly like any other uniform instruction --
  // "Uniform: leave it exactly as it is" in "Phase 4: Widening" -- rather
  // than being unconditionally decomposed into per-component wide vectors
  // the way `DecomposesHomogeneousVectorizableIntrinsicCall`'s *divergent*
  // one is. Reduced from a real `dEQP`-adjacent HLSL
  // `Feature/HLSLLib/abs.32.test` failure whose exact IR shape is
  // `{abs(In.xyz), abs(In.w)}`: `FunctionWidener::widenInstruction` used to
  // widen this call's vector-typed elementwise-vectorizable-intrinsic shape
  // unconditionally, ahead of the general `isDivergentAtDef` gate every
  // other producer/consumer shape respects, erasing and replacing a
  // uniform call whose own `extractelement` users -- correctly gated on
  // uniformity, and so left unchanged -- kept referencing the
  // since-erased value, observed as a `poison` read (see
  // `simdize-vector-intrinsic-uniform.ll` and `agent_thoughts.md` for the
  // full reduction).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %fabs = call <3 x float> @llvm.fabs.v3f32(
          <3 x float> <float -1.000000e+00, float -2.000000e+00, float -3.000000e+00>)
      %e0 = extractelement <3 x float> %fabs, i32 0
      %e1 = extractelement <3 x float> %fabs, i32 1
      %e2 = extractelement <3 x float> %fabs, i32 2
      %v0 = insertelement <3 x float> poison, float %e0, i32 0
      %v1 = insertelement <3 x float> %v0, float %e1, i32 1
      %v2 = insertelement <3 x float> %v1, float %e2, i32 2
      ret void
    }
    declare <3 x float> @llvm.fabs.v3f32(<3 x float>)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  unsigned FAbsV3Count = 0;
  for (Instruction &I : instructions(F)) {
    // The uniform `fabs` call, and every one of its `extractelement` users,
    // must survive completely unwidened -- no `poison` read anywhere.
    if (auto *EE = dyn_cast<ExtractElementInst>(&I))
      EXPECT_FALSE(isa<PoisonValue>(EE->getVectorOperand()));
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    Function *Callee = CI->getCalledFunction();
    if (Callee && Callee->getIntrinsicID() == Intrinsic::fabs &&
        CI->getType() == FixedVectorType::get(Type::getFloatTy(Ctx), 3))
      ++FAbsV3Count;
  }
  EXPECT_EQ(FAbsV3Count, 1u);
}

TEST(SIMDizeTest, DecomposesInsertElementChainIntoImageStore) {
  // Roadmap H19a: a `feme.cpu.image.store.2d.*` call's `Texel` operand is
  // vector-typed (unlike every other operand `widenImageCall` widens), so
  // it needs the same per-component decomposition
  // `DecomposesInsertElementChainIntoResourceStore` above already exercises
  // for `feme.cpu.resource.store.*` -- confirmed by reducing a real failing
  // `dEQP-VK.image.load_store.with_format.2d.*` case down to its exact IR
  // shape.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %image_heap, i32 %image_heap_count) #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = uitofp i32 %tid to float
      %v0 = insertelement <4 x float> poison, float %tidf, i32 0
      %v1 = insertelement <4 x float> %v0, float %tidf, i32 1
      %v2 = insertelement <4 x float> %v1, float %tidf, i32 2
      %v3 = insertelement <4 x float> %v2, float 1.000000e+00, i32 3
      call void @feme.cpu.image.store.2d.v4f32(
          ptr %image_heap, i32 %image_heap_count, i32 0, i32 %tid, i32 %tid,
          <4 x float> %v3, i1 true)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.cpu.image.store.2d.v4f32(ptr, i32, i32, i32, i32, <4 x float>, i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  // Decomposition never builds an illegal `<4 x <4 x float>>`, and the
  // scalarized call keeps its original, whole-vector `<4 x float>` Texel
  // argument type, reassembled per lane from the widened components.
  unsigned StoreCallCount = 0;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(I.getType()->isVectorTy() &&
                 cast<VectorType>(I.getType())->getElementType()->isVectorTy());
    auto *CI = dyn_cast<CallInst>(&I);
    if (CI && CI->getCalledFunction() &&
        CI->getCalledFunction()->getName() ==
            "feme.cpu.image.store.2d.v4f32") {
      ++StoreCallCount;
      EXPECT_TRUE(CI->getArgOperand(5)->getType()->isVectorTy());
    }
  }
  EXPECT_EQ(StoreCallCount, 4u);
}

TEST(SIMDizeTest, DecomposesInsertElementChainIntoImageStore2DMS) {
  // Roadmap H19l: `feme.cpu.image.store.2dms.v4i32`'s own `Texel` operand
  // (unlike its plain `Store2D` counterpart, already exercised by
  // `DecomposesInsertElementChainIntoImageStore` above) was never actually
  // reaching `widenImageCall` at all -- `matchImageCall`'s own `AllKinds`
  // lookup table never listed `Store2DMS`/`Store2DMSI32`, an oversight
  // from roadmap H19g's own original implementation, silently unexercised
  // until a real CTS re-run (once roadmap H19k's own `feme-cpu-linearize`
  // fix let one reach this far) confirmed every
  // `dEQP-VK.image.load_store_multisample.2d.*` case's own per-sample
  // `imageStore` call hit `feme-cpu-simdize`'s generic "used outside a
  // supported ... pattern" diagnostic. Confirmed via a real IR reduction of
  // that exact case (`glslangValidator` -> `feme-translate` ->
  // `feme-opt`'s `feme-cpu-fold-spirv-builtins,feme-cpu-prepare,...,
  // feme-cpu-linearize,feme-cpu-simdize` pipeline).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %image_heap, i32 %image_heap_count) #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %v0 = insertelement <4 x i32> poison, i32 %tid, i32 0
      %v1 = insertelement <4 x i32> %v0, i32 %tid, i32 1
      %v2 = insertelement <4 x i32> %v1, i32 %tid, i32 2
      %v3 = insertelement <4 x i32> %v2, i32 1, i32 3
      call void @feme.cpu.image.store.2dms.v4i32(
          ptr %image_heap, i32 %image_heap_count, i32 0, i32 %tid, i32 %tid,
          i32 0, <4 x i32> %v3, i1 true)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.cpu.image.store.2dms.v4i32(ptr, i32, i32, i32, i32, i32, <4 x i32>, i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  unsigned StoreCallCount = 0;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(I.getType()->isVectorTy() &&
                 cast<VectorType>(I.getType())->getElementType()->isVectorTy());
    auto *CI = dyn_cast<CallInst>(&I);
    if (CI && CI->getCalledFunction() &&
        CI->getCalledFunction()->getName() ==
            "feme.cpu.image.store.2dms.v4i32") {
      ++StoreCallCount;
      EXPECT_TRUE(CI->getArgOperand(6)->getType()->isVectorTy());
    }
  }
  EXPECT_EQ(StoreCallCount, 4u);
}

// Roadmap H6n: `llvm.spv.subgroup.id` is uniform for one widened-function
// call -- "which subgroup/wave this is" is exactly "which wave-loop
// iteration this is", the same value `Env.WaveIndex` already threads
// through the wave-body interface for `WorkgroupId` (see
// `FunctionWidener::replaceGroupIdCall`) -- so it substitutes directly for
// the wave-body's own `WaveIndex` parameter rather than ever being widened.
TEST(SIMDizeTest, ReplacesSubgroupIdWithWaveIndex) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %sg = call i32 @llvm.spv.subgroup.id()
      %tid = call i32 @llvm.spv.flattened.thread.id.in.group()
      %sum = add i32 %tid, %sg
      ret void
    }
    declare i32 @llvm.spv.subgroup.id()
    declare i32 @llvm.spv.flattened.thread.id.in.group()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  std::optional<WaveBodyEnv> Env = getWaveBodyEnv(*F);
  ASSERT_TRUE(Env);
  bool FoundSubgroupIdCall = false;
  bool FoundWideAddUsingWaveIndex = false;
  for (Instruction &I : instructions(F)) {
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction())
        if (Callee->getIntrinsicID() == Intrinsic::spv_subgroup_id)
          FoundSubgroupIdCall = true;
    if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
      if (BO->getOpcode() == Instruction::Add && BO->getType()->isVectorTy()) {
        for (Value *Op : BO->operands())
          if (auto *SV = dyn_cast<ShuffleVectorInst>(Op))
            if (auto *IE = dyn_cast<InsertElementInst>(SV->getOperand(0)))
              if (IE->getOperand(1) == Env->WaveIndex)
                FoundWideAddUsingWaveIndex = true;
      }
    }
  }
  EXPECT_FALSE(FoundSubgroupIdCall);
  EXPECT_TRUE(FoundWideAddUsingWaveIndex);
}

// Roadmap H6n: `llvm.spv.num.subgroups` folds directly to a compile-time
// constant -- `ceil(NumThreadsX * NumThreadsY * NumThreadsZ / WaveSize)`,
// mirroring feme/docs/FeMeCPUDesign.md's own `group = ceil(GroupSize / W)
// waves` formula -- rather than being widened or threaded through the
// wave-body interface. `hlsl.numthreads` is "10,1,1" and the wave size
// below is 4, so `ceil(10 / 4) == 3`.
TEST(SIMDizeTest, FoldsNumSubgroupsToCompileTimeConstant) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %ns = call i32 @llvm.spv.num.subgroups()
      %tid = call i32 @llvm.spv.flattened.thread.id.in.group()
      %sum = add i32 %tid, %ns
      ret void
    }
    declare i32 @llvm.spv.num.subgroups()
    declare i32 @llvm.spv.flattened.thread.id.in.group()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="10,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  bool FoundNumSubgroupsCall = false;
  bool FoundWideAddUsingConstantThree = false;
  for (Instruction &I : instructions(F)) {
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction())
        if (Callee->getIntrinsicID() == Intrinsic::spv_num_subgroups)
          FoundNumSubgroupsCall = true;
    if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
      if (BO->getOpcode() == Instruction::Add && BO->getType()->isVectorTy()) {
        for (Value *Op : BO->operands())
          if (auto *CV = dyn_cast<ConstantDataVector>(Op))
            if (CV->getSplatValue() &&
                cast<ConstantInt>(CV->getSplatValue())->equalsInt(3))
              FoundWideAddUsingConstantThree = true;
      }
    }
  }
  EXPECT_FALSE(FoundNumSubgroupsCall);
  EXPECT_TRUE(FoundWideAddUsingConstantThree);
}

// Roadmap H6o: `llvm.spv.num.workgroups` (SPIR-V's `NumWorkgroups`
// builtin, the dispatch's own grid size) is uniform for one
// widened-function call, exactly like `WorkgroupId`/`llvm.spv.group.id` --
// but, unlike `NumSubgroups` (roadmap H6n), it is a genuine *runtime*
// dispatch-time value, not a compile-time constant derivable from
// `hlsl.numthreads`. It therefore substitutes directly for the new
// `Env.GroupCountX/Y/Z` wave-body parameters (threaded through by
// `feme::cpu::EntryWrapperPass` from `FemeDispatchArgs::GroupCount`)
// rather than being widened or folded to a constant, mirroring how
// `WorkgroupId` substitutes for `Env.GroupIDX/Y/Z`.
TEST(SIMDizeTest, ReplacesNumWorkgroupsWithGroupCount) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %nwx = call i32 @llvm.spv.num.workgroups(i32 0)
      %tid = call i32 @llvm.spv.flattened.thread.id.in.group()
      %sum = add i32 %tid, %nwx
      ret void
    }
    declare i32 @llvm.spv.num.workgroups(i32)
    declare i32 @llvm.spv.flattened.thread.id.in.group()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  std::optional<WaveBodyEnv> Env = getWaveBodyEnv(*F);
  ASSERT_TRUE(Env);
  bool FoundNumWorkgroupsCall = false;
  bool FoundWideAddUsingGroupCountX = false;
  for (Instruction &I : instructions(F)) {
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction())
        if (Callee->getIntrinsicID() == Intrinsic::spv_num_workgroups)
          FoundNumWorkgroupsCall = true;
    if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
      if (BO->getOpcode() == Instruction::Add && BO->getType()->isVectorTy()) {
        for (Value *Op : BO->operands())
          if (auto *SV = dyn_cast<ShuffleVectorInst>(Op))
            if (auto *IE = dyn_cast<InsertElementInst>(SV->getOperand(0)))
              if (IE->getOperand(1) == Env->GroupCountX)
                FoundWideAddUsingGroupCountX = true;
      }
    }
  }
  EXPECT_FALSE(FoundNumWorkgroupsCall);
  EXPECT_TRUE(FoundWideAddUsingGroupCountX);
}

// Roadmap H6n: a divergently-indexed `store` of a whole *vector*-typed
// value previously crashed (an `llvm::FixedVectorType::get` assertion
// failure trying to build an illegal `<4 x <4 x float>>` nested vector)
// inside the fully-generic scalarization fallback -- the only widening
// rule that applies to an ordinary `store` through an ordinary (neither
// groupshared nor `feme.cpu.masked.*`) address. This regression-tests
// that it instead clones the store once per lane, each with its own
// reassembled `<4 x float>` operand.
TEST(SIMDizeTest, ScalarizesDivergentVectorStoreOperandWithoutCrashing) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @arr = external global [4 x <4 x float>]

    define void @main() #0 {
      %tid = call i32 @llvm.spv.flattened.thread.id.in.group()
      %ptr = getelementptr [4 x <4 x float>], ptr @arr, i32 0, i32 %tid
      store <4 x float> <float 1.0, float 2.0, float 3.0, float 4.0>, ptr %ptr, align 16
      ret void
    }
    declare i32 @llvm.spv.flattened.thread.id.in.group()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  unsigned StoreCount = 0;
  for (Instruction &I : instructions(F))
    if (auto *SI = dyn_cast<StoreInst>(&I)) {
      ++StoreCount;
      EXPECT_TRUE(SI->getValueOperand()->getType()->isVectorTy());
      EXPECT_FALSE(cast<VectorType>(SI->getValueOperand()->getType())
                       ->getElementType()
                       ->isVectorTy());
    }
  EXPECT_EQ(StoreCount, 4u);
}

// (Roadmap L49) A genuinely dynamic (per-invocation) task-payload store
// offset -- here, the per-lane thread ID itself, standing in for a real
// shader's `gl_LocalInvocationIndex`-indexed payload access once
// `CanonicalizeStagePass` (roadmap L47) has resolved it -- must be widened
// into a real `<4 x i32>` vector by `widenMaskedTaskPayloadStore`, not kept
// scalar: before this fix, a non-`Constant` `Offset` was passed through
// unchanged, which is not even a valid reference once `SIMDizePass` has
// built an entirely new, widened function (the raw `Value` was never
// cloned/widened into it).
TEST(SIMDizeTest, WidensDynamicTaskPayloadStoreOffset) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = uitofp i32 %tid to float
      call void @feme.stage.task.payload.store.f32(i32 %tid, float %tidf)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.stage.task.payload.store.f32(i32, float)
    attributes #0 = { "hlsl.shader"="amplification" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  bool FoundWidenedStore = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || !CI->getCalledFunction())
      continue;
    if (!CI->getCalledFunction()->getName().starts_with(
            "feme.cpu.masked.task.payload.store"))
      continue;
    FoundWidenedStore = true;
    Value *Offset = CI->getArgOperand(0);
    ASSERT_TRUE(isa<FixedVectorType>(Offset->getType()));
    EXPECT_EQ(cast<FixedVectorType>(Offset->getType())->getNumElements(), 4u);
    EXPECT_EQ(CI->getCalledFunction()->getName(),
              "feme.cpu.masked.task.payload.store.v4i32.v4f32");
  }
  EXPECT_TRUE(FoundWidenedStore);
}

// (Roadmap L49) The load-side counterpart of
// `WidensDynamicTaskPayloadStoreOffset` above: `widenStageOp`'s own
// `TaskPayloadLoad` handling must likewise only keep `offset` scalar when
// it really is a compile-time `Constant`, widening it into a real
// `<4 x i32>` vector otherwise (and mangling the resulting callee name by
// both the (unwidened) result type and the (widened) offset type
// independently, per `getOrInsertStageOp`'s own new `TaskPayloadLoad`
// special case).
TEST(SIMDizeTest, WidensDynamicTaskPayloadLoadOffset) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %v = call float @feme.stage.task.payload.load.f32(i32 %tid)
      call void @feme.stage.task.payload.store.f32(i32 0, float %v)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare float @feme.stage.task.payload.load.f32(i32)
    declare void @feme.stage.task.payload.store.f32(i32, float)
    attributes #0 = { "hlsl.shader"="amplification" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  bool FoundWidenedLoad = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || !CI->getCalledFunction())
      continue;
    if (!CI->getCalledFunction()->getName().starts_with(
            "feme.stage.task.payload.load"))
      continue;
    FoundWidenedLoad = true;
    Value *Offset = CI->getArgOperand(0);
    ASSERT_TRUE(isa<FixedVectorType>(Offset->getType()));
    EXPECT_EQ(cast<FixedVectorType>(Offset->getType())->getNumElements(), 4u);
    ASSERT_TRUE(isa<FixedVectorType>(CI->getType()));
    EXPECT_EQ(CI->getCalledFunction()->getName(),
              "feme.stage.task.payload.load.v4f32.v4i32");
  }
  EXPECT_TRUE(FoundWidenedLoad);
}

// Roadmap L7s: `llvm.spv.subgroup.local.invocation.id` (SPIR-V's own
// per-lane subgroup-invocation-index builtin, `gl_SubgroupInvocationID`) is
// `llvm.dx.wave.getlaneindex`'s exact twin -- both classify as
// `BuiltinCallKind::LaneIndex` and are unconditionally widened into a
// `feme.cpu.builtin.lane_index` call regardless of their own uniformity
// classification. Before this fix, `feme::cpu::computeWaveUniformity`
// (WaveUniformity.cpp) was missing a `NeverUniform` case for the SPIR-V
// intrinsic (unlike its DXIL sibling, which was already present), so a
// purely-arithmetic consumer of its scalar result -- e.g. `urem`, computing
// `gl_SubgroupInvocationID % 32` the way a manual/"software" subgroup ballot
// does (see `dEQP-VK.subgroups.basic.compute.subgroupelect`'s own
// `sharedMemoryBallot` helper) -- was left classified uniform and so never
// widened, even though the call it read from was unconditionally replaced
// with a genuinely per-lane vector value: the leftover scalar `urem` ended
// up reading a poisoned, since-erased operand, silently producing wrong
// results (not a crash) rather than the correct widened computation.
TEST(SIMDizeTest, WidensArithmeticConsumingSubgroupLocalInvocationId) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %id = call i32 @llvm.spv.subgroup.local.invocation.id()
      %bit = urem i32 %id, 32
      ret void
    }
    declare i32 @llvm.spv.subgroup.local.invocation.id()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  bool FoundWideUrem = false;
  for (Instruction &I : instructions(F)) {
    auto *BO = dyn_cast<BinaryOperator>(&I);
    if (!BO || BO->getOpcode() != Instruction::URem)
      continue;
    FoundWideUrem = true;
    EXPECT_TRUE(BO->getType()->isVectorTy());
    // Neither operand should be `poison`: both must have been widened
    // together, the operand tracing all the way back to the (also-widened)
    // lane-index builtin call rather than a dangling, since-erased scalar.
    for (Value *Op : BO->operands())
      EXPECT_FALSE(isa<PoisonValue>(Op));
  }
  EXPECT_TRUE(FoundWideUrem);
}

TEST(SIMDizeTest, WidensVectorAllEqualFeedingUniformSelect) {
  // Roadmap L7t: `subgroupAllEqual`/`WaveActiveAllEqual` over a genuinely
  // per-lane-divergent vector operand (`bvec2`/`ivec3`/`vec4`/...) is the
  // one `WaveCallKind` whose own result type mirrors its operand's arity
  // (`int_spv_wave_all_equal`'s `LLVMScalarOrSameVectorWidth<0,
  // llvm_i1_ty>` -- see `widenWaveCall`'s own comment): a vector operand
  // yields a *vector* `<N x i1>` result, immediately folded down to a
  // single scalar `i1` by a separate `llvm.vector.reduce.and` call (the
  // shape `AllEqualConversionPattern` in SPIRVToLLVMPatterns.cpp always
  // emits). `subgroupAllEqual`'s own result stays wave-uniform regardless
  // of its operand's divergence (see `WaveUniformity.cpp`'s
  // `AlwaysUniform` override for `spv_wave_all_equal`), so a real
  // `tempRes |= subgroupAllEqual(valueEqual) ? 0x8 : 0x0`-shaped consumer
  // (a `select` whose condition is this uniform reduce-and result, but
  // whose true/false arms are both plain constants, so the whole `select`
  // is itself uniform too) is left "exactly as it is" by
  // `widenInstruction`'s generic uniform fallback -- meaning it never
  // queries `Widened`, and needs the reduce-and's own *raw* SSA use fixed
  // up directly, not merely recorded in the `Widened` map the way a
  // divergent consumer resolves it. Reduced from a real
  // `dEQP-VK.subgroups.vote.compute.subgroupallequal_bvec2` "0 / 7 values
  // passed" runtime-verification failure (an earlier, now-corrected fix
  // attempt crashed instead, on a `replaceAllUsesWith` of `CI`'s own
  // `<2 x i1>` type with a scalar `i1`; see `agent_thoughts.md`).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %lane0 = icmp eq i32 %tid, 0
      %lane1 = icmp eq i32 %tid, 1
      %v0 = insertelement <2 x i1> poison, i1 %lane0, i32 0
      %valueEqual = insertelement <2 x i1> %v0, i1 %lane1, i32 1
      %componentEqual = call <2 x i1> @llvm.spv.wave.all.equal.v2i1(<2 x i1> %valueEqual)
      %allEqual = call i1 @llvm.vector.reduce.and.v2i1(<2 x i1> %componentEqual)
      %sel = select i1 %allEqual, i32 8, i32 0
      %res = or i32 %sel, 1
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare <2 x i1> @llvm.spv.wave.all.equal.v2i1(<2 x i1>)
    declare i1 @llvm.vector.reduce.and.v2i1(<2 x i1>)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  // Never build an illegal vector-of-vector type, and never leave a
  // dangling `llvm.spv.wave.all.equal`/`llvm.vector.reduce.and` call
  // behind (both must be fully lowered/widened away).
  bool FoundSelect = false;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(I.getType()->isVectorTy() &&
                 cast<VectorType>(I.getType())->getElementType()->isVectorTy());
    if (auto *RCI = dyn_cast<CallInst>(&I)) {
      Function *Callee = RCI->getCalledFunction();
      EXPECT_FALSE(Callee &&
                   (Callee->getIntrinsicID() == Intrinsic::vector_reduce_and ||
                    Callee->getIntrinsicID() == Intrinsic::spv_wave_all_equal));
    }
    // The `select`'s own condition must be a genuine, real boolean value
    // -- never `poison` from a since-erased `llvm.vector.reduce.and` call
    // whose only use (this `select`) was never fixed up.
    if (auto *Sel = dyn_cast<SelectInst>(&I)) {
      FoundSelect = true;
      EXPECT_FALSE(isa<PoisonValue>(Sel->getCondition()));
    }
  }
  EXPECT_TRUE(FoundSelect);
}

// Roadmap H97: `FunctionWidener::widenMaskedAllocaGEP` (the widening rule
// for any `getelementptr` chained off a `feme.cpu.masked.load`/`.store`-
// touched local -- see `widenMaskedAlloca`'s own comment) copied every one
// of that GEP's *index* operands unchanged from the not-yet-widened
// function, on the assumption that a masked-alloca's own indexing is
// always lane-uniform (its own motivating case: a lane-uniform loop index
// into a `Function`-storage matrix local). That assumption does not hold
// when the index is itself genuinely divergent -- e.g. a per-lane vertex
// index reading back a small per-vertex array a vertex shader stored
// through earlier in the same function (`dEQP-VK.rasterization.culling.
// primitive_id`'s own real shape, reduced here) -- so the stale,
// not-widened index `Value*` from the soon-to-be-discarded old function
// silently became `poison` once that function's own dead instructions
// were erased, producing a `getelementptr`/`llvm.masked.gather` whose
// address is `poison` for every lane and crashing with a bare `SIGSEGV`
// at runtime (no diagnostic at all -- this was one of the four "new
// crash/hang bugs" roadmap milestone H97 set out to triage). The fix
// mirrors `widenGroupSharedGEP`'s own index handling: substitute an
// index's own widened `<W x T>` form when one exists, exactly like every
// other divergent operand in this file, rather than assuming every index
// here is uniform.
TEST(SIMDizeTest, WidensDivergentIndexIntoMaskedAllocaArray) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %out) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %idx = urem i32 %tid, 3
      %a = alloca [3 x i32], align 4
      %p0 = getelementptr [3 x i32], ptr %a, i32 0, i32 0
      call void @feme.cpu.masked.store.i32(i32 10, ptr %p0, i32 4, i1 true)
      %p1 = getelementptr [3 x i32], ptr %a, i32 0, i32 1
      call void @feme.cpu.masked.store.i32(i32 20, ptr %p1, i32 4, i1 true)
      %p2 = getelementptr [3 x i32], ptr %a, i32 0, i32 2
      call void @feme.cpu.masked.store.i32(i32 30, ptr %p2, i32 4, i1 true)
      %addr = getelementptr [3 x i32], ptr %a, i32 0, i32 %idx
      %val = call i32 @feme.cpu.masked.load.i32(ptr %addr, i32 4, i1 true, i32 0)
      %off = zext i32 %tid to i64
      %outp = getelementptr i32, ptr %out, i64 %off
      store i32 %val, ptr %outp
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.cpu.masked.store.i32(i32, ptr, i32, i1)
    declare i32 @feme.cpu.masked.load.i32(ptr, i32, i1, i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  // The widened gather reading the array back must never end up with a
  // `poison` address operand -- every operand of every `getelementptr`
  // feeding it must be a real value, not a since-erased dangling one.
  bool FoundGather = false;
  for (Instruction &I : instructions(F)) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(&I))
      for (Value *Idx : GEP->indices())
        EXPECT_FALSE(isa<PoisonValue>(Idx));
    auto *CI = dyn_cast<CallInst>(&I);
    if (CI && CI->getCalledFunction() &&
        CI->getCalledFunction()->getIntrinsicID() ==
            Intrinsic::masked_gather) {
      FoundGather = true;
      EXPECT_FALSE(isa<PoisonValue>(CI->getArgOperand(0)));
    }
  }
  EXPECT_TRUE(FoundGather);
}

} // namespace


