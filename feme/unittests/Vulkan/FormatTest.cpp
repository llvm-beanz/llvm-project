//===- FormatTest.cpp - VkFormat -> ResourceFormat mapping tests ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "Format.h"

#include "gtest/gtest.h"

using namespace feme::vulkan;
using feme::cpu::ResourceFormat;

namespace {

TEST(FormatTest, MapsIdentityFloatFormats) {
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R32_SFLOAT), ResourceFormat::R32_FLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R32G32B32A32_SFLOAT),
            ResourceFormat::R32G32B32A32_FLOAT);
}

TEST(FormatTest, MapsPackedFormat) {
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R8G8B8A8_UNORM),
            ResourceFormat::R8G8B8A8_UNORM);
}

TEST(FormatTest, MapsDepthStencilFormats) {
  EXPECT_EQ(mapVkFormat(VK_FORMAT_D32_SFLOAT), ResourceFormat::D32_FLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_D24_UNORM_S8_UINT),
            ResourceFormat::D24_UNORM_S8_UINT);
}

TEST(FormatTest, RejectsUnsupportedFormat) {
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC1_RGB_UNORM_BLOCK), std::nullopt);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_UNDEFINED), std::nullopt);
}

TEST(FormatTest, ElementSizeMatchesFormatWidth) {
  EXPECT_EQ(formatElementSize(ResourceFormat::R32_FLOAT), 4u);
  EXPECT_EQ(formatElementSize(ResourceFormat::R32G32_FLOAT), 8u);
  EXPECT_EQ(formatElementSize(ResourceFormat::R32G32B32_FLOAT), 12u);
  EXPECT_EQ(formatElementSize(ResourceFormat::R32G32B32A32_FLOAT), 16u);
  EXPECT_EQ(formatElementSize(ResourceFormat::R8G8B8A8_UNORM), 4u);
  EXPECT_EQ(formatElementSize(ResourceFormat::Unknown), 0u);
}

} // namespace
