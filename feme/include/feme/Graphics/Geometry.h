//===- Geometry.h - Geometry stage state attributes -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap H5a: the geometry stage's counterpart to Tessellation.h. A SPIR-V
// geometry entry point declares its input primitive class (`InputPoints`/
// `InputLines`/`InputLinesAdjacency`/`Triangles`/`InputTrianglesAdjacency`
// execution modes), output primitive class (`OutputPoints`/
// `OutputLineStrip`/`OutputTriangleStrip`), instance count (`Invocations`,
// defaulting to 1 if not declared) and maximum emitted vertex count
// (`OutputVertices`) -- all captured by `ConvertSPIRVToLLVMPass` into
// `feme.geometry.*` passthrough attributes and read back here into a
// `GeometryState`, exactly the way `feme.tessellation.*`/`TessellationState`
// already work for the tessellation stages.
//
// Unlike the tessellation-only modes, two of these enumerant *values* are
// shared with tessellation's own execution modes at the SPIR-V level
// (`Triangles`, shared with `TessellatorDomain::Triangle`; `OutputVertices`,
// shared with the hull stage's output control point count) -- disambiguated
// by the declaring entry point's own stage, which `ConvertSPIRVToLLVMPass`
// already knows before it looks at that entry's execution modes.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_GEOMETRY_H
#define FEME_GRAPHICS_GEOMETRY_H

#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <optional>

namespace llvm {
class Function;
}

namespace feme::graphics {

/// The primitive class a geometry entry point's `Inputs` block assembles
/// per invocation, per SPIR-V's `InputPoints`/`InputLines`/
/// `InputLinesAdjacency`/`Triangles`/`InputTrianglesAdjacency` execution
/// modes.
enum class GeometryInputPrimitive : uint8_t {
  Points,
  Lines,
  LinesAdjacency,
  Triangles,
  TrianglesAdjacency,
};

/// The primitive class a geometry entry point emits, per SPIR-V's
/// `OutputPoints`/`OutputLineStrip`/`OutputTriangleStrip` execution modes.
enum class GeometryOutputPrimitive : uint8_t {
  Points,
  LineStrip,
  TriangleStrip,
};

/// How many vertices \p Primitive's assembled input primitive carries --
/// the multiplier host code needs to size `FemeGeometryArgs::Inputs`
/// (`VerticesPerPrimitive`): 1 for Points, 2 for Lines, 4 for
/// LinesAdjacency, 3 for Triangles, 6 for TrianglesAdjacency, per the
/// SPIR-V/GLSL geometry input primitive shapes.
uint32_t getVerticesPerPrimitive(GeometryInputPrimitive Primitive);

/// The declared shape of a geometry entry point: its input/output
/// primitive classes, instance count, and maximum emitted vertex count.
struct GeometryState {
  GeometryInputPrimitive InputPrimitive = GeometryInputPrimitive::Points;
  GeometryOutputPrimitive OutputPrimitive = GeometryOutputPrimitive::Points;
  /// SPIR-V's `Invocations` execution mode: how many times this entry
  /// point runs per assembled input primitive (`gl_InvocationID` ranges
  /// `[0, Invocations)`). Defaults to 1 -- the value GLSL's own compiler
  /// (glslang) fills in when the shader source declares no `invocations`
  /// layout qualifier, since SPIR-V requires the execution mode to always
  /// be present for a geometry entry point.
  uint32_t Invocations = 1;
  /// SPIR-V's `OutputVertices` execution mode: the maximum number of
  /// vertices this entry point may `emit` across every stream, in total,
  /// per invocation -- the bound `feme::graphics::GeometryStreamBuilder`'s
  /// `MaxVerticesPerStream` is constructed with.
  uint32_t MaxOutputVertices = 0;
};

llvm::StringRef getGeometryInputPrimitiveAttrName();
llvm::StringRef getGeometryOutputPrimitiveAttrName();
llvm::StringRef getGeometryInvocationsAttrName();
llvm::StringRef getGeometryMaxOutputVerticesAttrName();

/// Returns the geometry state encoded on \p F by the SPIR-V import path, or
/// `std::nullopt` if \p F carries no geometry attributes at all (not a
/// geometry entry point, or one whose attributes failed to parse).
std::optional<GeometryState> getGeometryState(const llvm::Function &F);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_GEOMETRY_H
