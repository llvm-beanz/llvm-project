//===- MeshletTest.cpp - Tests for assembleMeshlet -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Meshlet.h"

#include "llvm/Support/Error.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme::graphics;

namespace {

TEST(MeshletTest, AssemblesOnlyTheDeclaredActualCounts) {
  MeshOutputBuilder Builder(MeshOutputTopology::Triangles,
                            /*MaxVertices=*/8, /*MaxPrimitives=*/4);
  ASSERT_TRUE(Builder.setOutputCounts(3, 1));
  ASSERT_TRUE(Builder.setVertex(0, {0.0f, 0.0f}));
  ASSERT_TRUE(Builder.setVertex(1, {1.0f, 0.0f}));
  ASSERT_TRUE(Builder.setVertex(2, {0.0f, 1.0f}));
  ASSERT_TRUE(Builder.setPrimitive(0, {42.0f}));
  ASSERT_TRUE(Builder.setPrimitiveIndices(0, {0, 1, 2}));

  llvm::Expected<Meshlet> Result = assembleMeshlet(Builder);
  ASSERT_THAT_EXPECTED(Result, llvm::Succeeded());

  EXPECT_EQ(Result->getTopology(), MeshOutputTopology::Triangles);
  // Trimmed to the declared actual counts, not `MaxVertices`/`MaxPrimitives`.
  ASSERT_EQ(Result->getVertexCount(), 3u);
  ASSERT_EQ(Result->getPrimitiveCount(), 1u);
  EXPECT_EQ(Result->getVertices()[1], (MeshOutputRow{1.0f, 0.0f}));
  EXPECT_EQ(Result->getPrimitives()[0], (MeshOutputRow{42.0f}));
  EXPECT_EQ(Result->getPrimitiveIndices(0),
            (llvm::ArrayRef<uint32_t>{0u, 1u, 2u}));
}

TEST(MeshletTest, EmptyOutputAssemblesToAnEmptyMeshlet) {
  MeshOutputBuilder Builder(MeshOutputTopology::Points, 4, 2);
  // `setOutputCounts` never called: this workgroup emitted nothing.
  llvm::Expected<Meshlet> Result = assembleMeshlet(Builder);
  ASSERT_THAT_EXPECTED(Result, llvm::Succeeded());
  EXPECT_EQ(Result->getVertexCount(), 0u);
  EXPECT_EQ(Result->getPrimitiveCount(), 0u);
}

TEST(MeshletTest, DiagnosesAnOutOfRangeVertexIndexRatherThanReadingOOB) {
  // `MeshOutputBuilder::setPrimitiveIndices` already rejects an
  // out-of-range vertex index at write time; forge a builder whose stored
  // index list is out of range anyway (mirroring what a real compiled
  // workgroup's raw output could contain once wired directly, roadmap
  // H6c-a-a, bypassing that checked setter) by writing indices while the
  // declared vertex count is still large, then shrinking it.
  MeshOutputBuilder Builder(MeshOutputTopology::Lines, 4, 1);
  ASSERT_TRUE(Builder.setOutputCounts(4, 1));
  ASSERT_TRUE(Builder.setPrimitiveIndices(0, {1, 3}));
  // Re-declare a smaller actual vertex count: index 3 is now out of range
  // for this meshlet even though the builder's storage still holds it.
  ASSERT_TRUE(Builder.setOutputCounts(2, 1));

  llvm::Expected<Meshlet> Result = assembleMeshlet(Builder);
  ASSERT_FALSE((bool)Result);
  std::string Message = llvm::toString(Result.takeError());
  EXPECT_NE(Message.find("out-of-range"), std::string::npos);
}

} // namespace
