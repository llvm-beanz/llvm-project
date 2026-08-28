//===- Mesh.cpp - Mesh stage state attributes ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Mesh.h"

#include "llvm/IR/Function.h"

using namespace llvm;

namespace feme::graphics {

StringRef getMeshOutputTopologyAttrName() { return "feme.mesh.output_topology"; }

StringRef getMeshMaxOutputVerticesAttrName() {
  return "feme.mesh.max_output_vertices";
}

StringRef getMeshMaxOutputPrimitivesAttrName() {
  return "feme.mesh.max_output_primitives";
}

static std::optional<MeshOutputTopology>
parseOutputTopologyAttr(StringRef Attr) {
  if (Attr == "points")
    return MeshOutputTopology::Points;
  if (Attr == "lines")
    return MeshOutputTopology::Lines;
  if (Attr == "triangles")
    return MeshOutputTopology::Triangles;
  return std::nullopt;
}

std::optional<MeshState> getMeshState(const Function &F) {
  bool HasOutputTopology = F.hasFnAttribute(getMeshOutputTopologyAttrName());
  bool HasMaxOutputVertices =
      F.hasFnAttribute(getMeshMaxOutputVerticesAttrName());
  bool HasMaxOutputPrimitives =
      F.hasFnAttribute(getMeshMaxOutputPrimitivesAttrName());
  if (!HasOutputTopology && !HasMaxOutputVertices && !HasMaxOutputPrimitives)
    return std::nullopt;

  // SPIR-V requires a mesh entry point to declare exactly one output
  // topology mode, one `OutputVertices` execution mode and one
  // `OutputPrimitivesEXT` execution mode, so a well-formed entry always
  // carries all three attributes together; treat a partial set (a
  // malformed or hand-written test module) as absent rather than guessing
  // a default for the missing piece.
  if (!HasOutputTopology || !HasMaxOutputVertices || !HasMaxOutputPrimitives)
    return std::nullopt;

  MeshState State;
  auto OutputTopology = parseOutputTopologyAttr(
      F.getFnAttribute(getMeshOutputTopologyAttrName()).getValueAsString());
  if (!OutputTopology)
    return std::nullopt;
  State.OutputTopology = *OutputTopology;

  StringRef MaxOutputVertices =
      F.getFnAttribute(getMeshMaxOutputVerticesAttrName()).getValueAsString();
  uint32_t MaxOutputVerticesValue = 0;
  if (MaxOutputVertices.getAsInteger(10, MaxOutputVerticesValue))
    return std::nullopt;
  State.MaxOutputVertices = MaxOutputVerticesValue;

  StringRef MaxOutputPrimitives =
      F.getFnAttribute(getMeshMaxOutputPrimitivesAttrName())
          .getValueAsString();
  uint32_t MaxOutputPrimitivesValue = 0;
  if (MaxOutputPrimitives.getAsInteger(10, MaxOutputPrimitivesValue))
    return std::nullopt;
  State.MaxOutputPrimitives = MaxOutputPrimitivesValue;

  return State;
}

} // namespace feme::graphics
