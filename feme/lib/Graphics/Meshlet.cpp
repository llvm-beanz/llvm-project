//===- Meshlet.cpp - Assembled mesh workgroup output ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Meshlet.h"

#include "llvm/Support/raw_ostream.h"

using namespace feme::graphics;
using llvm::Error;
using llvm::Expected;

llvm::ArrayRef<uint32_t> Meshlet::getPrimitiveIndices(uint32_t Index) const {
  uint32_t Width = getVerticesPerPrimitive(Topology);
  if (Index >= getPrimitiveCount())
    return {};
  return llvm::ArrayRef<uint32_t>(PrimitiveIndices)
      .slice(static_cast<size_t>(Index) * Width, Width);
}

Expected<Meshlet>
feme::graphics::assembleMeshlet(const MeshOutputBuilder &Builder) {
  Meshlet Result;
  Result.Topology = Builder.getTopology();
  uint32_t VertexCount = Builder.getVertexCount();
  uint32_t PrimitiveCount = Builder.getPrimitiveCount();
  uint32_t Width = getVerticesPerPrimitive(Result.Topology);

  Result.Vertices.assign(Builder.getVertices().begin(),
                         Builder.getVertices().begin() + VertexCount);
  Result.Primitives.assign(Builder.getPrimitives().begin(),
                           Builder.getPrimitives().begin() + PrimitiveCount);
  Result.PrimitiveIndices.reserve(static_cast<size_t>(PrimitiveCount) * Width);

  for (uint32_t Primitive = 0; Primitive != PrimitiveCount; ++Primitive) {
    llvm::ArrayRef<uint32_t> Indices = Builder.getPrimitiveIndices(Primitive);
    for (uint32_t VertexIndex : Indices) {
      if (VertexIndex >= VertexCount)
        return llvm::createStringError(
            llvm::inconvertibleErrorCode(),
            "meshlet primitive %u names out-of-range vertex index %u "
            "(vertex count is %u)",
            Primitive, VertexIndex, VertexCount);
      Result.PrimitiveIndices.push_back(VertexIndex);
    }
  }
  return Result;
}
