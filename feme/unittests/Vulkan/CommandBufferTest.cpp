//===- CommandBufferTest.cpp - Command pool/buffer + dispatch tests -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "CommandBuffer.h"
#include "Buffer.h"
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
#include "llvm/Support/Error.h"
#include "llvm/Testing/Support/Error.h"

#include "gtest/gtest.h"

#include <cstring>

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

class CommandBufferTest : public ::testing::Test {
protected:
  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    VkDeviceCreateInfo DevInfo{};
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);

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
    ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &PipelineInfo,
                                       nullptr, &Pipeline),
              VK_SUCCESS);

    VkCommandPoolCreateInfo PoolInfo{};
    PoolInfo.queueFamilyIndex = 0;
    ASSERT_EQ(vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool),
              VK_SUCCESS);
  }
  void TearDown() override {
    vkDestroyCommandPool(Device, Pool, nullptr);
    vkDestroyPipeline(Device, Pipeline, nullptr);
    vkDestroyShaderModule(Device, Module, nullptr);
    vkDestroyPipelineLayout(Device, Layout, nullptr);
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  VkCommandBuffer allocateCommandBuffer() {
    VkCommandBufferAllocateInfo AllocInfo{};
    AllocInfo.commandPool = Pool;
    AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    AllocInfo.commandBufferCount = 1;
    VkCommandBuffer CmdBuf = VK_NULL_HANDLE;
    EXPECT_EQ(vkAllocateCommandBuffers(Device, &AllocInfo, &CmdBuf),
              VK_SUCCESS);
    return CmdBuf;
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  VkShaderModule Module = VK_NULL_HANDLE;
  VkPipeline Pipeline = VK_NULL_HANDLE;
  VkCommandPool Pool = VK_NULL_HANDLE;
};

TEST_F(CommandBufferTest, RecordAndExecuteDispatch) {
  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  ASSERT_NE(CmdBuf, VK_NULL_HANDLE);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(CmdBuf, &BeginInfo), VK_SUCCESS);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdDispatch(CmdBuf, 2, 3, 1);
  ASSERT_EQ(vkEndCommandBuffer(CmdBuf), VK_SUCCESS);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_EQ(Recorded->commands().size(), 2u);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());
}

TEST_F(CommandBufferTest, DispatchBaseOffsetsGroupID) {
  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdDispatchBase(CmdBuf, 4, 5, 6, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());
}

TEST_F(CommandBufferTest, DispatchIndirectReadsBuffer) {
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 16;
  BufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
  VkBuffer IndirectBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &IndirectBuf),
            VK_SUCCESS);

  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 16;
  AllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory), VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, IndirectBuf, Memory, 0), VK_SUCCESS);

  void *Data = nullptr;
  ASSERT_EQ(vkMapMemory(Device, Memory, 0, VK_WHOLE_SIZE, 0, &Data),
            VK_SUCCESS);
  uint32_t Dims[3] = {2, 1, 1};
  std::memcpy(Data, Dims, sizeof(Dims));
  vkUnmapMemory(Device, Memory);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdDispatchIndirect(CmdBuf, IndirectBuf, 0);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  vkDestroyBuffer(Device, IndirectBuf, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
}

TEST_F(CommandBufferTest, DispatchWithoutBoundPipelineFails) {
  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Failed());
}

TEST_F(CommandBufferTest, ResetCommandPoolClearsCommands) {
  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  ASSERT_EQ(vkResetCommandPool(Device, Pool, 0), VK_SUCCESS);
  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  EXPECT_TRUE(Recorded->commands().empty());
}

} // namespace
