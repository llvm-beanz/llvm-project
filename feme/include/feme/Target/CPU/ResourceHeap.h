//===- ResourceHeap.h - CPU target physical heap materialization -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares host-side helpers that prepare the ABI argument blocks
// compiled CPU-target stages consume: the compute dispatch ABI
// (`PreparedDispatch`) and roadmap R28's vertex/fragment batch ABIs
// (`PreparedVertexBatch`/`PreparedFragmentBatch`). Every one materializes the
// physical resource heap once from `ResourceInfo` plus caller-owned resources,
// then builds cheap by-value `Feme*Args` structs that borrow that prepared
// state.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_CPU_RESOURCEHEAP_H
#define FEME_TARGET_CPU_RESOURCEHEAP_H

#include "feme/Target/CPU/ResourceInfo.h"
#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/ArrayRef.h"

#include <array>
#include <cstdint>
#include <vector>

namespace feme::cpu {

/// One traditionally-bound *buffer* range's host-side descriptors, matched
/// to a `BoundResourceRange` of class `Buffer` by (Space, BaseRegister).
struct BoundResourceBinding {
  uint32_t Space = 0;
  uint32_t BaseRegister = 0;
  llvm::ArrayRef<FemeDescriptor> Descriptors;
};

/// The same, for a bound sampled/storage image range
/// (`BoundResourceClass::Image`), whose descriptors land in the image heap.
struct BoundImageBinding {
  uint32_t Space = 0;
  uint32_t BaseRegister = 0;
  llvm::ArrayRef<FemeImageDescriptor> Descriptors;
};

/// The same, for a bound sampler range (`BoundResourceClass::Sampler`).
struct BoundSamplerBinding {
  uint32_t Space = 0;
  uint32_t BaseRegister = 0;
  llvm::ArrayRef<FemeSamplerDescriptor> Descriptors;
};

/// Builds the physical resource heap a compiled stage indexes: \p Info's
/// reserved prefix, with each `BoundResourceClass::Buffer` range filled from
/// the matching \p Bindings entry, followed by \p DynamicHeap.
std::vector<FemeDescriptor>
materializeResourceHeap(const ResourceInfo &Info,
                        llvm::ArrayRef<BoundResourceBinding> Bindings,
                        llvm::ArrayRef<FemeDescriptor> DynamicHeap);

/// The image-heap counterpart of `materializeResourceHeap`: \p Info's
/// `ReservedImageHeapSize` prefix filled from the `BoundResourceClass::Image`
/// ranges, followed by \p DynamicHeap (a bindless heap the shader indexes
/// directly, which is all a DXIL-sourced shader uses).
std::vector<FemeImageDescriptor>
materializeImageHeap(const ResourceInfo &Info,
                     llvm::ArrayRef<BoundImageBinding> Bindings,
                     llvm::ArrayRef<FemeImageDescriptor> DynamicHeap);

/// The sampler-heap counterpart of `materializeResourceHeap`.
std::vector<FemeSamplerDescriptor>
materializeSamplerHeap(const ResourceInfo &Info,
                       llvm::ArrayRef<BoundSamplerBinding> Bindings,
                       llvm::ArrayRef<FemeSamplerDescriptor> DynamicHeap);

struct DispatchResources {
  llvm::ArrayRef<FemeDescriptor> ResourceHeap;
  llvm::ArrayRef<BoundResourceBinding> BoundResources;
  llvm::ArrayRef<BoundImageBinding> BoundImages;
  llvm::ArrayRef<BoundSamplerBinding> BoundSamplers;
  llvm::ArrayRef<FemeImageDescriptor> ImageHeap;
  llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
};

using EntryPointFn = void (*)(const FemeDispatchArgs *);
using VertexEntryPointFn = void (*)(const FemeVertexArgs *);
using FragmentEntryPointFn = void (*)(const FemeFragmentArgs *);
using PatchEntryPointFn = void (*)(const FemePatchArgs *);
using PatchConstantEntryPointFn = void (*)(const FemePatchConstantArgs *);
using DomainEntryPointFn = void (*)(const FemeDomainArgs *);
using GeometryEntryPointFn = void (*)(const FemeGeometryArgs *);

class PreparedDispatch {
public:
  static PreparedDispatch create(const ResourceInfo &Info,
                                 const DispatchResources &Resources,
                                 std::array<uint32_t, 3> GroupCount);

  FemeDispatchArgs argsFor(std::array<uint32_t, 3> GroupID,
                           llvm::MutableArrayRef<uint8_t> GroupShared) const;

private:
  PreparedDispatch(std::vector<FemeDescriptor> ResourceHeap,
                   std::vector<FemeImageDescriptor> ImageHeap,
                   std::vector<FemeSamplerDescriptor> SamplerHeap,
                   llvm::ArrayRef<uint8_t> RootConstants,
                   std::array<uint32_t, 3> GroupCount);

  std::vector<FemeDescriptor> ResourceHeap;
  std::vector<FemeImageDescriptor> ImageHeap;
  std::vector<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
  std::array<uint32_t, 3> GroupCount;
};

void invokeGroup(EntryPointFn EntryFn, const PreparedDispatch &Prepared,
                 std::array<uint32_t, 3> GroupID,
                 llvm::MutableArrayRef<uint8_t> GroupShared);

void runDispatch(EntryPointFn EntryFn, const ResourceInfo &Info,
                 const DispatchResources &Resources,
                 std::array<uint32_t, 3> GroupCount);

/// Caller-owned storage and invocation records for one vertex batch.
struct VertexResources {
  llvm::ArrayRef<FemeDescriptor> ResourceHeap;
  llvm::ArrayRef<BoundResourceBinding> BoundResources;
  llvm::ArrayRef<BoundImageBinding> BoundImages;
  llvm::ArrayRef<BoundSamplerBinding> BoundSamplers;
  llvm::ArrayRef<FemeImageDescriptor> ImageHeap;
  llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
  const FemeStageLayout *InputLayout = nullptr;
  const void *Inputs = nullptr;
  const FemeStageLayout *OutputLayout = nullptr;
  void *Outputs = nullptr;
  llvm::ArrayRef<FemeVertexInvocation> Invocations;
};

/// One prepared vertex batch: materialized resources plus borrowed stage
/// storage and invocation records. The caller owns the stage-storage blocks and
/// invocation array and must keep them alive through the `invokeVertices`
/// call that consumes this object.
class PreparedVertexBatch {
public:
  static PreparedVertexBatch create(const ResourceInfo &Info,
                                    const VertexResources &Resources);

  FemeVertexArgs args() const;

private:
  PreparedVertexBatch(std::vector<FemeDescriptor> ResourceHeap,
                      std::vector<FemeImageDescriptor> ImageHeap,
                      std::vector<FemeSamplerDescriptor> SamplerHeap,
                      llvm::ArrayRef<uint8_t> RootConstants,
                      const FemeStageLayout *InputLayout, const void *Inputs,
                      const FemeStageLayout *OutputLayout, void *Outputs,
                      llvm::ArrayRef<FemeVertexInvocation> Invocations);

  std::vector<FemeDescriptor> ResourceHeap;
  std::vector<FemeImageDescriptor> ImageHeap;
  std::vector<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
  FemeShaderResources ShaderResources{};
  const FemeStageLayout *InputLayout = nullptr;
  const void *Inputs = nullptr;
  const FemeStageLayout *OutputLayout = nullptr;
  void *Outputs = nullptr;
  llvm::ArrayRef<FemeVertexInvocation> Invocations;
};

/// Caller-owned storage and invocation/result records for one fragment batch.
struct FragmentResources {
  llvm::ArrayRef<FemeDescriptor> ResourceHeap;
  llvm::ArrayRef<BoundResourceBinding> BoundResources;
  llvm::ArrayRef<BoundImageBinding> BoundImages;
  llvm::ArrayRef<BoundSamplerBinding> BoundSamplers;
  llvm::ArrayRef<FemeImageDescriptor> ImageHeap;
  llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
  const FemeStageLayout *InputLayout = nullptr;
  const void *Inputs = nullptr;
  const FemeStageLayout *OutputLayout = nullptr;
  void *Outputs = nullptr;
  llvm::ArrayRef<FemeFragmentInvocation> Invocations;
  llvm::MutableArrayRef<FemeFragmentResult> Results;
  /// (roadmap F8a) One `FemeImageDescriptor` per logical input-attachment
  /// index, built fresh for every draw from the currently-bound
  /// dynamic-rendering color/depth/stencil attachments (not from
  /// `VkDescriptorSet` state) -- see `FemeShaderResources::
  /// SubpassInputHeap`'s comment. Passed straight through, unlike
  /// `ImageHeap` above: there is no compile-time-declared bound range to
  /// materialize a reserved prefix for.
  llvm::ArrayRef<FemeImageDescriptor> SubpassInputHeap;
};

/// One prepared fragment batch: materialized resources plus borrowed stage
/// storage and quad records.
class PreparedFragmentBatch {
public:
  static PreparedFragmentBatch create(const ResourceInfo &Info,
                                      const FragmentResources &Resources);

  FemeFragmentArgs args() const;

private:
  PreparedFragmentBatch(std::vector<FemeDescriptor> ResourceHeap,
                        std::vector<FemeImageDescriptor> ImageHeap,
                        std::vector<FemeSamplerDescriptor> SamplerHeap,
                        llvm::ArrayRef<uint8_t> RootConstants,
                        const FemeStageLayout *InputLayout, const void *Inputs,
                        const FemeStageLayout *OutputLayout, void *Outputs,
                        llvm::ArrayRef<FemeFragmentInvocation> Invocations,
                        llvm::MutableArrayRef<FemeFragmentResult> Results,
                        llvm::ArrayRef<FemeImageDescriptor> SubpassInputHeap);

  std::vector<FemeDescriptor> ResourceHeap;
  std::vector<FemeImageDescriptor> ImageHeap;
  std::vector<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
  FemeShaderResources ShaderResources{};
  const FemeStageLayout *InputLayout = nullptr;
  const void *Inputs = nullptr;
  const FemeStageLayout *OutputLayout = nullptr;
  void *Outputs = nullptr;
  llvm::ArrayRef<FemeFragmentInvocation> Invocations;
  llvm::MutableArrayRef<FemeFragmentResult> Results;
  llvm::ArrayRef<FemeImageDescriptor> SubpassInputHeap;
};

/// Caller-owned storage for one control-point batch (roadmap R34's
/// continuation): the control-point phase's inputs/outputs, addressed the
/// same way `VertexResources` is, batched over `OutputControlPointCount`
/// control points rather than an explicit invocation array (see
/// `FemePatchArgs`'s own comment for why a control point needs none).
struct PatchResources {
  llvm::ArrayRef<FemeDescriptor> ResourceHeap;
  llvm::ArrayRef<BoundResourceBinding> BoundResources;
  llvm::ArrayRef<BoundImageBinding> BoundImages;
  llvm::ArrayRef<BoundSamplerBinding> BoundSamplers;
  llvm::ArrayRef<FemeImageDescriptor> ImageHeap;
  llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
  const FemeStageLayout *InputLayout = nullptr;
  const void *Inputs = nullptr;
  const FemeStageLayout *OutputLayout = nullptr;
  void *Outputs = nullptr;
  uint32_t OutputControlPointCount = 0;
};

/// One prepared control-point batch: materialized resources plus borrowed
/// stage storage. The caller owns the stage-storage blocks and must keep
/// them alive through the `invokePatch` call that consumes this object.
class PreparedPatchBatch {
public:
  static PreparedPatchBatch create(const ResourceInfo &Info,
                                   const PatchResources &Resources);

  FemePatchArgs args() const;

private:
  PreparedPatchBatch(std::vector<FemeDescriptor> ResourceHeap,
                     std::vector<FemeImageDescriptor> ImageHeap,
                     std::vector<FemeSamplerDescriptor> SamplerHeap,
                     llvm::ArrayRef<uint8_t> RootConstants,
                     const FemeStageLayout *InputLayout, const void *Inputs,
                     const FemeStageLayout *OutputLayout, void *Outputs,
                     uint32_t OutputControlPointCount);

  std::vector<FemeDescriptor> ResourceHeap;
  std::vector<FemeImageDescriptor> ImageHeap;
  std::vector<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
  FemeShaderResources ShaderResources{};
  const FemeStageLayout *InputLayout = nullptr;
  const void *Inputs = nullptr;
  const FemeStageLayout *OutputLayout = nullptr;
  void *Outputs = nullptr;
  uint32_t OutputControlPointCount = 0;
};

/// Caller-owned storage for one patch-constant invocation (added after
/// roadmap R34's initial landing): the completed output control points this
/// phase reads, and the per-patch storage its tessellation-factor/patch-
/// constant writes go to. `InputPatch`/`InputPatchLayout`/
/// `InputPatchControlPointCount`, added in a further follow-up, are the
/// original, pre-control-stage input control points a patch-constant
/// function's own `InputPatch` parameter (if any) reads -- left null/zero
/// when the function declares none. See `FemePatchConstantArgs`'s own
/// comment.
struct PatchConstantResources {
  llvm::ArrayRef<FemeDescriptor> ResourceHeap;
  llvm::ArrayRef<BoundResourceBinding> BoundResources;
  llvm::ArrayRef<BoundImageBinding> BoundImages;
  llvm::ArrayRef<BoundSamplerBinding> BoundSamplers;
  llvm::ArrayRef<FemeImageDescriptor> ImageHeap;
  llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
  const FemeStageLayout *InputLayout = nullptr;
  const void *Inputs = nullptr;
  const FemeStageLayout *InputPatchLayout = nullptr;
  const void *InputPatch = nullptr;
  const FemeStageLayout *OutputLayout = nullptr;
  void *Outputs = nullptr;
  uint32_t OutputControlPointCount = 0;
  uint32_t InputPatchControlPointCount = 0;
};

/// One prepared patch-constant invocation: materialized resources plus
/// borrowed stage storage. The caller owns the stage-storage blocks and must
/// keep them alive through the `invokePatchConstant` call that consumes this
/// object.
class PreparedPatchConstantBatch {
public:
  static PreparedPatchConstantBatch
  create(const ResourceInfo &Info, const PatchConstantResources &Resources);

  FemePatchConstantArgs args() const;

private:
  PreparedPatchConstantBatch(std::vector<FemeDescriptor> ResourceHeap,
                             std::vector<FemeImageDescriptor> ImageHeap,
                             std::vector<FemeSamplerDescriptor> SamplerHeap,
                             llvm::ArrayRef<uint8_t> RootConstants,
                             const FemeStageLayout *InputLayout,
                             const void *Inputs,
                             const FemeStageLayout *InputPatchLayout,
                             const void *InputPatch,
                             const FemeStageLayout *OutputLayout, void *Outputs,
                             uint32_t OutputControlPointCount,
                             uint32_t InputPatchControlPointCount);

  std::vector<FemeDescriptor> ResourceHeap;
  std::vector<FemeImageDescriptor> ImageHeap;
  std::vector<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
  FemeShaderResources ShaderResources{};
  const FemeStageLayout *InputLayout = nullptr;
  const void *Inputs = nullptr;
  const FemeStageLayout *InputPatchLayout = nullptr;
  const void *InputPatch = nullptr;
  const FemeStageLayout *OutputLayout = nullptr;
  void *Outputs = nullptr;
  uint32_t OutputControlPointCount = 0;
  uint32_t InputPatchControlPointCount = 0;
};

/// Caller-owned storage for one domain/evaluation batch (roadmap R34's
/// continuation): the completed patch's control points and per-patch
/// tessellation factors/patch constants this batch evaluates against, the
/// tessellator-generated domain coordinates it evaluates at, and the
/// per-domain-point vertex outputs it produces. See `FemeDomainArgs`'s own
/// comment for why three input sources meet here.
struct DomainResources {
  llvm::ArrayRef<FemeDescriptor> ResourceHeap;
  llvm::ArrayRef<BoundResourceBinding> BoundResources;
  llvm::ArrayRef<BoundImageBinding> BoundImages;
  llvm::ArrayRef<BoundSamplerBinding> BoundSamplers;
  llvm::ArrayRef<FemeImageDescriptor> ImageHeap;
  llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
  const FemeStageLayout *InputLayout = nullptr;
  const void *Inputs = nullptr;
  const FemeStageLayout *PatchConstantLayout = nullptr;
  const void *PatchConstants = nullptr;
  const FemeStageLayout *OutputLayout = nullptr;
  void *Outputs = nullptr;
  llvm::ArrayRef<FemeDomainInvocation> Invocations;
  uint32_t OutputControlPointCount = 0;
};

/// One prepared domain batch: materialized resources plus borrowed stage
/// storage and domain-coordinate records. The caller owns the stage-storage
/// blocks and invocation array and must keep them alive through the
/// `invokeDomain` call that consumes this object.
class PreparedDomainBatch {
public:
  static PreparedDomainBatch create(const ResourceInfo &Info,
                                    const DomainResources &Resources);

  FemeDomainArgs args() const;

private:
  PreparedDomainBatch(std::vector<FemeDescriptor> ResourceHeap,
                      std::vector<FemeImageDescriptor> ImageHeap,
                      std::vector<FemeSamplerDescriptor> SamplerHeap,
                      llvm::ArrayRef<uint8_t> RootConstants,
                      const FemeStageLayout *InputLayout, const void *Inputs,
                      const FemeStageLayout *PatchConstantLayout,
                      const void *PatchConstants,
                      const FemeStageLayout *OutputLayout, void *Outputs,
                      llvm::ArrayRef<FemeDomainInvocation> Invocations,
                      uint32_t OutputControlPointCount);

  std::vector<FemeDescriptor> ResourceHeap;
  std::vector<FemeImageDescriptor> ImageHeap;
  std::vector<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
  FemeShaderResources ShaderResources{};
  const FemeStageLayout *InputLayout = nullptr;
  const void *Inputs = nullptr;
  const FemeStageLayout *PatchConstantLayout = nullptr;
  const void *PatchConstants = nullptr;
  const FemeStageLayout *OutputLayout = nullptr;
  void *Outputs = nullptr;
  llvm::ArrayRef<FemeDomainInvocation> Invocations;
  uint32_t OutputControlPointCount = 0;
};

/// Caller-owned storage for one geometry batch (roadmap R34's continuation,
/// closing its "geometry wrapper" open item): the assembled input
/// primitives' vertex attributes this batch reads, per-invocation output
/// scratch storage, and the flat, bounded `emit`/`cut` record storage
/// `feme::cpu::GeometryWrapperPass` lowers `StageOpKind::StreamEmit`/
/// `StreamCut` to write. See `FemeGeometryArgs`'s own comment for why output
/// is recorded this way rather than through one fixed per-invocation result
/// slot, and `feme::graphics::collectGeometryStreams`
/// (feme/include/feme/Graphics/GeometryStreamCollection.h) for how a caller
/// turns a completed batch's flat records back into real
/// `feme::graphics::GeometryStreamBuilder`s.
struct GeometryResources {
  llvm::ArrayRef<FemeDescriptor> ResourceHeap;
  llvm::ArrayRef<BoundResourceBinding> BoundResources;
  llvm::ArrayRef<BoundImageBinding> BoundImages;
  llvm::ArrayRef<BoundSamplerBinding> BoundSamplers;
  llvm::ArrayRef<FemeImageDescriptor> ImageHeap;
  llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
  const FemeStageLayout *InputLayout = nullptr;
  const void *Inputs = nullptr;
  const FemeStageLayout *OutputLayout = nullptr;
  void *Outputs = nullptr;
  llvm::ArrayRef<FemeGeometryInvocation> Invocations;
  uint32_t VerticesPerPrimitive = 0;
  uint32_t MaxVerticesPerStream = 0;
  uint32_t OutputScalarsPerVertex = 0;
  /// `PrimitiveCount * MaxVerticesPerStream * OutputScalarsPerVertex`
  /// elements, zero-initialized by the caller before `invokeGeometry`.
  llvm::MutableArrayRef<float> EmittedVertices;
  /// `PrimitiveCount` elements, zero-initialized by the caller.
  llvm::MutableArrayRef<uint32_t> EmittedVertexCounts;
  /// `PrimitiveCount * MaxVerticesPerStream` elements, zero-initialized by
  /// the caller.
  llvm::MutableArrayRef<uint8_t> StripEndsAfter;
};

/// One prepared geometry batch: materialized resources plus borrowed stage
/// and stream storage. The caller owns every referenced block and must keep
/// them alive through the `invokeGeometry` call that consumes this object.
class PreparedGeometryBatch {
public:
  static PreparedGeometryBatch create(const ResourceInfo &Info,
                                      const GeometryResources &Resources);

  FemeGeometryArgs args() const;

private:
  PreparedGeometryBatch(std::vector<FemeDescriptor> ResourceHeap,
                        std::vector<FemeImageDescriptor> ImageHeap,
                        std::vector<FemeSamplerDescriptor> SamplerHeap,
                        llvm::ArrayRef<uint8_t> RootConstants,
                        const FemeStageLayout *InputLayout, const void *Inputs,
                        const FemeStageLayout *OutputLayout, void *Outputs,
                        llvm::ArrayRef<FemeGeometryInvocation> Invocations,
                        uint32_t VerticesPerPrimitive,
                        uint32_t MaxVerticesPerStream,
                        uint32_t OutputScalarsPerVertex,
                        llvm::MutableArrayRef<float> EmittedVertices,
                        llvm::MutableArrayRef<uint32_t> EmittedVertexCounts,
                        llvm::MutableArrayRef<uint8_t> StripEndsAfter);

  std::vector<FemeDescriptor> ResourceHeap;
  std::vector<FemeImageDescriptor> ImageHeap;
  std::vector<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
  FemeShaderResources ShaderResources{};
  const FemeStageLayout *InputLayout = nullptr;
  const void *Inputs = nullptr;
  const FemeStageLayout *OutputLayout = nullptr;
  void *Outputs = nullptr;
  llvm::ArrayRef<FemeGeometryInvocation> Invocations;
  uint32_t VerticesPerPrimitive = 0;
  uint32_t MaxVerticesPerStream = 0;
  uint32_t OutputScalarsPerVertex = 0;
  llvm::MutableArrayRef<float> EmittedVertices;
  llvm::MutableArrayRef<uint32_t> EmittedVertexCounts;
  llvm::MutableArrayRef<uint8_t> StripEndsAfter;
};

} // namespace feme::cpu

#endif // FEME_TARGET_CPU_RESOURCEHEAP_H
