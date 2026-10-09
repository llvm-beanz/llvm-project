//===- TriangleRasterizationTest.cpp - Fixed-point coverage tests --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/TriangleRasterization.h"

#include "gtest/gtest.h"

using namespace feme::graphics;

namespace {

constexpr unsigned Bits = 4; // Matches feme::graphics::LineSubPixelPrecisionBits.

/// Mirrors `Executor.cpp`'s own top-left tie-break rule
/// (`isTopLeftEdge`/`In0`/`In1`/`In2` in the triangle coverage test): a
/// sample is inside a directed edge `A -> B` of a positively-wound triangle
/// when the edge value is strictly positive, or exactly zero on a "top" or
/// "left" edge.
bool isTopLeftEdge(std::array<float, 2> A, std::array<float, 2> B) {
  float Dy = B[1] - A[1];
  float Dx = B[0] - A[0];
  return (Dy == 0.0f && Dx < 0.0f) || Dy > 0.0f;
}

bool isInside(std::array<float, 2> V0, std::array<float, 2> V1,
             std::array<float, 2> V2, std::array<float, 2> P, unsigned Bits) {
  int64_t E0 = fixedPointEdgeFn(V1, V2, P, Bits);
  int64_t E1 = fixedPointEdgeFn(V2, V0, P, Bits);
  int64_t E2 = fixedPointEdgeFn(V0, V1, P, Bits);
  bool In0 = E0 > 0 || (E0 == 0 && isTopLeftEdge(V1, V2));
  bool In1 = E1 > 0 || (E1 == 0 && isTopLeftEdge(V2, V0));
  bool In2 = E2 > 0 || (E2 == 0 && isTopLeftEdge(V0, V1));
  return In0 && In1 && In2;
}

/// An axis-aligned right triangle, well away from any edge: the pixel
/// center deep inside it is covered, one well outside is not, in both
/// cases unambiguously (no near-tie, this just confirms the basic sign
/// convention and winding assumption are right).
TEST(TriangleRasterizationTest, AxisAlignedTriangleCoversItsOwnInterior) {
  // Ordered so that edgeFn(V0, V1, V2) (the triangle's own signed area) is
  // positive, matching the coverage test's own precondition that every
  // triangle reaching it has already been reordered to positive winding.
  std::array<float, 2> V0{0.0f, 0.0f};
  std::array<float, 2> V1{0.0f, 10.0f};
  std::array<float, 2> V2{10.0f, 0.0f};

  EXPECT_TRUE(isInside(V0, V1, V2, {2.5f, 2.5f}, Bits));
  EXPECT_FALSE(isInside(V0, V1, V2, {8.5f, 8.5f}, Bits));
}

/// The top-left rule gives a vertical shared edge exactly one owner: the
/// left triangle's own right edge is inclusive (a "left" edge, interior to
/// its right), the right triangle's own left edge is exclusive (a "right"
/// edge) -- so a sample sitting exactly on the shared boundary belongs to
/// the left triangle alone, with no overlap and no gap.
TEST(TriangleRasterizationTest, TopLeftRuleGivesASharedVerticalEdgeOneOwner) {
  // Two positively-wound triangles splitting an 8x4 rectangle along its
  // own vertical midline (x=4). The right triangle's own edge R0->R1
  // (the shared line, traversed top-to-bottom... i.e. Dy>0) is a "left"
  // edge and so inclusive; the left triangle's edge L1->L2 along the same
  // physical line is traversed the other way (Dy<0) and so is a "right"
  // edge, exclusive. A sample sitting exactly on the shared boundary thus
  // belongs to the right triangle alone -- no overlap, no gap.
  std::array<float, 2> L0{0.0f, 0.0f}, L1{4.0f, 4.0f}, L2{4.0f, 0.0f};
  std::array<float, 2> R0{4.0f, 0.0f}, R1{4.0f, 4.0f}, R2{8.0f, 0.0f};

  std::array<float, 2> OnSharedEdge{4.0f, 2.0f};
  EXPECT_FALSE(isInside(L0, L1, L2, OnSharedEdge, Bits));
  EXPECT_TRUE(isInside(R0, R1, R2, OnSharedEdge, Bits));
}

/// Roadmap L402's own near-tie case: triangle 24's exact clip-space
/// vertices from `dEQP-VK.{renderpass1,renderpass2}.suballocation.
/// subpass_dependencies.late_fragment_tests.render_size_32_32.
/// subpass_count_2.d24_unorm_s8_uint` (CTS's own fixed-seed-5 `deRandom`
/// vertex generator, replicated standalone to recover these), converted to
/// this case's 32x32 screen space. At full `double` precision
/// (`Executor.cpp`'s pre-L406 `edgeFnD`) pixel (8, 0)'s center lands
/// *inside* this triangle by roughly 0.018 pixels along edge V0->V1 -- a
/// genuine near-tie that VK-GL-CTS's own reference rasterizer (which
/// snaps every vertex to this same 4-bit fixed-point grid before its own
/// edge test) resolves the other way, confirmed against the actual
/// Reference/Result images from a captured CTS run (see roadmap L402).
/// This is the regression case L406 exists to fix: the fixed-point
/// primitive must agree with the reference and say "not covered".
TEST(TriangleRasterizationTest, L402NearTieMatchesReferenceRasterizer) {
  std::array<float, 2> V0{18.530607223510742f, 0.9789028167724609f};
  std::array<float, 2> V1{7.186938285827637f, 0.41660499572753906f};
  std::array<float, 2> V2{24.08245849609375f, 13.12424087524414f};
  std::array<float, 2> PixelCenter{8.5f, 0.5f};

  EXPECT_FALSE(isInside(V0, V1, V2, PixelCenter, Bits));
}

} // namespace
