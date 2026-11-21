//===- RootConstantLoweringTest.cpp - Constant row lowering tests --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/RootConstantLowering.h"
#include "feme/Transforms/CPU/UnsupportedOps.h"
#include "feme/Transforms/DXIL/OpRaising.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Endian.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

#include <array>

using namespace llvm;
using namespace feme::cpu;

namespace {

std::unique_ptr<Module> makeModule(LLVMContext &Context, Type *ElementTy,
                                   uint64_t ElementSize, uint32_t RangeSize) {
  auto M = std::make_unique<Module>("constants", Context);
  auto *F = Function::Create(FunctionType::get(Type::getVoidTy(Context), false),
                             GlobalValue::ExternalLinkage, "main", *M);
  IRBuilder<> B(BasicBlock::Create(Context, "entry", F));
  auto *HandleTy = TargetExtType::get(
      Context, "dx.CBuffer", {ArrayType::get(B.getInt8Ty(), ElementSize)});
  auto *HandleFn = Intrinsic::getOrInsertDeclaration(
      M.get(), Intrinsic::dx_resource_handlefrombinding, {HandleTy});
  auto *Handle = B.CreateCall(
      HandleFn, {B.getInt32(7), B.getInt32(3), B.getInt32(RangeSize),
                 B.getInt32(0), ConstantPointerNull::get(B.getPtrTy())});
  unsigned Components = 128 / ElementTy->getScalarSizeInBits();
  SmallVector<Type *, 9> Overloads(Components, ElementTy);
  auto *RowTy = StructType::get(Context, Overloads);
  Overloads.push_back(HandleTy);
  auto *LoadFn = Intrinsic::getOrInsertDeclaration(
      M.get(), feme::dxil::getCBufferRowIntrinsic(*RowTy), Overloads);
  B.CreateCall(LoadFn, {Handle, B.getInt32(0)});
  B.CreateRetVoid();
  return M;
}

TEST(RootConstantLoweringTest, CanonicalShapesRejectMalformedAggregates) {
  LLVMContext C;
  auto *I32 = Type::getInt32Ty(C);
  EXPECT_EQ(feme::dxil::getCBufferRowIntrinsic(*StructType::get(C)),
            Intrinsic::not_intrinsic);
  EXPECT_EQ(feme::dxil::getCBufferRowIntrinsic(*StructType::create(C)),
            Intrinsic::not_intrinsic);
  EXPECT_EQ(feme::dxil::getCBufferRowIntrinsic(*StructType::get(C, {I32, I32})),
            Intrinsic::not_intrinsic);
  EXPECT_EQ(feme::dxil::getCBufferRowIntrinsic(
                *StructType::get(C, {I32, I32, I32, Type::getFloatTy(C)})),
            Intrinsic::not_intrinsic);
}

TEST(RootConstantLoweringTest, LowersAllSixShapesAndVerifiesIR) {
  LLVMContext C;
  SmallVector<Type *, 6> Types{Type::getInt16Ty(C), Type::getHalfTy(C),
                               Type::getInt32Ty(C), Type::getFloatTy(C),
                               Type::getInt64Ty(C), Type::getDoubleTy(C)};
  for (Type *Ty : Types) {
    auto M = makeModule(C, Ty, 16, 1);
    auto Access = matchRootConstantAccess(*M->getFunction("main"));
    ASSERT_TRUE(Access);
    EXPECT_EQ(Access->ElementSize, 16u);
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
    ModuleAnalysisManager AM;
    RootConstantLoweringPass().run(*M, AM);
    EXPECT_FALSE(verifyModule(*M, &errs()));
    EXPECT_FALSE(matchRootConstantAccess(*M->getFunction("main")));
    EXPECT_EQ(M->getFunction("main")->arg_size(), 2u);
  }
}

TEST(RootConstantLoweringTest, ChecksExactABIByteSpanLimit) {
  LLVMContext C;
  auto Max = makeModule(C, Type::getInt32Ty(C), UINT32_MAX, 1);
  EXPECT_TRUE(matchRootConstantAccess(*Max->getFunction("main")));
  for (auto [Size, Range] :
       {std::pair<uint64_t, uint32_t>{uint64_t(UINT32_MAX) + 1, 1},
        {uint64_t(1) << 31, 2},
        {UINT32_MAX, 2},
        {16, UINT32_MAX}}) {
    auto M = makeModule(C, Type::getInt32Ty(C), Size, Range);
    EXPECT_FALSE(matchRootConstantAccess(*M->getFunction("main")));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  }
}

void setLayout(Function &F, ArrayRef<std::array<uint32_t, 5>> Parameters,
               uint32_t Flags = 0) {
  std::vector<uint8_t> Bytes;
  auto Append = [&](uint32_t Value) {
    size_t Offset = Bytes.size();
    Bytes.resize(Offset + 4);
    support::endian::write32le(Bytes.data() + Offset, Value);
  };
  uint32_t Payload = 24 + 12 * Parameters.size();
  for (uint32_t Word : {2u, uint32_t(Parameters.size()), 24u, 0u,
                        Payload + uint32_t(12 * Parameters.size()), Flags})
    Append(Word);
  for (const auto &P : Parameters) {
    Append(P[0]);
    Append(P[1]);
    Append(Payload);
    Payload += 12;
  }
  for (const auto &P : Parameters)
    for (unsigned I = 2; I != 5; ++I)
      Append(P[I]);
  feme::dxil::setRootSignature(F, Bytes);
}

TEST(RootConstantLoweringTest, ExplicitParameterControlsSpan) {
  LLVMContext C;
  for (uint32_t Declared : {4u, 32u}) {
    auto M = makeModule(C, Type::getInt32Ty(C), Declared, 1);
    auto &F = *M->getFunction("main");
    setLayout(F, {{{1, 0, 3, 7, 2}}});
    EXPECT_THAT_ERROR(
        selectRootSignatureConstants(F, feme::ShaderStage::Compute),
        Succeeded());
    auto Access = matchRootConstantAccess(F);
    ASSERT_TRUE(Access);
    EXPECT_EQ(Access->RequiredSize, 8u);
    EXPECT_EQ(Access->ElementSize, std::min(Declared, 8u));
    ModuleAnalysisManager AM;
    RootConstantLoweringPass().run(*M, AM);
    EXPECT_FALSE(verifyModule(*M, &errs()));
    auto *MD = M->getNamedMetadata("feme.cpu.resources")->getOperand(0);
    EXPECT_EQ(mdconst::extract<ConstantInt>(MD->getOperand(1))->getZExtValue(),
              8u);
    EXPECT_THAT_ERROR(validateRootConstantBindings(*M), Succeeded());
  }
}

TEST(RootConstantLoweringTest, ExplicitModeNeverFallsBackToAnotherBinding) {
  LLVMContext C;
  for (uint32_t Range : {1u, 2u}) {
    auto M = makeModule(C, Type::getInt32Ty(C), 16, Range);
    auto &F = *M->getFunction("main");
    EXPECT_TRUE(matchRootConstantAccess(F));
    setLayout(F, {{{1, 0, Range == 1 ? 4u : 3u, 7, 4}}});
    EXPECT_THAT_ERROR(
        selectRootSignatureConstants(F, feme::ShaderStage::Compute), Failed());
    EXPECT_FALSE(F.getMetadata("feme.cpu.rootconstantbinding"));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  }
}

TEST(RootConstantLoweringTest,
     RejectsMissingMalformedAndDescriptorOnlyLayouts) {
  LLVMContext C;
  auto M = makeModule(C, Type::getInt32Ty(C), 16, 1);
  auto &F = *M->getFunction("main");
  EXPECT_THAT_ERROR(selectRootSignatureConstants(F, feme::ShaderStage::Compute),
                    Failed());
  F.setMetadata(feme::dxil::getRootSignatureMDKind(), MDNode::get(C, {}));
  EXPECT_THAT_ERROR(selectRootSignatureConstants(F, feme::ShaderStage::Compute),
                    Failed());
  setLayout(F, {{{2, 0, 3, 7, 0}}});
  EXPECT_THAT_ERROR(selectRootSignatureConstants(F, feme::ShaderStage::Compute),
                    Failed());
  EXPECT_FALSE(F.getMetadata("feme.cpu.rootconstantbinding"));
  EXPECT_TRUE(matchRootConstantAccess(F));
}

TEST(RootConstantLoweringTest, AppliesStageVisibilityAndDenyFlags) {
  LLVMContext C;
  auto M = makeModule(C, Type::getInt32Ty(C), 16, 1);
  auto &F = *M->getFunction("main");
  setLayout(F, {{{1, 1, 3, 7, 4}}, {{1, 5, 3, 7, 2}}});
  EXPECT_THAT_ERROR(selectRootSignatureConstants(F, feme::ShaderStage::Compute),
                    Failed());
  EXPECT_THAT_ERROR(selectRootSignatureConstants(F, feme::ShaderStage::Vertex),
                    Succeeded());
  EXPECT_EQ(matchRootConstantAccess(F)->RequiredSize, 16u);
  EXPECT_THAT_ERROR(
      selectRootSignatureConstants(F, feme::ShaderStage::Fragment),
      Succeeded());
  EXPECT_EQ(matchRootConstantAccess(F)->RequiredSize, 8u);
  setLayout(F, {{{1, 0, 3, 7, 4}}}, 0x2);
  EXPECT_THAT_ERROR(selectRootSignatureConstants(F, feme::ShaderStage::Vertex),
                    Failed());
}

TEST(RootConstantLoweringTest, RejectsMultipleVisibleAndLocalParameters) {
  LLVMContext C;
  auto M = makeModule(C, Type::getInt32Ty(C), 16, 1);
  auto &F = *M->getFunction("main");
  setLayout(F, {{{1, 0, 3, 7, 4}}, {{1, 0, 4, 7, 4}}});
  EXPECT_THAT_ERROR(selectRootSignatureConstants(F, feme::ShaderStage::Compute),
                    Failed());
  setLayout(F, {{{1, 0, 3, 7, 4}}}, 0x80);
  EXPECT_THAT_ERROR(selectRootSignatureConstants(F, feme::ShaderStage::Compute),
                    Failed());
  EXPECT_THAT_ERROR(selectRootSignatureConstants(F, feme::ShaderStage::Library),
                    Failed());
}

TEST(RootConstantLoweringTest, RejectsMalformedExplicitSelectionMetadata) {
  LLVMContext C;
  auto M = makeModule(C, Type::getInt32Ty(C), 16, 1);
  auto &F = *M->getFunction("main");
  F.setMetadata("feme.cpu.rootconstantbinding", MDNode::get(C, {}));
  EXPECT_FALSE(matchRootConstantAccess(F));
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  IRBuilder<> B(&F.getEntryBlock());
  for (uint32_t Size : {0u, 3u, 260u}) {
    F.setMetadata("feme.cpu.rootconstantbinding",
                  MDNode::get(C, {ConstantAsMetadata::get(B.getInt32(7)),
                                  ConstantAsMetadata::get(B.getInt32(3)),
                                  ConstantAsMetadata::get(B.getInt32(Size))}));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  }
}

} // namespace
