//===- RootSignatureTest.cpp - D3D root-signature tests ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#include "feme/Direct3D/RootSignature.h"
#include "feme/Direct3D/BarrierPlan.h"
#include "feme/Direct3D/DescriptorHeap.h"
#include "feme/Direct3D/ResourcePlan.h"

#include "gtest/gtest.h"

#include "llvm/Support/Error.h"

using namespace feme::direct3d;

namespace {

TEST(RootSignatureTest, AcceptsRootConstantsAndTables) {
  RootSignaturePlan Plan;
  RootParameter Constants;
  Constants.Kind = RootParameterKind::Constants;
  Constants.Constants.Num32BitValues = 16;
  Constants.Constants.ShaderRegister = 0;
  Constants.Constants.RegisterSpace = 3;
  Plan.Parameters.push_back(Constants);

  RootParameter Table;
  Table.Kind = RootParameterKind::DescriptorTable;
  Table.Table.Ranges.push_back(DescriptorRange{RootDescriptorClass::SRV, 2, 4,
                                               1, 0, ShaderVisibility::Pixel});
  Plan.Parameters.push_back(Table);

  llvm::Error Err = validateRootSignaturePlan(Plan);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(RootSignatureTest, RejectsTooManyRootConstants) {
  RootSignaturePlan Plan;
  RootParameter Parameter;
  Parameter.Kind = RootParameterKind::Constants;
  Parameter.Constants.Num32BitValues = 65;
  Plan.Parameters.push_back(Parameter);

  llvm::Error Err = validateRootSignaturePlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(RootSignatureTest, RejectsOverlappingTableRanges) {
  RootSignaturePlan Plan;
  RootParameter Parameter;
  Parameter.Kind = RootParameterKind::DescriptorTable;
  Parameter.Table.Ranges.push_back(DescriptorRange{
      RootDescriptorClass::SRV, 0, 4, 0, 0, ShaderVisibility::All});
  Parameter.Table.Ranges.push_back(DescriptorRange{
      RootDescriptorClass::SRV, 3, 2, 0, 4, ShaderVisibility::All});
  Plan.Parameters.push_back(Parameter);

  llvm::Error Err = validateRootSignaturePlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(RootSignatureTest, ResolvesDescriptorTableRegister) {
  DescriptorTable Table;
  Table.Ranges.push_back(DescriptorRange{RootDescriptorClass::SRV, 10, 3, 2, 7,
                                         ShaderVisibility::All});

  EXPECT_EQ(resolveDescriptorIndex(Table, RootDescriptorClass::SRV, 2, 10),
            std::optional<uint32_t>(7));
  EXPECT_EQ(resolveDescriptorIndex(Table, RootDescriptorClass::SRV, 2, 12),
            std::optional<uint32_t>(9));
  EXPECT_EQ(resolveDescriptorIndex(Table, RootDescriptorClass::UAV, 2, 10),
            std::nullopt);
  EXPECT_EQ(resolveDescriptorIndex(Table, RootDescriptorClass::SRV, 1, 10),
            std::nullopt);
  EXPECT_EQ(resolveDescriptorIndex(Table, RootDescriptorClass::SRV, 2, 13),
            std::nullopt);
}

TEST(RootSignatureTest, RejectsDuplicateStaticSamplers) {
  RootSignaturePlan Plan;
  Plan.StaticSamplers.push_back(StaticSampler{1, 2, ShaderVisibility::All});
  Plan.StaticSamplers.push_back(StaticSampler{1, 2, ShaderVisibility::Pixel});

  llvm::Error Err = validateRootSignaturePlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(DescriptorHeapTest, AcceptsHeapRangesWithinCapacity) {
  DescriptorHeapPlan Plan{
      DescriptorHeapType::CBV_SRV_UAV, 32, {{0, 8}, {24, 8}}};
  llvm::Error Err = validateDescriptorHeapPlan(Plan);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(DescriptorHeapTest, RejectsAllocationPastHeapCapacity) {
  DescriptorHeapPlan Plan{DescriptorHeapType::Sampler, 16, {{8, 9}}};
  llvm::Error Err = validateDescriptorHeapPlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(DescriptorHeapTest, RejectsTypeMismatchAndOutOfBoundsCopy) {
  DescriptorHeapPlan Source{DescriptorHeapType::CBV_SRV_UAV, 16, {}};
  DescriptorHeapPlan Destination{DescriptorHeapType::Sampler, 16, {}};
  llvm::Error Err = validateDescriptorCopy(Source, 0, Destination, 0, 1);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  Destination.Type = DescriptorHeapType::CBV_SRV_UAV;
  Err = validateDescriptorCopy(Source, 15, Destination, 15, 2);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(ResourcePlanTest, AcceptsBufferAndArrayResources) {
  ResourcePlan Buffer{ResourceDimension::Buffer, 4096, 1, 1, 1, 1, 1};
  llvm::Error Err = validateResourcePlan(Buffer);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));

  ResourcePlan Array{ResourceDimension::Texture2D, 16, 8, 1, 3, 3, 4};
  Err = validateResourcePlan(Array);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(ResourcePlanTest, RejectsInvalidTextureState) {
  ResourcePlan Plan{ResourceDimension::Texture1D, 0, 1, 1, 1, 1, 1};
  llvm::Error Err = validateResourcePlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  Plan = ResourcePlan{ResourceDimension::Texture2D, 16, 8, 2, 1, 1, 3};
  Err = validateResourcePlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(ResourcePlanTest, ResolvesArrayAnd3DSubresources) {
  ResourcePlan Array{ResourceDimension::Texture2D, 8, 8, 1, 3, 3, 1};
  EXPECT_EQ(resolveSubresourceIndex(Array, 0, 0), std::optional<uint64_t>(0));
  EXPECT_EQ(resolveSubresourceIndex(Array, 0, 2), std::optional<uint64_t>(2));
  EXPECT_EQ(resolveSubresourceIndex(Array, 1, 1), std::optional<uint64_t>(4));
  EXPECT_EQ(resolveSubresourceIndex(Array, 3, 0), std::nullopt);

  ResourcePlan Volume{ResourceDimension::Texture3D, 8, 8, 8, 1, 3, 1};
  EXPECT_EQ(resolveSubresourceIndex(Volume, 0, 0, 7),
            std::optional<uint64_t>(7));
  EXPECT_EQ(resolveSubresourceIndex(Volume, 1, 0, 3),
            std::optional<uint64_t>(11));
  EXPECT_EQ(resolveSubresourceIndex(Volume, 1, 0, 4), std::nullopt);
  EXPECT_EQ(resolveSubresourceIndex(Volume, 2, 0, 1),
            std::optional<uint64_t>(13));
}

TEST(BarrierPlanTest, AcceptsOrderedTransitionAndUAVBarriers) {
  std::vector<BarrierPlan> Barriers{
      {BarrierKind::Transition, ResourceState::CopyDestination,
       ResourceState::ShaderResource, SubresourceRange{0, 4, 0, 2}},
      {BarrierKind::UAV, ResourceState::UnorderedAccess,
       ResourceState::UnorderedAccess, SubresourceRange{0, 1, 0, 1}},
  };
  llvm::Error Err = validateBarrierPlan(Barriers);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(BarrierPlanTest, RejectsEmptyRangeAndNoStateTransition) {
  std::vector<BarrierPlan> EmptyRange{
      {BarrierKind::Transition, ResourceState::Common,
       ResourceState::ShaderResource, SubresourceRange{0, 0, 0, 1}},
  };
  llvm::Error Err = validateBarrierPlan(EmptyRange);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  std::vector<BarrierPlan> NoTransition{
      {BarrierKind::Transition, ResourceState::Common, ResourceState::Common,
       SubresourceRange{0, 1, 0, 1}},
  };
  Err = validateBarrierPlan(NoTransition);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(BarrierPlanTest, RejectsUAVBarrierChangingState) {
  std::vector<BarrierPlan> Barriers{
      {BarrierKind::UAV, ResourceState::UnorderedAccess,
       ResourceState::ShaderResource, SubresourceRange{0, 1, 0, 1}},
  };
  llvm::Error Err = validateBarrierPlan(Barriers);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

} // namespace
