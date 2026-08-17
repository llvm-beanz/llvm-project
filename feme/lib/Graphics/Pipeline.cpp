//===- Pipeline.cpp - FeMe software graphics executor pipeline ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Pipeline.h"

#include "llvm/Support/ErrorHandling.h"

#include <cassert>

using namespace feme::graphics;

GraphicsPipeline::GraphicsPipeline(
    std::shared_ptr<cpu::CompiledStage> VertexStage,
    std::shared_ptr<cpu::CompiledStage> FragmentStage,
    PrimitiveTopology Topology, RasterState Raster, DepthState Depth,
    BlendMode Blend, uint32_t SampleCount,
    std::vector<AttachmentFormat> Attachments, StencilState Stencil,
    std::vector<BlendState> ColorBlends, bool LogicOpEnable, LogicOp Logic,
    std::array<float, 4> BlendConstants)
    : VertexStage(std::move(VertexStage)),
      FragmentStage(std::move(FragmentStage)), Topology(Topology),
      Raster(Raster), Depth(Depth), Blend(Blend), SampleCount(SampleCount),
      Attachments(std::move(Attachments)), Stencil(Stencil),
      ColorBlends(std::move(ColorBlends)), LogicOpEnable(LogicOpEnable),
      Logic(Logic), BlendConstants(BlendConstants) {}

bool feme::graphics::topologyHasAdjacency(PrimitiveTopology Topology) {
  switch (Topology) {
  case PrimitiveTopology::LineListWithAdjacency:
  case PrimitiveTopology::LineStripWithAdjacency:
  case PrimitiveTopology::TriangleListWithAdjacency:
  case PrimitiveTopology::TriangleStripWithAdjacency:
    return true;
  case PrimitiveTopology::PointList:
  case PrimitiveTopology::LineList:
  case PrimitiveTopology::LineStrip:
  case PrimitiveTopology::TriangleList:
  case PrimitiveTopology::TriangleStrip:
    return false;
  }
  llvm_unreachable("unhandled PrimitiveTopology");
}

PrimitiveTopology feme::graphics::stripAdjacency(PrimitiveTopology Topology) {
  switch (Topology) {
  case PrimitiveTopology::LineListWithAdjacency:
    return PrimitiveTopology::LineList;
  case PrimitiveTopology::LineStripWithAdjacency:
    return PrimitiveTopology::LineStrip;
  case PrimitiveTopology::TriangleListWithAdjacency:
    return PrimitiveTopology::TriangleList;
  case PrimitiveTopology::TriangleStripWithAdjacency:
    return PrimitiveTopology::TriangleStrip;
  case PrimitiveTopology::PointList:
  case PrimitiveTopology::LineList:
  case PrimitiveTopology::LineStrip:
  case PrimitiveTopology::TriangleList:
  case PrimitiveTopology::TriangleStrip:
    return Topology;
  }
  llvm_unreachable("unhandled PrimitiveTopology");
}

uint32_t
feme::graphics::getListPrimitiveVertexCount(PrimitiveTopology Topology) {
  switch (Topology) {
  case PrimitiveTopology::PointList:
    return 1;
  case PrimitiveTopology::LineList:
    return 2;
  case PrimitiveTopology::LineListWithAdjacency:
    return 4;
  case PrimitiveTopology::TriangleList:
    return 3;
  case PrimitiveTopology::TriangleListWithAdjacency:
    return 6;
  case PrimitiveTopology::LineStrip:
  case PrimitiveTopology::LineStripWithAdjacency:
  case PrimitiveTopology::TriangleStrip:
  case PrimitiveTopology::TriangleStripWithAdjacency:
    llvm_unreachable(
        "getListPrimitiveVertexCount does not support strip topologies -- "
        "see splitListPrimitiveAdjacency's scope note");
  }
  llvm_unreachable("unhandled PrimitiveTopology");
}

SplitPrimitiveAdjacency feme::graphics::splitListPrimitiveAdjacency(
    PrimitiveTopology Topology, llvm::ArrayRef<uint32_t> FetchedIndices) {
  assert(FetchedIndices.size() == getListPrimitiveVertexCount(Topology) &&
         "wrong fetched index count for this topology");
  SplitPrimitiveAdjacency Split;
  switch (Topology) {
  case PrimitiveTopology::PointList:
  case PrimitiveTopology::LineList:
  case PrimitiveTopology::TriangleList:
    Split.Primitive.assign(FetchedIndices.begin(), FetchedIndices.end());
    break;
  case PrimitiveTopology::LineListWithAdjacency:
    // (adj0, v0, v1, adj1)
    Split.Adjacent.push_back(FetchedIndices[0]);
    Split.Primitive.push_back(FetchedIndices[1]);
    Split.Primitive.push_back(FetchedIndices[2]);
    Split.Adjacent.push_back(FetchedIndices[3]);
    break;
  case PrimitiveTopology::TriangleListWithAdjacency:
    // (v0, adj01, v1, adj12, v2, adj20)
    Split.Primitive.push_back(FetchedIndices[0]);
    Split.Adjacent.push_back(FetchedIndices[1]);
    Split.Primitive.push_back(FetchedIndices[2]);
    Split.Adjacent.push_back(FetchedIndices[3]);
    Split.Primitive.push_back(FetchedIndices[4]);
    Split.Adjacent.push_back(FetchedIndices[5]);
    break;
  case PrimitiveTopology::LineStrip:
  case PrimitiveTopology::LineStripWithAdjacency:
  case PrimitiveTopology::TriangleStrip:
  case PrimitiveTopology::TriangleStripWithAdjacency:
    llvm_unreachable("splitListPrimitiveAdjacency does not support strip "
                     "topologies -- see its own scope note");
  }
  return Split;
}
