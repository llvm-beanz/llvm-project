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

struct BoundResourceBinding {
  uint32_t Space = 0;
  uint32_t BaseRegister = 0;
  llvm::ArrayRef<FemeDescriptor> Descriptors;
};

std::vector<FemeDescriptor>
materializeResourceHeap(const ResourceInfo &Info,
                        llvm::ArrayRef<BoundResourceBinding> Bindings,
                        llvm::ArrayRef<FemeDescriptor> DynamicHeap);

struct DispatchResources {
  llvm::ArrayRef<FemeDescriptor> ResourceHeap;
  llvm::ArrayRef<BoundResourceBinding> BoundResources;
  llvm::ArrayRef<FemeImageDescriptor> ImageHeap;
  llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
};

using EntryPointFn = void (*)(const FemeDispatchArgs *);
using VertexEntryPointFn = void (*)(const FemeVertexArgs *);
using FragmentEntryPointFn = void (*)(const FemeFragmentArgs *);
using PatchEntryPointFn = void (*)(const FemePatchArgs *);
using PatchConstantEntryPointFn = void (*)(const FemePatchConstantArgs *);

class PreparedDispatch {
public:
  static PreparedDispatch create(const ResourceInfo &Info,
                                 const DispatchResources &Resources,
                                 std::array<uint32_t, 3> GroupCount);

  FemeDispatchArgs argsFor(std::array<uint32_t, 3> GroupID,
                           llvm::MutableArrayRef<uint8_t> GroupShared) const;

private:
  PreparedDispatch(std::vector<FemeDescriptor> ResourceHeap,
                   llvm::ArrayRef<FemeImageDescriptor> ImageHeap,
                   llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap,
                   llvm::ArrayRef<uint8_t> RootConstants,
                   std::array<uint32_t, 3> GroupCount);

  std::vector<FemeDescriptor> ResourceHeap;
  llvm::ArrayRef<FemeImageDescriptor> ImageHeap;
  llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap;
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
                      llvm::ArrayRef<FemeImageDescriptor> ImageHeap,
                      llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap,
                      llvm::ArrayRef<uint8_t> RootConstants,
                      const FemeStageLayout *InputLayout, const void *Inputs,
                      const FemeStageLayout *OutputLayout, void *Outputs,
                      llvm::ArrayRef<FemeVertexInvocation> Invocations);

  std::vector<FemeDescriptor> ResourceHeap;
  llvm::ArrayRef<FemeImageDescriptor> ImageHeap;
  llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap;
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
  llvm::ArrayRef<FemeImageDescriptor> ImageHeap;
  llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
  const FemeStageLayout *InputLayout = nullptr;
  const void *Inputs = nullptr;
  const FemeStageLayout *OutputLayout = nullptr;
  void *Outputs = nullptr;
  llvm::ArrayRef<FemeFragmentInvocation> Invocations;
  llvm::MutableArrayRef<FemeFragmentResult> Results;
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
                        llvm::ArrayRef<FemeImageDescriptor> ImageHeap,
                        llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap,
                        llvm::ArrayRef<uint8_t> RootConstants,
                        const FemeStageLayout *InputLayout, const void *Inputs,
                        const FemeStageLayout *OutputLayout, void *Outputs,
                        llvm::ArrayRef<FemeFragmentInvocation> Invocations,
                        llvm::MutableArrayRef<FemeFragmentResult> Results);

  std::vector<FemeDescriptor> ResourceHeap;
  llvm::ArrayRef<FemeImageDescriptor> ImageHeap;
  llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
  FemeShaderResources ShaderResources{};
  const FemeStageLayout *InputLayout = nullptr;
  const void *Inputs = nullptr;
  const FemeStageLayout *OutputLayout = nullptr;
  void *Outputs = nullptr;
  llvm::ArrayRef<FemeFragmentInvocation> Invocations;
  llvm::MutableArrayRef<FemeFragmentResult> Results;
};

/// Caller-owned storage for one control-point batch (roadmap R34's
/// continuation): the control-point phase's inputs/outputs, addressed the
/// same way `VertexResources` is, batched over `OutputControlPointCount`
/// control points rather than an explicit invocation array (see
/// `FemePatchArgs`'s own comment for why a control point needs none).
struct PatchResources {
  llvm::ArrayRef<FemeDescriptor> ResourceHeap;
  llvm::ArrayRef<BoundResourceBinding> BoundResources;
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
                     llvm::ArrayRef<FemeImageDescriptor> ImageHeap,
                     llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap,
                     llvm::ArrayRef<uint8_t> RootConstants,
                     const FemeStageLayout *InputLayout, const void *Inputs,
                     const FemeStageLayout *OutputLayout, void *Outputs,
                     uint32_t OutputControlPointCount);

  std::vector<FemeDescriptor> ResourceHeap;
  llvm::ArrayRef<FemeImageDescriptor> ImageHeap;
  llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap;
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
                             llvm::ArrayRef<FemeImageDescriptor> ImageHeap,
                             llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap,
                             llvm::ArrayRef<uint8_t> RootConstants,
                             const FemeStageLayout *InputLayout,
                             const void *Inputs,
                             const FemeStageLayout *InputPatchLayout,
                             const void *InputPatch,
                             const FemeStageLayout *OutputLayout, void *Outputs,
                             uint32_t OutputControlPointCount,
                             uint32_t InputPatchControlPointCount);

  std::vector<FemeDescriptor> ResourceHeap;
  llvm::ArrayRef<FemeImageDescriptor> ImageHeap;
  llvm::ArrayRef<FemeSamplerDescriptor> SamplerHeap;
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

} // namespace feme::cpu

#endif // FEME_TARGET_CPU_RESOURCEHEAP_H
