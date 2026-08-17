//===- LayeredRendering.cpp - Render-target array layer selection --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/LayeredRendering.h"

using namespace feme::graphics;

std::optional<uint32_t>
feme::graphics::resolveRenderTargetArrayLayer(int32_t RequestedLayer,
                                              uint32_t LayerCount) {
  if (RequestedLayer < 0)
    return std::nullopt;
  auto Layer = static_cast<uint32_t>(RequestedLayer);
  if (Layer >= LayerCount)
    return std::nullopt;
  return Layer;
}

uint64_t feme::graphics::getAttachmentLayerByteOffset(uint32_t Layer,
                                                      uint64_t LayerSizeBytes) {
  return static_cast<uint64_t>(Layer) * LayerSizeBytes;
}
