//===- SyncTest.cpp - VkFence / vkQueueSubmit tests ----------------------===//
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

#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Target/SPIRV/Serialization.h"
#include "llvm/ADT/SmallVector.h"

#include "gtest/gtest.h"

using namespace feme::vulkan;

namespace {

std::vector<uint32_t> assembleSPIRV(llvm::StringRef Source) {
  mlir::MLIRContext Ctx;
  Ctx.loadDialect<mlir::spirv::SPIRVDialect>();
  mlir::OwningOpRef<mlir::spirv::ModuleOp> Module =
      mlir::parseSourceString<mlir::spirv::ModuleOp>(Source, &Ctx);
  if (!Module)
    return {};
  llvm::SmallVector<uint32_t, 64> Binary;
  if (mlir::failed(mlir::spirv::serialize(*Module, Binary)))
    return {};
  return std::vector<uint32_t>(Binary.begin(), Binary.end());
}

const char *kEmptyComputeShader = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
)mlir";

class SyncTest : public ::testing::Test {
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
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device),
              VK_SUCCESS);
    vkGetDeviceQueue(Device, 0, 0, &Queue);

    VkPipelineLayoutCreateInfo LayoutInfo{};
    ASSERT_EQ(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout),
              VK_SUCCESS);

    std::vector<uint32_t> Words = assembleSPIRV(kEmptyComputeShader);
    ASSERT_FALSE(Words.empty());
    VkShaderModuleCreateInfo ShaderInfo{};
    ShaderInfo.codeSize = Words.size() * sizeof(uint32_t);
    ShaderInfo.pCode = Words.data();
    ASSERT_EQ(vkCreateShaderModule(Device, &ShaderInfo, nullptr, &Module),
              VK_SUCCESS);

    VkComputePipelineCreateInfo PipelineInfo{};
    PipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    PipelineInfo.stage.module = Module;
    PipelineInfo.stage.pName = "main";
    PipelineInfo.layout = Layout;
    ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1,
                                       &PipelineInfo, nullptr, &Pipeline),
              VK_SUCCESS);

    VkCommandPoolCreateInfo PoolInfo{};
    PoolInfo.queueFamilyIndex = 0;
    ASSERT_EQ(vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool),
              VK_SUCCESS);

    VkCommandBufferAllocateInfo AllocInfo{};
    AllocInfo.commandPool = Pool;
    AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    AllocInfo.commandBufferCount = 1;
    ASSERT_EQ(vkAllocateCommandBuffers(Device, &AllocInfo, &CmdBuf),
              VK_SUCCESS);

    VkCommandBufferBeginInfo BeginInfo{};
    ASSERT_EQ(vkBeginCommandBuffer(CmdBuf, &BeginInfo), VK_SUCCESS);
    vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
    vkCmdDispatch(CmdBuf, 2, 2, 2);
    ASSERT_EQ(vkEndCommandBuffer(CmdBuf), VK_SUCCESS);
  }
  void TearDown() override {
    vkDestroyCommandPool(Device, Pool, nullptr);
    vkDestroyPipeline(Device, Pipeline, nullptr);
    vkDestroyShaderModule(Device, Module, nullptr);
    vkDestroyPipelineLayout(Device, Layout, nullptr);
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  VkQueue Queue = VK_NULL_HANDLE;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  VkShaderModule Module = VK_NULL_HANDLE;
  VkPipeline Pipeline = VK_NULL_HANDLE;
  VkCommandPool Pool = VK_NULL_HANDLE;
  VkCommandBuffer CmdBuf = VK_NULL_HANDLE;
};

// The V1 milestone's own end-to-end scenario: submit a recorded empty
// compute dispatch to a queue, and observe its fence signal.
TEST_F(SyncTest, SubmitDispatchAndWaitOnFence) {
  VkFenceCreateInfo FenceInfo{};
  VkFence Fence = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFence(Device, &FenceInfo, nullptr, &Fence), VK_SUCCESS);
  EXPECT_EQ(vkGetFenceStatus(Device, Fence), VK_NOT_READY);

  VkSubmitInfo Submit{};
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &CmdBuf;
  ASSERT_EQ(vkQueueSubmit(Queue, 1, &Submit, Fence), VK_SUCCESS);

  EXPECT_EQ(vkGetFenceStatus(Device, Fence), VK_SUCCESS);
  EXPECT_EQ(vkWaitForFences(Device, 1, &Fence, VK_TRUE, UINT64_MAX),
            VK_SUCCESS);
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);
  EXPECT_EQ(vkDeviceWaitIdle(Device), VK_SUCCESS);

  vkDestroyFence(Device, Fence, nullptr);
}

TEST_F(SyncTest, ResetFenceReturnsToUnsignaled) {
  VkFenceCreateInfo FenceInfo{};
  FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  VkFence Fence = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFence(Device, &FenceInfo, nullptr, &Fence), VK_SUCCESS);
  EXPECT_EQ(vkGetFenceStatus(Device, Fence), VK_SUCCESS);

  ASSERT_EQ(vkResetFences(Device, 1, &Fence), VK_SUCCESS);
  EXPECT_EQ(vkGetFenceStatus(Device, Fence), VK_NOT_READY);
  EXPECT_EQ(vkWaitForFences(Device, 1, &Fence, VK_TRUE, 0), VK_TIMEOUT);

  vkDestroyFence(Device, Fence, nullptr);
}

TEST_F(SyncTest, SubmitWithoutFenceSucceeds) {
  VkSubmitInfo Submit{};
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &CmdBuf;
  EXPECT_EQ(vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE), VK_SUCCESS);
}

TEST_F(SyncTest, SubmitRejectsSemaphores) {
  VkSemaphore FakeSemaphore = reinterpret_cast<VkSemaphore>(1);
  VkSubmitInfo Submit{};
  Submit.waitSemaphoreCount = 1;
  Submit.pWaitSemaphores = &FakeSemaphore;
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &CmdBuf;
  EXPECT_EQ(vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE),
            VK_ERROR_INITIALIZATION_FAILED);
}

} // namespace
