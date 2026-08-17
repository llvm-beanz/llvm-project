//===- GeometryStream.cpp - Bounded geometry-stage output streams --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/GeometryStream.h"

using namespace feme::graphics;

GeometryStreamBuilder::GeometryStreamBuilder(uint32_t StreamCount,
                                             uint32_t MaxVerticesPerStream)
    : Streams(StreamCount), MaxVerticesPerStream(MaxVerticesPerStream) {}

bool GeometryStreamBuilder::emit(uint32_t Stream,
                                 llvm::ArrayRef<float> Scalars) {
  if (Stream >= Streams.size())
    return false;
  StreamState &State = Streams[Stream];
  if (State.Vertices.size() >= MaxVerticesPerStream)
    return false;
  State.Vertices.emplace_back(Scalars.begin(), Scalars.end());
  return true;
}

void GeometryStreamBuilder::cut(uint32_t Stream) {
  if (Stream >= Streams.size())
    return;
  StreamState &State = Streams[Stream];
  uint32_t End = static_cast<uint32_t>(State.Vertices.size());
  if (End == State.OpenStripBegin)
    return;
  State.ClosedStrips.push_back({State.OpenStripBegin, End});
  State.OpenStripBegin = End;
}

llvm::ArrayRef<StreamVertex>
GeometryStreamBuilder::getVertices(uint32_t Stream) const {
  if (Stream >= Streams.size())
    return {};
  return Streams[Stream].Vertices;
}

std::vector<StreamStrip>
GeometryStreamBuilder::getStrips(uint32_t Stream) const {
  if (Stream >= Streams.size())
    return {};
  const StreamState &State = Streams[Stream];
  std::vector<StreamStrip> Strips = State.ClosedStrips;
  uint32_t End = static_cast<uint32_t>(State.Vertices.size());
  if (End != State.OpenStripBegin)
    Strips.push_back({State.OpenStripBegin, End});
  return Strips;
}
