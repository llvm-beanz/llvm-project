//===- ResourceCallsTest.cpp - Tests for `feme.cpu.resource.*` calls -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/ResourceCalls.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

class ResourceCallsTest : public testing::Test {
protected:
  LLVMContext Ctx;
  std::unique_ptr<Module> M;
  Function *F = nullptr;
  BasicBlock *BB = nullptr;

  void SetUp() override {
    M = std::make_unique<Module>("M", Ctx);
    FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx), false);
    F = Function::Create(FTy, Function::ExternalLinkage, "main", M.get());
    BB = BasicBlock::Create(Ctx, "entry", F);
  }

  ResourceCallEnv makeEnv(IRBuilderBase &Builder) {
    ResourceCallEnv Env;
    Env.ResourceHeap = ConstantPointerNull::get(PointerType::get(Ctx, 0));
    Env.ResourceHeapCount = Builder.getInt32(4);
    return Env;
  }
};

TEST_F(ResourceCallsTest, MangleNameMatchesDesignExample) {
  // The literal example from "Lowering" in feme/docs/FeMeCPUDesign.md.
  Type *V4F32 = FixedVectorType::get(Type::getFloatTy(Ctx), 4);
  EXPECT_EQ(mangleResourceCallName(ResourceCallKind::LoadTyped, V4F32),
            "feme.cpu.resource.load.typed.v4f32");
  EXPECT_EQ(mangleResourceCallName(ResourceCallKind::StoreTyped, V4F32),
            "feme.cpu.resource.store.typed.v4f32");
}

TEST_F(ResourceCallsTest, MangleScalarAndIntegerTypes) {
  EXPECT_EQ(
      mangleResourceCallName(ResourceCallKind::LoadRaw, Type::getInt32Ty(Ctx)),
      "feme.cpu.resource.load.raw.i32");
  EXPECT_EQ(
      mangleResourceCallName(ResourceCallKind::LoadRaw, Type::getInt8Ty(Ctx)),
      "feme.cpu.resource.load.raw.i8");
  EXPECT_EQ(
      mangleResourceCallName(ResourceCallKind::LoadTyped, Type::getHalfTy(Ctx)),
      "feme.cpu.resource.load.typed.f16");
}

TEST_F(ResourceCallsTest, CreateTypedLoadRoundTrips) {
  IRBuilder<> Builder(BB);
  ResourceCallEnv Env = makeEnv(Builder);
  Type *V4F32 = FixedVectorType::get(Type::getFloatTy(Ctx), 4);
  Value *DescIdx = Builder.getInt32(3);
  Value *ElemIdx = Builder.getInt64(7);
  Value *Mask = Builder.getTrue();

  CallInst *CI = createTypedLoad(Builder, Env, DescIdx, ElemIdx, Mask, V4F32);
  ASSERT_TRUE(CI);
  EXPECT_EQ(CI->getType(), V4F32);
  EXPECT_EQ(CI->getCalledFunction()->getName(),
            "feme.cpu.resource.load.typed.v4f32");

  std::optional<MatchedResourceCall> Matched = matchResourceCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ResourceCallKind::LoadTyped);
  EXPECT_EQ(Matched->ElementType, V4F32);
  EXPECT_EQ(Matched->DescriptorIndex, DescIdx);
  EXPECT_EQ(Matched->Offset, ElemIdx);
  EXPECT_EQ(Matched->Mask, Mask);
  EXPECT_EQ(Matched->StoredValue, nullptr);
  EXPECT_EQ(Matched->Env.ResourceHeap, Env.ResourceHeap);
}

TEST_F(ResourceCallsTest, CreateTypedStoreRoundTrips) {
  IRBuilder<> Builder(BB);
  ResourceCallEnv Env = makeEnv(Builder);
  Value *DescIdx = Builder.getInt32(1);
  Value *ElemIdx = Builder.getInt64(0);
  Value *Mask = Builder.getTrue();
  Value *Val = ConstantInt::get(Type::getInt32Ty(Ctx), 42);

  CallInst *CI = createTypedStore(Builder, Env, DescIdx, ElemIdx, Val, Mask);
  ASSERT_TRUE(CI);
  EXPECT_TRUE(CI->getType()->isVoidTy());
  EXPECT_EQ(CI->getCalledFunction()->getName(),
            "feme.cpu.resource.store.typed.i32");

  std::optional<MatchedResourceCall> Matched = matchResourceCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ResourceCallKind::StoreTyped);
  EXPECT_EQ(Matched->StoredValue, Val);
  EXPECT_EQ(Matched->ElementType, Val->getType());
}

TEST_F(ResourceCallsTest, CreateRawLoadAndStoreRoundTrip) {
  IRBuilder<> Builder(BB);
  ResourceCallEnv Env = makeEnv(Builder);
  Value *DescIdx = Builder.getInt32(2);
  Value *ByteOffset = Builder.getInt64(16);
  Value *Mask = Builder.getTrue();

  CallInst *Load = createRawLoad(Builder, Env, DescIdx, ByteOffset, Mask,
                                 Type::getInt32Ty(Ctx));
  std::optional<MatchedResourceCall> MatchedLoad = matchResourceCall(*Load);
  ASSERT_TRUE(MatchedLoad);
  EXPECT_EQ(MatchedLoad->Kind, ResourceCallKind::LoadRaw);

  Value *Val = ConstantInt::get(Type::getInt32Ty(Ctx), 7);
  CallInst *Store =
      createRawStore(Builder, Env, DescIdx, ByteOffset, Val, Mask);
  std::optional<MatchedResourceCall> MatchedStore = matchResourceCall(*Store);
  ASSERT_TRUE(MatchedStore);
  EXPECT_EQ(MatchedStore->Kind, ResourceCallKind::StoreRaw);
  EXPECT_EQ(MatchedStore->StoredValue, Val);
}

TEST_F(ResourceCallsTest, MemoryEffectsAreArgMemOnly) {
  Type *I32 = Type::getInt32Ty(Ctx);
  Function *Load = getOrInsertResourceCall(*M, ResourceCallKind::LoadRaw, I32);
  Function *Store =
      getOrInsertResourceCall(*M, ResourceCallKind::StoreRaw, I32);
  EXPECT_TRUE(Load->getMemoryEffects().onlyAccessesArgPointees());
  EXPECT_TRUE(Load->getMemoryEffects().onlyReadsMemory());
  EXPECT_TRUE(Store->getMemoryEffects().onlyAccessesArgPointees());
  EXPECT_TRUE(Store->getMemoryEffects().onlyWritesMemory());
}

TEST_F(ResourceCallsTest, MatchResourceCallRejectsUnrelatedCall) {
  IRBuilder<> Builder(BB);
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx), false);
  Function *Unrelated =
      Function::Create(FTy, Function::ExternalLinkage,
                       "feme.cpu.resource.load.typed.not_a_real_call", M.get());
  CallInst *CI = Builder.CreateCall(Unrelated);
  EXPECT_FALSE(matchResourceCall(*CI).has_value());
}

} // namespace
