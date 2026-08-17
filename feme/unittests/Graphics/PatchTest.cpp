//===- PatchTest.cpp - Tests for feme::graphics::PatchRecord -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Patch.h"

#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace feme::graphics;

namespace {

TEST(PatchTest, ControlPointReadWriteRoundTrips) {
  PatchRecord Patch(/*InputControlPointCount=*/3, /*OutputControlPointCount=*/
                    4, /*InputControlPointScalarCount=*/8,
                    /*ControlPointScalarCount=*/8,
                    /*PatchConstantScalarCount=*/2);
  EXPECT_EQ(Patch.getInputControlPointCount(), 3u);
  EXPECT_EQ(Patch.getOutputControlPointCount(), 4u);

  EXPECT_TRUE(Patch.writeControlPoint(2, 5, 3.5f));
  EXPECT_FLOAT_EQ(Patch.readControlPoint(2, 5), 3.5f);
  // Every other (control point, scalar) pair defaults to zero.
  EXPECT_FLOAT_EQ(Patch.readControlPoint(0, 0), 0.0f);
}

TEST(PatchTest, ControlPointWriteRejectsOutOfRangeIndices) {
  PatchRecord Patch(1, 2, 0, 4, 0);
  EXPECT_FALSE(Patch.writeControlPoint(/*ControlPoint=*/2, 0, 1.0f));
  EXPECT_FALSE(Patch.writeControlPoint(0, /*ScalarIndex=*/4, 1.0f));
  // Rejected writes leave storage untouched (still reads back as zero).
  EXPECT_FLOAT_EQ(Patch.readControlPoint(0, 0), 0.0f);
}

TEST(PatchTest, InputControlPointReadWriteRoundTrips) {
  PatchRecord Patch(/*InputControlPointCount=*/2, /*OutputControlPointCount=*/
                    3, /*InputControlPointScalarCount=*/4,
                    /*ControlPointScalarCount=*/0,
                    /*PatchConstantScalarCount=*/0);
  EXPECT_TRUE(Patch.writeInputControlPoint(1, 2, 9.5f));
  EXPECT_FLOAT_EQ(Patch.readInputControlPoint(1, 2), 9.5f);
  // Every other (control point, scalar) pair defaults to zero, and the
  // input storage is independent of the (empty here) output storage.
  EXPECT_FLOAT_EQ(Patch.readInputControlPoint(0, 0), 0.0f);
}

TEST(PatchTest, InputControlPointWriteRejectsOutOfRangeIndices) {
  PatchRecord Patch(/*InputControlPointCount=*/2, /*OutputControlPointCount=*/
                    1, /*InputControlPointScalarCount=*/4,
                    /*ControlPointScalarCount=*/0,
                    /*PatchConstantScalarCount=*/0);
  EXPECT_FALSE(Patch.writeInputControlPoint(/*ControlPoint=*/2, 0, 1.0f));
  EXPECT_FALSE(Patch.writeInputControlPoint(0, /*ScalarIndex=*/4, 1.0f));
  EXPECT_FLOAT_EQ(Patch.readInputControlPoint(0, 0), 0.0f);
}

TEST(PatchTest, PatchConstantReadWriteRoundTrips) {
  PatchRecord Patch(1, 1, 0, 0, 6);
  EXPECT_TRUE(Patch.writePatchConstant(3, 7.0f));
  EXPECT_FLOAT_EQ(Patch.readPatchConstant(3), 7.0f);
  EXPECT_FALSE(Patch.writePatchConstant(6, 1.0f));
}

TEST(PatchTest, FactorsAndTessellatorStateAreMutable) {
  PatchRecord Patch(1, 1, 0, 0, 0);
  Patch.Domain = TessellatorDomain::Quad;
  Patch.Partitioning = TessPartitioning::FractionalOdd;
  Patch.OutputPrimitive = TessOutputPrimitive::Point;
  Patch.getFactors().Inside = {3.0f, 3.0f};
  EXPECT_EQ(Patch.Domain, TessellatorDomain::Quad);
  EXPECT_FLOAT_EQ(Patch.getFactors().Inside[0], 3.0f);
}

TEST(PatchTest, ValidateControlPointCountsAcceptsInRangeCounts) {
  EXPECT_TRUE(validatePatchControlPointCounts(1, 1));
  EXPECT_TRUE(validatePatchControlPointCounts(3, MaxPatchControlPoints));
}

TEST(PatchTest, ValidateControlPointCountsRejectsZero) {
  std::string Err;
  llvm::raw_string_ostream OS(Err);
  EXPECT_FALSE(validatePatchControlPointCounts(0, 1, &OS));
  EXPECT_NE(Err.find("input"), std::string::npos);
}

TEST(PatchTest, ValidateControlPointCountsRejectsTooLarge) {
  EXPECT_FALSE(validatePatchControlPointCounts(1, MaxPatchControlPoints + 1));
}

} // namespace
