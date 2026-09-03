//===- SwapchainTest.cpp - VkSwapchainKHR tests -----------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "EntryPoints.h"
#include "Icd.h"
#include "Objects.h"

#include "gtest/gtest.h"

#include <vector>

using namespace feme::vulkan;

namespace {

class SwapchainTest : public ::testing::Test {
protected:
  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    float Priority = 1.0f;
    VkDeviceQueueCreateInfo QueueInfo{};
    QueueInfo.queueFamilyIndex = 0;
    QueueInfo.queueCount = 1;
    QueueInfo.pQueuePriorities = &Priority;
    VkDeviceCreateInfo DevInfo{};
    DevInfo.queueCreateInfoCount = 1;
    DevInfo.pQueueCreateInfos = &QueueInfo;
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);

    VkHeadlessSurfaceCreateInfoEXT SurfaceInfo{};
    SurfaceInfo.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
    ASSERT_EQ(
        vkCreateHeadlessSurfaceEXT(Instance, &SurfaceInfo, nullptr, &Surface),
        VK_SUCCESS);
  }
  void TearDown() override {
    vkDestroySurfaceKHR(Instance, Surface, nullptr);
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  VkSwapchainCreateInfoKHR defaultCreateInfo() const {
    VkSwapchainCreateInfoKHR Info{};
    Info.surface = Surface;
    Info.minImageCount = 2;
    Info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    Info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    Info.imageExtent = {64, 64};
    Info.imageArrayLayers = 1;
    Info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    Info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    Info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    Info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    Info.clipped = VK_TRUE;
    return Info;
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  VkSurfaceKHR Surface = VK_NULL_HANDLE;
};

TEST_F(SwapchainTest, CreateDestroy) {
  VkSwapchainCreateInfoKHR Info = defaultCreateInfo();
  VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSwapchainKHR(Device, &Info, nullptr, &Swapchain),
            VK_SUCCESS);
  ASSERT_NE(Swapchain, VK_NULL_HANDLE);
  vkDestroySwapchainKHR(Device, Swapchain, nullptr);
}

TEST_F(SwapchainTest, GetSwapchainImagesReturnsMinImageCountRealImages) {
  VkSwapchainCreateInfoKHR Info = defaultCreateInfo();
  VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSwapchainKHR(Device, &Info, nullptr, &Swapchain),
            VK_SUCCESS);

  uint32_t Count = 0;
  ASSERT_EQ(vkGetSwapchainImagesKHR(Device, Swapchain, &Count, nullptr),
            VK_SUCCESS);
  EXPECT_EQ(Count, Info.minImageCount);

  std::vector<VkImage> Images(Count);
  ASSERT_EQ(vkGetSwapchainImagesKHR(Device, Swapchain, &Count, Images.data()),
            VK_SUCCESS);
  for (VkImage Img : Images)
    EXPECT_NE(Img, VK_NULL_HANDLE);

  vkDestroySwapchainKHR(Device, Swapchain, nullptr);
}

TEST_F(SwapchainTest, AcquirePresentRoundTrip) {
  VkSwapchainCreateInfoKHR Info = defaultCreateInfo();
  Info.minImageCount = 2;
  VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSwapchainKHR(Device, &Info, nullptr, &Swapchain),
            VK_SUCCESS);

  VkSemaphoreCreateInfo SemInfo{};
  VkSemaphore Sem = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSemaphore(Device, &SemInfo, nullptr, &Sem), VK_SUCCESS);

  uint32_t ImageIndex = UINT32_MAX;
  ASSERT_EQ(vkAcquireNextImageKHR(Device, Swapchain, UINT64_MAX, Sem,
                                  VK_NULL_HANDLE, &ImageIndex),
            VK_SUCCESS);
  EXPECT_LT(ImageIndex, Info.minImageCount);

  VkQueue Queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(Device, 0, 0, &Queue);
  ASSERT_NE(Queue, VK_NULL_HANDLE);

  VkPresentInfoKHR PresentInfo{};
  PresentInfo.waitSemaphoreCount = 1;
  PresentInfo.pWaitSemaphores = &Sem;
  PresentInfo.swapchainCount = 1;
  PresentInfo.pSwapchains = &Swapchain;
  PresentInfo.pImageIndices = &ImageIndex;
  VkResult PerSwapchainResult = VK_ERROR_UNKNOWN;
  PresentInfo.pResults = &PerSwapchainResult;
  EXPECT_EQ(vkQueuePresentKHR(Queue, &PresentInfo), VK_SUCCESS);
  EXPECT_EQ(PerSwapchainResult, VK_SUCCESS);

  vkDestroySemaphore(Device, Sem, nullptr);
  vkDestroySwapchainKHR(Device, Swapchain, nullptr);
}

TEST_F(SwapchainTest, AcquireFailsOnceEveryImageIsAcquired) {
  VkSwapchainCreateInfoKHR Info = defaultCreateInfo();
  Info.minImageCount = 1;
  VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSwapchainKHR(Device, &Info, nullptr, &Swapchain),
            VK_SUCCESS);

  uint32_t ImageIndex = UINT32_MAX;
  ASSERT_EQ(vkAcquireNextImageKHR(Device, Swapchain, UINT64_MAX, VK_NULL_HANDLE,
                                  VK_NULL_HANDLE, &ImageIndex),
            VK_SUCCESS);
  // Every one of this swapchain's (single) image is now `Acquired` -- see
  // Swapchain.h's own comment on why no further acquire could ever
  // succeed without an intervening present on this synchronous ICD.
  uint32_t Second = UINT32_MAX;
  EXPECT_EQ(vkAcquireNextImageKHR(Device, Swapchain, UINT64_MAX, VK_NULL_HANDLE,
                                  VK_NULL_HANDLE, &Second),
            VK_TIMEOUT);

  vkDestroySwapchainKHR(Device, Swapchain, nullptr);
}

TEST_F(SwapchainTest, PresentingAnUnacquiredImageFails) {
  VkSwapchainCreateInfoKHR Info = defaultCreateInfo();
  VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSwapchainKHR(Device, &Info, nullptr, &Swapchain),
            VK_SUCCESS);

  VkQueue Queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(Device, 0, 0, &Queue);

  uint32_t ImageIndex = 0; // Never acquired.
  VkPresentInfoKHR PresentInfo{};
  PresentInfo.swapchainCount = 1;
  PresentInfo.pSwapchains = &Swapchain;
  PresentInfo.pImageIndices = &ImageIndex;
  EXPECT_EQ(vkQueuePresentKHR(Queue, &PresentInfo),
            VK_ERROR_INITIALIZATION_FAILED);

  vkDestroySwapchainKHR(Device, Swapchain, nullptr);
}

TEST_F(SwapchainTest, OldSwapchainIsRetiredAndCannotAcquireAgain) {
  VkSwapchainCreateInfoKHR Info = defaultCreateInfo();
  VkSwapchainKHR Old = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSwapchainKHR(Device, &Info, nullptr, &Old), VK_SUCCESS);

  VkSwapchainCreateInfoKHR NewInfo = Info;
  NewInfo.oldSwapchain = Old;
  VkSwapchainKHR New = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSwapchainKHR(Device, &NewInfo, nullptr, &New), VK_SUCCESS);

  uint32_t ImageIndex = UINT32_MAX;
  EXPECT_EQ(vkAcquireNextImageKHR(Device, Old, UINT64_MAX, VK_NULL_HANDLE,
                                  VK_NULL_HANDLE, &ImageIndex),
            VK_ERROR_OUT_OF_DATE_KHR);

  vkDestroySwapchainKHR(Device, New, nullptr);
  vkDestroySwapchainKHR(Device, Old, nullptr);
}

TEST_F(SwapchainTest, RejectsExtentBeyondDeviceLimits) {
  VkSwapchainCreateInfoKHR Info = defaultCreateInfo();
  Info.imageExtent = {0, 64};
  VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateSwapchainKHR(Device, &Info, nullptr, &Swapchain),
            VK_ERROR_INITIALIZATION_FAILED);
}

} // namespace
