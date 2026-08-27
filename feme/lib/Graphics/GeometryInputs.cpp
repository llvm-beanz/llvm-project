//===- GeometryInputs.cpp - Assembled-primitive-to-geometry-batch glue ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/GeometryInputs.h"

#include "feme/Target/CPU/RuntimeABI.h"

#include <algorithm>

using namespace feme::graphics;

std::vector<float>
feme::graphics::buildGeometryInputs(llvm::ArrayRef<uint32_t> VertexSlots,
                                    llvm::ArrayRef<float> VertexOutputs,
                                    uint32_t ScalarsPerVertex) {
  std::vector<float> Inputs(
      static_cast<size_t>(VertexSlots.size()) * ScalarsPerVertex, 0.0f);
  if (ScalarsPerVertex == 0)
    return Inputs;
  uint32_t VertexOutputCount = VertexOutputs.size() / ScalarsPerVertex;
  for (size_t Slot = 0; Slot != VertexSlots.size(); ++Slot) {
    uint32_t VertexOutputSlot = VertexSlots[Slot];
    if (VertexOutputSlot >= VertexOutputCount)
      continue;
    const float *Src =
        VertexOutputs.data() +
        static_cast<size_t>(VertexOutputSlot) * ScalarsPerVertex;
    float *Dst = Inputs.data() + Slot * ScalarsPerVertex;
    std::copy(Src, Src + ScalarsPerVertex, Dst);
  }
  return Inputs;
}

std::vector<feme::cpu::FemeGeometryInvocation>
feme::graphics::buildGeometryInvocations(
    llvm::ArrayRef<uint32_t> PrimitiveIDs,
    llvm::ArrayRef<uint32_t> InvocationIDs) {
  std::vector<cpu::FemeGeometryInvocation> Invocations;
  Invocations.reserve(PrimitiveIDs.size());
  for (size_t I = 0; I != PrimitiveIDs.size(); ++I) {
    cpu::FemeGeometryInvocation Invocation{};
    Invocation.PrimitiveID = PrimitiveIDs[I];
    Invocation.InvocationID = I < InvocationIDs.size() ? InvocationIDs[I] : 0;
    Invocations.push_back(Invocation);
  }
  return Invocations;
}
