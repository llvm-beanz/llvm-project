//===- BuiltinCallsTest.cpp - Tests for feme.cpu.builtin.* calls ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/BuiltinCalls.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

TEST(BuiltinCallsTest, RoundTripsThreadId) {
  LLVMContext Ctx;
  Module M("M", Ctx);
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx), false);
  Function *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "f", M);
  BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> Builder(BB);

  BuiltinCallEnv Env;
  Env.GroupIDX = Builder.getInt32(1);
  Env.GroupIDY = Builder.getInt32(2);
  Env.GroupIDZ = Builder.getInt32(3);
  Env.WaveIndex = Builder.getInt32(0);

  CallInst *CI = createBuiltinCall(Builder, BuiltinCallKind::ThreadId, Env,
                                   /*WaveSize=*/4, /*NumThreadsX=*/8,
                                   /*NumThreadsY=*/2, /*NumThreadsZ=*/1,
                                   /*Component=*/0);
  ASSERT_TRUE(CI);
  EXPECT_TRUE(CI->getType()->isVectorTy());
  EXPECT_EQ(cast<FixedVectorType>(CI->getType())->getNumElements(), 4u);

  std::optional<MatchedBuiltinCall> Matched = matchBuiltinCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, BuiltinCallKind::ThreadId);
  EXPECT_EQ(Matched->WaveSize, 4u);
  EXPECT_EQ(Matched->NumThreadsX, 8u);
  EXPECT_EQ(Matched->NumThreadsY, 2u);
  EXPECT_EQ(Matched->NumThreadsZ, 1u);
  EXPECT_EQ(Matched->Component, 0u);
  EXPECT_EQ(Matched->Env.GroupIDX, Env.GroupIDX);
}

TEST(BuiltinCallsTest, RoundTripsLaneIndex) {
  LLVMContext Ctx;
  Module M("M", Ctx);
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx), false);
  Function *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "f", M);
  BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> Builder(BB);

  BuiltinCallEnv Env;
  CallInst *CI = createBuiltinCall(Builder, BuiltinCallKind::LaneIndex, Env,
                                   /*WaveSize=*/8, 0, 0, 0);
  ASSERT_TRUE(CI);
  EXPECT_EQ(CI->arg_size(), 0u);

  std::optional<MatchedBuiltinCall> Matched = matchBuiltinCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, BuiltinCallKind::LaneIndex);
  EXPECT_EQ(Matched->WaveSize, 8u);
}

TEST(BuiltinCallsTest, DoesNotMatchUnrelatedCall) {
  LLVMContext Ctx;
  Module M("M", Ctx);
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx), false);
  Function *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "f", M);
  BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> Builder(BB);
  CallInst *CI = Builder.CreateCall(F, {});
  EXPECT_FALSE(matchBuiltinCall(*CI));
}

} // namespace
