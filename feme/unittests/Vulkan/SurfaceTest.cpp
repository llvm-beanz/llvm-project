//===- SurfaceTest.cpp - VkSurfaceKHR tests ---------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "Surface.h"
#include "EntryPoints.h"
#include "Icd.h"
#include "Objects.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace feme::vulkan;

namespace {

class SurfaceTest : public ::testing::Test {
protected:
  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);

    VkHeadlessSurfaceCreateInfoEXT SurfaceInfo{};
    SurfaceInfo.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
    ASSERT_EQ(
        vkCreateHeadlessSurfaceEXT(Instance, &SurfaceInfo, nullptr, &Surface),
        VK_SUCCESS);
    ASSERT_NE(Surface, VK_NULL_HANDLE);
  }
  void TearDown() override {
    vkDestroySurfaceKHR(Instance, Surface, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkSurfaceKHR Surface = VK_NULL_HANDLE;
};

TEST(SurfaceInstance, EnumerateInstanceExtensionsReportsSurfaceExtensions) {
  uint32_t Count = 0;
  ASSERT_EQ(vkEnumerateInstanceExtensionProperties(nullptr, &Count, nullptr),
            VK_SUCCESS);
  ASSERT_GE(Count, 2u);
  std::vector<VkExtensionProperties> Extensions(Count);
  ASSERT_EQ(vkEnumerateInstanceExtensionProperties(nullptr, &Count,
                                                   Extensions.data()),
            VK_SUCCESS);

  bool HasSurface = false, HasHeadless = false;
  for (const VkExtensionProperties &Ext : Extensions) {
    HasSurface |= std::strcmp(Ext.extensionName, "VK_KHR_surface") == 0;
    HasHeadless |=
        std::strcmp(Ext.extensionName, "VK_EXT_headless_surface") == 0;
    // (roadmap H10) `VK_KHR_swapchain` is `type="device"`, so it must
    // *not* appear in the instance-level list -- the whole point of this
    // milestone's own instance/device extension split.
    EXPECT_STRNE(Ext.extensionName, "VK_KHR_swapchain");
  }
  EXPECT_TRUE(HasSurface);
  EXPECT_TRUE(HasHeadless);
}

TEST(SurfaceInstance, CreateInstanceAcceptsSurfaceExtensions) {
  const char *Exts[] = {"VK_KHR_surface", "VK_EXT_headless_surface"};
  VkInstanceCreateInfo CreateInfo{};
  CreateInfo.enabledExtensionCount = 2;
  CreateInfo.ppEnabledExtensionNames = Exts;
  VkInstance Instance = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateInstance(&CreateInfo, nullptr, &Instance), VK_SUCCESS);
  vkDestroyInstance(Instance, nullptr);
}

TEST(SurfaceInstance, CreateInstanceRejectsDeviceOnlyExtensionByName) {
  // `VK_KHR_swapchain` is a real, implemented extension name (Swapchain.h)
  // -- just not one this ICD ever accepts at the *instance* level.
  const char *Ext = "VK_KHR_swapchain";
  VkInstanceCreateInfo CreateInfo{};
  CreateInfo.enabledExtensionCount = 1;
  CreateInfo.ppEnabledExtensionNames = &Ext;
  VkInstance Instance = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateInstance(&CreateInfo, nullptr, &Instance),
            VK_ERROR_EXTENSION_NOT_PRESENT);
}

TEST_F(SurfaceTest, SurfaceSupportIsAlwaysTrue) {
  VkBool32 Supported = VK_FALSE;
  EXPECT_EQ(
      vkGetPhysicalDeviceSurfaceSupportKHR(Physical, 0, Surface, &Supported),
      VK_SUCCESS);
  EXPECT_EQ(Supported, VK_TRUE);
}

TEST_F(SurfaceTest, CapabilitiesReportHeadlessSentinelExtent) {
  VkSurfaceCapabilitiesKHR Caps{};
  ASSERT_EQ(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Physical, Surface, &Caps),
            VK_SUCCESS);
  EXPECT_EQ(Caps.minImageCount, 1u);
  EXPECT_EQ(Caps.currentExtent.width, UINT32_MAX);
  EXPECT_EQ(Caps.currentExtent.height, UINT32_MAX);
  EXPECT_GE(Caps.maxImageExtent.width, Caps.minImageExtent.width);
  EXPECT_NE(Caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0u);
}

TEST_F(SurfaceTest, FormatsIncludeMandatoryPair) {
  uint32_t Count = 0;
  ASSERT_EQ(
      vkGetPhysicalDeviceSurfaceFormatsKHR(Physical, Surface, &Count, nullptr),
      VK_SUCCESS);
  ASSERT_GT(Count, 0u);
  std::vector<VkSurfaceFormatKHR> Formats(Count);
  ASSERT_EQ(vkGetPhysicalDeviceSurfaceFormatsKHR(Physical, Surface, &Count,
                                                 Formats.data()),
            VK_SUCCESS);
  bool HasBGRA = false;
  for (const VkSurfaceFormatKHR &Fmt : Formats)
    HasBGRA |= Fmt.format == VK_FORMAT_B8G8R8A8_UNORM &&
               Fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  EXPECT_TRUE(HasBGRA);
}

TEST_F(SurfaceTest, PresentModesIncludeFifo) {
  uint32_t Count = 0;
  ASSERT_EQ(vkGetPhysicalDeviceSurfacePresentModesKHR(Physical, Surface, &Count,
                                                      nullptr),
            VK_SUCCESS);
  ASSERT_GT(Count, 0u);
  std::vector<VkPresentModeKHR> Modes(Count);
  ASSERT_EQ(vkGetPhysicalDeviceSurfacePresentModesKHR(Physical, Surface, &Count,
                                                      Modes.data()),
            VK_SUCCESS);
  EXPECT_NE(std::find(Modes.begin(), Modes.end(), VK_PRESENT_MODE_FIFO_KHR),
            Modes.end());
}

// Roadmap H10a's own `Surface` object-model coverage that doesn't need a
// real xcb connection (XcbSurface.cpp's real `presentToSurface`/
// `currentSurfaceExtent` bodies, which do, are only exercised end-to-end
// by feme-vulkan-xcb-smoke.cpp / xcb-surface-smoke.test since they need a
// live X server).
TEST(SurfaceObjectModel, DefaultConstructedSurfaceIsHeadless) {
  Surface Surf;
  EXPECT_EQ(Surf.kind(), SurfaceKind::Headless);
  EXPECT_EQ(Surf.xcbConnection(), nullptr);
  EXPECT_EQ(Surf.xcbWindow(), 0u);
}

TEST(SurfaceObjectModel, XcbConstructedSurfaceStoresOpaqueState) {
  // A fabricated (never dereferenced) connection pointer/window ID:
  // `kind()`/the accessors are plain getters, so this doesn't need a real
  // `xcb_connection_t *`.
  int FakeConnection = 0;
  Surface Surf(&FakeConnection, 42);
  EXPECT_EQ(Surf.kind(), SurfaceKind::Xcb);
  EXPECT_EQ(Surf.xcbConnection(), &FakeConnection);
  EXPECT_EQ(Surf.xcbWindow(), 42u);
}

TEST(SurfaceObjectModel, PresentToHeadlessSurfaceIsNoop) {
  Surface Surf;
  uint8_t Pixels[4] = {0, 0, 0, 0};
  EXPECT_TRUE(presentToSurface(&Surf, Pixels, 1, 1, /*SwapRedBlue=*/true));
}

TEST(SurfaceObjectModel, CurrentExtentOfHeadlessSurfaceIsNullopt) {
  Surface Surf;
  EXPECT_EQ(currentSurfaceExtent(&Surf), std::nullopt);
}

// Roadmap H10j: `rowsPerPutImageChunk`'s own chunk-size arithmetic --
// needs no real `xcb_connection_t` (see its own declaration in Surface.h
// for why), so it's fully covered here rather than only end-to-end by
// `feme-vulkan-xcb-smoke.cpp`'s own real (but small, single-chunk) image.
TEST(RowsPerPutImageChunk, WholeImageFitsInOneRequest) {
  // A tiny image comfortably inside a real request-size cap: every
  // scanline fits in one chunk.
  EXPECT_EQ(rowsPerPutImageChunk(/*MaxRequestBytes=*/262144,
                                 /*HeaderBytes=*/24, /*RowBytes=*/256),
            1023u);
}

TEST(RowsPerPutImageChunk, LargeImageIsSplitAcrossMultipleRequests) {
  // A wide enough row that only a handful fit per request -- exercises
  // the actual division, not just the "everything fits" case above.
  EXPECT_EQ(rowsPerPutImageChunk(/*MaxRequestBytes=*/1024,
                                 /*HeaderBytes=*/24, /*RowBytes=*/100),
            10u);
}

TEST(RowsPerPutImageChunk, OversizedSingleScanlineStillGetsOne) {
  // A single scanline already larger than the whole request cap: still
  // has to be attempted as its own request rather than never sent (see
  // this function's own "no fixed upper bound on image size" contract).
  EXPECT_EQ(rowsPerPutImageChunk(/*MaxRequestBytes=*/1024,
                                 /*HeaderBytes=*/24, /*RowBytes=*/4096),
            1u);
}

TEST(RowsPerPutImageChunk, HeaderAtOrAboveCapStillGetsOne) {
  // A degenerate connection whose own reported cap doesn't even leave
  // room for one request's fixed header: still returns `1`, never `0` or
  // a division by a non-positive remainder.
  EXPECT_EQ(rowsPerPutImageChunk(/*MaxRequestBytes=*/24, /*HeaderBytes=*/24,
                                 /*RowBytes=*/64),
            1u);
  EXPECT_EQ(rowsPerPutImageChunk(/*MaxRequestBytes=*/16, /*HeaderBytes=*/24,
                                 /*RowBytes=*/64),
            1u);
}

TEST(RowsPerPutImageChunk, ZeroRowBytesStillGetsOne) {
  // A zero-width image (`RowBytes == 0`) must not divide by zero.
  EXPECT_EQ(rowsPerPutImageChunk(/*MaxRequestBytes=*/262144,
                                 /*HeaderBytes=*/24, /*RowBytes=*/0),
            1u);
}

} // namespace
