//===- Pipeline.h - FeMe software graphics executor pipeline ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::graphics::GraphicsPipeline, the normalized,
// immutable pipeline description the software graphics executor consumes
// (roadmap R31, "FeMeGraphics skeleton" -- see feme/docs/Roadmap.md and the
// "Normalized pipeline" section of feme/docs/FeMeGraphicsDesign.md).
//
// This is a *description*, not the executor itself: it owns the compiled
// vertex/fragment stages plus every piece of state affecting how a draw is
// executed, but implements no clip/raster/interpolation logic. Roadmap R32
// ("Basic triangle pipeline") adds the executor that walks a
// `GraphicsPipeline`/`PreparedDraw` pair; see PreparedDraw.h for the
// per-draw dynamic state counterpart.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_PIPELINE_H
#define FEME_GRAPHICS_PIPELINE_H

#include "feme/Graphics/AmplificationDispatch.h"
#include "feme/Graphics/Geometry.h"
#include "feme/Graphics/Mesh.h"
#include "feme/Graphics/PatchPipeline.h"
#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace feme::graphics {

/// How vertices are assembled into primitives. `PointList`, `LineList`,
/// `LineStrip`, `TriangleList`, `TriangleStrip`, and `TriangleFan` all have
/// an executor (roadmap R32/C4); the four `*WithAdjacency` topologies do
/// not yet, so a pipeline description must reject one of those rather
/// than silently misinterpret it. The four `*WithAdjacency` topologies
/// (roadmap R34) supply a geometry stage with each primitive's neighboring
/// vertices in addition to its own -- see `topologyHasAdjacency`/
/// `splitListPrimitiveAdjacency` below and "Geometry stages consume
/// assembled primitives plus adjacency" in feme/docs/FeMeGraphicsDesign.md.
enum class PrimitiveTopology : uint8_t {
  PointList,
  LineList,
  LineStrip,
  TriangleList,
  TriangleStrip,
  /// A fan of triangles sharing the first fetched vertex as a common
  /// pivot: primitive `i` (0-based) is `(v0, v[i+1], v[i+2])`. Part of the
  /// same "triangle class" as `TriangleList`/`TriangleStrip` (roadmap C4,
  /// `mapTopology` in feme/lib/Vulkan/GraphicsPipeline.cpp), sharing their
  /// clip/rasterize path with no rasterizer changes -- only a different
  /// per-primitive vertex-index assembly (see `feme::graphics::
  /// executeDraws`'s topology switch).
  TriangleFan,
  LineListWithAdjacency,
  LineStripWithAdjacency,
  TriangleListWithAdjacency,
  TriangleStripWithAdjacency,
  /// A list of tessellation patches
  /// (`VK_PRIMITIVE_TOPOLOGY_PATCH_LIST`/D3D's `*_CONTROL_POINT_PATCHLIST`):
  /// every `GraphicsPipeline::getTessellationState().InputControlPointCount`
  /// consecutive fetched vertices form one patch, and the fixed-function
  /// tessellator -- not this enumeration -- decides what primitives the
  /// rasterizer actually sees (roadmap H4). Only legal on a pipeline with
  /// tessellation stages, and vice versa (see `feme::graphics::
  /// executeDraws`).
  PatchList,
};

/// Whether \p Topology is one of the four "with adjacency" topologies.
bool topologyHasAdjacency(PrimitiveTopology Topology);

/// Whether \p Topology supports `primitiveRestartEnable` (roadmap H5e-b):
/// every strip/fan topology (`LineStrip`, `TriangleStrip`, `TriangleFan`,
/// and, since a geometry stage's adjacency vertices are assembled from the
/// same restartable strip, `LineStripWithAdjacency`/
/// `TriangleStripWithAdjacency`), matching `executeDraws`'s own
/// `RestartEnabled` condition. The remaining "list" topologies have no
/// notion of restarting an assembly in progress and, per
/// `VUID-VkPipelineInputAssemblyStateCreateInfo-topology-00428`/neighbors,
/// must not set `primitiveRestartEnable` (this ICD does not implement
/// `VK_EXT_primitive_topology_list_restart`, which would otherwise permit
/// it).
bool topologySupportsPrimitiveRestart(PrimitiveTopology Topology);

/// The non-adjacency topology sharing \p Topology's assembled primitives,
/// e.g. `TriangleListWithAdjacency` -> `TriangleList`. A geometry stage's
/// own primitives (the ones clipping/rasterization sees) are these;
/// adjacency vertices are visible only inside the geometry invocation. A
/// non-adjacency \p Topology maps to itself.
PrimitiveTopology stripAdjacency(PrimitiveTopology Topology);

/// One list-topology primitive's fetched indices, split into the
/// primitive's own vertices (`Primitive`, in the same order and count as
/// `stripAdjacency(Topology)`'s primitive would fetch) and, for an
/// adjacency topology, its adjacency-only vertices (`Adjacent`, empty for
/// a non-adjacency topology). Matches Vulkan/Direct3D's shared adjacency
/// vertex order: line adjacency is `(adj0, v0, v1, adj1)`; triangle
/// adjacency is `(v0, adj01, v1, adj12, v2, adj20)`.
struct SplitPrimitiveAdjacency {
  llvm::SmallVector<uint32_t, 3> Primitive;
  llvm::SmallVector<uint32_t, 3> Adjacent;
};

/// The number of vertex/index slots one *list*-topology (not strip)
/// primitive occupies: `getListPrimitiveVertexCount(PointList) == 1`,
/// `LineList == 2` (`LineListWithAdjacency == 4`), `TriangleList == 3`
/// (`TriangleListWithAdjacency == 6`). Strip topologies have no fixed
/// per-primitive count (each primitive after the first reuses vertices
/// from the last); splitting a strip topology's adjacency is a documented
/// follow-up (see `splitListPrimitiveAdjacency`'s own comment), so this
/// asserts \p Topology is not a strip topology.
uint32_t getListPrimitiveVertexCount(PrimitiveTopology Topology);

/// Splits one *list*-topology primitive's `getListPrimitiveVertexCount(
/// Topology)` fetched indices in \p FetchedIndices (which must have
/// exactly that many entries) per `SplitPrimitiveAdjacency`'s own comment.
///
/// Scope note: only list topologies are implemented here. A strip
/// topology's adjacency vertices interleave across a sliding window of
/// consecutive primitives rather than each primitive owning a disjoint
/// index range; see `splitStripPrimitiveAdjacency` below for that case.
SplitPrimitiveAdjacency
splitListPrimitiveAdjacency(PrimitiveTopology Topology,
                            llvm::ArrayRef<uint32_t> FetchedIndices);

/// The number of primitives a *strip*-topology's \p IndexCount fetched
/// indices produce, per Direct3D/Vulkan's shared sliding-window convention
/// (\p Topology must be `LineStripWithAdjacency` or
/// `TriangleStripWithAdjacency`): a line strip with adjacency advances its
/// 4-vertex window by 1 each primitive (`IndexCount - 3` primitives, 0 if
/// \p IndexCount < 4); a triangle strip with adjacency advances its
/// 6-vertex window by 2 each primitive (`(IndexCount - 4) / 2` primitives,
/// 0 if \p IndexCount < 6 or `IndexCount` is not `4 + 2 * primitiveCount`
/// for a whole number of primitives, i.e. `(IndexCount - 4)` is odd).
uint32_t getStripPrimitiveCount(PrimitiveTopology Topology,
                                uint32_t IndexCount);

/// Splits primitive \p PrimitiveIndex (< `getStripPrimitiveCount(Topology,
/// FetchedIndices.size())`) of a *strip*-topology's full \p FetchedIndices
/// per `SplitPrimitiveAdjacency`'s own comment, following the same
/// Vulkan/Direct3D vertex order `splitListPrimitiveAdjacency` documents
/// (line adjacency `(adj0, v0, v1, adj1)`; triangle adjacency `(v0, adj01,
/// v1, adj12, v2, adj20)`), just windowed rather than partitioned:
/// primitive \p PrimitiveIndex's window starts at \p PrimitiveIndex for a
/// line strip (the shared "leading vertices are 1, 2, 3, ..." convention)
/// or `2 * PrimitiveIndex` for a triangle strip ("leading vertices are 0,
/// 2, 4, ..."), matching Microsoft's "Winding Direction and Leading Vertex
/// Positions" documentation for D3D_PRIMITIVE_TOPOLOGY_*_ADJ.
///
/// \p Topology must be `LineStripWithAdjacency` or
/// `TriangleStripWithAdjacency`; a non-adjacency or list topology's strip
/// has no separate adjacency-splitting step (a plain strip's own vertex
/// fetch already produces its primitive directly) and is not accepted
/// here.
SplitPrimitiveAdjacency
splitStripPrimitiveAdjacency(PrimitiveTopology Topology,
                             llvm::ArrayRef<uint32_t> FetchedIndices,
                             uint32_t PrimitiveIndex);

/// Which primitive-facing direction, if any, is discarded before
/// rasterization. `FrontAndBack` (`VK_CULL_MODE_FRONT_AND_BACK`) discards
/// every primitive regardless of winding -- the executor still assembles
/// and clips them, since Vulkan's own model culls per triangle after
/// facing is known, but never rasterizes one.
enum class CullMode : uint8_t {
  None,
  Front,
  Back,
  FrontAndBack,
};

/// Which vertex winding order is front-facing.
enum class FrontFace : uint8_t {
  CounterClockwise,
  Clockwise,
};

/// A depth/stencil comparison function, shared by depth testing and (once
/// stencil lands, roadmap R33) stencil testing.
enum class CompareOp : uint8_t {
  Never,
  Less,
  Equal,
  LessEqual,
  Greater,
  NotEqual,
  GreaterEqual,
  Always,
};

/// How a fragment's color is combined with an attachment's existing value.
/// `Replace` (no blending) matches the scene YAML's own `blend: replace`
/// spelling ("Textual scene and image fixtures" in feme/docs/Design.md).
/// The executor's actual blend behavior is driven by `BlendState` (below,
/// part of `GraphicsPipeline`'s color-attachment state since roadmap R33);
/// this enum is retained only for the scene YAML's simple `blend: replace`
/// spelling and existing call sites that pass it directly.
enum class BlendMode : uint8_t {
  Replace,
};

/// One operand of a blend equation, matching Vulkan's `VkBlendFactor`/
/// Direct3D's `D3D12_BLEND` one-for-one, including the four dual-source
/// (`VK_BLEND_FACTOR_SRC1_*`) factors: `Src1Color`/`Src1Alpha` read the
/// fragment stage's second color output (`SV_Target0`'s `Index=1`
/// companion, `feme::graphics::executeDraws`' `FSColor1` -- see "Dual-
/// source blending" in feme/docs/FeMeGraphicsDesign.md) rather than its
/// ordinary one (`SrcColor`/`SrcAlpha`).
enum class BlendFactor : uint8_t {
  Zero,
  One,
  SrcColor,
  OneMinusSrcColor,
  DstColor,
  OneMinusDstColor,
  SrcAlpha,
  OneMinusSrcAlpha,
  DstAlpha,
  OneMinusDstAlpha,
  ConstantColor,
  OneMinusConstantColor,
  ConstantAlpha,
  OneMinusConstantAlpha,
  SrcAlphaSaturate,
  Src1Color,
  OneMinusSrc1Color,
  Src1Alpha,
  OneMinusSrc1Alpha,
};

/// How a blend equation combines its two scaled operands, matching
/// Vulkan's `VkBlendOp`/Direct3D's `D3D12_BLEND_OP`.
enum class BlendOp : uint8_t {
  Add,
  Subtract,
  ReverseSubtract,
  Min,
  Max,
};

/// A bitwise logic operation between a fragment's integer color and an
/// attachment's existing value, matching Vulkan's `VkLogicOp`/Direct3D's
/// `D3D12_LOGIC_OP`. Mutually exclusive with blending: enabling a logic op
/// disables blending for every color attachment, per both APIs.
enum class LogicOp : uint8_t {
  Clear,
  Set,
  Copy,
  CopyInverted,
  NoOp,
  Invert,
  And,
  Nand,
  Or,
  Nor,
  Xor,
  Equivalent,
  AndReverse,
  AndInverted,
  OrReverse,
  OrInverted,
};

/// One color attachment's blend/write-mask state. `WriteMask` bit 0/1/2/3
/// gate the R/G/B/A channels respectively; a cleared bit leaves that
/// channel's stored value untouched regardless of `BlendEnable`.
/// `BlendEnable == false` is the `BlendMode::Replace` equation (the
/// fragment's color entirely replaces the masked channels).
struct BlendState {
  bool BlendEnable = false;
  BlendFactor SrcColorFactor = BlendFactor::One;
  BlendFactor DstColorFactor = BlendFactor::Zero;
  BlendOp ColorOp = BlendOp::Add;
  BlendFactor SrcAlphaFactor = BlendFactor::One;
  BlendFactor DstAlphaFactor = BlendFactor::Zero;
  BlendOp AlphaOp = BlendOp::Add;
  uint8_t WriteMask = 0xF;
};

/// What a passing/failing depth or stencil test does to a bound stencil
/// attachment's value, shared by both stencil faces (`StencilFaceState`).
enum class StencilOp : uint8_t {
  Keep,
  Zero,
  Replace,
  IncrementClamp,
  DecrementClamp,
  Invert,
  IncrementWrap,
  DecrementWrap,
};

/// One face's (front- or back-facing primitive's) stencil test/update
/// state, matching Vulkan's `VkStencilOpState`/Direct3D's
/// `D3D12_DEPTH_STENCILOP_DESC` one-for-one.
struct StencilFaceState {
  CompareOp Compare = CompareOp::Always;
  StencilOp FailOp = StencilOp::Keep;
  StencilOp DepthFailOp = StencilOp::Keep;
  StencilOp PassOp = StencilOp::Keep;
  uint8_t CompareMask = 0xFF;
  uint8_t WriteMask = 0xFF;
  uint8_t Reference = 0;
};

/// Stencil test/write state for one pipeline (roadmap R33): a bound
/// `S8_UINT` attachment (`DepthStencilAttachment::Stencil` in
/// PreparedDraw.h) is required whenever `TestEnable` is set, matching
/// `DepthState`'s own attachment requirement.
struct StencilState {
  bool TestEnable = false;
  StencilFaceState Front;
  StencilFaceState Back;
};

/// Depth test/write state for one pipeline.
struct DepthState {
  bool TestEnable = false;
  bool WriteEnable = false;
  CompareOp Compare = CompareOp::Less;
};

/// How a line primitive's width is turned into covered pixels, matching
/// `VK_KHR_line_rasterization`'s `VkLineRasterizationMode` one-for-one
/// (roadmap F5). `Rectangular` generalizes the fixed 1-pixel-wide quad
/// expansion roadmap C4d built (`executeDraws`' line-topology path) to
/// `RasterState::LineWidth`'s actual value: a screen-space rectangle
/// `LineWidth` pixels wide, binary-covered exactly like a triangle.
/// `Bresenham` instead walks the integer pixel grid with Bresenham's own
/// algorithm, always exactly 1 pixel wide regardless of `LineWidth` (per
/// the spec, "the width of the line is not adjustable, and it is always
/// as if it were 1.0"). `RectangularSmooth` is `Rectangular` with a 1-pixel
/// antialiasing feather: fragments near the line's edge get fractional
/// coverage (`ScreenTriangle::EdgeDistance`) instead of a binary in/out
/// test, and that coverage multiplies into the written alpha (see
/// "Smooth line antialiasing" in feme/docs/FeMeGraphicsDesign.md).
enum class LineRasterizationMode : uint8_t {
  Rectangular,
  Bresenham,
  RectangularSmooth,
};

/// Fixed-function rasterization state that does not vary per draw.
struct RasterState {
  CullMode Cull = CullMode::None;
  FrontFace Front = FrontFace::CounterClockwise;
  /// (roadmap F5) Which of the three `VK_KHR_line_rasterization` styles a
  /// line-topology primitive is drawn with; meaningless for point/triangle
  /// primitives.
  LineRasterizationMode LineMode = LineRasterizationMode::Rectangular;
  /// (roadmap F5) A line primitive's screen-space width in pixels
  /// (`VkPipelineRasterizationStateCreateInfo::lineWidth`, or
  /// `vkCmdSetLineWidth`'s value when dynamic). Ignored entirely by
  /// `Bresenham` mode, which is always 1 pixel wide.
  float LineWidth = 1.0f;
  /// (roadmap F5) Whether a line primitive is stippled -- rejected in a
  /// per-fragment repeating on/off pattern along the line's length --
  /// per `VkPipelineRasterizationLineStateCreateInfo::stippledLineEnable`.
  bool StippledLineEnable = false;
  /// (roadmap F5) The stipple pattern's repeat factor in pixels, `[1,
  /// 256]` (`VkPipelineRasterizationLineStateCreateInfo::
  /// lineStippleFactor`, or `vkCmdSetLineStippleKHR`'s value when
  /// dynamic): each of `StipplePattern`'s 16 bits covers this many pixels
  /// of the line's length before the pattern advances to its next bit.
  uint32_t StippleFactor = 1;
  /// (roadmap F5) The 16-bit repeating on/off pattern itself
  /// (`VkPipelineRasterizationLineStateCreateInfo::lineStipplePattern`):
  /// bit 0 is tested first, at the line's starting end.
  uint16_t StipplePattern = 0xFFFF;
};

/// One attachment's format/extent identity, part of the pipeline's cache key
/// (see "Normalized pipeline"'s "attachment format classes" bullet).
struct AttachmentFormat {
  cpu::ResourceFormat Format = cpu::ResourceFormat::Unknown;
  uint32_t Width = 0;
  uint32_t Height = 0;
};

/// The normalized, immutable pipeline description the software graphics
/// executor consumes (see the file comment above): the compiled raster
/// stages, primitive topology, rasterization/depth/blend state, and the
/// attachment formats it was built against. Everything here is either
/// invariant across every draw using this pipeline or part of the pipeline
/// cache identity ("Normalized pipeline" in FeMeGraphicsDesign.md);
/// draw-varying state (vertex data, resources, viewport, draw counts) lives
/// in `PreparedDraw` instead.
///
/// Only the vertex/fragment conventional-pipeline shape is described so
/// far: tessellation, geometry, amplification/mesh and ray-tracing stages
/// are later milestones (G5-G8) with their own signature shapes
/// (FeMeGraphicsDesign.md's "Tessellation and geometry stage model" etc.),
/// and this skeleton has no test exercising them yet.
class GraphicsPipeline {
public:
  /// \p FragmentStage may be `nullptr` (roadmap H2j): a graphics pipeline
  /// whose render target has no color attachments legally omits the
  /// fragment stage entirely (`VUID-VkGraphicsPipelineCreateInfo-
  /// pStages-06894`/neighbors), running only vertex-stage clip/rasterize/
  /// early-depth-test with no per-fragment shading at all. Callers must
  /// check `hasFragmentStage()` before calling `getFragmentStage()`.
  GraphicsPipeline(std::shared_ptr<cpu::CompiledStage> VertexStage,
                   std::shared_ptr<cpu::CompiledStage> FragmentStage,
                   PrimitiveTopology Topology, RasterState Raster,
                   DepthState Depth, BlendMode Blend, uint32_t SampleCount,
                   std::vector<AttachmentFormat> Attachments,
                   StencilState Stencil = StencilState{},
                   std::vector<BlendState> ColorBlends = {BlendState{}},
                   bool LogicOpEnable = false, LogicOp Logic = LogicOp::Copy,
                   std::array<float, 4> BlendConstants = {0.0f, 0.0f, 0.0f,
                                                          0.0f},
                   bool PrimitiveRestartEnable = false);

  const cpu::CompiledStage &getVertexStage() const { return *VertexStage; }
  /// Whether this pipeline has a fragment stage at all (roadmap H2j); false
  /// for a depth/stencil-only pipeline that omitted one.
  bool hasFragmentStage() const { return FragmentStage != nullptr; }
  /// Only valid to call when `hasFragmentStage()` is true.
  const cpu::CompiledStage &getFragmentStage() const { return *FragmentStage; }
  PrimitiveTopology getTopology() const { return Topology; }
  const RasterState &getRasterState() const { return Raster; }
  const DepthState &getDepthState() const { return Depth; }
  const StencilState &getStencilState() const { return Stencil; }
  BlendMode getBlendMode() const { return Blend; }
  /// One `BlendState` per color attachment (roadmap R33's "multiple render
  /// targets"), indexed the same way `PreparedDraw::Attachments` and each
  /// fragment output `Location` are.
  llvm::ArrayRef<BlendState> getColorBlends() const { return ColorBlends; }
  bool getLogicOpEnable() const { return LogicOpEnable; }
  LogicOp getLogicOp() const { return Logic; }
  const std::array<float, 4> &getBlendConstants() const {
    return BlendConstants;
  }
  uint32_t getSampleCount() const { return SampleCount; }
  llvm::ArrayRef<AttachmentFormat> getAttachments() const {
    return Attachments;
  }
  /// Whether a strip topology's indexed draw restarts primitive assembly at
  /// the index type's all-1-bits value
  /// (`VkPipelineInputAssemblyStateCreateInfo::primitiveRestartEnable`).
  bool getPrimitiveRestartEnable() const { return PrimitiveRestartEnable; }

  /// Attaches the three compiled stages a tessellation-enabled pipeline
  /// runs between its vertex stage and rasterization -- a hull shader's
  /// control-point phase and patch-constant phase, and a domain shader --
  /// along with the fixed-function tessellator state they declare
  /// (roadmap H4). `executeDraws` then feeds each patch of a
  /// `PrimitiveTopology::PatchList` draw through
  /// `feme::graphics::runPatchPipeline` and rasterizes the domain stage's
  /// own per-domain-point outputs in place of the vertex stage's, so the
  /// domain stage -- not the vertex stage -- is what must write
  /// `SV_Position` and every varying the fragment stage consumes.
  void setTessellationStages(std::shared_ptr<cpu::CompiledStage> HullStage,
                             std::shared_ptr<cpu::CompiledStage> PatchConstant,
                             std::shared_ptr<cpu::CompiledStage> DomainStage,
                             TessellationState State);

  /// Whether this pipeline tessellates (roadmap H4). True exactly when
  /// `setTessellationStages` has been called; a pipeline for which this is
  /// true must use `PrimitiveTopology::PatchList`, and one for which it is
  /// false must not.
  bool hasTessellationStages() const { return DomainStage != nullptr; }
  /// Only valid to call when `hasTessellationStages()` is true.
  const cpu::CompiledStage &getHullStage() const { return *HullStage; }
  /// Only valid to call when `hasTessellationStages()` is true.
  const cpu::CompiledStage &getPatchConstantStage() const {
    return *PatchConstantStage;
  }
  /// Only valid to call when `hasTessellationStages()` is true.
  const cpu::CompiledStage &getDomainStage() const { return *DomainStage; }
  /// Only valid to call when `hasTessellationStages()` is true.
  const TessellationState &getTessellationState() const { return Tessellation; }

  /// Attaches the compiled geometry stage a pipeline runs between its
  /// assembled primitives (with adjacency vertices, for one of the four
  /// `*WithAdjacency` topologies) and rasterization, along with its
  /// declared shape (input/output primitive class, invocation count,
  /// maximum emitted vertex count -- roadmap H5d, mirroring
  /// `setTessellationStages`'s role for the hull/domain pair). `executeDraws`
  /// then assembles each draw's primitives, runs them through
  /// `GeometryStage`, and rasterizes the merged emitted stream's own strips
  /// in place of the vertex/domain stage's output -- the same "last
  /// pre-rasterization stage" substitution `setTessellationStages`'s own
  /// `RasterSig` chaining already established.
  void setGeometryStage(std::shared_ptr<cpu::CompiledStage> GeometryStage,
                        GeometryState State);

  /// Whether this pipeline runs a geometry stage (roadmap H5d). True
  /// exactly when `setGeometryStage` has been called.
  bool hasGeometryStages() const { return GeometryStage != nullptr; }
  /// Only valid to call when `hasGeometryStages()` is true.
  const cpu::CompiledStage &getGeometryStage() const { return *GeometryStage; }
  /// Only valid to call when `hasGeometryStages()` is true.
  const GeometryState &getGeometryState() const { return Geometry; }

  /// Attaches the compiled mesh stage (and, optionally, the task
  /// (amplification) stage that drives it) a mesh pipeline runs in place
  /// of the vertex-fetch/assembly/tessellation/geometry chain (roadmap
  /// H6e). `executeDraws` runs `TaskStage`'s own workgroups (if bound) and
  /// reads back each one's requested mesh-workgroup count through a
  /// checked `feme::graphics::AmplificationDispatchQueue` (roadmap H6d),
  /// or -- with no task stage -- treats each `PreparedDraw::MeshDraws`
  /// entry's own group count as `MeshStage`'s direct workgroup dispatch
  /// (mirroring `vkCmdDrawMeshTasksEXT`'s shape); every dispatched
  /// workgroup's completed output is assembled into a `feme::graphics::
  /// Meshlet` and merged into the same clipping/rasterization path the
  /// vertex/tessellation/geometry chain already uses. A mesh pipeline has
  /// no vertex-input/input-assembly state at all (roadmap H6f), so
  /// `setTessellationStages`/`setGeometryStage` must not also be called on
  /// a pipeline this is called on, and vice versa.
  ///
  /// \p MeshLimits bounds the mesh stage's own dispatch -- either
  /// `PreparedDraw::MeshDraws`' own direct group count (no task stage) or
  /// a bound task workgroup's own `EmitMeshTasksEXT` request (a task
  /// stage) -- and \p TaskLimits bounds `TaskStage`'s own dispatch
  /// (`MeshDraws`' group count, when a task stage is bound); both mirror
  /// `VkPhysicalDeviceMeshShaderPropertiesEXT::maxMeshWorkGroupCount`/
  /// `maxMeshWorkGroupTotalCount` and their `maxTaskWorkGroup*`
  /// counterparts (roadmap H6f advertises the real values these are
  /// constructed from; `Executor::executeDraws` used a hardcoded
  /// placeholder before this parameter existed). \p TaskLimits and
  /// \p MaxTaskPayloadBytes are unused, and may be left default-
  /// constructed/zero, when \p TaskStage is null.
  ///
  /// \p MaxTaskPayloadBytes (roadmap H6c-a-b) bounds `TaskStage`'s own
  /// payload storage (`feme::graphics::TaskPayloadBuilder`,
  /// `Executor::executeDraws`), mirroring
  /// `VkPhysicalDeviceMeshShaderPropertiesEXT::maxTaskPayloadSize` the
  /// same way \p MeshLimits/\p TaskLimits mirror their own properties.
  void setMeshStage(std::shared_ptr<cpu::CompiledStage> TaskStage,
                    std::shared_ptr<cpu::CompiledStage> MeshStage,
                    MeshState State, AmplificationDispatchLimits MeshLimits,
                    AmplificationDispatchLimits TaskLimits = {},
                    uint32_t MaxTaskPayloadBytes = 0);

  /// Whether this pipeline runs a mesh stage (roadmap H6e). True exactly
  /// when `setMeshStage` has been called.
  bool hasMeshStages() const { return MeshStage != nullptr; }
  /// Only valid to call when `hasMeshStages()` is true.
  const cpu::CompiledStage &getMeshStage() const { return *MeshStage; }
  /// Whether a task (amplification) stage was bound alongside the mesh
  /// stage. Only valid to call when `hasMeshStages()` is true.
  bool hasTaskStage() const { return TaskStage != nullptr; }
  /// Only valid to call when `hasTaskStage()` is true.
  const cpu::CompiledStage &getTaskStage() const { return *TaskStage; }
  /// Only valid to call when `hasMeshStages()` is true.
  const MeshState &getMeshState() const { return Mesh; }
  /// Only valid to call when `hasMeshStages()` is true. See `setMeshStage`'s
  /// own comment.
  const AmplificationDispatchLimits &getMeshDispatchLimits() const {
    return MeshLimits;
  }
  /// Only valid to call when `hasTaskStage()` is true. See `setMeshStage`'s
  /// own comment.
  const AmplificationDispatchLimits &getTaskDispatchLimits() const {
    return TaskLimits;
  }
  /// Only valid to call when `hasTaskStage()` is true. See `setMeshStage`'s
  /// own comment.
  uint32_t getMaxTaskPayloadBytes() const { return MaxTaskPayloadBytes; }

private:
  std::shared_ptr<cpu::CompiledStage> VertexStage;
  std::shared_ptr<cpu::CompiledStage> FragmentStage;
  std::shared_ptr<cpu::CompiledStage> HullStage;
  std::shared_ptr<cpu::CompiledStage> PatchConstantStage;
  std::shared_ptr<cpu::CompiledStage> DomainStage;
  TessellationState Tessellation;
  std::shared_ptr<cpu::CompiledStage> GeometryStage;
  GeometryState Geometry;
  std::shared_ptr<cpu::CompiledStage> TaskStage;
  std::shared_ptr<cpu::CompiledStage> MeshStage;
  MeshState Mesh;
  AmplificationDispatchLimits MeshLimits;
  AmplificationDispatchLimits TaskLimits;
  uint32_t MaxTaskPayloadBytes = 0;
  PrimitiveTopology Topology;
  RasterState Raster;
  DepthState Depth;
  BlendMode Blend;
  uint32_t SampleCount;
  std::vector<AttachmentFormat> Attachments;
  StencilState Stencil;
  std::vector<BlendState> ColorBlends;
  bool LogicOpEnable;
  LogicOp Logic;
  std::array<float, 4> BlendConstants;
  bool PrimitiveRestartEnable;
};

} // namespace feme::graphics

#endif // FEME_GRAPHICS_PIPELINE_H
