//===- SyncTest.cpp - VkFence / vkQueueSubmit tests ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "CommandBuffer.h"
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
#include "llvm/Testing/Support/Error.h"

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
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);
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
    ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &PipelineInfo,
                                       nullptr, &Pipeline),
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

TEST_F(SyncTest, SubmitRejectsUnsignaledBinarySemaphore) {
  // Waiting on a binary semaphore nothing has signaled yet is a real
  // ordering error under this ICD's synchronous execution model (see
  // Sync.h's file comment): there is no future signal left to wait for.
  VkSemaphoreCreateInfo SemInfo{};
  VkSemaphore Sem = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSemaphore(Device, &SemInfo, nullptr, &Sem), VK_SUCCESS);

  VkSubmitInfo Submit{};
  Submit.waitSemaphoreCount = 1;
  Submit.pWaitSemaphores = &Sem;
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &CmdBuf;
  EXPECT_EQ(vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE),
            VK_ERROR_INITIALIZATION_FAILED);

  vkDestroySemaphore(Device, Sem, nullptr);
}

TEST_F(SyncTest, BinarySemaphoreSignalThenWaitSucceeds) {
  VkSemaphoreCreateInfo SemInfo{};
  VkSemaphore Sem = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSemaphore(Device, &SemInfo, nullptr, &Sem), VK_SUCCESS);

  VkSubmitInfo Signal{};
  Signal.signalSemaphoreCount = 1;
  Signal.pSignalSemaphores = &Sem;
  Signal.commandBufferCount = 1;
  Signal.pCommandBuffers = &CmdBuf;
  ASSERT_EQ(vkQueueSubmit(Queue, 1, &Signal, VK_NULL_HANDLE), VK_SUCCESS);

  VkSubmitInfo Wait{};
  Wait.waitSemaphoreCount = 1;
  Wait.pWaitSemaphores = &Sem;
  Wait.commandBufferCount = 1;
  Wait.pCommandBuffers = &CmdBuf;
  EXPECT_EQ(vkQueueSubmit(Queue, 1, &Wait, VK_NULL_HANDLE), VK_SUCCESS);

  // A binary semaphore is consumed by the wait: submitting a second wait
  // with nothing signaling it again in between must fail.
  EXPECT_EQ(vkQueueSubmit(Queue, 1, &Wait, VK_NULL_HANDLE),
            VK_ERROR_INITIALIZATION_FAILED);

  vkDestroySemaphore(Device, Sem, nullptr);
}

TEST_F(SyncTest, TimelineSemaphoreHostSignalAndWait) {
  VkSemaphoreTypeCreateInfo TypeInfo{};
  TypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
  TypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  TypeInfo.initialValue = 0;
  VkSemaphoreCreateInfo SemInfo{};
  SemInfo.pNext = &TypeInfo;
  VkSemaphore Sem = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSemaphore(Device, &SemInfo, nullptr, &Sem), VK_SUCCESS);

  uint64_t Value = 0;
  ASSERT_EQ(vkGetSemaphoreCounterValue(Device, Sem, &Value), VK_SUCCESS);
  EXPECT_EQ(Value, 0u);

  VkSemaphoreSignalInfo SignalInfo{};
  SignalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
  SignalInfo.semaphore = Sem;
  SignalInfo.value = 5;
  ASSERT_EQ(vkSignalSemaphore(Device, &SignalInfo), VK_SUCCESS);

  ASSERT_EQ(vkGetSemaphoreCounterValue(Device, Sem, &Value), VK_SUCCESS);
  EXPECT_EQ(Value, 5u);

  uint64_t WaitValue = 5;
  VkSemaphoreWaitInfo WaitInfo{};
  WaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
  WaitInfo.semaphoreCount = 1;
  WaitInfo.pSemaphores = &Sem;
  WaitInfo.pValues = &WaitValue;
  EXPECT_EQ(vkWaitSemaphores(Device, &WaitInfo, UINT64_MAX), VK_SUCCESS);

  uint64_t TooHigh = 6;
  WaitInfo.pValues = &TooHigh;
  EXPECT_EQ(vkWaitSemaphores(Device, &WaitInfo, 0), VK_TIMEOUT);

  vkDestroySemaphore(Device, Sem, nullptr);
}

TEST_F(SyncTest, TimelineSemaphoreAcrossQueueSubmit) {
  VkSemaphoreTypeCreateInfo TypeInfo{};
  TypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
  TypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  VkSemaphoreCreateInfo SemInfo{};
  SemInfo.pNext = &TypeInfo;
  VkSemaphore Sem = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSemaphore(Device, &SemInfo, nullptr, &Sem), VK_SUCCESS);

  uint64_t SignalValue = 3;
  VkTimelineSemaphoreSubmitInfo TimelineInfo{};
  TimelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
  TimelineInfo.signalSemaphoreValueCount = 1;
  TimelineInfo.pSignalSemaphoreValues = &SignalValue;
  VkSubmitInfo Submit{};
  Submit.pNext = &TimelineInfo;
  Submit.signalSemaphoreCount = 1;
  Submit.pSignalSemaphores = &Sem;
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &CmdBuf;
  ASSERT_EQ(vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE), VK_SUCCESS);

  uint64_t Value = 0;
  ASSERT_EQ(vkGetSemaphoreCounterValue(Device, Sem, &Value), VK_SUCCESS);
  EXPECT_EQ(Value, SignalValue);

  vkDestroySemaphore(Device, Sem, nullptr);
}

// Roadmap E3: `vkQueueSubmit2`'s own end-to-end scenario, mirroring
// `SubmitDispatchAndWaitOnFence` above through the `VkSubmitInfo2`/
// `VkCommandBufferSubmitInfo` shape instead.
TEST_F(SyncTest, QueueSubmit2DispatchAndWaitOnFence) {
  VkFenceCreateInfo FenceInfo{};
  VkFence Fence = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFence(Device, &FenceInfo, nullptr, &Fence), VK_SUCCESS);
  EXPECT_EQ(vkGetFenceStatus(Device, Fence), VK_NOT_READY);

  VkCommandBufferSubmitInfo CmdBufInfo{};
  CmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
  CmdBufInfo.commandBuffer = CmdBuf;
  VkSubmitInfo2 Submit{};
  Submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  Submit.commandBufferInfoCount = 1;
  Submit.pCommandBufferInfos = &CmdBufInfo;
  ASSERT_EQ(vkQueueSubmit2(Queue, 1, &Submit, Fence), VK_SUCCESS);

  EXPECT_EQ(vkGetFenceStatus(Device, Fence), VK_SUCCESS);
  EXPECT_EQ(vkWaitForFences(Device, 1, &Fence, VK_TRUE, UINT64_MAX),
            VK_SUCCESS);

  vkDestroyFence(Device, Fence, nullptr);
}

TEST_F(SyncTest, QueueSubmit2RejectsUnsignaledBinarySemaphore) {
  // Mirrors `SubmitRejectsUnsignaledBinarySemaphore` above through
  // `vkQueueSubmit2`'s `VkSemaphoreSubmitInfo` shape.
  VkSemaphoreCreateInfo SemInfo{};
  VkSemaphore Sem = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSemaphore(Device, &SemInfo, nullptr, &Sem), VK_SUCCESS);

  VkSemaphoreSubmitInfo WaitInfo{};
  WaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  WaitInfo.semaphore = Sem;
  VkCommandBufferSubmitInfo CmdBufInfo{};
  CmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
  CmdBufInfo.commandBuffer = CmdBuf;
  VkSubmitInfo2 Submit{};
  Submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  Submit.waitSemaphoreInfoCount = 1;
  Submit.pWaitSemaphoreInfos = &WaitInfo;
  Submit.commandBufferInfoCount = 1;
  Submit.pCommandBufferInfos = &CmdBufInfo;
  EXPECT_EQ(vkQueueSubmit2(Queue, 1, &Submit, VK_NULL_HANDLE),
            VK_ERROR_INITIALIZATION_FAILED);

  vkDestroySemaphore(Device, Sem, nullptr);
}

TEST_F(SyncTest, QueueSubmit2BinarySemaphoreSignalThenWaitSucceeds) {
  VkSemaphoreCreateInfo SemInfo{};
  VkSemaphore Sem = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSemaphore(Device, &SemInfo, nullptr, &Sem), VK_SUCCESS);

  VkCommandBufferSubmitInfo CmdBufInfo{};
  CmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
  CmdBufInfo.commandBuffer = CmdBuf;
  VkSemaphoreSubmitInfo SignalInfo{};
  SignalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  SignalInfo.semaphore = Sem;
  VkSubmitInfo2 Signal{};
  Signal.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  Signal.commandBufferInfoCount = 1;
  Signal.pCommandBufferInfos = &CmdBufInfo;
  Signal.signalSemaphoreInfoCount = 1;
  Signal.pSignalSemaphoreInfos = &SignalInfo;
  ASSERT_EQ(vkQueueSubmit2(Queue, 1, &Signal, VK_NULL_HANDLE), VK_SUCCESS);

  VkSemaphoreSubmitInfo WaitInfo{};
  WaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  WaitInfo.semaphore = Sem;
  VkSubmitInfo2 Wait{};
  Wait.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  Wait.waitSemaphoreInfoCount = 1;
  Wait.pWaitSemaphoreInfos = &WaitInfo;
  Wait.commandBufferInfoCount = 1;
  Wait.pCommandBufferInfos = &CmdBufInfo;
  EXPECT_EQ(vkQueueSubmit2(Queue, 1, &Wait, VK_NULL_HANDLE), VK_SUCCESS);

  // Consumed by the wait above: a second wait with nothing signaling it
  // again must fail, exactly like `vkQueueSubmit`'s own test.
  EXPECT_EQ(vkQueueSubmit2(Queue, 1, &Wait, VK_NULL_HANDLE),
            VK_ERROR_INITIALIZATION_FAILED);

  vkDestroySemaphore(Device, Sem, nullptr);
}

TEST_F(SyncTest, QueueSubmit2TimelineSemaphoreSignalThenWait) {
  // `VkSemaphoreSubmitInfo::value` unifies `vkQueueSubmit`'s split
  // `VkSubmitInfo`/`VkTimelineSemaphoreSubmitInfo` shape into one field,
  // used here for both the signal and the wait.
  VkSemaphoreTypeCreateInfo TypeInfo{};
  TypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
  TypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  VkSemaphoreCreateInfo SemInfo{};
  SemInfo.pNext = &TypeInfo;
  VkSemaphore Sem = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSemaphore(Device, &SemInfo, nullptr, &Sem), VK_SUCCESS);

  VkCommandBufferSubmitInfo CmdBufInfo{};
  CmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
  CmdBufInfo.commandBuffer = CmdBuf;
  VkSemaphoreSubmitInfo SignalInfo{};
  SignalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  SignalInfo.semaphore = Sem;
  SignalInfo.value = 3;
  VkSubmitInfo2 Signal{};
  Signal.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  Signal.commandBufferInfoCount = 1;
  Signal.pCommandBufferInfos = &CmdBufInfo;
  Signal.signalSemaphoreInfoCount = 1;
  Signal.pSignalSemaphoreInfos = &SignalInfo;
  ASSERT_EQ(vkQueueSubmit2(Queue, 1, &Signal, VK_NULL_HANDLE), VK_SUCCESS);

  uint64_t Value = 0;
  ASSERT_EQ(vkGetSemaphoreCounterValue(Device, Sem, &Value), VK_SUCCESS);
  EXPECT_EQ(Value, 3u);

  VkSemaphoreSubmitInfo WaitInfo{};
  WaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  WaitInfo.semaphore = Sem;
  WaitInfo.value = 3;
  VkSubmitInfo2 Wait{};
  Wait.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  Wait.waitSemaphoreInfoCount = 1;
  Wait.pWaitSemaphoreInfos = &WaitInfo;
  Wait.commandBufferInfoCount = 1;
  Wait.pCommandBufferInfos = &CmdBufInfo;
  EXPECT_EQ(vkQueueSubmit2(Queue, 1, &Wait, VK_NULL_HANDLE), VK_SUCCESS);

  // Not yet reached: the semaphore is still at 3.
  WaitInfo.value = 4;
  EXPECT_EQ(vkQueueSubmit2(Queue, 1, &Wait, VK_NULL_HANDLE),
            VK_ERROR_INITIALIZATION_FAILED);

  vkDestroySemaphore(Device, Sem, nullptr);
}

TEST_F(SyncTest, HostEventSetResetStatus) {
  VkEventCreateInfo EventInfo{};
  VkEvent Ev = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateEvent(Device, &EventInfo, nullptr, &Ev), VK_SUCCESS);
  EXPECT_EQ(vkGetEventStatus(Device, Ev), VK_EVENT_RESET);

  ASSERT_EQ(vkSetEvent(Device, Ev), VK_SUCCESS);
  EXPECT_EQ(vkGetEventStatus(Device, Ev), VK_EVENT_SET);

  ASSERT_EQ(vkResetEvent(Device, Ev), VK_SUCCESS);
  EXPECT_EQ(vkGetEventStatus(Device, Ev), VK_EVENT_RESET);

  vkDestroyEvent(Device, Ev, nullptr);
}

TEST_F(SyncTest, CommandBufferSetEventThenWaitSucceeds) {
  VkEventCreateInfo EventInfo{};
  VkEvent Ev = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateEvent(Device, &EventInfo, nullptr, &Ev), VK_SUCCESS);

  VkCommandBufferAllocateInfo AllocInfo{};
  AllocInfo.commandPool = Pool;
  AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  AllocInfo.commandBufferCount = 1;
  VkCommandBuffer SetCmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &AllocInfo, &SetCmdBuf),
            VK_SUCCESS);
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(SetCmdBuf, &BeginInfo);
  vkCmdSetEvent(SetCmdBuf, Ev, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
  vkEndCommandBuffer(SetCmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*fromHandle<CommandBuffer>(SetCmdBuf)),
                    llvm::Succeeded());
  EXPECT_EQ(vkGetEventStatus(Device, Ev), VK_EVENT_SET);

  VkCommandBuffer WaitCmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &AllocInfo, &WaitCmdBuf),
            VK_SUCCESS);
  vkBeginCommandBuffer(WaitCmdBuf, &BeginInfo);
  vkCmdWaitEvents(WaitCmdBuf, 1, &Ev, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, nullptr, 0, nullptr,
                  0, nullptr);
  vkEndCommandBuffer(WaitCmdBuf);
  EXPECT_THAT_ERROR(
      executeCommandBuffer(*fromHandle<CommandBuffer>(WaitCmdBuf)),
      llvm::Succeeded());

  vkDestroyEvent(Device, Ev, nullptr);
}

TEST_F(SyncTest, CommandBufferWaitEventsFailsWhenUnsignaled) {
  VkEventCreateInfo EventInfo{};
  VkEvent Ev = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateEvent(Device, &EventInfo, nullptr, &Ev), VK_SUCCESS);

  VkCommandBufferAllocateInfo AllocInfo{};
  AllocInfo.commandPool = Pool;
  AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  AllocInfo.commandBufferCount = 1;
  VkCommandBuffer WaitCmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &AllocInfo, &WaitCmdBuf),
            VK_SUCCESS);
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(WaitCmdBuf, &BeginInfo);
  vkCmdWaitEvents(WaitCmdBuf, 1, &Ev, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, nullptr, 0, nullptr,
                  0, nullptr);
  vkEndCommandBuffer(WaitCmdBuf);
  EXPECT_THAT_ERROR(
      executeCommandBuffer(*fromHandle<CommandBuffer>(WaitCmdBuf)),
      llvm::Failed());

  vkDestroyEvent(Device, Ev, nullptr);
}

// Roadmap E3: mirrors `CommandBufferSetEventThenWaitSucceeds` above through
// `vkCmdSetEvent2`/`vkCmdWaitEvents2`'s `VkDependencyInfo` shape (empty, so
// the barrier arrays it could carry are irrelevant here) and
// `vkCmdResetEvent2`'s 2-stage-mask.
TEST_F(SyncTest, CommandBufferSetEvent2ThenWaitEvents2Succeeds) {
  VkEventCreateInfo EventInfo{};
  VkEvent Ev = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateEvent(Device, &EventInfo, nullptr, &Ev), VK_SUCCESS);

  VkCommandBufferAllocateInfo AllocInfo{};
  AllocInfo.commandPool = Pool;
  AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  AllocInfo.commandBufferCount = 1;
  VkCommandBuffer SetCmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &AllocInfo, &SetCmdBuf),
            VK_SUCCESS);
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(SetCmdBuf, &BeginInfo);
  VkDependencyInfo DepInfo{};
  DepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  vkCmdSetEvent2(SetCmdBuf, Ev, &DepInfo);
  vkEndCommandBuffer(SetCmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*fromHandle<CommandBuffer>(SetCmdBuf)),
                    llvm::Succeeded());
  EXPECT_EQ(vkGetEventStatus(Device, Ev), VK_EVENT_SET);

  VkCommandBuffer ResetCmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &AllocInfo, &ResetCmdBuf),
            VK_SUCCESS);
  vkBeginCommandBuffer(ResetCmdBuf, &BeginInfo);
  vkCmdResetEvent2(ResetCmdBuf, Ev, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
  vkEndCommandBuffer(ResetCmdBuf);
  ASSERT_THAT_ERROR(
      executeCommandBuffer(*fromHandle<CommandBuffer>(ResetCmdBuf)),
      llvm::Succeeded());
  EXPECT_EQ(vkGetEventStatus(Device, Ev), VK_EVENT_RESET);

  ASSERT_EQ(vkSetEvent(Device, Ev), VK_SUCCESS);
  VkCommandBuffer WaitCmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &AllocInfo, &WaitCmdBuf),
            VK_SUCCESS);
  vkBeginCommandBuffer(WaitCmdBuf, &BeginInfo);
  vkCmdWaitEvents2(WaitCmdBuf, 1, &Ev, &DepInfo);
  vkEndCommandBuffer(WaitCmdBuf);
  EXPECT_THAT_ERROR(
      executeCommandBuffer(*fromHandle<CommandBuffer>(WaitCmdBuf)),
      llvm::Succeeded());

  vkDestroyEvent(Device, Ev, nullptr);
}

TEST_F(SyncTest, CommandBufferWaitEvents2FailsWhenUnsignaled) {
  VkEventCreateInfo EventInfo{};
  VkEvent Ev = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateEvent(Device, &EventInfo, nullptr, &Ev), VK_SUCCESS);

  VkCommandBufferAllocateInfo AllocInfo{};
  AllocInfo.commandPool = Pool;
  AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  AllocInfo.commandBufferCount = 1;
  VkCommandBuffer WaitCmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &AllocInfo, &WaitCmdBuf),
            VK_SUCCESS);
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(WaitCmdBuf, &BeginInfo);
  VkDependencyInfo DepInfo{};
  DepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  vkCmdWaitEvents2(WaitCmdBuf, 1, &Ev, &DepInfo);
  vkEndCommandBuffer(WaitCmdBuf);
  EXPECT_THAT_ERROR(
      executeCommandBuffer(*fromHandle<CommandBuffer>(WaitCmdBuf)),
      llvm::Failed());

  vkDestroyEvent(Device, Ev, nullptr);
}

} // namespace
