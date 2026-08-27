//===- GeometryInputsTest.cpp - Tests for GeometryInputs.h helpers -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/GeometryInputs.h"

#include "feme/Target/CPU/RuntimeABI.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace feme::cpu;
using namespace feme::graphics;

namespace {

// Three vertex-output slots (2 scalars each) assembled into a single
// triangle primitive (vertices 2, 0, 1, in that primitive order).
TEST(GeometryInputsTest, GathersPrimitiveVerticesInRequestedOrder) {
  std::vector<float> VertexOutputs = {
      /*slot 0=*/0.0f, 1.0f, /*slot 1=*/2.0f, 3.0f, /*slot 2=*/4.0f, 5.0f};
  std::vector<uint32_t> VertexSlots = {2, 0, 1};

  std::vector<float> Inputs =
      buildGeometryInputs(VertexSlots, VertexOutputs, /*ScalarsPerVertex=*/2);

  ASSERT_EQ(Inputs.size(), 6u);
  EXPECT_FLOAT_EQ(Inputs[0], 4.0f);
  EXPECT_FLOAT_EQ(Inputs[1], 5.0f);
  EXPECT_FLOAT_EQ(Inputs[2], 0.0f);
  EXPECT_FLOAT_EQ(Inputs[3], 1.0f);
  EXPECT_FLOAT_EQ(Inputs[4], 2.0f);
  EXPECT_FLOAT_EQ(Inputs[5], 3.0f);
}

// Two triangle primitives (6 geometry-input slots) gathered from a shared
// 4-vertex vertex-output batch, matching how a triangle strip's second
// primitive reuses two of its predecessor's vertices.
TEST(GeometryInputsTest, GathersMultiplePrimitivesFromSharedVertexBatch) {
  std::vector<float> VertexOutputs = {0.0f, 10.0f, 20.0f, 30.0f};
  std::vector<uint32_t> VertexSlots = {0, 1, 2, 3};

  std::vector<float> Inputs =
      buildGeometryInputs(VertexSlots, VertexOutputs, /*ScalarsPerVertex=*/1);

  ASSERT_EQ(Inputs.size(), 4u);
  EXPECT_FLOAT_EQ(Inputs[0], 0.0f);
  EXPECT_FLOAT_EQ(Inputs[3], 30.0f);
}

TEST(GeometryInputsTest, OutOfRangeSlotGathersAsZeroRatherThanReadingOob) {
  std::vector<float> VertexOutputs = {1.0f, 2.0f};
  std::vector<uint32_t> VertexSlots = {0, 5};

  std::vector<float> Inputs =
      buildGeometryInputs(VertexSlots, VertexOutputs, /*ScalarsPerVertex=*/2);

  ASSERT_EQ(Inputs.size(), 4u);
  EXPECT_FLOAT_EQ(Inputs[0], 1.0f);
  EXPECT_FLOAT_EQ(Inputs[1], 2.0f);
  EXPECT_FLOAT_EQ(Inputs[2], 0.0f);
  EXPECT_FLOAT_EQ(Inputs[3], 0.0f);
}

TEST(GeometryInputsTest, ZeroScalarsPerVertexProducesEmptyInputs) {
  std::vector<float> VertexOutputs = {1.0f, 2.0f};
  std::vector<uint32_t> VertexSlots = {0};

  EXPECT_TRUE(
      buildGeometryInputs(VertexSlots, VertexOutputs, /*ScalarsPerVertex=*/0)
          .empty());
}

TEST(GeometryInputsTest, BuildsOneInvocationPerPrimitiveIdInOrder) {
  std::vector<uint32_t> PrimitiveIDs = {5, 2, 9};

  std::vector<FemeGeometryInvocation> Invocations =
      buildGeometryInvocations(PrimitiveIDs);

  ASSERT_EQ(Invocations.size(), 3u);
  EXPECT_EQ(Invocations[0].PrimitiveID, 5u);
  EXPECT_EQ(Invocations[1].PrimitiveID, 2u);
  EXPECT_EQ(Invocations[2].PrimitiveID, 9u);
  // No invocation IDs supplied: every record defaults to gl_InvocationID 0,
  // matching a `layout(invocations = 1)` (or unspecified) shader.
  EXPECT_EQ(Invocations[0].InvocationID, 0u);
  EXPECT_EQ(Invocations[1].InvocationID, 0u);
  EXPECT_EQ(Invocations[2].InvocationID, 0u);
}

// (Roadmap H5d-a) Two primitives, each declaring two geometry-shader
// invocations: `PrimitiveIDs`/`InvocationIDs` pair up row-by-row, matching
// how `Executor.cpp` fans one real primitive out into
// `GeometryState::Invocations` ABI rows that all share the same
// `PrimitiveID` but carry a distinct `InvocationID`.
TEST(GeometryInputsTest, BuildsDistinctInvocationIDsForRepeatedPrimitives) {
  std::vector<uint32_t> PrimitiveIDs = {3, 3, 7, 7};
  std::vector<uint32_t> InvocationIDs = {0, 1, 0, 1};

  std::vector<FemeGeometryInvocation> Invocations =
      buildGeometryInvocations(PrimitiveIDs, InvocationIDs);

  ASSERT_EQ(Invocations.size(), 4u);
  EXPECT_EQ(Invocations[0].PrimitiveID, 3u);
  EXPECT_EQ(Invocations[0].InvocationID, 0u);
  EXPECT_EQ(Invocations[1].PrimitiveID, 3u);
  EXPECT_EQ(Invocations[1].InvocationID, 1u);
  EXPECT_EQ(Invocations[2].PrimitiveID, 7u);
  EXPECT_EQ(Invocations[2].InvocationID, 0u);
  EXPECT_EQ(Invocations[3].PrimitiveID, 7u);
  EXPECT_EQ(Invocations[3].InvocationID, 1u);
}

} // namespace
