//===- MeshOutput.h - Bounded mesh-stage output storage --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap H6c's mesh-output counterpart to `feme::graphics::
// GeometryStreamBuilder` (GeometryStream.h): bounded per-invocation storage
// for a mesh entry point's (`VK_EXT_mesh_shader`'s `MeshEXT` execution
// model) emitted vertices and primitives.
//
// The two builders model genuinely different hardware shapes, which is why
// this is a new class rather than a `GeometryStreamBuilder` reuse:
//
//  - A geometry invocation's output is *stream-ordered*: `emit`/`cut` append
//    one vertex record at a time, in whatever order the invocation calls
//    them, and a strip boundary is positional (roadmap H5's `StreamStrip`).
//  - A mesh workgroup's output is *structure-of-arrays, randomly
//    addressed*: `SetMeshOutputsEXT(vertexCount, primitiveCount)` first
//    declares how many of the two bounded arrays this workgroup actually
//    populates (both `<=` the entry point's own declared `OutputVertices`/
//    `OutputPrimitivesEXT` maxima, `feme::graphics::MeshState`), and every
//    invocation may then write *any* vertex or primitive slot in
//    `[0, vertexCount)`/`[0, primitiveCount)` directly (`gl_MeshVerticesEXT
//    [i].gl_Position = ...`, `gl_MeshPrimitivesEXT[i] = ...`) -- there is no
//    per-invocation emission order to preserve, and no strip concept at
//    all: a primitive's own shape (point/line/triangle, `MeshOutputTopology`)
//    is spelled out explicitly by an index list per primitive
//    (`gl_PrimitiveTriangleIndicesEXT[i] = uvec3(...)`), not inferred from
//    strip adjacency the way a geometry stream's strips are.
//
// This models one mesh workgroup's bounded output storage: the per-vertex
// and per-primitive scalar records `feme.stage.output.store`'s existing
// `Vertex`-operand addressing (roadmap H6b) writes into once wired to a real
// wrapper, the primitive index lists identifying which vertices each
// primitive uses, and the workgroup's own declared actual counts. Wiring a
// real compiled mesh entry point's writes into a live `MeshOutputBuilder`
// object -- the same "flat, host-owned arrays now, object replay later"
// approach `feme::cpu::collectGeometryStreams` uses for geometry -- is left
// to whichever roadmap row actually lowers `SetMeshOutputsEXT`/a per-vertex
// or per-primitive output store into IR (H6c leaves both undefined pending
// H6h/H6i; see agent_thoughts.md's H6c entry).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_MESHOUTPUT_H
#define FEME_GRAPHICS_MESHOUTPUT_H

#include "feme/Graphics/Mesh.h"

#include "llvm/ADT/ArrayRef.h"

#include <cstdint>
#include <vector>

namespace feme::graphics {

/// Returns the number of vertices one primitive of \p Topology names (1 for
/// `Points`, 2 for `Lines`, 3 for `Triangles`) -- the width of one row of
/// `MeshOutputBuilder::getPrimitiveIndices`.
uint32_t getVerticesPerPrimitive(MeshOutputTopology Topology);

/// One emitted vertex or primitive record: the mesh stage's current output
/// signature values for that row, as a flat scalar array (mirroring
/// `feme::graphics::StreamVertex`'s same flattened shape). Interpreting
/// which scalar is which signature element is the compiled stage's own
/// `FemeStageLayout` knowledge, not this storage's concern.
using MeshOutputRow = std::vector<float>;

/// Bounded per-workgroup storage for a mesh stage's per-vertex and
/// per-primitive outputs, per the file comment above.
class MeshOutputBuilder {
public:
  /// \p Topology fixes how many vertices `setPrimitiveIndices` expects per
  /// primitive; \p MaxVertices/\p MaxPrimitives are the entry point's own
  /// declared maxima (`MeshState::MaxOutputVertices`/`MaxOutputPrimitives`),
  /// matching "checks the declared maximum output count before every
  /// write" the same discipline `GeometryStreamBuilder` already documents.
  MeshOutputBuilder(MeshOutputTopology Topology, uint32_t MaxVertices,
                    uint32_t MaxPrimitives);

  MeshOutputTopology getTopology() const { return Topology; }
  uint32_t getMaxVertices() const { return static_cast<uint32_t>(Vertices.size()); }
  uint32_t getMaxPrimitives() const {
    return static_cast<uint32_t>(Primitives.size());
  }

  /// The mesh stage's `SetMeshOutputsEXT(vertexCount, primitiveCount)`
  /// operation: declares how many of the bounded vertex/primitive slots
  /// this workgroup actually populates. Returns false, leaving the counts
  /// unset, if either exceeds this builder's own declared maximum -- a
  /// workgroup that never calls this (or calls it with an out-of-range
  /// count) has emitted nothing, mirroring how an entry point that never
  /// calls `emit` produces an empty geometry stream.
  bool setOutputCounts(uint32_t VertexCount, uint32_t PrimitiveCount);

  uint32_t getVertexCount() const { return VertexCount; }
  uint32_t getPrimitiveCount() const { return PrimitiveCount; }

  /// Writes vertex slot \p Index's output scalars. Returns false, leaving
  /// storage unmodified, if \p Index is `>= getVertexCount()` (including
  /// the case where `setOutputCounts` was never called, i.e.
  /// `getVertexCount() == 0`) -- unlike geometry's append-only `emit`, a
  /// mesh write is a random-access store into an already-sized array, so
  /// the bound is the declared count, not a running length.
  bool setVertex(uint32_t Index, llvm::ArrayRef<float> Scalars);

  /// The per-primitive counterpart of `setVertex`, for a primitive's own
  /// output scalars (e.g. a user-defined `perprimitiveEXT` varying, or
  /// `gl_PrimitiveID`) rather than its vertex index list.
  bool setPrimitive(uint32_t Index, llvm::ArrayRef<float> Scalars);

  /// Writes primitive slot \p Index's vertex index list: which of this
  /// workgroup's written vertex slots (`[0, getVertexCount())`) make up
  /// this primitive, `getVerticesPerPrimitive(getTopology())` wide. Returns
  /// false, leaving storage unmodified, if \p Index is out of the declared
  /// primitive count, \p Indices is the wrong width for this builder's
  /// topology, or any named vertex index is itself out of the declared
  /// vertex count (an out-of-range primitive index is a real authoring
  /// error, diagnosed rather than silently clamped or read out of bounds --
  /// see roadmap H6d's own "topology validation" follow-up, which this
  /// bounds check exists to make possible).
  bool setPrimitiveIndices(uint32_t Index, llvm::ArrayRef<uint32_t> Indices);

  /// Every written vertex row, `getMaxVertices()` wide (rows at or beyond
  /// `getVertexCount()` are default-constructed, never written).
  llvm::ArrayRef<MeshOutputRow> getVertices() const { return Vertices; }

  /// Every written primitive row, `getMaxPrimitives()` wide, mirroring
  /// `getVertices()`.
  llvm::ArrayRef<MeshOutputRow> getPrimitives() const { return Primitives; }

  /// Primitive \p Index's own vertex index list, `getVerticesPerPrimitive
  /// (getTopology())` wide. Empty (all zero) if never written via
  /// `setPrimitiveIndices`.
  llvm::ArrayRef<uint32_t> getPrimitiveIndices(uint32_t Index) const;

private:
  MeshOutputTopology Topology;
  std::vector<MeshOutputRow> Vertices;
  std::vector<MeshOutputRow> Primitives;
  /// Flat `getMaxPrimitives() * getVerticesPerPrimitive(Topology)` storage,
  /// primitive-major, mirroring `FemeGeometryArgs::EmittedVertices`'s own
  /// flat-array convention.
  std::vector<uint32_t> PrimitiveIndices;
  uint32_t VertexCount = 0;
  uint32_t PrimitiveCount = 0;
};

} // namespace feme::graphics

#endif // FEME_GRAPHICS_MESHOUTPUT_H
