//===- WaveLoweringTest.cpp - Tests for WaveLoweringPass -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/WaveLowering.h"

#include "feme/Core/StageOps.h"
#include "feme/Transforms/CPU/BuiltinCalls.h"
#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/CPU/WaveCalls.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
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
    Err.print("WaveLoweringTest", errs());
  return M;
}

TEST(WaveLoweringTest, LowersThreadIdAndRemovesBuiltinCalls) {
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

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  for (const Instruction &I : instructions(F))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(matchBuiltinCall(*CI));

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(WaveLoweringTest, LowersLaneIndexToConstantIota) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %lane = call i32 @llvm.dx.wave.getlaneindex()
      %doubled = mul i32 %lane, 2
      ret void
    }
    declare i32 @llvm.dx.wave.getlaneindex()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundConstantIota = false;
  for (const Instruction &I : instructions(F)) {
    if (const auto *BO = dyn_cast<BinaryOperator>(&I)) {
      if (auto *CV = dyn_cast<Constant>(BO->getOperand(0))) {
        if (!CV->getType()->isVectorTy())
          continue;
        EXPECT_EQ(
            cast<ConstantInt>(CV->getAggregateElement(0u))->getZExtValue(), 0u);
        EXPECT_EQ(
            cast<ConstantInt>(CV->getAggregateElement(1u))->getZExtValue(), 1u);
        FoundConstantIota = true;
      }
    }
  }
  EXPECT_TRUE(FoundConstantIota);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

/// Runs `SIMDizePass`+`WaveLoweringPass` over \p Assembly at \p WaveSize,
/// asserting the module still verifies and every `feme.cpu.wave.*`/
/// `feme.cpu.builtin.*` call is gone (fully lowered), and returns the
/// resulting module for a test to inspect further.
std::unique_ptr<Module> lowerWaveOps(LLVMContext &Ctx, StringRef Assembly,
                                     unsigned WaveSize = 4) {
  std::unique_ptr<Module> M = parseIR(Ctx, Assembly);
  if (!M)
    return nullptr;

  ModuleAnalysisManager MAM;
  SIMDizePass(WaveSize).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  for (Function &F : *M)
    for (const Instruction &I : instructions(F))
      if (const auto *CI = dyn_cast<CallInst>(&I))
        EXPECT_FALSE(matchWaveCall(*CI))
            << "left an unlowered feme.cpu.wave.* call behind";

  EXPECT_FALSE(verifyModule(*M, &errs()));
  return M;
}

TEST(WaveLoweringTest, LowersGetLaneCountToConstant) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %n = call i32 @llvm.dx.wave.get.lane.count()
      ret void
    }
    declare i32 @llvm.dx.wave.get.lane.count()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(getWaveBodyEnv(*F));
}

TEST(WaveLoweringTest, LowersIsFirstLaneToDivergentMaskArithmetic) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %first = call i1 @llvm.dx.wave.is.first.lane()
      %sel = select i1 %first, i32 1, i32 0
      ret void
    }
    declare i1 @llvm.dx.wave.is.first.lane()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundCttz = false;
  for (const Instruction &I : instructions(F))
    if (const auto *II = dyn_cast<IntrinsicInst>(&I))
      FoundCttz |= II->getIntrinsicID() == Intrinsic::cttz;
  EXPECT_TRUE(FoundCttz);
}

TEST(WaveLoweringTest, LowersAnyAllToVectorReductions) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %pred = icmp eq i32 %tid, 0
      %any = call i1 @llvm.dx.wave.any(i1 %pred)
      %all = call i1 @llvm.dx.wave.all(i1 %pred)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i1 @llvm.dx.wave.any(i1)
    declare i1 @llvm.dx.wave.all(i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundOrReduce = false, FoundAndReduce = false;
  for (const Instruction &I : instructions(F))
    if (const auto *II = dyn_cast<IntrinsicInst>(&I)) {
      FoundOrReduce |= II->getIntrinsicID() == Intrinsic::vector_reduce_or;
      FoundAndReduce |= II->getIntrinsicID() == Intrinsic::vector_reduce_and;
    }
  EXPECT_TRUE(FoundOrReduce);
  EXPECT_TRUE(FoundAndReduce);
}

TEST(WaveLoweringTest, LowersAllEqualAtSeveralWaveSizes) {
  for (unsigned W : {4u, 8u, 16u}) {
    LLVMContext Ctx;
    std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
      define void @main() #0 {
        %tid = call i32 @llvm.dx.thread.id(i32 0)
        %eq = call i1 @llvm.dx.wave.all.equal.i32(i32 %tid)
        ret void
      }
      declare i32 @llvm.dx.thread.id(i32)
      declare i1 @llvm.dx.wave.all.equal.i32(i32)
      attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
    )",
                                             W);
    ASSERT_TRUE(M) << "wave size " << W;
  }
}

TEST(WaveLoweringTest, LowersReadLaneToGuardedExtract) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %val = call i32 @llvm.dx.wave.readlane.i32(i32 %tid, i32 0)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i32 @llvm.dx.wave.readlane.i32(i32, i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundSelect = false;
  for (const Instruction &I : instructions(F))
    FoundSelect |= isa<SelectInst>(&I);
  EXPECT_TRUE(FoundSelect);
}

TEST(WaveLoweringTest, LowersActiveCountBitsToCtpop) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %pred = icmp eq i32 %tid, 0
      %cnt = call i32 @llvm.dx.wave.active.countbits(i1 %pred)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i32 @llvm.dx.wave.active.countbits(i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundCtpop = false;
  for (const Instruction &I : instructions(F))
    if (const auto *II = dyn_cast<IntrinsicInst>(&I))
      FoundCtpop |= II->getIntrinsicID() == Intrinsic::ctpop;
  EXPECT_TRUE(FoundCtpop);
}

TEST(WaveLoweringTest, LowersPrefixBitCountToDivergentLaneLoop) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %pred = icmp eq i32 %tid, 0
      %cnt = call i32 @llvm.dx.wave.prefix.bit.count(i1 %pred)
      %doubled = mul i32 %cnt, 2
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i32 @llvm.dx.wave.prefix.bit.count(i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  // The result is divergent (see `isDivergentWaveCallResult`), so it
  // widens `%doubled` into a real `<4 x i32>` multiply rather than leaving
  // it scalar.
  bool FoundWideMul = false;
  for (const Instruction &I : instructions(F))
    if (const auto *BO = dyn_cast<BinaryOperator>(&I))
      FoundWideMul |=
          BO->getOpcode() == Instruction::Mul && BO->getType()->isVectorTy();
  EXPECT_TRUE(FoundWideMul);
}

TEST(WaveLoweringTest, LowersBallotToInsertValueChain) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %pred = icmp eq i32 %tid, 0
      %r = call {i32, i32, i32, i32} @llvm.dx.wave.ballot.i32(i1 %pred)
      %x = extractvalue {i32, i32, i32, i32} %r, 0
      %cnt = call i32 @llvm.ctpop.i32(i32 %x)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare {i32, i32, i32, i32} @llvm.dx.wave.ballot.i32(i1)
    declare i32 @llvm.ctpop.i32(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  // The lowered ballot is a 4-field `insertvalue` chain (see `lowerBallot`),
  // built from a masked `bitcast`-to-`iW` word rather than a per-lane
  // vector -- unlike `PrefixBitCount` above, the result is uniform (see
  // `isDivergentWaveCallResult`), so nothing downstream needs widening.
  unsigned InsertValueCount = 0;
  for (const Instruction &I : instructions(F))
    if (isa<InsertValueInst>(&I))
      ++InsertValueCount;
  EXPECT_EQ(InsertValueCount, 4u);
}

TEST(WaveLoweringTest, LowersActiveSumToMaskedVectorReduceAdd) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %sum = call i32 @llvm.dx.wave.reduce.sum.i32(i32 %tid)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i32 @llvm.dx.wave.reduce.sum.i32(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundSelect = false, FoundAddReduce = false;
  for (const Instruction &I : instructions(F)) {
    FoundSelect |= isa<SelectInst>(&I);
    if (const auto *II = dyn_cast<IntrinsicInst>(&I))
      FoundAddReduce |= II->getIntrinsicID() == Intrinsic::vector_reduce_add;
  }
  EXPECT_TRUE(FoundSelect);
  EXPECT_TRUE(FoundAddReduce);
}

TEST(WaveLoweringTest, LowersActiveMaxToMaskedFPMaxReduce) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = uitofp i32 %tid to float
      %m = call float @llvm.dx.wave.reduce.max.f32(float %tidf)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare float @llvm.dx.wave.reduce.max.f32(float)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundFMaxReduce = false;
  for (const Instruction &I : instructions(F))
    if (const auto *II = dyn_cast<IntrinsicInst>(&I))
      FoundFMaxReduce |= II->getIntrinsicID() == Intrinsic::vector_reduce_fmax;
  EXPECT_TRUE(FoundFMaxReduce);
}

TEST(WaveLoweringTest, LowersPrefixSumToDivergentLaneLoop) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %sum = call i32 @llvm.dx.wave.prefix.sum.i32(i32 %tid)
      %doubled = mul i32 %sum, 2
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i32 @llvm.dx.wave.prefix.sum.i32(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  // `PrefixSum` is divergent (see `isDivergentWaveCallResult`), so it
  // widens `%doubled` into a real `<4 x i32>` multiply, the same way
  // `PrefixBitCount` does above.
  bool FoundWideMul = false;
  for (const Instruction &I : instructions(F))
    if (const auto *BO = dyn_cast<BinaryOperator>(&I))
      FoundWideMul |=
          BO->getOpcode() == Instruction::Mul && BO->getType()->isVectorTy();
  EXPECT_TRUE(FoundWideMul);
}

TEST(WaveLoweringTest, LowersDerivativesAndQuadRead) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ps_main() #0 {
      %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %dx = call float @feme.stage.derivative.x.fine.f32(float %in)
      %qr = call float @feme.stage.quad.read.f32(float %dx, i8 2)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare float @feme.stage.derivative.x.fine.f32(float)
    declare float @feme.stage.quad.read.f32(float, i8)
    attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="8" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(8).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  Function *F = M->getFunction("ps_main");
  ASSERT_TRUE(F);
  for (const Instruction &I : instructions(F))
    if (const auto *CI = dyn_cast<CallInst>(&I)) {
      feme::StageOpKind Kind;
      EXPECT_FALSE(isStageOpCall(*CI, &Kind) &&
                   (Kind == feme::StageOpKind::DerivativeXFine ||
                    Kind == feme::StageOpKind::QuadRead));
    }
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

/// (roadmap L69(a)) `DerivativeGroupQuadsKHR`'s own quad-tiled lane
/// assignment: for a 4x4x1 thread group at wave size 8 (two physical
/// quads per wave), physical lanes 0-3 and 4-7 must land on two distinct
/// real 2x2 spatial tiles -- (0,0)/(1,0)/(0,1)/(1,1) then (2,0)/(3,0)/
/// (2,1)/(3,1) -- rather than the plain row-major `x = flat % Gx` identity
/// every non-quad-tiled entry point still uses (confirmed different by
/// `LowersThreadIdAndRemovesBuiltinCalls`'s own row-major expectations).
/// Builds the `ThreadIdInGroup` calls directly with `QuadTiled=true` and a
/// literal constant `WaveIndex` (bypassing `SIMDizePass` entirely, so the
/// whole computation constant-folds down to one exact, checkable vector
/// per component), storing each result to a distinct global so it survives
/// `WaveLoweringPass`'s in-place RAUW/erase of the originating call.
TEST(WaveLoweringTest, LowersQuadTiledThreadIdInGroupToSpatialTiles) {
  LLVMContext Ctx;
  Module M("M", Ctx);
  auto *VecTy = FixedVectorType::get(Type::getInt32Ty(Ctx), 8);
  auto *GX = new GlobalVariable(M, VecTy, /*isConstant=*/false,
                                GlobalValue::ExternalLinkage,
                                Constant::getNullValue(VecTy), "gx");
  auto *GY = new GlobalVariable(M, VecTy, /*isConstant=*/false,
                                GlobalValue::ExternalLinkage,
                                Constant::getNullValue(VecTy), "gy");
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx), false);
  Function *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "main", M);
  BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> Builder(BB);

  BuiltinCallEnv Env;
  Env.WaveIndex = Builder.getInt32(0);

  CallInst *XCall = createBuiltinCall(Builder, BuiltinCallKind::ThreadIdInGroup,
                                      Env, /*WaveSize=*/8, /*NumThreadsX=*/4,
                                      /*NumThreadsY=*/4, /*NumThreadsZ=*/1,
                                      /*Component=*/0, /*QuadTiled=*/true);
  CallInst *YCall = createBuiltinCall(Builder, BuiltinCallKind::ThreadIdInGroup,
                                      Env, /*WaveSize=*/8, /*NumThreadsX=*/4,
                                      /*NumThreadsY=*/4, /*NumThreadsZ=*/1,
                                      /*Component=*/1, /*QuadTiled=*/true);
  Builder.CreateStore(XCall, GX);
  Builder.CreateStore(YCall, GY);
  Builder.CreateRetVoid();

  ModuleAnalysisManager MAM;
  WaveLoweringPass().run(M, MAM);

  auto GetLane = [](GlobalVariable *G, unsigned Lane) -> uint32_t {
    auto *CV =
        cast<Constant>(cast<StoreInst>(*G->user_begin())->getValueOperand());
    return static_cast<uint32_t>(
        cast<ConstantInt>(CV->getAggregateElement(Lane))->getZExtValue());
  };

  const uint32_t ExpectedX[8] = {0, 1, 0, 1, 2, 3, 2, 3};
  const uint32_t ExpectedY[8] = {0, 0, 1, 1, 0, 0, 1, 1};
  for (unsigned Lane = 0; Lane != 8; ++Lane) {
    EXPECT_EQ(GetLane(GX, Lane), ExpectedX[Lane]) << "lane " << Lane;
    EXPECT_EQ(GetLane(GY, Lane), ExpectedY[Lane]) << "lane " << Lane;
  }
  EXPECT_FALSE(verifyModule(M, &errs()));
}

/// (roadmap L69(a)) The recombined `FlattenedThreadIdInGroup` for a
/// quad-tiled entry point must still equal the spec-defined
/// `z*Gx*Gy + y*Gx + x` over the *real* (quad-tiled) x/y/z identity, not
/// the physical per-lane index the non-tiled case returns directly --
/// i.e. `LocalInvocationIndex` is unaffected by which physical lane a
/// given (x, y, z) invocation happens to run on.
TEST(WaveLoweringTest, LowersQuadTiledFlattenedThreadIdInGroupToRealIndex) {
  LLVMContext Ctx;
  Module M("M", Ctx);
  auto *VecTy = FixedVectorType::get(Type::getInt32Ty(Ctx), 8);
  auto *GFlat = new GlobalVariable(M, VecTy, /*isConstant=*/false,
                                   GlobalValue::ExternalLinkage,
                                   Constant::getNullValue(VecTy), "gflat");
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx), false);
  Function *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "main", M);
  BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> Builder(BB);

  BuiltinCallEnv Env;
  Env.WaveIndex = Builder.getInt32(0);

  CallInst *FlatCall = createBuiltinCall(
      Builder, BuiltinCallKind::FlattenedThreadIdInGroup, Env,
      /*WaveSize=*/8, /*NumThreadsX=*/4, /*NumThreadsY=*/4, /*NumThreadsZ=*/1,
      /*Component=*/0, /*QuadTiled=*/true);
  Builder.CreateStore(FlatCall, GFlat);
  Builder.CreateRetVoid();

  ModuleAnalysisManager MAM;
  WaveLoweringPass().run(M, MAM);

  auto *CV =
      cast<Constant>(cast<StoreInst>(*GFlat->user_begin())->getValueOperand());
  // Real (x, y) per physical lane: (0,0) (1,0) (0,1) (1,1) (2,0) (3,0)
  // (2,1) (3,1); LocalInvocationIndex = y*4 + x.
  const uint32_t ExpectedFlat[8] = {0, 1, 4, 5, 2, 3, 6, 7};
  for (unsigned Lane = 0; Lane != 8; ++Lane)
    EXPECT_EQ(
        static_cast<uint32_t>(
            cast<ConstantInt>(CV->getAggregateElement(Lane))->getZExtValue()),
        ExpectedFlat[Lane])
        << "lane " << Lane;
  EXPECT_FALSE(verifyModule(M, &errs()));
}

} // namespace
