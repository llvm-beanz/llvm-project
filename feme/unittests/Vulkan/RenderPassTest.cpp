//===- RenderPassTest.cpp - VkRenderPass/VkFramebuffer tests ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (V6) Covers the render-target object model: `VkRenderPass` compiling into
// normalized attachment/subpass descriptions, `VkFramebuffer` binding image
// views to them, and the creation-time rejection of any format, sample
// count, or subpass shape this driver cannot honor -- "a layout the driver
// cannot honor fails at render-pass creation, not at draw time" (see
// "Render passes and dynamic rendering" in feme/docs/FeMeVulkanDesign.md).
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "RenderPass.h"
#include "EntryPoints.h"
#include "Icd.h"
#include "Image.h"
#include "Objects.h"

#include "gtest/gtest.h"

#include <vector>

using namespace feme::vulkan;

namespace {

class RenderPassTest : public ::testing::Test {
protected:
  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    VkDeviceCreateInfo DevInfo{};
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);
  }
  void TearDown() override {
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  /// A one-color-attachment render pass over \p Format at \p Samples.
  VkResult createSimpleRenderPass(VkFormat Format, VkRenderPass &Out,
                                  VkSampleCountFlagBits Samples =
                                      VK_SAMPLE_COUNT_1_BIT) {
    VkAttachmentDescription Attachment{};
    Attachment.format = Format;
    Attachment.samples = Samples;
    Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    Attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkAttachmentReference ColorRef{};
    ColorRef.attachment = 0;
    ColorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription Subpass{};
    Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Subpass.colorAttachmentCount = 1;
    Subpass.pColorAttachments = &ColorRef;

    VkRenderPassCreateInfo Info{};
    Info.attachmentCount = 1;
    Info.pAttachments = &Attachment;
    Info.subpassCount = 1;
    Info.pSubpasses = &Subpass;
    return vkCreateRenderPass(Device, &Info, nullptr, &Out);
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
};

TEST_F(RenderPassTest, CompilesAttachmentsAndSubpasses) {
  VkRenderPass Pass = VK_NULL_HANDLE;
  ASSERT_EQ(createSimpleRenderPass(VK_FORMAT_R8G8B8A8_UNORM, Pass), VK_SUCCESS);
  ASSERT_NE(Pass, VK_NULL_HANDLE);

  const auto *Obj = fromHandle<RenderPass>(Pass);
  ASSERT_EQ(Obj->attachments().size(), 1u);
  EXPECT_EQ(Obj->attachments()[0].Format,
            feme::cpu::ResourceFormat::R8G8B8A8_UNORM);
  EXPECT_EQ(Obj->attachments()[0].SampleCount, 1u);
  EXPECT_EQ(Obj->attachments()[0].LoadOp, VK_ATTACHMENT_LOAD_OP_CLEAR);
  EXPECT_EQ(Obj->attachments()[0].StoreOp, VK_ATTACHMENT_STORE_OP_STORE);
  ASSERT_EQ(Obj->subpasses().size(), 1u);
  ASSERT_EQ(Obj->subpasses()[0].ColorAttachments.size(), 1u);
  EXPECT_EQ(Obj->subpasses()[0].ColorAttachments[0], 0u);
  EXPECT_EQ(Obj->subpasses()[0].DepthStencilAttachment,
            uint32_t(VK_ATTACHMENT_UNUSED));

  vkDestroyRenderPass(Device, Pass, nullptr);
}

TEST_F(RenderPassTest, CompilesDepthAttachment) {
  VkAttachmentDescription Attachments[2]{};
  Attachments[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  Attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].format = VK_FORMAT_D32_SFLOAT;
  Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;

  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference DepthRef{
      1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  Subpass.pDepthStencilAttachment = &DepthRef;

  VkRenderPassCreateInfo Info{};
  Info.attachmentCount = 2;
  Info.pAttachments = Attachments;
  Info.subpassCount = 1;
  Info.pSubpasses = &Subpass;

  VkRenderPass Pass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &Info, nullptr, &Pass), VK_SUCCESS);
  const auto *Obj = fromHandle<RenderPass>(Pass);
  EXPECT_EQ(Obj->subpasses()[0].DepthStencilAttachment, 1u);
  EXPECT_EQ(Obj->attachments()[1].Format, feme::cpu::ResourceFormat::D32_FLOAT);
  vkDestroyRenderPass(Device, Pass, nullptr);
}

/// A packed depth/stencil format has no `feme::graphics` representation
/// (depth and stencil are two separate single-component images there), so it
/// is rejected at creation rather than silently misinterpreted.
TEST_F(RenderPassTest, RejectsUnsupportedAttachmentFormat) {
  VkRenderPass Pass = VK_NULL_HANDLE;
  EXPECT_EQ(createSimpleRenderPass(VK_FORMAT_D24_UNORM_S8_UINT, Pass),
            VK_ERROR_FORMAT_NOT_SUPPORTED);
  EXPECT_EQ(createSimpleRenderPass(VK_FORMAT_R5G6B5_UNORM_PACK16, Pass),
            VK_ERROR_FORMAT_NOT_SUPPORTED);
}

TEST_F(RenderPassTest, RejectsUnsupportedSampleCount) {
  VkRenderPass Pass = VK_NULL_HANDLE;
  EXPECT_EQ(createSimpleRenderPass(VK_FORMAT_R8G8B8A8_UNORM, Pass,
                                   VK_SAMPLE_COUNT_8_BIT),
            VK_ERROR_FORMAT_NOT_SUPPORTED);
  EXPECT_EQ(createSimpleRenderPass(VK_FORMAT_R8G8B8A8_UNORM, Pass,
                                   VK_SAMPLE_COUNT_4_BIT),
            VK_SUCCESS);
  vkDestroyRenderPass(Device, Pass, nullptr);
}

/// Input attachments would need either tile-local subpass merging or a
/// sampled-image round trip; neither is implemented, so a subpass declaring
/// one fails creation instead of rendering something wrong.
TEST_F(RenderPassTest, RejectsInputAttachments) {
  VkAttachmentDescription Attachment{};
  Attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachment.samples = VK_SAMPLE_COUNT_1_BIT;

  VkAttachmentReference Ref{0, VK_IMAGE_LAYOUT_GENERAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.inputAttachmentCount = 1;
  Subpass.pInputAttachments = &Ref;

  VkRenderPassCreateInfo Info{};
  Info.attachmentCount = 1;
  Info.pAttachments = &Attachment;
  Info.subpassCount = 1;
  Info.pSubpasses = &Subpass;

  VkRenderPass Pass = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateRenderPass(Device, &Info, nullptr, &Pass),
            VK_ERROR_INITIALIZATION_FAILED);
}

TEST_F(RenderPassTest, FramebufferBindsMatchingViews) {
  VkRenderPass Pass = VK_NULL_HANDLE;
  ASSERT_EQ(createSimpleRenderPass(VK_FORMAT_R8G8B8A8_UNORM, Pass), VK_SUCCESS);

  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ImageInfo.extent = {4, 4, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = 1;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  VkImage Img = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &Img), VK_SUCCESS);

  VkImageViewCreateInfo ViewInfo{};
  ViewInfo.image = Img;
  ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  ViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  ViewInfo.subresourceRange.levelCount = 1;
  ViewInfo.subresourceRange.layerCount = 1;
  VkImageView View = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateImageView(Device, &ViewInfo, nullptr, &View), VK_SUCCESS);

  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = Pass;
  FbInfo.attachmentCount = 1;
  FbInfo.pAttachments = &View;
  FbInfo.width = 4;
  FbInfo.height = 4;
  FbInfo.layers = 1;
  VkFramebuffer Fb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &Fb), VK_SUCCESS);
  EXPECT_EQ(fromHandle<Framebuffer>(Fb)->attachments().size(), 1u);
  EXPECT_EQ(fromHandle<Framebuffer>(Fb)->width(), 4u);

  // A framebuffer whose attachment count disagrees with its render pass's
  // is rejected.
  FbInfo.attachmentCount = 0;
  FbInfo.pAttachments = nullptr;
  VkFramebuffer Bad = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &Bad),
            VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyFramebuffer(Device, Fb, nullptr);
  vkDestroyImageView(Device, View, nullptr);
  vkDestroyImage(Device, Img, nullptr);
  vkDestroyRenderPass(Device, Pass, nullptr);
}

} // namespace
