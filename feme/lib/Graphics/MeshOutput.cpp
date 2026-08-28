//===- MeshOutput.cpp - Bounded mesh-stage output storage ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/MeshOutput.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ErrorHandling.h"

using namespace feme::graphics;

uint32_t feme::graphics::getVerticesPerPrimitive(MeshOutputTopology Topology) {
  switch (Topology) {
  case MeshOutputTopology::Points:
    return 1;
  case MeshOutputTopology::Lines:
    return 2;
  case MeshOutputTopology::Triangles:
    return 3;
  }
  llvm_unreachable("not a mesh output topology");
}

MeshOutputBuilder::MeshOutputBuilder(MeshOutputTopology Topology,
                                     uint32_t MaxVertices,
                                     uint32_t MaxPrimitives)
    : Topology(Topology), Vertices(MaxVertices), Primitives(MaxPrimitives),
      PrimitiveIndices(static_cast<size_t>(MaxPrimitives) *
                       getVerticesPerPrimitive(Topology)) {}

bool MeshOutputBuilder::setOutputCounts(uint32_t VertexCount,
                                        uint32_t PrimitiveCount) {
  if (VertexCount > getMaxVertices() || PrimitiveCount > getMaxPrimitives())
    return false;
  this->VertexCount = VertexCount;
  this->PrimitiveCount = PrimitiveCount;
  return true;
}

bool MeshOutputBuilder::setVertex(uint32_t Index,
                                  llvm::ArrayRef<float> Scalars) {
  if (Index >= VertexCount)
    return false;
  Vertices[Index].assign(Scalars.begin(), Scalars.end());
  return true;
}

bool MeshOutputBuilder::setPrimitive(uint32_t Index,
                                     llvm::ArrayRef<float> Scalars) {
  if (Index >= PrimitiveCount)
    return false;
  Primitives[Index].assign(Scalars.begin(), Scalars.end());
  return true;
}

bool MeshOutputBuilder::setPrimitiveIndices(uint32_t Index,
                                            llvm::ArrayRef<uint32_t> Indices) {
  uint32_t Width = getVerticesPerPrimitive(Topology);
  if (Index >= PrimitiveCount || Indices.size() != Width)
    return false;
  for (uint32_t VertexIndex : Indices)
    if (VertexIndex >= VertexCount)
      return false;
  llvm::copy(Indices, PrimitiveIndices.begin() + Index * Width);
  return true;
}

llvm::ArrayRef<uint32_t>
MeshOutputBuilder::getPrimitiveIndices(uint32_t Index) const {
  uint32_t Width = getVerticesPerPrimitive(Topology);
  if (Index >= getMaxPrimitives())
    return {};
  return llvm::ArrayRef<uint32_t>(PrimitiveIndices)
      .slice(static_cast<size_t>(Index) * Width, Width);
}
