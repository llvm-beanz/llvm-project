//===- Pipeline.cpp - FeMe software graphics executor pipeline ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Pipeline.h"

using namespace feme::graphics;

GraphicsPipeline::GraphicsPipeline(
    std::shared_ptr<cpu::CompiledStage> VertexStage,
    std::shared_ptr<cpu::CompiledStage> FragmentStage,
    PrimitiveTopology Topology, RasterState Raster, DepthState Depth,
    BlendMode Blend, uint32_t SampleCount,
    std::vector<AttachmentFormat> Attachments)
    : VertexStage(std::move(VertexStage)),
      FragmentStage(std::move(FragmentStage)), Topology(Topology),
      Raster(Raster), Depth(Depth), Blend(Blend), SampleCount(SampleCount),
      Attachments(std::move(Attachments)) {}
