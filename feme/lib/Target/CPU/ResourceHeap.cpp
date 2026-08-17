//===- ResourceHeap.cpp - CPU target physical heap materialization ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/ResourceHeap.h"

#include "llvm/ADT/STLExtras.h"

#include <algorithm>

using namespace llvm;

namespace feme::cpu {

std::vector<FemeDescriptor>
materializeResourceHeap(const ResourceInfo &Info,
                        ArrayRef<BoundResourceBinding> Bindings,
                        ArrayRef<FemeDescriptor> DynamicHeap) {
  std::vector<FemeDescriptor> Heap(
      static_cast<size_t>(Info.ReservedResourceHeapSize) + DynamicHeap.size(),
      FemeDescriptor{});

  for (const BoundResourceRange &Range : Info.BoundRanges) {
    const BoundResourceBinding *Matched = nullptr;
    for (const BoundResourceBinding &Binding : Bindings)
      if (Binding.Space == Range.Space &&
          Binding.BaseRegister == Range.BaseRegister) {
        Matched = &Binding;
        break;
      }
    if (!Matched)
      continue;

    size_t NumToCopy =
        std::min<size_t>(Range.RangeSize, Matched->Descriptors.size());
    for (size_t J = 0; J != NumToCopy; ++J)
      Heap[Range.HeapBase + J] = Matched->Descriptors[J];
  }

  llvm::copy(DynamicHeap, Heap.begin() + Info.ReservedResourceHeapSize);
  return Heap;
}

PreparedDispatch::PreparedDispatch(std::vector<FemeDescriptor> ResourceHeap,
                                   ArrayRef<FemeImageDescriptor> ImageHeap,
                                   ArrayRef<FemeSamplerDescriptor> SamplerHeap,
                                   ArrayRef<uint8_t> RootConstants,
                                   std::array<uint32_t, 3> GroupCount)
    : ResourceHeap(std::move(ResourceHeap)), ImageHeap(ImageHeap),
      SamplerHeap(SamplerHeap), RootConstants(RootConstants),
      GroupCount(GroupCount) {}

PreparedDispatch PreparedDispatch::create(const ResourceInfo &Info,
                                          const DispatchResources &Resources,
                                          std::array<uint32_t, 3> GroupCount) {
  return PreparedDispatch(materializeResourceHeap(Info,
                                                  Resources.BoundResources,
                                                  Resources.ResourceHeap),
                          Resources.ImageHeap, Resources.SamplerHeap,
                          Resources.RootConstants, GroupCount);
}

FemeDispatchArgs
PreparedDispatch::argsFor(std::array<uint32_t, 3> GroupID,
                          MutableArrayRef<uint8_t> GroupShared) const {
  FemeDispatchArgs Args{};
  Args.Resources.ResourceHeap = ResourceHeap.data();
  Args.Resources.ResourceHeapCount = static_cast<uint32_t>(ResourceHeap.size());
  Args.Resources.ImageHeap = ImageHeap.data();
  Args.Resources.ImageHeapCount = static_cast<uint32_t>(ImageHeap.size());
  Args.Resources.SamplerHeap = SamplerHeap.data();
  Args.Resources.SamplerHeapCount = static_cast<uint32_t>(SamplerHeap.size());
  Args.Resources.RootConstants = RootConstants.data();
  Args.Resources.RootConstantSize = static_cast<uint32_t>(RootConstants.size());
  Args.GroupCount[0] = GroupCount[0];
  Args.GroupCount[1] = GroupCount[1];
  Args.GroupCount[2] = GroupCount[2];
  Args.GroupID[0] = GroupID[0];
  Args.GroupID[1] = GroupID[1];
  Args.GroupID[2] = GroupID[2];
  Args.GroupShared = GroupShared.data();
  return Args;
}

void invokeGroup(EntryPointFn EntryFn, const PreparedDispatch &Prepared,
                 std::array<uint32_t, 3> GroupID,
                 MutableArrayRef<uint8_t> GroupShared) {
  FemeDispatchArgs Args = Prepared.argsFor(GroupID, GroupShared);
  EntryFn(&Args);
}

void runDispatch(EntryPointFn EntryFn, const ResourceInfo &Info,
                 const DispatchResources &Resources,
                 std::array<uint32_t, 3> GroupCount) {
  PreparedDispatch Prepared =
      PreparedDispatch::create(Info, Resources, GroupCount);

  for (uint32_t Z = 0; Z != GroupCount[2]; ++Z)
    for (uint32_t Y = 0; Y != GroupCount[1]; ++Y)
      for (uint32_t X = 0; X != GroupCount[0]; ++X)
        invokeGroup(EntryFn, Prepared, {X, Y, Z}, /*GroupShared=*/{});
}

PreparedVertexBatch::PreparedVertexBatch(
    std::vector<FemeDescriptor> ResourceHeap,
    ArrayRef<FemeImageDescriptor> ImageHeap,
    ArrayRef<FemeSamplerDescriptor> SamplerHeap,
    ArrayRef<uint8_t> RootConstants, const FemeStageLayout *InputLayout,
    const void *Inputs, const FemeStageLayout *OutputLayout, void *Outputs,
    ArrayRef<FemeVertexInvocation> Invocations)
    : ResourceHeap(std::move(ResourceHeap)), ImageHeap(ImageHeap),
      SamplerHeap(SamplerHeap), RootConstants(RootConstants),
      InputLayout(InputLayout), Inputs(Inputs), OutputLayout(OutputLayout),
      Outputs(Outputs), Invocations(Invocations) {
  ShaderResources.ResourceHeap = this->ResourceHeap.data();
  ShaderResources.ResourceHeapCount =
      static_cast<uint32_t>(this->ResourceHeap.size());
  ShaderResources.ImageHeap = this->ImageHeap.data();
  ShaderResources.ImageHeapCount =
      static_cast<uint32_t>(this->ImageHeap.size());
  ShaderResources.SamplerHeap = this->SamplerHeap.data();
  ShaderResources.SamplerHeapCount =
      static_cast<uint32_t>(this->SamplerHeap.size());
  ShaderResources.RootConstants = this->RootConstants.data();
  ShaderResources.RootConstantSize =
      static_cast<uint32_t>(this->RootConstants.size());
}

PreparedVertexBatch
PreparedVertexBatch::create(const ResourceInfo &Info,
                            const VertexResources &Resources) {
  return PreparedVertexBatch(
      materializeResourceHeap(Info, Resources.BoundResources,
                              Resources.ResourceHeap),
      Resources.ImageHeap, Resources.SamplerHeap, Resources.RootConstants,
      Resources.InputLayout, Resources.Inputs, Resources.OutputLayout,
      Resources.Outputs, Resources.Invocations);
}

FemeVertexArgs PreparedVertexBatch::args() const {
  FemeVertexArgs Args{};
  Args.AbiVersion = StageArgsAbiVersion;
  Args.InvocationCount = static_cast<uint32_t>(Invocations.size());
  Args.Resources = &ShaderResources;
  Args.InputLayout = InputLayout;
  Args.Inputs = Inputs;
  Args.OutputLayout = OutputLayout;
  Args.Outputs = Outputs;
  Args.Invocations = Invocations.data();
  return Args;
}

PreparedFragmentBatch::PreparedFragmentBatch(
    std::vector<FemeDescriptor> ResourceHeap,
    ArrayRef<FemeImageDescriptor> ImageHeap,
    ArrayRef<FemeSamplerDescriptor> SamplerHeap,
    ArrayRef<uint8_t> RootConstants, const FemeStageLayout *InputLayout,
    const void *Inputs, const FemeStageLayout *OutputLayout, void *Outputs,
    ArrayRef<FemeFragmentInvocation> Invocations,
    MutableArrayRef<FemeFragmentResult> Results)
    : ResourceHeap(std::move(ResourceHeap)), ImageHeap(ImageHeap),
      SamplerHeap(SamplerHeap), RootConstants(RootConstants),
      InputLayout(InputLayout), Inputs(Inputs), OutputLayout(OutputLayout),
      Outputs(Outputs), Invocations(Invocations), Results(Results) {
  ShaderResources.ResourceHeap = this->ResourceHeap.data();
  ShaderResources.ResourceHeapCount =
      static_cast<uint32_t>(this->ResourceHeap.size());
  ShaderResources.ImageHeap = this->ImageHeap.data();
  ShaderResources.ImageHeapCount =
      static_cast<uint32_t>(this->ImageHeap.size());
  ShaderResources.SamplerHeap = this->SamplerHeap.data();
  ShaderResources.SamplerHeapCount =
      static_cast<uint32_t>(this->SamplerHeap.size());
  ShaderResources.RootConstants = this->RootConstants.data();
  ShaderResources.RootConstantSize =
      static_cast<uint32_t>(this->RootConstants.size());
}

PreparedFragmentBatch
PreparedFragmentBatch::create(const ResourceInfo &Info,
                              const FragmentResources &Resources) {
  return PreparedFragmentBatch(
      materializeResourceHeap(Info, Resources.BoundResources,
                              Resources.ResourceHeap),
      Resources.ImageHeap, Resources.SamplerHeap, Resources.RootConstants,
      Resources.InputLayout, Resources.Inputs, Resources.OutputLayout,
      Resources.Outputs, Resources.Invocations, Resources.Results);
}

FemeFragmentArgs PreparedFragmentBatch::args() const {
  FemeFragmentArgs Args{};
  Args.AbiVersion = StageArgsAbiVersion;
  Args.QuadCount = static_cast<uint32_t>(Invocations.size());
  Args.Resources = &ShaderResources;
  Args.InputLayout = InputLayout;
  Args.Inputs = Inputs;
  Args.OutputLayout = OutputLayout;
  Args.Outputs = Outputs;
  Args.Invocations = Invocations.data();
  Args.Results = Results.data();
  return Args;
}

PreparedPatchBatch::PreparedPatchBatch(
    std::vector<FemeDescriptor> ResourceHeap,
    ArrayRef<FemeImageDescriptor> ImageHeap,
    ArrayRef<FemeSamplerDescriptor> SamplerHeap,
    ArrayRef<uint8_t> RootConstants, const FemeStageLayout *InputLayout,
    const void *Inputs, const FemeStageLayout *OutputLayout, void *Outputs,
    uint32_t OutputControlPointCount)
    : ResourceHeap(std::move(ResourceHeap)), ImageHeap(ImageHeap),
      SamplerHeap(SamplerHeap), RootConstants(RootConstants),
      InputLayout(InputLayout), Inputs(Inputs), OutputLayout(OutputLayout),
      Outputs(Outputs), OutputControlPointCount(OutputControlPointCount) {
  ShaderResources.ResourceHeap = this->ResourceHeap.data();
  ShaderResources.ResourceHeapCount =
      static_cast<uint32_t>(this->ResourceHeap.size());
  ShaderResources.ImageHeap = this->ImageHeap.data();
  ShaderResources.ImageHeapCount =
      static_cast<uint32_t>(this->ImageHeap.size());
  ShaderResources.SamplerHeap = this->SamplerHeap.data();
  ShaderResources.SamplerHeapCount =
      static_cast<uint32_t>(this->SamplerHeap.size());
  ShaderResources.RootConstants = this->RootConstants.data();
  ShaderResources.RootConstantSize =
      static_cast<uint32_t>(this->RootConstants.size());
}

PreparedPatchBatch PreparedPatchBatch::create(const ResourceInfo &Info,
                                              const PatchResources &Resources) {
  return PreparedPatchBatch(
      materializeResourceHeap(Info, Resources.BoundResources,
                              Resources.ResourceHeap),
      Resources.ImageHeap, Resources.SamplerHeap, Resources.RootConstants,
      Resources.InputLayout, Resources.Inputs, Resources.OutputLayout,
      Resources.Outputs, Resources.OutputControlPointCount);
}

FemePatchArgs PreparedPatchBatch::args() const {
  FemePatchArgs Args{};
  Args.AbiVersion = StageArgsAbiVersion;
  Args.OutputControlPointCount = OutputControlPointCount;
  Args.Resources = &ShaderResources;
  Args.InputLayout = InputLayout;
  Args.Inputs = Inputs;
  Args.OutputLayout = OutputLayout;
  Args.Outputs = Outputs;
  return Args;
}

PreparedPatchConstantBatch::PreparedPatchConstantBatch(
    std::vector<FemeDescriptor> ResourceHeap,
    ArrayRef<FemeImageDescriptor> ImageHeap,
    ArrayRef<FemeSamplerDescriptor> SamplerHeap,
    ArrayRef<uint8_t> RootConstants, const FemeStageLayout *InputLayout,
    const void *Inputs, const FemeStageLayout *InputPatchLayout,
    const void *InputPatch, const FemeStageLayout *OutputLayout, void *Outputs,
    uint32_t OutputControlPointCount, uint32_t InputPatchControlPointCount)
    : ResourceHeap(std::move(ResourceHeap)), ImageHeap(ImageHeap),
      SamplerHeap(SamplerHeap), RootConstants(RootConstants),
      InputLayout(InputLayout), Inputs(Inputs),
      InputPatchLayout(InputPatchLayout), InputPatch(InputPatch),
      OutputLayout(OutputLayout), Outputs(Outputs),
      OutputControlPointCount(OutputControlPointCount),
      InputPatchControlPointCount(InputPatchControlPointCount) {
  ShaderResources.ResourceHeap = this->ResourceHeap.data();
  ShaderResources.ResourceHeapCount =
      static_cast<uint32_t>(this->ResourceHeap.size());
  ShaderResources.ImageHeap = this->ImageHeap.data();
  ShaderResources.ImageHeapCount =
      static_cast<uint32_t>(this->ImageHeap.size());
  ShaderResources.SamplerHeap = this->SamplerHeap.data();
  ShaderResources.SamplerHeapCount =
      static_cast<uint32_t>(this->SamplerHeap.size());
  ShaderResources.RootConstants = this->RootConstants.data();
  ShaderResources.RootConstantSize =
      static_cast<uint32_t>(this->RootConstants.size());
}

PreparedPatchConstantBatch
PreparedPatchConstantBatch::create(const ResourceInfo &Info,
                                   const PatchConstantResources &Resources) {
  return PreparedPatchConstantBatch(
      materializeResourceHeap(Info, Resources.BoundResources,
                              Resources.ResourceHeap),
      Resources.ImageHeap, Resources.SamplerHeap, Resources.RootConstants,
      Resources.InputLayout, Resources.Inputs, Resources.InputPatchLayout,
      Resources.InputPatch, Resources.OutputLayout, Resources.Outputs,
      Resources.OutputControlPointCount,
      Resources.InputPatchControlPointCount);
}

FemePatchConstantArgs PreparedPatchConstantBatch::args() const {
  FemePatchConstantArgs Args{};
  Args.AbiVersion = StageArgsAbiVersion;
  Args.OutputControlPointCount = OutputControlPointCount;
  Args.InputPatchControlPointCount = InputPatchControlPointCount;
  Args.Resources = &ShaderResources;
  Args.InputLayout = InputLayout;
  Args.Inputs = Inputs;
  Args.InputPatchLayout = InputPatchLayout;
  Args.InputPatch = InputPatch;
  Args.OutputLayout = OutputLayout;
  Args.Outputs = Outputs;
  return Args;
}

} // namespace feme::cpu
