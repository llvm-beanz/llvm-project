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
    std::array<float, 4> BlendConstants, bool PrimitiveRestartEnable,
    bool SampleShadingEnable, bool AlphaToOneEnable, bool AlphaToCoverageEnable,
    bool PreRasterViewIndexIsDeviceIndex, bool FragmentViewIndexIsDeviceIndex)
    : VertexStage(std::move(VertexStage)),
      FragmentStage(std::move(FragmentStage)), Topology(Topology),
      Raster(Raster), Depth(Depth), Blend(Blend), SampleCount(SampleCount),
      Attachments(std::move(Attachments)), Stencil(Stencil),
      ColorBlends(std::move(ColorBlends)), LogicOpEnable(LogicOpEnable),
      Logic(Logic), BlendConstants(BlendConstants),
      PrimitiveRestartEnable(PrimitiveRestartEnable),
      SampleShadingEnable(SampleShadingEnable),
      AlphaToOneEnable(AlphaToOneEnable),
      AlphaToCoverageEnable(AlphaToCoverageEnable),
      PreRasterViewIndexIsDeviceIndex(PreRasterViewIndexIsDeviceIndex),
      FragmentViewIndexIsDeviceIndex(FragmentViewIndexIsDeviceIndex) {}

void GraphicsPipeline::setTessellationStages(
    std::shared_ptr<cpu::CompiledStage> HullStage,
    std::shared_ptr<cpu::CompiledStage> PatchConstant,
    std::shared_ptr<cpu::CompiledStage> DomainStage, TessellationState State) {
  assert(HullStage && PatchConstant && DomainStage &&
         "a tessellation-enabled pipeline needs all three stages");
  this->HullStage = std::move(HullStage);
  this->PatchConstantStage = std::move(PatchConstant);
  this->DomainStage = std::move(DomainStage);
  this->Tessellation = State;
}

void GraphicsPipeline::setGeometryStage(
    std::shared_ptr<cpu::CompiledStage> GeometryStage, GeometryState State) {
  assert(GeometryStage && "a geometry-enabled pipeline needs its stage");
  this->GeometryStage = std::move(GeometryStage);
  this->Geometry = State;
}

void GraphicsPipeline::setMeshStage(
    std::shared_ptr<cpu::CompiledStage> TaskStage,
    std::shared_ptr<cpu::CompiledStage> MeshStage, MeshState State,
    AmplificationDispatchLimits MeshLimits,
    AmplificationDispatchLimits TaskLimits, uint32_t MaxTaskPayloadBytes) {
  assert(MeshStage && "a mesh pipeline needs its own mesh stage");
  // \p TaskStage is legitimately null: a mesh pipeline with no task stage
  // dispatches its mesh workgroups directly (`vkCmdDrawMeshTasksEXT`'s own
  // shape), rather than through a task entry's `EmitMeshTasksEXT`.
  this->TaskStage = std::move(TaskStage);
  this->MeshStage = std::move(MeshStage);
  this->Mesh = State;
  this->MeshLimits = MeshLimits;
  this->TaskLimits = TaskLimits;
  this->MaxTaskPayloadBytes = MaxTaskPayloadBytes;
}

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
  case PrimitiveTopology::TriangleFan:
  case PrimitiveTopology::PatchList:
    return false;
  }
  llvm_unreachable("unhandled PrimitiveTopology");
}

bool feme::graphics::topologySupportsPrimitiveRestart(
    PrimitiveTopology Topology) {
  switch (Topology) {
  case PrimitiveTopology::LineStrip:
  case PrimitiveTopology::TriangleStrip:
  case PrimitiveTopology::TriangleFan:
  case PrimitiveTopology::LineStripWithAdjacency:
  case PrimitiveTopology::TriangleStripWithAdjacency:
    return true;
  case PrimitiveTopology::PointList:
  case PrimitiveTopology::LineList:
  case PrimitiveTopology::TriangleList:
  case PrimitiveTopology::LineListWithAdjacency:
  case PrimitiveTopology::TriangleListWithAdjacency:
  case PrimitiveTopology::PatchList:
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
  case PrimitiveTopology::TriangleFan:
  case PrimitiveTopology::PatchList:
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
  case PrimitiveTopology::TriangleFan:
  case PrimitiveTopology::PatchList:
    llvm_unreachable(
        "getListPrimitiveVertexCount does not support strip or patch "
        "topologies -- "
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
  case PrimitiveTopology::TriangleFan:
  case PrimitiveTopology::PatchList:
    llvm_unreachable("splitListPrimitiveAdjacency does not support strip or "
                     "patch topologies -- see its own scope note");
  }
  return Split;
}

uint32_t feme::graphics::getStripPrimitiveCount(PrimitiveTopology Topology,
                                                uint32_t IndexCount) {
  switch (Topology) {
  case PrimitiveTopology::LineStripWithAdjacency:
    return IndexCount < 4 ? 0 : IndexCount - 3;
  case PrimitiveTopology::TriangleStripWithAdjacency:
    // (Roadmap L359) An odd `IndexCount` is not actually illegal -- it
    // simply leaves one trailing vertex unconsumed by any complete
    // 6-vertex window (exactly like a plain `TriangleStrip`'s own
    // trailing vertex when `IndexCount` is odd), so this must floor
    // rather than reject: unsigned integer division by 2 already floors
    // for an odd `(IndexCount - 4)` numerator (e.g. `3 / 2 == 1`), so no
    // explicit rounding is needed once the `% 2 != 0`-rejecting early
    // return (this function's own pre-`L359` behavior) is removed.
    return IndexCount < 6 ? 0 : (IndexCount - 4) / 2;
  case PrimitiveTopology::PointList:
  case PrimitiveTopology::LineList:
  case PrimitiveTopology::LineStrip:
  case PrimitiveTopology::TriangleList:
  case PrimitiveTopology::TriangleStrip:
  case PrimitiveTopology::TriangleFan:
  case PrimitiveTopology::LineListWithAdjacency:
  case PrimitiveTopology::TriangleListWithAdjacency:
  case PrimitiveTopology::PatchList:
    llvm_unreachable("getStripPrimitiveCount only supports the two "
                     "strip-with-adjacency topologies");
  }
  llvm_unreachable("unhandled PrimitiveTopology");
}

SplitPrimitiveAdjacency feme::graphics::splitStripPrimitiveAdjacency(
    PrimitiveTopology Topology, llvm::ArrayRef<uint32_t> FetchedIndices,
    uint32_t PrimitiveIndex) {
  assert(PrimitiveIndex <
             getStripPrimitiveCount(Topology, FetchedIndices.size()) &&
         "primitive index out of range for this strip");
  SplitPrimitiveAdjacency Split;
  switch (Topology) {
  case PrimitiveTopology::LineStripWithAdjacency: {
    // Window [i, i+3]: (adj0, v0, v1, adj1), advancing by 1 each primitive
    // ("leading vertices are 1, 2, 3, ...").
    uint32_t Base = PrimitiveIndex;
    Split.Adjacent.push_back(FetchedIndices[Base]);
    Split.Primitive.push_back(FetchedIndices[Base + 1]);
    Split.Primitive.push_back(FetchedIndices[Base + 2]);
    Split.Adjacent.push_back(FetchedIndices[Base + 3]);
    break;
  }
  case PrimitiveTopology::TriangleStripWithAdjacency: {
    // (Roadmap L359 follow-up) NOT a simple sliding window of 6 -- unlike
    // every other strip-with-adjacency topology here, GL_EXT_geometry_shader
    // section 10.1.7tsa's own Table 10.X1 special-cases the *first*,
    // *last*, and (for every interior primitive) the *odd-vs-even* primitive
    // index, because a strip triangle's two "sideways" adjacent vertices
    // come from whichever neighboring triangle hasn't already been visited
    // by the previous primitive's own window -- which neighbor that is
    // flips every other triangle. Concretely (0-indexed, table's own
    // 1-indexed vertex numbers minus 1), for primitive `i` of `N`:
    //   only (N == 1):           primitive {0, 2, 4},       adjacent {1, 5, 3}
    //   first (i == 0, N > 1):   primitive {0, 2, 4},       adjacent {1, 6, 3}
    //   middle, i odd:           primitive {2i+2, 2i, 2i+4}, adjacent {2i-2, 2i+3, 2i+6}
    //   middle, i even:          primitive {2i, 2i+2, 2i+4}, adjacent {2i-2, 2i+6, 2i+3}
    //   last, i odd:             primitive {2i+2, 2i, 2i+4}, adjacent {2i-2, 2i+3, 2i+5}
    //   last, i even:            primitive {2i, 2i+2, 2i+4}, adjacent {2i-2, 2i+5, 2i+3}
    // Getting this wrong doesn't just misorder vertices (which a
    // geometry shader could tolerate) -- it changes the *set* of 6
    // vertices read entirely, which is externally visible even to a
    // shader that doesn't care about `gl_in[]` order.
    uint32_t N = getStripPrimitiveCount(
        Topology, static_cast<uint32_t>(FetchedIndices.size()));
    uint32_t I = PrimitiveIndex;
    auto At = [&](uint32_t Idx) { return FetchedIndices[Idx]; };
    if (N == 1) {
      Split.Primitive = {At(0), At(2), At(4)};
      Split.Adjacent = {At(1), At(5), At(3)};
    } else if (I == 0) {
      Split.Primitive = {At(0), At(2), At(4)};
      Split.Adjacent = {At(1), At(6), At(3)};
    } else if (I == N - 1) {
      if (I % 2 == 1) {
        Split.Primitive = {At(2 * I + 2), At(2 * I), At(2 * I + 4)};
        Split.Adjacent = {At(2 * I - 2), At(2 * I + 3), At(2 * I + 5)};
      } else {
        Split.Primitive = {At(2 * I), At(2 * I + 2), At(2 * I + 4)};
        Split.Adjacent = {At(2 * I - 2), At(2 * I + 5), At(2 * I + 3)};
      }
    } else { // interior ("middle")
      if (I % 2 == 1) {
        Split.Primitive = {At(2 * I + 2), At(2 * I), At(2 * I + 4)};
        Split.Adjacent = {At(2 * I - 2), At(2 * I + 3), At(2 * I + 6)};
      } else {
        Split.Primitive = {At(2 * I), At(2 * I + 2), At(2 * I + 4)};
        Split.Adjacent = {At(2 * I - 2), At(2 * I + 6), At(2 * I + 3)};
      }
    }
    break;
  }
  case PrimitiveTopology::PointList:
  case PrimitiveTopology::LineList:
  case PrimitiveTopology::LineStrip:
  case PrimitiveTopology::TriangleList:
  case PrimitiveTopology::TriangleStrip:
  case PrimitiveTopology::TriangleFan:
  case PrimitiveTopology::LineListWithAdjacency:
  case PrimitiveTopology::TriangleListWithAdjacency:
  case PrimitiveTopology::PatchList:
    llvm_unreachable("splitStripPrimitiveAdjacency only supports the two "
                     "strip-with-adjacency topologies");
  }
  return Split;
}
