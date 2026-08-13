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

} // namespace
