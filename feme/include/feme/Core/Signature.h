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
// component type, shape, interpolation, frequency, stream, transform-
// feedback capture buffer/offset/stride), a structural verifier for it, and
// a versioned byte-layout serialization.
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
  /// (Roadmap H29r) `gl_PrimitiveTriangleIndicesEXT`/
  /// `gl_PrimitiveLineIndicesEXT`/`gl_PrimitivePointIndicesEXT`: a mesh
  /// stage's per-primitive *vertex index list*, naming which of the
  /// workgroup's own emitted vertices each output primitive is built from.
  /// Unlike every other mesh output, this one does not live in the
  /// per-vertex/per-primitive attribute storage
  /// (`FemeMeshArgs::VertexOutputs`/`PrimitiveOutputs`) at all: it has its
  /// own flat, primitive-major `uint32_t` array
  /// (`FemeMeshArgs::PrimitiveIndices`), so `MeshOutputWrapperPass` routes
  /// a store to it by this system value rather than by
  /// `SignatureElement::Frequency`. `ComponentCount` is the topology's own
  /// vertices-per-primitive (3 for triangles, 2 for lines, 1 for points).
  /// Added at the end for the same no-renumbering reason `PointSize` was.
  PrimitiveIndices,
  /// (Roadmap H106) `gl_CullPrimitiveEXT`: a mesh stage's own per-
  /// primitive output, `true` when the workgroup wants this one
  /// primitive discarded before rasterization -- an ordinary `bool`
  /// output otherwise, stored and replicated across a primitive's own
  /// vertex slots exactly like `PrimitiveID`'s own
  /// `AuthoredPrimitiveID`/`unflattenMeshPrimitiveRow` path
  /// (`Executor.cpp`'s `PrimitiveState::Culled` reads it back once per
  /// primitive, ahead of every other per-primitive test). Added at the
  /// end for the same no-renumbering reason `PointSize`/`PrimitiveIndices`
  /// were.
  CullPrimitive,
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
  /// real matrix one row at a time) needs this flag instead. (Roadmap H9b)
  /// `feme/lib/Graphics/StageLink.cpp`'s `linkStageElements` is exactly
  /// such a consumer: it folds this flag down to an effective `RowCount`
  /// of 1 (the real, single-vertex shape a per-vertex-arrayed `Input`'s
  /// producer always has) before comparing shapes or building a
  /// `LinkedStageElement`, rather than comparing the raw, folded value.
  /// False for every element that is not this shape, including a builtin
  /// interface block's own per-member elements (e.g. `gl_in[].gl_
  /// Position`), whose `RowCount` already reflects only that member's own
  /// shape -- the outer per-vertex array dimension is peeled off before
  /// those are built and never folds into any member's `RowCount` to
  /// begin with.
  /// (Roadmap H6j) Also false for a mesh entry's own plain per-vertex/
  /// per-primitive `Output` array (e.g. a user-defined `PerVertexEXT`/
  /// `PerPrimitiveEXT` varying): unlike `Input`, that element's `RowCount`
  /// is linked, by `Location`, against the fragment stage's corresponding
  /// input, so its own per-vertex/per-primitive array dimension is peeled
  /// off (like a builtin interface block's per-member element already is,
  /// above) rather than folded into `RowCount` and flagged here -- see
  /// `CanonicalizeStagePass::run`'s own `addElements` comment for why.
  bool RowCountIsVertexArray = false;

  /// (Roadmap H101g/H101c) `RowCount`'s own instance-array/inner-row
  /// split for GLSL's "array of interface-block-instances" syntax
  /// (`layout(xfb_buffer = B, ...) out BlockName { ... } blockVar[N];`).
  /// Per the GLSL/SPIR-V transform-feedback model, each of the `N` array
  /// elements is its own independently-captured stream: array index `k`
  /// captures to `XfbBuffer + k`, not to a byte offset deeper within
  /// `XfbBuffer` itself (the packing every other multi-row element -- a
  /// real matrix, or a block member's own array member, e.g. `ivec2
  /// b[3]`, decomposed by `CanonicalizeStage.cpp`'s `addElements` into
  /// one `SignatureElement` per member -- already uses). `RowCount`
  /// itself flattens both dimensions together when the block's one
  /// member is itself a matrix (e.g. `layout(...) out Block { mat4 var;
  /// } block[3];`, `RowCount == 12`: 3 instances of a 4-row matrix, per
  /// `getStageIORowShape`'s own alternating-peel accumulation) -- so
  /// `Row` alone cannot say where one instance ends and the next begins.
  /// 0 when this element is not this shape, meaning every other
  /// `RowCount > 1` element (a genuine matrix, or a block member's own
  /// array member) really does pack every row into the same buffer; when
  /// non-zero, it is the number of *inner* rows one array instance's own
  /// member spans (1 for a plain scalar/vector member, e.g. `uvec4`; the
  /// matrix's own row count, e.g. 4 for `mat4`, otherwise), so
  /// `Executor.cpp`'s `captureTransformFeedback` recovers `(Instance,
  /// InnerRow) = (Row / XfbBufferArrayStride, Row % XfbBufferArrayStride)`
  /// and routes each row to buffer `XfbBuffer + Instance` at byte offset
  /// `InnerRow * ComponentCount * 4`, instead of packing every row into
  /// one buffer -- found via `dEQP-VK.transform_feedback.fuzz.
  /// random_geometry.all_unordered_and_instance_array.28`'s own `BlockB {
  /// uvec4 a; } blockB[3];` (`XfbBufferArrayStride == 1`, one row per
  /// instance) and `dEQP-VK.transform_feedback.fuzz.
  /// instance_array_basic_type.mat4.*`'s own `Block { mat4 var; }
  /// block[3];` (`XfbBufferArrayStride == 4`, one matrix row per
  /// instance), whose later instances'/rows' own XFB bytes were silently
  /// captured to the wrong buffer (or dropped once the destination
  /// buffer's own real, per-instance size was accounted for) instead of
  /// their own, separately-bound buffers.
  uint32_t XfbBufferArrayStride = 0;

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

  /// (Roadmap L82) True for an `Input`-direction element of a hull
  /// shader's patch-constant phase (see
  /// `feme::cpu::PatchConstantWrapperPass::lowerPatchConstantInputLoad`)
  /// that reads back one of `feme::CanonicalizeStagePass`'s own
  /// synthetic `<entry>.patchconst.capture.N` globals -- the mechanism
  /// (roadmap H4c) that threads a value the patch-constant region reads
  /// but that was computed before the one barrier the control-point
  /// phase splits at back across the two now-separate LLVM functions.
  /// Unlike an ordinary patch-constant `Input` read (which may
  /// legitimately address any control point via its own dynamic/constant
  /// `ControlPoint` operand -- that is the whole point of a patch-
  /// constant function reading `OutputPatch`/`InputPatch` data), a
  /// captured cross-barrier value was, in the original unsplit shader,
  /// simply a bare SSA re-use of a value *this exact invocation* itself
  /// already computed pre-barrier -- there is no real "which control
  /// point" question to ask at all, since the answer is always "this
  /// one". The capture's own store side (an ordinary
  /// `feme.stage.output.store` in the control-point phase, lowered by
  /// `feme::cpu::HullWrapperPass::lowerHullOutputStore`) already writes
  /// to *this* invocation's own storage slot unconditionally (an output
  /// store never addresses any other invocation's slot to begin with);
  /// but because the capture global itself is a plain scalar (no
  /// `AccessChain`/GEP survives its unindexed load/store pair --
  /// `CanonicalizeStage.cpp`'s own capture-creation code never
  /// synthesizes one), `resolveStageIOAccess` finds no dynamic-or-
  /// constant vertex index to carry on the read side either, and
  /// defaults its `ControlPoint` operand to a literal `0` -- silently
  /// making *every* invocation re-read invocation 0's own captured
  /// value instead of its own. This flag lets
  /// `lowerPatchConstantInputLoad` recognize that shape and substitute
  /// this lane's own flat invocation index in the read's `ControlPoint`
  /// operand's place, exactly mirroring the store side's own
  /// unconditional self-addressing.
  bool CapturedSelfIndex = false;

  /// (Roadmap H21a) The `VK_EXT_transform_feedback` buffer index this
  /// `Output`-direction element captures to (SPIR-V's `XfbBuffer`
  /// decoration), or `std::nullopt` if the element is not captured at all
  /// -- the common case for every element until a later roadmap H21 row
  /// wires actual capture behavior. Only meaningful for `Output`; every
  /// other direction leaves this `std::nullopt` (see `verifySignature`).
  std::optional<uint32_t> XfbBuffer;
  /// The byte offset, within one captured vertex's record in `*XfbBuffer`,
  /// this element's own captured bytes start at (SPIR-V's plain `Offset`
  /// decoration, reused here for its transform-feedback meaning rather
  /// than a struct member's memory layout). Only meaningful when
  /// `XfbBuffer` has a value.
  uint32_t XfbOffset = 0;
  /// The byte stride between consecutive captured vertices' records in
  /// `*XfbBuffer` (SPIR-V's `XfbStride` decoration) -- the same value for
  /// every element captured to the same buffer, in a real shader, but
  /// recorded per-element here rather than per-buffer to match how SPIR-V
  /// itself repeats the decoration on every one of that buffer's captured
  /// variables. Only meaningful when `XfbBuffer` has a value.
  uint32_t XfbStride = 0;
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
///    with no accompanying name is not meaningful;
///  - `Direction` and `Frequency` agree: `PatchInput`/`PatchOutput`
///    elements are `PerPatch`, and `Input`/`Output` elements are not; and
///  - (Roadmap H21a) `XfbOffset`/`XfbStride` are 0 when `XfbBuffer` is
///    `std::nullopt`, since neither is meaningful without it, and
///    `XfbBuffer` is only ever set on an `Output`-direction element (the
///    only direction `VK_EXT_transform_feedback` ever captures from).
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
/// `SignatureElement::RowCountIsVertexArray`. Version 5 appends
/// `SignatureElement::XfbBuffer`/`XfbOffset`/`XfbStride` (roadmap H21a).
/// Version 6 appends `SignatureElement::CapturedSelfIndex` (roadmap L82).
/// Version 7 appends `SignatureElement::XfbBufferArrayStride` (roadmap
/// H101g).
constexpr uint32_t SignatureAbiVersion = 7;

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
