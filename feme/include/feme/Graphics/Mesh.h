//===- Mesh.h - Mesh stage state attributes ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap H6a: the mesh stage's counterpart to Geometry.h. A SPIR-V mesh
// entry point (`VK_EXT_mesh_shader`'s `MeshEXT` execution model) declares
// its output topology (`OutputPoints`/`OutputLinesEXT`/`OutputTrianglesEXT`
// execution modes), maximum emitted vertex count (`OutputVertices`) and
// maximum emitted primitive count (`OutputPrimitivesEXT`) -- all captured by
// `ConvertSPIRVToLLVMPass` into `feme.mesh.*` passthrough attributes and
// read back here into a `MeshState`, exactly the way `feme.geometry.*`/
// `GeometryState` already work for the geometry stage (roadmap H5a). A mesh
// entry point's workgroup size (`LocalSize`) is captured the same way a
// compute entry point's is, since the mesh and task (amplification) stages
// dispatch as bounded workgroups just like compute -- see the existing
// `hlsl.numthreads` passthrough attribute.
//
// Two of these enumerant *values* are shared with the geometry stage's own
// execution modes at the SPIR-V level (`OutputPoints`, shared between the
// `Geometry` and `MeshShadingEXT` capabilities; `OutputVertices`, shared
// with both tessellation's output control point count and geometry's own
// maximum emitted vertex count) -- disambiguated by the declaring entry
// point's own stage, exactly as H5a already disambiguates `Triangles`/
// `OutputVertices` between tessellation and geometry.
//
// The task (amplification) stage declares no shape of its own beyond its
// workgroup size: it dispatches mesh workgroups via `EmitMeshTasksEXT` with
// a bounded payload, but emits no vertices or primitives itself, so it has
// no `MeshState` counterpart here.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_MESH_H
#define FEME_GRAPHICS_MESH_H

#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <optional>

namespace llvm {
class Function;
}

namespace feme::graphics {

/// The primitive topology a mesh entry point emits, per SPIR-V's
/// `OutputPoints`/`OutputLinesEXT`/`OutputTrianglesEXT` execution modes.
enum class MeshOutputTopology : uint8_t {
  Points,
  Lines,
  Triangles,
};

/// The declared shape of a mesh entry point: its output topology and the
/// maximum number of vertices and primitives it may emit.
struct MeshState {
  MeshOutputTopology OutputTopology = MeshOutputTopology::Points;
  /// SPIR-V's `OutputVertices` execution mode: the maximum number of
  /// vertices this entry point may write to its per-vertex output arrays,
  /// the bound a mesh-output builder is constructed with.
  uint32_t MaxOutputVertices = 0;
  /// SPIR-V's `OutputPrimitivesEXT` execution mode: the maximum number of
  /// primitives (points, lines, or triangles, per `OutputTopology`) this
  /// entry point may write to its per-primitive output arrays.
  uint32_t MaxOutputPrimitives = 0;
};

llvm::StringRef getMeshOutputTopologyAttrName();
llvm::StringRef getMeshMaxOutputVerticesAttrName();
llvm::StringRef getMeshMaxOutputPrimitivesAttrName();

/// Returns the mesh state encoded on \p F by the SPIR-V import path, or
/// `std::nullopt` if \p F carries no mesh attributes at all (not a mesh
/// entry point, or one whose attributes failed to parse).
std::optional<MeshState> getMeshState(const llvm::Function &F);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_MESH_H
