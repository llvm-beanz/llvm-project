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

#include <chrono>
#include <thread>

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
    VkDeviceQueueCreateInfo QueueInfos[2]{};
    QueueInfos[0].queueFamilyIndex = 0;
    QueueInfos[0].queueCount = 1;
    QueueInfos[0].pQueuePriorities = &Priority;
    // (Roadmap L228(h)/(i)) A second queue, on the dedicated-compute
    // family (`PhysicalDeviceInfo.cpp`'s family index 2), purely so
    // cross-queue tests below have two genuinely independent
    // `QueueExecutor` worker threads to hand a dependency between,
    // mirroring the CTS `one_to_n`/`wait_before_signal` shape this
    // change fixes. Existing single-queue tests are unaffected: they
    // only ever touch `Queue` (family 0), exactly as before.
    QueueInfos[1].queueFamilyIndex = 2;
    QueueInfos[1].queueCount = 1;
    QueueInfos[1].pQueuePriorities = &Priority;
    VkDeviceCreateInfo DevInfo{};
    DevInfo.queueCreateInfoCount = 2;
    DevInfo.pQueueCreateInfos = QueueInfos;
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);
    vkGetDeviceQueue(Device, 0, 0, &Queue);
    vkGetDeviceQueue(Device, 2, 0, &QueueB);

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
    VkCommandPoolCreateInfo PoolBInfo{};
    PoolBInfo.queueFamilyIndex = 2;
    ASSERT_EQ(vkCreateCommandPool(Device, &PoolBInfo, nullptr, &PoolB),
              VK_SUCCESS);

    VkCommandBufferAllocateInfo AllocInfo{};
    AllocInfo.commandPool = Pool;
    AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    AllocInfo.commandBufferCount = 1;
    ASSERT_EQ(vkAllocateCommandBuffers(Device, &AllocInfo, &CmdBuf),
              VK_SUCCESS);
    VkCommandBufferAllocateInfo AllocBInfo{};
    AllocBInfo.commandPool = PoolB;
    AllocBInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    AllocBInfo.commandBufferCount = 1;
    ASSERT_EQ(vkAllocateCommandBuffers(Device, &AllocBInfo, &CmdBufB),
              VK_SUCCESS);

    VkCommandBufferBeginInfo BeginInfo{};
    ASSERT_EQ(vkBeginCommandBuffer(CmdBuf, &BeginInfo), VK_SUCCESS);
    vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
    vkCmdDispatch(CmdBuf, 2, 2, 2);
    ASSERT_EQ(vkEndCommandBuffer(CmdBuf), VK_SUCCESS);

    ASSERT_EQ(vkBeginCommandBuffer(CmdBufB, &BeginInfo), VK_SUCCESS);
    vkCmdBindPipeline(CmdBufB, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
    vkCmdDispatch(CmdBufB, 2, 2, 2);
    ASSERT_EQ(vkEndCommandBuffer(CmdBufB), VK_SUCCESS);
  }
  void TearDown() override {
    vkDestroyCommandPool(Device, Pool, nullptr);
    vkDestroyCommandPool(Device, PoolB, nullptr);
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
  // (Roadmap L228(h)/(i)) A second, independent queue -- see its own
  // `SetUp` comment above.
  VkQueue QueueB = VK_NULL_HANDLE;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  VkShaderModule Module = VK_NULL_HANDLE;
  VkPipeline Pipeline = VK_NULL_HANDLE;
  VkCommandPool Pool = VK_NULL_HANDLE;
  VkCommandPool PoolB = VK_NULL_HANDLE;
  VkCommandBuffer CmdBuf = VK_NULL_HANDLE;
  VkCommandBuffer CmdBufB = VK_NULL_HANDLE;
};

// The V1 milestone's own end-to-end scenario: submit a recorded empty
// compute dispatch to a queue, and observe its fence signal. (Roadmap
// L228(h)/(i): `vkQueueSubmit` now only enqueues the work -- `Fence`
// status is only guaranteed resolved after an explicit
// `vkWaitForFences`/`vkQueueWaitIdle`, not immediately after
// `vkQueueSubmit` returns.)
TEST_F(SyncTest, SubmitDispatchAndWaitOnFence) {
  VkFenceCreateInfo FenceInfo{};
  VkFence Fence = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFence(Device, &FenceInfo, nullptr, &Fence), VK_SUCCESS);
  EXPECT_EQ(vkGetFenceStatus(Device, Fence), VK_NOT_READY);

  VkSubmitInfo Submit{};
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &CmdBuf;
  ASSERT_EQ(vkQueueSubmit(Queue, 1, &Submit, Fence), VK_SUCCESS);

  EXPECT_EQ(vkWaitForFences(Device, 1, &Fence, VK_TRUE, UINT64_MAX),
            VK_SUCCESS);
  EXPECT_EQ(vkGetFenceStatus(Device, Fence), VK_SUCCESS);
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
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);
}

// Roadmap L228(h)/(i): `vkQueueSubmit` must return promptly even when its
// own wait can never be satisfied (an unsignaled binary semaphore, with
// nothing else in the test left to signal it) -- the whole point of this
// change is that it no longer blocks the calling thread inline. The
// failure this used to report synchronously is instead only observable
// later, once the queue's own worker thread actually reaches (and gives
// up on) that wait: `vkQueueWaitIdle` surfaces it as `VK_ERROR_DEVICE_LOST`
// (`Device::markLost`, "Device loss is latched once" in
// "Queues, Scheduling, and Synchronization"), not the old
// `VK_ERROR_INITIALIZATION_FAILED` return from `vkQueueSubmit` itself.
// This test genuinely waits out `Sync.h`'s own `SafetyNetTimeoutNs`
// safety net (several seconds), matching this file's existing precedent
// of real wall-clock delays to make blocking behavior observable
// (`TimelineSemaphoreWaitBlocksUntilHostSignal` et al.), just a larger
// one -- there being no future signal to wait for is exactly the
// scenario that safety net exists to bound.
TEST_F(SyncTest, SubmitEnqueuesPromptlyThenLatchesDeviceLostOnUnmetBinaryWait) {
  VkSemaphoreCreateInfo SemInfo{};
  VkSemaphore Sem = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSemaphore(Device, &SemInfo, nullptr, &Sem), VK_SUCCESS);

  VkSubmitInfo Submit{};
  Submit.waitSemaphoreCount = 1;
  Submit.pWaitSemaphores = &Sem;
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &CmdBuf;

  auto Start = std::chrono::steady_clock::now();
  EXPECT_EQ(vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE), VK_SUCCESS);
  auto Elapsed = std::chrono::steady_clock::now() - Start;
  // The defining behavioral change: this must return near-instantly, not
  // block for anywhere close to the eventual safety-net timeout.
  EXPECT_LT(Elapsed, std::chrono::seconds(1));

  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_ERROR_DEVICE_LOST);
  // Latched: every subsequent queue/device operation keeps reporting it.
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_ERROR_DEVICE_LOST);
  EXPECT_EQ(vkDeviceWaitIdle(Device), VK_ERROR_DEVICE_LOST);

  vkDestroySemaphore(Device, Sem, nullptr);
}

// Roadmap L234: `vkWaitForFences`'s `waitAll == VK_TRUE` path used to
// check `Device::isLost()` only once, before starting to block on a
// fence -- so if the device were latched lost *during* that block, this
// call would still block for the fence's own full remaining timeout
// (which the lost device's fence-signal task now deliberately skips
// satisfying, `makeFenceSignalTask`) before reporting a misleading
// `VK_TIMEOUT` -- rather than `VK_ERROR_DEVICE_LOST`, promptly, once the
// loss actually happened. Marks the device lost directly (`Objects.h`'s
// `fromHandle<Device>`, already included by this file) from a background
// thread partway through a `vkWaitForFences(..., UINT64_MAX)` call,
// rather than via a genuinely-unmet wait racing `Sync.h`'s own
// `SafetyNetTimeoutNs` (which -- since both that internal wait and this
// call's own timeout clamp to the very same constant -- resolves their
// relative order by incidental scheduling, not this fix, and so cannot
// deterministically exercise the mid-wait-detection path this test means
// to cover).
TEST_F(SyncTest, WaitForFencesReportsDeviceLostPromptlyNotAfterFullTimeout) {
  VkFenceCreateInfo FenceInfo{};
  VkFence Fence = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFence(Device, &FenceInfo, nullptr, &Fence), VK_SUCCESS);

  std::thread MarkLost([this] {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    fromHandle<feme::vulkan::Device>(Device)->markLost();
  });

  auto Start = std::chrono::steady_clock::now();
  EXPECT_EQ(vkWaitForFences(Device, 1, &Fence, VK_TRUE, UINT64_MAX),
            VK_ERROR_DEVICE_LOST);
  auto Elapsed = std::chrono::steady_clock::now() - Start;
  // Well under `Sync.h`'s `SafetyNetTimeoutNs` (5s) -- just the ~200ms
  // delay above plus at most one `DeviceLostPollSliceNs` (50ms) more for
  // this call's own polling loop to notice.
  EXPECT_LT(Elapsed, std::chrono::seconds(1));

  MarkLost.join();
  vkDestroyFence(Device, Fence, nullptr);
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
  // Same queue: FIFO order (one `QueueExecutor` worker thread) guarantees
  // the signal task above always runs before this wait task, exactly
  // like the old synchronous model's program-order guarantee -- roadmap
  // L228(h)/(i) changes nothing observable for a single queue.
  EXPECT_EQ(vkQueueSubmit(Queue, 1, &Wait, VK_NULL_HANDLE), VK_SUCCESS);
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);

  vkDestroySemaphore(Device, Sem, nullptr);
}

// Roadmap L228(h)/(i): a binary semaphore is consumed by a wait --
// resubmitting a second wait with nothing signaling it again in between
// must eventually latch device loss, the same as
// `SubmitEnqueuesPromptlyThenLatchesDeviceLostOnUnmetBinaryWait` above
// (and, like that test, genuinely waits out the safety-net timeout).
TEST_F(SyncTest, BinarySemaphoreSecondWaitWithoutNewSignalLatchesDeviceLost) {
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
  ASSERT_EQ(vkQueueSubmit(Queue, 1, &Wait, VK_NULL_HANDLE), VK_SUCCESS);
  ASSERT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);

  // Nothing signals `Sem` again: this second wait can never be satisfied.
  EXPECT_EQ(vkQueueSubmit(Queue, 1, &Wait, VK_NULL_HANDLE), VK_SUCCESS);
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_ERROR_DEVICE_LOST);

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
  // (Roadmap L228(h)/(i)) The signal only happens once the queue's own
  // worker thread actually runs this submission's task -- wait for the
  // queue to drain before reading the counter, rather than assuming it
  // is already resolved immediately after `vkQueueSubmit` returns.
  ASSERT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);

  uint64_t Value = 0;
  ASSERT_EQ(vkGetSemaphoreCounterValue(Device, Sem, &Value), VK_SUCCESS);
  EXPECT_EQ(Value, SignalValue);

  vkDestroySemaphore(Device, Sem, nullptr);
}

// Roadmap L228(a): `vkWaitSemaphores` must genuinely block a real host
// thread until a *different* real host thread's own, later
// `vkSignalSemaphore` call reaches the awaited value -- not merely
// succeed if the value already happens to be met at the moment of the
// call (`TimelineSemaphoreHostSignalAndWait` above only covers the
// same-thread, already-resolved case). This mirrors
// `dEQP-VK.synchronization.timeline_semaphore.device_host.*`'s own
// `HostCopyThread`, which relies on exactly this real cross-thread
// blocking.
TEST_F(SyncTest, TimelineSemaphoreWaitBlocksUntilHostSignal) {
  VkSemaphoreTypeCreateInfo TypeInfo{};
  TypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
  TypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  TypeInfo.initialValue = 0;
  VkSemaphoreCreateInfo SemInfo{};
  SemInfo.pNext = &TypeInfo;
  VkSemaphore Sem = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSemaphore(Device, &SemInfo, nullptr, &Sem), VK_SUCCESS);

  constexpr auto SignalDelay = std::chrono::milliseconds(200);
  std::thread Signaler([&] {
    std::this_thread::sleep_for(SignalDelay);
    VkSemaphoreSignalInfo SignalInfo{};
    SignalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    SignalInfo.semaphore = Sem;
    SignalInfo.value = 1;
    EXPECT_EQ(vkSignalSemaphore(Device, &SignalInfo), VK_SUCCESS);
  });

  uint64_t WaitValue = 1;
  VkSemaphoreWaitInfo WaitInfo{};
  WaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
  WaitInfo.semaphoreCount = 1;
  WaitInfo.pSemaphores = &Sem;
  WaitInfo.pValues = &WaitValue;

  auto Start = std::chrono::steady_clock::now();
  EXPECT_EQ(vkWaitSemaphores(Device, &WaitInfo, UINT64_MAX), VK_SUCCESS);
  auto Elapsed = std::chrono::steady_clock::now() - Start;
  // A non-blocking (pre-fix) implementation would return instantly,
  // failing this check well before `SignalDelay` elapses.
  EXPECT_GE(Elapsed, SignalDelay);

  Signaler.join();
  vkDestroySemaphore(Device, Sem, nullptr);
}

// Roadmap L228(h)/(i): the defining regression test for this change.
// `vkQueueSubmit`'s own implicit wait on an unmet timeline semaphore must
// now return *promptly* -- the opposite of what this test (previously
// named `QueueSubmitBlocksUntilHostSignalsTimelineSemaphore`, added for
// roadmap L228(a)) used to assert. Blocking the calling thread here is
// exactly the bug this change fixes: it is precisely what made a
// `one_to_n`/`wait_before_signal`-shaped submission chain
// (`TimelineSemaphoreCrossQueueSubmitChainCompletesAfterHostSignal`
// below) deadlock, since the *real* releasing signal a real application
// would send from this same calling thread could then never be reached.
// The actual dependency is still genuinely honored, just off the calling
// thread: the fence this submission signals only becomes ready once the
// background `QueueExecutor` worker thread's own wait step observes the
// `Signaler` thread's later `vkSignalSemaphore` call.
TEST_F(SyncTest, QueueSubmitReturnsPromptlyThenCompletesAfterHostSignal) {
  VkSemaphoreTypeCreateInfo TypeInfo{};
  TypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
  TypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  TypeInfo.initialValue = 0;
  VkSemaphoreCreateInfo SemInfo{};
  SemInfo.pNext = &TypeInfo;
  VkSemaphore Sem = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSemaphore(Device, &SemInfo, nullptr, &Sem), VK_SUCCESS);

  constexpr auto SignalDelay = std::chrono::milliseconds(200);
  std::thread Signaler([&] {
    std::this_thread::sleep_for(SignalDelay);
    VkSemaphoreSignalInfo SignalInfo{};
    SignalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    SignalInfo.semaphore = Sem;
    SignalInfo.value = 1;
    EXPECT_EQ(vkSignalSemaphore(Device, &SignalInfo), VK_SUCCESS);
  });

  uint64_t WaitValue = 1;
  VkTimelineSemaphoreSubmitInfo TimelineInfo{};
  TimelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
  TimelineInfo.waitSemaphoreValueCount = 1;
  TimelineInfo.pWaitSemaphoreValues = &WaitValue;
  VkSubmitInfo Submit{};
  Submit.pNext = &TimelineInfo;
  Submit.waitSemaphoreCount = 1;
  Submit.pWaitSemaphores = &Sem;
  VkPipelineStageFlags WaitStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  Submit.pWaitDstStageMask = &WaitStage;
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &CmdBuf;

  VkFenceCreateInfo FenceInfo{};
  VkFence Fence = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFence(Device, &FenceInfo, nullptr, &Fence), VK_SUCCESS);

  auto Start = std::chrono::steady_clock::now();
  EXPECT_EQ(vkQueueSubmit(Queue, 1, &Submit, Fence), VK_SUCCESS);
  auto SubmitElapsed = std::chrono::steady_clock::now() - Start;
  // The core assertion: returns near-instantly, well before `Signaler`
  // ever signals -- not blocked in-call the way it used to be.
  EXPECT_LT(SubmitElapsed, SignalDelay);

  // The dependency is still honored, just observed later: the fence only
  // becomes signaled once the background worker thread's own wait
  // actually resolves.
  EXPECT_EQ(vkWaitForFences(Device, 1, &Fence, VK_TRUE, UINT64_MAX),
            VK_SUCCESS);
  auto TotalElapsed = std::chrono::steady_clock::now() - Start;
  EXPECT_GE(TotalElapsed, SignalDelay);

  Signaler.join();
  vkDestroyFence(Device, Fence, nullptr);
  vkDestroySemaphore(Device, Sem, nullptr);
}

// Roadmap L228(h)/(i): the actual CTS-motivating shape
// (`dEQP-VK.synchronization*.timeline_semaphore.{one_to_n,
// wait_before_signal}`) -- a whole dependent submission *chain* is
// submitted up front, across two different queues (so two independent
// `QueueExecutor` worker threads), and only released by one later host
// signal from this same calling thread. Under the old, synchronous
// `vkQueueSubmit` this deadlocked outright: the first submission's own
// blocking wait, executed inline, meant this thread could never reach
// the `vkSignalSemaphore` call below that would have satisfied it.
TEST_F(SyncTest, TimelineSemaphoreCrossQueueSubmitChainCompletesAfterHostSignal) {
  VkSemaphoreTypeCreateInfo TypeInfo{};
  TypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
  TypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  TypeInfo.initialValue = 0;
  VkSemaphoreCreateInfo SemInfo{};
  SemInfo.pNext = &TypeInfo;
  VkSemaphore Sem = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSemaphore(Device, &SemInfo, nullptr, &Sem), VK_SUCCESS);

  // Submission 1, on `Queue`: waits for the host to signal `Sem` to 1
  // (the release this whole chain is blocked on), then signals `Sem` to
  // 2 for submission 2 (on the *other* queue, `QueueB`) to consume.
  uint64_t Wait1 = 1, Signal1 = 2;
  VkTimelineSemaphoreSubmitInfo TimelineInfo1{};
  TimelineInfo1.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
  TimelineInfo1.waitSemaphoreValueCount = 1;
  TimelineInfo1.pWaitSemaphoreValues = &Wait1;
  TimelineInfo1.signalSemaphoreValueCount = 1;
  TimelineInfo1.pSignalSemaphoreValues = &Signal1;
  VkPipelineStageFlags WaitStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkSubmitInfo Submit1{};
  Submit1.pNext = &TimelineInfo1;
  Submit1.waitSemaphoreCount = 1;
  Submit1.pWaitSemaphores = &Sem;
  Submit1.pWaitDstStageMask = &WaitStage;
  Submit1.signalSemaphoreCount = 1;
  Submit1.pSignalSemaphores = &Sem;
  Submit1.commandBufferCount = 1;
  Submit1.pCommandBuffers = &CmdBuf;

  // Submission 2, on `QueueB`: waits for `Sem` to reach 2 (submission 1's
  // own signal above), then signals it to 3, which this test's own final
  // `vkWaitSemaphores` call below observes.
  uint64_t Wait2 = 2, Signal2 = 3;
  VkTimelineSemaphoreSubmitInfo TimelineInfo2{};
  TimelineInfo2.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
  TimelineInfo2.waitSemaphoreValueCount = 1;
  TimelineInfo2.pWaitSemaphoreValues = &Wait2;
  TimelineInfo2.signalSemaphoreValueCount = 1;
  TimelineInfo2.pSignalSemaphoreValues = &Signal2;
  VkSubmitInfo Submit2{};
  Submit2.pNext = &TimelineInfo2;
  Submit2.waitSemaphoreCount = 1;
  Submit2.pWaitSemaphores = &Sem;
  Submit2.pWaitDstStageMask = &WaitStage;
  Submit2.signalSemaphoreCount = 1;
  Submit2.pSignalSemaphores = &Sem;
  Submit2.commandBufferCount = 1;
  Submit2.pCommandBuffers = &CmdBufB;

  // Both submitted before any host signal exists at all: a synchronous
  // `vkQueueSubmit` could never return from the first of these two calls.
  auto Start = std::chrono::steady_clock::now();
  ASSERT_EQ(vkQueueSubmit(Queue, 1, &Submit1, VK_NULL_HANDLE), VK_SUCCESS);
  ASSERT_EQ(vkQueueSubmit(QueueB, 1, &Submit2, VK_NULL_HANDLE), VK_SUCCESS);
  auto SubmitElapsed = std::chrono::steady_clock::now() - Start;
  EXPECT_LT(SubmitElapsed, std::chrono::seconds(1));

  // *Now* the one release signal the whole chain was waiting on.
  VkSemaphoreSignalInfo SignalInfo{};
  SignalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
  SignalInfo.semaphore = Sem;
  SignalInfo.value = 1;
  ASSERT_EQ(vkSignalSemaphore(Device, &SignalInfo), VK_SUCCESS);

  uint64_t FinalValue = 3;
  VkSemaphoreWaitInfo WaitInfo{};
  WaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
  WaitInfo.semaphoreCount = 1;
  WaitInfo.pSemaphores = &Sem;
  WaitInfo.pValues = &FinalValue;
  EXPECT_EQ(vkWaitSemaphores(Device, &WaitInfo, UINT64_MAX), VK_SUCCESS);

  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);
  EXPECT_EQ(vkQueueWaitIdle(QueueB), VK_SUCCESS);

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

  EXPECT_EQ(vkWaitForFences(Device, 1, &Fence, VK_TRUE, UINT64_MAX),
            VK_SUCCESS);
  EXPECT_EQ(vkGetFenceStatus(Device, Fence), VK_SUCCESS);

  vkDestroyFence(Device, Fence, nullptr);
}

// Roadmap L228(h)/(i): mirrors
// `SubmitEnqueuesPromptlyThenLatchesDeviceLostOnUnmetBinaryWait` above
// through `vkQueueSubmit2`'s `VkSemaphoreSubmitInfo` shape -- genuinely
// waits out the safety-net timeout, same as that test.
TEST_F(SyncTest, QueueSubmit2EnqueuesPromptlyThenLatchesDeviceLostOnUnmetBinaryWait) {
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
  EXPECT_EQ(vkQueueSubmit2(Queue, 1, &Submit, VK_NULL_HANDLE), VK_SUCCESS);
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_ERROR_DEVICE_LOST);

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
  // again can never be satisfied, so (roadmap L228(h)/(i)) it eventually
  // latches device loss instead of the old immediate failure return --
  // exactly like `vkQueueSubmit`'s own
  // `BinarySemaphoreSecondWaitWithoutNewSignalLatchesDeviceLost` test
  // (also genuinely waits out the safety-net timeout).
  EXPECT_EQ(vkQueueSubmit2(Queue, 1, &Wait, VK_NULL_HANDLE), VK_SUCCESS);
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_ERROR_DEVICE_LOST);

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
  ASSERT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);

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

  // Not yet reached: the semaphore is still at 3, and nothing will ever
  // signal it to 4 again -- roadmap L228(h)/(i) eventually latches device
  // loss instead of the old immediate failure return (genuinely waits
  // out the safety-net timeout, same as the binary-semaphore test above).
  WaitInfo.value = 4;
  EXPECT_EQ(vkQueueSubmit2(Queue, 1, &Wait, VK_NULL_HANDLE), VK_SUCCESS);
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_ERROR_DEVICE_LOST);

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
