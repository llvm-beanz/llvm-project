//===- Tessellation.cpp - Tessellation state attributes -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Tessellation.h"

#include "llvm/IR/Function.h"

using namespace llvm;

namespace feme::graphics {

StringRef getTessellationDomainAttrName() { return "feme.tessellation.domain"; }

StringRef getTessellationPartitioningAttrName() {
  return "feme.tessellation.partitioning";
}

StringRef getTessellationOutputPrimitiveAttrName() {
  return "feme.tessellation.output_primitive";
}

StringRef getTessellationOutputControlPointCountAttrName() {
  return "feme.tessellation.output_control_points";
}

static std::optional<TessellatorDomain> parseDomainAttr(StringRef Attr) {
  if (Attr == "isoline")
    return TessellatorDomain::Isoline;
  if (Attr == "triangle")
    return TessellatorDomain::Triangle;
  if (Attr == "quad")
    return TessellatorDomain::Quad;
  return std::nullopt;
}

static std::optional<TessPartitioning> parsePartitioningAttr(StringRef Attr) {
  if (Attr == "integer")
    return TessPartitioning::Integer;
  if (Attr == "fractional_odd")
    return TessPartitioning::FractionalOdd;
  if (Attr == "fractional_even")
    return TessPartitioning::FractionalEven;
  return std::nullopt;
}

static std::optional<TessOutputPrimitive>
parseOutputPrimitiveAttr(StringRef Attr) {
  if (Attr == "point")
    return TessOutputPrimitive::Point;
  if (Attr == "line")
    return TessOutputPrimitive::Line;
  if (Attr == "triangle_cw")
    return TessOutputPrimitive::TriangleCw;
  if (Attr == "triangle_ccw")
    return TessOutputPrimitive::TriangleCcw;
  return std::nullopt;
}

std::optional<TessellationState> getTessellationState(const Function &F) {
  bool HasDomain = F.hasFnAttribute(getTessellationDomainAttrName());
  bool HasOutputControlPointCount =
      F.hasFnAttribute(getTessellationOutputControlPointCountAttrName());
  if (!HasDomain && !HasOutputControlPointCount)
    return std::nullopt;

  TessellationState State;

  // The tessellation-evaluation-only fields (SPIR-V's `Triangles`/`Quads`/
  // `Isolines`, spacing and vertex-order/point-mode execution modes) always
  // arrive together -- see ConvertSPIRVToLLVMPass.cpp's
  // applyEntryPointAttributes -- so \p F either carries all three or none.
  if (HasDomain) {
    auto Domain = parseDomainAttr(
        F.getFnAttribute(getTessellationDomainAttrName()).getValueAsString());
    auto Partitioning = parsePartitioningAttr(
        F.getFnAttribute(getTessellationPartitioningAttrName())
            .getValueAsString());
    auto OutputPrimitive = parseOutputPrimitiveAttr(
        F.getFnAttribute(getTessellationOutputPrimitiveAttrName())
            .getValueAsString());
    if (!Domain || !Partitioning || !OutputPrimitive)
      return std::nullopt;
    State.Domain = *Domain;
    State.Partitioning = *Partitioning;
    State.OutputPrimitive = *OutputPrimitive;
  }

  // The tessellation-control-only output control point count (SPIR-V's
  // `OutputVertices`) is a wholly separate execution mode, absent from a
  // tessellation-evaluation entry point's own attributes entirely: leave
  // `TessellationState::OutputControlPointCount` at its default for one.
  if (HasOutputControlPointCount) {
    StringRef Count =
        F.getFnAttribute(getTessellationOutputControlPointCountAttrName())
            .getValueAsString();
    uint32_t OutputControlPointCount = 0;
    if (Count.getAsInteger(10, OutputControlPointCount))
      return std::nullopt;
    State.OutputControlPointCount = OutputControlPointCount;
  }
  return State;
}

} // namespace feme::graphics
