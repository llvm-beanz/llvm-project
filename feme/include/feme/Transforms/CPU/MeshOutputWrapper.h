//===- MeshOutputWrapper.h - Mesh stage per-vertex/-primitive output ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::MeshOutputWrapperPass, roadmap H6c-a-a:
// lowers a mesh entry point's canonicalized `feme.stage.output.store`
// per-vertex/per-primitive writes (roadmap H6b's `Vertex` operand) into the
// flat `VertexOutputs`/`PrimitiveOutputs` storage `FemeMeshArgs` (roadmap
// H6c) already reserves for them, and (roadmap H6c-a-a-i) the canonicalized
// `feme.stage.set_mesh_outputs` (SPIR-V's `SetMeshOutputsEXT`) call into
// `FemeMeshArgs::ActualVertexCount`/`ActualPrimitiveCount`.
//
// Unlike `feme::cpu::GeometryWrapperPass`, this pass does *not* build the
// `feme_cpu_entry_<name>` ABI wrapper itself: roadmap H6c already
// established that a mesh workgroup dispatches exactly like a compute one
// (`FemeMeshArgs` shares `FemeDispatchArgs`'s own `Resources`/`GroupID`/
// `GroupCount`/`GroupShared` prefix field-for-field), so
// `feme::cpu::EntryWrapperPass` builds that wrapper unmodified -- this pass
// only needs to run *before* it, appending the extra wave-body parameters
// (`mesh_vertex_output_layout`, `mesh_vertex_outputs`,
// `mesh_primitive_output_layout`, `mesh_primitive_outputs`,
// `mesh_max_output_vertices`, `mesh_max_output_primitives`) that lowered
// output stores address, and that `EntryWrapperPass` (extended by this
// roadmap row) knows how to fill from a real `FemeMeshArgs*` by name, the
// same "recovered by name" convention every other wave-body parameter
// already uses (see `feme::cpu::WaveBodyEnv`'s own comment).
//
// A `SignatureElement::Frequency` of `PerPrimitive` addresses
// `PrimitiveOutputs`; every other element (ordinary `PerVertex`, the
// default for every mesh output today) addresses `VertexOutputs`. Both
// arrays are structure-of-arrays over the workgroup's declared output
// slots, addressed by the store's own dynamic `Vertex` operand exactly the
// way `feme::graphics::MeshOutputBuilder::setVertex`/`setPrimitive` are
// (the host-side mirror this pass's storage feeds, once
// `CompiledStage::invokeMesh`'s caller replays it -- see
// `feme::graphics::Executor::executeDraws`'s own `runMeshWorkgroup`).
//
// A lowered `feme.stage.set_mesh_outputs(vertexCount, primitiveCount)` call
// (roadmap H6c-a-a-i) writes both counts through the two `uint32_t*`
// pointer fields this pass appends (mirroring `TaskPayloadWrapperPass`'s
// own plain-pointer, no-`FemeStageLayout` treatment of `task_payload`):
// unlike `VertexOutputs`/`PrimitiveOutputs`, `ActualVertexCount`/
// `ActualPrimitiveCount` are single scalars, not structure-of-arrays
// storage, since `SetMeshOutputsEXT` is called once (with identical
// arguments) by the whole workgroup, not once per output slot.
//
// **Left open by this row**: a primitive's own vertex index list
// (`PrimitiveIndices`, `gl_PrimitiveTriangleIndicesEXT`-shaped) has no
// canonicalized `feme.stage.*` op to lower at all yet, so it is still not
// written by this pass -- see roadmap H6c-a-a-ii for `flattenMeshRow`'s
// own remaining `PerPrimitive` gap, and a future row for
// `PrimitiveIndices` itself.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_MESHOUTPUTWRAPPER_H
#define FEME_TRANSFORMS_CPU_MESHOUTPUTWRAPPER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Roadmap H6c-a-a: lowers a mesh entry point's per-vertex/per-primitive
/// `feme.stage.output.store` writes into `FemeMeshArgs`'s flat output
/// storage. See the file comment above for scope.
class MeshOutputWrapperPass
    : public llvm::PassInfoMixin<MeshOutputWrapperPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-wrap-mesh-output"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_MESHOUTPUTWRAPPER_H
