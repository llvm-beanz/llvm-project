//===- RootSignatureTest.cpp - D3D root-signature tests ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#include "feme/Direct3D/RootSignature.h"
#include "feme/Direct3D/AccelerationStructurePlan.h"
#include "feme/Direct3D/AdvancedGraphicsPlan.h"
#include "feme/Direct3D/BarrierPlan.h"
#include "feme/Direct3D/CapabilityInventory.h"
#include "feme/Direct3D/ComputePlan.h"
#include "feme/Direct3D/ConformanceReport.h"
#include "feme/Direct3D/DescriptorHeap.h"
#include "feme/Direct3D/DrawPlan.h"
#include "feme/Direct3D/EnhancedBarrierPlan.h"
#include "feme/Direct3D/ExecutionPlan.h"
#include "feme/Direct3D/InputLayoutPlan.h"
#include "feme/Direct3D/MeshDispatchPlan.h"
#include "feme/Direct3D/ObjectModel.h"
#include "feme/Direct3D/PipelinePlan.h"
#include "feme/Direct3D/PresentationPlan.h"
#include "feme/Direct3D/RayTracingPlan.h"
#include "feme/Direct3D/ResidencyPlan.h"
#include "feme/Direct3D/ResourcePlan.h"
#include "feme/Direct3D/ViewportPlan.h"

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

TEST(PipelinePlanTest, AcceptsConventionalTrianglePipeline) {
  PipelinePlan Plan;
  Plan.Primitive = PrimitiveTopology::TriangleList;
  Plan.Rasterizer.Cull = CullMode::Back;
  Plan.DepthStencil.DepthEnable = true;
  Plan.DepthStencil.DepthWriteEnable = true;
  llvm::Error Err = validatePipelinePlan(Plan);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(PipelinePlanTest, RejectsUndefinedTopologyAndTooManyTargets) {
  PipelinePlan Plan;
  Plan.Primitive = PrimitiveTopology::Undefined;
  llvm::Error Err = validatePipelinePlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  Plan = PipelinePlan{};
  Plan.RenderTargets.NumRenderTargets = 9;
  Err = validatePipelinePlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(PipelinePlanTest, RejectsDepthOnlyPipelineWithNoDepthTarget) {
  PipelinePlan Plan;
  Plan.RenderTargets.NumRenderTargets = 0;
  Plan.DepthStencil.DepthEnable = true;
  llvm::Error Err = validatePipelinePlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(PipelinePlanTest, AcceptsMRTIndependentBlend) {
  PipelinePlan Plan;
  Plan.RenderTargets.NumRenderTargets = 2;
  Plan.RenderTargets.IndependentBlendEnable = true;
  Plan.Blend.Attachments[1].BlendEnable = true;
  Plan.Blend.Attachments[1].SrcColor = BlendFactor::SrcAlpha;
  llvm::Error Err = validatePipelinePlan(Plan);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(PipelinePlanTest, RejectsDifferentMRTBlendWithoutIndependentBlend) {
  PipelinePlan Plan;
  Plan.RenderTargets.NumRenderTargets = 2;
  Plan.Blend.Attachments[1].BlendEnable = true;
  llvm::Error Err = validatePipelinePlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(AdvancedGraphicsPlanTest, AcceptsFL122AdvancedLimits) {
  AdvancedGraphicsPlan Plan;
  Plan.Vrs = VrsTier::Tier2;
  Plan.SamplerFeedback = true;
  Plan.ConservativeRaster = true;
  Plan.TiledResources = true;
  Plan.DepthBounds = true;
  llvm::Error Err = validateAdvancedGraphicsPlan(Plan);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(AdvancedGraphicsPlanTest, RejectsOversizedMeshOutput) {
  AdvancedGraphicsPlan Plan;
  Plan.Mesh.MaxPayloadBytes = 32768;
  llvm::Error Err = validateAdvancedGraphicsPlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(RayTracingPlanTest, AcceptsDXR11Bounds) {
  RayTracingPlan Plan{1024, 32, 31, 65536, 65536};
  llvm::Error Err = validateRayTracingPlan(Plan);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(RayTracingPlanTest, RejectsOversizedPayloadAndZeroRecursion) {
  RayTracingPlan Payload{8192, 32, 31, 1, 1};
  llvm::Error Err = validateRayTracingPlan(Payload);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  RayTracingPlan Recursion{1024, 32, 0, 1, 1};
  Err = validateRayTracingPlan(Recursion);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(PresentationPlanTest, AcceptsRotationAndHDRSharedInterop) {
  PresentationPlan Plan{1920, 1080, DisplayRotation::Rotate90, false, true,
                        true, true};
  llvm::Error Err = validatePresentationPlan(Plan);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(PresentationPlanTest, RejectsInvalidPresentationContracts) {
  PresentationPlan Zero{0,     1080, DisplayRotation::Identity, false, false,
                        false, false};
  llvm::Error Err = validatePresentationPlan(Zero);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  PresentationPlan HDR{1920,  1080, DisplayRotation::Identity, false, true,
                       false, false};
  Err = validatePresentationPlan(HDR);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(CapabilityInventoryTest, RejectsMissingNegotiatedDDI) {
  CapabilityInventory Inventory;
  Inventory.Compute = true;
  llvm::Error Err = validateCapabilityInventory(Inventory);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(CapabilityInventoryTest, RejectsIncompleteFL122Claim) {
  CapabilityInventory Inventory{1, true, true, false, true, true, true};
  llvm::Error Err = validateCapabilityInventory(Inventory);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(CapabilityInventoryTest, AcceptsCompleteFL122Evidence) {
  CapabilityInventory Inventory{1, true, true, true, true, true, true};
  llvm::Error Err = validateCapabilityInventory(Inventory);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(ComputePlanTest, AcceptsHeadlessComputeDispatch) {
  DevicePlan Device;
  Device.RequestedWaveSize = 8;
  Device.RepeatDispatches = 3;
  llvm::Error Err = validateDevicePlan(Device);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));

  ComputeDispatchPlan Dispatch{{5, 3, 2}, {3, 2, 1}, 8, 2};
  Err = validateComputeDispatchPlan(Dispatch);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
  EXPECT_EQ(totalComputeThreads(Dispatch), std::optional<uint64_t>(360));
  EXPECT_EQ(ceilComputeThreadsByWave(Dispatch), std::optional<uint64_t>(45));
}

TEST(ComputePlanTest, RejectsInvalidWaveAndGroupSizes) {
  DevicePlan Device;
  Device.RequestedWaveSize = 6;
  llvm::Error Err = validateDevicePlan(Device);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  ComputeDispatchPlan Dispatch{{0, 3, 2}, {1, 1, 1}, 8, 1};
  Err = validateComputeDispatchPlan(Dispatch);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(ComputePlanTest, DetectsThreadTotalOverflow) {
  ComputeDispatchPlan Dispatch{{UINT32_MAX, UINT32_MAX, UINT32_MAX},
                               {UINT32_MAX, UINT32_MAX, UINT32_MAX},
                               64,
                               1};
  llvm::Error Err = validateComputeDispatchPlan(Dispatch);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
  EXPECT_EQ(totalComputeThreads(Dispatch), std::nullopt);
}

TEST(ExecutionPlanTest, AcceptsBufferCopyAndMonotonicFence) {
  ExecutionPlan Plan;
  Plan.Copies.push_back(BufferCopyPlan{16, 32, 64, 128, 32});
  Plan.Fence = FencePlan{0, 7, 7, false};
  Plan.RepeatCount = 3;
  llvm::Error Err = validateExecutionPlan(Plan);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(ExecutionPlanTest, RejectsOutOfBoundsBufferCopy) {
  ExecutionPlan Plan;
  Plan.Copies.push_back(BufferCopyPlan{48, 0, 64, 64, 32});
  llvm::Error Err = validateExecutionPlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(ExecutionPlanTest, RejectsPastTargetAndDecreasingFence) {
  ExecutionPlan Plan;
  Plan.Fence = FencePlan{0, 9, 8, false};
  llvm::Error Err = validateExecutionPlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  Plan.Fence = FencePlan{8, 7, 7, false};
  Err = validateExecutionPlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(ObjectModelTest, CreatesAndClosesTypedObjects) {
  ObjectModel Model;
  ObjectHandle Device = Model.create(ObjectKind::Device);
  ObjectHandle Queue = Model.create(ObjectKind::CommandQueue);
  EXPECT_TRUE(Model.isAlive(Device));
  EXPECT_TRUE(Model.isAlive(Queue));
  llvm::Error Err = Model.close(Device);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
  EXPECT_FALSE(Model.isAlive(Device));
  EXPECT_TRUE(Model.isAlive(Queue));
}

TEST(ObjectModelTest, RejectsCloseWithPendingWork) {
  ObjectModel Model;
  ObjectHandle Queue = Model.create(ObjectKind::CommandQueue);
  llvm::Error Err = Model.setPending(Queue, 2);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
  Err = Model.close(Queue);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
  EXPECT_TRUE(Model.isAlive(Queue));
  EXPECT_EQ(Model.pending(Queue), 2);
}

TEST(ObjectModelTest, RejectsResetForNonAllocators) {
  ObjectModel Model;
  ObjectHandle Device = Model.create(ObjectKind::Device);
  llvm::Error Err = Model.reset(Device);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(ObjectModelTest, ResetsIdleAllocatorAndReplaysIt) {
  ObjectModel Model;
  ObjectHandle Allocator = Model.create(ObjectKind::Allocator);
  llvm::Error Err = Model.reset(Allocator);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
  Err = Model.replay(Allocator);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
  EXPECT_EQ(Model.replayCount(Allocator), 1);
}

TEST(ObjectModelTest, RejectsResetWithPendingWork) {
  ObjectModel Model;
  ObjectHandle Allocator = Model.create(ObjectKind::Allocator);
  llvm::Error Err = Model.setPending(Allocator, 1);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
  Err = Model.reset(Allocator);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(ResidencyPlanTest, AcceptsAlignedHeapsAndPlacedResources) {
  HeapPlan Heap{HeapKind::Upload, 65536, 65536, true};
  llvm::Error Err = validateHeapPlan(Heap);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));

  PlacedResourcePlan Placed{65536, 32768, 32768, 4096};
  Err = validatePlacedResourcePlan(Placed);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(ResidencyPlanTest, RejectsMisalignedAndUnboundedHeaps) {
  HeapPlan Heap{HeapKind::Default, 65537, 65536, false};
  llvm::Error Err = validateHeapPlan(Heap);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  Heap = HeapPlan{HeapKind::Readback, 65536, 65536, false};
  Err = validateHeapPlan(Heap);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(ResidencyPlanTest, RejectsPlacedResourceOutsideHeap) {
  PlacedResourcePlan Placed{65536, 61440, 7000, 4096};
  llvm::Error Err = validatePlacedResourcePlan(Placed);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(ResidencyPlanTest, EnforcesResidencyBudgetAndCount) {
  ResidencyPlan Plan{{4096, 8192, 16384}, 24576, 2};
  llvm::Error Err = validateResidencyPlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  Plan = ResidencyPlan{{4096, 8192, 4096}, 32768, 3};
  Err = validateResidencyPlan(Plan);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(DrawPlanTest, AcceptsIndexedAndInstancedDraw) {
  DrawPlan Indexed{0, 0, 4, 2, true, 12, 3, -4, 1024, 20, false};
  llvm::Error Err = validateDrawPlan(Indexed);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));

  DrawPlan NonIndexed{36, 10, 3, 1, false, 0, 0, 0, UINT64_MAX, 0, true};
  Err = validateDrawPlan(NonIndexed);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(DrawPlanTest, RejectsEmptyAndOverflowingDraws) {
  DrawPlan Empty{0, 0, 1, 0, false, 0, 0, 0, UINT64_MAX, 0, false};
  llvm::Error Err = validateDrawPlan(Empty);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  DrawPlan Overflow{0, UINT32_MAX, 1, 0, false, 0, 0, 0, UINT64_MAX, 0, false};
  Overflow.VertexCount = 2;
  Err = validateDrawPlan(Overflow);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(DrawPlanTest, RequiresPositiveIndirectArgumentSize) {
  DrawPlan Plan{36, 0, 1, 0, false, 0, 0, 0, 128, 0, false};
  llvm::Error Err = validateDrawPlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(MeshDispatchPlanTest, AcceptsTier1MeshDispatch) {
  MeshDispatchPlan Plan{{4, 2, 1}, 4096, 256, 256, 512, 32};
  llvm::Error Err = validateMeshDispatchPlan(Plan);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(MeshDispatchPlanTest, RejectsOversizedPayloadAndOutput) {
  MeshDispatchPlan Payload{{1, 1, 1}, 16385, 64, 64, UINT64_MAX, 0};
  llvm::Error Err = validateMeshDispatchPlan(Payload);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  MeshDispatchPlan Output{{1, 1, 1}, 1024, 257, 64, UINT64_MAX, 0};
  Err = validateMeshDispatchPlan(Output);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(MeshDispatchPlanTest, RejectsZeroGroupAndIndirectSize) {
  MeshDispatchPlan Zero{{0, 1, 1}, 1024, 64, 64, UINT64_MAX, 0};
  llvm::Error Err = validateMeshDispatchPlan(Zero);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  MeshDispatchPlan Indirect{{1, 1, 1}, 1024, 64, 64, 128, 0};
  Err = validateMeshDispatchPlan(Indirect);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(AccelerationStructurePlanTest, AcceptsBLASAndTLASBuildPlans) {
  AccelerationStructurePlan BLAS{
      AccelerationStructureKind::BottomLevel, 65536, 256, 4, 0, true, false};
  llvm::Error Err = validateAccelerationStructurePlan(BLAS);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));

  AccelerationStructurePlan TLAS{
      AccelerationStructureKind::TopLevel, 131072, 512, 0, 128, false, true};
  Err = validateAccelerationStructurePlan(TLAS);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(AccelerationStructurePlanTest, RejectsInvalidSizeAndAlignment) {
  AccelerationStructurePlan Plan{
      AccelerationStructureKind::BottomLevel, 0, 0, 1, 0, false, false};
  llvm::Error Err = validateAccelerationStructurePlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  Plan.SizeBytes = 4096;
  Plan.OffsetBytes = 255;
  Err = validateAccelerationStructurePlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(AccelerationStructurePlanTest,
     RejectsMissingCountsAndCombinedBuildPolicy) {
  AccelerationStructurePlan TLAS{
      AccelerationStructureKind::TopLevel, 4096, 0, 0, 0, false, false};
  llvm::Error Err = validateAccelerationStructurePlan(TLAS);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  AccelerationStructurePlan Combined{
      AccelerationStructureKind::BottomLevel, 4096, 0, 1, 0, true, true};
  Err = validateAccelerationStructurePlan(Combined);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(EnhancedBarrierPlanTest, AcceptsSingleAndSplitBarriers) {
  EnhancedBarrierPlan Single{
      BarrierPhase::Single,      BarrierSync::Copy,
      BarrierSync::Compute,      BarrierAccess::CopyWrite,
      BarrierAccess::ShaderRead, EnhancedBarrierRange{0, 4, 0, 2, 0, 1}};
  llvm::Error Err = validateEnhancedBarrierPlan(Single);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));

  EnhancedBarrierPlan Begin{
      BarrierPhase::Begin,     BarrierSync::Draw,
      BarrierSync::None,       BarrierAccess::RenderTarget,
      BarrierAccess::NoAccess, EnhancedBarrierRange{0, 1, 0, 1, 0, 1}};
  Err = validateEnhancedBarrierPlan(Begin);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(EnhancedBarrierPlanTest, RejectsEmptyRangeAndMissingSync) {
  EnhancedBarrierPlan Empty{
      BarrierPhase::Single,    BarrierSync::Draw,
      BarrierSync::Copy,       BarrierAccess::RenderTarget,
      BarrierAccess::CopyRead, EnhancedBarrierRange{0, 0, 0, 1, 0, 1}};
  llvm::Error Err = validateEnhancedBarrierPlan(Empty);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  EnhancedBarrierPlan Missing{
      BarrierPhase::End,       BarrierSync::Draw,
      BarrierSync::None,       BarrierAccess::RenderTarget,
      BarrierAccess::NoAccess, EnhancedBarrierRange{0, 1, 0, 1, 0, 1}};
  Err = validateEnhancedBarrierPlan(Missing);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(ConformanceReportTest, AcceptsReadyAndRejectsPartialReports) {
  ConformanceReport Ready{100, 100, 0, 0, 0, 0};
  llvm::Error Err = validateConformanceReport(Ready);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
  EXPECT_TRUE(isConformanceReleaseReady(Ready));

  ConformanceReport Partial{100, 90, 2, 8, 0, 0};
  Err = validateConformanceReport(Partial);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
  EXPECT_FALSE(isConformanceReleaseReady(Partial));
}

TEST(ConformanceReportTest, RejectsClassifiedCountOverflow) {
  ConformanceReport Report{10, 7, 2, 2, 0, 0};
  llvm::Error Err = validateConformanceReport(Report);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
  EXPECT_FALSE(isConformanceReleaseReady(Report));
}

TEST(ViewportPlanTest, AcceptsViewportAndScissor) {
  ViewportPlan Viewport{0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f};
  ScissorRectPlan Scissor{0, 0, 1920, 1080};
  llvm::Error Err = validateViewportPlan(Viewport, Scissor);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(ViewportPlanTest, RejectsInvalidSizeAndDepth) {
  ViewportPlan Viewport{0.0f, 0.0f, 0.0f, 1080.0f, 0.0f, 1.0f};
  ScissorRectPlan Scissor{0, 0, 1920, 1080};
  llvm::Error Err = validateViewportPlan(Viewport, Scissor);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  Viewport = ViewportPlan{0.0f, 0.0f, 1920.0f, 1080.0f, 0.5f, 0.5f};
  Err = validateViewportPlan(Viewport, Scissor);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(ViewportPlanTest, RejectsReversedScissor) {
  ViewportPlan Viewport{0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f};
  ScissorRectPlan Scissor{1920, 1080, 0, 0};
  llvm::Error Err = validateViewportPlan(Viewport, Scissor);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(InputLayoutPlanTest, AcceptsVectorAndColorElements) {
  InputLayoutPlan Plan;
  Plan.Elements.push_back(InputElementPlan{0, 0, 24, 0, InputSemantic::Position,
                                           InputFormat::Float32x3});
  Plan.Elements.push_back(InputElementPlan{0, 12, 24, 0, InputSemantic::Color,
                                           InputFormat::Float32x3});
  llvm::Error Err = validateInputLayoutPlan(Plan);
  EXPECT_FALSE(static_cast<bool>(Err));
  if (Err)
    llvm::consumeError(std::move(Err));
}

TEST(InputLayoutPlanTest, RejectsDuplicateSemantics) {
  InputLayoutPlan Plan;
  Plan.Elements.push_back(InputElementPlan{0, 0, 16, 0, InputSemantic::Texcoord,
                                           InputFormat::Float32x2});
  Plan.Elements.push_back(InputElementPlan{0, 8, 16, 0, InputSemantic::Texcoord,
                                           InputFormat::Float32x2});
  llvm::Error Err = validateInputLayoutPlan(Plan);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

TEST(InputLayoutPlanTest, RejectsEmptyAndOutOfStrideElements) {
  InputLayoutPlan Empty;
  llvm::Error Err = validateInputLayoutPlan(Empty);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));

  InputLayoutPlan OutOfStride;
  OutOfStride.Elements.push_back(InputElementPlan{
      0, 12, 16, 0, InputSemantic::Position, InputFormat::Float32x4});
  Err = validateInputLayoutPlan(OutOfStride);
  ASSERT_TRUE(static_cast<bool>(Err));
  llvm::consumeError(std::move(Err));
}

} // namespace
