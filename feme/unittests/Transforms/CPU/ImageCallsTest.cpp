//===- ImageCallsTest.cpp - Tests for `feme.cpu.image.*` calls -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/ImageCalls.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "gtest/gtest.h"

#include <limits>

using namespace feme::cpu;
using namespace llvm;

namespace {

class ImageCallsTest : public testing::Test {
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

  ImageCallEnv makeEnv(IRBuilderBase &Builder) {
    ImageCallEnv Env;
    Env.ImageHeap = ConstantPointerNull::get(PointerType::get(Ctx, 0));
    Env.ImageHeapCount = Builder.getInt32(1);
    Env.SamplerHeap = ConstantPointerNull::get(PointerType::get(Ctx, 0));
    Env.SamplerHeapCount = Builder.getInt32(1);
    return Env;
  }
};

// Roadmap H19l: `matchImageCall`'s own `AllKinds` lookup table (used to
// recognize a call by its mangled callee name before dispatching to the
// per-kind operand-extraction switch) never listed `Store2DMS`/
// `Store2DMSI32` at all -- an oversight from roadmap H19g's own original
// implementation, silently unexercised the whole time because H19g's own
// feature bit stayed `VK_FALSE` (blocked first by roadmap H19k's `feme-cpu-
// linearize` gap) until a real CTS re-run could ever reach
// `feme::cpu::FunctionWidener::widenImageCall`'s `matchImageCall` call for
// this specific call kind. The result: every multisampled storage-image
// store was silently never widened at all, and its own vector-typed
// `Texel` operand's divergent-vector-decomposition consumer check (in
// `checkAndPrepareForWidening`) fell through to `feme-cpu-simdize`'s
// generic "not yet supported" diagnostic -- a shape that looked identical
// to a real missing decomposition pattern, but was actually a simple
// name-table omission, confirmed by reducing a real failing
// `dEQP-VK.image.load_store_multisample.2d.*` case down to its exact IR
// shape (see "Roadmap H19l: measured impact" in VulkanCTSReport.md).
TEST_F(ImageCallsTest, MatchesStore2DMSCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *Texel = ConstantVector::get(
      {ConstantFP::get(Builder.getFloatTy(), 0.0),
       ConstantFP::get(Builder.getFloatTy(), 0.0),
       ConstantFP::get(Builder.getFloatTy(), 0.0),
       ConstantFP::get(Builder.getFloatTy(), 1.0)});
  CallInst *CI = createStore2DMS(Builder, Env, Builder.getInt32(3),
                                 Builder.getInt32(1), Builder.getInt32(2),
                                 Builder.getInt32(0), Texel,
                                 Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Store2DMS);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->U, Builder.getInt32(1));
  EXPECT_EQ(Matched->V, Builder.getInt32(2));
  EXPECT_EQ(Matched->Sample, Builder.getInt32(0));
  EXPECT_EQ(Matched->Texel, Texel);
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

TEST_F(ImageCallsTest, MatchesStore2DMSI32Call) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *Texel = ConstantVector::get(
      {Builder.getInt32(0), Builder.getInt32(0), Builder.getInt32(0),
       Builder.getInt32(0)});
  CallInst *CI = createStore2DMSI32(Builder, Env, Builder.getInt32(3),
                                    Builder.getInt32(1), Builder.getInt32(2),
                                    Builder.getInt32(0), Texel,
                                    Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Store2DMSI32);
  EXPECT_EQ(Matched->Texel, Texel);
  EXPECT_EQ(Matched->Sample, Builder.getInt32(0));
}

// Roadmap H19m: `Store2DArrayMS`/`Store2DArrayMSI32` are new call kinds
// combining `Store2DArray`'s own `Layer` operand and `Store2DMS`'s own
// `Sample` operand -- neither existing kind had a spare operand slot for
// the other axis, so a dedicated pair of kinds was added rather than
// widening either in place (see `ImageCalls.h`'s own comment). This test
// guards against a repeat of H19l's own `AllKinds` omission bug for these
// two new kinds specifically.
TEST_F(ImageCallsTest, MatchesStore2DArrayMSCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *Texel = ConstantVector::get(
      {ConstantFP::get(Builder.getFloatTy(), 0.0),
       ConstantFP::get(Builder.getFloatTy(), 0.0),
       ConstantFP::get(Builder.getFloatTy(), 0.0),
       ConstantFP::get(Builder.getFloatTy(), 1.0)});
  CallInst *CI = createStore2DArrayMS(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(1),
      Builder.getInt32(2), Builder.getInt32(4), Builder.getInt32(0), Texel,
      Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Store2DArrayMS);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->U, Builder.getInt32(1));
  EXPECT_EQ(Matched->V, Builder.getInt32(2));
  EXPECT_EQ(Matched->Layer, Builder.getInt32(4));
  EXPECT_EQ(Matched->Sample, Builder.getInt32(0));
  EXPECT_EQ(Matched->Texel, Texel);
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

TEST_F(ImageCallsTest, MatchesStore2DArrayMSI32Call) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *Texel = ConstantVector::get(
      {Builder.getInt32(0), Builder.getInt32(0), Builder.getInt32(0),
       Builder.getInt32(0)});
  CallInst *CI = createStore2DArrayMSI32(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(1),
      Builder.getInt32(2), Builder.getInt32(4), Builder.getInt32(0), Texel,
      Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Store2DArrayMSI32);
  EXPECT_EQ(Matched->Layer, Builder.getInt32(4));
  EXPECT_EQ(Matched->Sample, Builder.getInt32(0));
  EXPECT_EQ(Matched->Texel, Texel);
}

// Roadmap H19m: `Load2DArrayI32` was widened in place to add a `Sample`
// operand before `Mask` -- confirm `matchImageCall` extracts a real
// (non-zero) sample value through the new operand position correctly.
TEST_F(ImageCallsTest, MatchesLoad2DArrayI32CallWithRealSample) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createLoad2DArrayI32(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(1),
      Builder.getInt32(2), Builder.getInt32(4), Builder.getInt32(0),
      Builder.getInt32(5), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Load2DArrayI32);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->U, Builder.getInt32(1));
  EXPECT_EQ(Matched->V, Builder.getInt32(2));
  EXPECT_EQ(Matched->Layer, Builder.getInt32(4));
  EXPECT_EQ(Matched->Lod, Builder.getInt32(0));
  EXPECT_EQ(Matched->Sample, Builder.getInt32(5));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// Roadmap H8v: `createAtomicAdd2D`/`matchImageCall` round-trip a plain RMW
// atomic's `(image_heap, image_heap_count, image_index, x, y, value, mask)`
// scalar-shaped call correctly, including `AtomicValue` -- the field
// distinguishing an atomic from every ordinary Load*/Store* kind above
// (which use `Texel` instead, and never a bare scalar `Value`).
TEST_F(ImageCallsTest, MatchesAtomicAdd2DCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI =
      createAtomicAdd2D(Builder, Env, Builder.getInt32(3), Builder.getInt32(1),
                        Builder.getInt32(2), Builder.getInt32(7),
                        Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::AtomicAdd2D);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->U, Builder.getInt32(1));
  EXPECT_EQ(Matched->V, Builder.getInt32(2));
  EXPECT_EQ(Matched->AtomicValue, Builder.getInt32(7));
  EXPECT_EQ(Matched->Comparator, nullptr);
  EXPECT_EQ(Matched->Texel, nullptr);
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// Every other RMW kind (`AtomicUMax2D` picked arbitrarily) shares
// `AtomicAdd2D`'s own shape -- confirm the callee name alone is what
// `matchImageCall` uses to distinguish the RMW operation actually
// performed (`AtomicRMWInst::getOperation()` is already resolved by the
// time `SPIRVResourceLowering.cpp` picks a `createAtomic*2D` wrapper, so
// `matchImageCall`'s own job here is only to recognize the resulting
// callee, not to reinterpret the operation).
TEST_F(ImageCallsTest, MatchesAtomicUMax2DCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createAtomicUMax2D(
      Builder, Env, Builder.getInt32(0), Builder.getInt32(4),
      Builder.getInt32(5), Builder.getInt32(9), Builder.getInt1(false));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::AtomicUMax2D);
  EXPECT_EQ(Matched->AtomicValue, Builder.getInt32(9));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(false));
}

// Roadmap H8v: `createAtomicCompareExchange2D`/`matchImageCall` round-trip
// the one atomic kind with an extra `Comparator` operand before `Value`.
TEST_F(ImageCallsTest, MatchesAtomicCompareExchange2DCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createAtomicCompareExchange2D(
      Builder, Env, Builder.getInt32(2), Builder.getInt32(1),
      Builder.getInt32(2), Builder.getInt32(0), Builder.getInt32(42),
      Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::AtomicCompareExchange2D);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(2));
  EXPECT_EQ(Matched->U, Builder.getInt32(1));
  EXPECT_EQ(Matched->V, Builder.getInt32(2));
  EXPECT_EQ(Matched->Comparator, Builder.getInt32(0));
  EXPECT_EQ(Matched->AtomicValue, Builder.getInt32(42));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// Roadmap L61(c): `matchImageCall`'s own `Sample1D`/`Sample1DArray` cases
// hardcoded the pre-fix `arg_size()` (10/11) `createSample1D`/
// `createSample1DArray` had before this row added a real `Bias`/
// `MinLodClamp` pair (see `ImageCalls.cpp`'s own updated cases) --
// exactly the same "arg-count table not kept in sync with a real operand
// addition" shape roadmap H19l already caught once for
// `Store2DMS`/`Store2DMSI32` above, here newly reachable the moment
// `hasOnlySupportedImageUses` (roadmap L61(c)) started accepting a
// `Bias`/`MinLodClamp` sample against `Plain1D`/`Array1D` at all: a real
// `dEQP-VK.glsl.texture_functions.texture.sampler1d_bias_*_fragment`
// re-run (see `VulkanCTSReport.md`) hit `feme-cpu-simdize`'s generic
// "not yet supported" diagnostic for the exact same reason -- `AllKinds`
// found the callee by name, but the arg-count guard below it silently
// rejected the call, so `matchImageCall` returned `std::nullopt` for a
// perfectly real, well-formed `feme.cpu.image.sample.1d.v4f32` call.
TEST_F(ImageCallsTest, MatchesSample1DCallWithBiasAndMinLodClamp) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *U = ConstantFP::get(Builder.getFloatTy(), 0.25);
  Value *DUdX = ConstantFP::get(Builder.getFloatTy(), 0.1);
  Value *DUdY = ConstantFP::get(Builder.getFloatTy(), 0.2);
  Value *Lod = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Bias = ConstantFP::get(Builder.getFloatTy(), 1.0);
  Value *Offset = Builder.getInt32(7);
  Value *MinLodClamp = ConstantFP::get(Builder.getFloatTy(), 0.5);
  CallInst *CI =
      createSample1D(Builder, Env, Builder.getInt32(2), Builder.getInt32(1), U,
                     DUdX, DUdY, Lod, Builder.getInt1(false), Bias, Offset,
                     MinLodClamp, Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Sample1D);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(2));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(1));
  EXPECT_EQ(Matched->U, U);
  EXPECT_EQ(Matched->DUdX, DUdX);
  EXPECT_EQ(Matched->DUdY, DUdY);
  EXPECT_EQ(Matched->Lod, Lod);
  EXPECT_EQ(Matched->UseExplicitLod, Builder.getInt1(false));
  EXPECT_EQ(Matched->Bias, Bias);
  EXPECT_EQ(Matched->OffsetX, Offset);
  EXPECT_EQ(Matched->OffsetY, nullptr);
  EXPECT_EQ(Matched->MinLodClamp, MinLodClamp);
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

TEST_F(ImageCallsTest, MatchesSample1DArrayCallWithBiasAndMinLodClamp) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *U = ConstantFP::get(Builder.getFloatTy(), 0.25);
  Value *ArrayLayer = ConstantFP::get(Builder.getFloatTy(), 2.0);
  Value *DUdX = ConstantFP::get(Builder.getFloatTy(), 0.1);
  Value *DUdY = ConstantFP::get(Builder.getFloatTy(), 0.2);
  Value *Lod = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Bias = ConstantFP::get(Builder.getFloatTy(), 1.0);
  Value *Offset = Builder.getInt32(7);
  Value *MinLodClamp = ConstantFP::get(Builder.getFloatTy(), 0.5);
  CallInst *CI = createSample1DArray(
      Builder, Env, Builder.getInt32(2), Builder.getInt32(1), U, ArrayLayer,
      DUdX, DUdY, Lod, Builder.getInt1(false), Bias, Offset, MinLodClamp,
      Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Sample1DArray);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(2));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(1));
  EXPECT_EQ(Matched->U, U);
  EXPECT_EQ(Matched->ArrayLayer, ArrayLayer);
  EXPECT_EQ(Matched->DUdX, DUdX);
  EXPECT_EQ(Matched->DUdY, DUdY);
  EXPECT_EQ(Matched->Lod, Lod);
  EXPECT_EQ(Matched->UseExplicitLod, Builder.getInt1(false));
  EXPECT_EQ(Matched->Bias, Bias);
  EXPECT_EQ(Matched->OffsetX, Offset);
  EXPECT_EQ(Matched->OffsetY, nullptr);
  EXPECT_EQ(Matched->MinLodClamp, MinLodClamp);
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// (Roadmap L62) The depth-comparison counterparts of the two tests above,
// added for the same reason: `createSampleCmp1D`/`createSampleCmpArray1D`
// each grew a real `Bias`/`MinLodClamp` operand pair, so `matchImageCall`'s
// own hardcoded arg-count guards had to grow with them (11 -> 13, 12 -> 14).
// Roadmap L66(f) grew both again with a real `DUdX`/`DUdY` scalar
// derivative pair (13 -> 15, 14 -> 16); these two tests pass zero
// constants for that pair, mirroring an explicit-LOD/non-`Grad` caller,
// while `MatchesSampleCmp1DCallWithRealGradDerivatives`/
// `MatchesSampleCmpArray1DCallWithRealGradDerivatives` below cover a real
// nonzero derivative. Roadmap L66(k) grew both once more with a real
// bare-scalar `Offset` operand (15 -> 16, 16 -> 17); these two tests pass
// a zero constant for it, mirroring the trivial always-zero case, while
// `MatchesSampleCmp1DCallWithRealOffset`/
// `MatchesSampleCmpArray1DCallWithRealOffset` below cover a real nonzero
// offset.
TEST_F(ImageCallsTest, MatchesSampleCmp1DCallWithBiasAndMinLodClamp) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *U = ConstantFP::get(Builder.getFloatTy(), 0.25);
  Value *DUdX = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *DUdY = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Lod = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Dref = ConstantFP::get(Builder.getFloatTy(), 0.75);
  Value *Bias = ConstantFP::get(Builder.getFloatTy(), 1.0);
  Value *Offset = Builder.getInt32(0);
  Value *MinLodClamp = ConstantFP::get(Builder.getFloatTy(), 0.5);
  CallInst *CI =
      createSampleCmp1D(Builder, Env, Builder.getInt32(2), Builder.getInt32(1),
                        U, DUdX, DUdY, Lod, Builder.getInt1(false), Dref, Bias,
                        Offset, MinLodClamp, Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::SampleCmp1D);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(2));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(1));
  EXPECT_EQ(Matched->U, U);
  EXPECT_EQ(Matched->DUdX, DUdX);
  EXPECT_EQ(Matched->DUdY, DUdY);
  EXPECT_EQ(Matched->Lod, Lod);
  EXPECT_EQ(Matched->UseExplicitLod, Builder.getInt1(false));
  EXPECT_EQ(Matched->Dref, Dref);
  EXPECT_EQ(Matched->Bias, Bias);
  EXPECT_EQ(Matched->OffsetX, Offset);
  EXPECT_EQ(Matched->MinLodClamp, MinLodClamp);
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// Roadmap L66(f): a real nonzero `DUdX`/`DUdY` pair, confirming
// `createSampleCmp1D`'s newly widened signature and `matchImageCall`'s
// decode both thread it through correctly.
TEST_F(ImageCallsTest, MatchesSampleCmp1DCallWithRealGradDerivatives) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *U = ConstantFP::get(Builder.getFloatTy(), 0.25);
  Value *DUdX = ConstantFP::get(Builder.getFloatTy(), 0.125);
  Value *DUdY = ConstantFP::get(Builder.getFloatTy(), 0.0625);
  Value *Lod = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Dref = ConstantFP::get(Builder.getFloatTy(), 0.75);
  Value *Bias = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Offset = Builder.getInt32(0);
  Value *MinLodClamp = ConstantFP::get(Builder.getFloatTy(),
                                       -std::numeric_limits<float>::infinity());
  CallInst *CI =
      createSampleCmp1D(Builder, Env, Builder.getInt32(2), Builder.getInt32(1),
                        U, DUdX, DUdY, Lod, Builder.getInt1(false), Dref, Bias,
                        Offset, MinLodClamp, Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->DUdX, DUdX);
  EXPECT_EQ(Matched->DUdY, DUdY);
}

// Roadmap L66(k): a real, nonzero, bare-scalar `Offset`, confirming
// `createSampleCmp1D`'s newly widened signature and `matchImageCall`'s
// decode both thread it through correctly -- mirroring `Sample1D`'s own
// `Sample1DHonorsNonZeroTexelOffset` precedent, but for the matcher phase
// rather than the runtime phase.
TEST_F(ImageCallsTest, MatchesSampleCmp1DCallWithRealOffset) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *U = ConstantFP::get(Builder.getFloatTy(), 0.25);
  Value *DUdX = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *DUdY = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Lod = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Dref = ConstantFP::get(Builder.getFloatTy(), 0.75);
  Value *Bias = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Offset = Builder.getInt32(1);
  Value *MinLodClamp = ConstantFP::get(Builder.getFloatTy(),
                                       -std::numeric_limits<float>::infinity());
  CallInst *CI =
      createSampleCmp1D(Builder, Env, Builder.getInt32(2), Builder.getInt32(1),
                        U, DUdX, DUdY, Lod, Builder.getInt1(false), Dref, Bias,
                        Offset, MinLodClamp, Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->OffsetX, Offset);
}

TEST_F(ImageCallsTest, MatchesSampleCmpArray1DCallWithBiasAndMinLodClamp) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *U = ConstantFP::get(Builder.getFloatTy(), 0.25);
  Value *ArrayLayer = ConstantFP::get(Builder.getFloatTy(), 2.0);
  Value *DUdX = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *DUdY = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Lod = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Dref = ConstantFP::get(Builder.getFloatTy(), 0.75);
  Value *Bias = ConstantFP::get(Builder.getFloatTy(), 1.0);
  Value *Offset = Builder.getInt32(0);
  Value *MinLodClamp = ConstantFP::get(Builder.getFloatTy(), 0.5);
  CallInst *CI = createSampleCmpArray1D(
      Builder, Env, Builder.getInt32(2), Builder.getInt32(1), U, ArrayLayer,
      DUdX, DUdY, Lod, Builder.getInt1(false), Dref, Bias, Offset, MinLodClamp,
      Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::SampleCmpArray1D);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(2));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(1));
  EXPECT_EQ(Matched->U, U);
  EXPECT_EQ(Matched->ArrayLayer, ArrayLayer);
  EXPECT_EQ(Matched->DUdX, DUdX);
  EXPECT_EQ(Matched->DUdY, DUdY);
  EXPECT_EQ(Matched->Lod, Lod);
  EXPECT_EQ(Matched->UseExplicitLod, Builder.getInt1(false));
  EXPECT_EQ(Matched->Dref, Dref);
  EXPECT_EQ(Matched->Bias, Bias);
  EXPECT_EQ(Matched->OffsetX, Offset);
  EXPECT_EQ(Matched->MinLodClamp, MinLodClamp);
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// Roadmap L66(f): a real nonzero `DUdX`/`DUdY` pair for `Array1D` too --
// only `U` is ever differentiated, `ArrayLayer` never is.
TEST_F(ImageCallsTest, MatchesSampleCmpArray1DCallWithRealGradDerivatives) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *U = ConstantFP::get(Builder.getFloatTy(), 0.25);
  Value *ArrayLayer = ConstantFP::get(Builder.getFloatTy(), 2.0);
  Value *DUdX = ConstantFP::get(Builder.getFloatTy(), 0.125);
  Value *DUdY = ConstantFP::get(Builder.getFloatTy(), 0.0625);
  Value *Lod = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Dref = ConstantFP::get(Builder.getFloatTy(), 0.75);
  Value *Bias = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Offset = Builder.getInt32(0);
  Value *MinLodClamp = ConstantFP::get(Builder.getFloatTy(),
                                       -std::numeric_limits<float>::infinity());
  CallInst *CI = createSampleCmpArray1D(
      Builder, Env, Builder.getInt32(2), Builder.getInt32(1), U, ArrayLayer,
      DUdX, DUdY, Lod, Builder.getInt1(false), Dref, Bias, Offset, MinLodClamp,
      Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->ArrayLayer, ArrayLayer);
  EXPECT_EQ(Matched->DUdX, DUdX);
  EXPECT_EQ(Matched->DUdY, DUdY);
}

// Roadmap L66(k): a real, nonzero, bare-scalar `Offset` for `Array1D` too
// -- mirroring `MatchesSampleCmp1DCallWithRealOffset` above's identical
// `Plain1D` precedent.
TEST_F(ImageCallsTest, MatchesSampleCmpArray1DCallWithRealOffset) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *U = ConstantFP::get(Builder.getFloatTy(), 0.25);
  Value *ArrayLayer = ConstantFP::get(Builder.getFloatTy(), 2.0);
  Value *DUdX = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *DUdY = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Lod = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Dref = ConstantFP::get(Builder.getFloatTy(), 0.75);
  Value *Bias = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Offset = Builder.getInt32(1);
  Value *MinLodClamp = ConstantFP::get(Builder.getFloatTy(),
                                       -std::numeric_limits<float>::infinity());
  CallInst *CI = createSampleCmpArray1D(
      Builder, Env, Builder.getInt32(2), Builder.getInt32(1), U, ArrayLayer,
      DUdX, DUdY, Lod, Builder.getInt1(false), Dref, Bias, Offset, MinLodClamp,
      Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->OffsetX, Offset);
}

// Roadmap L66(g): a real nonzero `DUdX`/`DUdY`/`DVdX`/`DVdY` set for
// `Array2D` too -- only `U`/`V`, never `ArrayLayer`, is ever
// differentiated, mirroring
// `MatchesSampleCmpArray1DCallWithRealGradDerivatives` above's identical
// `Array1D` precedent.
TEST_F(ImageCallsTest, MatchesSampleCmpArray2DCallWithRealGradDerivatives) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *U = ConstantFP::get(Builder.getFloatTy(), 0.25);
  Value *V = ConstantFP::get(Builder.getFloatTy(), 0.5);
  Value *ArrayLayer = ConstantFP::get(Builder.getFloatTy(), 2.0);
  Value *DUdX = ConstantFP::get(Builder.getFloatTy(), 0.125);
  Value *DUdY = ConstantFP::get(Builder.getFloatTy(), 0.0625);
  Value *DVdX = ConstantFP::get(Builder.getFloatTy(), 0.25);
  Value *DVdY = ConstantFP::get(Builder.getFloatTy(), 0.375);
  Value *Lod = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Dref = ConstantFP::get(Builder.getFloatTy(), 0.75);
  Value *Bias = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *OffsetX = Builder.getInt32(0);
  Value *OffsetY = Builder.getInt32(0);
  Value *MinLodClamp = ConstantFP::get(Builder.getFloatTy(),
                                       -std::numeric_limits<float>::infinity());
  CallInst *CI = createSampleCmpArray2D(
      Builder, Env, Builder.getInt32(2), Builder.getInt32(1), U, V, ArrayLayer,
      DUdX, DUdY, DVdX, DVdY, Lod, Builder.getInt1(false), Dref, Bias, OffsetX,
      OffsetY, MinLodClamp, Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::SampleCmpArray2D);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(2));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(1));
  EXPECT_EQ(Matched->U, U);
  EXPECT_EQ(Matched->V, V);
  EXPECT_EQ(Matched->ArrayLayer, ArrayLayer);
  EXPECT_EQ(Matched->DUdX, DUdX);
  EXPECT_EQ(Matched->DUdY, DUdY);
  EXPECT_EQ(Matched->DVdX, DVdX);
  EXPECT_EQ(Matched->DVdY, DVdY);
  EXPECT_EQ(Matched->Lod, Lod);
  EXPECT_EQ(Matched->UseExplicitLod, Builder.getInt1(false));
  EXPECT_EQ(Matched->Dref, Dref);
  EXPECT_EQ(Matched->Bias, Bias);
  EXPECT_EQ(Matched->OffsetX, OffsetX);
  EXPECT_EQ(Matched->OffsetY, OffsetY);
  EXPECT_EQ(Matched->MinLodClamp, MinLodClamp);
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// Roadmap L66(h): a real nonzero `DDirXdX`/`DDirXdY`/`DDirYdX`/`DDirYdY`/
// `DDirZdX`/`DDirZdY` sextuple for `Cube` -- the full 3-component
// direction-vector derivative pair `createSampleCube`'s own identical
// `Grad` operands already carry for an ordinary sample, roadmap L59.
TEST_F(ImageCallsTest, MatchesSampleCmpCubeCallWithRealGradDerivatives) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *DirX = ConstantFP::get(Builder.getFloatTy(), 1.0);
  Value *DirY = ConstantFP::get(Builder.getFloatTy(), 0.25);
  Value *DirZ = ConstantFP::get(Builder.getFloatTy(), 0.5);
  Value *DDirXdX = ConstantFP::get(Builder.getFloatTy(), 0.1);
  Value *DDirXdY = ConstantFP::get(Builder.getFloatTy(), 0.2);
  Value *DDirYdX = ConstantFP::get(Builder.getFloatTy(), 0.3);
  Value *DDirYdY = ConstantFP::get(Builder.getFloatTy(), 0.4);
  Value *DDirZdX = ConstantFP::get(Builder.getFloatTy(), 0.5);
  Value *DDirZdY = ConstantFP::get(Builder.getFloatTy(), 0.6);
  Value *Lod = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Dref = ConstantFP::get(Builder.getFloatTy(), 0.75);
  Value *Bias = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *MinLodClamp = ConstantFP::get(Builder.getFloatTy(),
                                       -std::numeric_limits<float>::infinity());
  CallInst *CI = createSampleCmpCube(
      Builder, Env, Builder.getInt32(2), Builder.getInt32(1), DirX, DirY, DirZ,
      DDirXdX, DDirXdY, DDirYdX, DDirYdY, DDirZdX, DDirZdY, Lod,
      Builder.getInt1(false), Dref, Bias, MinLodClamp, Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::SampleCmpCube);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(2));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(1));
  EXPECT_EQ(Matched->U, DirX);
  EXPECT_EQ(Matched->V, DirY);
  EXPECT_EQ(Matched->W, DirZ);
  EXPECT_EQ(Matched->DDirXdX, DDirXdX);
  EXPECT_EQ(Matched->DDirXdY, DDirXdY);
  EXPECT_EQ(Matched->DDirYdX, DDirYdX);
  EXPECT_EQ(Matched->DDirYdY, DDirYdY);
  EXPECT_EQ(Matched->DDirZdX, DDirZdX);
  EXPECT_EQ(Matched->DDirZdY, DDirZdY);
  EXPECT_EQ(Matched->Lod, Lod);
  EXPECT_EQ(Matched->UseExplicitLod, Builder.getInt1(false));
  EXPECT_EQ(Matched->Dref, Dref);
  EXPECT_EQ(Matched->Bias, Bias);
  EXPECT_EQ(Matched->MinLodClamp, MinLodClamp);
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// Roadmap L66(i): the `CubeArray` counterpart of the test just above -- a
// real nonzero `DDirXdX`/`DDirXdY`/`DDirYdX`/`DDirYdY`/`DDirZdX`/`DDirZdY`
// sextuple threaded through alongside a real `ArrayLayer`.
TEST_F(ImageCallsTest, MatchesSampleCmpCubeArrayCallWithRealGradDerivatives) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *DirX = ConstantFP::get(Builder.getFloatTy(), 1.0);
  Value *DirY = ConstantFP::get(Builder.getFloatTy(), 0.25);
  Value *DirZ = ConstantFP::get(Builder.getFloatTy(), 0.5);
  Value *DDirXdX = ConstantFP::get(Builder.getFloatTy(), 0.1);
  Value *DDirXdY = ConstantFP::get(Builder.getFloatTy(), 0.2);
  Value *DDirYdX = ConstantFP::get(Builder.getFloatTy(), 0.3);
  Value *DDirYdY = ConstantFP::get(Builder.getFloatTy(), 0.4);
  Value *DDirZdX = ConstantFP::get(Builder.getFloatTy(), 0.5);
  Value *DDirZdY = ConstantFP::get(Builder.getFloatTy(), 0.6);
  Value *ArrayLayer = ConstantFP::get(Builder.getFloatTy(), 2.0);
  Value *Lod = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Dref = ConstantFP::get(Builder.getFloatTy(), 0.75);
  Value *Bias = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *MinLodClamp = ConstantFP::get(Builder.getFloatTy(),
                                       -std::numeric_limits<float>::infinity());
  CallInst *CI = createSampleCmpCubeArray(
      Builder, Env, Builder.getInt32(2), Builder.getInt32(1), DirX, DirY, DirZ,
      DDirXdX, DDirXdY, DDirYdX, DDirYdY, DDirZdX, DDirZdY, ArrayLayer, Lod,
      Builder.getInt1(false), Dref, Bias, MinLodClamp, Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::SampleCmpCubeArray);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(2));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(1));
  EXPECT_EQ(Matched->U, DirX);
  EXPECT_EQ(Matched->V, DirY);
  EXPECT_EQ(Matched->W, DirZ);
  EXPECT_EQ(Matched->DDirXdX, DDirXdX);
  EXPECT_EQ(Matched->DDirXdY, DDirXdY);
  EXPECT_EQ(Matched->DDirYdX, DDirYdX);
  EXPECT_EQ(Matched->DDirYdY, DDirYdY);
  EXPECT_EQ(Matched->DDirZdX, DDirZdX);
  EXPECT_EQ(Matched->DDirZdY, DDirZdY);
  EXPECT_EQ(Matched->ArrayLayer, ArrayLayer);
  EXPECT_EQ(Matched->Lod, Lod);
  EXPECT_EQ(Matched->UseExplicitLod, Builder.getInt1(false));
  EXPECT_EQ(Matched->Dref, Dref);
  EXPECT_EQ(Matched->Bias, Bias);
  EXPECT_EQ(Matched->MinLodClamp, MinLodClamp);
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}
// roadmap L67(a) and a real `ConstOffset` triple by roadmap L67(c))
// `createSample3D`'s own ordinary `Plain3D` sample: a real `(U, V, W)`
// coordinate plus its own screen-space derivative triple, a real
// `Bias`/`MinLodClamp` pair, and a real `OffsetX`/`OffsetY`/`OffsetZ`
// triple (see `ImageCallKind::Sample3D`'s own doc).
TEST_F(ImageCallsTest, MatchesSample3DCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *U = ConstantFP::get(Builder.getFloatTy(), 0.25);
  Value *V = ConstantFP::get(Builder.getFloatTy(), 0.5);
  Value *W = ConstantFP::get(Builder.getFloatTy(), 0.75);
  Value *DUdX = ConstantFP::get(Builder.getFloatTy(), 0.1);
  Value *DUdY = ConstantFP::get(Builder.getFloatTy(), 0.2);
  Value *DVdX = ConstantFP::get(Builder.getFloatTy(), 0.3);
  Value *DVdY = ConstantFP::get(Builder.getFloatTy(), 0.4);
  Value *DWdX = ConstantFP::get(Builder.getFloatTy(), 0.5);
  Value *DWdY = ConstantFP::get(Builder.getFloatTy(), 0.6);
  Value *Lod = ConstantFP::get(Builder.getFloatTy(), 0.0);
  Value *Bias = ConstantFP::get(Builder.getFloatTy(), 1.0);
  Value *OffsetX = Builder.getInt32(-1);
  Value *OffsetY = Builder.getInt32(2);
  Value *OffsetZ = Builder.getInt32(-3);
  Value *MinLodClamp = ConstantFP::get(Builder.getFloatTy(), 0.5);
  CallInst *CI = createSample3D(
      Builder, Env, Builder.getInt32(2), Builder.getInt32(1), U, V, W, DUdX,
      DUdY, DVdX, DVdY, DWdX, DWdY, Lod, Builder.getInt1(false), Bias,
      OffsetX, OffsetY, OffsetZ, MinLodClamp, Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Sample3D);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(2));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(1));
  EXPECT_EQ(Matched->U, U);
  EXPECT_EQ(Matched->V, V);
  EXPECT_EQ(Matched->W, W);
  EXPECT_EQ(Matched->DUdX, DUdX);
  EXPECT_EQ(Matched->DUdY, DUdY);
  EXPECT_EQ(Matched->DVdX, DVdX);
  EXPECT_EQ(Matched->DVdY, DVdY);
  EXPECT_EQ(Matched->DWdX, DWdX);
  EXPECT_EQ(Matched->DWdY, DWdY);
  EXPECT_EQ(Matched->Lod, Lod);
  EXPECT_EQ(Matched->UseExplicitLod, Builder.getInt1(false));
  EXPECT_EQ(Matched->Bias, Bias);
  EXPECT_EQ(Matched->OffsetX, OffsetX);
  EXPECT_EQ(Matched->OffsetY, OffsetY);
  EXPECT_EQ(Matched->OffsetZ, OffsetZ);
  EXPECT_EQ(Matched->MinLodClamp, MinLodClamp);
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// `createGetDimensions2D`'s own `feme.cpu.image.getdimensions.2d.v2i32` call
// (roadmap L70): a plain 2D image's mip-0 `(Width, Height)` extent query --
// see `ImageCallKind::GetDimensions2D`'s own doc. Takes only an image index
// and a mask, unlike every sample/fetch call's own larger operand list.
TEST_F(ImageCallsTest, MatchesGetDimensions2DCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createGetDimensions2D(Builder, Env, Builder.getInt32(3),
                                       Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::GetDimensions2D);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// `createQuerySizeLod2D`'s own `feme.cpu.image.getdimensions.lod.2d.v2i32`
// call (roadmap L72(d)): a plain 2D image's own extent at an explicit mip
// level -- see `ImageCallKind::QuerySizeLod2D`'s own doc. Same shape as
// `GetDimensions2D` plus the explicit mip level itself.
TEST_F(ImageCallsTest, MatchesQuerySizeLod2DCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI =
      createQuerySizeLod2D(Builder, Env, Builder.getInt32(3),
                           Builder.getInt32(2), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::QuerySizeLod2D);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->Lod, Builder.getInt32(2));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// `createQuerySizeLod1D`'s own `feme.cpu.image.getdimensions.lod.1d.i32`
// call (roadmap L75): a plain 1D image's own extent at an explicit mip
// level -- see `ImageCallKind::QuerySizeLod1D`'s own doc. Same operand
// list as `QuerySizeLod2D`, just a scalar result.
TEST_F(ImageCallsTest, MatchesQuerySizeLod1DCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI =
      createQuerySizeLod1D(Builder, Env, Builder.getInt32(3),
                           Builder.getInt32(2), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::QuerySizeLod1D);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->Lod, Builder.getInt32(2));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// `createQuerySizeLod1DArray`'s own
// `feme.cpu.image.getdimensions.lod.1darray.v2i32` call (roadmap L75): an
// arrayed 1D image's own extent at an explicit mip level -- see
// `ImageCallKind::QuerySizeLod1DArray`'s own doc.
TEST_F(ImageCallsTest, MatchesQuerySizeLod1DArrayCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI =
      createQuerySizeLod1DArray(Builder, Env, Builder.getInt32(3),
                                Builder.getInt32(2), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::QuerySizeLod1DArray);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->Lod, Builder.getInt32(2));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// `createQuerySizeLod2DArray`'s own
// `feme.cpu.image.getdimensions.lod.2darray.v3i32` call (roadmap L75): an
// arrayed 2D image's own extent at an explicit mip level -- see
// `ImageCallKind::QuerySizeLod2DArray`'s own doc.
TEST_F(ImageCallsTest, MatchesQuerySizeLod2DArrayCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI =
      createQuerySizeLod2DArray(Builder, Env, Builder.getInt32(3),
                                Builder.getInt32(2), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::QuerySizeLod2DArray);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->Lod, Builder.getInt32(2));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// `createQuerySizeLod3D`'s own `feme.cpu.image.getdimensions.lod.3d.v3i32`
// call (roadmap L75): a plain 3D (volume) image's own extent at an
// explicit mip level -- see `ImageCallKind::QuerySizeLod3D`'s own doc.
TEST_F(ImageCallsTest, MatchesQuerySizeLod3DCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI =
      createQuerySizeLod3D(Builder, Env, Builder.getInt32(3),
                           Builder.getInt32(2), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::QuerySizeLod3D);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->Lod, Builder.getInt32(2));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// `createQuerySizeLodCubeArray`'s own
// `feme.cpu.image.getdimensions.lod.cubearray.v3i32` call (roadmap L75): a
// cube-array image's own extent at an explicit mip level -- see
// `ImageCallKind::QuerySizeLodCubeArray`'s own doc.
TEST_F(ImageCallsTest, MatchesQuerySizeLodCubeArrayCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI =
      createQuerySizeLodCubeArray(Builder, Env, Builder.getInt32(3),
                                  Builder.getInt32(2), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::QuerySizeLodCubeArray);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->Lod, Builder.getInt32(2));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

// `createQueryLevels`'s own `feme.cpu.image.querylevels.i32` call (roadmap
// L72(d)): an image's own total mip-level count -- see
// `ImageCallKind::QueryLevels`'s own doc. The smallest operand list of any
// `feme.cpu.image.*` call: just an image index, no mask or mip level.
TEST_F(ImageCallsTest, MatchesQueryLevelsCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createQueryLevels(Builder, Env, Builder.getInt32(3));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::QueryLevels);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
}

// `createQuerySamples`'s own `feme.cpu.image.querysamples.i32` call
// (roadmap L73): a multisampled image's own sample count -- see
// `ImageCallKind::QuerySamples`'s own doc. Same operand list as
// `QueryLevels`.
TEST_F(ImageCallsTest, MatchesQuerySamplesCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createQuerySamples(Builder, Env, Builder.getInt32(3));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::QuerySamples);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
}

// `createSample2DI32`'s own `feme.cpu.image.sample.2d.v4i32` call (roadmap
// H109): a nearest-filtered, explicit-LOD-only sample against an
// integer-channel (`usampler2D`/`isampler2D`) sampled image -- see
// `ImageCallKind::Sample2DI32`'s own doc.
TEST_F(ImageCallsTest, MatchesSample2DI32Call) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createSample2DI32(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(4),
      ConstantFP::get(Builder.getFloatTy(), 0.5),
      ConstantFP::get(Builder.getFloatTy(), 0.5),
      ConstantFP::get(Builder.getFloatTy(), 0.0), Builder.getInt32(0),
      Builder.getInt32(0), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Sample2DI32);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->Env.SamplerHeap, Env.SamplerHeap);
  EXPECT_EQ(Matched->Env.SamplerHeapCount, Env.SamplerHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(4));
  EXPECT_EQ(Matched->U, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->V, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->Lod, ConstantFP::get(Builder.getFloatTy(), 0.0));
  EXPECT_EQ(Matched->OffsetX, Builder.getInt32(0));
  EXPECT_EQ(Matched->OffsetY, Builder.getInt32(0));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
  EXPECT_TRUE(isa<FixedVectorType>(CI->getType()));
  EXPECT_TRUE(cast<FixedVectorType>(CI->getType())->getElementType()->isIntegerTy(32));
}

TEST_F(ImageCallsTest, MatchesSample1DI32Call) {
  // Roadmap L125(b): the `Plain1D` counterpart of `MatchesSample2DI32Call`
  // above, confirming `matchImageCall`'s new `Sample1DI32` case extracts a
  // bare scalar `U`/`Offset` (no `V`/`OffsetY`), mirroring `Sample1D`'s own
  // relationship to `Sample2D`.
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createSample1DI32(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(4),
      ConstantFP::get(Builder.getFloatTy(), 0.5),
      ConstantFP::get(Builder.getFloatTy(), 0.0), Builder.getInt32(0),
      Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Sample1DI32);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->Env.SamplerHeap, Env.SamplerHeap);
  EXPECT_EQ(Matched->Env.SamplerHeapCount, Env.SamplerHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(4));
  EXPECT_EQ(Matched->U, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->Lod, ConstantFP::get(Builder.getFloatTy(), 0.0));
  EXPECT_EQ(Matched->OffsetX, Builder.getInt32(0));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
  EXPECT_TRUE(isa<FixedVectorType>(CI->getType()));
  EXPECT_TRUE(cast<FixedVectorType>(CI->getType())->getElementType()->isIntegerTy(32));
}

TEST_F(ImageCallsTest, MatchesSample1DArrayI32Call) {
  // Roadmap L125(b): the `Array1D` counterpart of `MatchesSample1DI32Call`
  // above, confirming `matchImageCall`'s new `Sample1DArrayI32` case
  // extracts the added `ArrayLayer` operand alongside `U`, mirroring
  // `Sample1DArray`'s own relationship to `Sample1D`.
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createSample1DArrayI32(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(4),
      ConstantFP::get(Builder.getFloatTy(), 0.5),
      ConstantFP::get(Builder.getFloatTy(), 2.0),
      ConstantFP::get(Builder.getFloatTy(), 0.0), Builder.getInt32(0),
      Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Sample1DArrayI32);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->Env.SamplerHeap, Env.SamplerHeap);
  EXPECT_EQ(Matched->Env.SamplerHeapCount, Env.SamplerHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(4));
  EXPECT_EQ(Matched->U, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->ArrayLayer, ConstantFP::get(Builder.getFloatTy(), 2.0));
  EXPECT_EQ(Matched->Lod, ConstantFP::get(Builder.getFloatTy(), 0.0));
  EXPECT_EQ(Matched->OffsetX, Builder.getInt32(0));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
  EXPECT_TRUE(isa<FixedVectorType>(CI->getType()));
  EXPECT_TRUE(cast<FixedVectorType>(CI->getType())->getElementType()->isIntegerTy(32));
}


TEST_F(ImageCallsTest, MatchesSample2DArrayI32Call) {
  // Roadmap L125(b): the `Array2D` counterpart of `MatchesSample1DArrayI32
  // Call` above, confirming `matchImageCall`'s new `Sample2DArrayI32` case
  // extracts `V`/`ArrayLayer`/`OffsetY` alongside `U`/`OffsetX`, mirroring
  // `Sample2DArray`'s own relationship to `Sample1DArray`.
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createSample2DArrayI32(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(4),
      ConstantFP::get(Builder.getFloatTy(), 0.5),
      ConstantFP::get(Builder.getFloatTy(), 0.25),
      ConstantFP::get(Builder.getFloatTy(), 2.0),
      ConstantFP::get(Builder.getFloatTy(), 0.0), Builder.getInt32(1),
      Builder.getInt32(-1), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Sample2DArrayI32);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->Env.SamplerHeap, Env.SamplerHeap);
  EXPECT_EQ(Matched->Env.SamplerHeapCount, Env.SamplerHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(4));
  EXPECT_EQ(Matched->U, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->V, ConstantFP::get(Builder.getFloatTy(), 0.25));
  EXPECT_EQ(Matched->ArrayLayer, ConstantFP::get(Builder.getFloatTy(), 2.0));
  EXPECT_EQ(Matched->Lod, ConstantFP::get(Builder.getFloatTy(), 0.0));
  EXPECT_EQ(Matched->OffsetX, Builder.getInt32(1));
  EXPECT_EQ(Matched->OffsetY, Builder.getInt32(-1));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
  EXPECT_TRUE(isa<FixedVectorType>(CI->getType()));
  EXPECT_TRUE(cast<FixedVectorType>(CI->getType())->getElementType()->isIntegerTy(32));
}

TEST_F(ImageCallsTest, MatchesSample3DI32Call) {
  // Roadmap L125(b): the `Plain3D` counterpart of `MatchesSample2DArrayI32
  // Call` above, confirming `matchImageCall`'s new `Sample3DI32` case
  // extracts `W`/`OffsetZ` alongside `U`/`V`/`OffsetX`/`OffsetY` (no
  // `ArrayLayer`, unlike `Sample2DArrayI32`), mirroring `Sample3D`'s own
  // relationship to `Sample2D`.
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createSample3DI32(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(4),
      ConstantFP::get(Builder.getFloatTy(), 0.5),
      ConstantFP::get(Builder.getFloatTy(), 0.25),
      ConstantFP::get(Builder.getFloatTy(), 0.125),
      ConstantFP::get(Builder.getFloatTy(), 0.0), Builder.getInt32(1),
      Builder.getInt32(-1), Builder.getInt32(2), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Sample3DI32);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->Env.SamplerHeap, Env.SamplerHeap);
  EXPECT_EQ(Matched->Env.SamplerHeapCount, Env.SamplerHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(4));
  EXPECT_EQ(Matched->U, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->V, ConstantFP::get(Builder.getFloatTy(), 0.25));
  EXPECT_EQ(Matched->W, ConstantFP::get(Builder.getFloatTy(), 0.125));
  EXPECT_EQ(Matched->Lod, ConstantFP::get(Builder.getFloatTy(), 0.0));
  EXPECT_EQ(Matched->OffsetX, Builder.getInt32(1));
  EXPECT_EQ(Matched->OffsetY, Builder.getInt32(-1));
  EXPECT_EQ(Matched->OffsetZ, Builder.getInt32(2));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
  EXPECT_TRUE(isa<FixedVectorType>(CI->getType()));
  EXPECT_TRUE(cast<FixedVectorType>(CI->getType())->getElementType()->isIntegerTy(32));
}

TEST_F(ImageCallsTest, MatchesSampleCubeI32Call) {
  // Roadmap L125(b): the `Cube` counterpart of `MatchesSample3DI32Call`
  // above, confirming `matchImageCall`'s new `SampleCubeI32` case
  // extracts `U`/`V`/`W` as the `(DirX, DirY, DirZ)` direction vector
  // (mirroring `SampleCube`'s own identical field-reuse convention) and
  // no `OffsetX`/`OffsetY`/`OffsetZ` at all (`Dim::Cube` forbids
  // `ConstOffset` outright).
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createSampleCubeI32(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(4),
      ConstantFP::get(Builder.getFloatTy(), 0.5),
      ConstantFP::get(Builder.getFloatTy(), 0.25),
      ConstantFP::get(Builder.getFloatTy(), 0.125),
      ConstantFP::get(Builder.getFloatTy(), 0.0), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::SampleCubeI32);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->Env.SamplerHeap, Env.SamplerHeap);
  EXPECT_EQ(Matched->Env.SamplerHeapCount, Env.SamplerHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(4));
  EXPECT_EQ(Matched->U, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->V, ConstantFP::get(Builder.getFloatTy(), 0.25));
  EXPECT_EQ(Matched->W, ConstantFP::get(Builder.getFloatTy(), 0.125));
  EXPECT_EQ(Matched->Lod, ConstantFP::get(Builder.getFloatTy(), 0.0));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
  EXPECT_TRUE(isa<FixedVectorType>(CI->getType()));
  EXPECT_TRUE(cast<FixedVectorType>(CI->getType())->getElementType()->isIntegerTy(32));
}

TEST_F(ImageCallsTest, MatchesSampleCubeArrayI32Call) {
  // Roadmap L125(b): the `CubeArray` counterpart of
  // `MatchesSampleCubeI32Call` above, confirming `matchImageCall`'s new
  // `SampleCubeArrayI32` case extracts `U`/`V`/`W` as the direction
  // vector (same field-reuse convention) plus `ArrayLayer` as a fourth
  // operand, mirroring `SampleCubeArray`'s own identical relationship to
  // `SampleCube`.
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createSampleCubeArrayI32(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(4),
      ConstantFP::get(Builder.getFloatTy(), 0.5),
      ConstantFP::get(Builder.getFloatTy(), 0.25),
      ConstantFP::get(Builder.getFloatTy(), 0.125),
      ConstantFP::get(Builder.getFloatTy(), 2.0),
      ConstantFP::get(Builder.getFloatTy(), 0.0), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::SampleCubeArrayI32);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->Env.SamplerHeap, Env.SamplerHeap);
  EXPECT_EQ(Matched->Env.SamplerHeapCount, Env.SamplerHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(4));
  EXPECT_EQ(Matched->U, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->V, ConstantFP::get(Builder.getFloatTy(), 0.25));
  EXPECT_EQ(Matched->W, ConstantFP::get(Builder.getFloatTy(), 0.125));
  EXPECT_EQ(Matched->ArrayLayer, ConstantFP::get(Builder.getFloatTy(), 2.0));
  EXPECT_EQ(Matched->Lod, ConstantFP::get(Builder.getFloatTy(), 0.0));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
  EXPECT_TRUE(isa<FixedVectorType>(CI->getType()));
  EXPECT_TRUE(cast<FixedVectorType>(CI->getType())->getElementType()->isIntegerTy(32));
}

TEST_F(ImageCallsTest, MatchesGatherCmpArray2DCall) {
  IRBuilder<> Builder(BB);

  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createGatherCmpArray2D(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(4),
      ConstantFP::get(Builder.getFloatTy(), 0.5),
      ConstantFP::get(Builder.getFloatTy(), 0.5),
      ConstantFP::get(Builder.getFloatTy(), 2.0),
      ConstantFP::get(Builder.getFloatTy(), 0.25), Builder.getInt32(0),
      Builder.getInt32(0), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::GatherCmpArray2D);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->Env.SamplerHeap, Env.SamplerHeap);
  EXPECT_EQ(Matched->Env.SamplerHeapCount, Env.SamplerHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(4));
  EXPECT_EQ(Matched->U, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->V, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->ArrayLayer, ConstantFP::get(Builder.getFloatTy(), 2.0));
  EXPECT_EQ(Matched->Dref, ConstantFP::get(Builder.getFloatTy(), 0.25));
  EXPECT_EQ(Matched->OffsetX, Builder.getInt32(0));
  EXPECT_EQ(Matched->OffsetY, Builder.getInt32(0));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
  EXPECT_TRUE(isa<FixedVectorType>(CI->getType()));
  EXPECT_TRUE(
      cast<FixedVectorType>(CI->getType())->getElementType()->isFloatTy());
}

// `createGatherArray2D`'s own `feme.cpu.image.gather.array2d.v4f32` call
// (roadmap H124q): the `Array2D` counterpart of `ImageCallKind::Gather2D`,
// with an extra `ArrayLayer` operand.
TEST_F(ImageCallsTest, MatchesGatherArray2DCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createGatherArray2D(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(4),
      ConstantFP::get(Builder.getFloatTy(), 0.5),
      ConstantFP::get(Builder.getFloatTy(), 0.5),
      ConstantFP::get(Builder.getFloatTy(), 2.0), Builder.getInt32(1),
      Builder.getInt32(0), Builder.getInt32(0), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::GatherArray2D);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->Env.SamplerHeap, Env.SamplerHeap);
  EXPECT_EQ(Matched->Env.SamplerHeapCount, Env.SamplerHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(4));
  EXPECT_EQ(Matched->U, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->V, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->ArrayLayer, ConstantFP::get(Builder.getFloatTy(), 2.0));
  EXPECT_EQ(Matched->Component, Builder.getInt32(1));
  EXPECT_EQ(Matched->OffsetX, Builder.getInt32(0));
  EXPECT_EQ(Matched->OffsetY, Builder.getInt32(0));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
  EXPECT_TRUE(isa<FixedVectorType>(CI->getType()));
  EXPECT_TRUE(
      cast<FixedVectorType>(CI->getType())->getElementType()->isFloatTy());
}

// `createGatherCmpCube`'s own `feme.cpu.image.gathercmp.cube.v4f32` call
// (roadmap H124r): the `Cube` counterpart of `ImageCallKind::
// GatherCmpArray2D`, taking a 3-component direction vector in place of
// `(U, V, ArrayLayer)` and no offset operand at all.
TEST_F(ImageCallsTest, MatchesGatherCmpCubeCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createGatherCmpCube(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(4),
      ConstantFP::get(Builder.getFloatTy(), 1.0),
      ConstantFP::get(Builder.getFloatTy(), 0.0),
      ConstantFP::get(Builder.getFloatTy(), 0.0),
      ConstantFP::get(Builder.getFloatTy(), 0.25), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::GatherCmpCube);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->Env.SamplerHeap, Env.SamplerHeap);
  EXPECT_EQ(Matched->Env.SamplerHeapCount, Env.SamplerHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(4));
  EXPECT_EQ(Matched->U, ConstantFP::get(Builder.getFloatTy(), 1.0));
  EXPECT_EQ(Matched->V, ConstantFP::get(Builder.getFloatTy(), 0.0));
  EXPECT_EQ(Matched->W, ConstantFP::get(Builder.getFloatTy(), 0.0));
  EXPECT_EQ(Matched->Dref, ConstantFP::get(Builder.getFloatTy(), 0.25));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
  EXPECT_TRUE(isa<FixedVectorType>(CI->getType()));
  EXPECT_TRUE(
      cast<FixedVectorType>(CI->getType())->getElementType()->isFloatTy());
}

// `createGatherCube`'s own `feme.cpu.image.gather.cube.v4f32` call
// (roadmap H124r): the `Cube` counterpart of `ImageCallKind::
// GatherArray2D`, mirroring `GatherCmpCube`'s own direction-vector
// coordinate and lack of an offset operand.
TEST_F(ImageCallsTest, MatchesGatherCubeCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI =
      createGatherCube(Builder, Env, Builder.getInt32(3), Builder.getInt32(4),
                       ConstantFP::get(Builder.getFloatTy(), 1.0),
                       ConstantFP::get(Builder.getFloatTy(), 0.0),
                       ConstantFP::get(Builder.getFloatTy(), 0.0),
                       Builder.getInt32(1), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::GatherCube);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->Env.SamplerHeap, Env.SamplerHeap);
  EXPECT_EQ(Matched->Env.SamplerHeapCount, Env.SamplerHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(4));
  EXPECT_EQ(Matched->U, ConstantFP::get(Builder.getFloatTy(), 1.0));
  EXPECT_EQ(Matched->V, ConstantFP::get(Builder.getFloatTy(), 0.0));
  EXPECT_EQ(Matched->W, ConstantFP::get(Builder.getFloatTy(), 0.0));
  EXPECT_EQ(Matched->Component, Builder.getInt32(1));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
  EXPECT_TRUE(isa<FixedVectorType>(CI->getType()));
  EXPECT_TRUE(
      cast<FixedVectorType>(CI->getType())->getElementType()->isFloatTy());
}

// `createGather2DI32`'s own `feme.cpu.image.gather.2d.v4i32` call
// (roadmap L125(k)): the integer-channel counterpart of `ImageCallKind::
// Gather2D`, identical operand shape, but returning `<4 x i32>`.
TEST_F(ImageCallsTest, MatchesGather2DI32Call) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createGather2DI32(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(4),
      ConstantFP::get(Builder.getFloatTy(), 0.5),
      ConstantFP::get(Builder.getFloatTy(), 0.5), Builder.getInt32(1),
      Builder.getInt32(0), Builder.getInt32(0), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Gather2DI32);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->Env.SamplerHeap, Env.SamplerHeap);
  EXPECT_EQ(Matched->Env.SamplerHeapCount, Env.SamplerHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(4));
  EXPECT_EQ(Matched->U, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->V, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->Component, Builder.getInt32(1));
  EXPECT_EQ(Matched->OffsetX, Builder.getInt32(0));
  EXPECT_EQ(Matched->OffsetY, Builder.getInt32(0));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
  EXPECT_TRUE(isa<FixedVectorType>(CI->getType()));
  EXPECT_TRUE(
      cast<FixedVectorType>(CI->getType())->getElementType()->isIntegerTy(32));
}

// `createGatherArray2DI32`'s own `feme.cpu.image.gather.array2d.v4i32`
// call (roadmap L125(k)): the `Array2D` counterpart of `ImageCallKind::
// Gather2DI32`, with an extra `ArrayLayer` operand, mirroring
// `GatherArray2D`'s own relationship to `Gather2D`.
TEST_F(ImageCallsTest, MatchesGatherArray2DI32Call) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createGatherArray2DI32(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(4),
      ConstantFP::get(Builder.getFloatTy(), 0.5),
      ConstantFP::get(Builder.getFloatTy(), 0.5),
      ConstantFP::get(Builder.getFloatTy(), 2.0), Builder.getInt32(1),
      Builder.getInt32(0), Builder.getInt32(0), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::GatherArray2DI32);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->Env.SamplerHeap, Env.SamplerHeap);
  EXPECT_EQ(Matched->Env.SamplerHeapCount, Env.SamplerHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(4));
  EXPECT_EQ(Matched->U, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->V, ConstantFP::get(Builder.getFloatTy(), 0.5));
  EXPECT_EQ(Matched->ArrayLayer, ConstantFP::get(Builder.getFloatTy(), 2.0));
  EXPECT_EQ(Matched->Component, Builder.getInt32(1));
  EXPECT_EQ(Matched->OffsetX, Builder.getInt32(0));
  EXPECT_EQ(Matched->OffsetY, Builder.getInt32(0));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
  EXPECT_TRUE(isa<FixedVectorType>(CI->getType()));
  EXPECT_TRUE(
      cast<FixedVectorType>(CI->getType())->getElementType()->isIntegerTy(32));
}

// `createGatherCubeI32`'s own `feme.cpu.image.gather.cube.v4i32` call
// (roadmap L125(k)): the integer-channel counterpart of `ImageCallKind::
// GatherCube`, mirroring that call's direction-vector coordinate and
// lack of an offset operand.
TEST_F(ImageCallsTest, MatchesGatherCubeI32Call) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createGatherCubeI32(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(4),
      ConstantFP::get(Builder.getFloatTy(), 1.0),
      ConstantFP::get(Builder.getFloatTy(), 0.0),
      ConstantFP::get(Builder.getFloatTy(), 0.0), Builder.getInt32(1),
      Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::GatherCubeI32);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->Env.ImageHeap, Env.ImageHeap);
  EXPECT_EQ(Matched->Env.ImageHeapCount, Env.ImageHeapCount);
  EXPECT_EQ(Matched->Env.SamplerHeap, Env.SamplerHeap);
  EXPECT_EQ(Matched->Env.SamplerHeapCount, Env.SamplerHeapCount);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->SamplerIndex, Builder.getInt32(4));
  EXPECT_EQ(Matched->U, ConstantFP::get(Builder.getFloatTy(), 1.0));
  EXPECT_EQ(Matched->V, ConstantFP::get(Builder.getFloatTy(), 0.0));
  EXPECT_EQ(Matched->W, ConstantFP::get(Builder.getFloatTy(), 0.0));
  EXPECT_EQ(Matched->Component, Builder.getInt32(1));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
  EXPECT_TRUE(isa<FixedVectorType>(CI->getType()));
  EXPECT_TRUE(
      cast<FixedVectorType>(CI->getType())->getElementType()->isIntegerTy(32));
}

} // namespace
