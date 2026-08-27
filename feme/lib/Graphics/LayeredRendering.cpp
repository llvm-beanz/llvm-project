//===- LayeredRendering.cpp - Render-target array layer selection --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/LayeredRendering.h"

using namespace feme::graphics;

namespace {

std::optional<uint32_t> resolveArrayIndex(int32_t RequestedIndex,
                                          uint32_t Count) {
  if (RequestedIndex < 0)
    return std::nullopt;
  auto Index = static_cast<uint32_t>(RequestedIndex);
  if (Index >= Count)
    return std::nullopt;
  return Index;
}

} // namespace

std::optional<uint32_t>
feme::graphics::resolveRenderTargetArrayLayer(int32_t RequestedLayer,
                                              uint32_t LayerCount) {
  return resolveArrayIndex(RequestedLayer, LayerCount);
}

std::optional<uint32_t>
feme::graphics::resolveViewportArrayIndex(int32_t RequestedViewport,
                                          uint32_t ViewportCount) {
  return resolveArrayIndex(RequestedViewport, ViewportCount);
}

uint64_t feme::graphics::getAttachmentLayerByteOffset(uint32_t Layer,
                                                      uint64_t LayerSizeBytes) {
  return static_cast<uint64_t>(Layer) * LayerSizeBytes;
}
