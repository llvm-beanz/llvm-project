//===- Meshlet.h - Assembled mesh workgroup output -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap H6d's meshlet-assembly half: turns one mesh workgroup's completed
// `feme::graphics::MeshOutputBuilder` (its declared `SetMeshOutputsEXT`
// counts, per-vertex/per-primitive rows, and primitive index lists) into a
// `Meshlet` -- the trimmed, executor-consumable unit `Executor::
// executeDraws` will chain each dispatched mesh workgroup's output into
// (roadmap H6e), mirroring the same "ordered vertex array plus, per
// primitive, the vertex indices it uses" shape an indexed draw's own
// vertex/index buffer pair already gives the rasterizer.
//
// `MeshOutputBuilder::setPrimitiveIndices` already rejects an out-of-range
// vertex index at write time (roadmap H6c). `assembleMeshlet` re-validates
// that same bound at assembly time rather than trusting the builder's own
// history: once a real compiled mesh workgroup's output reaches this
// function (roadmap H6c-a-a, still pending), it will have been populated by
// compiler-generated stores into the builder's underlying arrays, not
// exclusively through `setPrimitiveIndices`'s own checked setter, so an
// out-of-range vertex index in a primitive's index list must still be
// diagnosed here rather than read out of bounds when the executor later
// walks it -- exactly the "topology validation" roadmap H6d asks for.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_MESHLET_H
#define FEME_GRAPHICS_MESHLET_H

#include "feme/Graphics/MeshOutput.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <vector>

namespace feme::graphics {

/// One mesh workgroup's assembled, validated output: exactly
/// `getVertices().size()`/`getPrimitives().size()` rows long (the
/// workgroup's own `SetMeshOutputsEXT`-declared actual counts, not the
/// entry point's declared maxima `MeshOutputBuilder` itself is sized to),
/// ready for `Executor::executeDraws` to chain into the same clipping/
/// rasterization path a vertex or geometry primitive already uses.
class Meshlet {
public:
  MeshOutputTopology getTopology() const { return Topology; }

  /// This meshlet's own emitted vertex rows, `getVertexCount()` wide.
  llvm::ArrayRef<MeshOutputRow> getVertices() const { return Vertices; }
  uint32_t getVertexCount() const {
    return static_cast<uint32_t>(Vertices.size());
  }

  /// This meshlet's own emitted primitive rows, `getPrimitiveCount()` wide.
  llvm::ArrayRef<MeshOutputRow> getPrimitives() const { return Primitives; }
  uint32_t getPrimitiveCount() const {
    return static_cast<uint32_t>(Primitives.size());
  }

  /// Primitive \p Index's own vertex index list into `getVertices()`,
  /// `getVerticesPerPrimitive(getTopology())` wide. Every entry is
  /// guaranteed `< getVertexCount()` -- `assembleMeshlet` never returns a
  /// `Meshlet` whose index lists could name an out-of-range vertex.
  llvm::ArrayRef<uint32_t> getPrimitiveIndices(uint32_t Index) const;

private:
  friend llvm::Expected<Meshlet> assembleMeshlet(const MeshOutputBuilder &);

  MeshOutputTopology Topology = MeshOutputTopology::Points;
  std::vector<MeshOutputRow> Vertices;
  std::vector<MeshOutputRow> Primitives;
  /// Flat `getPrimitiveCount() * getVerticesPerPrimitive(Topology)`
  /// storage, primitive-major, mirroring `MeshOutputBuilder`'s own.
  std::vector<uint32_t> PrimitiveIndices;
};

/// Assembles \p Builder's declared-count output (its `getVertexCount()`/
/// `getPrimitiveCount()` prefix, not its full declared-maxima storage) into
/// a `Meshlet`, revalidating that every primitive's vertex index list only
/// names an in-range vertex slot. Returns an `Error` -- diagnosing which
/// primitive and which out-of-range index -- instead of a `Meshlet` if that
/// check fails, per this file's own "diagnosed, not read out of bounds"
/// contract.
llvm::Expected<Meshlet> assembleMeshlet(const MeshOutputBuilder &Builder);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_MESHLET_H
