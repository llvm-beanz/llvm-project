//===- Signature.h - Source-independent signature reflection --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the signature reflection data model described by the
// "Signature reflection" section of feme/docs/FeMeGraphicsDesign.md: a
// source-independent record of an entry point's input and output signature
// elements (element ID, direction, location, semantic, system value,
// component type, shape, interpolation, frequency, stream), a structural
// verifier for it, and a versioned byte-layout serialization.
//
// This is roadmap R17. It intentionally stops at the model itself: DXIL's
// `!dx.entryPoints` rows and SPIR-V's `Input`/`Output` variables are not yet
// converted into it (that is R18/R19). Canonical stage operations
// (`feme.stage.input.load` and peers) refer to elements by the stable
// `ElementID` this model assigns, rather than embedding semantic strings in
// every operation, which is also why `ElementID` is a plain integer and not
// derived from the semantic name.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_CORE_SIGNATURE_H
#define FEME_CORE_SIGNATURE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace feme {

/// Where a signature element sits in an entry point's data flow.
enum class SignatureDirection : uint8_t {
  Input,
  Output,
  PatchInput,
  PatchOutput,
};

/// A source-independent system value, i.e. a signature element whose value
/// the pipeline supplies or consumes rather than one that is a plain user
/// varying. Covers the vertex and fragment builtins the design's "Builtins
/// and system values" section names; later milestones extend this set for
/// tessellation, geometry, mesh and ray stages.
enum class SignatureSystemValue : uint8_t {
  /// Not a system value: an ordinary user varying, identified by
  /// `SignatureElement::Location` (and, for Direct3D linkage,
  /// `SignatureElement::SemanticName`/`SemanticIndex`) instead.
  None,
  Position,
  ClipDistance,
  CullDistance,
  VertexID,
  InstanceID,
  BaseVertex,
  BaseInstance,
  DrawID,
  PrimitiveID,
  IsFrontFace,
  SampleIndex,
  Coverage,
  IsHelperLane,
  Depth,
  StencilRef,
  RenderTargetArrayIndex,
  ViewportArrayIndex,
  /// A hull shader's patch-constant output: one outer (edge) tessellation
  /// factor. `SignatureElement::RowCount` gives how many of these the patch
  /// declares (2 for an isoline domain, 3 for a triangle domain, 4 for a
  /// quad domain), matching "outer ... tessellation levels" in
  /// "Tessellation and geometry stage model"
  /// (feme/docs/FeMeGraphicsDesign.md).
  TessFactorEdge,
  /// SPIR-V/GLSL's spelling of `TessFactorEdge` (`gl_TessLevelOuter`).
  TessLevelOuter = TessFactorEdge,
  /// A hull shader's patch-constant output: one inner tessellation factor
  /// (`RowCount` is 0 for an isoline domain, which has none; 1 for a
  /// triangle domain; 2 for a quad domain).
  TessFactorInside,
  /// SPIR-V/GLSL's spelling of `TessFactorInside` (`gl_TessLevelInner`).
  TessLevelInner = TessFactorInside,
  /// The domain/evaluation stage's own generated-coordinate input: the
  /// tessellator's per-invocation (u, v[, w]) domain location (`RowCount`
  /// gives the number of components the domain needs: 2 for isoline/quad,
  /// 3 for a triangle's barycentric coordinate).
  DomainLocation,
  /// SPIR-V/GLSL's spelling of `DomainLocation` (`gl_TessCoord`).
  TessCoord = DomainLocation,
  /// A hull shader control-point-phase output's invocation index: which
  /// output control point of the patch the current invocation is
  /// producing, distinct from `VertexID` because a hull shader may declare
  /// a different output control point count than its input.
  OutputControlPointID,
  /// SPIR-V/GLSL's spelling of `OutputControlPointID` (`gl_InvocationID`).
  InvocationID = OutputControlPointID,
  /// SPIR-V/GLSL's `gl_PatchVerticesIn`: how many input control points the
  /// current patch contains.
  PatchVertices,
  /// (Roadmap H2) `gl_ViewIndex`: the multiview render-pass instance view
  /// a vertex/fragment invocation is running for, an *input* readable by
  /// either stage (unlike `RenderTargetArrayIndex`, a vertex/geometry
  /// *output*). Sourced from the draw's own current view index
  /// (`feme::graphics::PreparedDraw::ViewIndex`), not from per-invocation
  /// data, so every invocation of one draw sees the same value.
  ViewIndex,
  /// (roadmap H7e) `gl_PointSize`: the last pre-rasterization stage's own
  /// vertex output giving the derived, screen-space diameter (in pixels)
  /// of a point-topology primitive's own quad expansion. Always an
  /// output; a point-topology draw is the only consumer, so the value is
  /// meaningless for any other primitive topology. Added at the end,
  /// not alphabetically/thematically alongside `Position`, to avoid
  /// renumbering every later enumerator -- several `.ll` regression
  /// tests (e.g. `feme/test/Tools/feme-render/*.test`) embed a
  /// `SignatureSystemValue`'s raw numeric value in a hand-encoded,
  /// pre-serialized signature-metadata byte blob, which a renumbering
  /// would silently desync from this enum.
  PointSize,
  // Keep last: the number of system values, for range checks.
  NumSystemValues,
};

/// A signature element's logical scalar type, independent of its bit width
/// (recorded separately in `SignatureElement::BitWidth`).
enum class SignatureComponentType : uint8_t {
  Float,
  SInt,
  UInt,
  Bool,
};

/// How a fragment-stage input is interpolated across a primitive. The
/// enumerators pair a base mode (flat, perspective, no-perspective) with an
/// optional sampling qualifier (centroid, sample), matching DXIL's
/// `InterpolationMode` kinds so that DXIL import (R18) can map onto this
/// enumeration directly instead of re-deriving the pairing.
enum class SignatureInterpolationMode : uint8_t {
  Flat,
  Perspective,
  PerspectiveCentroid,
  PerspectiveSample,
  NoPerspective,
  NoPerspectiveCentroid,
  NoPerspectiveSample,
};

/// How often a signature element's value changes across an invocation
/// group.
enum class SignatureFrequency : uint8_t {
  PerVertex,
  PerPrimitive,
  PerPatch,
  PerSample,
};

/// One row of an entry point's signature: everything canonical stage
/// operations and cross-stage linkage need to retain both source identity
/// and executable linkage for a single element (see "Signature reflection"
/// in feme/docs/FeMeGraphicsDesign.md).
struct SignatureElement {
  /// A stable numeric ID, unique within one entry point's `EntrySignature`.
  /// Canonical stage operations refer to elements by this ID rather than by
  /// semantic string; import metadata separately maps IDs back to DXIL
  /// signature rows/columns or SPIR-V interface variables.
  uint32_t ElementID = 0;

  SignatureDirection Direction = SignatureDirection::Input;

  /// The API-neutral user-varying location Vulkan links by, or
  /// `std::nullopt` for an element with no location (a pure system value,
  /// or one only Direct3D's semantic-based linkage identifies).
  std::optional<uint32_t> Location;

  /// The dual-source-blend index (SPIR-V/GLSL's `Index` layout qualifier,
  /// Vulkan's own dual-source-blend model): 0 for every ordinary output,
  /// or 1 for the second color an `Index=1`-decorated fragment output at
  /// the same `Location` as an `Index=0` one supplies
  /// (`VK_BLEND_FACTOR_SRC1_*`'s "second color", see "Dual-source
  /// blending" in feme/docs/FeMeGraphicsDesign.md). Only meaningful for a
  /// fragment stage's `Output`-direction elements with a `Location`;
  /// every other element leaves this at its default.
  uint32_t Index = 0;

  /// The DXIL semantic name, or empty if the element has no source-visible
  /// semantic (e.g. it was authored directly against a location). Direct3D
  /// links compatible signatures by semantic name/index; `SemanticIndex` is
  /// only meaningful when this is non-empty.
  std::string SemanticName;
  uint32_t SemanticIndex = 0;

  SignatureSystemValue SystemValue = SignatureSystemValue::None;

  SignatureComponentType ComponentType = SignatureComponentType::Float;
  /// The element's logical bit width (e.g. 16, 32, 64), independent of
  /// `ComponentType`.
  uint32_t BitWidth = 32;

  /// The first of the up to 4 register components this element occupies.
  uint32_t FirstComponent = 0;
  /// The number of contiguous components starting at `FirstComponent`,
  /// i.e. `FirstComponent + ComponentCount <= 4`.
  uint32_t ComponentCount = 1;
  /// The number of rows (matrix rows, or an array element count) this
  /// element spans.
  uint32_t RowCount = 1;

  /// (Roadmap H5f) Whether `RowCount` above actually counts a per-vertex
  /// array's own extent (a geometry entry's `gl_in[]`-shaped `Input`, e.g.
  /// 3 for a triangle) rather than a real matrix's row count. The two are
  /// structurally identical in SPIR-V/LLVM IR -- both an `ArrayType`
  /// wrapping the per-row/per-vertex value -- and `RowCount` alone cannot
  /// tell them apart; a consumer that must (e.g. canonicalizing a
  /// geometry entry's own per-vertex addressing, as opposed to widening a
  /// real matrix one row at a time) needs this flag instead. False for
  /// every element that is not this shape, including a builtin interface
  /// block's own per-member elements (e.g. `gl_in[].gl_Position`), whose
  /// `RowCount` already reflects only that member's own shape -- the
  /// outer per-vertex array dimension is peeled off before those are
  /// built and never folds into any member's `RowCount` to begin with.
  /// (Roadmap H6j) Also false for a mesh entry's own plain per-vertex/
  /// per-primitive `Output` array (e.g. a user-defined `PerVertexEXT`/
  /// `PerPrimitiveEXT` varying): unlike `Input`, that element's `RowCount`
  /// is linked, by `Location`, against the fragment stage's corresponding
  /// input, so its own per-vertex/per-primitive array dimension is peeled
  /// off (like a builtin interface block's per-member element already is,
  /// above) rather than folded into `RowCount` and flagged here -- see
  /// `CanonicalizeStagePass::run`'s own `addElements` comment for why.
  bool RowCountIsVertexArray = false;

  SignatureInterpolationMode Interpolation =
      SignatureInterpolationMode::Perspective;
  SignatureFrequency Frequency = SignatureFrequency::PerVertex;

  /// The geometry-stage output stream this element belongs to (see
  /// `feme::graphics::GeometryStreamBuilder`, feme/include/feme/Graphics/
  /// GeometryStream.h). Always 0 for every non-geometry stage.
  uint32_t Stream = 0;

  /// Only meaningful for an `Input`-direction element of a hull shader's
  /// patch-constant phase (see `feme::cpu::PatchConstantWrapperPass`): true
  /// if the element is read from the original, pre-control-stage
  /// `InputPatch` rather than the completed `OutputPatch` the control-point
  /// phase produced. Every other stage's `Input` elements leave this false,
  /// since they have only one input source to begin with.
  bool FromInputPatch = false;
};

/// One entry point's whole signature: its input, output, patch-input and
/// patch-output elements together, distinguished by each element's own
/// `Direction`.
struct EntrySignature {
  std::vector<SignatureElement> Elements;
};

/// Checks that \p Sig is internally consistent:
///
///  - every `ElementID` is unique;
///  - `FirstComponent`/`ComponentCount`/`RowCount` describe a component
///    range that fits within one register (`FirstComponent < 4`,
///    `1 <= ComponentCount <= 4`, `FirstComponent + ComponentCount <= 4`,
///    `RowCount >= 1`);
///  - `BitWidth` is one of the widths FeMe's component types support (8,
///    16, 32, 64);
///  - `SemanticIndex` is 0 when `SemanticName` is empty, since an index
///    with no accompanying name is not meaningful; and
///  - `Direction` and `Frequency` agree: `PatchInput`/`PatchOutput`
///    elements are `PerPatch`, and `Input`/`Output` elements are not.
///
/// Every violation found is reported to \p ErrOS (if non-null); returns
/// whether \p Sig satisfied every one of them.
bool verifySignature(const EntrySignature &Sig,
                     llvm::raw_ostream *ErrOS = nullptr);

/// The current version of the `EntrySignature` byte layout. Bumped whenever
/// that layout changes incompatibly; `parseSignature` rejects any other
/// value rather than guessing at a different field order.
///
/// Version 2 appends `SignatureElement::FromInputPatch`. Version 3 appends
/// `SignatureElement::Index`. Version 4 appends
/// `SignatureElement::RowCountIsVertexArray`.
constexpr uint32_t SignatureAbiVersion = 4;

/// Serializes \p Sig to the byte layout `parseSignature` reads back: a
/// little-endian `SignatureAbiVersion`, the element count, then each
/// element's fields in declaration order (with `SemanticName` written as a
/// length-prefixed byte string).
std::vector<uint8_t> serializeSignature(const EntrySignature &Sig);

/// Parses \p Bytes as a serialized `EntrySignature`, or an `Error` if it is
/// too short, has a length inconsistent with its element or semantic-name
/// counts, declares an ABI version other than `SignatureAbiVersion`, or
/// names a `SignatureSystemValue`/`SignatureComponentType`/
/// `SignatureInterpolationMode`/`SignatureFrequency`/`SignatureDirection`
/// enumerator this build does not know.
llvm::Expected<EntrySignature> parseSignature(llvm::ArrayRef<uint8_t> Bytes);

} // namespace feme

#endif // FEME_CORE_SIGNATURE_H
