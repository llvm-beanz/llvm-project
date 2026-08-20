//===- DescriptorTest.cpp - Descriptor set layout/pool/set tests ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "Descriptor.h"
#include "Buffer.h"
#include "EntryPoints.h"
#include "Icd.h"
#include "Image.h"
#include "Objects.h"

#include "gtest/gtest.h"

using namespace feme::vulkan;

namespace {

class DescriptorTest : public ::testing::Test {
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

  VkBuffer createStorageBuffer(VkDeviceSize Size) {
    VkBufferCreateInfo BufferInfo{};
    BufferInfo.size = Size;
    BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkBuffer Buf = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf), VK_SUCCESS);
    return Buf;
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
};

TEST_F(DescriptorTest, CreateSetLayoutWithStorageBuffers) {
  VkDescriptorSetLayoutBinding Bindings[2]{};
  Bindings[0].binding = 0;
  Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Bindings[0].descriptorCount = 1;
  Bindings[1].binding = 1;
  Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
  Bindings[1].descriptorCount = 2;

  VkDescriptorSetLayoutCreateInfo LayoutInfo{};
  LayoutInfo.bindingCount = 2;
  LayoutInfo.pBindings = Bindings;
  VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorSetLayout(Device, &LayoutInfo, nullptr, &Layout),
            VK_SUCCESS);
  ASSERT_NE(Layout, VK_NULL_HANDLE);

  auto *L = fromHandle<DescriptorSetLayout>(Layout);
  EXPECT_EQ(L->find(0)->Count, 1u);
  EXPECT_EQ(L->find(1)->Count, 2u);
  EXPECT_EQ(L->dynamicOffsetCount(), 2u); // Only binding 1 is dynamic.

  vkDestroyDescriptorSetLayout(Device, Layout, nullptr);
}

TEST_F(DescriptorTest, InputAttachmentDescriptorTypeIsAccepted) {
  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Binding.descriptorCount = 1;

  VkDescriptorSetLayoutCreateInfo LayoutInfo{};
  LayoutInfo.bindingCount = 1;
  LayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateDescriptorSetLayout(Device, &LayoutInfo, nullptr, &Layout),
            VK_SUCCESS);
  vkDestroyDescriptorSetLayout(Device, Layout, nullptr);
}

TEST_F(DescriptorTest, GetLayoutSupportReportsSupportedBindings) {
  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Binding.descriptorCount = 1;

  VkDescriptorSetLayoutCreateInfo LayoutInfo{};
  LayoutInfo.bindingCount = 1;
  LayoutInfo.pBindings = &Binding;

  VkDescriptorSetLayoutSupport Support{};
  vkGetDescriptorSetLayoutSupport(Device, &LayoutInfo, &Support);
  EXPECT_EQ(Support.supported, VK_TRUE);
}

TEST_F(DescriptorTest, GetLayoutSupportReportsInputAttachmentBindings) {
  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Binding.descriptorCount = 1;

  VkDescriptorSetLayoutCreateInfo LayoutInfo{};
  LayoutInfo.bindingCount = 1;
  LayoutInfo.pBindings = &Binding;

  VkDescriptorSetLayoutSupport Support{};
  vkGetDescriptorSetLayoutSupport(Device, &LayoutInfo, &Support);
  EXPECT_EQ(Support.supported, VK_TRUE);
}

TEST_F(DescriptorTest, InputAttachmentPoolSizeTypeIsAccepted) {
  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1};
  VkDescriptorPoolCreateInfo PoolInfo{};
  PoolInfo.maxSets = 1;
  PoolInfo.poolSizeCount = 1;
  PoolInfo.pPoolSizes = &PoolSize;
  VkDescriptorPool Pool = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &Pool),
            VK_SUCCESS);
  vkDestroyDescriptorPool(Device, Pool, nullptr);
}

TEST_F(DescriptorTest, AllocateUpdateAndReadBackWrite) {
  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Binding.descriptorCount = 1;
  VkDescriptorSetLayoutCreateInfo LayoutInfo{};
  LayoutInfo.bindingCount = 1;
  LayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorSetLayout(Device, &LayoutInfo, nullptr, &Layout),
            VK_SUCCESS);

  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
  VkDescriptorPoolCreateInfo PoolInfo{};
  PoolInfo.maxSets = 1;
  PoolInfo.poolSizeCount = 1;
  PoolInfo.pPoolSizes = &PoolSize;
  VkDescriptorPool Pool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &Pool),
            VK_SUCCESS);

  VkDescriptorSetAllocateInfo AllocInfo{};
  AllocInfo.descriptorPool = Pool;
  AllocInfo.descriptorSetCount = 1;
  AllocInfo.pSetLayouts = &Layout;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateDescriptorSets(Device, &AllocInfo, &Set), VK_SUCCESS);
  ASSERT_NE(Set, VK_NULL_HANDLE);

  VkBuffer Buf = createStorageBuffer(256);
  VkDescriptorBufferInfo BufInfo{Buf, 16, 64};
  VkWriteDescriptorSet Write{};
  Write.dstSet = Set;
  Write.dstBinding = 0;
  Write.descriptorCount = 1;
  Write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Write.pBufferInfo = &BufInfo;
  vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);

  auto *S = fromHandle<DescriptorSet>(Set);
  llvm::ArrayRef<DescriptorBufferBinding> Array = S->bindingArray(0);
  ASSERT_EQ(Array.size(), 1u);
  EXPECT_EQ(Array[0].Buf, fromHandle<Buffer>(Buf));
  EXPECT_EQ(Array[0].Offset, 16u);
  EXPECT_EQ(Array[0].Range, 64u);

  ASSERT_EQ(vkFreeDescriptorSets(Device, Pool, 1, &Set), VK_SUCCESS);
  vkDestroyBuffer(Device, Buf, nullptr);
  vkDestroyDescriptorPool(Device, Pool, nullptr);
  vkDestroyDescriptorSetLayout(Device, Layout, nullptr);
}

/// Found missing entirely -- a segfault through a null device-dispatch-
/// table entry, not merely a rejection -- by the first real Vulkan-CTS run
/// against this ICD (`dEQP-VK.binding_model.descriptorset_random.*`, which
/// exercises `vkCreateDescriptorUpdateTemplate` heavily). Writes the same
/// buffer binding `AllocateUpdateAndReadBackWrite` writes through
/// `vkUpdateDescriptorSets`, but through a template over a raw byte buffer
/// instead, and expects the identical `DescriptorBufferBinding` result.
TEST_F(DescriptorTest, UpdateWithTemplateMatchesDirectWrite) {
  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Binding.descriptorCount = 1;
  VkDescriptorSetLayoutCreateInfo LayoutInfo{};
  LayoutInfo.bindingCount = 1;
  LayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorSetLayout(Device, &LayoutInfo, nullptr, &Layout),
            VK_SUCCESS);

  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
  VkDescriptorPoolCreateInfo PoolInfo{};
  PoolInfo.maxSets = 1;
  PoolInfo.poolSizeCount = 1;
  PoolInfo.pPoolSizes = &PoolSize;
  VkDescriptorPool Pool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &Pool),
            VK_SUCCESS);

  VkDescriptorSetAllocateInfo AllocInfo{};
  AllocInfo.descriptorPool = Pool;
  AllocInfo.descriptorSetCount = 1;
  AllocInfo.pSetLayouts = &Layout;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateDescriptorSets(Device, &AllocInfo, &Set), VK_SUCCESS);

  VkDescriptorUpdateTemplateEntry Entry{};
  Entry.dstBinding = 0;
  Entry.descriptorCount = 1;
  Entry.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Entry.offset = 0;
  Entry.stride = sizeof(VkDescriptorBufferInfo);
  VkDescriptorUpdateTemplateCreateInfo TemplateInfo{};
  TemplateInfo.descriptorUpdateEntryCount = 1;
  TemplateInfo.pDescriptorUpdateEntries = &Entry;
  TemplateInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
  VkDescriptorUpdateTemplate Template = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorUpdateTemplate(Device, &TemplateInfo, nullptr,
                                             &Template),
            VK_SUCCESS);

  VkBuffer Buf = createStorageBuffer(256);
  VkDescriptorBufferInfo BufInfo{Buf, 16, 64};
  vkUpdateDescriptorSetWithTemplate(Device, Set, Template, &BufInfo);

  auto *S = fromHandle<DescriptorSet>(Set);
  llvm::ArrayRef<DescriptorBufferBinding> Array = S->bindingArray(0);
  ASSERT_EQ(Array.size(), 1u);
  EXPECT_EQ(Array[0].Buf, fromHandle<Buffer>(Buf));
  EXPECT_EQ(Array[0].Offset, 16u);
  EXPECT_EQ(Array[0].Range, 64u);

  vkDestroyDescriptorUpdateTemplate(Device, Template, nullptr);
  ASSERT_EQ(vkFreeDescriptorSets(Device, Pool, 1, &Set), VK_SUCCESS);
  vkDestroyBuffer(Device, Buf, nullptr);
  vkDestroyDescriptorPool(Device, Pool, nullptr);
  vkDestroyDescriptorSetLayout(Device, Layout, nullptr);
}

/// `_PUSH_DESCRIPTORS_KHR` needs `VK_KHR_push_descriptor`, which this ICD
/// does not implement -- rejected at creation rather than accepted and
/// then behaving as an ordinary descriptor-set template.
TEST_F(DescriptorTest, PushDescriptorTemplateTypeIsRejected) {
  VkDescriptorUpdateTemplateEntry Entry{};
  Entry.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Entry.descriptorCount = 1;
  VkDescriptorUpdateTemplateCreateInfo TemplateInfo{};
  TemplateInfo.descriptorUpdateEntryCount = 1;
  TemplateInfo.pDescriptorUpdateEntries = &Entry;
  TemplateInfo.templateType =
      VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;
  VkDescriptorUpdateTemplate Template = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateDescriptorUpdateTemplate(Device, &TemplateInfo, nullptr,
                                             &Template),
            VK_ERROR_INITIALIZATION_FAILED);
}

TEST_F(DescriptorTest, PoolExhaustionFailsAllocation) {
  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Binding.descriptorCount = 1;
  VkDescriptorSetLayoutCreateInfo LayoutInfo{};
  LayoutInfo.bindingCount = 1;
  LayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorSetLayout(Device, &LayoutInfo, nullptr, &Layout),
            VK_SUCCESS);

  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
  VkDescriptorPoolCreateInfo PoolInfo{};
  PoolInfo.maxSets = 1;
  PoolInfo.poolSizeCount = 1;
  PoolInfo.pPoolSizes = &PoolSize;
  VkDescriptorPool Pool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &Pool),
            VK_SUCCESS);

  VkDescriptorSetLayout Layouts[2] = {Layout, Layout};
  VkDescriptorSetAllocateInfo AllocInfo{};
  AllocInfo.descriptorPool = Pool;
  AllocInfo.descriptorSetCount = 2;
  AllocInfo.pSetLayouts = Layouts;
  VkDescriptorSet Sets[2] = {reinterpret_cast<VkDescriptorSet>(1),
                             reinterpret_cast<VkDescriptorSet>(1)};
  EXPECT_EQ(vkAllocateDescriptorSets(Device, &AllocInfo, Sets),
            VK_ERROR_OUT_OF_POOL_MEMORY);
  EXPECT_EQ(Sets[0], VK_NULL_HANDLE);
  EXPECT_EQ(Sets[1], VK_NULL_HANDLE);

  vkDestroyDescriptorPool(Device, Pool, nullptr);
  vkDestroyDescriptorSetLayout(Device, Layout, nullptr);
}

TEST_F(DescriptorTest, ResetDescriptorPoolRestoresCapacity) {
  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Binding.descriptorCount = 1;
  VkDescriptorSetLayoutCreateInfo LayoutInfo{};
  LayoutInfo.bindingCount = 1;
  LayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorSetLayout(Device, &LayoutInfo, nullptr, &Layout),
            VK_SUCCESS);

  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
  VkDescriptorPoolCreateInfo PoolInfo{};
  PoolInfo.maxSets = 1;
  PoolInfo.poolSizeCount = 1;
  PoolInfo.pPoolSizes = &PoolSize;
  VkDescriptorPool Pool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &Pool),
            VK_SUCCESS);

  VkDescriptorSetAllocateInfo AllocInfo{};
  AllocInfo.descriptorPool = Pool;
  AllocInfo.descriptorSetCount = 1;
  AllocInfo.pSetLayouts = &Layout;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateDescriptorSets(Device, &AllocInfo, &Set), VK_SUCCESS);

  ASSERT_EQ(vkResetDescriptorPool(Device, Pool, 0), VK_SUCCESS);
  ASSERT_EQ(vkAllocateDescriptorSets(Device, &AllocInfo, &Set), VK_SUCCESS);
  EXPECT_NE(Set, VK_NULL_HANDLE);

  vkDestroyDescriptorPool(Device, Pool, nullptr);
  vkDestroyDescriptorSetLayout(Device, Layout, nullptr);
}

TEST_F(DescriptorTest, UniformBufferLayoutAcceptedAndDynamicOffsetCounted) {
  // V3: uniform buffers (see Descriptor.h's file comment for this
  // milestone's object-model-only scope).
  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  Binding.descriptorCount = 1;
  VkDescriptorSetLayoutCreateInfo LayoutInfo{};
  LayoutInfo.bindingCount = 1;
  LayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorSetLayout(Device, &LayoutInfo, nullptr, &Layout),
            VK_SUCCESS);
  EXPECT_EQ(fromHandle<DescriptorSetLayout>(Layout)->dynamicOffsetCount(), 1u);

  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1};
  VkDescriptorPoolCreateInfo PoolInfo{};
  PoolInfo.maxSets = 1;
  PoolInfo.poolSizeCount = 1;
  PoolInfo.pPoolSizes = &PoolSize;
  VkDescriptorPool Pool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &Pool),
            VK_SUCCESS);

  VkDescriptorSetAllocateInfo AllocInfo{};
  AllocInfo.descriptorPool = Pool;
  AllocInfo.descriptorSetCount = 1;
  AllocInfo.pSetLayouts = &Layout;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateDescriptorSets(Device, &AllocInfo, &Set), VK_SUCCESS);

  VkBuffer Buf = createStorageBuffer(64);
  VkDescriptorBufferInfo BufInfo{Buf, 0, 32};
  VkWriteDescriptorSet Write{};
  Write.dstSet = Set;
  Write.dstBinding = 0;
  Write.descriptorCount = 1;
  Write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  Write.pBufferInfo = &BufInfo;
  vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);

  auto *S = fromHandle<DescriptorSet>(Set);
  llvm::ArrayRef<DescriptorBufferBinding> Array = S->bindingArray(0);
  ASSERT_EQ(Array.size(), 1u);
  EXPECT_EQ(Array[0].Buf, fromHandle<Buffer>(Buf));
  EXPECT_EQ(Array[0].Range, 32u);

  vkDestroyBuffer(Device, Buf, nullptr);
  vkDestroyDescriptorPool(Device, Pool, nullptr);
  vkDestroyDescriptorSetLayout(Device, Layout, nullptr);
}

TEST_F(DescriptorTest, InputAttachmentWriteAndReadBack) {
  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Binding.descriptorCount = 1;
  VkDescriptorSetLayoutCreateInfo LayoutInfo{};
  LayoutInfo.bindingCount = 1;
  LayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorSetLayout(Device, &LayoutInfo, nullptr, &Layout),
            VK_SUCCESS);

  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1};
  VkDescriptorPoolCreateInfo PoolInfo{};
  PoolInfo.maxSets = 1;
  PoolInfo.poolSizeCount = 1;
  PoolInfo.pPoolSizes = &PoolSize;
  VkDescriptorPool Pool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &Pool),
            VK_SUCCESS);

  VkDescriptorSetAllocateInfo AllocInfo{};
  AllocInfo.descriptorPool = Pool;
  AllocInfo.descriptorSetCount = 1;
  AllocInfo.pSetLayouts = &Layout;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateDescriptorSets(Device, &AllocInfo, &Set), VK_SUCCESS);

  VkDescriptorImageInfo ImageInfo{};
  ImageInfo.imageView = reinterpret_cast<VkImageView>(0x1234);
  ImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  VkWriteDescriptorSet Write{};
  Write.dstSet = Set;
  Write.dstBinding = 0;
  Write.descriptorCount = 1;
  Write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Write.pImageInfo = &ImageInfo;
  vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);

  auto *S = fromHandle<DescriptorSet>(Set);
  llvm::ArrayRef<DescriptorImageBinding> Array = S->imageBindingArray(0);
  ASSERT_EQ(Array.size(), 1u);
  EXPECT_EQ(Array[0].View, fromHandle<ImageView>(ImageInfo.imageView));
  EXPECT_EQ(Array[0].Samp, nullptr);
  EXPECT_EQ(Array[0].Layout, VK_IMAGE_LAYOUT_GENERAL);
  EXPECT_TRUE(S->bindingArray(0).empty());

  ASSERT_EQ(vkFreeDescriptorSets(Device, Pool, 1, &Set), VK_SUCCESS);
  vkDestroyDescriptorPool(Device, Pool, nullptr);
  vkDestroyDescriptorSetLayout(Device, Layout, nullptr);
}

/// (V5) A `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` binding writes both
/// halves of `DescriptorImageBinding` from one `VkDescriptorImageInfo`, and
/// is materialized through `imageBindingArray` rather than `bindingArray`
/// (see `DescriptorSet`'s constructor).
TEST_F(DescriptorTest, CombinedImageSamplerWriteAndReadBack) {
  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  Binding.descriptorCount = 1;
  VkDescriptorSetLayoutCreateInfo LayoutInfo{};
  LayoutInfo.bindingCount = 1;
  LayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorSetLayout(Device, &LayoutInfo, nullptr, &Layout),
            VK_SUCCESS);

  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
  VkDescriptorPoolCreateInfo PoolInfo{};
  PoolInfo.maxSets = 1;
  PoolInfo.poolSizeCount = 1;
  PoolInfo.pPoolSizes = &PoolSize;
  VkDescriptorPool Pool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &Pool),
            VK_SUCCESS);

  VkDescriptorSetAllocateInfo AllocInfo{};
  AllocInfo.descriptorPool = Pool;
  AllocInfo.descriptorSetCount = 1;
  AllocInfo.pSetLayouts = &Layout;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateDescriptorSets(Device, &AllocInfo, &Set), VK_SUCCESS);

  VkSamplerCreateInfo SamplerInfo{};
  VkSampler Samp = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSampler(Device, &SamplerInfo, nullptr, &Samp), VK_SUCCESS);

  VkDescriptorImageInfo ImageInfo{};
  ImageInfo.sampler = Samp;
  ImageInfo.imageView = reinterpret_cast<VkImageView>(0x1234);
  ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkWriteDescriptorSet Write{};
  Write.dstSet = Set;
  Write.dstBinding = 0;
  Write.descriptorCount = 1;
  Write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  Write.pImageInfo = &ImageInfo;
  vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);

  auto *S = fromHandle<DescriptorSet>(Set);
  llvm::ArrayRef<DescriptorImageBinding> Array = S->imageBindingArray(0);
  ASSERT_EQ(Array.size(), 1u);
  EXPECT_EQ(Array[0].View, fromHandle<ImageView>(ImageInfo.imageView));
  EXPECT_EQ(Array[0].Samp, fromHandle<Sampler>(Samp));
  EXPECT_EQ(Array[0].Layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  // The plain (non-image) binding array is unaffected.
  EXPECT_TRUE(S->bindingArray(0).empty());

  vkDestroySampler(Device, Samp, nullptr);
  vkDestroyDescriptorPool(Device, Pool, nullptr);
  vkDestroyDescriptorSetLayout(Device, Layout, nullptr);
}

} // namespace
