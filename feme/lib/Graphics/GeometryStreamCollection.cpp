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

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

using namespace feme::graphics;

GeometryStreamMergeResult
feme::graphics::collectGeometryStreams(const cpu::FemeGeometryArgs &Args,
                                       GeometryStreamBuilder &Combined) {
  // (Roadmap H21e) `Args.StreamCount` streams share every primitive's
  // `MaxVerticesPerStream` bound; primitive `P` stream `S`'s own records
  // live at flat index `P * Args.StreamCount + S` -- see
  // `FemeGeometryArgs`'s own comment in RuntimeABI.h for the exact
  // addressing this mirrors.
  uint32_t StreamCount = std::max(Args.StreamCount, 1u);
  std::vector<GeometryStreamBuilder> Lanes;
  Lanes.reserve(Args.PrimitiveCount);
  for (uint32_t Primitive = 0; Primitive != Args.PrimitiveCount; ++Primitive) {
    GeometryStreamBuilder Lane(StreamCount, Args.MaxVerticesPerStream);
    for (uint32_t Stream = 0; Stream != StreamCount; ++Stream) {
      uint32_t StreamSlot = Primitive * StreamCount + Stream;
      uint32_t Count = Args.EmittedVertexCounts[StreamSlot];
      for (uint32_t Vertex = 0; Vertex != Count; ++Vertex) {
        uint32_t Slot = StreamSlot * Args.MaxVerticesPerStream + Vertex;
        const float *Scalars = Args.EmittedVertices +
                               (uint64_t)Slot * Args.OutputScalarsPerVertex;
        Lane.emit(Stream,
                  llvm::ArrayRef(Scalars, Args.OutputScalarsPerVertex));
        if (Args.StripEndsAfter[Slot])
          Lane.cut(Stream);
      }
    }
    Lanes.push_back(std::move(Lane));
  }
  return mergeGeometryStreamsInLaneOrder(Lanes, Combined);
}
