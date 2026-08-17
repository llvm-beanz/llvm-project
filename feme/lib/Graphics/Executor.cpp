//===- Executor.cpp - FeMe software graphics executor --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the "Draw flow" Executor.h describes. Roadmap R32 ("Basic
// triangle pipeline") scopes this to one triangle-list/triangle-strip draw,
// one color attachment, one viewport/scissor, and no multisampling; the
// scope decisions this file makes -- each deliberately deferred to a later
// roadmap step rather than silently approximated -- are:
//
//  - No post-transform vertex cache: every (instance, vertex-or-index) pair
//    re-runs the vertex stage, matching "the first implementation may
//    perform all vertex work before tile work" in "Draw flow".
//  - Depth/stencil testing, blending beyond `BlendMode::Replace`, and
//    multisampling are rejected rather than run (roadmap R33).
//  - Vertex/fragment stage elements are 32-bit scalars/vectors only
//    (`RowCount == 1`, `BitWidth == 32`); matrices and 16-/64-bit varyings
//    are a mechanical, on-demand addition once a test needs them.
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
#include "feme/Graphics/ImageFixture.h"
#include "feme/Graphics/Pipeline.h"
#include "feme/Graphics/PreparedDraw.h"
#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/CPU/ResourceHeap.h"
#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using namespace llvm;

namespace feme::graphics {

namespace {

//===----------------------------------------------------------------------===//
// Signature-driven stage storage
//===----------------------------------------------------------------------===//

/// Reads \p Sig's serialized `feme::EntrySignature` metadata, or an `Error`
/// if \p Stage attached none -- every stage the executor runs must have
/// been imported/authored with its signature attached (roadmap R17/R18).
Expected<EntrySignature> getStageSignature(const cpu::CompiledStage &Stage) {
  std::vector<uint8_t> Bytes = Stage.getArtifactInfo().Signature;
  if (Bytes.empty())
    return createStringError(inconvertibleErrorCode(),
                             "compiled stage has no attached signature "
                             "metadata; the executor cannot bind its "
                             "inputs/outputs");
  return parseSignature(Bytes);
}

uint32_t scalarKindFor(SignatureComponentType Ty) {
  switch (Ty) {
  case SignatureComponentType::Float:
    return static_cast<uint32_t>(cpu::StageLayoutScalarKind::Float);
  case SignatureComponentType::SInt:
    return static_cast<uint32_t>(cpu::StageLayoutScalarKind::SInt);
  case SignatureComponentType::UInt:
    return static_cast<uint32_t>(cpu::StageLayoutScalarKind::UInt);
  case SignatureComponentType::Bool:
    return static_cast<uint32_t>(cpu::StageLayoutScalarKind::Bool);
  }
  llvm_unreachable("unhandled SignatureComponentType");
}

/// Host-owned structure-of-arrays storage for one stage's input or output
/// block (`FemeVertexArgs::Inputs`/`Outputs`, or the fragment equivalent),
/// plus the dense `FemeStageLayout` describing it. Built once per draw
/// command per direction from that stage's own `EntrySignature` elements
/// (see the file comment above for the 32-bit-scalar-only scope).
struct StageStorage {
  std::vector<cpu::FemeStageElement> Elements;
  std::vector<uint8_t> Data;

  cpu::FemeStageLayout layout() const {
    cpu::FemeStageLayout L{};
    L.Elements = Elements.data();
    L.ElementCount = static_cast<uint32_t>(Elements.size());
    return L;
  }

  uint32_t readRaw(uint32_t ElementID, uint32_t Component,
                   uint32_t Invocation) const {
    const cpu::FemeStageElement &E = Elements[ElementID];
    uint64_t Off =
        E.DataOffset +
        (uint64_t)(Component - E.FirstComponent) * E.ComponentStride +
        (uint64_t)Invocation * E.InvocationStride;
    uint32_t V;
    memcpy(&V, Data.data() + Off, sizeof(uint32_t));
    return V;
  }

  void writeRaw(uint32_t ElementID, uint32_t Component, uint32_t Invocation,
                uint32_t Value) {
    const cpu::FemeStageElement &E = Elements[ElementID];
    uint64_t Off =
        E.DataOffset +
        (uint64_t)(Component - E.FirstComponent) * E.ComponentStride +
        (uint64_t)Invocation * E.InvocationStride;
    memcpy(Data.data() + Off, &Value, sizeof(uint32_t));
  }

  float readFloat(uint32_t ElementID, uint32_t Component,
                  uint32_t Invocation) const {
    uint32_t Bits = readRaw(ElementID, Component, Invocation);
    float F;
    memcpy(&F, &Bits, sizeof(float));
    return F;
  }

  void writeFloat(uint32_t ElementID, uint32_t Component, uint32_t Invocation,
                  float Value) {
    uint32_t Bits;
    memcpy(&Bits, &Value, sizeof(float));
    writeRaw(ElementID, Component, Invocation, Bits);
  }
};

/// Builds a `StageStorage` for every `Direction`-matching element of \p Sig,
/// sized for \p InvocationCount invocations. A system-value element (e.g.
/// vertex ID, `SV_Position` on a fragment *input*) gets a dense
/// `FemeStageElement` entry (system-value sourcing is baked into the
/// compiled wrapper at compile time, not read from this layout -- see
/// VertexWrapper.cpp/FragmentWrapper.cpp) but no storage, since the wrapper
/// never reads `DataOffset` for it.
Expected<StageStorage> buildStageStorage(const EntrySignature &Sig,
                                         SignatureDirection Direction,
                                         uint32_t InvocationCount) {
  StageStorage Storage;
  uint32_t MaxID = 0;
  bool Any = false;
  for (const SignatureElement &Elt : Sig.Elements) {
    if (Elt.Direction != Direction)
      continue;
    Any = true;
    MaxID = std::max(MaxID, Elt.ElementID);
  }
  if (!Any)
    return Storage;

  Storage.Elements.assign(MaxID + 1, cpu::FemeStageElement{});
  uint64_t Offset = 0;
  for (const SignatureElement &Elt : Sig.Elements) {
    if (Elt.Direction != Direction)
      continue;
    if (Elt.RowCount != 1)
      return createStringError(
          inconvertibleErrorCode(),
          "stage element %u spans %u rows; only RowCount == 1 (no "
          "matrices/arrays) is implemented yet",
          Elt.ElementID, Elt.RowCount);

    cpu::FemeStageElement &E = Storage.Elements[Elt.ElementID];
    E.ElementID = Elt.ElementID;
    E.FirstComponent = Elt.FirstComponent;
    E.ComponentCount = Elt.ComponentCount;
    E.RowCount = 1;
    E.Interpolation = static_cast<uint32_t>(Elt.Interpolation);
    E.Frequency = static_cast<uint32_t>(Elt.Frequency);
    E.SystemValue = static_cast<uint32_t>(Elt.SystemValue);
    if (Elt.SystemValue != SignatureSystemValue::None)
      E.Flags |= cpu::FEME_STAGE_ELEMENT_SYSTEM_VALUE;
    // A system-value *input* is sourced from the invocation record by the
    // compiled wrapper, not from this layout's `DataOffset` (see
    // VertexWrapper.cpp/FragmentWrapper.cpp's `lowerVertexInputLoad`/
    // `lowerFragmentInputLoad`), so it needs no storage. An output is
    // always written through stage storage regardless of `SystemValue`
    // (e.g. `SV_Position` -- see `lowerVertexOutputStore`), so only skip
    // allocating storage for an input.
    if (Elt.SystemValue != SignatureSystemValue::None &&
        Direction == SignatureDirection::Input)
      continue;
    if (Elt.BitWidth != 32)
      return createStringError(inconvertibleErrorCode(),
                               "stage element %u has a %u-bit scalar; only "
                               "32-bit elements are implemented yet",
                               Elt.ElementID, Elt.BitWidth);

    E.ScalarKind = scalarKindFor(Elt.ComponentType);
    E.BitWidth = 32;
    E.InvocationStride = 4;
    E.ComponentStride = InvocationCount * 4;
    E.RowStride = E.ComponentStride * Elt.ComponentCount;
    E.DataOffset = Offset;
    Offset += (uint64_t)Elt.ComponentCount * InvocationCount * 4;
  }
  Storage.Data.assign(Offset, 0);
  return Storage;
}

const SignatureElement *findElement(const EntrySignature &Sig,
                                    SignatureDirection Direction,
                                    SignatureSystemValue SysVal) {
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.Direction == Direction && Elt.SystemValue == SysVal)
      return &Elt;
  return nullptr;
}

const SignatureElement *findElementByLocation(const EntrySignature &Sig,
                                              SignatureDirection Direction,
                                              uint32_t Location) {
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.Direction == Direction &&
        Elt.SystemValue == SignatureSystemValue::None &&
        Elt.Location == Location)
      return &Elt;
  return nullptr;
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

/// Whether the directed edge A->B is a "top" or "left" edge of a
/// positively-wound (by `edgeFn`) triangle in pixel space -- the tie-break
/// rule ("Rasterization correctness": "top-left fill") that gives an edge
/// shared by two triangles exactly one owner.
bool isTopLeftEdge(std::array<float, 2> A, std::array<float, 2> B) {
  float Dy = B[1] - A[1];
  float Dx = B[0] - A[0];
  return (Dy == 0.0f && Dx > 0.0f) || Dy < 0.0f;
}

} // namespace

Error executeDraws(const GraphicsPipeline &Pipeline, const PreparedDraw &Draw) {
  if (Pipeline.getSampleCount() != 1)
    return createStringError(inconvertibleErrorCode(),
                             "multisampling is not yet implemented (roadmap "
                             "R33, 'Depth, stencil, blending, and "
                             "multisampling')");
  if (Pipeline.getDepthState().TestEnable ||
      Pipeline.getDepthState().WriteEnable)
    return createStringError(inconvertibleErrorCode(),
                             "depth testing/writes are not yet implemented "
                             "(roadmap R33)");
  if (Draw.Attachments.size() != 1)
    return createStringError(inconvertibleErrorCode(),
                             "exactly one color attachment is implemented "
                             "yet (roadmap R33 adds depth/stencil and "
                             "multiple render targets); got %zu",
                             Draw.Attachments.size());
  if (Pipeline.getTopology() != PrimitiveTopology::TriangleList &&
      Pipeline.getTopology() != PrimitiveTopology::TriangleStrip)
    return createStringError(inconvertibleErrorCode(),
                             "only TriangleList/TriangleStrip topologies are "
                             "implemented yet (roadmap R32)");

  const cpu::CompiledStage &VS = Pipeline.getVertexStage();
  const cpu::CompiledStage &FS = Pipeline.getFragmentStage();
  Expected<EntrySignature> VSSig = getStageSignature(VS);
  if (!VSSig)
    return VSSig.takeError();
  Expected<EntrySignature> FSSig = getStageSignature(FS);
  if (!FSSig)
    return FSSig.takeError();

  const SignatureElement *VSPosition = findElement(
      *VSSig, SignatureDirection::Output, SignatureSystemValue::Position);
  if (!VSPosition)
    return createStringError(inconvertibleErrorCode(),
                             "vertex stage does not write an SV_Position "
                             "output; the executor cannot clip/rasterize "
                             "without one");
  if (VSPosition->ComponentCount != 4)
    return createStringError(inconvertibleErrorCode(),
                             "SV_Position output must have 4 components");

  // Link every non-system-value fragment input to the vertex output at the
  // same `Location` (Vulkan-style linkage; see "Normalized pipeline").
  SmallVector<LinkedVarying, 8> Varyings;
  for (const SignatureElement &FSIn : FSSig->Elements) {
    if (FSIn.Direction != SignatureDirection::Input ||
        FSIn.SystemValue != SignatureSystemValue::None)
      continue;
    if (!FSIn.Location)
      return createStringError(inconvertibleErrorCode(),
                               "fragment input element %u has no location "
                               "to link against a vertex output",
                               FSIn.ElementID);
    const SignatureElement *VSOut = findElementByLocation(
        *VSSig, SignatureDirection::Output, *FSIn.Location);
    if (!VSOut)
      return createStringError(inconvertibleErrorCode(),
                               "fragment input location %u has no matching "
                               "vertex stage output",
                               *FSIn.Location);
    if (VSOut->ComponentCount != FSIn.ComponentCount ||
        VSOut->ComponentType != FSIn.ComponentType)
      return createStringError(inconvertibleErrorCode(),
                               "vertex output and fragment input at "
                               "location %u disagree on component "
                               "count/type",
                               *FSIn.Location);
    Varyings.push_back({VSOut->ElementID, FSIn.ElementID, FSIn.ComponentCount,
                        FSIn.ComponentType, FSIn.Interpolation});
  }

  const SignatureElement *FSColor =
      findElementByLocation(*FSSig, SignatureDirection::Output, 0);
  if (!FSColor)
    return createStringError(inconvertibleErrorCode(),
                             "fragment stage has no output at location 0 "
                             "(SV_Target0)");
  if (FSColor->ComponentCount != 4 ||
      FSColor->ComponentType != SignatureComponentType::Float)
    return createStringError(inconvertibleErrorCode(),
                             "SV_Target0 must be a 4-component "
                             "floating-point output");

  AttachmentView &Color = Draw.Attachments[0];
  Expected<uint32_t> ColorElemSize = getFixtureFormatElementSize(Color.Format);
  if (!ColorElemSize)
    return ColorElemSize.takeError();

  int32_t ScissorMinX = std::max<int32_t>(0, Draw.Scissor.X);
  int32_t ScissorMinY = std::max<int32_t>(0, Draw.Scissor.Y);
  int32_t ScissorMaxX = std::min<int32_t>(
      Color.Width, Draw.Scissor.X + static_cast<int32_t>(Draw.Scissor.Width));
  int32_t ScissorMaxY = std::min<int32_t>(
      Color.Height, Draw.Scissor.Y + static_cast<int32_t>(Draw.Scissor.Height));

  uint32_t PrimitiveCounter = 0;

  for (const DrawCommand &Cmd : Draw.Draws) {
    if (Cmd.VertexCount == 0 || Cmd.InstanceCount == 0)
      continue;

    uint32_t PerInstance = Cmd.VertexCount;
    uint32_t Total = PerInstance * Cmd.InstanceCount;

    // --- Vertex/index fetch: assemble invocation keys. ---
    std::vector<cpu::FemeVertexInvocation> Invocations(Total);
    std::vector<uint32_t> VertexIndices(Total);
    for (uint32_t Inst = 0; Inst != Cmd.InstanceCount; ++Inst) {
      for (uint32_t J = 0; J != PerInstance; ++J) {
        uint32_t Flat = Inst * PerInstance + J;
        uint32_t VertexIndex;
        if (Cmd.Indexed) {
          uint32_t IndexPos = Cmd.FirstIndex + J;
          size_t ElemSize = Draw.IndexBuffer.Type == IndexType::UInt16 ? 2 : 4;
          size_t Off = (size_t)IndexPos * ElemSize;
          if (Off + ElemSize > Draw.IndexBuffer.Data.size())
            return createStringError(inconvertibleErrorCode(),
                                     "index buffer read is out of bounds");
          uint32_t RawIndex;
          if (ElemSize == 2) {
            uint16_t V;
            memcpy(&V, Draw.IndexBuffer.Data.data() + Off, 2);
            RawIndex = V;
          } else {
            memcpy(&RawIndex, Draw.IndexBuffer.Data.data() + Off, 4);
          }
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
        uint64_t SrcOff =
            (uint64_t)Binding->Stride * VertexIndices[Flat] + Attr->Offset;
        Expected<uint32_t> CompByteSize =
            attributeComponentByteSize(Attr->Format);
        if (!CompByteSize)
          return CompByteSize.takeError();
        if (SrcOff + (uint64_t)Elt.ComponentCount * *CompByteSize >
            Binding->Data.size())
          return createStringError(inconvertibleErrorCode(),
                                   "vertex buffer read is out of bounds");
        std::array<uint32_t, 4> Bits{};
        if (Error E =
                decodeAttribute(Attr->Format, Binding->Data.data() + SrcOff,
                                Elt.ComponentCount, Elt.ComponentType, Bits))
          return E;
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

    // --- Assemble primitives, clip, viewport-transform, cull. ---
    auto vertexAt = [&](uint32_t Flat) {
      RasterVertex V;
      for (unsigned C = 0; C != 4; ++C)
        V.Clip[C] = VSOutput->readFloat(VSPosition->ElementID, C, Flat);
      V.Varyings.resize(0);
      for (const LinkedVarying &LV : Varyings)
        for (uint32_t C = 0; C != LV.ComponentCount; ++C)
          V.Varyings.push_back(VSOutput->readRaw(LV.VSElementID, C, Flat));
      return V;
    };

    SmallVector<std::array<uint32_t, 3>, 8> TriIndices;
    if (Pipeline.getTopology() == PrimitiveTopology::TriangleList) {
      for (uint32_t T = 0; T + 3 <= PerInstance; T += 3)
        TriIndices.push_back({T, T + 1, T + 2});
    } else {
      for (uint32_t T = 0; T + 3 <= PerInstance; ++T)
        TriIndices.push_back(T % 2 == 0
                                 ? std::array<uint32_t, 3>{T, T + 1, T + 2}
                                 : std::array<uint32_t, 3>{T + 1, T, T + 2});
    }

    // Screen-space triangles plus their owning varying storage, binned into
    // tiles for the deferred per-tile rasterization pass below.
    std::vector<ScreenTriangle> ScreenTris;
    std::vector<std::unique_ptr<SmallVector<uint32_t, 8>>> TriVaryingStore;

    for (uint32_t Inst = 0; Inst != Cmd.InstanceCount; ++Inst) {
      for (std::array<uint32_t, 3> Tri : TriIndices) {
        std::array<RasterVertex, 3> V = {vertexAt(Inst * PerInstance + Tri[0]),
                                         vertexAt(Inst * PerInstance + Tri[1]),
                                         vertexAt(Inst * PerInstance + Tri[2])};
        std::vector<RasterVertex> Clipped = clipTriangle(V, Varyings);
        for (size_t I = 1; I + 1 < Clipped.size(); ++I) {
          std::array<const RasterVertex *, 3> Poly = {&Clipped[0], &Clipped[I],
                                                      &Clipped[I + 1]};
          std::array<std::array<float, 2>, 3> Screen;
          std::array<float, 3> InvW, Depth;
          for (unsigned K = 0; K != 3; ++K) {
            float W = Poly[K]->Clip[3];
            float NdcX = Poly[K]->Clip[0] / W;
            float NdcY = Poly[K]->Clip[1] / W;
            float NdcZ = Poly[K]->Clip[2] / W;
            Screen[K] = {Draw.Viewport.X +
                             (NdcX * 0.5f + 0.5f) * Draw.Viewport.Width,
                         Draw.Viewport.Y + (1.0f - (NdcY * 0.5f + 0.5f)) *
                                               Draw.Viewport.Height};
            InvW[K] = 1.0f / W;
            Depth[K] = Draw.Viewport.MinDepth +
                       NdcZ * (Draw.Viewport.MaxDepth - Draw.Viewport.MinDepth);
          }

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
          if ((Cull == CullMode::Front && FrontFacing) ||
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
          ScreenTris.push_back(ST);
          TriVaryingStore.push_back(std::move(VaryingBits));
        }
      }
    }

    // --- Bin primitives into tiles. ---
    int32_t MinX = ScissorMinX, MinY = ScissorMinY;
    int32_t MaxX = ScissorMaxX, MaxY = ScissorMaxY;
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
      float BBMinXf = std::min({Tri.Pos[0][0], Tri.Pos[1][0], Tri.Pos[2][0]});
      float BBMaxXf = std::max({Tri.Pos[0][0], Tri.Pos[1][0], Tri.Pos[2][0]});
      float BBMinYf = std::min({Tri.Pos[0][1], Tri.Pos[1][1], Tri.Pos[2][1]});
      float BBMaxYf = std::max({Tri.Pos[0][1], Tri.Pos[1][1], Tri.Pos[2][1]});
      int32_t BBMinX =
          std::max(MinX, static_cast<int32_t>(std::floor(BBMinXf)));
      int32_t BBMaxX = std::min(MaxX, static_cast<int32_t>(std::ceil(BBMaxXf)));
      int32_t BBMinY =
          std::max(MinY, static_cast<int32_t>(std::floor(BBMinYf)));
      int32_t BBMaxY = std::min(MaxY, static_cast<int32_t>(std::ceil(BBMaxYf)));
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
    // and perform output merge (painter's-order, since no depth test is
    // implemented -- see the file comment above). ---
    for (int32_t TY = 0; TY != TilesY; ++TY) {
      for (int32_t TX = 0; TX != TilesX; ++TX) {
        ArrayRef<uint32_t> Bin = Bins[tileIndex(TX, TY)];
        if (Bin.empty())
          continue;
        int32_t TileMinX = MinX + TX * TileSize;
        int32_t TileMinY = MinY + TY * TileSize;
        int32_t TileMaxX = std::min(MaxX, TileMinX + TileSize);
        int32_t TileMaxY = std::min(MaxY, TileMinY + TileSize);

        struct PendingQuad {
          uint32_t Coverage = 0; // per-lane bit
          std::array<int32_t, 4> PixelX;
          std::array<int32_t, 4> PixelY;
          uint32_t TriIdx = 0;
          std::array<float, 4> Bary0, Bary1, Bary2;
        };
        std::vector<PendingQuad> Quads;
        std::vector<cpu::FemeFragmentInvocation> QuadInvocations;

        for (uint32_t TriIdx : Bin) {
          const ScreenTriangle &Tri = ScreenTris[TriIdx];
          float BBMinXf =
              std::min({Tri.Pos[0][0], Tri.Pos[1][0], Tri.Pos[2][0]});
          float BBMaxXf =
              std::max({Tri.Pos[0][0], Tri.Pos[1][0], Tri.Pos[2][0]});
          float BBMinYf =
              std::min({Tri.Pos[0][1], Tri.Pos[1][1], Tri.Pos[2][1]});
          float BBMaxYf =
              std::max({Tri.Pos[0][1], Tri.Pos[1][1], Tri.Pos[2][1]});
          int32_t QMinX =
              std::max(TileMinX, static_cast<int32_t>(std::floor(BBMinXf)));
          int32_t QMaxX =
              std::min(TileMaxX, static_cast<int32_t>(std::ceil(BBMaxXf)));
          int32_t QMinY =
              std::max(TileMinY, static_cast<int32_t>(std::floor(BBMinYf)));
          int32_t QMaxY =
              std::min(TileMaxY, static_cast<int32_t>(std::ceil(BBMaxYf)));
          if (QMinX >= QMaxX || QMinY >= QMaxY)
            continue;
          // Align the 2x2 quad grid globally so adjacent tiles/primitives
          // share quad boundaries.
          int32_t QStartX = QMinX - (QMinX & 1);
          int32_t QStartY = QMinY - (QMinY & 1);

          float Area = edgeFn(Tri.Pos[0], Tri.Pos[1], Tri.Pos[2]);
          for (int32_t QY = QStartY; QY < QMaxY; QY += 2) {
            for (int32_t QX = QStartX; QX < QMaxX; QX += 2) {
              PendingQuad Quad;
              Quad.TriIdx = TriIdx;
              cpu::FemeFragmentInvocation Inv{};
              bool AnyCovered = false;
              static constexpr int32_t Dx[4] = {0, 1, 0, 1};
              static constexpr int32_t Dy[4] = {0, 0, 1, 1};
              for (unsigned Lane = 0; Lane != 4; ++Lane) {
                int32_t PX = QX + Dx[Lane];
                int32_t PY = QY + Dy[Lane];
                Quad.PixelX[Lane] = PX;
                Quad.PixelY[Lane] = PY;
                std::array<float, 2> Center{PX + 0.5f, PY + 0.5f};
                float E0 = edgeFn(Tri.Pos[1], Tri.Pos[2], Center);
                float E1 = edgeFn(Tri.Pos[2], Tri.Pos[0], Center);
                float E2 = edgeFn(Tri.Pos[0], Tri.Pos[1], Center);
                bool In0 = E0 > 0.0f || (E0 == 0.0f &&
                                         isTopLeftEdge(Tri.Pos[1], Tri.Pos[2]));
                bool In1 = E1 > 0.0f || (E1 == 0.0f &&
                                         isTopLeftEdge(Tri.Pos[2], Tri.Pos[0]));
                bool In2 = E2 > 0.0f || (E2 == 0.0f &&
                                         isTopLeftEdge(Tri.Pos[0], Tri.Pos[1]));
                bool InBounds =
                    PX >= MinX && PX < MaxX && PY >= MinY && PY < MaxY;
                bool Covered = InBounds && In0 && In1 && In2;
                if (Covered) {
                  Quad.Coverage |= (1u << Lane);
                  AnyCovered = true;
                }
                Quad.Bary0[Lane] = E0 / Area;
                Quad.Bary1[Lane] = E1 / Area;
                Quad.Bary2[Lane] = E2 / Area;
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
                Inv.Coverage[Lane] = (Quad.Coverage >> Lane) & 1u;
                Inv.IsFrontFace[Lane] = Tri.FrontFacing ? 1 : 0;
              }
              Inv.LiveMask = 0xF;
              Inv.SideEffectMask = Quad.Coverage;

              QuadInvocations.push_back(Inv);
              Quads.push_back(Quad);
            }
          }
        }

        if (Quads.empty())
          continue;

        uint32_t QuadCount = static_cast<uint32_t>(Quads.size());
        Expected<StageStorage> FSInput =
            buildStageStorage(*FSSig, SignatureDirection::Input, QuadCount * 4);
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
                FSInput->writeRaw(LV.FSElementID, C, Invocation, Bits);
              }
            }
          }
        }

        Expected<StageStorage> FSOutput = buildStageStorage(
            *FSSig, SignatureDirection::Output, QuadCount * 4);
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
        FRes.InputLayout = &FSInLayout;
        FRes.Inputs = FSInput->Data.data();
        FRes.OutputLayout = &FSOutLayout;
        FRes.Outputs = FSOutput->Data.data();
        FRes.Invocations = QuadInvocations;
        FRes.Results = Results;
        cpu::PreparedFragmentBatch PFB =
            cpu::PreparedFragmentBatch::create(FS.getResourceInfo(), FRes);
        if (Error E = FS.invokeFragments(PFB))
          return E;

        for (uint32_t Q = 0; Q != QuadCount; ++Q) {
          const cpu::FemeFragmentResult &Result = Results[Q];
          const PendingQuad &Quad = Quads[Q];
          for (unsigned Lane = 0; Lane != 4; ++Lane) {
            if (!((Result.SideEffectMask >> Lane) & 1u))
              continue;
            int32_t PX = Quad.PixelX[Lane];
            int32_t PY = Quad.PixelY[Lane];
            std::array<double, 4> RGBA;
            for (unsigned C = 0; C != 4; ++C)
              RGBA[C] =
                  FSOutput->readFloat(FSColor->ElementID, C, Q * 4 + Lane);
            size_t Off = ((size_t)PY * Color.Width + PX) * *ColorElemSize;
            if (Error E = packClearColor(
                    Color.Format, RGBA,
                    MutableArrayRef(Color.Data.data() + Off, *ColorElemSize)))
              return E;
          }
        }
      }
    }
  }

  return Error::success();
}

} // namespace feme::graphics
