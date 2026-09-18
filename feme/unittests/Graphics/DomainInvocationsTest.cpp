//===- DomainInvocationsTest.cpp - Tests for buildDomainInvocations ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/DomainInvocations.h"

#include "feme/Target/CPU/RuntimeABI.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace feme::cpu;
using namespace feme::graphics;

namespace {

TEST(DomainInvocationsTest, ConvertsEachPointInOrder) {
  TessellatedPatch Patch;
  Patch.Points = {{0.0f, 0.0f, 0.0f}, {0.25f, 0.5f, 0.0f}, {1.0f, 1.0f, 0.0f}};

  std::vector<FemeDomainInvocation> Invocations = buildDomainInvocations(Patch);

  ASSERT_EQ(Invocations.size(), 3u);
  EXPECT_FLOAT_EQ(Invocations[1].DomainLocation[0], 0.25f);
  EXPECT_FLOAT_EQ(Invocations[1].DomainLocation[1], 0.5f);
  EXPECT_FLOAT_EQ(Invocations[1].DomainLocation[2], 0.0f);
  EXPECT_FLOAT_EQ(Invocations[2].DomainLocation[0], 1.0f);
}

TEST(DomainInvocationsTest, PreservesTriangleBarycentricThirdComponent) {
  TessellatedPatch Patch;
  Patch.Points = {{0.2f, 0.3f, 0.5f}};

  std::vector<FemeDomainInvocation> Invocations = buildDomainInvocations(Patch);

  ASSERT_EQ(Invocations.size(), 1u);
  EXPECT_FLOAT_EQ(Invocations[0].DomainLocation[0], 0.2f);
  EXPECT_FLOAT_EQ(Invocations[0].DomainLocation[1], 0.3f);
  EXPECT_FLOAT_EQ(Invocations[0].DomainLocation[2], 0.5f);
}

TEST(DomainInvocationsTest, EmptyPatchProducesNoInvocations) {
  TessellatedPatch Patch;
  EXPECT_TRUE(buildDomainInvocations(Patch).empty());
}

TEST(DomainInvocationsTest, ReservedFieldIsZeroed) {
  TessellatedPatch Patch;
  Patch.Points = {{1.0f, 1.0f, 1.0f}};

  std::vector<FemeDomainInvocation> Invocations = buildDomainInvocations(Patch);

  ASSERT_EQ(Invocations.size(), 1u);
  for (uint32_t Reserved : Invocations[0].Reserved)
    EXPECT_EQ(Reserved, 0u);
}

TEST(DomainInvocationsTest, DefaultsPrimitiveIDToZero) {
  TessellatedPatch Patch;
  Patch.Points = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};

  std::vector<FemeDomainInvocation> Invocations = buildDomainInvocations(Patch);

  ASSERT_EQ(Invocations.size(), 2u);
  EXPECT_EQ(Invocations[0].PrimitiveID, 0u);
  EXPECT_EQ(Invocations[1].PrimitiveID, 0u);
  EXPECT_EQ(Invocations[0].ViewIndex, 0u);
  EXPECT_EQ(Invocations[1].ViewIndex, 0u);
}

TEST(DomainInvocationsTest, BroadcastsPrimitiveIDToEveryPoint) {
  // (Roadmap L81) `SV_PrimitiveID` is uniform across an entire patch, so
  // every domain point generated for one patch must carry the same
  // `PrimitiveID`, matching how `PatchPipeline.cpp` calls this function once
  // per patch with that patch's own index.
  TessellatedPatch Patch;
  Patch.Points = {{0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 0.0f}};

  std::vector<FemeDomainInvocation> Invocations =
      buildDomainInvocations(Patch, /*PrimitiveID=*/7);

  ASSERT_EQ(Invocations.size(), 3u);
  for (const FemeDomainInvocation &Invocation : Invocations)
    EXPECT_EQ(Invocation.PrimitiveID, 7u);
}

/// (Roadmap H51/L109) `gl_ViewIndex` is likewise uniform across an entire
/// patch (this draw's own view, not something the tessellator generates
/// per point), so it must be broadcast to every domain point the same way
/// `PrimitiveID` is above.
TEST(DomainInvocationsTest, BroadcastsViewIndexToEveryPoint) {
  TessellatedPatch Patch;
  Patch.Points = {{0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 0.0f}};

  std::vector<FemeDomainInvocation> Invocations =
      buildDomainInvocations(Patch, /*PrimitiveID=*/7, /*ViewIndex=*/2);

  ASSERT_EQ(Invocations.size(), 3u);
  for (const FemeDomainInvocation &Invocation : Invocations) {
    EXPECT_EQ(Invocation.PrimitiveID, 7u);
    EXPECT_EQ(Invocation.ViewIndex, 2u);
  }
}

} // namespace
