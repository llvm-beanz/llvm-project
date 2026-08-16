//===- ResourceHeapTest.cpp - Tests for materializeResourceHeap ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/ResourceHeap.h"

#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

FemeDescriptor makeDescriptor(void *Data) {
  FemeDescriptor Desc{};
  Desc.Data = Data;
  Desc.SizeInBytes = 4;
  Desc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Desc.Flags = FEME_DESCRIPTOR_UAV;
  return Desc;
}

TEST(ResourceHeapTest, NoReservedPrefixIsExactlyTheDynamicHeap) {
  ResourceInfo Info;
  int Dummy = 0;
  std::vector<FemeDescriptor> Dynamic = {makeDescriptor(&Dummy)};

  std::vector<FemeDescriptor> Heap =
      materializeResourceHeap(Info, /*Bindings=*/{}, Dynamic);
  ASSERT_EQ(Heap.size(), 1u);
  EXPECT_EQ(Heap[0].Data, &Dummy);
}

TEST(ResourceHeapTest, FillsMatchedRangeAndAppendsDynamicHeap) {
  ResourceInfo Info;
  Info.ReservedResourceHeapSize = 4;
  Info.BoundRanges = {BoundResourceRange{/*Space=*/0, /*BaseRegister=*/0,
                                         /*RangeSize=*/4, /*HeapBase=*/0}};

  int Bound0 = 1, Bound1 = 2, Dyn0 = 3;
  BoundResourceBinding Binding;
  Binding.Space = 0;
  Binding.BaseRegister = 0;
  std::vector<FemeDescriptor> BoundDescs = {makeDescriptor(&Bound0),
                                            makeDescriptor(&Bound1)};
  Binding.Descriptors = BoundDescs;

  std::vector<FemeDescriptor> Dynamic = {makeDescriptor(&Dyn0)};

  std::vector<FemeDescriptor> Heap =
      materializeResourceHeap(Info, {Binding}, Dynamic);
  ASSERT_EQ(Heap.size(), 5u); // 4 reserved + 1 dynamic.
  EXPECT_EQ(Heap[0].Data, &Bound0);
  EXPECT_EQ(Heap[1].Data, &Bound1);
  // Slots 2 and 3 of the range were never supplied: left as the zero
  // descriptor (see "Descriptor heaps" in feme/docs/FeMeCPUDesign.md).
  EXPECT_EQ(Heap[2].Data, nullptr);
  EXPECT_EQ(Heap[2].Kind, static_cast<uint32_t>(ResourceKind::None));
  EXPECT_EQ(Heap[3].Data, nullptr);
  EXPECT_EQ(Heap[4].Data, &Dyn0);
}

TEST(ResourceHeapTest, UnboundRangeIsEntirelyZeroDescriptors) {
  ResourceInfo Info;
  Info.ReservedResourceHeapSize = 2;
  Info.BoundRanges = {BoundResourceRange{0, 0, 2, 0}};

  std::vector<FemeDescriptor> Heap =
      materializeResourceHeap(Info, /*Bindings=*/{}, /*DynamicHeap=*/{});
  ASSERT_EQ(Heap.size(), 2u);
  EXPECT_EQ(Heap[0].Kind, static_cast<uint32_t>(ResourceKind::None));
  EXPECT_EQ(Heap[1].Kind, static_cast<uint32_t>(ResourceKind::None));
}

TEST(ResourceHeapTest, MultipleRangesFillTheirOwnHeapBase) {
  ResourceInfo Info;
  Info.ReservedResourceHeapSize = 3;
  Info.BoundRanges = {
      BoundResourceRange{0, 0, 1, 0},
      BoundResourceRange{0, 1, 2, 1},
  };

  int A = 1, B = 2;
  BoundResourceBinding BindingA, BindingB;
  BindingA.Space = 0;
  BindingA.BaseRegister = 0;
  std::vector<FemeDescriptor> ADescs = {makeDescriptor(&A)};
  BindingA.Descriptors = ADescs;

  BindingB.Space = 0;
  BindingB.BaseRegister = 1;
  std::vector<FemeDescriptor> BDescs = {makeDescriptor(&B)};
  BindingB.Descriptors = BDescs;

  std::vector<FemeDescriptor> Heap =
      materializeResourceHeap(Info, {BindingA, BindingB}, /*DynamicHeap=*/{});
  ASSERT_EQ(Heap.size(), 3u);
  EXPECT_EQ(Heap[0].Data, &A);
  EXPECT_EQ(Heap[1].Data, &B);
  EXPECT_EQ(Heap[2].Kind, static_cast<uint32_t>(ResourceKind::None));
}

namespace {
// Records every `FemeDispatchArgs::GroupID` `runDispatch` calls it with,
// and (for the second test below) whether the resource heap it saw inside
// the call matches expectations -- captured eagerly inside the callback,
// since `Args->ResourceHeap` points into `runDispatch`'s own local,
// materialized heap and is not valid once the call returns.
std::vector<std::array<uint32_t, 3>> *RecordedGroupIDs = nullptr;
const void *ExpectedFirstDescriptorData = nullptr;
bool SawExpectedResourceHeap = false;

void recordingEntryFn(const FemeDispatchArgs *Args) {
  RecordedGroupIDs->push_back(
      {Args->GroupID[0], Args->GroupID[1], Args->GroupID[2]});
  SawExpectedResourceHeap =
      Args->ResourceHeapCount == 1 &&
      Args->ResourceHeap[0].Data == ExpectedFirstDescriptorData;
}
} // namespace

TEST(RunDispatchTest, CallsEntryOncePerGroupInXYZOrder) {
  std::vector<std::array<uint32_t, 3>> GroupIDs;
  RecordedGroupIDs = &GroupIDs;

  ResourceInfo Info;
  DispatchResources Resources;
  runDispatch(recordingEntryFn, Info, Resources, {2, 2, 1});

  EXPECT_EQ(GroupIDs, (std::vector<std::array<uint32_t, 3>>{
                          {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}}));
}

TEST(RunDispatchTest, PassesTheMaterializedHeapToTheEntryPoint) {
  std::vector<std::array<uint32_t, 3>> GroupIDs;
  RecordedGroupIDs = &GroupIDs;

  int Dummy = 0;
  std::vector<FemeDescriptor> Dynamic = {makeDescriptor(&Dummy)};
  ExpectedFirstDescriptorData = &Dummy;
  SawExpectedResourceHeap = false;

  ResourceInfo Info;
  DispatchResources Resources;
  Resources.ResourceHeap = Dynamic;
  runDispatch(recordingEntryFn, Info, Resources, {1, 1, 1});

  EXPECT_TRUE(SawExpectedResourceHeap);
}

// Roadmap milestone R21's `PreparedDispatch`/`invokeGroup`: covers the two
// directly at the granularity `feme::cpu::CompiledStage::invokeGroup` (and,
// through it, a worker-pool `JITEngine::dispatch`) actually calls them at,
// rather than only indirectly through `runDispatch`'s own group loop above.
TEST(PreparedDispatchTest, ArgsForFillsInTheRequestedGroupIDAndGroupCount) {
  ResourceInfo Info;
  DispatchResources Resources;
  PreparedDispatch Prepared =
      PreparedDispatch::create(Info, Resources, {2, 3, 1});

  FemeDispatchArgs Args = Prepared.argsFor({1, 2, 0}, /*GroupShared=*/{});
  EXPECT_EQ(Args.GroupID[0], 1u);
  EXPECT_EQ(Args.GroupID[1], 2u);
  EXPECT_EQ(Args.GroupID[2], 0u);
  EXPECT_EQ(Args.GroupCount[0], 2u);
  EXPECT_EQ(Args.GroupCount[1], 3u);
  EXPECT_EQ(Args.GroupCount[2], 1u);
}

TEST(PreparedDispatchTest, ArgsForCarriesTheMaterializedHeapAndGroupShared) {
  int Dummy = 0;
  std::vector<FemeDescriptor> Dynamic = {makeDescriptor(&Dummy)};

  ResourceInfo Info;
  DispatchResources Resources;
  Resources.ResourceHeap = Dynamic;
  PreparedDispatch Prepared =
      PreparedDispatch::create(Info, Resources, {1, 1, 1});

  std::vector<uint8_t> GroupShared(4, 0);
  FemeDispatchArgs Args = Prepared.argsFor({0, 0, 0}, GroupShared);
  ASSERT_EQ(Args.ResourceHeapCount, 1u);
  EXPECT_EQ(Args.ResourceHeap[0].Data, &Dummy);
  EXPECT_EQ(Args.GroupShared, GroupShared.data());
}

TEST(InvokeGroupTest, CallsEntryOnceWithTheRequestedGroupID) {
  std::vector<std::array<uint32_t, 3>> GroupIDs;
  RecordedGroupIDs = &GroupIDs;

  ResourceInfo Info;
  DispatchResources Resources;
  PreparedDispatch Prepared =
      PreparedDispatch::create(Info, Resources, {2, 1, 1});

  invokeGroup(recordingEntryFn, Prepared, {1, 0, 0}, /*GroupShared=*/{});

  EXPECT_EQ(GroupIDs, (std::vector<std::array<uint32_t, 3>>{{1, 0, 0}}));
}

TEST(PreparedVertexBatchTest, ArgsExposeCallerOwnedStageStorage) {
  ResourceInfo Info;
  FemeStageLayout Layout{};
  std::vector<float> Inputs(2, 0.0f);
  std::vector<float> Outputs(2, 0.0f);
  FemeVertexInvocation Invocations[2] = {};

  VertexResources Resources;
  Resources.InputLayout = &Layout;
  Resources.Inputs = Inputs.data();
  Resources.OutputLayout = &Layout;
  Resources.Outputs = Outputs.data();
  Resources.Invocations = Invocations;
  PreparedVertexBatch Prepared = PreparedVertexBatch::create(Info, Resources);

  FemeVertexArgs Args = Prepared.args();
  EXPECT_EQ(Args.AbiVersion, StageArgsAbiVersion);
  EXPECT_EQ(Args.InvocationCount, 2u);
  EXPECT_EQ(Args.InputLayout, &Layout);
  EXPECT_EQ(Args.Inputs, Inputs.data());
  EXPECT_EQ(Args.Outputs, Outputs.data());
  EXPECT_EQ(Args.Invocations, Invocations);
}

TEST(PreparedFragmentBatchTest, ArgsExposeCallerOwnedStageStorage) {
  ResourceInfo Info;
  FemeStageLayout Layout{};
  std::vector<float> Inputs(4, 0.0f);
  std::vector<float> Outputs(4, 0.0f);
  FemeFragmentInvocation Invocation{};
  FemeFragmentResult Result{};

  FragmentResources Resources;
  Resources.InputLayout = &Layout;
  Resources.Inputs = Inputs.data();
  Resources.OutputLayout = &Layout;
  Resources.Outputs = Outputs.data();
  Resources.Invocations = ArrayRef<FemeFragmentInvocation>(&Invocation, 1);
  Resources.Results = MutableArrayRef<FemeFragmentResult>(&Result, 1);
  PreparedFragmentBatch Prepared =
      PreparedFragmentBatch::create(Info, Resources);

  FemeFragmentArgs Args = Prepared.args();
  EXPECT_EQ(Args.AbiVersion, StageArgsAbiVersion);
  EXPECT_EQ(Args.QuadCount, 1u);
  EXPECT_EQ(Args.InputLayout, &Layout);
  EXPECT_EQ(Args.Inputs, Inputs.data());
  EXPECT_EQ(Args.Outputs, Outputs.data());
  EXPECT_EQ(Args.Invocations, &Invocation);
  EXPECT_EQ(Args.Results, &Result);
}

} // namespace
