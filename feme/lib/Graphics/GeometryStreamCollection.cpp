//===- GeometryStreamCollection.cpp - Compiled-batch stream replay -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/GeometryStreamCollection.h"

#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/ArrayRef.h"

#include <cstdint>
#include <utility>
#include <vector>

using namespace feme::graphics;

GeometryStreamMergeResult
feme::graphics::collectGeometryStreams(const cpu::FemeGeometryArgs &Args,
                                       GeometryStreamBuilder &Combined) {
  std::vector<GeometryStreamBuilder> Lanes;
  Lanes.reserve(Args.PrimitiveCount);
  for (uint32_t Primitive = 0; Primitive != Args.PrimitiveCount; ++Primitive) {
    GeometryStreamBuilder Lane(/*StreamCount=*/1, Args.MaxVerticesPerStream);
    uint32_t Count = Args.EmittedVertexCounts[Primitive];
    for (uint32_t Vertex = 0; Vertex != Count; ++Vertex) {
      uint32_t Slot = Primitive * Args.MaxVerticesPerStream + Vertex;
      const float *Scalars =
          Args.EmittedVertices + (uint64_t)Slot * Args.OutputScalarsPerVertex;
      Lane.emit(/*Stream=*/0,
                llvm::ArrayRef(Scalars, Args.OutputScalarsPerVertex));
      if (Args.StripEndsAfter[Slot])
        Lane.cut(/*Stream=*/0);
    }
    Lanes.push_back(std::move(Lane));
  }
  return mergeGeometryStreamsInLaneOrder(Lanes, Combined);
}
