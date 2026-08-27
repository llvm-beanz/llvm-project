//===- Executor.cpp - FeMe software graphics executor --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the "Draw flow" Executor.h describes. Roadmap R32 ("Basic
// triangle pipeline") scoped this to one triangle-list/triangle-strip draw
// and one viewport/scissor; roadmap H3 lifts the singleton viewport/scissor
// part to array state selected per primitive. The scope decisions this file
// still makes -- each
// deliberately deferred to a later roadmap step rather than silently
// approximated -- are:
//
//  - No post-transform vertex cache: every (instance, vertex-or-index) pair
//    re-runs the vertex stage, matching "the first implementation may
//    perform all vertex work before tile work" in "Draw flow".
//  - Depth/stencil testing/writes (roadmap R33; combined-format support
//    added by roadmap C1) support `D16_UNORM`/`D32_FLOAT` depth,
//    `S8_UINT` stencil, and `D24_UNORM_S8_UINT` combined depth+stencil
//    attachments -- the last shares one word of storage between
//    `PreparedDraw.h`'s `DepthStencilAttachment::Depth` and `::Stencil`
//    views, so each is a read-modify-write of only its own bits -- with
//    early or late scheduling chosen from the fragment stage's own
//    `SV_Depth`/`SV_StencilRef`/discard reflection. Full blend-factor/op
//    combinations, write masks (per `BlendState`), logic ops
//    (`R8G8B8A8_*` only), multiple render targets (one `BlendState`/
//    `SV_TargetN` per color attachment,
//    `GraphicsPipeline::getColorBlends()`), and 1/2/4-sample multisampling
//    (coverage tested at fixed per-sample offsets, "Fixed per-pixel sample
//    offsets" below; shading, depth, and stencil interpolation stay
//    per-pixel, not per-sample -- a documented precision scope decision,
//    not a correctness gap in the coverage/resolve behavior a completion
//    test observes) are implemented. Depth/stencil resolve and 8+ sample
//    counts are mechanical, on-demand additions to this same shape.
//  - Vertex/fragment stage elements are 32-bit scalars only, per component
//    (`BitWidth == 32`); a matrix/array-typed varying or `Output` element
//    (`RowCount > 1`, roadmap C8) is supported for the vertex-output ->
//    fragment-input path (linked, interpolated, and stored/read one row at
//    a time -- see `LinkedVarying::RowCount` and `StageStorage::readRaw`/
//    `writeRaw`'s `Row` parameter). A matrix *vertex attribute* (bound from
//    a vertex buffer, not a varying) and 16-/64-bit varyings remain a
//    mechanical, on-demand addition once a test needs them.
//  - A non-`Position` varying whose `SignatureComponentType` is not `Float`
//    is passed through unmodified from the first vertex of the (possibly
//    clipped) triangle actually rasterized, rather than tracking each
//    original mesh vertex's provoking-vertex identity through clipping --
//    the provoking-vertex convention "Normalized pipeline" in
//    feme/docs/FeMeGraphicsDesign.md notes is not modelled yet.
//  - Vertex attribute and color-output formats are the subset "Texture
//    layout and formats" in feme/docs/FeMeGraphicsDesign.md already
//    implements (the 32-bit-per-component family and `R8G8B8A8_*`); other
//    formats are a mechanical, on-demand addition to `decodeAttribute`.
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Executor.h"

#include "feme/Core/Signature.h"
#include "feme/Graphics/Geometry.h"
#include "feme/Graphics/GeometryInputs.h"
#include "feme/Graphics/GeometryStream.h"
#include "feme/Graphics/GeometryStreamCollection.h"
#include "feme/Graphics/ImageFixture.h"
#include "feme/Graphics/LayeredRendering.h"
#include "feme/Graphics/Pipeline.h"
#include "feme/Graphics/PreparedDraw.h"
#include "feme/Graphics/StageLink.h"
#include "feme/Graphics/StageStorage.h"
#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/CPU/ResourceHeap.h"
#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <thread>
#include <vector>

using namespace llvm;

namespace feme::graphics {

namespace {

int32_t readSignedStageValue(const StageStorage &Storage,
                             const SignatureElement &Elt, uint32_t Invocation) {
  uint32_t Bits =
      Storage.readRaw(Elt.ElementID, Elt.FirstComponent, Invocation);
  int32_t Value;
  memcpy(&Value, &Bits, sizeof(Value));
  return Value;
}

AttachmentView sliceAttachmentLayer(const AttachmentView &View,
                                    uint32_t Layer) {
  if (View.Data.empty() || View.ArrayLayers <= 1)
    return View;
  uint64_t LayerSizeBytes = View.Data.size() / View.ArrayLayers;
  uint64_t LayerOffset = getAttachmentLayerByteOffset(Layer, LayerSizeBytes);
  AttachmentView Sliced = View;
  Sliced.Data = View.Data.slice(LayerOffset, LayerSizeBytes);
  Sliced.ArrayLayers = 1;
  return Sliced;
}

uint32_t getDrawLayerCount(const PreparedDraw &Draw) {
  for (const AttachmentView &Attachment : Draw.Attachments)
    if (!Attachment.Data.empty())
      return Attachment.ArrayLayers;
  if (!Draw.DepthStencil.Depth.Data.empty())
    return Draw.DepthStencil.Depth.ArrayLayers;
  if (!Draw.DepthStencil.Stencil.Data.empty())
    return Draw.DepthStencil.Stencil.ArrayLayers;
  return 1;
}

//===----------------------------------------------------------------------===//
// Vertex attribute fetch
//===----------------------------------------------------------------------===//

/// Decodes up to 4 components of \p Format from \p Src into \p Out as
/// 32-bit words matching \p WantType's storage convention (an IEEE-754 bit
/// pattern for `Float`, a sign/zero-extended 32-bit integer otherwise). See
/// the file comment above for the supported format subset.
Error decodeAttribute(cpu::ResourceFormat Format, const uint8_t *Src,
                      uint32_t WantComponents, SignatureComponentType WantType,
                      std::array<uint32_t, 4> &Out) {
  auto putFloat = [&](uint32_t I, float F) {
    memcpy(&Out[I], &F, sizeof(float));
  };

  switch (Format) {
  case cpu::ResourceFormat::R32_FLOAT:
  case cpu::ResourceFormat::R32G32_FLOAT:
  case cpu::ResourceFormat::R32G32B32_FLOAT:
  case cpu::ResourceFormat::R32G32B32A32_FLOAT: {
    if (WantType != SignatureComponentType::Float)
      return createStringError(inconvertibleErrorCode(),
                               "vertex attribute format is floating-point "
                               "but the shader input is not");
    for (uint32_t I = 0; I != WantComponents; ++I)
      memcpy(&Out[I], Src + I * 4, 4);
    return Error::success();
  }
  case cpu::ResourceFormat::R32_UINT:
  case cpu::ResourceFormat::R32G32_UINT:
  case cpu::ResourceFormat::R32G32B32_UINT:
  case cpu::ResourceFormat::R32G32B32A32_UINT: {
    if (WantType != SignatureComponentType::UInt)
      return createStringError(inconvertibleErrorCode(),
                               "vertex attribute format is UInt but the "
                               "shader input is not");
    for (uint32_t I = 0; I != WantComponents; ++I)
      memcpy(&Out[I], Src + I * 4, 4);
    return Error::success();
  }
  case cpu::ResourceFormat::R32_SINT:
  case cpu::ResourceFormat::R32G32_SINT:
  case cpu::ResourceFormat::R32G32B32_SINT:
  case cpu::ResourceFormat::R32G32B32A32_SINT: {
    if (WantType != SignatureComponentType::SInt)
      return createStringError(inconvertibleErrorCode(),
                               "vertex attribute format is SInt but the "
                               "shader input is not");
    for (uint32_t I = 0; I != WantComponents; ++I)
      memcpy(&Out[I], Src + I * 4, 4);
    return Error::success();
  }
  case cpu::ResourceFormat::R8G8B8A8_UNORM:
  case cpu::ResourceFormat::R8G8B8A8_UNORM_SRGB: {
    if (WantType != SignatureComponentType::Float)
      return createStringError(inconvertibleErrorCode(),
                               "R8G8B8A8_UNORM vertex attribute requires a "
                               "floating-point shader input");
    for (uint32_t I = 0; I != WantComponents; ++I)
      putFloat(I, Src[I] / 255.0f);
    return Error::success();
  }
  case cpu::ResourceFormat::R8G8B8A8_SNORM: {
    if (WantType != SignatureComponentType::Float)
      return createStringError(inconvertibleErrorCode(),
                               "R8G8B8A8_SNORM vertex attribute requires a "
                               "floating-point shader input");
    for (uint32_t I = 0; I != WantComponents; ++I)
      putFloat(I,
               std::clamp(static_cast<int8_t>(Src[I]) / 127.0f, -1.0f, 1.0f));
    return Error::success();
  }
  case cpu::ResourceFormat::R8G8B8A8_UINT: {
    if (WantType != SignatureComponentType::UInt)
      return createStringError(inconvertibleErrorCode(),
                               "R8G8B8A8_UINT vertex attribute requires a "
                               "UInt shader input");
    for (uint32_t I = 0; I != WantComponents; ++I)
      Out[I] = Src[I];
    return Error::success();
  }
  case cpu::ResourceFormat::R8G8B8A8_SINT: {
    if (WantType != SignatureComponentType::SInt)
      return createStringError(inconvertibleErrorCode(),
                               "R8G8B8A8_SINT vertex attribute requires a "
                               "SInt shader input");
    for (uint32_t I = 0; I != WantComponents; ++I)
      Out[I] = static_cast<uint32_t>(
          static_cast<int32_t>(static_cast<int8_t>(Src[I])));
    return Error::success();
  }
  default:
    return createStringError(inconvertibleErrorCode(),
                             "vertex attribute format is not yet supported "
                             "(mechanical, added on demand)");
  }
}

/// The per-component byte size of \p Format in the vertex-attribute decode
/// table above (distinct from ImageFixture.cpp's texel-encoding table: this
/// one describes what `decodeAttribute` reads, not how a fixture stores a
/// full texel).
Expected<uint32_t> attributeComponentByteSize(cpu::ResourceFormat Format) {
  switch (Format) {
  case cpu::ResourceFormat::R32_FLOAT:
  case cpu::ResourceFormat::R32G32_FLOAT:
  case cpu::ResourceFormat::R32G32B32_FLOAT:
  case cpu::ResourceFormat::R32G32B32A32_FLOAT:
  case cpu::ResourceFormat::R32_UINT:
  case cpu::ResourceFormat::R32G32_UINT:
  case cpu::ResourceFormat::R32G32B32_UINT:
  case cpu::ResourceFormat::R32G32B32A32_UINT:
  case cpu::ResourceFormat::R32_SINT:
  case cpu::ResourceFormat::R32G32_SINT:
  case cpu::ResourceFormat::R32G32B32_SINT:
  case cpu::ResourceFormat::R32G32B32A32_SINT:
    return 4;
  case cpu::ResourceFormat::R8G8B8A8_UNORM:
  case cpu::ResourceFormat::R8G8B8A8_UNORM_SRGB:
  case cpu::ResourceFormat::R8G8B8A8_SNORM:
  case cpu::ResourceFormat::R8G8B8A8_UINT:
  case cpu::ResourceFormat::R8G8B8A8_SINT:
    return 1;
  default:
    return createStringError(inconvertibleErrorCode(),
                             "vertex attribute format is not yet supported "
                             "(mechanical, added on demand)");
  }
}

//===----------------------------------------------------------------------===//
// Rasterizer-side data model
//===----------------------------------------------------------------------===//

/// One matched vertex-output/fragment-input varying, linked by location
/// (`SignatureElement::Location`, "Normalized pipeline"'s Vulkan-style
/// linkage) since no `StageInterfaceMap` exists yet.
struct LinkedVarying {
  uint32_t VSElementID;
  uint32_t FSElementID;
  uint32_t ComponentCount;
  uint32_t RowCount;
  SignatureComponentType ComponentType;
  SignatureInterpolationMode Interpolation;
};

/// One post-vertex-shader vertex: clip-space position plus every linked
/// varying's raw 32-bit words (IEEE-754 bits for a `Float` component,
/// otherwise the integer value), flattened in `LinkedVarying` order. Used
/// through clipping (where `Float` components are lerped and others are
/// carried from the first operand, see the file comment above) and
/// rasterization (where they become the fragment stage's per-lane inputs).
struct RasterVertex {
  std::array<float, 4> Clip;
  SmallVector<uint32_t, 8> Varyings;
};

RasterVertex lerpVertex(const RasterVertex &A, const RasterVertex &B, float T,
                        ArrayRef<LinkedVarying> Varyings) {
  RasterVertex Out;
  for (unsigned I = 0; I != 4; ++I)
    Out.Clip[I] = A.Clip[I] + (B.Clip[I] - A.Clip[I]) * T;
  Out.Varyings.resize(A.Varyings.size());
  unsigned Idx = 0;
  for (const LinkedVarying &V : Varyings) {
    for (unsigned Row = 0; Row != V.RowCount; ++Row) {
      for (unsigned C = 0; C != V.ComponentCount; ++C, ++Idx) {
        if (V.ComponentType == SignatureComponentType::Float) {
          float Av, Bv;
          memcpy(&Av, &A.Varyings[Idx], 4);
          memcpy(&Bv, &B.Varyings[Idx], 4);
          float R = Av + (Bv - Av) * T;
          memcpy(&Out.Varyings[Idx], &R, 4);
        } else {
          Out.Varyings[Idx] = A.Varyings[Idx];
        }
      }
    }
  }
  return Out;
}

/// One homogeneous clip-space half-space, `Distance(V) >= 0` meaning
/// "inside". Sutherland-Hodgman clips a polygon against each of these in
/// turn (see the file comment above's guard-band-plane note).
constexpr float ClipEpsilon = 1e-5f;

std::vector<RasterVertex>
clipAgainstPlane(std::vector<RasterVertex> In,
                 float (*Distance)(const std::array<float, 4> &),
                 ArrayRef<LinkedVarying> Varyings) {
  if (In.empty())
    return In;
  std::vector<RasterVertex> Out;
  for (size_t I = 0; I != In.size(); ++I) {
    const RasterVertex &Cur = In[I];
    const RasterVertex &Prev = In[(I + In.size() - 1) % In.size()];
    float DCur = Distance(Cur.Clip);
    float DPrev = Distance(Prev.Clip);
    bool CurIn = DCur >= 0.0f;
    bool PrevIn = DPrev >= 0.0f;
    if (CurIn != PrevIn) {
      float T = DPrev / (DPrev - DCur);
      Out.push_back(lerpVertex(Prev, Cur, T, Varyings));
    }
    if (CurIn)
      Out.push_back(Cur);
  }
  return Out;
}

std::vector<RasterVertex> clipTriangle(std::array<RasterVertex, 3> Tri,
                                       ArrayRef<LinkedVarying> Varyings) {
  std::vector<RasterVertex> Poly(Tri.begin(), Tri.end());
  static constexpr float (*Planes[])(const std::array<float, 4> &) = {
      [](const std::array<float, 4> &C) { return C[3] - ClipEpsilon; },
      [](const std::array<float, 4> &C) { return C[3] - C[0]; },
      [](const std::array<float, 4> &C) { return C[3] + C[0]; },
      [](const std::array<float, 4> &C) { return C[3] - C[1]; },
      [](const std::array<float, 4> &C) { return C[3] + C[1]; },
      [](const std::array<float, 4> &C) { return C[3] - C[2]; },
      [](const std::array<float, 4> &C) { return C[2]; },
  };
  for (auto *Plane : Planes) {
    Poly = clipAgainstPlane(std::move(Poly), Plane, Varyings);
    if (Poly.size() < 3)
      return {};
  }
  return Poly;
}

/// One clipped, culled, viewport-transformed triangle ready for rasterization.
struct ScreenTriangle {
  std::array<std::array<float, 2>, 3> Pos;  // pixel-space x/y
  std::array<float, 3> InvW;                // 1/clip-space w, for SV_Position.w
  std::array<float, 3> Depth;               // viewport-mapped depth
  std::array<const uint32_t *, 3> Varyings; // pointer into owning storage
  bool FrontFacing;
  uint32_t PrimitiveID;
  uint32_t TargetLayer = 0;
  // (Roadmap H3a) The resolved `gl_ViewportIndex` value, carried through to
  // `FemeFragmentInvocation::ViewportIndex` for a fragment shader reading it
  // back as an input -- see `PrimitiveState::ViewportIndex`'s own comment.
  uint32_t ViewportIndex = 0;
  int32_t ScissorMinX = 0;
  int32_t ScissorMinY = 0;
  int32_t ScissorMaxX = 0;
  int32_t ScissorMaxY = 0;
  /// (roadmap F5) Whether this synthetic triangle is one half of a line
  /// primitive's quad expansion: gates whether `EdgeDistance`/`ArcLength`
  /// below are consulted at all (a point or real triangle primitive
  /// leaves them at 0, meaningless).
  bool IsLine = false;
  /// (roadmap F5) Each corner's signed perpendicular distance in pixels
  /// from the line's centerline (positive on one side, negative on the
  /// other): interpolated per-pixel to drive `RectangularSmooth`'s
  /// antialiasing coverage (see `LineRasterizationMode`'s comment).
  std::array<float, 3> EdgeDistance{0.0f, 0.0f, 0.0f};
  /// (roadmap F5) Each corner's distance in pixels along the line's
  /// length from the start of its containing line-list segment or
  /// line-strip (a strip's distance keeps accumulating across its
  /// segments, matching Vulkan's "continuously stippled" connected-strip
  /// behavior): interpolated per-sample to drive the stipple pattern
  /// test.
  std::array<float, 3> ArcLength{0.0f, 0.0f, 0.0f};
};

} // namespace

//===----------------------------------------------------------------------===//
// executeDraws
//===----------------------------------------------------------------------===//

namespace {

constexpr int32_t TileSize = 16;

/// Barycentric edge function for the directed edge A->B evaluated at P, in
/// pixel-space (y increasing downward). For a positively-wound triangle (by
/// this same function), a point is inside when all three of its edges'
/// values are non-negative (see the top-left rule application below).
float edgeFn(std::array<float, 2> A, std::array<float, 2> B,
             std::array<float, 2> P) {
  return (P[0] - A[0]) * (B[1] - A[1]) - (P[1] - A[1]) * (B[0] - A[0]);
}

/// Same directed-edge function as `edgeFn`, but evaluated in `double`
/// precision. Two triangles sharing an exact edge (the tessellator's own
/// crack-free bridging, or a quad/triangle-domain core lattice's per-cell
/// diagonal split) reuse the *same* pair of `float` vertex positions for
/// that shared edge, each in the opposite direction; the `float`-precision
/// `edgeFn` above is not antisymmetric bit-for-bit under that reversal (its
/// two directions subtract different operand pairs, e.g. `P - A` versus
/// `P - B`), so a sample that lies almost exactly on the shared edge can
/// round to a tiny negative value from *both* directions, leaving a
/// 1-sample gap neither triangle covers (roadmap H4j). Evaluating in
/// `double` does not change the geometry -- the `float` vertex positions
/// convert to `double` exactly, with no precision lost -- but shrinks the
/// rounding of the edge function itself (still a sum of products, so not
/// bit-exactly antisymmetric either) from `float`'s ~2^-23 relative
/// precision to `double`'s ~2^-52, several orders of magnitude below any
/// real crack this rasterizer's own coordinate range produces.
double edgeFnD(std::array<float, 2> A, std::array<float, 2> B,
               std::array<float, 2> P) {
  double Ax = A[0], Ay = A[1], Bx = B[0], By = B[1], Px = P[0], Py = P[1];
  return (Px - Ax) * (By - Ay) - (Py - Ay) * (Bx - Ax);
}

/// Whether the directed edge A->B is a "top" or "left" edge of a
/// positively-wound (by `edgeFn`) triangle in pixel space (y increasing
/// downward) -- the tie-break rule ("Rasterization correctness": "top-left
/// fill") that gives an edge shared by two triangles exactly one owner,
/// and (roadmap H4j) gives a lone triangle's own outer boundary (no
/// sharing triangle at all, e.g. a tessellated domain's own edge against
/// the clear color) a single, well-defined inside/outside answer at any
/// sample that lands exactly on it. A "positively-wound" triangle here
/// (`edgeFn(Pos0, Pos1, Pos2) > 0`, guaranteed for every triangle that
/// reaches the coverage test below, see the `SArea < 0.0f` reorder in the
/// triangle-assembly loop above) walks its boundary such that a "top"
/// edge is exactly horizontal and points *leftward* (`Dx < 0`, since the
/// interior lies below it) and a "left" edge points *downward* (`Dy > 0`,
/// since the interior lies to its right); their complements ("bottom":
/// horizontal and rightward, "right": upward) are correctly excluded.
bool isTopLeftEdge(std::array<float, 2> A, std::array<float, 2> B) {
  float Dy = B[1] - A[1];
  float Dx = B[0] - A[0];
  return (Dy == 0.0f && Dx < 0.0f) || Dy > 0.0f;
}

/// Fixed per-pixel sample offsets (relative to the pixel's top-left
/// corner) for a given multisample count (roadmap R33, "multisample
/// coverage and resolves"). These are FeMe's own deterministic convention
/// -- "Determinism and Reference Execution" in
/// feme/docs/FeMeGraphicsDesign.md requires fixed sample locations, not
/// any particular API's -- rather than a copy of Vulkan's or Direct3D's
/// standard sample pattern table. 1/2/4/8 samples are implemented; a
/// higher count is a mechanical, on-demand addition of another row here.
Expected<ArrayRef<std::array<float, 2>>> samplePositions(uint32_t Count) {
  static constexpr std::array<float, 2> One[] = {{0.5f, 0.5f}};
  static constexpr std::array<float, 2> Two[] = {{0.25f, 0.25f},
                                                 {0.75f, 0.75f}};
  static constexpr std::array<float, 2> Four[] = {
      {0.375f, 0.125f}, {0.875f, 0.375f}, {0.125f, 0.625f}, {0.625f, 0.875f}};
  // An "N-rooks" pattern: each sample's X coordinate is a distinct
  // 1/16-grid center, and the Y coordinates are the same set cyclically
  // shifted by three (rather than left in X order, which would put every
  // sample on the pixel's main diagonal) so no two samples share an X or
  // a Y coordinate and none coincide.
  static constexpr std::array<float, 2> Eight[] = {
      {0.0625f, 0.4375f}, {0.1875f, 0.5625f}, {0.3125f, 0.6875f},
      {0.4375f, 0.8125f}, {0.5625f, 0.9375f}, {0.6875f, 0.0625f},
      {0.8125f, 0.1875f}, {0.9375f, 0.3125f}};
  switch (Count) {
  case 1:
    return ArrayRef(One);
  case 2:
    return ArrayRef(Two);
  case 4:
    return ArrayRef(Four);
  case 8:
    return ArrayRef(Eight);
  default:
    return createStringError(inconvertibleErrorCode(),
                             "sample count %u is not yet supported (only 1, "
                             "2, 4, and 8 are implemented)",
                             Count);
  }
}

/// Evaluates \p Op for a candidate depth/stencil value \p New against the
/// attachment's current value \p Old, per the `CompareOp` semantics
/// Vulkan/Direct3D share (`Always`/`Never` ignore both operands).
bool compareOp(CompareOp Op, float New, float Old) {
  switch (Op) {
  case CompareOp::Never:
    return false;
  case CompareOp::Less:
    return New < Old;
  case CompareOp::Equal:
    return New == Old;
  case CompareOp::LessEqual:
    return New <= Old;
  case CompareOp::Greater:
    return New > Old;
  case CompareOp::NotEqual:
    return New != Old;
  case CompareOp::GreaterEqual:
    return New >= Old;
  case CompareOp::Always:
    return true;
  }
  llvm_unreachable("unhandled CompareOp");
}

/// Reads the depth attachment's stored value at pixel (\p PX, \p PY),
/// sample \p Sample of \p SampleCount (0/1 for a single-sample
/// attachment), converting `D16_UNORM` (and the depth half of
/// `D24_UNORM_S8_UINT`) to the same [0, 1] float convention every other
/// depth format already uses. Multisample storage interleaves samples
/// within a pixel: texel `(PX, PY, Sample)` is at flat index
/// `(PY * Width + PX) * SampleCount + Sample`.
Expected<float> readDepth(const AttachmentView &Depth, uint32_t SampleCount,
                          int32_t PX, int32_t PY, uint32_t Sample) {
  size_t Idx = ((size_t)PY * Depth.Width + PX) * SampleCount + Sample;
  switch (Depth.Format) {
  case cpu::ResourceFormat::D32_FLOAT: {
    float V;
    memcpy(&V, Depth.Data.data() + Idx * 4, 4);
    return V;
  }
  case cpu::ResourceFormat::D16_UNORM: {
    uint16_t V;
    memcpy(&V, Depth.Data.data() + Idx * 2, 2);
    return V / 65535.0f;
  }
  case cpu::ResourceFormat::D24_UNORM_S8_UINT: {
    double D;
    if (Error E = unpackDepth(
            Depth.Format, ArrayRef<uint8_t>(Depth.Data.data() + Idx * 4, 4), D))
      return std::move(E);
    return static_cast<float>(D);
  }
  default:
    return createStringError(inconvertibleErrorCode(),
                             "depth attachment format is not yet supported "
                             "(mechanical, added on demand)");
  }
}

Error writeDepth(AttachmentView &Depth, uint32_t SampleCount, int32_t PX,
                 int32_t PY, uint32_t Sample, float Value) {
  size_t Idx = ((size_t)PY * Depth.Width + PX) * SampleCount + Sample;
  switch (Depth.Format) {
  case cpu::ResourceFormat::D32_FLOAT:
    memcpy(Depth.Data.data() + Idx * 4, &Value, 4);
    return Error::success();
  case cpu::ResourceFormat::D16_UNORM: {
    uint16_t V = static_cast<uint16_t>(
        std::lround(std::clamp(Value, 0.0f, 1.0f) * 65535.0f));
    memcpy(Depth.Data.data() + Idx * 2, &V, 2);
    return Error::success();
  }
  case cpu::ResourceFormat::D24_UNORM_S8_UINT:
    // A read-modify-write of only the low 24 bits: the high byte
    // (stencil) may be shared with `writeStencil` on the very same word
    // and must not be clobbered here.
    return packDepthClear(
        Depth.Format, Value,
        MutableArrayRef<uint8_t>(Depth.Data.data() + Idx * 4, 4));
  default:
    return createStringError(inconvertibleErrorCode(),
                             "depth attachment format is not yet supported "
                             "(mechanical, added on demand)");
  }
}

/// Reads the stencil attachment's stored value, the same indexing
/// `readDepth` uses. `S8_UINT` is one raw byte per texel; the stencil half
/// of `D24_UNORM_S8_UINT` is the high byte of the same 4-byte word
/// `readDepth`/`writeDepth` address for that attachment's depth half.
Expected<uint8_t> readStencil(const AttachmentView &Stencil,
                              uint32_t SampleCount, int32_t PX, int32_t PY,
                              uint32_t Sample) {
  size_t Idx = ((size_t)PY * Stencil.Width + PX) * SampleCount + Sample;
  switch (Stencil.Format) {
  case cpu::ResourceFormat::S8_UINT:
    return Stencil.Data[Idx];
  case cpu::ResourceFormat::D24_UNORM_S8_UINT: {
    uint32_t S;
    if (Error E = unpackStencil(
            Stencil.Format, ArrayRef<uint8_t>(Stencil.Data.data() + Idx * 4, 4),
            S))
      return std::move(E);
    return static_cast<uint8_t>(S);
  }
  default:
    return createStringError(inconvertibleErrorCode(),
                             "stencil attachment format is not yet "
                             "supported (mechanical, added on demand)");
  }
}

Error writeStencil(AttachmentView &Stencil, uint32_t SampleCount, int32_t PX,
                   int32_t PY, uint32_t Sample, uint8_t Value) {
  size_t Idx = ((size_t)PY * Stencil.Width + PX) * SampleCount + Sample;
  switch (Stencil.Format) {
  case cpu::ResourceFormat::S8_UINT:
    Stencil.Data[Idx] = Value;
    return Error::success();
  case cpu::ResourceFormat::D24_UNORM_S8_UINT:
    // A read-modify-write of only the high byte -- see `writeDepth`'s
    // comment for why the other half must survive.
    return packStencilClear(
        Stencil.Format, Value,
        MutableArrayRef<uint8_t>(Stencil.Data.data() + Idx * 4, 4));
  default:
    return createStringError(inconvertibleErrorCode(),
                             "stencil attachment format is not yet "
                             "supported (mechanical, added on demand)");
  }
}

/// Applies \p Op ("Depth/Stencil" per Vulkan's `VkStencilOp`/Direct3D's
/// `D3D12_STENCIL_OP`) to \p Current, using \p Reference for `Replace`.
uint8_t applyStencilOp(StencilOp Op, uint8_t Current, uint8_t Reference) {
  switch (Op) {
  case StencilOp::Keep:
    return Current;
  case StencilOp::Zero:
    return 0;
  case StencilOp::Replace:
    return Reference;
  case StencilOp::IncrementClamp:
    return Current == 0xFF ? 0xFF : static_cast<uint8_t>(Current + 1);
  case StencilOp::DecrementClamp:
    return Current == 0 ? 0 : static_cast<uint8_t>(Current - 1);
  case StencilOp::Invert:
    return static_cast<uint8_t>(~Current);
  case StencilOp::IncrementWrap:
    return static_cast<uint8_t>(Current + 1);
  case StencilOp::DecrementWrap:
    return static_cast<uint8_t>(Current - 1);
  }
  llvm_unreachable("unhandled StencilOp");
}

/// Runs the combined depth/stencil test at pixel (\p PX, \p PY), sample
/// \p Sample of \p SampleCount, against candidate depth \p NewDepth, in
/// the fixed-function order every API shares: the stencil test first
/// (applying `FailOp` on failure), then -- only if stencil passed -- the
/// depth test (applying `DepthFailOp`/`PassOp`). Returns whether the
/// fragment may proceed to color write. \p DepthAttachment/
/// \p StencilAttachment are only dereferenced when the corresponding
/// test/write is enabled (the caller already validated they are bound in
/// that case). Every sample of one pixel shares \p NewDepth (the
/// rasterizer interpolates once per pixel, not per sample -- true
/// per-sample depth divergence is a later precision improvement, see the
/// file comment above), but each sample's stored depth/stencil value, and
/// therefore each sample's test result, is independent.
Expected<bool>
testDepthStencil(const DepthState &Depth, const StencilState &Stencil,
                 AttachmentView &DepthAttachment,
                 AttachmentView &StencilAttachment, uint32_t SampleCount,
                 bool FrontFacing, int32_t PX, int32_t PY, uint32_t Sample,
                 float NewDepth, std::optional<uint8_t> RefOverride = {}) {
  StencilFaceState Face = FrontFacing ? Stencil.Front : Stencil.Back;
  if (RefOverride)
    Face.Reference = *RefOverride;
  bool StencilPass = true;
  uint8_t StoredStencil = 0;
  if (Stencil.TestEnable) {
    Expected<uint8_t> Stored =
        readStencil(StencilAttachment, SampleCount, PX, PY, Sample);
    if (!Stored)
      return Stored.takeError();
    StoredStencil = *Stored;
    uint8_t Masked = StoredStencil & Face.CompareMask;
    uint8_t Ref = Face.Reference & Face.CompareMask;
    StencilPass = compareOp(Face.Compare, static_cast<float>(Ref),
                            static_cast<float>(Masked));
  }

  auto applyFace = [&](StencilOp Op) -> Error {
    uint8_t Updated = applyStencilOp(Op, StoredStencil, Face.Reference);
    uint8_t Merged =
        (StoredStencil & ~Face.WriteMask) | (Updated & Face.WriteMask);
    return writeStencil(StencilAttachment, SampleCount, PX, PY, Sample, Merged);
  };

  if (!StencilPass) {
    if (Stencil.TestEnable)
      if (Error E = applyFace(Face.FailOp))
        return std::move(E);
    return false;
  }

  bool DepthPass = true;
  if (Depth.TestEnable) {
    Expected<float> OldDepth =
        readDepth(DepthAttachment, SampleCount, PX, PY, Sample);
    if (!OldDepth)
      return OldDepth.takeError();
    DepthPass = compareOp(Depth.Compare, NewDepth, *OldDepth);
  }

  if (!DepthPass) {
    if (Stencil.TestEnable)
      if (Error E = applyFace(Face.DepthFailOp))
        return std::move(E);
    return false;
  }

  if (Stencil.TestEnable)
    if (Error E = applyFace(Face.PassOp))
      return std::move(E);
  if (Depth.WriteEnable) {
    if (Error E =
            writeDepth(DepthAttachment, SampleCount, PX, PY, Sample, NewDepth))
      return std::move(E);
  }
  return true;
}

//===----------------------------------------------------------------------===//
// Blending, write masks, and logic ops
//===----------------------------------------------------------------------===//

/// Evaluates one blend operand (`Src`/`DstColorFactor`/`SrcAlphaFactor`/
/// etc.) at color channel \p Channel (0=R, 1=G, 2=B, 3=A), matching
/// Vulkan's `VkBlendFactor`/Direct3D's `D3D12_BLEND` semantics. \p Src1 is
/// the fragment stage's second color output (`SV_Target0`'s `Index=1`
/// companion), read by the four dual-source (`VK_BLEND_FACTOR_SRC1_*`)
/// factors; every other pipeline never reads it (see "Dual-source
/// blending" in feme/docs/FeMeGraphicsDesign.md).
double blendFactorValue(BlendFactor F, unsigned Channel,
                        const std::array<double, 4> &Src,
                        const std::array<double, 4> &Dst,
                        const std::array<float, 4> &Constant,
                        const std::array<double, 4> &Src1) {
  switch (F) {
  case BlendFactor::Zero:
    return 0.0;
  case BlendFactor::One:
    return 1.0;
  case BlendFactor::SrcColor:
    return Src[Channel];
  case BlendFactor::OneMinusSrcColor:
    return 1.0 - Src[Channel];
  case BlendFactor::DstColor:
    return Dst[Channel];
  case BlendFactor::OneMinusDstColor:
    return 1.0 - Dst[Channel];
  case BlendFactor::SrcAlpha:
    return Src[3];
  case BlendFactor::OneMinusSrcAlpha:
    return 1.0 - Src[3];
  case BlendFactor::DstAlpha:
    return Dst[3];
  case BlendFactor::OneMinusDstAlpha:
    return 1.0 - Dst[3];
  case BlendFactor::ConstantColor:
    return Constant[Channel];
  case BlendFactor::OneMinusConstantColor:
    return 1.0 - Constant[Channel];
  case BlendFactor::ConstantAlpha:
    return Constant[3];
  case BlendFactor::OneMinusConstantAlpha:
    return 1.0 - Constant[3];
  case BlendFactor::SrcAlphaSaturate:
    return Channel == 3 ? 1.0 : std::min(Src[3], 1.0 - Dst[3]);
  case BlendFactor::Src1Color:
    return Src1[Channel];
  case BlendFactor::OneMinusSrc1Color:
    return 1.0 - Src1[Channel];
  case BlendFactor::Src1Alpha:
    return Src1[3];
  case BlendFactor::OneMinusSrc1Alpha:
    return 1.0 - Src1[3];
  }
  llvm_unreachable("unhandled BlendFactor");
}

double applyBlendOp(BlendOp Op, double SrcTerm, double DstTerm) {
  switch (Op) {
  case BlendOp::Add:
    return SrcTerm + DstTerm;
  case BlendOp::Subtract:
    return SrcTerm - DstTerm;
  case BlendOp::ReverseSubtract:
    return DstTerm - SrcTerm;
  case BlendOp::Min:
    return std::min(SrcTerm, DstTerm);
  case BlendOp::Max:
    return std::max(SrcTerm, DstTerm);
  }
  llvm_unreachable("unhandled BlendOp");
}

/// Blends \p Src (the fragment's new color) with \p Dst (the attachment's
/// existing color) per \p Blend's equation, matching every graphics API's
/// shared "scale each operand, then combine" blend model. \p Src1 is the
/// fragment stage's second color output, or all-zero for a pipeline with
/// no dual-source blend factor (see `blendFactorValue`'s own comment).
std::array<double, 4> blendColor(const BlendState &Blend,
                                 const std::array<double, 4> &Src,
                                 const std::array<double, 4> &Dst,
                                 const std::array<float, 4> &Constant,
                                 const std::array<double, 4> &Src1) {
  std::array<double, 4> Result;
  for (unsigned C = 0; C != 3; ++C) {
    double SF =
        blendFactorValue(Blend.SrcColorFactor, C, Src, Dst, Constant, Src1);
    double DF =
        blendFactorValue(Blend.DstColorFactor, C, Src, Dst, Constant, Src1);
    Result[C] = applyBlendOp(Blend.ColorOp, Src[C] * SF, Dst[C] * DF);
  }
  double SFA =
      blendFactorValue(Blend.SrcAlphaFactor, 3, Src, Dst, Constant, Src1);
  double DFA =
      blendFactorValue(Blend.DstAlphaFactor, 3, Src, Dst, Constant, Src1);
  Result[3] = applyBlendOp(Blend.AlphaOp, Src[3] * SFA, Dst[3] * DFA);
  return Result;
}

/// Applies \p Op ("Set"/"Copy"/"And"/etc., matching Vulkan's `VkLogicOp`)
/// bitwise to one byte of a fragment's new color (\p Src) and an
/// attachment's existing value (\p Dst).
uint8_t applyLogicOp(LogicOp Op, uint8_t Src, uint8_t Dst) {
  switch (Op) {
  case LogicOp::Clear:
    return 0;
  case LogicOp::Set:
    return 0xFF;
  case LogicOp::Copy:
    return Src;
  case LogicOp::CopyInverted:
    return static_cast<uint8_t>(~Src);
  case LogicOp::NoOp:
    return Dst;
  case LogicOp::Invert:
    return static_cast<uint8_t>(~Dst);
  case LogicOp::And:
    return Src & Dst;
  case LogicOp::Nand:
    return static_cast<uint8_t>(~(Src & Dst));
  case LogicOp::Or:
    return Src | Dst;
  case LogicOp::Nor:
    return static_cast<uint8_t>(~(Src | Dst));
  case LogicOp::Xor:
    return Src ^ Dst;
  case LogicOp::Equivalent:
    return static_cast<uint8_t>(~(Src ^ Dst));
  case LogicOp::AndReverse:
    return Src & static_cast<uint8_t>(~Dst);
  case LogicOp::AndInverted:
    return static_cast<uint8_t>(~Src) & Dst;
  case LogicOp::OrReverse:
    return Src | static_cast<uint8_t>(~Dst);
  case LogicOp::OrInverted:
    return static_cast<uint8_t>(~Src) | Dst;
  }
  llvm_unreachable("unhandled LogicOp");
}

/// Merges a fragment's new color \p Src into \p Texel (the attachment's
/// existing texel, read and overwritten in place) per \p Pipeline's blend/
/// logic-op/write-mask state (roadmap R33). A logic op, when enabled,
/// takes priority over blending -- matching Vulkan/Direct3D, where
/// enabling one disables the other -- and is only implemented for 8-bit
/// unsigned-normalized formats (`R8G8B8A8_*`), the same restriction both
/// APIs place on which formats support a logic op at all. \p Src1 is the
/// fragment stage's second color output for a dual-source blend factor
/// (see `blendColor`'s own comment), or all-zero when the pipeline has
/// none.
Error mergeColor(const BlendState &Blend, bool LogicOpEnable, LogicOp Logic,
                 const std::array<float, 4> &BlendConstants,
                 cpu::ResourceFormat Format, const std::array<double, 4> &Src,
                 const std::array<double, 4> &Src1,
                 MutableArrayRef<uint8_t> Texel) {
  if (LogicOpEnable) {
    if (Format != cpu::ResourceFormat::R8G8B8A8_UNORM &&
        Format != cpu::ResourceFormat::R8G8B8A8_UINT &&
        Format != cpu::ResourceFormat::R8G8B8A8_SINT)
      return createStringError(inconvertibleErrorCode(),
                               "logic ops are only implemented for "
                               "R8G8B8A8_UNORM/_UINT/_SINT attachments "
                               "(mechanical, added on demand)");
    std::array<uint8_t, 4> SrcBytes{};
    if (Error E = packClearColor(Format, Src, SrcBytes))
      return E;
    for (unsigned C = 0; C != 4; ++C)
      if ((Blend.WriteMask >> C) & 1u)
        Texel[C] = applyLogicOp(Logic, SrcBytes[C], Texel[C]);
    return Error::success();
  }

  std::array<double, 4> Final = Src;
  if (Blend.BlendEnable) {
    std::array<double, 4> Dst{};
    if (Error E = unpackColor(Format, Texel, Dst))
      return E;
    Final = blendColor(Blend, Src, Dst, BlendConstants, Src1);
  }
  if (Blend.WriteMask != 0xF) {
    std::array<double, 4> Dst{};
    if (Error E = unpackColor(Format, Texel, Dst))
      return E;
    for (unsigned C = 0; C != 4; ++C)
      if (!((Blend.WriteMask >> C) & 1u))
        Final[C] = Dst[C];
  }
  return packClearColor(Format, Final, Texel);
}

} // namespace

Error executeDraws(const GraphicsPipeline &Pipeline, const PreparedDraw &Draw,
                   uint32_t WorkerCount) {
  uint32_t SampleCount = Pipeline.getSampleCount();
  if (SampleCount != 1 && SampleCount != 2 && SampleCount != 4 &&
      SampleCount != 8)
    return createStringError(inconvertibleErrorCode(),
                             "sample count %u is not yet supported (only 1, "
                             "2, 4, and 8 are implemented, roadmap R33/C4b)",
                             SampleCount);
  Expected<ArrayRef<std::array<float, 2>>> SamplePositions =
      samplePositions(SampleCount);
  if (!SamplePositions)
    return SamplePositions.takeError();
  if (!Draw.ResolveAttachments.empty() &&
      Draw.ResolveAttachments.size() != Draw.Attachments.size())
    return createStringError(inconvertibleErrorCode(),
                             "the draw has %zu resolve attachment(s) but "
                             "%zu color attachment(s); they must match "
                             "one-for-one or ResolveAttachments must be "
                             "empty",
                             Draw.ResolveAttachments.size(),
                             Draw.Attachments.size());
  const DepthState &PipelineDepth = Pipeline.getDepthState();
  if ((PipelineDepth.TestEnable || PipelineDepth.WriteEnable) &&
      Draw.DepthStencil.Depth.Data.empty())
    return createStringError(inconvertibleErrorCode(),
                             "depth testing/writes are enabled but the draw "
                             "has no bound depth attachment");
  const StencilState &PipelineStencil = Pipeline.getStencilState();
  if (PipelineStencil.TestEnable && Draw.DepthStencil.Stencil.Data.empty())
    return createStringError(inconvertibleErrorCode(),
                             "stencil testing is enabled but the draw has "
                             "no bound stencil attachment");
  // A depth-only pipeline (no fragment shader color output) legally has
  // zero color attachments (`dEQP-VK.multiview.depth_without_fragment_
  // shader`'s own shape, roadmap H2b); the count-match check below is
  // what actually enforces consistency between the pipeline and the draw,
  // in the zero-attachment case as much as any other.
  if (Pipeline.getColorBlends().size() != Draw.Attachments.size())
    return createStringError(inconvertibleErrorCode(),
                             "the pipeline has %zu color blend state(s) but "
                             "the draw has %zu color attachment(s)",
                             Pipeline.getColorBlends().size(),
                             Draw.Attachments.size());
  switch (Pipeline.getTopology()) {
  case PrimitiveTopology::PointList:
  case PrimitiveTopology::LineList:
  case PrimitiveTopology::LineStrip:
  case PrimitiveTopology::TriangleList:
  case PrimitiveTopology::TriangleStrip:
  case PrimitiveTopology::TriangleFan:
    if (Pipeline.hasTessellationStages())
      return createStringError(inconvertibleErrorCode(),
                               "a pipeline with tessellation stages must use "
                               "the patch-list topology");
    break;
  case PrimitiveTopology::PatchList:
    if (!Pipeline.hasTessellationStages())
      return createStringError(inconvertibleErrorCode(),
                               "the patch-list topology requires a pipeline "
                               "with tessellation stages");
    break;
  case PrimitiveTopology::LineListWithAdjacency:
  case PrimitiveTopology::LineStripWithAdjacency:
  case PrimitiveTopology::TriangleListWithAdjacency:
  case PrimitiveTopology::TriangleStripWithAdjacency:
    // (roadmap H5d) An adjacency topology's whole purpose is handing a
    // geometry stage its primitives' neighboring vertices; without one
    // there is nowhere for that adjacency data to go.
    if (!Pipeline.hasGeometryStages())
      return createStringError(inconvertibleErrorCode(),
                               "an adjacency topology requires a pipeline "
                               "with a geometry stage");
    break;
  }

  const cpu::CompiledStage &VS = Pipeline.getVertexStage();
  Expected<EntrySignature> VSSig = getStageSignature(VS);
  if (!VSSig)
    return VSSig.takeError();
  // (roadmap H2j) A depth/stencil-only pipeline may legally omit its
  // fragment stage entirely; `FSSig` is then left with no elements at all,
  // which every loop below over `FSSig.Elements` (varying linkage, color
  // outputs, depth/stencil overrides) already treats as "this stage
  // declares none of these", exactly the right behavior for "no fragment
  // stage runs at all".
  EntrySignature FSSig;
  if (Pipeline.hasFragmentStage()) {
    Expected<EntrySignature> Sig =
        getStageSignature(Pipeline.getFragmentStage());
    if (!Sig)
      return Sig.takeError();
    FSSig = std::move(*Sig);
  }

  // (roadmap H4) The stage whose per-vertex outputs actually reach
  // clipping, the viewport transform and the interpolator: the domain
  // stage on a tessellating pipeline, otherwise the vertex stage. Every
  // lookup below -- `SV_Position`, the layer/viewport-index outputs, and
  // the fragment stage's varying linkage -- is against *that* stage's
  // signature, since on a tessellating pipeline the vertex stage's own
  // outputs are consumed by the hull stage instead and need not include
  // `SV_Position` at all.
  std::optional<PatchPipelineLinkage> TessLink;
  if (Pipeline.hasTessellationStages()) {
    PatchPipelineStages Stages{Pipeline.getHullStage(),
                               Pipeline.getPatchConstantStage(),
                               Pipeline.getDomainStage()};
    Expected<PatchPipelineLinkage> Link = linkPatchPipeline(*VSSig, Stages);
    if (!Link)
      return Link.takeError();
    TessLink = std::move(*Link);
  }
  // (roadmap H5d) The stage whose primitives a geometry stage assembles
  // from: the domain stage's evaluated vertices on a tessellating
  // pipeline, otherwise the vertex stage's own -- the same "last
  // pre-vertex-stage-chain output" `TessLink` above already names.
  const EntrySignature &PreGeometrySig =
      TessLink ? TessLink->DomainSig : *VSSig;

  // (roadmap H5d) A geometry stage, if present, becomes the new "last
  // pre-rasterization stage": every lookup below that used to read the
  // vertex/domain stage's own signature now reads the geometry stage's
  // instead, exactly mirroring how tessellation's own `TessLink` already
  // substitutes the domain stage for the vertex stage above.
  std::optional<EntrySignature> GSSig;
  SmallVector<LinkedStageElement, 4> GeomInputLinks;
  GeometryInputPrimitive GeomExpectedInput = GeometryInputPrimitive::Points;
  uint32_t GeomVerticesPerPrimitive = 0;
  if (Pipeline.hasGeometryStages()) {
    Expected<EntrySignature> Sig =
        getStageSignature(Pipeline.getGeometryStage());
    if (!Sig)
      return Sig.takeError();
    GSSig = std::move(*Sig);

    // The geometry stage's own declared input primitive class must match
    // what its producer (the tessellator's output primitive, or the
    // draw's own topology) actually assembles -- there is no implicit
    // conversion between e.g. a triangle and a triangle-with-adjacency
    // input.
    if (TessLink) {
      switch (Pipeline.getTessellationState().OutputPrimitive) {
      case TessOutputPrimitive::Point:
        GeomExpectedInput = GeometryInputPrimitive::Points;
        break;
      case TessOutputPrimitive::Line:
        GeomExpectedInput = GeometryInputPrimitive::Lines;
        break;
      case TessOutputPrimitive::TriangleCw:
      case TessOutputPrimitive::TriangleCcw:
        GeomExpectedInput = GeometryInputPrimitive::Triangles;
        break;
      }
    } else {
      switch (Pipeline.getTopology()) {
      case PrimitiveTopology::PointList:
        GeomExpectedInput = GeometryInputPrimitive::Points;
        break;
      case PrimitiveTopology::LineList:
      case PrimitiveTopology::LineStrip:
        GeomExpectedInput = GeometryInputPrimitive::Lines;
        break;
      case PrimitiveTopology::LineListWithAdjacency:
      case PrimitiveTopology::LineStripWithAdjacency:
        GeomExpectedInput = GeometryInputPrimitive::LinesAdjacency;
        break;
      case PrimitiveTopology::TriangleList:
      case PrimitiveTopology::TriangleStrip:
      case PrimitiveTopology::TriangleFan:
        GeomExpectedInput = GeometryInputPrimitive::Triangles;
        break;
      case PrimitiveTopology::TriangleListWithAdjacency:
      case PrimitiveTopology::TriangleStripWithAdjacency:
        GeomExpectedInput = GeometryInputPrimitive::TrianglesAdjacency;
        break;
      case PrimitiveTopology::PatchList:
        llvm_unreachable("PatchList always sets TessLink");
      }
    }
    if (Pipeline.getGeometryState().InputPrimitive != GeomExpectedInput)
      return createStringError(inconvertibleErrorCode(),
                               "the geometry stage's declared input "
                               "primitive class does not match the "
                               "pipeline's topology/tessellation output "
                               "primitive");
    GeomVerticesPerPrimitive = getVerticesPerPrimitive(GeomExpectedInput);

    // Links every geometry-input element (except `SV_PrimitiveID`, sourced
    // from `FemeGeometryInvocation` instead) to the producing stage's
    // matching output by `Location`/system value (e.g. `SV_Position`),
    // mirroring the fragment-input varying linkage below.
    Expected<SmallVector<LinkedStageElement, 4>> Links = linkStageElements(
        PreGeometrySig, SignatureDirection::Output, *GSSig,
        SignatureDirection::Input,
        "vertex/domain stage output -> geometry stage input",
        [](const SignatureElement &Elt) {
          return Elt.SystemValue != SignatureSystemValue::PrimitiveID;
        });
    if (!Links)
      return Links.takeError();
    GeomInputLinks = std::move(*Links);
  }
  const EntrySignature &RasterSig = GSSig ? *GSSig : PreGeometrySig;

  const SignatureElement *VSPosition = findElement(
      RasterSig, SignatureDirection::Output, SignatureSystemValue::Position);
  const SignatureElement *VSLayerOut =
      findElement(RasterSig, SignatureDirection::Output,
                  SignatureSystemValue::RenderTargetArrayIndex);
  const SignatureElement *VSViewportOut =
      findElement(RasterSig, SignatureDirection::Output,
                  SignatureSystemValue::ViewportArrayIndex);
  if (!VSPosition)
    return createStringError(inconvertibleErrorCode(),
                             "the last pre-rasterization stage does not "
                             "write an SV_Position output; the executor "
                             "cannot clip/rasterize without one");
  if (VSPosition->ComponentCount != 4)
    return createStringError(inconvertibleErrorCode(),
                             "SV_Position output must have 4 components");

  // Link every non-system-value fragment input to the vertex output at the
  // same `Location` (Vulkan-style linkage; see "Normalized pipeline").
  SmallVector<LinkedVarying, 8> Varyings;
  for (const SignatureElement &FSIn : FSSig.Elements) {
    if (FSIn.Direction != SignatureDirection::Input ||
        FSIn.SystemValue != SignatureSystemValue::None)
      continue;
    if (!FSIn.Location)
      return createStringError(inconvertibleErrorCode(),
                               "fragment input element %u has no location "
                               "to link against a vertex output",
                               FSIn.ElementID);
    const SignatureElement *VSOut = findElementByLocation(
        RasterSig, SignatureDirection::Output, *FSIn.Location);
    if (!VSOut)
      return createStringError(inconvertibleErrorCode(),
                               "fragment input location %u has no matching "
                               "vertex stage output",
                               *FSIn.Location);
    if (VSOut->ComponentCount != FSIn.ComponentCount ||
        VSOut->RowCount != FSIn.RowCount ||
        VSOut->ComponentType != FSIn.ComponentType)
      return createStringError(inconvertibleErrorCode(),
                               "vertex output and fragment input at "
                               "location %u disagree on component/row "
                               "count or type",
                               *FSIn.Location);
    Varyings.push_back({VSOut->ElementID, FSIn.ElementID, FSIn.ComponentCount,
                        FSIn.RowCount, FSIn.ComponentType, FSIn.Interpolation});
  }

  // One `SV_TargetN` fragment output per color attachment (roadmap R33's
  // "multiple render targets"), linked by `Location` the same way varyings
  // are: `FSColors[i]` writes into `Draw.Attachments[i]`.
  //
  // (roadmap F8) `vkCmdSetRenderingAttachmentLocations` can remap which
  // output location feeds which attachment index; `Draw.
  // ColorAttachmentLocations[Location]` names the attachment index (or
  // `0xFFFFFFFF`/`VK_ATTACHMENT_UNUSED` for "writes nowhere"), the identity
  // mapping when empty. `locationForAttachment` inverts it once per draw --
  // attachment index `I`'s own source location -- rather than repeating an
  // O(N) search per attachment for every draw's every triangle.
  auto locationForAttachment = [&](uint32_t I) -> std::optional<uint32_t> {
    if (Draw.ColorAttachmentLocations.empty())
      return I;
    for (uint32_t Loc = 0; Loc != Draw.ColorAttachmentLocations.size(); ++Loc)
      if (Draw.ColorAttachmentLocations[Loc] == I)
        return Loc;
    return std::nullopt;
  };
  SmallVector<const SignatureElement *, 4> FSColors;
  for (uint32_t I = 0; I != Draw.Attachments.size(); ++I) {
    if (Draw.Attachments[I].Data.empty()) {
      // (Roadmap E5) `VK_KHR_maintenance5`: this color slot's
      // `VkRenderingAttachmentInfo::imageView` was `VK_NULL_HANDLE` --
      // present but unused. The spec requires the fragment shader not to
      // write here, so no output is required (or consulted) at this
      // location either.
      FSColors.push_back(nullptr);
      continue;
    }
    std::optional<uint32_t> Loc = locationForAttachment(I);
    if (!Loc) {
      // (roadmap F8) No fragment output location is remapped onto this
      // attachment: it keeps whatever it already held, exactly like an
      // unused `VkRenderingAttachmentInfo` slot above.
      FSColors.push_back(nullptr);
      continue;
    }
    const SignatureElement *FSColor =
        findElementByLocation(FSSig, SignatureDirection::Output, *Loc);
    if (!FSColor)
      return createStringError(inconvertibleErrorCode(),
                               "fragment stage has no output at location %u "
                               "(mapped to color attachment %u)",
                               *Loc, I);
    if (FSColor->ComponentCount != 4 ||
        FSColor->ComponentType != SignatureComponentType::Float)
      return createStringError(inconvertibleErrorCode(),
                               "the fragment output at location %u mapped "
                               "to color attachment %u must be a "
                               "4-component floating-point output",
                               *Loc, I);
    FSColors.push_back(FSColor);
  }

  // Dual-source blending (`VK_BLEND_FACTOR_SRC1_*`, "Dual-source blending"
  // in feme/docs/FeMeGraphicsDesign.md): `SV_Target0`'s `Index=1`
  // companion, an `EntrySignature` element sharing `Location == 0` but
  // with `Index == 1` (see `SignatureElement::Index`'s own comment).
  // Vulkan requires a pipeline using a `SRC1` factor to have exactly one
  // color attachment, so this is only ever looked up against attachment
  // 0; every other attachment's blend state reading a `SRC1` factor would
  // be a validation bug upstream, not something this executor need
  // consider.
  auto usesDualSourceBlend = [](BlendFactor F) {
    return F == BlendFactor::Src1Color || F == BlendFactor::OneMinusSrc1Color ||
           F == BlendFactor::Src1Alpha || F == BlendFactor::OneMinusSrc1Alpha;
  };
  bool NeedsSrc1 =
      !Pipeline.getColorBlends().empty() &&
      (usesDualSourceBlend(Pipeline.getColorBlends()[0].SrcColorFactor) ||
       usesDualSourceBlend(Pipeline.getColorBlends()[0].DstColorFactor) ||
       usesDualSourceBlend(Pipeline.getColorBlends()[0].SrcAlphaFactor) ||
       usesDualSourceBlend(Pipeline.getColorBlends()[0].DstAlphaFactor));
  const SignatureElement *FSColor1 = nullptr;
  if (NeedsSrc1) {
    FSColor1 = findElementByLocation(FSSig, SignatureDirection::Output, 0,
                                     /*Index=*/1);
    if (!FSColor1)
      return createStringError(inconvertibleErrorCode(),
                               "a dual-source blend factor is used but the "
                               "fragment stage has no Index=1 output at "
                               "location 0");
    if (FSColor1->ComponentCount != 4 ||
        FSColor1->ComponentType != SignatureComponentType::Float)
      return createStringError(inconvertibleErrorCode(),
                               "the Index=1 output at location 0 must be a "
                               "4-component floating-point output");
  }

  // (Roadmap E5/H3) The extent used to clamp each selected scissor rect
  // below: the first bound (non-unused) color attachment, or else the
  // depth/stencil attachment, since attachment 0 itself may be an unused
  // `VK_NULL_HANDLE` slot with no extent of its own.
  uint32_t ExtentWidth = 0, ExtentHeight = 0;
  for (const AttachmentView &A : Draw.Attachments)
    if (!A.Data.empty()) {
      ExtentWidth = A.Width;
      ExtentHeight = A.Height;
      break;
    }
  if (ExtentWidth == 0 && ExtentHeight == 0) {
    if (!Draw.DepthStencil.Depth.Data.empty()) {
      ExtentWidth = Draw.DepthStencil.Depth.Width;
      ExtentHeight = Draw.DepthStencil.Depth.Height;
    } else if (!Draw.DepthStencil.Stencil.Data.empty()) {
      ExtentWidth = Draw.DepthStencil.Stencil.Width;
      ExtentHeight = Draw.DepthStencil.Stencil.Height;
    }
  }
  SmallVector<uint32_t, 4> ColorElemSizes;
  for (const AttachmentView &A : Draw.Attachments) {
    if (A.Data.empty()) {
      // An unused slot has no format/extent to validate against.
      ColorElemSizes.push_back(0);
      continue;
    }
    Expected<uint32_t> ElemSize = getFixtureFormatElementSize(A.Format);
    if (!ElemSize)
      return ElemSize.takeError();
    ColorElemSizes.push_back(*ElemSize);
    size_t ExpectedSize =
        (size_t)A.Width * A.Height * A.ArrayLayers * SampleCount * *ElemSize;
    if (A.Data.size() != ExpectedSize)
      return createStringError(
          inconvertibleErrorCode(),
          "a color attachment's data is %zu byte(s), "
          "expected %zu (width * height * layer count * sample "
          "count * element size)",
          A.Data.size(), ExpectedSize);
  }

  uint32_t DrawLayerCount = getDrawLayerCount(Draw);

  // --- Depth/stencil test/write setup (roadmap R33). ---
  //
  // An early test -- performed before the fragment stage runs, using the
  // rasterizer's own interpolated depth and each face's fixed stencil
  // reference -- is only correct when the fragment stage cannot override
  // either (no `SV_Depth`/`SV_StencilRef` output) and cannot conditionally
  // suppress its own side effects (no discard/demote): "An early depth
  // pass may reject side-effect invocations before fragment execution only
  // when the source API permits it" ("Early and late tests" in
  // feme/docs/FeMeGraphicsDesign.md). Every other case defers the test
  // until after the fragment stage returns, matching output merge's own
  // "depth, stencil, blend, and attachment writes in specification order".
  const SignatureElement *FSDepthOut = findElement(
      FSSig, SignatureDirection::Output, SignatureSystemValue::Depth);
  const SignatureElement *FSStencilRefOut = findElement(
      FSSig, SignatureDirection::Output, SignatureSystemValue::StencilRef);
  // (roadmap H2j) A fragment-less pipeline can neither write `SV_Depth`/
  // `SV_StencilRef` (its empty `FSSig` above already guarantees `FSDepthOut`/
  // `FSStencilRefOut` are null) nor discard/demote, so `UseEarlyDepthStencil`
  // below is unconditionally true whenever depth/stencil testing is needed
  // at all -- matching "only vertex-stage clip/rasterize/early-depth-test,
  // no per-fragment shading" exactly.
  uint32_t FSFlags = Pipeline.hasFragmentStage()
                         ? Pipeline.getFragmentStage().getArtifactInfo().Flags
                         : 0;
  bool FSMayDiscard = (FSFlags & (cpu::FEME_CPU_ARTIFACT_USES_DISCARD |
                                  cpu::FEME_CPU_ARTIFACT_USES_DEMOTE)) != 0;
  bool DepthTestOrWrite = PipelineDepth.TestEnable || PipelineDepth.WriteEnable;
  bool NeedsDepthStencil = DepthTestOrWrite || PipelineStencil.TestEnable;
  bool UseEarlyDepthStencil =
      NeedsDepthStencil && !FSDepthOut && !FSStencilRefOut && !FSMayDiscard;
  // (roadmap H4) Which primitive class actually reaches the rasterizer. A
  // patch-list pipeline's own topology says nothing about that -- the
  // tessellator's `TessOutputPrimitive` does -- so this is the
  // tessellator's output primitive when tessellating and the topology's
  // own class otherwise.
  enum class RasterPrimitiveClass { Point, Line, Triangle };
  RasterPrimitiveClass RasterClass = RasterPrimitiveClass::Triangle;
  if (GSSig) {
    // (roadmap H5d) A geometry stage's own declared output primitive
    // class -- not the topology/tessellator's -- is what rasterization
    // now sees: the geometry stage is the new "last pre-rasterization
    // stage".
    switch (Pipeline.getGeometryState().OutputPrimitive) {
    case GeometryOutputPrimitive::Points:
      RasterClass = RasterPrimitiveClass::Point;
      break;
    case GeometryOutputPrimitive::LineStrip:
      RasterClass = RasterPrimitiveClass::Line;
      break;
    case GeometryOutputPrimitive::TriangleStrip:
      RasterClass = RasterPrimitiveClass::Triangle;
      break;
    }
  } else if (TessLink) {
    switch (Pipeline.getTessellationState().OutputPrimitive) {
    case TessOutputPrimitive::Point:
      RasterClass = RasterPrimitiveClass::Point;
      break;
    case TessOutputPrimitive::Line:
      RasterClass = RasterPrimitiveClass::Line;
      break;
    case TessOutputPrimitive::TriangleCw:
    case TessOutputPrimitive::TriangleCcw:
      RasterClass = RasterPrimitiveClass::Triangle;
      break;
    }
  } else if (Pipeline.getTopology() == PrimitiveTopology::PointList) {
    RasterClass = RasterPrimitiveClass::Point;
  } else if (Pipeline.getTopology() == PrimitiveTopology::LineList ||
             Pipeline.getTopology() == PrimitiveTopology::LineStrip) {
    RasterClass = RasterPrimitiveClass::Line;
  }
  uint32_t PrimitiveCounter = 0;

  for (const DrawCommand &Cmd : Draw.Draws) {
    if (Cmd.VertexCount == 0 || Cmd.InstanceCount == 0)
      continue;

    uint32_t PerInstance = Cmd.VertexCount;
    uint32_t Total = PerInstance * Cmd.InstanceCount;

    // Primitive restart (`primitiveRestartEnable`) only applies to an
    // indexed strip/fan draw: a special index value ends the current
    // strip/fan and starts a new one, exactly as an unindexed strip/fan
    // would begin fresh. Vulkan applies this to every strip and fan
    // topology (`LineStrip`, `TriangleStrip`, `TriangleFan`, and (roadmap
    // H5d) the two `*StripWithAdjacency` topologies), not just
    // `TriangleStrip`.
    bool RestartEnabled =
        Cmd.Indexed && Pipeline.getPrimitiveRestartEnable() &&
        (Pipeline.getTopology() == PrimitiveTopology::LineStrip ||
         Pipeline.getTopology() == PrimitiveTopology::TriangleStrip ||
         Pipeline.getTopology() == PrimitiveTopology::TriangleFan ||
         Pipeline.getTopology() == PrimitiveTopology::LineStripWithAdjacency ||
         Pipeline.getTopology() ==
             PrimitiveTopology::TriangleStripWithAdjacency);
    // The primitive-restart marker is the index type's own all-1-bits value
    // (`0xFF`/`0xFFFF`/`0xFFFFFFFF`), matching the raw index's own width.
    uint32_t RestartValue = Draw.IndexBuffer.Type == IndexType::UInt8 ? 0xFFu
                            : Draw.IndexBuffer.Type == IndexType::UInt16
                                ? 0xFFFFu
                                : 0xFFFFFFFFu;
    std::vector<bool> IsRestart(PerInstance, false);

    // --- Vertex/index fetch: assemble invocation keys. ---
    std::vector<cpu::FemeVertexInvocation> Invocations(Total);
    std::vector<uint32_t> VertexIndices(Total);
    for (uint32_t Inst = 0; Inst != Cmd.InstanceCount; ++Inst) {
      for (uint32_t J = 0; J != PerInstance; ++J) {
        uint32_t Flat = Inst * PerInstance + J;
        uint32_t VertexIndex;
        if (Cmd.Indexed) {
          uint32_t IndexPos = Cmd.FirstIndex + J;
          size_t ElemSize = Draw.IndexBuffer.Type == IndexType::UInt8    ? 1
                            : Draw.IndexBuffer.Type == IndexType::UInt16 ? 2
                                                                         : 4;
          size_t Off = (size_t)IndexPos * ElemSize;
          if (Off + ElemSize > Draw.IndexBuffer.Data.size())
            return createStringError(inconvertibleErrorCode(),
                                     "index buffer read is out of bounds");
          uint32_t RawIndex;
          if (ElemSize == 1) {
            RawIndex = Draw.IndexBuffer.Data[Off];
          } else if (ElemSize == 2) {
            uint16_t V;
            memcpy(&V, Draw.IndexBuffer.Data.data() + Off, 2);
            RawIndex = V;
          } else {
            memcpy(&RawIndex, Draw.IndexBuffer.Data.data() + Off, 4);
          }
          if (RestartEnabled && RawIndex == RestartValue)
            IsRestart[J] = true;
          VertexIndex = static_cast<uint32_t>(static_cast<int64_t>(RawIndex) +
                                              Cmd.VertexOffset);
        } else {
          VertexIndex = Cmd.FirstVertex + J;
        }
        VertexIndices[Flat] = VertexIndex;
        cpu::FemeVertexInvocation &Inv = Invocations[Flat];
        Inv.VertexID = VertexIndex;
        Inv.InstanceID = Cmd.FirstInstance + Inst;
        Inv.BaseVertex = Cmd.Indexed ? Cmd.VertexOffset
                                     : static_cast<int32_t>(Cmd.FirstVertex);
        Inv.BaseInstance = Cmd.FirstInstance;
        Inv.DrawID = 0;
        Inv.ViewIndex = Draw.ViewIndex;
      }
    }

    // --- Fetch/convert vertex attributes into the vertex-input block. ---
    Expected<StageStorage> VSInput =
        buildStageStorage(*VSSig, SignatureDirection::Input, Total);
    if (!VSInput)
      return VSInput.takeError();
    for (const SignatureElement &Elt : VSSig->Elements) {
      if (Elt.Direction != SignatureDirection::Input ||
          Elt.SystemValue != SignatureSystemValue::None)
        continue;
      if (!Elt.Location)
        return createStringError(inconvertibleErrorCode(),
                                 "vertex input element %u has no location "
                                 "to bind a vertex buffer attribute",
                                 Elt.ElementID);
      // A matrix vertex *attribute* needs one
      // `VkVertexInputAttributeDescription` per column, at consecutive
      // locations (unlike a matrix varying/ `Output`, which this executor's
      // `StageStorage`/`readRaw`/`writeRaw` now support directly via `Row`) --
      // that per-column attribute splitting is not implemented yet, so this is
      // still rejected explicitly rather than silently binding only row 0's
      // data.
      if (Elt.RowCount != 1)
        return createStringError(
            inconvertibleErrorCode(),
            "vertex input element %u spans %u rows; matrix vertex "
            "attributes are not implemented yet",
            Elt.ElementID, Elt.RowCount);
      const VertexBufferBinding *Binding = nullptr;
      const VertexAttribute *Attr = nullptr;
      for (const VertexBufferBinding &VB : Draw.VertexBuffers)
        for (const VertexAttribute &A : VB.Attributes)
          if (A.Location == *Elt.Location) {
            Binding = &VB;
            Attr = &A;
          }
      if (!Binding)
        return createStringError(inconvertibleErrorCode(),
                                 "vertex input location %u has no bound "
                                 "vertex buffer attribute",
                                 *Elt.Location);

      for (uint32_t Flat = 0; Flat != Total; ++Flat) {
        // A restart-marker index fetches no vertex at all (its lane never
        // appears in an assembled primitive below), and the raw index
        // arithmetic above is not a valid array index for it.
        if (RestartEnabled && IsRestart[Flat % PerInstance])
          continue;
        // A per-instance binding advances once per instance rather than
        // once per vertex (`VkVertexInputRate::VK_VERTEX_INPUT_RATE_
        // INSTANCE`): it is indexed by the invocation's instance index, not
        // its vertex index. (roadmap F6) `VK_KHR_vertex_attribute_divisor`
        // generalizes that one-fetch-per-instance step to one fetch every
        // `Divisor` instances (`Divisor == 1`, the default, is exactly the
        // plain per-instance case above), and `Divisor == 0`
        // (`vertexAttributeInstanceRateZeroDivisor`) is the one further
        // case where every instance reads the same vertex, at
        // `firstInstance` -- not a new fetch mechanism, just this same
        // formula's own degenerate divide-by-zero case spelled out
        // explicitly.
        uint32_t FetchIndex;
        if (!Binding->PerInstance) {
          FetchIndex = VertexIndices[Flat];
        } else if (Binding->Divisor == 0) {
          FetchIndex = Invocations[Flat].BaseInstance;
        } else {
          uint32_t FirstInstance = Invocations[Flat].BaseInstance;
          FetchIndex =
              FirstInstance +
              (Invocations[Flat].InstanceID - FirstInstance) / Binding->Divisor;
        }
        uint64_t SrcOff = (uint64_t)Binding->Stride * FetchIndex + Attr->Offset;
        Expected<uint32_t> CompByteSize =
            attributeComponentByteSize(Attr->Format);
        if (!CompByteSize)
          return CompByteSize.takeError();
        // (roadmap F10) `VkPipelineRobustnessCreateInfo::vertexInputs` /
        // `robustBufferAccess` (unconditionally on, see
        // `PhysicalDeviceInfo.cpp`'s own comment): an out-of-bounds vertex
        // attribute read must return zero per component -- like the
        // descriptor-bound helper path's own "For a vector access the check
        // is per-component" convention ("Bounds checking" in
        // FeMeCPUDesign.md) -- rather than fail the whole draw. Only the
        // components that actually fit within `Binding->Data` are decoded;
        // `Bits` is already zero-initialized for the rest.
        uint64_t AvailableBytes =
            SrcOff < Binding->Data.size() ? Binding->Data.size() - SrcOff : 0;
        uint32_t InBoundsComponents = static_cast<uint32_t>(std::min<uint64_t>(
            Elt.ComponentCount, AvailableBytes / *CompByteSize));
        std::array<uint32_t, 4> Bits{};
        if (InBoundsComponents != 0) {
          if (Error E =
                  decodeAttribute(Attr->Format, Binding->Data.data() + SrcOff,
                                  InBoundsComponents, Elt.ComponentType, Bits))
            return E;
        }
        for (uint32_t C = 0; C != Elt.ComponentCount; ++C)
          VSInput->writeRaw(Elt.ElementID, Elt.FirstComponent + C, Flat,
                            Bits[C]);
      }
    }

    Expected<StageStorage> VSOutput =
        buildStageStorage(*VSSig, SignatureDirection::Output, Total);
    if (!VSOutput)
      return VSOutput.takeError();

    cpu::FemeStageLayout VSInLayout = VSInput->layout();
    cpu::FemeStageLayout VSOutLayout = VSOutput->layout();
    cpu::VertexResources VRes;
    VRes.ResourceHeap = Draw.Resources.ResourceHeap;
    VRes.BoundResources = Draw.Resources.BoundResources;
    VRes.ImageHeap = Draw.Resources.ImageHeap;
    VRes.SamplerHeap = Draw.Resources.SamplerHeap;
    VRes.RootConstants = Draw.Resources.RootConstants;
    VRes.InputLayout = &VSInLayout;
    VRes.Inputs = VSInput->Data.data();
    VRes.OutputLayout = &VSOutLayout;
    VRes.Outputs = VSOutput->Data.data();
    VRes.Invocations = Invocations;
    cpu::PreparedVertexBatch PVB =
        cpu::PreparedVertexBatch::create(VS.getResourceInfo(), VRes);
    if (Error E = VS.invokeVertices(PVB))
      return E;

    // --- Tessellation (roadmap H4). ---
    //
    // Each patch of the patch-list draw is run through its own
    // hull/patch-constant/tessellator/domain chain
    // (`feme::graphics::runPatchPipeline`), and every patch's domain-stage
    // output is concatenated into one flat block whose invocation indices
    // the assembled `TessTris`/`TessLines` below already refer to
    // absolutely -- instancing folded in, since a patch's tessellation
    // factors are computed from its own instance's control points and so
    // two instances of the same patch need not produce the same number of
    // domain points at all.
    //
    // `RasterOut` is what everything downstream (clipping, the viewport
    // transform, the interpolator) reads: the domain stage's own outputs
    // when tessellating, the vertex stage's otherwise.
    const StageStorage *RasterOut = &*VSOutput;
    StageStorage TessOutput;
    SmallVector<std::array<uint32_t, 3>, 8> TessTris;
    SmallVector<std::array<uint32_t, 2>, 8> TessLines;
    if (TessLink) {
      const TessellationState &Tess = Pipeline.getTessellationState();
      if (Tess.InputControlPointCount == 0 ||
          PerInstance % Tess.InputControlPointCount != 0)
        return createStringError(inconvertibleErrorCode(),
                                 "a patch-list draw's vertex count (%u) must "
                                 "be a non-zero multiple of the pipeline's "
                                 "patch control point count (%u)",
                                 PerInstance, Tess.InputControlPointCount);
      PatchPipelineStages Stages{Pipeline.getHullStage(),
                                 Pipeline.getPatchConstantStage(),
                                 Pipeline.getDomainStage()};
      uint32_t PatchesPerInstance = PerInstance / Tess.InputControlPointCount;
      std::vector<PatchPipelineResult> Patches;
      SmallVector<uint32_t, 8> PatchBases;
      SmallVector<uint32_t, 8> ControlPointInvocations;
      uint32_t TotalPoints = 0;
      for (uint32_t Inst = 0; Inst != Cmd.InstanceCount; ++Inst) {
        for (uint32_t P = 0; P != PatchesPerInstance; ++P) {
          ControlPointInvocations.clear();
          for (uint32_t C = 0; C != Tess.InputControlPointCount; ++C)
            ControlPointInvocations.push_back(
                Inst * PerInstance + P * Tess.InputControlPointCount + C);
          Expected<PatchPipelineResult> Patch =
              runPatchPipeline(Stages, *TessLink, Tess, *VSOutput,
                               ControlPointInvocations, &Draw.Resources);
          if (!Patch)
            return Patch.takeError();
          PatchBases.push_back(TotalPoints);
          TotalPoints +=
              static_cast<uint32_t>(Patch->Tessellated.Points.size());
          Patches.push_back(std::move(*Patch));
        }
      }

      Expected<StageStorage> Flat = buildStageStorage(
          TessLink->DomainSig, SignatureDirection::Output, TotalPoints);
      if (!Flat)
        return Flat.takeError();
      TessOutput = std::move(*Flat);
      for (size_t I = 0; I != Patches.size(); ++I) {
        // A patch the tessellator culled entirely still carries a
        // minimally-sized (one-invocation) output block rather than an
        // empty one, so guard on its own point count rather than on that
        // block's invocation count.
        if (Patches[I].Tessellated.Points.empty())
          continue;
        appendStageInvocations(Patches[I].DomainOutputs, TessOutput,
                               PatchBases[I]);
        uint32_t Base = PatchBases[I];
        ArrayRef<uint32_t> Indices = Patches[I].Tessellated.Indices;
        if (Tess.OutputPrimitive == TessOutputPrimitive::Line) {
          for (size_t K = 0; K + 2 <= Indices.size(); K += 2)
            TessLines.push_back({Base + Indices[K], Base + Indices[K + 1]});
        } else if (Tess.OutputPrimitive != TessOutputPrimitive::Point) {
          for (size_t K = 0; K + 3 <= Indices.size(); K += 3)
            TessTris.push_back({Base + Indices[K], Base + Indices[K + 1],
                                Base + Indices[K + 2]});
        }
      }
      RasterOut = &TessOutput;
    }

    // --- Assemble primitives, clip, viewport-transform, cull. ---
    auto vertexAt = [&](uint32_t Flat) {
      RasterVertex V;
      for (unsigned C = 0; C != 4; ++C)
        V.Clip[C] = RasterOut->readFloat(VSPosition->ElementID, C, Flat);
      V.Varyings.resize(0);
      for (const LinkedVarying &LV : Varyings)
        for (uint32_t Row = 0; Row != LV.RowCount; ++Row)
          for (uint32_t C = 0; C != LV.ComponentCount; ++C)
            V.Varyings.push_back(
                RasterOut->readRaw(LV.VSElementID, C, Flat, Row));
      return V;
    };

    // Maps one already-clipped vertex's clip-space position to its
    // rasterizer-space position/`1/w`/viewport-mapped depth (Vulkan's
    // "coordinate transformations"). Shared by the triangle path below and
    // the point/line quad-expansion path further down so both apply
    // exactly the same viewport transform.
    struct PrimitiveState {
      const ViewportState *Viewport = nullptr;
      const ScissorRect *Scissor = nullptr;
      uint32_t TargetLayer = 0;
      // (Roadmap H3a) The resolved `gl_ViewportIndex` value itself, not
      // just the `Draw.Viewports`/`Draw.Scissors` slot it selects: a
      // fragment shader reading `gl_ViewportIndex` back as an input (e.g.
      // `dEQP-VK.draw.*.shader_viewport_index.fragment_shader_*`) needs the
      // resolved index value carried all the way to `FemeFragmentInvocation
      // ::ViewportIndex`, mirroring how `TargetLayer` above is already
      // carried through for `gl_Layer`/`SV_RenderTargetArrayIndex`.
      uint32_t ViewportIndex = 0;
    };
    auto resolvePrimitiveState =
        [&](uint32_t Invocation) -> std::optional<PrimitiveState> {
      int32_t RequestedViewport = 0;
      if (VSViewportOut)
        RequestedViewport =
            readSignedStageValue(*RasterOut, *VSViewportOut, Invocation);
      std::optional<uint32_t> ViewportIndex =
          resolveViewportArrayIndex(RequestedViewport, Draw.Viewports.size());
      std::optional<uint32_t> ScissorIndex =
          resolveViewportArrayIndex(RequestedViewport, Draw.Scissors.size());
      if (!ViewportIndex || !ScissorIndex)
        return std::nullopt;

      // Default to layer 0, not `Draw.ViewIndex`: for multiview, the caller
      // (CommandBuffer.cpp) already invokes one `Draw` per view with its
      // attachments pre-sliced down to that single view's own layer, so
      // `DrawLayerCount` here is already 1 and `Draw.ViewIndex` (which can be
      // > 0) would spuriously fail `resolveRenderTargetArrayLayer` below and
      // discard every primitive. `Draw.ViewIndex` remains available to the
      // shader itself via the `ViewIndex` built-in (see `Inv.ViewIndex`);
      // this default only matters when there is no `VSLayerOut` (no shader
      // output layer), i.e. no explicit `gl_Layer`/`SV_RenderTargetArrayIndex`
      // write routing the primitive to a specific array layer.
      int32_t RequestedLayer = 0;
      if (VSLayerOut)
        RequestedLayer =
            readSignedStageValue(*RasterOut, *VSLayerOut, Invocation);
      std::optional<uint32_t> Layer =
          resolveRenderTargetArrayLayer(RequestedLayer, DrawLayerCount);
      if (!Layer)
        return std::nullopt;

      return PrimitiveState{&Draw.Viewports[*ViewportIndex],
                            &Draw.Scissors[*ScissorIndex], *Layer,
                            *ViewportIndex};
    };

    auto projectVertex = [&](const RasterVertex &Vtx,
                             const ViewportState &Viewport,
                             std::array<float, 2> &Screen, float &InvW,
                             float &Depth) {
      float W = Vtx.Clip[3];
      float NdcX = Vtx.Clip[0] / W;
      float NdcY = Vtx.Clip[1] / W;
      float NdcZ = Vtx.Clip[2] / W;
      Screen = {Viewport.X + (NdcX * 0.5f + 0.5f) * Viewport.Width,
                Viewport.Y + (1.0f - (NdcY * 0.5f + 0.5f)) * Viewport.Height};
      InvW = 1.0f / W;
      Depth =
          Viewport.MinDepth + NdcZ * (Viewport.MaxDepth - Viewport.MinDepth);
    };

    // Assembles one strip segment [Start, End)'s triangles, alternating
    // winding order starting fresh at each segment -- exactly what a
    // restart does to an unindexed strip, and what a strip with no restart
    // markers does over its one whole segment.
    auto emitStripSegment = [](uint32_t Start, uint32_t End,
                               SmallVectorImpl<std::array<uint32_t, 3>> &Out) {
      uint32_t Local = 0;
      for (uint32_t T = Start; T + 3 <= End; ++T, ++Local)
        Out.push_back(Local % 2 == 0
                          ? std::array<uint32_t, 3>{T, T + 1, T + 2}
                          : std::array<uint32_t, 3>{T + 1, T, T + 2});
    };

    // Assembles one fan segment [Start, End)'s triangles: `Start` (the
    // segment's first vertex) is every triangle's shared pivot, exactly
    // as an unindexed fan pivots on its first vertex and a restart begins
    // a fresh fan (with a fresh pivot) at the next segment.
    auto emitFanSegment = [](uint32_t Start, uint32_t End,
                             SmallVectorImpl<std::array<uint32_t, 3>> &Out) {
      for (uint32_t T = Start + 1; T + 2 <= End; ++T)
        Out.push_back({Start, T, T + 1});
    };

    SmallVector<std::array<uint32_t, 3>, 8> TriIndices;
    if (Pipeline.getTopology() == PrimitiveTopology::TriangleList) {
      for (uint32_t T = 0; T + 3 <= PerInstance; T += 3)
        TriIndices.push_back({T, T + 1, T + 2});
    } else if (Pipeline.getTopology() == PrimitiveTopology::TriangleFan) {
      if (RestartEnabled) {
        uint32_t SegStart = 0;
        for (uint32_t J = 0; J != PerInstance; ++J) {
          if (!IsRestart[J])
            continue;
          emitFanSegment(SegStart, J, TriIndices);
          SegStart = J + 1;
        }
        emitFanSegment(SegStart, PerInstance, TriIndices);
      } else {
        emitFanSegment(0, PerInstance, TriIndices);
      }
    } else if (Pipeline.getTopology() == PrimitiveTopology::TriangleStrip) {
      if (RestartEnabled) {
        uint32_t SegStart = 0;
        for (uint32_t J = 0; J != PerInstance; ++J) {
          if (!IsRestart[J])
            continue;
          emitStripSegment(SegStart, J, TriIndices);
          SegStart = J + 1;
        }
        emitStripSegment(SegStart, PerInstance, TriIndices);
      } else {
        emitStripSegment(0, PerInstance, TriIndices);
      }
    }

    // Assembles a line-list/line-strip topology's line segments (as
    // vertex-index pairs), honoring restart on a strip exactly as the
    // triangle assembly above does.
    SmallVector<std::array<uint32_t, 2>, 8> LineIndices;
    if (Pipeline.getTopology() == PrimitiveTopology::LineList) {
      for (uint32_t T = 0; T + 2 <= PerInstance; T += 2)
        LineIndices.push_back({T, T + 1});
    } else if (Pipeline.getTopology() == PrimitiveTopology::LineStrip) {
      auto emitLineStripSegment =
          [](uint32_t Start, uint32_t End,
             SmallVectorImpl<std::array<uint32_t, 2>> &Out) {
            for (uint32_t T = Start; T + 2 <= End; ++T)
              Out.push_back({T, T + 1});
          };
      if (RestartEnabled) {
        uint32_t SegStart = 0;
        for (uint32_t J = 0; J != PerInstance; ++J) {
          if (!IsRestart[J])
            continue;
          emitLineStripSegment(SegStart, J, LineIndices);
          SegStart = J + 1;
        }
        emitLineStripSegment(SegStart, PerInstance, LineIndices);
      } else {
        emitLineStripSegment(0, PerInstance, LineIndices);
      }
    }

    // (roadmap H4) The primitive index lists everything below actually
    // rasterizes, in absolute (already instance-offset) invocation
    // indices: the topology-assembled per-instance lists above biased by
    // each instance's own base invocation, or the tessellator's own
    // connectivity, which spans every instance already -- each patch
    // produced its own point count, so a tessellated draw has no single
    // per-instance stride to bias by.
    SmallVector<std::array<uint32_t, 3>, 8> AbsTriIndices;
    SmallVector<std::array<uint32_t, 2>, 8> AbsLineIndices;
    if (TessLink) {
      AbsTriIndices = std::move(TessTris);
      AbsLineIndices = std::move(TessLines);
    } else {
      for (uint32_t Inst = 0; Inst != Cmd.InstanceCount; ++Inst) {
        uint32_t Base = Inst * PerInstance;
        for (std::array<uint32_t, 3> T : TriIndices)
          AbsTriIndices.push_back({Base + T[0], Base + T[1], Base + T[2]});
        for (std::array<uint32_t, 2> L : LineIndices)
          AbsLineIndices.push_back({Base + L[0], Base + L[1]});
      }
    }

    // --- Geometry stage (roadmap H5d). ---
    //
    // Assembles this draw's own input primitives (adjacency vertices
    // included, for one of the four `*WithAdjacency` topologies) out of
    // `RasterOut`'s already-instance-biased invocation positions, runs the
    // compiled geometry stage over the whole batch, and replays its flat
    // emitted-vertex/strip-boundary records back into one merged stream
    // whose own strips take over `AbsTriIndices`/`AbsLineIndices` and whose
    // own vertex storage takes over `RasterOut` -- the same "last
    // pre-rasterization stage" substitution `TessOutput`/`RasterOut` above
    // already established for tessellation.
    StageStorage GeomStreamOutput;
    if (GSSig) {
      const GeometryState &GState = Pipeline.getGeometryState();

      // Every input primitive's vertices, as absolute (already
      // instance-biased) invocation indices into `RasterOut`, in the
      // primitive's own declared `gl_in[]` vertex order -- adjacency
      // vertices interleaved back into that order, since
      // `splitListPrimitiveAdjacency`/`splitStripPrimitiveAdjacency`
      // instead return primitive/adjacent vertices grouped separately
      // (see their own comments in Pipeline.h).
      SmallVector<SmallVector<uint32_t, 6>, 8> Primitives;
      auto reorderAdjacency = [](const SplitPrimitiveAdjacency &Split) {
        SmallVector<uint32_t, 6> Order;
        if (Split.Adjacent.size() == 2) { // a *lines*-with-adjacency window
          Order = {Split.Adjacent[0], Split.Primitive[0], Split.Primitive[1],
                   Split.Adjacent[1]};
        } else { // a triangle-with-adjacency window
          Order = {Split.Primitive[0], Split.Adjacent[0],  Split.Primitive[1],
                   Split.Adjacent[1],  Split.Primitive[2], Split.Adjacent[2]};
        }
        return Order;
      };

      PrimitiveTopology Topology = Pipeline.getTopology();
      if (topologyHasAdjacency(Topology)) {
        bool IsStrip =
            Topology == PrimitiveTopology::LineStripWithAdjacency ||
            Topology == PrimitiveTopology::TriangleStripWithAdjacency;
        // Restart delimits an adjacency strip's own windows exactly as it
        // does every other strip topology above: each segment between
        // restart markers assembles its primitives independently, never
        // sliding an adjacency window across the boundary.
        SmallVector<std::pair<uint32_t, uint32_t>, 4> Segments;
        if (IsStrip && RestartEnabled) {
          uint32_t SegStart = 0;
          for (uint32_t J = 0; J != PerInstance; ++J) {
            if (!IsRestart[J])
              continue;
            Segments.push_back({SegStart, J});
            SegStart = J + 1;
          }
          Segments.push_back({SegStart, PerInstance});
        } else {
          Segments.push_back({0, PerInstance});
        }
        for (uint32_t Inst = 0; Inst != Cmd.InstanceCount; ++Inst) {
          uint32_t Base = Inst * PerInstance;
          if (IsStrip) {
            for (std::pair<uint32_t, uint32_t> Seg : Segments) {
              SmallVector<uint32_t, 16> Window;
              for (uint32_t J = Seg.first; J != Seg.second; ++J)
                Window.push_back(Base + J);
              uint32_t Count = getStripPrimitiveCount(
                  Topology, static_cast<uint32_t>(Window.size()));
              for (uint32_t P = 0; P != Count; ++P)
                Primitives.push_back(reorderAdjacency(
                    splitStripPrimitiveAdjacency(Topology, Window, P)));
            }
          } else {
            uint32_t VertsPerPrim = getListPrimitiveVertexCount(Topology);
            for (uint32_t P = 0; P + VertsPerPrim <= PerInstance;
                 P += VertsPerPrim) {
              SmallVector<uint32_t, 6> Window;
              for (uint32_t K = 0; K != VertsPerPrim; ++K)
                Window.push_back(Base + P + K);
              Primitives.push_back(reorderAdjacency(
                  splitListPrimitiveAdjacency(Topology, Window)));
            }
          }
        }
      } else if (GeomExpectedInput == GeometryInputPrimitive::Points) {
        for (uint32_t J = 0; J != RasterOut->InvocationCount; ++J)
          Primitives.push_back({J});
      } else if (GeomExpectedInput == GeometryInputPrimitive::Lines) {
        for (std::array<uint32_t, 2> Ln : AbsLineIndices)
          Primitives.push_back({Ln[0], Ln[1]});
      } else { // Triangles
        for (std::array<uint32_t, 3> Tri : AbsTriIndices)
          Primitives.push_back({Tri[0], Tri[1], Tri[2]});
      }

      // Every `Output`-direction element of the geometry stage's own
      // signature, in signature order: the exact flattening order
      // GeometryWrapper.cpp's `lowerGeometryStreamEmit` writes one emitted
      // vertex record's scalars in, which this must mirror to read them
      // back correctly.
      SmallVector<const SignatureElement *, 8> GSOutputElements;
      uint32_t OutputScalarsPerVertex = 0;
      for (const SignatureElement &Elt : GSSig->Elements)
        if (Elt.Direction == SignatureDirection::Output) {
          GSOutputElements.push_back(&Elt);
          OutputScalarsPerVertex += Elt.ComponentCount * Elt.RowCount;
        }

      uint32_t PrimitiveCount = static_cast<uint32_t>(Primitives.size());
      GeometryStreamBuilder Combined(/*StreamCount=*/1,
                                     GState.MaxOutputVertices);
      if (PrimitiveCount != 0) {
        Expected<StageStorage> GSInput =
            buildStageStorage(*GSSig, SignatureDirection::Input,
                              PrimitiveCount * GeomVerticesPerPrimitive);
        if (!GSInput)
          return GSInput.takeError();
        SmallVector<uint32_t, 32> SourceInvocations;
        SourceInvocations.reserve(PrimitiveCount * GeomVerticesPerPrimitive);
        for (const SmallVector<uint32_t, 6> &Prim : Primitives)
          for (uint32_t V : Prim)
            SourceInvocations.push_back(V);
        copyLinkedElements(*RasterOut, *GSInput, GeomInputLinks,
                           PrimitiveCount * GeomVerticesPerPrimitive,
                           SourceInvocations);

        Expected<StageStorage> GSScratch = buildStageStorage(
            *GSSig, SignatureDirection::Output, PrimitiveCount);
        if (!GSScratch)
          return GSScratch.takeError();

        std::vector<uint32_t> PrimitiveIDs(PrimitiveCount);
        std::iota(PrimitiveIDs.begin(), PrimitiveIDs.end(), 0u);
        std::vector<cpu::FemeGeometryInvocation> GeomInvocations =
            buildGeometryInvocations(PrimitiveIDs);

        std::vector<float> EmittedVertices((size_t)PrimitiveCount *
                                               GState.MaxOutputVertices *
                                               OutputScalarsPerVertex,
                                           0.0f);
        std::vector<uint32_t> EmittedVertexCounts(PrimitiveCount, 0);
        std::vector<uint8_t> StripEndsAfter(
            (size_t)PrimitiveCount * GState.MaxOutputVertices, 0);

        cpu::FemeStageLayout GSInLayout = GSInput->layout();
        cpu::FemeStageLayout GSOutLayout = GSScratch->layout();
        cpu::GeometryResources GRes;
        GRes.ResourceHeap = Draw.Resources.ResourceHeap;
        GRes.BoundResources = Draw.Resources.BoundResources;
        GRes.BoundImages = Draw.Resources.BoundImages;
        GRes.BoundSamplers = Draw.Resources.BoundSamplers;
        GRes.ImageHeap = Draw.Resources.ImageHeap;
        GRes.SamplerHeap = Draw.Resources.SamplerHeap;
        GRes.RootConstants = Draw.Resources.RootConstants;
        GRes.InputLayout = &GSInLayout;
        GRes.Inputs = GSInput->Data.data();
        GRes.OutputLayout = &GSOutLayout;
        GRes.Outputs = GSScratch->Data.data();
        GRes.Invocations = GeomInvocations;
        GRes.VerticesPerPrimitive = GeomVerticesPerPrimitive;
        GRes.MaxVerticesPerStream = GState.MaxOutputVertices;
        GRes.OutputScalarsPerVertex = OutputScalarsPerVertex;
        GRes.EmittedVertices = EmittedVertices;
        GRes.EmittedVertexCounts = EmittedVertexCounts;
        GRes.StripEndsAfter = StripEndsAfter;
        cpu::PreparedGeometryBatch Prepared =
            cpu::PreparedGeometryBatch::create(
                Pipeline.getGeometryStage().getResourceInfo(), GRes);
        if (Error E = Pipeline.getGeometryStage().invokeGeometry(Prepared))
          return E;

        collectGeometryStreams(Prepared.args(), Combined);
      }

      // Rebuilds the merged stream's flat vertex records into a
      // `StageStorage` shaped like `GSSig`'s own outputs, so everything
      // downstream (clipping, the viewport transform, the interpolator)
      // reads it exactly as it already reads `RasterOut`.
      llvm::ArrayRef<StreamVertex> MergedVerts = Combined.getVertices(0);
      Expected<StageStorage> Flat =
          buildStageStorage(*GSSig, SignatureDirection::Output,
                            static_cast<uint32_t>(MergedVerts.size()));
      if (!Flat)
        return Flat.takeError();
      GeomStreamOutput = std::move(*Flat);
      for (uint32_t Slot = 0; Slot != MergedVerts.size(); ++Slot) {
        const StreamVertex &Vtx = MergedVerts[Slot];
        uint32_t Cursor = 0;
        for (const SignatureElement *Elt : GSOutputElements)
          for (uint32_t Row = 0; Row != Elt->RowCount; ++Row)
            for (uint32_t Comp = 0; Comp != Elt->ComponentCount; ++Comp)
              GeomStreamOutput.writeFloat(Elt->ElementID,
                                          Elt->FirstComponent + Comp, Slot,
                                          Vtx[Cursor++], Row);
      }

      // Rebuilds the primitive lists rasterization consumes from the
      // merged stream's own strips: `Points` needs none (the point
      // rasterization loop below already just iterates `RasterOut`'s
      // whole invocation range).
      AbsTriIndices.clear();
      AbsLineIndices.clear();
      switch (GState.OutputPrimitive) {
      case GeometryOutputPrimitive::Points:
        break;
      case GeometryOutputPrimitive::LineStrip:
        for (const StreamStrip &S : Combined.getStrips(0))
          for (uint32_t I = S.Begin; I + 2 <= S.End; ++I)
            AbsLineIndices.push_back({I, I + 1});
        break;
      case GeometryOutputPrimitive::TriangleStrip:
        for (const StreamStrip &S : Combined.getStrips(0)) {
          uint32_t Local = 0;
          for (uint32_t I = S.Begin; I + 3 <= S.End; ++I, ++Local)
            AbsTriIndices.push_back(
                Local % 2 == 0 ? std::array<uint32_t, 3>{I, I + 1, I + 2}
                               : std::array<uint32_t, 3>{I + 1, I, I + 2});
        }
        break;
      }
      RasterOut = &GeomStreamOutput;
    }

    // Screen-space triangles plus their owning varying storage, binned into
    // tiles for the deferred per-tile rasterization pass below.
    std::vector<ScreenTriangle> ScreenTris;
    std::vector<std::unique_ptr<SmallVector<uint32_t, 8>>> TriVaryingStore;

    for (std::array<uint32_t, 3> Tri : AbsTriIndices) {
      std::optional<PrimitiveState> Primitive = resolvePrimitiveState(Tri[0]);
      if (!Primitive)
        continue;
      std::array<RasterVertex, 3> V = {vertexAt(Tri[0]), vertexAt(Tri[1]),
                                       vertexAt(Tri[2])};
      std::vector<RasterVertex> Clipped = clipTriangle(V, Varyings);
      for (size_t I = 1; I + 1 < Clipped.size(); ++I) {
        std::array<const RasterVertex *, 3> Poly = {&Clipped[0], &Clipped[I],
                                                    &Clipped[I + 1]};
        std::array<std::array<float, 2>, 3> Screen;
        std::array<float, 3> InvW, Depth;
        for (unsigned K = 0; K != 3; ++K)
          projectVertex(*Poly[K], *Primitive->Viewport, Screen[K], InvW[K],
                        Depth[K]);

        // `SArea` uses the same directed-edge formula (`edgeFn`) the
        // rasterizer's own coverage test does below, so that after the
        // positive-orientation normalization a covered point's edge
        // values are guaranteed non-negative. It is the negative of the
        // "positive area = CCW when authored in NDC" convention (the
        // viewport transform above flips Y), so `IsCCW` compensates.
        float SArea = edgeFn(Screen[0], Screen[1], Screen[2]);
        if (SArea == 0.0f)
          continue;
        bool IsCCW = SArea > 0.0f;
        bool FrontFacing = (Pipeline.getRasterState().Front ==
                            FrontFace::CounterClockwise) == IsCCW;
        CullMode Cull = Pipeline.getRasterState().Cull;
        if (Cull == CullMode::FrontAndBack ||
            (Cull == CullMode::Front && FrontFacing) ||
            (Cull == CullMode::Back && !FrontFacing))
          continue;

        if (SArea < 0.0f) {
          std::swap(Screen[1], Screen[2]);
          std::swap(InvW[1], InvW[2]);
          std::swap(Depth[1], Depth[2]);
          std::swap(Poly[1], Poly[2]);
        }

        auto VaryingBits = std::make_unique<SmallVector<uint32_t, 8>>();
        for (unsigned K = 0; K != 3; ++K)
          VaryingBits->append(Poly[K]->Varyings.begin(),
                              Poly[K]->Varyings.end());

        ScreenTriangle ST;
        ST.Pos = Screen;
        ST.InvW = InvW;
        ST.Depth = Depth;
        size_t Stride = Poly[0]->Varyings.size();
        for (unsigned K = 0; K != 3; ++K)
          ST.Varyings[K] = VaryingBits->data() + K * Stride;
        ST.FrontFacing = FrontFacing;
        ST.PrimitiveID = PrimitiveCounter++;
        ST.TargetLayer = Primitive->TargetLayer;
        ST.ViewportIndex = Primitive->ViewportIndex;
        ST.ScissorMinX = std::max<int32_t>(0, Primitive->Scissor->X);
        ST.ScissorMinY = std::max<int32_t>(0, Primitive->Scissor->Y);
        ST.ScissorMaxX = std::min<int32_t>(
            ExtentWidth, Primitive->Scissor->X +
                             static_cast<int32_t>(Primitive->Scissor->Width));
        ST.ScissorMaxY = std::min<int32_t>(
            ExtentHeight, Primitive->Scissor->Y +
                              static_cast<int32_t>(Primitive->Scissor->Height));
        ScreenTris.push_back(ST);
        TriVaryingStore.push_back(std::move(VaryingBits));
      }
    }

    // Points/lines (roadmap C4) reuse the triangle rasterizer above by
    // expanding each into a two-triangle screen-space quad -- there being
    // no separate point/line rasterizer, only this mechanical
    // pre-expansion -- rather than clip/rasterize a 1- or 2-vertex
    // primitive directly. The selected viewport/scissor's bounds (via the
    // tile-binning pass below) are the only "clipping" a point/line gets:
    // there is no Sutherland-Hodgman-style near/far/side-plane clip here,
    // only a whole-primitive reject when a vertex is behind the eye (`W`
    // at or below `clipTriangle`'s own `ClipEpsilon` guard), since
    // Vulkan's own point/line clipping rules are a documented deviation
    // this milestone accepts (see FeMeGraphicsDesign.md's status note).
    // `pointSizeRange` (`PhysicalDeviceInfo.cpp`) is fixed at "1.0 is the
    // only legal size" (`largePoints` is not an advertised device
    // feature), so a point's quad expansion still hardcodes a 0.5-pixel
    // half-extent. A line's width/style is no longer hardcoded the same
    // way: roadmap F5 threads `RasterState::LineWidth`/`LineMode`/stipple
    // fields (`vkCmdSetLineWidth`/`VkPipelineRasterizationLineStateCreate
    // Info`'s values) through the expansion below -- see
    // `LineRasterizationMode`'s own comment (feme/include/feme/Graphics/
    // Pipeline.h) for the three styles' shapes.
    //
    /// One corner of a synthetic point/line quad triangle, bundling every
    /// per-vertex value `pushQuadTriangle` needs: `Edge`/`Arc` (roadmap
    /// F5's per-corner perpendicular/arc-length distance, 0 and
    /// meaningless for a point) ride along the same varying-interpolation
    /// path `Depth`/`InvW` already use.
    struct QuadCorner {
      std::array<float, 2> Pos;
      float InvW;
      float Depth;
      const RasterVertex *Vtx;
      float Edge = 0.0f;
      float Arc = 0.0f;
    };
    auto pushQuadTriangle = [&](QuadCorner A, QuadCorner B, QuadCorner C,
                                const PrimitiveState &Primitive, bool IsLine) {
      std::array<std::array<float, 2>, 3> Screen = {A.Pos, B.Pos, C.Pos};
      std::array<float, 3> InvW = {A.InvW, B.InvW, C.InvW};
      std::array<float, 3> Depth = {A.Depth, B.Depth, C.Depth};
      std::array<float, 3> Edge = {A.Edge, B.Edge, C.Edge};
      std::array<float, 3> Arc = {A.Arc, B.Arc, C.Arc};
      std::array<const RasterVertex *, 3> Src = {A.Vtx, B.Vtx, C.Vtx};
      float SArea = edgeFn(Screen[0], Screen[1], Screen[2]);
      if (SArea == 0.0f)
        return;
      if (SArea < 0.0f) {
        std::swap(Screen[1], Screen[2]);
        std::swap(InvW[1], InvW[2]);
        std::swap(Depth[1], Depth[2]);
        std::swap(Edge[1], Edge[2]);
        std::swap(Arc[1], Arc[2]);
        std::swap(Src[1], Src[2]);
      }
      auto VaryingBits = std::make_unique<SmallVector<uint32_t, 8>>();
      for (unsigned K = 0; K != 3; ++K)
        VaryingBits->append(Src[K]->Varyings.begin(), Src[K]->Varyings.end());
      ScreenTriangle ST;
      ST.Pos = Screen;
      ST.InvW = InvW;
      ST.Depth = Depth;
      ST.IsLine = IsLine;
      ST.EdgeDistance = Edge;
      ST.ArcLength = Arc;
      size_t Stride = Src[0]->Varyings.size();
      for (unsigned K = 0; K != 3; ++K)
        ST.Varyings[K] = VaryingBits->data() + K * Stride;
      // Points/lines are never culled: `VkCullModeFlags` only ever applies
      // to a "polygon" (Vulkan's "Culling" section), never to a point or
      // line primitive, so this synthetic triangle's winding carries no
      // front/back-facing meaning worth computing.
      ST.FrontFacing = true;
      ST.PrimitiveID = PrimitiveCounter++;
      ST.TargetLayer = Primitive.TargetLayer;
      ST.ViewportIndex = Primitive.ViewportIndex;
      ST.ScissorMinX = std::max<int32_t>(0, Primitive.Scissor->X);
      ST.ScissorMinY = std::max<int32_t>(0, Primitive.Scissor->Y);
      ST.ScissorMaxX = std::min<int32_t>(
          ExtentWidth, Primitive.Scissor->X +
                           static_cast<int32_t>(Primitive.Scissor->Width));
      ST.ScissorMaxY = std::min<int32_t>(
          ExtentHeight, Primitive.Scissor->Y +
                            static_cast<int32_t>(Primitive.Scissor->Height));
      ScreenTris.push_back(ST);
      TriVaryingStore.push_back(std::move(VaryingBits));
    };

    if (RasterClass == RasterPrimitiveClass::Point) {
      uint32_t PointCount = TessLink ? TessOutput.InvocationCount : Total;
      for (uint32_t J = 0; J != PointCount; ++J) {
        std::optional<PrimitiveState> Primitive = resolvePrimitiveState(J);
        if (!Primitive)
          continue;
        RasterVertex V = vertexAt(J);
        if (V.Clip[3] <= ClipEpsilon)
          continue;
        std::array<float, 2> P;
        float InvW, Depth;
        projectVertex(V, *Primitive->Viewport, P, InvW, Depth);
        constexpr float Half = 0.5f; // fixed 1-pixel point size
        QuadCorner TL{{P[0] - Half, P[1] - Half}, InvW, Depth, &V};
        QuadCorner TR{{P[0] + Half, P[1] - Half}, InvW, Depth, &V};
        QuadCorner BR{{P[0] + Half, P[1] + Half}, InvW, Depth, &V};
        QuadCorner BL{{P[0] - Half, P[1] + Half}, InvW, Depth, &V};
        pushQuadTriangle(TL, TR, BR, *Primitive, /*IsLine=*/false);
        pushQuadTriangle(TL, BR, BL, *Primitive, /*IsLine=*/false);
      }
    } else if (RasterClass == RasterPrimitiveClass::Line) {
      const RasterState &Raster = Pipeline.getRasterState();
      // (roadmap F5) The stipple pattern's arc-length parameter keeps
      // accumulating across a `LineStrip`'s consecutive segments
      // (Vulkan's "continuously stippled" rule for connected strips);
      // a `LineList`'s independent segments -- and a strip's own
      // restart boundary, which also breaks segment contiguity --
      // instead each restart at 0. `AbsLineIndices` preserves assembly
      // order (and biases each instance's segments by its own base
      // invocation), so "the next segment's first vertex is the previous
      // segment's second" is exactly the contiguity test needed, with no
      // separate restart or instance-boundary bookkeeping here.
      float ArcAccum = 0.0f;
      std::optional<uint32_t> PrevEnd;
      for (std::array<uint32_t, 2> Ln : AbsLineIndices) {
        std::optional<PrimitiveState> Primitive = resolvePrimitiveState(Ln[0]);
        if (!Primitive)
          continue;
        if (!PrevEnd || *PrevEnd != Ln[0])
          ArcAccum = 0.0f;
        PrevEnd = Ln[1];
        RasterVertex V0 = vertexAt(Ln[0]);
        RasterVertex V1 = vertexAt(Ln[1]);
        if (V0.Clip[3] <= ClipEpsilon || V1.Clip[3] <= ClipEpsilon)
          continue;
        std::array<float, 2> P0, P1;
        float InvW0, Depth0, InvW1, Depth1;
        projectVertex(V0, *Primitive->Viewport, P0, InvW0, Depth0);
        projectVertex(V1, *Primitive->Viewport, P1, InvW1, Depth1);
        float Dx = P1[0] - P0[0], Dy = P1[1] - P0[1];
        float Len = std::sqrt(Dx * Dx + Dy * Dy);
        if (Len == 0.0f)
          continue;
        float ArcEnd = ArcAccum + Len;

        if (Raster.LineMode == LineRasterizationMode::Bresenham) {
          // `Bresenham` mode walks the integer pixel grid directly
          // (`LineRasterizationMode`'s comment): the line's width is
          // never consulted, and each covered pixel becomes its own
          // 1x1 axis-aligned quad, shaded/interpolated at the line
          // parameter `T` nearest that pixel's center -- there is no
          // per-pixel width to expand, unlike the rectangular styles
          // below.
          int32_t X0 = static_cast<int32_t>(std::floor(P0[0]));
          int32_t Y0 = static_cast<int32_t>(std::floor(P0[1]));
          int32_t X1 = static_cast<int32_t>(std::floor(P1[0]));
          int32_t Y1 = static_cast<int32_t>(std::floor(P1[1]));
          int32_t StepDx = std::abs(X1 - X0), Sx = X0 < X1 ? 1 : -1;
          int32_t StepDy = -std::abs(Y1 - Y0), Sy = Y0 < Y1 ? 1 : -1;
          int32_t Err = StepDx + StepDy;
          int32_t X = X0, Y = Y0;
          for (;;) {
            std::array<float, 2> Center{X + 0.5f, Y + 0.5f};
            float T = ((Center[0] - P0[0]) * Dx + (Center[1] - P0[1]) * Dy) /
                      (Len * Len);
            T = std::clamp(T, 0.0f, 1.0f);
            RasterVertex Vt = lerpVertex(V0, V1, T, Varyings);
            float InvWt = InvW0 + (InvW1 - InvW0) * T;
            float Deptht = Depth0 + (Depth1 - Depth0) * T;
            float Arc = ArcAccum + T * Len;
            QuadCorner TL{{float(X), float(Y)}, InvWt, Deptht, &Vt, 0.0f, Arc};
            QuadCorner TR{
                {float(X + 1), float(Y)}, InvWt, Deptht, &Vt, 0.0f, Arc};
            QuadCorner BR{
                {float(X + 1), float(Y + 1)}, InvWt, Deptht, &Vt, 0.0f, Arc};
            QuadCorner BL{
                {float(X), float(Y + 1)}, InvWt, Deptht, &Vt, 0.0f, Arc};
            pushQuadTriangle(TL, TR, BR, *Primitive, /*IsLine=*/true);
            pushQuadTriangle(TL, BR, BL, *Primitive, /*IsLine=*/true);
            if (X == X1 && Y == Y1)
              break;
            int32_t E2 = 2 * Err;
            if (E2 >= StepDy) {
              Err += StepDy;
              X += Sx;
            }
            if (E2 <= StepDx) {
              Err += StepDx;
              Y += Sy;
            }
          }
        } else {
          // `Rectangular`/`RectangularSmooth`: a screen-space rectangle
          // `Raster.LineWidth` pixels wide, generalizing the fixed
          // 1-pixel-wide quad roadmap C4d built. `RectangularSmooth`
          // additionally feathers the quad 1 pixel wider than the
          // nominal width on each side so a fragment near the true
          // edge gets a fractional `EdgeDistance` to antialias against
          // (see "Smooth line antialiasing" in
          // feme/docs/FeMeGraphicsDesign.md), rather than the binary
          // in/out test `Rectangular` still uses.
          float HalfWidth = Raster.LineWidth * 0.5f;
          float Feather =
              Raster.LineMode == LineRasterizationMode::RectangularSmooth
                  ? 1.0f
                  : 0.0f;
          float HalfExtent = HalfWidth + Feather;
          std::array<float, 2> Perp{-Dy / Len * HalfExtent,
                                    Dx / Len * HalfExtent};
          QuadCorner QA{{P0[0] + Perp[0], P0[1] + Perp[1]},
                        InvW0,
                        Depth0,
                        &V0,
                        HalfExtent,
                        ArcAccum};
          QuadCorner QB{{P0[0] - Perp[0], P0[1] - Perp[1]},
                        InvW0,
                        Depth0,
                        &V0,
                        -HalfExtent,
                        ArcAccum};
          QuadCorner QC{{P1[0] - Perp[0], P1[1] - Perp[1]},
                        InvW1,
                        Depth1,
                        &V1,
                        -HalfExtent,
                        ArcEnd};
          QuadCorner QD{{P1[0] + Perp[0], P1[1] + Perp[1]},
                        InvW1,
                        Depth1,
                        &V1,
                        HalfExtent,
                        ArcEnd};
          pushQuadTriangle(QA, QB, QC, *Primitive, /*IsLine=*/true);
          pushQuadTriangle(QA, QC, QD, *Primitive, /*IsLine=*/true);
        }
        ArcAccum = ArcEnd;
      }
    }

    // --- Bin primitives into tiles. ---
    int32_t MinX = 0, MinY = 0;
    int32_t MaxX = static_cast<int32_t>(ExtentWidth);
    int32_t MaxY = static_cast<int32_t>(ExtentHeight);
    if (MinX >= MaxX || MinY >= MaxY)
      continue;
    int32_t TilesX = (MaxX - MinX + TileSize - 1) / TileSize;
    int32_t TilesY = (MaxY - MinY + TileSize - 1) / TileSize;
    std::vector<std::vector<uint32_t>> Bins((size_t)std::max(TilesX, 0) *
                                            std::max(TilesY, 0));

    auto tileIndex = [&](int32_t TX, int32_t TY) {
      return (size_t)TY * TilesX + TX;
    };

    for (uint32_t I = 0; I != ScreenTris.size(); ++I) {
      const ScreenTriangle &Tri = ScreenTris[I];
      if (Tri.ScissorMinX >= Tri.ScissorMaxX ||
          Tri.ScissorMinY >= Tri.ScissorMaxY)
        continue;
      float BBMinXf = std::min({Tri.Pos[0][0], Tri.Pos[1][0], Tri.Pos[2][0]});
      float BBMaxXf = std::max({Tri.Pos[0][0], Tri.Pos[1][0], Tri.Pos[2][0]});
      float BBMinYf = std::min({Tri.Pos[0][1], Tri.Pos[1][1], Tri.Pos[2][1]});
      float BBMaxYf = std::max({Tri.Pos[0][1], Tri.Pos[1][1], Tri.Pos[2][1]});
      int32_t BBMinX =
          std::max(Tri.ScissorMinX, static_cast<int32_t>(std::floor(BBMinXf)));
      int32_t BBMaxX =
          std::min(Tri.ScissorMaxX, static_cast<int32_t>(std::ceil(BBMaxXf)));
      int32_t BBMinY =
          std::max(Tri.ScissorMinY, static_cast<int32_t>(std::floor(BBMinYf)));
      int32_t BBMaxY =
          std::min(Tri.ScissorMaxY, static_cast<int32_t>(std::ceil(BBMaxYf)));
      if (BBMinX >= BBMaxX || BBMinY >= BBMaxY)
        continue;
      int32_t TX0 = (BBMinX - MinX) / TileSize;
      int32_t TX1 = (BBMaxX - 1 - MinX) / TileSize;
      int32_t TY0 = (BBMinY - MinY) / TileSize;
      int32_t TY1 = (BBMaxY - 1 - MinY) / TileSize;
      for (int32_t TY = TY0; TY <= TY1; ++TY)
        for (int32_t TX = TX0; TX <= TX1; ++TX)
          Bins[tileIndex(TX, TY)].push_back(I);
    }

    // --- Per tile: generate covered 2x2 quads, interpolate, run fragments,
    // and perform output merge (painter's-order for a color/depth/stencil
    // value one tile writes more than once; across tiles, "Tiling and
    // scheduling" in feme/docs/FeMeGraphicsDesign.md's "each tile task
    // owns disjoint attachment regions" means processing order across
    // tiles cannot change the result, which is what makes the parallel
    // dispatch below (roadmap R33, "deterministic parallel tiled
    // schedules") safe). ---
    auto processTile = [&](int32_t TX, int32_t TY) -> Error {
      ArrayRef<uint32_t> Bin = Bins[tileIndex(TX, TY)];
      if (Bin.empty())
        return Error::success();
      int32_t TileMinX = MinX + TX * TileSize;
      int32_t TileMinY = MinY + TY * TileSize;
      int32_t TileMaxX = std::min(MaxX, TileMinX + TileSize);
      int32_t TileMaxY = std::min(MaxY, TileMinY + TileSize);

      struct PendingQuad {
        uint32_t Coverage = 0; // per-lane bit: any sample covered
        std::array<uint32_t, 4> SampleMask{}; // per-lane sample bitmask
        std::array<int32_t, 4> PixelX;
        std::array<int32_t, 4> PixelY;
        uint32_t TriIdx = 0;
        uint32_t TargetLayer = 0;
        std::array<float, 4> Bary0, Bary1, Bary2;
        // (roadmap F5) Each lane's `RectangularSmooth` antialiasing
        // coverage (`1.0` for every non-line/non-smooth triangle,
        // computed from `ScreenTriangle::EdgeDistance` otherwise): an
        // extra alpha multiplier applied only when writing a line
        // primitive's color in `RectangularSmooth` mode (see the merge
        // loop below).
        std::array<float, 4> LineCoverage{1.0f, 1.0f, 1.0f, 1.0f};
      };
      std::vector<PendingQuad> Quads;
      std::vector<cpu::FemeFragmentInvocation> QuadInvocations;
      const RasterState &Raster = Pipeline.getRasterState();

      for (uint32_t TriIdx : Bin) {
        const ScreenTriangle &Tri = ScreenTris[TriIdx];
        AttachmentView DepthAttachment =
            sliceAttachmentLayer(Draw.DepthStencil.Depth, Tri.TargetLayer);
        AttachmentView StencilAttachment =
            sliceAttachmentLayer(Draw.DepthStencil.Stencil, Tri.TargetLayer);
        float BBMinXf = std::min({Tri.Pos[0][0], Tri.Pos[1][0], Tri.Pos[2][0]});
        float BBMaxXf = std::max({Tri.Pos[0][0], Tri.Pos[1][0], Tri.Pos[2][0]});
        float BBMinYf = std::min({Tri.Pos[0][1], Tri.Pos[1][1], Tri.Pos[2][1]});
        float BBMaxYf = std::max({Tri.Pos[0][1], Tri.Pos[1][1], Tri.Pos[2][1]});
        int32_t QMinX = std::max(std::max(TileMinX, Tri.ScissorMinX),
                                 static_cast<int32_t>(std::floor(BBMinXf)));
        int32_t QMaxX = std::min(std::min(TileMaxX, Tri.ScissorMaxX),
                                 static_cast<int32_t>(std::ceil(BBMaxXf)));
        int32_t QMinY = std::max(std::max(TileMinY, Tri.ScissorMinY),
                                 static_cast<int32_t>(std::floor(BBMinYf)));
        int32_t QMaxY = std::min(std::min(TileMaxY, Tri.ScissorMaxY),
                                 static_cast<int32_t>(std::ceil(BBMaxYf)));
        if (QMinX >= QMaxX || QMinY >= QMaxY)
          continue;
        // Align the 2x2 quad grid globally so adjacent tiles/primitives
        // share quad boundaries.
        int32_t QStartX = QMinX - (QMinX & 1);
        int32_t QStartY = QMinY - (QMinY & 1);

        float Area = edgeFn(Tri.Pos[0], Tri.Pos[1], Tri.Pos[2]);
        // (roadmap F5) Whether a covered sample of this triangle also
        // needs to pass the stipple pattern test: only a line
        // primitive's synthetic triangle carries a meaningful
        // `ArcLength`, and only when the pipeline actually stipples.
        bool NeedsStippleTest = Tri.IsLine && Raster.StippledLineEnable;
        for (int32_t QY = QStartY; QY < QMaxY; QY += 2) {
          for (int32_t QX = QStartX; QX < QMaxX; QX += 2) {
            PendingQuad Quad;
            Quad.TriIdx = TriIdx;
            Quad.TargetLayer = Tri.TargetLayer;
            cpu::FemeFragmentInvocation Inv{};
            bool AnyCovered = false;
            static constexpr int32_t Dx[4] = {0, 1, 0, 1};
            static constexpr int32_t Dy[4] = {0, 0, 1, 1};
            for (unsigned Lane = 0; Lane != 4; ++Lane) {
              int32_t PX = QX + Dx[Lane];
              int32_t PY = QY + Dy[Lane];
              Quad.PixelX[Lane] = PX;
              Quad.PixelY[Lane] = PY;
              bool InBounds = PX >= Tri.ScissorMinX && PX < Tri.ScissorMaxX &&
                              PY >= Tri.ScissorMinY && PY < Tri.ScissorMaxY;
              // Coverage is tested once per sample position (a fixed
              // offset within the pixel, "Fixed per-pixel sample
              // offsets" above); with one sample this is exactly the
              // pixel-center test the single-sample path always used.
              uint32_t SampleMask = 0;
              if (InBounds) {
                for (uint32_t S = 0; S != SampleCount; ++S) {
                  std::array<float, 2> Offset = (*SamplePositions)[S];
                  std::array<float, 2> P{PX + Offset[0], PY + Offset[1]};
                  // The inside test itself is evaluated in `double`
                  // (`edgeFnD`, see its own comment) so that two triangles
                  // sharing an exact edge cannot both round a shared-edge
                  // sample to a (spuriously) negative value and leave a
                  // crack -- the barycentric weights derived from this
                  // same sample below stay in `float` (this is a coverage
                  // decision, not an interpolated value).
                  double E0 = edgeFnD(Tri.Pos[1], Tri.Pos[2], P);
                  double E1 = edgeFnD(Tri.Pos[2], Tri.Pos[0], P);
                  double E2 = edgeFnD(Tri.Pos[0], Tri.Pos[1], P);
                  bool In0 =
                      E0 > 0.0 ||
                      (E0 == 0.0 && isTopLeftEdge(Tri.Pos[1], Tri.Pos[2]));
                  bool In1 =
                      E1 > 0.0 ||
                      (E1 == 0.0 && isTopLeftEdge(Tri.Pos[2], Tri.Pos[0]));
                  bool In2 =
                      E2 > 0.0 ||
                      (E2 == 0.0 && isTopLeftEdge(Tri.Pos[0], Tri.Pos[1]));
                  if (!(In0 && In1 && In2))
                    continue;
                  // (roadmap F5) The stipple test rejects an otherwise-
                  // covered sample whose position along the line
                  // (`ArcLength`, interpolated the same way as the
                  // coverage test's own barycentric weights) falls in
                  // one of `StipplePattern`'s "off" bits: bit `floor(
                  // Arc / StippleFactor) % 16` gates this exact sample,
                  // matching a real stipple's per-sample granularity.
                  if (NeedsStippleTest) {
                    float B0 = E0 / Area, B1 = E1 / Area, B2 = E2 / Area;
                    float Arc = B0 * Tri.ArcLength[0] + B1 * Tri.ArcLength[1] +
                                B2 * Tri.ArcLength[2];
                    uint32_t Bit = static_cast<uint32_t>(
                                       Arc / float(Raster.StippleFactor)) %
                                   16;
                    if (((Raster.StipplePattern >> Bit) & 1u) == 0)
                      continue;
                  }
                  SampleMask |= (1u << S);
                }
              }
              if (SampleMask) {
                Quad.Coverage |= (1u << Lane);
                AnyCovered = true;
              }
              Quad.SampleMask[Lane] = SampleMask;
              // Barycentric coordinates for shading/depth interpolation
              // are still evaluated once, at the pixel center: only the
              // coverage test itself is per-sample (see the file
              // comment above's precision scope note).
              std::array<float, 2> Center{PX + 0.5f, PY + 0.5f};
              Quad.Bary0[Lane] = edgeFn(Tri.Pos[1], Tri.Pos[2], Center) / Area;
              Quad.Bary1[Lane] = edgeFn(Tri.Pos[2], Tri.Pos[0], Center) / Area;
              Quad.Bary2[Lane] = edgeFn(Tri.Pos[0], Tri.Pos[1], Center) / Area;
              // (roadmap F5) `RectangularSmooth`'s antialiasing coverage:
              // the pixel-center perpendicular distance from the line's
              // centerline (interpolated the same way `Depth` is), turned
              // into a 0..1 falloff across the 1-pixel feather region
              // `Rectangular`'s exact-half-width quad does not have.
              // Every other triangle (points, real polygons, and lines in
              // `Rectangular`/`Bresenham` mode) keeps the default `1.0`.
              if (Tri.IsLine &&
                  Raster.LineMode == LineRasterizationMode::RectangularSmooth) {
                float B0 = Quad.Bary0[Lane], B1 = Quad.Bary1[Lane],
                      B2 = Quad.Bary2[Lane];
                float EdgeDist = B0 * Tri.EdgeDistance[0] +
                                 B1 * Tri.EdgeDistance[1] +
                                 B2 * Tri.EdgeDistance[2];
                float HalfWidth = Raster.LineWidth * 0.5f;
                Quad.LineCoverage[Lane] = std::clamp(
                    HalfWidth + 0.5f - std::abs(EdgeDist), 0.0f, 1.0f);
              }
            }
            if (!AnyCovered)
              continue;

            for (unsigned Lane = 0; Lane != 4; ++Lane) {
              float B0 = Quad.Bary0[Lane], B1 = Quad.Bary1[Lane],
                    B2 = Quad.Bary2[Lane];
              float InvW =
                  B0 * Tri.InvW[0] + B1 * Tri.InvW[1] + B2 * Tri.InvW[2];
              float Depth =
                  B0 * Tri.Depth[0] + B1 * Tri.Depth[1] + B2 * Tri.Depth[2];
              Inv.Position[Lane][0] = Quad.PixelX[Lane] + 0.5f;
              Inv.Position[Lane][1] = Quad.PixelY[Lane] + 0.5f;
              Inv.Position[Lane][2] = Depth;
              Inv.Position[Lane][3] = InvW;
              Inv.PrimitiveID[Lane] = Tri.PrimitiveID;
              Inv.SampleIndex[Lane] = 0;
              Inv.Coverage[Lane] = Quad.SampleMask[Lane];
              Inv.IsFrontFace[Lane] = Tri.FrontFacing ? 1 : 0;
              Inv.ViewportIndex[Lane] = Tri.ViewportIndex;
              Inv.ViewIndex = Draw.ViewIndex;

              if (UseEarlyDepthStencil && Quad.SampleMask[Lane]) {
                int32_t PX = Quad.PixelX[Lane], PY = Quad.PixelY[Lane];
                uint32_t Survived = 0;
                for (uint32_t S = 0; S != SampleCount; ++S) {
                  if (!((Quad.SampleMask[Lane] >> S) & 1u))
                    continue;
                  Expected<bool> Pass = testDepthStencil(
                      PipelineDepth, PipelineStencil, DepthAttachment,
                      StencilAttachment, SampleCount, Tri.FrontFacing, PX, PY,
                      S, Depth);
                  if (!Pass)
                    return Pass.takeError();
                  if (*Pass)
                    Survived |= (1u << S);
                }
                Quad.SampleMask[Lane] = Survived;
                Inv.Coverage[Lane] = Survived;
                if (Survived == 0)
                  Quad.Coverage &= ~(1u << Lane);
              }
            }
            Inv.LiveMask = 0xF;
            Inv.SideEffectMask = Quad.Coverage;
            if (Quad.Coverage == 0)
              continue;

            QuadInvocations.push_back(Inv);
            Quads.push_back(Quad);
          }
        }
      }

      if (Quads.empty())
        return Error::success();

      if (!Pipeline.hasFragmentStage()) {
        // (roadmap H2j) No fragment stage runs at all: the early
        // depth/stencil test above (unconditionally used whenever depth or
        // stencil testing is needed, since there is no fragment stage that
        // could override it, see `UseEarlyDepthStencil`) already resolved
        // final per-sample pass/fail and performed any depth/stencil
        // writes; only per-sample occlusion-query bookkeeping remains
        // (there is no color attachment to write into either -- pipeline
        // creation only allows a fragment-less pipeline when the render
        // target has none).
        if (Draw.PassedSampleCounter)
          for (uint32_t Q = 0, E = static_cast<uint32_t>(Quads.size()); Q != E;
               ++Q)
            for (unsigned Lane = 0; Lane != 4; ++Lane)
              if ((Quads[Q].Coverage >> Lane) & 1u)
                *Draw.PassedSampleCounter +=
                    llvm::popcount(QuadInvocations[Q].Coverage[Lane]);
        return Error::success();
      }

      uint32_t QuadCount = static_cast<uint32_t>(Quads.size());
      Expected<StageStorage> FSInput =
          buildStageStorage(FSSig, SignatureDirection::Input, QuadCount * 4);
      if (!FSInput)
        return FSInput.takeError();
      for (uint32_t Q = 0; Q != QuadCount; ++Q) {
        const PendingQuad &Quad = Quads[Q];
        const ScreenTriangle &Tri = ScreenTris[Quad.TriIdx];
        for (unsigned Lane = 0; Lane != 4; ++Lane) {
          float B0 = Quad.Bary0[Lane], B1 = Quad.Bary1[Lane],
                B2 = Quad.Bary2[Lane];
          uint32_t Invocation = Q * 4 + Lane;
          size_t Idx = 0;
          for (const LinkedVarying &LV : Varyings) {
            for (uint32_t Row = 0; Row != LV.RowCount; ++Row) {
              for (uint32_t C = 0; C != LV.ComponentCount; ++C, ++Idx) {
                uint32_t Bits;
                if (LV.ComponentType == SignatureComponentType::Float) {
                  float V0, V1, V2;
                  memcpy(&V0, &Tri.Varyings[0][Idx], 4);
                  memcpy(&V1, &Tri.Varyings[1][Idx], 4);
                  memcpy(&V2, &Tri.Varyings[2][Idx], 4);
                  float Value;
                  bool Perspective =
                      LV.Interpolation !=
                          SignatureInterpolationMode::NoPerspective &&
                      LV.Interpolation !=
                          SignatureInterpolationMode::NoPerspectiveCentroid &&
                      LV.Interpolation !=
                          SignatureInterpolationMode::NoPerspectiveSample;
                  if (LV.Interpolation == SignatureInterpolationMode::Flat) {
                    Value = V0;
                  } else if (Perspective) {
                    float InvW =
                        B0 * Tri.InvW[0] + B1 * Tri.InvW[1] + B2 * Tri.InvW[2];
                    float Numerator = B0 * Tri.InvW[0] * V0 +
                                      B1 * Tri.InvW[1] * V1 +
                                      B2 * Tri.InvW[2] * V2;
                    Value = Numerator / InvW;
                  } else {
                    Value = B0 * V0 + B1 * V1 + B2 * V2;
                  }
                  memcpy(&Bits, &Value, 4);
                } else {
                  Bits = Tri.Varyings[0][Idx];
                }
                FSInput->writeRaw(LV.FSElementID, C, Invocation, Bits, Row);
              }
            }
          }
        }
      }

      Expected<StageStorage> FSOutput =
          buildStageStorage(FSSig, SignatureDirection::Output, QuadCount * 4);
      if (!FSOutput)
        return FSOutput.takeError();

      cpu::FemeStageLayout FSInLayout = FSInput->layout();
      cpu::FemeStageLayout FSOutLayout = FSOutput->layout();
      std::vector<cpu::FemeFragmentResult> Results(QuadCount);

      cpu::FragmentResources FRes;
      FRes.ResourceHeap = Draw.Resources.ResourceHeap;
      FRes.BoundResources = Draw.Resources.BoundResources;
      FRes.ImageHeap = Draw.Resources.ImageHeap;
      FRes.SamplerHeap = Draw.Resources.SamplerHeap;
      FRes.RootConstants = Draw.Resources.RootConstants;
      FRes.SubpassInputHeap = Draw.SubpassInputHeap;
      FRes.InputLayout = &FSInLayout;
      FRes.Inputs = FSInput->Data.data();
      FRes.OutputLayout = &FSOutLayout;
      FRes.Outputs = FSOutput->Data.data();
      FRes.Invocations = QuadInvocations;
      FRes.Results = Results;
      const cpu::CompiledStage &FS = Pipeline.getFragmentStage();
      cpu::PreparedFragmentBatch PFB =
          cpu::PreparedFragmentBatch::create(FS.getResourceInfo(), FRes);
      if (Error E = FS.invokeFragments(PFB))
        return E;

      for (uint32_t Q = 0; Q != QuadCount; ++Q) {
        const cpu::FemeFragmentResult &Result = Results[Q];
        const PendingQuad &Quad = Quads[Q];
        AttachmentView DepthAttachment =
            sliceAttachmentLayer(Draw.DepthStencil.Depth, Quad.TargetLayer);
        AttachmentView StencilAttachment =
            sliceAttachmentLayer(Draw.DepthStencil.Stencil, Quad.TargetLayer);
        for (unsigned Lane = 0; Lane != 4; ++Lane) {
          if (!((Result.SideEffectMask >> Lane) & 1u))
            continue;
          int32_t PX = Quad.PixelX[Lane];
          int32_t PY = Quad.PixelY[Lane];

          // A late depth/stencil test/write happens here, after the
          // fragment stage ran, using its `SV_Depth`/`SV_StencilRef`
          // outputs when it wrote them (an early test already handled
          // the alternative above, per sample, and is not repeated here
          // -- see "Depth/stencil test/write setup"). Every sample this
          // lane covers is tested independently: they share the
          // fragment's one shaded color/depth candidate, but each has
          // its own stored depth/stencil value and therefore its own
          // pass/fail result.
          uint32_t PassMask = QuadInvocations[Q].Coverage[Lane];
          if (!UseEarlyDepthStencil && NeedsDepthStencil) {
            float FragDepth = QuadInvocations[Q].Position[Lane][2];
            if (FSDepthOut)
              FragDepth =
                  FSOutput->readFloat(FSDepthOut->ElementID, 0, Q * 4 + Lane);
            std::optional<uint8_t> RefOverride;
            if (FSStencilRefOut)
              RefOverride = static_cast<uint8_t>(FSOutput->readRaw(
                  FSStencilRefOut->ElementID, 0, Q * 4 + Lane));
            PassMask = 0;
            for (uint32_t S = 0; S != SampleCount; ++S) {
              if (!((QuadInvocations[Q].Coverage[Lane] >> S) & 1u))
                continue;
              Expected<bool> Pass = testDepthStencil(
                  PipelineDepth, PipelineStencil, DepthAttachment,
                  StencilAttachment, SampleCount,
                  QuadInvocations[Q].IsFrontFace[Lane] != 0, PX, PY, S,
                  FragDepth, RefOverride);
              if (!Pass)
                return Pass.takeError();
              if (*Pass)
                PassMask |= (1u << S);
            }
          }
          if (PassMask == 0)
            continue;
          if (Draw.PassedSampleCounter)
            *Draw.PassedSampleCounter += llvm::popcount(PassMask);

          for (uint32_t AttIdx = 0; AttIdx != Draw.Attachments.size();
               ++AttIdx) {
            AttachmentView Att = sliceAttachmentLayer(Draw.Attachments[AttIdx],
                                                      Quad.TargetLayer);
            if (Att.Data.empty())
              // (Roadmap E5) An unused (`VK_NULL_HANDLE`) color slot: the
              // write is discarded rather than performed.
              continue;
            if (!FSColors[AttIdx])
              // (roadmap F8) `vkCmdSetRenderingAttachmentLocations` mapped
              // no fragment output location onto this attachment: it is
              // left exactly as it was, the same "nothing to write" case
              // as an unused slot above.
              continue;
            std::array<double, 4> RGBA;
            for (unsigned C = 0; C != 4; ++C)
              RGBA[C] = FSOutput->readFloat(FSColors[AttIdx]->ElementID, C,
                                            Q * 4 + Lane);
            // (roadmap F5) `RectangularSmooth`'s antialiasing coverage
            // (`Quad.LineCoverage`, `1.0` for every non-line/non-smooth
            // triangle) multiplies into the written alpha so a partially-
            // covered edge fragment blends proportionally rather than
            // writing fully opaque or not at all.
            RGBA[3] *= Quad.LineCoverage[Lane];
            // Only attachment 0 ever has a second source color to read:
            // Vulkan requires exactly one color attachment for a pipeline
            // using a dual-source blend factor (see `FSColor1`'s own
            // comment above).
            std::array<double, 4> RGBA1{};
            if (AttIdx == 0 && FSColor1)
              for (unsigned C = 0; C != 4; ++C)
                RGBA1[C] =
                    FSOutput->readFloat(FSColor1->ElementID, C, Q * 4 + Lane);
            const BlendState &AttBlend = Pipeline.getColorBlends()[AttIdx];
            for (uint32_t S = 0; S != SampleCount; ++S) {
              if (!((PassMask >> S) & 1u))
                continue;
              size_t Off = (((size_t)PY * Att.Width + PX) * SampleCount + S) *
                           ColorElemSizes[AttIdx];
              if (Error E = mergeColor(AttBlend, Pipeline.getLogicOpEnable(),
                                       Pipeline.getLogicOp(),
                                       Pipeline.getBlendConstants(), Att.Format,
                                       RGBA, RGBA1,
                                       MutableArrayRef(Att.Data.data() + Off,
                                                       ColorElemSizes[AttIdx])))
                return E;
            }
          }
        }
      }
      return Error::success();
    };

    // "The conservative implementation completes vertex work, joins, then
    // processes tiles" ("Tiling and scheduling"): vertex work above is
    // already complete and joined by this point, so every tile below may
    // run on any worker in any order. `WorkerCount == 1` uses the plain
    // sequential row-major order every earlier roadmap step's tests
    // already assume; a higher count partitions the flat tile index space
    // across a small thread pool, each worker claiming the next
    // unprocessed tile from a shared atomic cursor. Because tiles own
    // disjoint attachment regions (no two tiles ever read or write the
    // same pixel), the result is identical regardless of which worker
    // processes which tile or in what order -- the deterministic
    // parallel schedule this milestone adds.
    if (WorkerCount <= 1) {
      for (int32_t TY = 0; TY != TilesY; ++TY)
        for (int32_t TX = 0; TX != TilesX; ++TX)
          if (Error E = processTile(TX, TY))
            return E;
    } else {
      std::atomic<int32_t> Cursor{0};
      int32_t TotalTiles = TilesX * TilesY;
      unsigned NumThreads =
          std::min<unsigned>(WorkerCount, std::max(TotalTiles, 1));
      std::mutex ErrorMutex;
      std::vector<std::string> ErrorMessages;
      std::vector<std::thread> Threads;
      Threads.reserve(NumThreads);
      for (unsigned T = 0; T != NumThreads; ++T) {
        Threads.emplace_back([&]() {
          for (;;) {
            int32_t Idx = Cursor.fetch_add(1);
            if (Idx >= TotalTiles)
              return;
            int32_t TX = Idx % TilesX, TY = Idx / TilesX;
            if (Error E = processTile(TX, TY)) {
              std::lock_guard<std::mutex> Lock(ErrorMutex);
              ErrorMessages.push_back(toString(std::move(E)));
            }
          }
        });
      }
      for (std::thread &Th : Threads)
        Th.join();
      if (!ErrorMessages.empty())
        return createStringError(inconvertibleErrorCode(), "%s",
                                 ErrorMessages.front().c_str());
    }
  }

  // --- Multisample resolve (roadmap R33): box-filter average every
  // sample of each color attachment into its resolve attachment, once
  // every draw above has run. A single-sample pipeline has nothing to
  // resolve (`ResolveAttachments` is required to be empty in that case by
  // convention -- see PreparedDraw.h). ---
  if (!Draw.ResolveAttachments.empty()) {
    for (uint32_t AttIdx = 0; AttIdx != Draw.Attachments.size(); ++AttIdx) {
      const AttachmentView &Src = Draw.Attachments[AttIdx];
      AttachmentView &Dst = Draw.ResolveAttachments[AttIdx];
      for (uint32_t Layer = 0; Layer != Src.ArrayLayers; ++Layer) {
        AttachmentView SrcLayer = sliceAttachmentLayer(Src, Layer);
        AttachmentView DstLayer = sliceAttachmentLayer(Dst, Layer);
        for (uint32_t PY = 0; PY != Src.Height; ++PY) {
          for (uint32_t PX = 0; PX != Src.Width; ++PX) {
            std::array<double, 4> Sum{};
            for (uint32_t S = 0; S != SampleCount; ++S) {
              size_t Off = (((size_t)PY * Src.Width + PX) * SampleCount + S) *
                           ColorElemSizes[AttIdx];
              std::array<double, 4> Sample{};
              if (Error E = unpackColor(SrcLayer.Format,
                                        ArrayRef(SrcLayer.Data.data() + Off,
                                                 ColorElemSizes[AttIdx]),
                                        Sample))
                return E;
              for (unsigned C = 0; C != 4; ++C)
                Sum[C] += Sample[C];
            }
            std::array<double, 4> Avg{};
            for (unsigned C = 0; C != 4; ++C)
              Avg[C] = Sum[C] / SampleCount;
            size_t DstOff =
                ((size_t)PY * Dst.Width + PX) * ColorElemSizes[AttIdx];
            if (Error E = packClearColor(
                    DstLayer.Format, Avg,
                    MutableArrayRef(DstLayer.Data.data() + DstOff,
                                    ColorElemSizes[AttIdx])))
              return E;
          }
        }
      }
    }
  }

  return Error::success();
}

} // namespace feme::graphics
