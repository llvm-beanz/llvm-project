//===- Geometry.cpp - Geometry stage state attributes
//----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Geometry.h"

#include "llvm/IR/Function.h"

using namespace llvm;

namespace feme::graphics {

uint32_t getVerticesPerPrimitive(GeometryInputPrimitive Primitive) {
  switch (Primitive) {
  case GeometryInputPrimitive::Points:
    return 1;
  case GeometryInputPrimitive::Lines:
    return 2;
  case GeometryInputPrimitive::LinesAdjacency:
    return 4;
  case GeometryInputPrimitive::Triangles:
    return 3;
  case GeometryInputPrimitive::TrianglesAdjacency:
    return 6;
  }
  llvm_unreachable("unhandled GeometryInputPrimitive");
}

StringRef getGeometryInputPrimitiveAttrName() {
  return "feme.geometry.input_primitive";
}

StringRef getGeometryOutputPrimitiveAttrName() {
  return "feme.geometry.output_primitive";
}

StringRef getGeometryInvocationsAttrName() {
  return "feme.geometry.invocations";
}

StringRef getGeometryMaxOutputVerticesAttrName() {
  return "feme.geometry.max_output_vertices";
}

static std::optional<GeometryInputPrimitive>
parseInputPrimitiveAttr(StringRef Attr) {
  if (Attr == "points")
    return GeometryInputPrimitive::Points;
  if (Attr == "lines")
    return GeometryInputPrimitive::Lines;
  if (Attr == "lines_adjacency")
    return GeometryInputPrimitive::LinesAdjacency;
  if (Attr == "triangles")
    return GeometryInputPrimitive::Triangles;
  if (Attr == "triangles_adjacency")
    return GeometryInputPrimitive::TrianglesAdjacency;
  return std::nullopt;
}

static std::optional<GeometryOutputPrimitive>
parseOutputPrimitiveAttr(StringRef Attr) {
  if (Attr == "points")
    return GeometryOutputPrimitive::Points;
  if (Attr == "line_strip")
    return GeometryOutputPrimitive::LineStrip;
  if (Attr == "triangle_strip")
    return GeometryOutputPrimitive::TriangleStrip;
  return std::nullopt;
}

std::optional<GeometryState> getGeometryState(const Function &F) {
  bool HasInputPrimitive =
      F.hasFnAttribute(getGeometryInputPrimitiveAttrName());
  bool HasOutputPrimitive =
      F.hasFnAttribute(getGeometryOutputPrimitiveAttrName());
  bool HasMaxOutputVertices =
      F.hasFnAttribute(getGeometryMaxOutputVerticesAttrName());
  if (!HasInputPrimitive && !HasOutputPrimitive && !HasMaxOutputVertices)
    return std::nullopt;

  // SPIR-V requires a geometry entry point to declare exactly one input
  // primitive mode, one output primitive mode and one `OutputVertices`
  // execution mode, so a well-formed entry always carries all three
  // attributes together; treat a partial set (a malformed or hand-written
  // test module) as absent rather than guessing a default for the missing
  // piece.
  if (!HasInputPrimitive || !HasOutputPrimitive || !HasMaxOutputVertices)
    return std::nullopt;

  GeometryState State;
  auto InputPrimitive = parseInputPrimitiveAttr(
      F.getFnAttribute(getGeometryInputPrimitiveAttrName()).getValueAsString());
  auto OutputPrimitive = parseOutputPrimitiveAttr(
      F.getFnAttribute(getGeometryOutputPrimitiveAttrName())
          .getValueAsString());
  if (!InputPrimitive || !OutputPrimitive)
    return std::nullopt;
  State.InputPrimitive = *InputPrimitive;
  State.OutputPrimitive = *OutputPrimitive;

  StringRef MaxOutputVertices =
      F.getFnAttribute(getGeometryMaxOutputVerticesAttrName())
          .getValueAsString();
  uint32_t MaxOutputVerticesValue = 0;
  if (MaxOutputVertices.getAsInteger(10, MaxOutputVerticesValue))
    return std::nullopt;
  State.MaxOutputVertices = MaxOutputVerticesValue;

  // `Invocations` defaults to 1 (see `GeometryState::Invocations`'s own
  // comment) if this entry point's module never declared the execution
  // mode at all, which the attribute's own absence signals here.
  if (F.hasFnAttribute(getGeometryInvocationsAttrName())) {
    StringRef Invocations =
        F.getFnAttribute(getGeometryInvocationsAttrName()).getValueAsString();
    uint32_t InvocationsValue = 0;
    if (Invocations.getAsInteger(10, InvocationsValue))
      return std::nullopt;
    State.Invocations = InvocationsValue;
  }
  return State;
}

} // namespace feme::graphics
