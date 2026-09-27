//===- Tessellator.h - Fixed-function tessellator state/generation -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::graphics::tessellate, the API-neutral
// fixed-function tessellator "Tessellation and geometry stage model" in
// feme/docs/FeMeGraphicsDesign.md describes: given the normalized domain,
// partitioning/spacing, winding, point-mode, and tessellation-factor state a
// compiled hull/control stage produces, it generates the domain coordinates
// and primitive connectivity the domain/evaluation stage runs over.
//
// This is roadmap R34's "tessellator state and domain-coordinate
// generation": a pure, host-side data transform with no compiled-shader
// dependency, so it is exercised directly by unittests/Graphics/
// TessellatorTest.cpp against the analytic properties the design's G5
// completion test asks for (point/primitive counts, coordinate ranges, no
// gaps) rather than only through image comparison.
//
// Crack-free non-uniform per-edge tessellation: the triangle and quad
// domains place their boundary vertices from each edge's own outer factor
// (so two adjacent patches that agree on a shared edge's factor produce
// identical vertices along it, regardless of their other edges' or their
// interior's factors), then bridge that boundary ring to an interior "core"
// -- inset strictly inside the boundary, hence never itself a cross-patch
// cracking concern -- with a standard concentric-ring triangulation that
// walks both rings by proportional arc length, always advancing whichever
// ring's next vertex comes first (see `bridgeRingsByEdge` in
// Tessellator.cpp).
//
// (Roadmap L220) The triangle domain's own interior is no longer a single
// uniformly-subdivided core: `tessellateTriangle` now generates the exact
// interior point *set* the Vulkan/GLSL spec's own "Triangle Tessellation"
// concentric-ring algorithm requires -- a recursion of successively-
// shrinking equilateral rings, each one's own per-edge segment count
// exactly 2 less than the previous, terminating at either an
// un-subdivided single triangle (segment count 3) or a degenerate single
// point at the centroid (segment count 2, fanned from the previous ring's
// own boundary vertices). Each ring reduces to a closed-form homothety
// (uniform scale toward the barycentric centroid) composed multiplicatively
// across the recursion, rather than real per-ring perpendicular-projection
// geometry (see `appendTriangleRingBoundary`/`fanRingToPoint` and
// `tessellateTriangle`'s own comments in Tessellator.cpp for the full
// derivation). `bridgeRingsByEdge` is reused unchanged for every ring-to-
// ring annulus fill.
//
// (Roadmap L221) The quad domain's own interior is likewise no longer a
// single uniformly-inset lattice: `tessellateQuad` now generates the exact
// interior point set the Vulkan/GLSL spec's own "Quad Tessellation"
// algorithm requires -- a regular `(N - 1) x (M - 1)` grid (`M`/`N` the
// clamped, epsilon-bumped first/second inner tessellation levels, governing
// the v-axis/u-axis segment counts respectively), of which only the cells
// not adjacent to an outer edge are triangulated directly, with the
// remaining annulus bridged to the real, independently-subdivided outer
// boundary exactly as the triangle domain's own rings are. When either axis
// clamps to exactly 2 segments the interior grid degenerates to a single
// line of points (one axis fixed at 0.5); when both do, it degenerates to
// the single center point `(0.5, 0.5)`, each handled as its own explicit
// case (see `tessellateQuad`'s own comments in Tessellator.cpp for the
// full derivation, including the `bridgeEdge`/`fanEdgeToPoint` single-edge
// helpers factored out of `bridgeRingsByEdge`/`fanRingToPoint` so the
// degenerate cases' own non-ring-shaped interior can still reuse the same
// per-edge bridging/fanning logic).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_TESSELLATOR_H
#define FEME_GRAPHICS_TESSELLATOR_H

#include <array>
#include <cstdint>
#include <vector>

namespace feme::graphics {

/// The tessellator domain a patch declares, matching DXIL's
/// `hlsl.tessellation.domain`/SPIR-V's `ExecutionMode` triangle/quad/isoline
/// selection.
enum class TessellatorDomain : uint8_t {
  Isoline,
  Triangle,
  Quad,
};

/// How a (possibly fractional) tessellation factor is rounded into an
/// integer segment count, matching DXIL's `hlsl.tessellation.partitioning`/
/// SPIR-V's `SpacingEqual`/`SpacingFractionalOdd`/`SpacingFractionalEven`
/// execution modes (`Pow2` is Direct3D-only: SPIR-V has no equivalent, so a
/// frontend importing it from DXIL must be prepared for
/// `feme::graphics::ValidateStagePass`-style rejection on a SPIR-V target
/// that cannot express it -- see the file comment's "reject rather than
/// approximate" note).
enum class TessPartitioning : uint8_t {
  Integer,
  Pow2,
  FractionalOdd,
  FractionalEven,
};

/// The connectivity the tessellator emits, matching DXIL's
/// `hlsl.tessellation.outputPrimitive`/SPIR-V's `PointMode`/
/// `VertexOrderCw`/`VertexOrderCcw` execution modes. `Point` is the
/// point-mode override (legal for any domain); `Line` is isoline's own
/// natural output; `TriangleCw`/`TriangleCcw` are triangle/quad's natural
/// output, distinguished by winding.
enum class TessOutputPrimitive : uint8_t {
  Point,
  Line,
  TriangleCw,
  TriangleCcw,
};

/// The default cap on a rounded segment count, matching Direct3D/Vulkan's
/// shared `MaxTessFactor` (64) query limit. Callers may pass a smaller
/// device-reported limit to `tessellate`.
constexpr uint32_t DefaultMaxTessFactor = 64;

/// A patch's outer (edge) and inner tessellation factors, produced by the
/// control/patch-constant stage as `SignatureSystemValue::TessFactorEdge`/
/// `TessFactorInside` outputs. Which entries a given `TessellatorDomain`
/// reads:
///
///  - `Isoline`: `Edges[0]` is line density (how many copies of the line),
///    `Edges[1]` is per-line detail (segments along each line); `Inside` is
///    unused.
///  - `Triangle`: `Edges[0..2]` are the three edge factors (edge `i` is
///    opposite input control point `i`); `Inside[0]` is the interior
///    factor; `Inside[1]` is unused.
///  - `Quad`: `Edges[0..3]` are the four edge factors (`0`/`2` are the
///    `u == 0`/`u == 1` edges, `1`/`3` are the `v == 0`/`v == 1` edges);
///    `Inside[0]`/`Inside[1]` are the `u`/`v` interior factors.
///
/// An *outer edge* factor `<= 0` culls the whole patch (produces no
/// output), matching both APIs' degenerate-patch rule; an *inside* factor
/// `<= 0` never does (Roadmap L224) -- it is simply clamped up to `1`
/// like any other out-of-range factor.
struct TessFactors {
  std::array<float, 4> Edges = {1.0f, 1.0f, 1.0f, 1.0f};
  std::array<float, 2> Inside = {1.0f, 1.0f};
};

/// One generated domain coordinate: a triangle domain uses all three
/// (barycentric, `U + V + W == 1`); isoline/quad use only `U`/`V`. (Roadmap
/// L24(b)) For `Isoline`, `U`/`V` are not an arbitrary internal ordering --
/// they must match the real `SV_DomainLocation`/`gl_TessCoord` convention
/// exactly, since `feme::graphics::buildDomainInvocations` copies them
/// straight into the domain shader's own coordinate operand with no
/// per-domain remapping: `U` is the position *along* one line (varies
/// continuously across the line's own segments, `[0, 1]`), and `V` is
/// *which* line out of a patch's own `Edges[0]`-many lines this point
/// belongs to (a discrete value in `[0, 1)`) -- confirmed against the real
/// D3D11 `SV_DomainLocation` documentation ("u ... varies along the length
/// of a single line ... v ... selects which isoline"). See
/// `tessellateIsoline`'s own comment for how the "density"/"detail" factor
/// terminology maps onto this `U`/`V` split.
struct DomainPoint {
  float U = 0.0f;
  float V = 0.0f;
  float W = 0.0f;
};

/// The tessellator's output for one patch: generated domain coordinates
/// plus primitive connectivity indexing them, per `TessOutputPrimitive`
/// (unindexed for `Point`, a line list for `Line`, a triangle list for
/// `TriangleCw`/`TriangleCcw`).
struct TessellatedPatch {
  std::vector<DomainPoint> Points;
  std::vector<uint32_t> Indices;
};

/// Rounds \p Factor into an integer segment count per \p Partitioning's
/// rule (each first clamps \p Factor to its own range, per the Vulkan/GLSL
/// spec's "Tessellator Spacing" section):
///
///  - `Integer`: \p Factor clamped to `[1, MaxTessFactor]`, then the
///    ceiling.
///  - `Pow2`: \p Factor clamped to `[1, MaxTessFactor]`, then the smallest
///    power of two at least the ceiling.
///  - `FractionalOdd`: \p Factor clamped to `[1, MaxTessFactor]`, then the
///    smallest odd integer at least the ceiling (1 when `Factor <= 1`).
///  - `FractionalEven`: \p Factor clamped to `[2, MaxTessFactor]` -- a
///    stricter minimum than every other mode's own `[1, MaxTessFactor]`,
///    per spec -- then the smallest even integer at least the ceiling
///    (always `>= 2`; `FractionalEven` can never collapse an edge to an
///    un-subdivided single point the way `Integer`/`FractionalOdd` do at
///    `Factor <= 1`).
///
/// This is FeMe's own normalized rounding rule (see the file comment's
/// scope note): it matches each partitioning mode's qualitative shape --
/// monotonic in \p Factor, odd/even/power-of-two segment counts -- rather
/// than reproducing either API's exact fractional vertex placement.
uint32_t computeSegmentCount(float Factor, TessPartitioning Partitioning,
                             uint32_t MaxTessFactor = DefaultMaxTessFactor);

/// Generates domain coordinates and connectivity for one patch, per the
/// file comment's crack-free per-edge tessellation note. Returns an empty
/// `TessellatedPatch` (no points, no indices) when any factor `Factors`
/// reads for \p Domain is `<= 0`, per `TessFactors`'s own comment.
TessellatedPatch tessellate(TessellatorDomain Domain,
                            TessPartitioning Partitioning,
                            TessOutputPrimitive OutputPrimitive,
                            const TessFactors &Factors,
                            uint32_t MaxTessFactor = DefaultMaxTessFactor);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_TESSELLATOR_H
