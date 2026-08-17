//===- GeometryStream.h - Bounded geometry-stage output streams -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::graphics::GeometryStreamBuilder, roadmap R34's
// "bounded geometry streams" and "stream output": the per-invocation output
// stream storage "Patch and geometry wrappers" in
// feme/docs/FeMeGraphicsDesign.md describes -- "The geometry wrapper
// receives primitive records and owns a bounded stream builder per
// invocation ... SIMD lanes reserve stream ranges with checked prefix
// sums; deterministic mode uses lane order" -- and "Stream output and
// rasterization consume the same emitted records but retain their distinct
// API ordering and capture rules" from "Tessellation and geometry stage
// model".
//
// This models the single-invocation, deterministic-lane-order case: one
// `GeometryStreamBuilder` is exactly the bounded storage one geometry
// invocation's `emit`/`cut` calls (`feme::createStageStreamEmit`/
// `createStageStreamCut`, feme/include/feme/Core/StageOps.h) would target.
// The emitted vertex records it accumulates are what both rasterization and
// stream-output capture read from, in the same emission order, matching the
// design's "same emitted records" language above. SIMD-lane range
// reservation (batching many invocations' emissions together with a
// checked prefix sum so lanes do not race for stream storage) is an
// executor/wrapper-level concern once a compiled geometry stage exists and
// is a documented follow-up; this builder is what such a wrapper needs to
// batch on top of.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_GEOMETRYSTREAM_H
#define FEME_GRAPHICS_GEOMETRYSTREAM_H

#include "llvm/ADT/ArrayRef.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace feme::graphics {

/// One emitted vertex record: the geometry stage's current output signature
/// values at the point `emit` was called, as a flat scalar array (the same
/// shape a vertex-wave's structure-of-arrays output row would flatten to;
/// interpreting which scalar is which signature element is the compiled
/// stage's own `FemeStageLayout` knowledge, not this storage's concern).
using StreamVertex = std::vector<float>;

/// A closed or still-open primitive strip within one stream: the half-open
/// `[Begin, End)` range of `GeometryStreamBuilder::getVertices`'s indices
/// forming it, in emission order.
struct StreamStrip {
  uint32_t Begin = 0;
  uint32_t End = 0;
};

/// Bounded per-invocation storage for a geometry stage's output streams,
/// per the file comment above.
class GeometryStreamBuilder {
public:
  /// \p StreamCount output streams, each bounded to \p MaxVerticesPerStream
  /// emitted vertices -- the wrapper's own validated maximum output vertex
  /// count (a hull-shader-style declared limit), matching "checks the
  /// declared maximum output count before every emission" in
  /// FeMeGraphicsDesign.md.
  GeometryStreamBuilder(uint32_t StreamCount, uint32_t MaxVerticesPerStream);

  uint32_t getStreamCount() const { return Streams.size(); }
  uint32_t getMaxVerticesPerStream() const { return MaxVerticesPerStream; }

  /// The geometry stage's `emit(stream)` operation: appends \p Scalars as
  /// one vertex record onto stream \p Stream. Returns false, leaving the
  /// stream unmodified, if \p Stream is out of range or already holds
  /// `MaxVerticesPerStream` vertices -- emission is side-effecting (see the
  /// file comment), so a caller must observe this failure rather than
  /// silently dropping or overrunning storage.
  bool emit(uint32_t Stream, llvm::ArrayRef<float> Scalars);

  /// The geometry stage's `cut(stream)` operation: closes the strip
  /// currently accumulating on stream \p Stream (a no-op, closing nothing,
  /// if that strip has no vertices yet -- calling `cut` twice in a row, or
  /// before any `emit`, is legal and simply does nothing the second time).
  /// Does nothing if \p Stream is out of range.
  void cut(uint32_t Stream);

  /// Every vertex emitted onto stream \p Stream so far, in emission order.
  /// Empty if \p Stream is out of range.
  llvm::ArrayRef<StreamVertex> getVertices(uint32_t Stream) const;

  /// Every strip on stream \p Stream, in emission order: every `cut`-closed
  /// strip, plus a final still-open one (per `cut`'s own comment) if it has
  /// at least one vertex. Empty if \p Stream is out of range.
  std::vector<StreamStrip> getStrips(uint32_t Stream) const;

private:
  struct StreamState {
    std::vector<StreamVertex> Vertices;
    std::vector<StreamStrip> ClosedStrips;
    uint32_t OpenStripBegin = 0;
  };

  std::vector<StreamState> Streams;
  uint32_t MaxVerticesPerStream;
};

} // namespace feme::graphics

#endif // FEME_GRAPHICS_GEOMETRYSTREAM_H
